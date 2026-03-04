#include "engine_v2.hpp"

#include "csv_reader.hpp"
#include "metrics.hpp"
#include "portfolio.hpp"
#include "rolling_stats.hpp"
#include "tick_logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

enum class SpikeKind { kNone, kUp, kDown };

struct SpikeCandidate {
  bool active{false};
  std::size_t tick_index{0};
  SpikeKind kind{SpikeKind::kNone};
  double spike_price{0.0};
  double spike_volume{0.0};
  bool saw_initial_reversion{false};
};

struct PositionPlan {
  int initial_qty{0};
  int stage_index{0};
  std::vector<double> thresholds{0.5, 1.0, 1.5};
};

struct EquityPoint {
  std::string timestamp;
  double equity{0.0};
};

struct RoundTripMetrics {
  int trade_count{0};
  int win_count{0};
  double total_pnl{0.0};
};

struct V2Params {
  // 1) Execution quality + edge gate.
  double spread_cap_tight{2.0};
  double spread_cap_relaxed{3.0};
  double spike_size_for_relaxed_spread{8.0};
  double fee_buffer_round_trip{1.0};
  double extra_edge_buffer{1.0};

  // 2) Absorption confirmation (multi-tick).
  int confirmation_window_ticks{3};
  double initial_reversion_ticks{1.0};
  double retrace_ticks_normal{2.0};
  double retrace_ticks_near_boundary{3.0};
  double volume_decay_normal{0.80};
  double volume_decay_near_boundary{0.70};

  // 3) Boundary-aware entry filter/sizing.
  double boundary_hard_block{5.0};
  double boundary_reduced_size{10.0};
  double reduced_size_mult{0.5};
};

std::string SpikeToString(SpikeKind kind) {
  if (kind == SpikeKind::kUp) {
    return "up";
  }
  if (kind == SpikeKind::kDown) {
    return "down";
  }
  return "none";
}

void EnsureParentDir(const std::string& path) {
  const fs::path p(path);
  const fs::path parent = p.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

bool WriteMetricsJson(const std::string& path,
                      double total_return,
                      double max_drawdown,
                      int trade_count,
                      double win_rate,
                      double avg_trade_pnl,
                      bool include_sharpe,
                      double sharpe_value,
                      std::string* error) {
  EnsureParentDir(path);
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Unable to write metrics.json: " + path;
    }
    return false;
  }
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"total_return\": " << total_return << ",\n";
  out << "  \"max_drawdown\": " << max_drawdown << ",\n";
  out << "  \"trade_count\": " << trade_count << ",\n";
  out << "  \"win_rate\": " << win_rate << ",\n";
  out << "  \"avg_trade_pnl\": " << avg_trade_pnl;
  if (include_sharpe) {
    out << ",\n  \"sharpe\": " << sharpe_value << "\n";
  } else {
    out << "\n";
  }
  out << "}\n";
  return true;
}

int SignOfPosition(int qty) {
  if (qty > 0) {
    return 1;
  }
  if (qty < 0) {
    return -1;
  }
  return 0;
}

RoundTripMetrics ComputeRoundTripMetrics(const std::vector<TradeRecord>& fills) {
  RoundTripMetrics m;
  int running_position = 0;
  bool in_trade = false;
  double current_trade_pnl = 0.0;

  for (const TradeRecord& f : fills) {
    if (!in_trade && running_position == 0 && f.qty != 0) {
      in_trade = true;
      current_trade_pnl = 0.0;
    }

    current_trade_pnl += f.pnl;
    running_position += f.qty;

    if (in_trade && running_position == 0) {
      ++m.trade_count;
      if (current_trade_pnl > 0.0) {
        ++m.win_count;
      }
      m.total_pnl += current_trade_pnl;
      in_trade = false;
      current_trade_pnl = 0.0;
    }
  }

  return m;
}

}  // namespace

int RunBacktestV2(const BacktestConfig& config) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();

  const V2Params v2{};

  fs::create_directories(config.outdir);
  EnsureParentDir(config.log_path);

  TickLogger tick_log(config.log_path);
  if (!tick_log.IsOpen()) {
    std::cerr << "Unable to open log path: " << config.log_path << "\n";
    return 1;
  }
  tick_log.WriteHeader();

  std::ofstream equity_csv((fs::path(config.outdir) / "equity.csv").string(), std::ios::out | std::ios::trunc);
  std::ofstream trades_csv((fs::path(config.outdir) / "trades.csv").string(), std::ios::out | std::ios::trunc);
  if (!equity_csv.is_open() || !trades_csv.is_open()) {
    std::cerr << "Unable to open output files in outdir: " << config.outdir << "\n";
    return 1;
  }
  equity_csv << "timestamp,equity\n";
  trades_csv << "timestamp,action,qty,price,pnl\n";

  RollingStats stats(static_cast<std::size_t>(config.rolling_window));
  Portfolio portfolio(config.initial_cash);
  MaxDrawdownTracker drawdown;
  SharpeTracker sharpe;

  SpikeCandidate candidate;
  PositionPlan position_plan;
  std::size_t tick_index = 0;
  int ticks_in_position = 0;
  double prev_equity = config.initial_cash;

  std::vector<EquityPoint> equity_points;
  equity_points.reserve(200000);

  CsvReadStats csv_stats;
  std::string csv_error;
  const bool ok = StreamTicksFromCsv(
      config.csv_path,
      [&](const Tick& tick) {
        ++tick_index;
        const bool ready_before = stats.IsReady();
        const double mean_before = stats.Mean();
        const double std_before = stats.StdDev();

        SpikeKind spike_flag = SpikeKind::kNone;
        std::string confirm_status = "na";
        std::string action = "none";
        bool entered_this_tick = false;

        if (candidate.active) {
          const std::size_t age = tick_index - candidate.tick_index;
          if (age > static_cast<std::size_t>(v2.confirmation_window_ticks)) {
            candidate.active = false;
            confirm_status = "expired";
          } else {
            const double move_against = candidate.kind == SpikeKind::kUp
                                            ? (candidate.spike_price - tick.price)
                                            : (tick.price - candidate.spike_price);
            if (!candidate.saw_initial_reversion && move_against >= v2.initial_reversion_ticks) {
              candidate.saw_initial_reversion = true;
            }

            if (candidate.saw_initial_reversion && portfolio.PositionQty() == 0) {
              const double d_to_bound = std::min(tick.price, 100.0 - tick.price);
              if (d_to_bound <= v2.boundary_hard_block) {
                confirm_status = "boundary_block";
                candidate.active = false;
              } else {
                const bool no_new_extreme = candidate.kind == SpikeKind::kUp
                                                ? (tick.price <= candidate.spike_price)
                                                : (tick.price >= candidate.spike_price);
                const double retrace_need = d_to_bound <= v2.boundary_reduced_size
                                                ? v2.retrace_ticks_near_boundary
                                                : v2.retrace_ticks_normal;
                const double vol_decay_limit = d_to_bound <= v2.boundary_reduced_size
                                                   ? v2.volume_decay_near_boundary
                                                   : v2.volume_decay_normal;
                const bool volume_decay_ok =
                    candidate.spike_volume <= 0.0 || tick.volume <= vol_decay_limit * candidate.spike_volume;
                const bool retrace_ok = move_against >= retrace_need;
                if (no_new_extreme && volume_decay_ok && retrace_ok) {
                  const double spread = tick.ask - tick.bid;
                  const double mid = (tick.ask + tick.bid) * 0.5;
                  const double spike_size = std::abs(candidate.spike_price - mean_before);
                  const bool spread_ok =
                      spread <= v2.spread_cap_tight ||
                      (spread <= v2.spread_cap_relaxed && spike_size >= v2.spike_size_for_relaxed_spread);
                  const double target = std::min(0.5 * spike_size, std::abs(mid - mean_before));
                  const bool edge_ok = target >= (spread + v2.fee_buffer_round_trip + v2.extra_edge_buffer);

                  if (!spread_ok) {
                    confirm_status = "filtered_spread";
                    candidate.active = false;
                  } else if (!edge_ok) {
                    confirm_status = "filtered_edge";
                    candidate.active = false;
                  } else {
                    int qty = config.position_size;
                    if (d_to_bound <= v2.boundary_reduced_size) {
                      qty = std::max(1, static_cast<int>(std::lround(
                                            static_cast<double>(config.position_size) * v2.reduced_size_mult)));
                    }
                    const int entry_qty = candidate.kind == SpikeKind::kUp ? -qty : qty;
                    portfolio.ApplyFill(tick.timestamp, "entry", entry_qty, tick.price, config.fee_per_contract,
                                        config.slippage_points);
                    position_plan.initial_qty = std::abs(entry_qty);
                    position_plan.stage_index = 0;
                    ticks_in_position = 0;
                    action = "enter";
                    entered_this_tick = true;
                    confirm_status = "confirmed";
                    candidate.active = false;
                  }
                } else {
                  confirm_status = "pending_absorption";
                }
              }
            }
          }
        }

        if (!candidate.active && portfolio.PositionQty() == 0 && ready_before && std_before > 0.0) {
          const double upper = mean_before + config.spike_threshold * std_before;
          const double lower = mean_before - config.spike_threshold * std_before;
          if (tick.price > upper) {
            candidate = SpikeCandidate{true, tick_index, SpikeKind::kUp, tick.price, tick.volume, false};
            spike_flag = SpikeKind::kUp;
          } else if (tick.price < lower) {
            candidate = SpikeCandidate{true, tick_index, SpikeKind::kDown, tick.price, tick.volume, false};
            spike_flag = SpikeKind::kDown;
          }
        }

        const int pos_before_risk = portfolio.PositionQty();
        if (pos_before_risk != 0) {
          if (!entered_this_tick) {
            ++ticks_in_position;
          }
          const int sign = SignOfPosition(pos_before_risk);
          const double favorable_move = sign > 0 ? (tick.price - portfolio.AverageEntryPrice())
                                                 : (portfolio.AverageEntryPrice() - tick.price);
          const double adverse_move = -favorable_move;

          if (position_plan.stage_index < static_cast<int>(position_plan.thresholds.size())) {
            const double threshold = position_plan.thresholds[static_cast<std::size_t>(position_plan.stage_index)];
            if (favorable_move >= threshold) {
              int total_abs = std::abs(portfolio.PositionQty());
              int qty_to_exit = position_plan.initial_qty / 3;
              if (qty_to_exit <= 0) {
                qty_to_exit = 1;
              }
              if (position_plan.stage_index == 2) {
                qty_to_exit = total_abs;
              } else {
                qty_to_exit = std::min(qty_to_exit, total_abs);
              }
              if (qty_to_exit > 0) {
                const int signed_exit = sign > 0 ? -qty_to_exit : qty_to_exit;
                portfolio.ApplyFill(tick.timestamp, "exit", signed_exit, tick.price, config.fee_per_contract,
                                    config.slippage_points);
                action = action == "none" ? "gradual_exit" : action + "|gradual_exit";
                ++position_plan.stage_index;
              }
            }
          }

          if (portfolio.PositionQty() != 0 && adverse_move >= config.stop_loss_points) {
            const int signed_exit = -portfolio.PositionQty();
            portfolio.ApplyFill(tick.timestamp, "stop_loss", signed_exit, tick.price, config.fee_per_contract,
                                config.slippage_points);
            action = action == "none" ? "stop_loss" : action + "|stop_loss";
          }

          if (portfolio.PositionQty() != 0 && ticks_in_position >= config.max_hold_ticks) {
            const int signed_exit = -portfolio.PositionQty();
            portfolio.ApplyFill(tick.timestamp, "max_hold", signed_exit, tick.price, config.fee_per_contract,
                                config.slippage_points);
            action = action == "none" ? "max_hold_exit" : action + "|max_hold_exit";
          }

          if (portfolio.PositionQty() == 0) {
            ticks_in_position = 0;
            position_plan.initial_qty = 0;
            position_plan.stage_index = 0;
          }
        }

        stats.Update(tick.price);
        const double unrealized = portfolio.UnrealizedPnl(tick.price);
        const double equity = portfolio.Equity(tick.price);
        drawdown.Update(equity);
        if (config.compute_sharpe && prev_equity != 0.0) {
          sharpe.Update((equity - prev_equity) / prev_equity);
        }
        prev_equity = equity;

        equity_points.push_back(EquityPoint{tick.timestamp, equity});
        equity_csv << tick.timestamp << "," << std::fixed << std::setprecision(6) << equity << "\n";

        tick_log.LogRow(tick.timestamp, tick.price, stats.Mean(), stats.StdDev(), SpikeToString(spike_flag),
                        confirm_status, action, portfolio.PositionQty(), portfolio.Cash(), equity,
                        portfolio.RealizedPnl(), unrealized);
        return true;
      },
      &csv_stats, &csv_error);

  if (!ok) {
    std::cerr << csv_error << "\n";
    return 1;
  }

  const std::vector<TradeRecord>& trades = portfolio.Trades();
  for (const TradeRecord& t : trades) {
    trades_csv << t.timestamp << "," << t.action << "," << t.qty << ","
               << std::fixed << std::setprecision(6) << t.price << "," << t.pnl << "\n";
  }

  const RoundTripMetrics rt = ComputeRoundTripMetrics(trades);

  const double final_equity = equity_points.empty() ? config.initial_cash : equity_points.back().equity;
  const double total_return = config.initial_cash != 0.0 ? (final_equity - config.initial_cash) / config.initial_cash
                                                          : 0.0;
  const int trade_count = rt.trade_count;
  const double win_rate = trade_count > 0 ? static_cast<double>(rt.win_count) / static_cast<double>(trade_count)
                                          : 0.0;
  const double avg_trade_pnl = trade_count > 0 ? rt.total_pnl / static_cast<double>(trade_count) : 0.0;

  std::string metrics_error;
  const bool metrics_ok = WriteMetricsJson((fs::path(config.outdir) / "metrics.json").string(), total_return,
                                           drawdown.MaxDrawdown(), trade_count, win_rate, avg_trade_pnl,
                                           config.compute_sharpe, sharpe.Sharpe(), &metrics_error);
  if (!metrics_ok) {
    std::cerr << metrics_error << "\n";
    return 1;
  }

  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

  std::cout << std::fixed << std::setprecision(6);
  std::cout << "Backtest complete (strategy v2)\n";
  std::cout << "Rows total: " << csv_stats.rows_total << "\n";
  std::cout << "Rows emitted: " << csv_stats.rows_emitted << "\n";
  std::cout << "Rows skipped: " << csv_stats.rows_skipped << "\n";
  std::cout << "Final equity: " << final_equity << "\n";
  std::cout << "Total return: " << total_return << "\n";
  std::cout << "Max drawdown: " << drawdown.MaxDrawdown() << "\n";
  std::cout << "Trade count: " << trade_count << "\n";
  std::cout << "Win rate: " << win_rate << "\n";
  std::cout << "Avg trade pnl: " << avg_trade_pnl << "\n";
  if (config.compute_sharpe) {
    std::cout << "Sharpe: " << sharpe.Sharpe() << "\n";
  }
  std::cout << "Elapsed ms: " << elapsed_ms << "\n";
  std::cout << "Outputs: " << (fs::path(config.outdir) / "equity.csv").string() << ", "
            << (fs::path(config.outdir) / "trades.csv").string() << ", "
            << (fs::path(config.outdir) / "metrics.json").string() << "\n";
  std::cout << "Tick log: " << config.log_path << "\n";

  return 0;
}

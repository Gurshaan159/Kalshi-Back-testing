#include "engine.hpp"

#include "csv_reader.hpp"
#include "metrics.hpp"
#include "portfolio.hpp"
#include "rolling_stats.hpp"
#include "tick_logger.hpp"

#include <chrono>
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

int RunBacktest(const BacktestConfig& config) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();

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

        if (candidate.active && tick_index == candidate.tick_index + 1U) {
          const bool confirmed =
              (candidate.kind == SpikeKind::kUp && tick.price < candidate.spike_price) ||
              (candidate.kind == SpikeKind::kDown && tick.price > candidate.spike_price);
          confirm_status = confirmed ? "confirmed" : "rejected";
          if (confirmed && portfolio.PositionQty() == 0) {
            const int entry_qty = candidate.kind == SpikeKind::kUp ? -config.position_size : config.position_size;
            portfolio.ApplyFill(tick.timestamp, "entry", entry_qty, tick.price, config.fee_per_contract,
                                config.slippage_points);
            position_plan.initial_qty = std::abs(entry_qty);
            position_plan.stage_index = 0;
            ticks_in_position = 0;
            action = "enter";
            entered_this_tick = true;
          }
          candidate.active = false;
        } else if (candidate.active && tick_index > candidate.tick_index + 1U) {
          candidate.active = false;
          confirm_status = "expired";
        }

        if (!candidate.active && portfolio.PositionQty() == 0 && ready_before && std_before > 0.0) {
          const double upper = mean_before + config.spike_threshold * std_before;
          const double lower = mean_before - config.spike_threshold * std_before;
          if (tick.price > upper) {
            candidate = SpikeCandidate{true, tick_index, SpikeKind::kUp, tick.price};
            spike_flag = SpikeKind::kUp;
          } else if (tick.price < lower) {
            candidate = SpikeCandidate{true, tick_index, SpikeKind::kDown, tick.price};
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

          // Gradual exit: 3 chunks at configured thresholds.
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

          // Stop-loss.
          if (portfolio.PositionQty() != 0 && adverse_move >= config.stop_loss_points) {
            const int signed_exit = -portfolio.PositionQty();
            portfolio.ApplyFill(tick.timestamp, "stop_loss", signed_exit, tick.price, config.fee_per_contract,
                                config.slippage_points);
            action = action == "none" ? "stop_loss" : action + "|stop_loss";
          }

          // Max hold.
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
  std::cout << "Backtest complete\n";
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

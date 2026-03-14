#include "engine.hpp"

#include "csv_reader.hpp"
#include "execution.hpp"
#include "metrics.hpp"
#include "portfolio.hpp"
#include "resting_order.hpp"
#include "rolling_stats.hpp"
#include "tick_logger.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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

struct RestingMetrics {
  int signals_generated{0};
  int resting_orders_submitted{0};
  int resting_orders_fully_filled{0};
  int resting_orders_partially_filled{0};
  int resting_orders_expired{0};
  int resting_orders_canceled{0};
  int total_partial_fill_events{0};
  double average_fill_delay_ticks{0.0};
  double average_filled_fraction{0.0};
  int missed_trades_due_to_expiry{0};
  bool enabled{false};
};

bool WriteMetricsJson(const std::string& path,
                      double total_return,
                      double max_drawdown,
                      int trade_count,
                      double win_rate,
                      double avg_trade_pnl,
                      bool include_sharpe,
                      double sharpe_value,
                      const RestingMetrics* resting,
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
    out << ",\n  \"sharpe\": " << sharpe_value;
  }
  if (resting != nullptr && resting->enabled) {
    out << ",\n  \"signals_generated\": " << resting->signals_generated
        << ",\n  \"resting_orders_submitted\": " << resting->resting_orders_submitted
        << ",\n  \"resting_orders_fully_filled\": " << resting->resting_orders_fully_filled
        << ",\n  \"resting_orders_partially_filled\": " << resting->resting_orders_partially_filled
        << ",\n  \"resting_orders_expired\": " << resting->resting_orders_expired
        << ",\n  \"resting_orders_canceled\": " << resting->resting_orders_canceled
        << ",\n  \"total_partial_fill_events\": " << resting->total_partial_fill_events
        << ",\n  \"average_fill_delay_ticks\": " << resting->average_fill_delay_ticks
        << ",\n  \"average_filled_fraction\": " << resting->average_filled_fraction
        << ",\n  \"missed_trades_due_to_expiry\": " << resting->missed_trades_due_to_expiry;
  }
  out << "\n}\n";
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

struct RoundTripAccumulator {
  RoundTripMetrics metrics;
  int running_position = 0;
  bool in_trade = false;
  double current_trade_pnl = 0.0;

  void Update(const TradeRecord& f) {
    if (!in_trade && running_position == 0 && f.qty != 0) {
      in_trade = true;
      current_trade_pnl = 0.0;
    }

    current_trade_pnl += f.pnl;
    running_position += f.qty;

    if (in_trade && running_position == 0) {
      ++metrics.trade_count;
      if (current_trade_pnl > 0.0) {
        ++metrics.win_count;
      }
      metrics.total_pnl += current_trade_pnl;
      in_trade = false;
      current_trade_pnl = 0.0;
    }
  }
};

}  // namespace

BacktestRunResult RunSingleBacktest(const BacktestConfig& config,
                                    const std::string& run_id,
                                    const SingleRunOutputOptions& output_options) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();

  BacktestRunResult result;
  result.run_id = run_id;
  result.config_used = config;

  auto SetDuration = [&]() {
    result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
  };
  auto Fail = [&](const std::string& message) {
    result.success = false;
    result.error_message = message;
    SetDuration();
    return result;
  };

  std::unique_ptr<TickLogger> tick_log;
  std::ofstream equity_csv;
  std::ofstream trades_csv;

  if (output_options.write_outputs) {
    fs::create_directories(config.outdir);
    EnsureParentDir(config.log_path);

    tick_log = std::make_unique<TickLogger>(config.log_path);
    if (!tick_log->IsOpen()) {
      return Fail("Unable to open log path: " + config.log_path);
    }
    tick_log->WriteHeader();

    equity_csv.open((fs::path(config.outdir) / "equity.csv").string(), std::ios::out | std::ios::trunc);
    trades_csv.open((fs::path(config.outdir) / "trades.csv").string(), std::ios::out | std::ios::trunc);
    if (!equity_csv.is_open() || !trades_csv.is_open()) {
      return Fail("Unable to open output files in outdir: " + config.outdir);
    }
    equity_csv << "timestamp,equity\n";
    trades_csv << "timestamp,action,qty,price,pnl\n";
  }

  RollingStats stats(static_cast<std::size_t>(config.rolling_window));
  Portfolio portfolio(config.initial_cash, false);
  MaxDrawdownTracker drawdown;
  SharpeTracker sharpe;
  RoundTripAccumulator round_trips;

  SpikeCandidate candidate;
  PositionPlan position_plan;
  std::size_t tick_index = 0;
  int ticks_in_position = 0;
  double prev_equity = config.initial_cash;

  double final_equity = config.initial_cash;
  int fill_attempts = 0;
  int fill_rejections = 0;
  int partial_fill_count = 0;
  int requested_contracts = 0;
  int filled_contracts = 0;

  int signals_generated = 0;
  int resting_orders_submitted = 0;
  int resting_orders_fully_filled = 0;
  int resting_orders_partially_filled = 0;
  int resting_orders_expired = 0;
  int resting_orders_canceled = 0;
  int total_partial_fill_events = 0;
  int fill_delay_ticks_sum = 0;
  int fill_event_count = 0;
  double filled_fraction_sum = 0.0;
  int filled_fraction_count = 0;
  int missed_trades_due_to_expiry = 0;

  std::vector<RestingOrder> active_orders;
  int order_id_counter = 0;

  const bool use_resting =
      config.enable_resting_orders && config.resting_order_lifetime_ticks > 0;

  auto ApplyAndRecordFill = [&](const Tick& tick, const std::string& action_name, int qty_signed) {
    ++fill_attempts;
    requested_contracts += std::abs(qty_signed);
    const OrderRequest order{tick.timestamp, action_name, qty_signed};
    const ExecutionResult exec = SimulateFill(tick, order, config);
    if (!exec.did_fill) {
      ++fill_rejections;
      return false;
    }
    filled_contracts += std::abs(exec.filled_qty_signed);
    if (exec.was_partial) {
      ++partial_fill_count;
    }
    TradeRecord tr;
    portfolio.ApplyFill(order.timestamp, order.action, exec.filled_qty_signed, exec.fill_price,
                        config.fee_per_contract, &tr);
    if (output_options.write_outputs) {
      trades_csv << tr.timestamp << "," << tr.action << "," << tr.qty << "," << std::fixed
                 << std::setprecision(6) << tr.price << "," << tr.pnl << "\n";
    }
    round_trips.Update(tr);
    return true;
  };

  auto HasActiveEntryOrder = [&]() {
    for (const auto& o : active_orders) {
      if (o.intent == RestingOrderIntent::Entry) return true;
    }
    return false;
  };

  CsvReadStats csv_stats;
  std::string csv_error;
  const bool ok = StreamTicksFromCsv(
      config.csv_path,
      [&](const Tick& tick) {
        ++tick_index;

        // 1. Update strategy state: capture stats before adding current tick (for spike detection)
        const bool ready_before = stats.IsReady();
        const double mean_before = stats.Mean();
        const double std_before = stats.StdDev();
        stats.Update(tick.price);

        // Expire candidate if past confirmation window
        if (candidate.active && tick_index > candidate.tick_index + 1U) {
          candidate.active = false;
        }

        // 2. Process active resting orders (before new signals)
        bool entered_this_tick = false;
        auto it = active_orders.begin();
        while (it != active_orders.end()) {
          RestingOrder& order = *it;
          if (tick_index > order.expiry_tick_index) {
            if (order.remaining_qty_signed != 0) {
              ++resting_orders_expired;
              ++resting_orders_canceled;
              if (order.intent == RestingOrderIntent::Entry) {
                ++missed_trades_due_to_expiry;
              }
            }
            it = active_orders.erase(it);
            continue;
          }

          const OrderRequest req{tick.timestamp, order.action, order.remaining_qty_signed};
          BacktestConfig exec_cfg = config;
          if (!config.allow_resting_partial_fills) {
            exec_cfg.allow_partial_fills = false;
          }
          ++fill_attempts;
          const ExecutionResult exec = SimulateFill(tick, req, exec_cfg);

          if (exec.did_fill) {
            filled_contracts += std::abs(exec.filled_qty_signed);
            if (exec.was_partial) {
              ++partial_fill_count;
              ++total_partial_fill_events;
            }

            TradeRecord tr;
            portfolio.ApplyFill(req.timestamp, req.action, exec.filled_qty_signed, exec.fill_price,
                               config.fee_per_contract, &tr);
            if (output_options.write_outputs) {
              trades_csv << tr.timestamp << "," << tr.action << "," << tr.qty << "," << std::fixed
                         << std::setprecision(6) << tr.price << "," << tr.pnl << "\n";
            }
            round_trips.Update(tr);

            const int delay = static_cast<int>(tick_index - order.submit_tick_index);
            order.fill_delay_ticks_sum += delay;
            order.fill_event_count += 1;
            order.cumulative_filled_qty += static_cast<double>(std::abs(exec.filled_qty_signed));
            order.avg_fill_price =
                (order.avg_fill_price * (order.fill_event_count - 1) + exec.fill_price) /
                static_cast<double>(order.fill_event_count);
            order.remaining_qty_signed -= exec.filled_qty_signed;

            fill_delay_ticks_sum += delay;
            ++fill_event_count;
            filled_fraction_sum +=
                static_cast<double>(std::abs(exec.filled_qty_signed)) /
                static_cast<double>(std::abs(order.desired_qty_signed));
            ++filled_fraction_count;

            if (order.remaining_qty_signed == 0) {
              ++resting_orders_fully_filled;
              if (order.intent == RestingOrderIntent::Entry) {
                entered_this_tick = true;
                position_plan.initial_qty = std::abs(portfolio.PositionQty());
                position_plan.stage_index = 0;
                ticks_in_position = 0;
              }
              it = active_orders.erase(it);
              continue;
            }
            order.status = RestingOrderStatus::PartiallyFilled;
            ++resting_orders_partially_filled;
            if (order.intent == RestingOrderIntent::Entry && std::abs(portfolio.PositionQty()) > 0) {
              entered_this_tick = true;
              position_plan.initial_qty = std::abs(portfolio.PositionQty());
              position_plan.stage_index = 0;
              ticks_in_position = 0;
            }
          } else {
            ++fill_rejections;
          }
          ++it;
        }

        // 3. Strategy decision logic
        SpikeKind spike_flag = SpikeKind::kNone;
        std::string confirm_status = "na";
        std::string action = "none";

        if (candidate.active && tick_index == candidate.tick_index + 1U) {
          const bool confirmed =
              (candidate.kind == SpikeKind::kUp && tick.price < candidate.spike_price) ||
              (candidate.kind == SpikeKind::kDown && tick.price > candidate.spike_price);
          confirm_status = confirmed ? "confirmed" : "rejected";
          if (confirmed && portfolio.PositionQty() == 0 && !HasActiveEntryOrder()) {
            const int entry_qty = candidate.kind == SpikeKind::kUp ? -config.position_size : config.position_size;
            ++signals_generated;
            if (use_resting) {
              std::ostringstream oss;
              oss << "ro-" << (++order_id_counter);
              RestingOrder ro;
              ro.order_id = oss.str();
              ro.intent = RestingOrderIntent::Entry;
              ro.action = "entry";
              ro.desired_qty_signed = entry_qty;
              ro.remaining_qty_signed = entry_qty;
              ro.submit_tick_index = tick_index;
              ro.expiry_tick_index = tick_index + static_cast<std::size_t>(config.resting_order_lifetime_ticks);
              ro.submit_timestamp = tick.timestamp;
              ro.status = RestingOrderStatus::Active;
              active_orders.push_back(ro);
              ++resting_orders_submitted;
              requested_contracts += std::abs(entry_qty);
              action = "enter";
            } else {
              if (ApplyAndRecordFill(tick, "entry", entry_qty)) {
                position_plan.initial_qty = std::abs(portfolio.PositionQty());
                position_plan.stage_index = 0;
                ticks_in_position = 0;
                action = "enter";
                entered_this_tick = true;
              }
            }
          }
          candidate.active = false;
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

          // Gradual exit (immediate fill for Phase 1)
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
                if (ApplyAndRecordFill(tick, "exit", signed_exit)) {
                  action = action == "none" ? "gradual_exit" : action + "|gradual_exit";
                  ++position_plan.stage_index;
                }
              }
            }
          }

          if (portfolio.PositionQty() != 0 && adverse_move >= config.stop_loss_points) {
            const int signed_exit = -portfolio.PositionQty();
            if (ApplyAndRecordFill(tick, "stop_loss", signed_exit)) {
              action = action == "none" ? "stop_loss" : action + "|stop_loss";
            }
          }

          if (portfolio.PositionQty() != 0 && ticks_in_position >= config.max_hold_ticks) {
            const int signed_exit = -portfolio.PositionQty();
            if (ApplyAndRecordFill(tick, "max_hold", signed_exit)) {
              action = action == "none" ? "max_hold_exit" : action + "|max_hold_exit";
            }
          }

          if (portfolio.PositionQty() == 0) {
            ticks_in_position = 0;
            position_plan.initial_qty = 0;
            position_plan.stage_index = 0;
          }
        }

        // 4. Advance to next tick: equity, drawdown, logging
        const double unrealized = portfolio.UnrealizedPnl(tick.price);
        const double equity = portfolio.Equity(tick.price);
        drawdown.Update(equity);
        if (config.compute_sharpe && prev_equity != 0.0) {
          sharpe.Update((equity - prev_equity) / prev_equity);
        }
        prev_equity = equity;

        final_equity = equity;
        if (output_options.write_outputs) {
          equity_csv << tick.timestamp << "," << std::fixed << std::setprecision(6) << equity << "\n";
          tick_log->LogRow(tick.timestamp, tick.price, stats.Mean(), stats.StdDev(), SpikeToString(spike_flag),
                           confirm_status, action, portfolio.PositionQty(), portfolio.Cash(), equity,
                           portfolio.RealizedPnl(), unrealized);
        }
        return true;
      },
      &csv_stats, &csv_error);

  if (!ok) {
    return Fail(csv_error);
  }

  const RoundTripMetrics rt = round_trips.metrics;
  const double total_return = config.initial_cash != 0.0 ? (final_equity - config.initial_cash) / config.initial_cash
                                                          : 0.0;
  const int trade_count = rt.trade_count;
  const double win_rate = trade_count > 0 ? static_cast<double>(rt.win_count) / static_cast<double>(trade_count)
                                          : 0.0;
  const double avg_trade_pnl = trade_count > 0 ? rt.total_pnl / static_cast<double>(trade_count) : 0.0;
  const double total_pnl = final_equity - config.initial_cash;

  RestingMetrics resting_metrics;
  if (use_resting) {
    resting_metrics.enabled = true;
    resting_metrics.signals_generated = signals_generated;
    resting_metrics.resting_orders_submitted = resting_orders_submitted;
    resting_metrics.resting_orders_fully_filled = resting_orders_fully_filled;
    resting_metrics.resting_orders_partially_filled = resting_orders_partially_filled;
    resting_metrics.resting_orders_expired = resting_orders_expired;
    resting_metrics.resting_orders_canceled = resting_orders_canceled;
    resting_metrics.total_partial_fill_events = total_partial_fill_events;
    resting_metrics.average_fill_delay_ticks =
        fill_event_count > 0 ? static_cast<double>(fill_delay_ticks_sum) / static_cast<double>(fill_event_count)
                            : 0.0;
    resting_metrics.average_filled_fraction =
        filled_fraction_count > 0 ? filled_fraction_sum / static_cast<double>(filled_fraction_count) : 0.0;
    resting_metrics.missed_trades_due_to_expiry = missed_trades_due_to_expiry;
  }

  if (output_options.write_outputs) {
    std::string metrics_error;
    const bool metrics_ok = WriteMetricsJson(
        (fs::path(config.outdir) / "metrics.json").string(), total_return, drawdown.MaxDrawdown(), trade_count,
        win_rate, avg_trade_pnl, config.compute_sharpe, sharpe.Sharpe(),
        use_resting ? &resting_metrics : nullptr, &metrics_error);
    if (!metrics_ok) {
      return Fail(metrics_error);
    }
  }

  result.success = true;
  result.csv_stats = csv_stats;
  result.metrics.final_equity = final_equity;
  result.metrics.total_pnl = total_pnl;
  result.metrics.total_return = total_return;
  result.metrics.trade_count = trade_count;
  result.metrics.win_rate = win_rate;
  result.metrics.avg_trade_pnl = avg_trade_pnl;
  result.metrics.max_drawdown = drawdown.MaxDrawdown();
  result.metrics.sharpe = sharpe.Sharpe();
  result.metrics.has_sharpe = config.compute_sharpe;
  result.metrics.fill_attempts = fill_attempts;
  result.metrics.fill_rejections = fill_rejections;
  result.metrics.partial_fill_count = partial_fill_count;
  result.metrics.requested_contracts = requested_contracts;
  result.metrics.filled_contracts = filled_contracts;
  result.metrics.signals_generated = signals_generated;
  result.metrics.resting_orders_submitted = resting_orders_submitted;
  result.metrics.resting_orders_fully_filled = resting_orders_fully_filled;
  result.metrics.resting_orders_partially_filled = resting_orders_partially_filled;
  result.metrics.resting_orders_expired = resting_orders_expired;
  result.metrics.resting_orders_canceled = resting_orders_canceled;
  result.metrics.total_partial_fill_events = total_partial_fill_events;
  result.metrics.average_fill_delay_ticks =
      fill_event_count > 0 ? static_cast<double>(fill_delay_ticks_sum) / static_cast<double>(fill_event_count)
                          : 0.0;
  result.metrics.average_filled_fraction =
      filled_fraction_count > 0 ? filled_fraction_sum / static_cast<double>(filled_fraction_count) : 0.0;
  result.metrics.missed_trades_due_to_expiry = missed_trades_due_to_expiry;
  SetDuration();

  if (output_options.print_summary) {
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
    std::cout << "Elapsed ms: " << result.duration_ms << "\n";
    if (output_options.write_outputs) {
      std::cout << "Outputs: " << (fs::path(config.outdir) / "equity.csv").string() << ", "
                << (fs::path(config.outdir) / "trades.csv").string() << ", "
                << (fs::path(config.outdir) / "metrics.json").string() << "\n";
      std::cout << "Tick log: " << config.log_path << "\n";
    }
  }

  return result;
}

int RunBacktest(const BacktestConfig& config) {
  const SingleRunOutputOptions options{true, true};
  const BacktestRunResult result = RunSingleBacktest(config, "single-run", options);
  if (!result.success) {
    std::cerr << result.error_message << "\n";
    return 1;
  }
  return 0;
}

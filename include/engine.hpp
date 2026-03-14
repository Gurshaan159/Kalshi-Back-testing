#pragma once

#include "csv_reader.hpp"

#include <string>

struct BacktestConfig {
  std::string csv_path;
  std::string outdir{"out"};
  std::string log_path{"logs/run.log"};
  int rolling_window{50};
  double spike_threshold{2.5};
  int position_size{3};
  double stop_loss_points{3.0};
  int max_hold_ticks{100};
  double fee_per_contract{0.10};
  double slippage_points{0.50};
  double max_spread_points{2.0};
  double volume_fill_ratio{0.25};
  bool allow_partial_fills{true};
  bool use_quotes_for_fills{true};
  double initial_cash{10000.0};
  bool compute_sharpe{false};
  bool enable_resting_orders{true};
  int resting_order_lifetime_ticks{3};
  bool allow_resting_partial_fills{true};
};

struct BacktestRunMetrics {
  double final_equity{0.0};
  double total_pnl{0.0};
  double total_return{0.0};
  int trade_count{0};
  double win_rate{0.0};
  double avg_trade_pnl{0.0};
  double max_drawdown{0.0};
  double sharpe{0.0};
  bool has_sharpe{false};
  int fill_attempts{0};
  int fill_rejections{0};
  int partial_fill_count{0};
  int requested_contracts{0};
  int filled_contracts{0};
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
};

struct SingleRunOutputOptions {
  bool write_outputs{true};
  bool print_summary{true};
};

struct BacktestRunResult {
  std::string run_id;
  BacktestConfig config_used;
  bool success{false};
  std::string error_message;
  long long duration_ms{0};
  CsvReadStats csv_stats;
  BacktestRunMetrics metrics;
};

BacktestRunResult RunSingleBacktest(const BacktestConfig& config,
                                    const std::string& run_id,
                                    const SingleRunOutputOptions& output_options);

int RunBacktest(const BacktestConfig& config);

#pragma once

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
};

int RunBacktest(const BacktestConfig& config);

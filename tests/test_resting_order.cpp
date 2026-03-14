#include "engine.hpp"
#include "test_common.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

bool WriteTempCsv(const std::string& path, const std::string& content) {
  std::ofstream f(path);
  if (!f) return false;
  f << content;
  return f.good();
}

}  // namespace

bool TestRestingOrderLifecycle() {
  const std::string csv_path = "out/test_resting_lifecycle.csv";
  const std::string csv_content =
      "timestamp,price,bid,ask,volume\n"
      "t1,50.0,49.5,50.5,10\n"
      "t2,50.5,50.0,51.0,10\n"
      "t3,49.5,49.0,50.0,10\n"
      "t4,50.0,49.5,50.5,10\n"
      "t5,50.0,49.5,50.5,10\n"
      "t6,200.0,199.5,200.5,10\n"
      "t7,50.0,49.5,50.5,10\n"
      "t8,51.0,50.5,51.5,10\n";
  if (!WriteTempCsv(csv_path, csv_content)) {
    return ReportFailure("TestRestingOrderLifecycle", "failed to write temp CSV");
  }

  BacktestConfig config;
  config.csv_path = csv_path;
  config.outdir = "out/test_resting_out_lifecycle";
  config.log_path = "logs/test_resting_lifecycle.log";
  config.rolling_window = 5;
  config.spike_threshold = 2.5;
  config.position_size = 2;
  config.enable_resting_orders = true;
  config.resting_order_lifetime_ticks = 3;

  const SingleRunOutputOptions opts{false, false};
  const BacktestRunResult result = RunSingleBacktest(config, "lifecycle", opts);

  if (!result.success) {
    std::remove(csv_path.c_str());
    return ReportFailure("TestRestingOrderLifecycle", result.error_message.c_str());
  }
  std::remove(csv_path.c_str());
  if (result.metrics.resting_orders_submitted < 1) {
    return ReportFailure("TestRestingOrderLifecycle", "expected at least one resting order submitted");
  }
  if (result.metrics.resting_orders_fully_filled < 1) {
    return ReportFailure("TestRestingOrderLifecycle", "expected at least one resting order to fill");
  }
  return true;
}

bool TestPartialFillsOverTime() {
  const std::string csv_path = "out/test_resting_partial.csv";
  const std::string csv_content =
      "timestamp,price,bid,ask,volume\n"
      "100,50.0,49.5,50.5,10\n"
      "101,50.5,50.0,51.0,10\n"
      "102,49.5,49.0,50.0,10\n"
      "103,50.0,49.5,50.5,10\n"
      "104,50.0,49.5,50.5,10\n"
      "105,200.0,199.5,200.5,4\n"
      "106,50.0,49.5,50.5,4\n"
      "107,51.0,50.5,51.5,4\n"
      "108,50.5,50.0,51.0,4\n"
      "109,50.0,49.5,50.5,10\n";
  if (!WriteTempCsv(csv_path, csv_content)) {
    return ReportFailure("TestPartialFillsOverTime", "failed to write temp CSV");
  }

  BacktestConfig config;
  config.csv_path = csv_path;
  config.outdir = "out/test_resting_out_partial";
  config.log_path = "logs/test_resting_partial.log";
  config.rolling_window = 5;
  config.spike_threshold = 2.5;
  config.position_size = 3;
  config.volume_fill_ratio = 0.25;
  config.enable_resting_orders = true;
  config.resting_order_lifetime_ticks = 5;
  config.allow_resting_partial_fills = true;

  const SingleRunOutputOptions opts{false, false};
  const BacktestRunResult result = RunSingleBacktest(config, "partial", opts);

  std::remove(csv_path.c_str());

  if (!result.success) {
    return ReportFailure("TestPartialFillsOverTime", result.error_message.c_str());
  }
  if (result.metrics.resting_orders_fully_filled < 1) {
    return ReportFailure("TestPartialFillsOverTime", "expected at least one resting order to fill");
  }
  return true;
}

bool TestExpiryBehavior() {
  const std::string csv_path = "out/test_resting_expiry.csv";
  const std::string csv_content =
      "timestamp,price,bid,ask,volume\n"
      "100,50.0,48.0,52.0,10\n"
      "101,50.5,48.0,52.0,10\n"
      "102,49.5,48.0,52.0,10\n"
      "103,50.0,48.0,52.0,10\n"
      "104,50.0,48.0,52.0,10\n"
      "105,200.0,48.0,252.0,10\n"
      "106,50.0,48.0,56.0,10\n"
      "107,51.0,48.0,54.0,10\n"
      "108,50.0,48.0,52.0,10\n"
      "109,50.0,48.0,52.0,10\n";
  if (!WriteTempCsv(csv_path, csv_content)) {
    return ReportFailure("TestExpiryBehavior", "failed to write temp CSV");
  }

  BacktestConfig config;
  config.csv_path = csv_path;
  config.outdir = "out/test_resting_out_expiry";
  config.log_path = "logs/test_resting_expiry.log";
  config.rolling_window = 5;
  config.spike_threshold = 2.5;
  config.position_size = 2;
  config.max_spread_points = 1.0;
  config.enable_resting_orders = true;
  config.resting_order_lifetime_ticks = 2;

  const SingleRunOutputOptions opts{false, false};
  const BacktestRunResult result = RunSingleBacktest(config, "expiry", opts);

  std::remove(csv_path.c_str());

  if (!result.success) {
    return ReportFailure("TestExpiryBehavior", result.error_message.c_str());
  }
  if (result.metrics.missed_trades_due_to_expiry < 1 || result.metrics.resting_orders_expired < 1) {
    return ReportFailure("TestExpiryBehavior", "expected expired orders and missed trades");
  }
  return true;
}

bool TestImmediateFillCompatibility() {
  const std::string csv_path = "out/test_resting_immediate.csv";
  const std::string csv_content =
      "timestamp,price,bid,ask,volume\n"
      "100,50.0,49.5,50.5,10\n"
      "101,50.5,50.0,51.0,10\n"
      "102,49.5,49.0,50.0,10\n"
      "103,50.0,49.5,50.5,10\n"
      "104,50.0,49.5,50.5,10\n"
      "105,200.0,199.5,200.5,10\n"
      "106,50.0,49.5,50.5,10\n";
  if (!WriteTempCsv(csv_path, csv_content)) {
    return ReportFailure("TestImmediateFillCompatibility", "failed to write temp CSV");
  }

  BacktestConfig config_resting;
  config_resting.csv_path = csv_path;
  config_resting.outdir = "out/test_resting_out_imm1";
  config_resting.log_path = "logs/test_resting_imm1.log";
  config_resting.rolling_window = 5;
  config_resting.spike_threshold = 2.5;
  config_resting.position_size = 2;
  config_resting.enable_resting_orders = true;
  config_resting.resting_order_lifetime_ticks = 3;

  BacktestConfig config_immediate;
  config_immediate.csv_path = csv_path;
  config_immediate.outdir = "out/test_resting_out_imm2";
  config_immediate.log_path = "logs/test_resting_imm2.log";
  config_immediate.rolling_window = 5;
  config_immediate.spike_threshold = 2.5;
  config_immediate.position_size = 2;
  config_immediate.enable_resting_orders = false;

  const SingleRunOutputOptions opts{false, false};
  const BacktestRunResult r_resting = RunSingleBacktest(config_resting, "resting", opts);
  const BacktestRunResult r_immediate = RunSingleBacktest(config_immediate, "immediate", opts);

  std::remove(csv_path.c_str());

  if (!r_resting.success || !r_immediate.success) {
    return ReportFailure("TestImmediateFillCompatibility", "one or both runs failed");
  }
  if (r_immediate.metrics.resting_orders_submitted != 0) {
    return ReportFailure("TestImmediateFillCompatibility", "immediate mode should have 0 resting orders");
  }
  return true;
}

bool TestRestingOrderMetrics() {
  const std::string csv_path = "out/test_resting_metrics.csv";
  const std::string csv_content =
      "timestamp,price,bid,ask,volume\n"
      "100,50.0,49.5,50.5,10\n"
      "101,50.5,50.0,51.0,10\n"
      "102,49.5,49.0,50.0,10\n"
      "103,50.0,49.5,50.5,10\n"
      "104,50.0,49.5,50.5,10\n"
      "105,200.0,199.5,200.5,10\n"
      "106,50.0,49.5,50.5,10\n"
      "107,51.0,50.5,51.5,10\n";
  if (!WriteTempCsv(csv_path, csv_content)) {
    return ReportFailure("TestRestingOrderMetrics", "failed to write temp CSV");
  }

  BacktestConfig config;
  config.csv_path = csv_path;
  config.outdir = "out/test_resting_out_metrics";
  config.log_path = "logs/test_resting_metrics.log";
  config.rolling_window = 5;
  config.spike_threshold = 2.5;
  config.position_size = 2;
  config.enable_resting_orders = true;
  config.resting_order_lifetime_ticks = 3;

  const SingleRunOutputOptions opts{false, false};
  const BacktestRunResult result = RunSingleBacktest(config, "metrics", opts);

  std::remove(csv_path.c_str());

  if (!result.success) {
    return ReportFailure("TestRestingOrderMetrics", result.error_message.c_str());
  }
  if (result.metrics.resting_orders_submitted < 1) {
    return ReportFailure("TestRestingOrderMetrics", "expected resting_orders_submitted >= 1");
  }
  if (result.metrics.resting_orders_fully_filled + result.metrics.resting_orders_expired < 1) {
    return ReportFailure("TestRestingOrderMetrics", "expected fully_filled or expired count");
  }
  return true;
}

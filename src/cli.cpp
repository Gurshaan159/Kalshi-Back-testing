#include "cli.hpp"

#include "engine.hpp"
#include "kalshi_fetch.hpp"
#include "sweep.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

void PrintHelp() {
  std::cout
      << "kalshi_backtest usage\n"
      << "\n"
      << "Commands:\n"
      << "  backtest --csv <path> --outdir <dir> --log <path> [options]\n"
      << "  sweep --csv <path> [--outdir <dir>] [--logdir <dir>] [--concurrency <n>] [options]\n"
      << "  fetch --contract <ticker> --out <path> [--api-key <key>] [--start-ts <unix>] [--end-ts <unix>] [--period <1|60|1440>]\n"
      << "\n"
      << "Options:\n"
      << "  --window <int>             Rolling window (default: 50)\n"
      << "  --spike-threshold <float>  Spike threshold in std devs (default: 2.5)\n"
      << "  --position-size <int>      Contracts per entry (default: 3)\n"
      << "  --stop-loss <float>        Stop-loss in price points (default: 3.0)\n"
      << "  --max-hold <int>           Max holding ticks (default: 100)\n"
      << "  --fee <float>              Fee per contract per fill points (default: 0.10)\n"
      << "  --slippage <float>         Slippage per fill points (default: 0.50)\n"
      << "  --initial-cash <float>     Initial cash (default: 10000)\n"
      << "  --sharpe                   Compute optional Sharpe from per-tick returns\n"
      << "  --no-sharpe                Disable Sharpe computation in sweep mode\n"
      << "  --concurrency <int>        Sweep worker count (default: min(cpu,8))\n"
      << "  --logdir <dir>             Sweep log root dir (default: logs/sweeps)\n"
      << "  --sweep-id <id>            Optional sweep id folder name\n"
      << "  --api-key <key>            Optional Kalshi API key for fetch\n"
      << "  --start-ts <unix>          Fetch start timestamp (seconds)\n"
      << "  --end-ts <unix>            Fetch end timestamp (seconds)\n"
      << "  --period <int>             Candle interval minutes (default: 1)\n"
      << "  --help                     Show this message\n"
      << "\n"
      << "Example:\n"
      << "  kalshi_backtest backtest --csv data/sample.csv --outdir out --log logs/run.log\n";
}

bool ParseInt(const std::string& raw, int* out) {
  try {
    std::size_t idx = 0;
    *out = std::stoi(raw, &idx);
    if (idx != raw.size()) {
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseDouble(const std::string& raw, double* out) {
  try {
    std::size_t idx = 0;
    *out = std::stod(raw, &idx);
    if (idx != raw.size()) {
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool ApplyBacktestOptions(const std::unordered_map<std::string, std::string>& kv,
                          BacktestConfig* config,
                          std::string* error) {
  if (kv.find("--csv") == kv.end()) {
    *error = "--csv is required";
    return false;
  }
  config->csv_path = kv.at("--csv");

  if (kv.find("--outdir") != kv.end()) {
    config->outdir = kv.at("--outdir");
  }
  if (kv.find("--log") != kv.end()) {
    config->log_path = kv.at("--log");
  }
  if (kv.find("--window") != kv.end() && !ParseInt(kv.at("--window"), &config->rolling_window)) {
    *error = "Invalid --window";
    return false;
  }
  if (kv.find("--spike-threshold") != kv.end() &&
      !ParseDouble(kv.at("--spike-threshold"), &config->spike_threshold)) {
    *error = "Invalid --spike-threshold";
    return false;
  }
  if (kv.find("--position-size") != kv.end() &&
      !ParseInt(kv.at("--position-size"), &config->position_size)) {
    *error = "Invalid --position-size";
    return false;
  }
  if (kv.find("--stop-loss") != kv.end() &&
      !ParseDouble(kv.at("--stop-loss"), &config->stop_loss_points)) {
    *error = "Invalid --stop-loss";
    return false;
  }
  if (kv.find("--max-hold") != kv.end() && !ParseInt(kv.at("--max-hold"), &config->max_hold_ticks)) {
    *error = "Invalid --max-hold";
    return false;
  }
  if (kv.find("--fee") != kv.end() && !ParseDouble(kv.at("--fee"), &config->fee_per_contract)) {
    *error = "Invalid --fee";
    return false;
  }
  if (kv.find("--slippage") != kv.end() && !ParseDouble(kv.at("--slippage"), &config->slippage_points)) {
    *error = "Invalid --slippage";
    return false;
  }
  if (kv.find("--initial-cash") != kv.end() && !ParseDouble(kv.at("--initial-cash"), &config->initial_cash)) {
    *error = "Invalid --initial-cash";
    return false;
  }
  if (config->rolling_window <= 1 || config->position_size <= 0 || config->max_hold_ticks <= 0 ||
      config->spike_threshold <= 0.0 || config->stop_loss_points <= 0.0 || config->fee_per_contract < 0.0 ||
      config->slippage_points < 0.0) {
    *error = "One or more options are out of valid range";
    return false;
  }
  return true;
}

}  // namespace

int RunCli(int argc, char** argv) {
  if (argc <= 1) {
    PrintHelp();
    return 1;
  }

  const std::string command = argv[1];
  if (command == "--help" || command == "-h" || command == "help") {
    PrintHelp();
    return 0;
  }

  if (command == "fetch") {
    std::unordered_map<std::string, std::string> kv;
    int i = 2;
    while (i < argc) {
      const std::string key = argv[i];
      if (key == "--help" || key == "-h") {
        PrintHelp();
        return 0;
      }
      if (i + 1 >= argc) {
        std::cerr << "Missing value after " << key << "\n";
        return 1;
      }
      kv[key] = argv[i + 1];
      i += 2;
    }

    FetchConfig fetch_cfg;
    if (kv.find("--market") != kv.end()) {
      fetch_cfg.market_id = kv["--market"];
    }
    if (kv.find("--contract") != kv.end()) {
      fetch_cfg.contract_id = kv["--contract"];
    }
    if (kv.find("--api-key") != kv.end()) {
      fetch_cfg.api_key = kv["--api-key"];
    }
    if (kv.find("--out") != kv.end()) {
      fetch_cfg.out_csv_path = kv["--out"];
    }
    if (kv.find("--start-ts") != kv.end()) {
      long long v = 0;
      try {
        std::size_t idx = 0;
        v = std::stoll(kv["--start-ts"], &idx);
        if (idx != kv["--start-ts"].size()) {
          throw std::invalid_argument("bad");
        }
      } catch (...) {
        std::cerr << "Invalid --start-ts\n";
        return 1;
      }
      fetch_cfg.start_ts = v;
    }
    if (kv.find("--end-ts") != kv.end()) {
      long long v = 0;
      try {
        std::size_t idx = 0;
        v = std::stoll(kv["--end-ts"], &idx);
        if (idx != kv["--end-ts"].size()) {
          throw std::invalid_argument("bad");
        }
      } catch (...) {
        std::cerr << "Invalid --end-ts\n";
        return 1;
      }
      fetch_cfg.end_ts = v;
    }
    if (kv.find("--period") != kv.end()) {
      int v = 0;
      if (!ParseInt(kv["--period"], &v) || (v != 1 && v != 60 && v != 1440)) {
        std::cerr << "Invalid --period; expected 1, 60, or 1440\n";
        return 1;
      }
      fetch_cfg.period_interval = v;
    }
    return RunFetch(fetch_cfg);
  }

  if (command == "sweep") {
    BacktestConfig config;
    std::unordered_map<std::string, std::string> kv;
    bool want_sharpe = true;
    int i = 2;
    while (i < argc) {
      const std::string key = argv[i];
      if (key == "--help" || key == "-h") {
        PrintHelp();
        return 0;
      }
      if (key == "--sharpe") {
        want_sharpe = true;
        ++i;
        continue;
      }
      if (key == "--no-sharpe") {
        want_sharpe = false;
        ++i;
        continue;
      }
      if (i + 1 >= argc) {
        std::cerr << "Missing value after " << key << "\n";
        return 1;
      }
      kv[key] = argv[i + 1];
      i += 2;
    }

    std::string error;
    if (!ApplyBacktestOptions(kv, &config, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
    config.compute_sharpe = want_sharpe;

    SweepRunOptions sweep_options;
    if (kv.find("--outdir") != kv.end()) {
      sweep_options.out_root_dir = kv["--outdir"];
    }
    if (kv.find("--logdir") != kv.end()) {
      sweep_options.log_root_dir = kv["--logdir"];
    }
    if (kv.find("--sweep-id") != kv.end()) {
      sweep_options.sweep_id = kv["--sweep-id"];
    }
    if (kv.find("--concurrency") != kv.end()) {
      int concurrency = 0;
      if (!ParseInt(kv["--concurrency"], &concurrency) || concurrency <= 0) {
        std::cerr << "Invalid --concurrency\n";
        return 1;
      }
      sweep_options.concurrency = concurrency;
    }
    sweep_options.force_sharpe = want_sharpe;

    const SweepBatchResult batch =
        RunSweep(config, sweep_options, SweepGridConfig{}, SweepCombinationFilter{});
    if (!batch.success) {
      std::cerr << "Sweep failed: " << batch.error_message << "\n";
      return 1;
    }
    std::cout << "Sweep complete\n";
    std::cout << "Sweep id: " << batch.sweep_id << "\n";
    std::cout << "Runs executed: " << batch.total_executed << "\n";
    std::cout << "Batch outputs: " << batch.batch_output_dir << "\n";
    std::cout << "Batch logs: " << batch.batch_log_dir << "\n";
    return 0;
  }

  if (command != "backtest") {
    std::cerr << "Unknown command: " << command << "\n";
    PrintHelp();
    return 1;
  }

  BacktestConfig config;
  std::unordered_map<std::string, std::string> kv;
  bool want_sharpe = false;
  int i = 2;
  while (i < argc) {
    const std::string key = argv[i];
    if (key == "--sharpe") {
      want_sharpe = true;
      ++i;
      continue;
    }
    if (key == "--help" || key == "-h") {
      PrintHelp();
      return 0;
    }
    if (i + 1 >= argc) {
      std::cerr << "Missing value after " << key << "\n";
      return 1;
    }
    kv[key] = argv[i + 1];
    i += 2;
  }

  std::string error;
  if (!ApplyBacktestOptions(kv, &config, &error)) {
    std::cerr << error << "\n";
    return 1;
  }
  config.compute_sharpe = want_sharpe;
  return RunBacktest(config);
}

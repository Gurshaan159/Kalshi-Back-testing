#include "cli.hpp"

#include "engine.hpp"
#include "kalshi_fetch.hpp"

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

  if (kv.find("--csv") == kv.end()) {
    std::cerr << "--csv is required\n";
    return 1;
  }
  config.csv_path = kv["--csv"];

  if (kv.find("--outdir") != kv.end()) {
    config.outdir = kv["--outdir"];
  }
  if (kv.find("--log") != kv.end()) {
    config.log_path = kv["--log"];
  }
  if (kv.find("--window") != kv.end() && !ParseInt(kv["--window"], &config.rolling_window)) {
    std::cerr << "Invalid --window\n";
    return 1;
  }
  if (kv.find("--spike-threshold") != kv.end() &&
      !ParseDouble(kv["--spike-threshold"], &config.spike_threshold)) {
    std::cerr << "Invalid --spike-threshold\n";
    return 1;
  }
  if (kv.find("--position-size") != kv.end() &&
      !ParseInt(kv["--position-size"], &config.position_size)) {
    std::cerr << "Invalid --position-size\n";
    return 1;
  }
  if (kv.find("--stop-loss") != kv.end() &&
      !ParseDouble(kv["--stop-loss"], &config.stop_loss_points)) {
    std::cerr << "Invalid --stop-loss\n";
    return 1;
  }
  if (kv.find("--max-hold") != kv.end() && !ParseInt(kv["--max-hold"], &config.max_hold_ticks)) {
    std::cerr << "Invalid --max-hold\n";
    return 1;
  }
  if (kv.find("--fee") != kv.end() && !ParseDouble(kv["--fee"], &config.fee_per_contract)) {
    std::cerr << "Invalid --fee\n";
    return 1;
  }
  if (kv.find("--slippage") != kv.end() &&
      !ParseDouble(kv["--slippage"], &config.slippage_points)) {
    std::cerr << "Invalid --slippage\n";
    return 1;
  }
  if (kv.find("--initial-cash") != kv.end() &&
      !ParseDouble(kv["--initial-cash"], &config.initial_cash)) {
    std::cerr << "Invalid --initial-cash\n";
    return 1;
  }
  config.compute_sharpe = want_sharpe;

  if (config.rolling_window <= 1 || config.position_size <= 0 || config.max_hold_ticks <= 0 ||
      config.spike_threshold <= 0.0 || config.stop_loss_points <= 0.0 || config.fee_per_contract < 0.0 ||
      config.slippage_points < 0.0) {
    std::cerr << "One or more options are out of valid range\n";
    return 1;
  }
  return RunBacktest(config);
}

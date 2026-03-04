#pragma once

#include <string>

struct FetchConfig {
  std::string market_id;    // Optional, kept for CLI compatibility.
  std::string contract_id;  // Preferred Kalshi market ticker.
  std::string api_key;      // Optional for public market data.
  std::string out_csv_path;
  long long start_ts{0};  // Unix seconds; 0 means auto default.
  long long end_ts{0};    // Unix seconds; 0 means auto default.
  int period_interval{1}; // Minutes; Kalshi supports 1/60/1440.
};

int RunFetch(const FetchConfig& config);

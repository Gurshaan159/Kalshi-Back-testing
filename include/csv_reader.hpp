#pragma once

#include <cstddef>
#include <functional>
#include <string>

struct Tick {
  std::string timestamp;
  double price{0.0};
  double bid{0.0};
  double ask{0.0};
  double volume{0.0};
  bool has_bid{false};
  bool has_ask{false};
  bool has_volume{false};
};

struct CsvReadStats {
  std::size_t rows_total{0};
  std::size_t rows_emitted{0};
  std::size_t rows_skipped{0};
};

using TickCallback = std::function<bool(const Tick&)>;

// Streams CSV rows one-by-one and invokes callback for each valid tick.
// Returns false if the file cannot be opened or parsed header is invalid.
bool StreamTicksFromCsv(const std::string& path,
                        const TickCallback& callback,
                        CsvReadStats* stats,
                        std::string* error_message);

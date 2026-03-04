#include "csv_reader.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace {

std::string Trim(const std::string& input) {
  std::size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
    ++start;
  }
  std::size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
    --end;
  }
  return input.substr(start, end - start);
}

std::vector<std::string> SplitCsvSimple(const std::string& line) {
  std::vector<std::string> out;
  std::string current;
  current.reserve(line.size());
  for (char c : line) {
    if (c == ',') {
      out.push_back(Trim(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  out.push_back(Trim(current));
  return out;
}

bool ParseDouble(const std::string& raw, double* out_value) {
  if (raw.empty()) {
    return false;
  }
  char* end_ptr = nullptr;
  const double v = std::strtod(raw.c_str(), &end_ptr);
  if (end_ptr == raw.c_str() || *end_ptr != '\0') {
    return false;
  }
  if (!std::isfinite(v)) {
    return false;
  }
  *out_value = v;
  return true;
}

}  // namespace

bool StreamTicksFromCsv(const std::string& path,
                        const TickCallback& callback,
                        CsvReadStats* stats,
                        std::string* error_message) {
  if (stats != nullptr) {
    *stats = CsvReadStats{};
  }
  if (error_message != nullptr) {
    error_message->clear();
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    if (error_message != nullptr) {
      *error_message = "Unable to open CSV: " + path;
    }
    return false;
  }

  std::string header_line;
  if (!std::getline(input, header_line)) {
    if (error_message != nullptr) {
      *error_message = "CSV is empty: " + path;
    }
    return false;
  }

  const std::vector<std::string> header_cols = SplitCsvSimple(header_line);
  std::unordered_map<std::string, std::size_t> index;
  index.reserve(header_cols.size());
  for (std::size_t i = 0; i < header_cols.size(); ++i) {
    index[header_cols[i]] = i;
  }

  if (index.find("timestamp") == index.end() || index.find("price") == index.end()) {
    if (error_message != nullptr) {
      *error_message = "CSV must contain required columns: timestamp,price";
    }
    return false;
  }

  const bool has_bid_col = index.find("bid") != index.end();
  const bool has_ask_col = index.find("ask") != index.end();
  const bool has_volume_col = index.find("volume") != index.end();
  std::string prev_timestamp;
  bool has_prev_timestamp = false;

  std::string line;
  while (std::getline(input, line)) {
    if (stats != nullptr) {
      ++stats->rows_total;
    }
    if (line.empty()) {
      if (stats != nullptr) {
        ++stats->rows_skipped;
      }
      continue;
    }

    const std::vector<std::string> cols = SplitCsvSimple(line);
    const std::size_t ts_idx = index["timestamp"];
    const std::size_t px_idx = index["price"];
    if (ts_idx >= cols.size() || px_idx >= cols.size()) {
      if (stats != nullptr) {
        ++stats->rows_skipped;
      }
      continue;
    }

    Tick tick;
    tick.timestamp = cols[ts_idx];
    if (tick.timestamp.empty()) {
      if (stats != nullptr) {
        ++stats->rows_skipped;
      }
      continue;
    }
    if (has_prev_timestamp && tick.timestamp < prev_timestamp) {
      if (stats != nullptr) {
        ++stats->rows_skipped;
      }
      continue;
    }

    if (!ParseDouble(cols[px_idx], &tick.price)) {
      if (stats != nullptr) {
        ++stats->rows_skipped;
      }
      continue;
    }

    if (has_bid_col) {
      const std::size_t idx_bid = index["bid"];
      if (idx_bid < cols.size() && ParseDouble(cols[idx_bid], &tick.bid)) {
        tick.has_bid = true;
      }
    }
    if (has_ask_col) {
      const std::size_t idx_ask = index["ask"];
      if (idx_ask < cols.size() && ParseDouble(cols[idx_ask], &tick.ask)) {
        tick.has_ask = true;
      }
    }
    if (has_volume_col) {
      const std::size_t idx_vol = index["volume"];
      if (idx_vol < cols.size() && ParseDouble(cols[idx_vol], &tick.volume)) {
        tick.has_volume = true;
      }
    }

    if (stats != nullptr) {
      ++stats->rows_emitted;
    }
    prev_timestamp = tick.timestamp;
    has_prev_timestamp = true;
    if (!callback(tick)) {
      return true;
    }
  }
  return true;
}

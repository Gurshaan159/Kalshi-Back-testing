#include "kalshi_fetch.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string Trim(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
    ++b;
  }
  std::size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
    --e;
  }
  return s.substr(b, e - b);
}

void EnsureParent(const std::string& file_path) {
  const fs::path p(file_path);
  const fs::path parent = p.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

std::string UrlEncode(const std::string& in) {
  std::ostringstream out;
  out << std::hex << std::uppercase;
  for (char ch : in) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out << static_cast<char>(c);
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return out.str();
}

bool ReadFileString(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::in);
  if (!in.is_open()) {
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  *out = buffer.str();
  return true;
}

std::size_t FindKey(const std::string& s, const std::string& key, std::size_t start = 0) {
  const std::string token = "\"" + key + "\"";
  return s.find(token, start);
}

std::size_t SkipWs(const std::string& s, std::size_t i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
    ++i;
  }
  return i;
}

bool ExtractBalanced(const std::string& s, std::size_t open_pos, char open_c, char close_c, std::string* out) {
  if (open_pos >= s.size() || s[open_pos] != open_c) {
    return false;
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = open_pos; i < s.size(); ++i) {
    const char c = s[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == open_c) {
      ++depth;
    } else if (c == close_c) {
      --depth;
      if (depth == 0) {
        *out = s.substr(open_pos, i - open_pos + 1);
        return true;
      }
    }
  }
  return false;
}

bool ExtractObjectByKey(const std::string& s, const std::string& key, std::string* out, std::size_t start = 0) {
  const std::size_t k = FindKey(s, key, start);
  if (k == std::string::npos) {
    return false;
  }
  std::size_t p = s.find(':', k);
  if (p == std::string::npos) {
    return false;
  }
  p = SkipWs(s, p + 1);
  return ExtractBalanced(s, p, '{', '}', out);
}

bool ExtractArrayByKey(const std::string& s, const std::string& key, std::string* out, std::size_t start = 0) {
  const std::size_t k = FindKey(s, key, start);
  if (k == std::string::npos) {
    return false;
  }
  std::size_t p = s.find(':', k);
  if (p == std::string::npos) {
    return false;
  }
  p = SkipWs(s, p + 1);
  return ExtractBalanced(s, p, '[', ']', out);
}

bool ExtractRawValueByKey(const std::string& obj,
                          const std::string& key,
                          std::string* raw,
                          bool* is_null,
                          std::size_t start = 0) {
  const std::size_t k = FindKey(obj, key, start);
  if (k == std::string::npos) {
    return false;
  }
  std::size_t p = obj.find(':', k);
  if (p == std::string::npos) {
    return false;
  }
  p = SkipWs(obj, p + 1);
  if (p >= obj.size()) {
    return false;
  }
  if (obj[p] == '"') {
    std::string out;
    bool escaped = false;
    for (std::size_t i = p + 1; i < obj.size(); ++i) {
      const char c = obj[i];
      if (escaped) {
        out.push_back(c);
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        *raw = out;
        *is_null = false;
        return true;
      } else {
        out.push_back(c);
      }
    }
    return false;
  }
  std::size_t e = p;
  while (e < obj.size() && obj[e] != ',' && obj[e] != '}') {
    ++e;
  }
  const std::string token = Trim(obj.substr(p, e - p));
  if (token.empty()) {
    return false;
  }
  if (token == "null") {
    *is_null = true;
    raw->clear();
    return true;
  }
  *raw = token;
  *is_null = false;
  return true;
}

bool ParseDouble(const std::string& s, double* out) {
  try {
    std::size_t idx = 0;
    const double v = std::stod(s, &idx);
    if (idx != s.size()) {
      return false;
    }
    *out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseLongLong(const std::string& s, long long* out) {
  try {
    std::size_t idx = 0;
    const long long v = std::stoll(s, &idx);
    if (idx != s.size()) {
      return false;
    }
    *out = v;
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<std::string> ExtractObjectsFromArray(const std::string& arr) {
  std::vector<std::string> out;
  if (arr.size() < 2 || arr.front() != '[' || arr.back() != ']') {
    return out;
  }
  std::size_t i = 1;
  while (i + 1 < arr.size()) {
    while (i + 1 < arr.size() && (std::isspace(static_cast<unsigned char>(arr[i])) != 0 || arr[i] == ',')) {
      ++i;
    }
    if (i + 1 >= arr.size()) {
      break;
    }
    if (arr[i] != '{') {
      ++i;
      continue;
    }
    std::string obj;
    if (!ExtractBalanced(arr, i, '{', '}', &obj)) {
      break;
    }
    out.push_back(obj);
    i += obj.size();
  }
  return out;
}

bool GetPricePointsFromObj(const std::string& obj, const std::string& base_key, double* out) {
  std::string raw;
  bool is_null = false;
  if (ExtractRawValueByKey(obj, base_key + "_dollars", &raw, &is_null) && !is_null) {
    double dollars = 0.0;
    if (ParseDouble(raw, &dollars)) {
      *out = dollars * 100.0;
      return true;
    }
  }
  if (ExtractRawValueByKey(obj, base_key, &raw, &is_null) && !is_null) {
    double points = 0.0;
    if (ParseDouble(raw, &points)) {
      *out = points;
      return true;
    }
  }
  return false;
}

bool FetchUrlToFile(const std::string& url, const std::string& api_key, const std::string& out_file) {
  std::ostringstream cmd;
  cmd << "curl.exe -sS --fail \"" << url << "\"";
  if (!api_key.empty()) {
    cmd << " -H \"KALSHI-ACCESS-KEY: " << api_key << "\"";
  }
  cmd << " -o \"" << out_file << "\"";
  const int rc = std::system(cmd.str().c_str());
  return rc == 0;
}

struct CandleCsvRow {
  long long ts{0};
  double price{0.0};
  double bid{0.0};
  double ask{0.0};
  double volume{0.0};
  bool has_bid{false};
  bool has_ask{false};
};

}  // namespace

int RunFetch(const FetchConfig& config) {
  const std::string ticker = !config.contract_id.empty() ? config.contract_id : config.market_id;
  if (ticker.empty() || config.out_csv_path.empty()) {
    std::cerr << "fetch requires --contract <ticker> and --out <path>\n";
    return 1;
  }

  long long end_ts = config.end_ts;
  long long start_ts = config.start_ts;
  if (end_ts == 0) {
    end_ts = static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
  }
  if (start_ts == 0) {
    start_ts = end_ts - 86400;  // Default 24h.
  }
  if (start_ts >= end_ts) {
    std::cerr << "Invalid fetch range: start_ts must be < end_ts\n";
    return 1;
  }

  EnsureParent(config.out_csv_path);

  const fs::path temp_path = fs::temp_directory_path() / "kalshi_fetch_candles.json";
  const std::string url =
      "https://api.elections.kalshi.com/trade-api/v2/markets/candlesticks?market_tickers=" +
      UrlEncode(ticker) + "&start_ts=" + std::to_string(start_ts) + "&end_ts=" + std::to_string(end_ts) +
      "&period_interval=" + std::to_string(config.period_interval);

  if (!FetchUrlToFile(url, config.api_key, temp_path.string())) {
    std::cerr << "Fetch failed from Kalshi endpoint.\n";
    std::cerr << "URL: " << url << "\n";
    return 1;
  }

  std::string json;
  if (!ReadFileString(temp_path.string(), &json) || json.empty()) {
    std::cerr << "Failed to read fetched JSON response\n";
    return 1;
  }

  std::string markets_arr;
  if (!ExtractArrayByKey(json, "markets", &markets_arr)) {
    std::cerr << "Unexpected response: missing markets array\n";
    return 1;
  }
  const std::vector<std::string> markets = ExtractObjectsFromArray(markets_arr);
  if (markets.empty()) {
    std::cerr << "No markets returned for ticker: " << ticker << "\n";
    return 1;
  }

  std::string candles_arr;
  if (!ExtractArrayByKey(markets[0], "candlesticks", &candles_arr)) {
    std::cerr << "No candlesticks array returned for ticker: " << ticker << "\n";
    return 1;
  }
  const std::vector<std::string> candles = ExtractObjectsFromArray(candles_arr);

  std::ofstream out(config.out_csv_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    std::cerr << "Unable to write output CSV: " << config.out_csv_path << "\n";
    return 1;
  }

  out << "timestamp,price,bid,ask,volume\n";
  std::vector<CandleCsvRow> rows;
  rows.reserve(candles.size());
  for (const std::string& candle : candles) {
    std::string raw_ts;
    bool is_null = false;
    if (!ExtractRawValueByKey(candle, "end_period_ts", &raw_ts, &is_null) || is_null) {
      continue;
    }
    long long ts = 0;
    if (!ParseLongLong(raw_ts, &ts)) {
      continue;
    }

    std::string price_obj;
    double price = 0.0;
    bool has_price = false;
    if (ExtractObjectByKey(candle, "price", &price_obj)) {
      has_price = GetPricePointsFromObj(price_obj, "close", &price);
    }

    std::string bid_obj;
    std::string ask_obj;
    double bid = 0.0;
    double ask = 0.0;
    const bool has_bid = ExtractObjectByKey(candle, "yes_bid", &bid_obj) &&
                         GetPricePointsFromObj(bid_obj, "close", &bid);
    const bool has_ask = ExtractObjectByKey(candle, "yes_ask", &ask_obj) &&
                         GetPricePointsFromObj(ask_obj, "close", &ask);

    if (!has_price && has_bid && has_ask) {
      price = (bid + ask) / 2.0;
      has_price = true;
    }
    if (!has_price) {
      continue;
    }

    std::string raw_volume;
    double volume = 0.0;
    bool has_volume = false;
    if (ExtractRawValueByKey(candle, "volume_fp", &raw_volume, &is_null) && !is_null) {
      has_volume = ParseDouble(raw_volume, &volume);
    }
    if (!has_volume && ExtractRawValueByKey(candle, "volume", &raw_volume, &is_null) && !is_null) {
      has_volume = ParseDouble(raw_volume, &volume);
    }
    if (!has_volume) {
      volume = 0.0;
    }

    rows.push_back(CandleCsvRow{
        ts,
        price,
        bid,
        ask,
        volume,
        has_bid,
        has_ask,
    });
  }

  std::sort(rows.begin(), rows.end(), [](const CandleCsvRow& a, const CandleCsvRow& b) {
    return a.ts < b.ts;
  });

  int rows_written = 0;
  for (const CandleCsvRow& r : rows) {
    out << r.ts << "," << std::fixed << std::setprecision(6) << r.price << ",";
    if (r.has_bid) {
      out << r.bid;
    }
    out << ",";
    if (r.has_ask) {
      out << r.ask;
    }
    out << "," << r.volume << "\n";
    ++rows_written;
  }

  std::cout << "Fetched ticker: " << ticker << "\n";
  std::cout << "Range: [" << start_ts << ", " << end_ts << "] period=" << config.period_interval << "m\n";
  std::cout << "Rows written: " << rows_written << "\n";
  std::cout << "Output CSV: " << config.out_csv_path << "\n";
  if (rows_written == 0) {
    std::cout << "No tradable candles in range. Try a wider time window or another ticker.\n";
  }
  return 0;
}

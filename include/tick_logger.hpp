#pragma once

#include <fstream>
#include <iomanip>
#include <string>

class TickLogger {
 public:
  explicit TickLogger(const std::string& path) : out_(path, std::ios::out | std::ios::trunc) {}

  bool IsOpen() const { return out_.is_open(); }

  void WriteHeader() {
    out_ << "timestamp,price,rolling_mean,rolling_std,spike_flag,confirmation_status,action_taken,"
            "position,cash,equity,realized_pnl,unrealized_pnl\n";
  }

  void LogRow(const std::string& timestamp,
              double price,
              double rolling_mean,
              double rolling_std,
              const std::string& spike_flag,
              const std::string& confirmation_status,
              const std::string& action_taken,
              int position,
              double cash,
              double equity,
              double realized_pnl,
              double unrealized_pnl) {
    out_ << timestamp << "," << std::fixed << std::setprecision(6) << price << "," << rolling_mean << ","
         << rolling_std << "," << spike_flag << "," << confirmation_status << "," << action_taken << ","
         << position << "," << cash << "," << equity << "," << realized_pnl << "," << unrealized_pnl
         << "\n";
  }

 private:
  std::ofstream out_;
};

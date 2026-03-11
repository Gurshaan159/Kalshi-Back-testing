#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

struct TradeRecord {
  std::string timestamp;
  std::string action;
  int qty{0};
  double price{0.0};
  double pnl{0.0};
};

struct FillResult {
  double realized_delta{0.0};
};

class Portfolio {
 public:
  explicit Portfolio(double initial_cash, bool keep_trade_history = true)
      : initial_cash_(initial_cash), cash_(initial_cash), keep_trade_history_(keep_trade_history) {}

  FillResult ApplyFill(const std::string& timestamp,
                       const std::string& action,
                       int qty_signed,
                       double fill_price,
                       double fee_per_contract,
                       TradeRecord* out_trade = nullptr) {
    FillResult result;
    if (qty_signed == 0) {
      return result;
    }

    const int qty_abs = std::abs(qty_signed);
    const double fee_cost = fee_per_contract * static_cast<double>(qty_abs);

    // Cash flow: buy decreases cash, sell increases cash.
    cash_ -= static_cast<double>(qty_signed) * fill_price;
    cash_ -= fee_cost;

    // Fees are treated as realized PnL immediately.
    realized_pnl_ -= fee_cost;
    result.realized_delta -= fee_cost;

    int remaining = qty_signed;
    if (position_qty_ == 0 || SameSide(position_qty_, qty_signed)) {
      const int new_qty = position_qty_ + qty_signed;
      const double weighted_notional =
          (avg_entry_price_ * static_cast<double>(std::abs(position_qty_))) +
          (fill_price * static_cast<double>(qty_abs));
      avg_entry_price_ = weighted_notional / static_cast<double>(std::abs(new_qty));
      position_qty_ = new_qty;
    } else {
      // Closing all or part of existing position first.
      const int closable = std::min(std::abs(position_qty_), std::abs(remaining));
      const int close_direction = position_qty_ > 0 ? 1 : -1;
      const double pnl_per_contract =
          close_direction > 0 ? (fill_price - avg_entry_price_)
                              : (avg_entry_price_ - fill_price);
      const double close_pnl = pnl_per_contract * static_cast<double>(closable);
      realized_pnl_ += close_pnl;
      result.realized_delta += close_pnl;

      position_qty_ -= close_direction * closable;
      remaining -= (-close_direction) * closable;

      if (position_qty_ == 0) {
        avg_entry_price_ = 0.0;
      }

      if (remaining != 0) {
        position_qty_ = remaining;
        avg_entry_price_ = fill_price;
      }
    }

    TradeRecord rec{timestamp, action, qty_signed, fill_price, result.realized_delta};
    if (out_trade != nullptr) {
      *out_trade = rec;
    }
    if (keep_trade_history_) {
      trades_.push_back(std::move(rec));
    }
    return result;
  }

  double UnrealizedPnl(double mark_price) const {
    if (position_qty_ == 0) {
      return 0.0;
    }
    if (position_qty_ > 0) {
      return (mark_price - avg_entry_price_) * static_cast<double>(position_qty_);
    }
    return (avg_entry_price_ - mark_price) * static_cast<double>(std::abs(position_qty_));
  }

  double Equity(double mark_price) const {
    return cash_ + (static_cast<double>(position_qty_) * mark_price);
  }

  int PositionQty() const { return position_qty_; }
  double AverageEntryPrice() const { return avg_entry_price_; }
  double Cash() const { return cash_; }
  double RealizedPnl() const { return realized_pnl_; }
  double InitialCash() const { return initial_cash_; }
  const std::vector<TradeRecord>& Trades() const { return trades_; }

 private:
  static bool SameSide(int a, int b) {
    return (a > 0 && b > 0) || (a < 0 && b < 0);
  }

  double initial_cash_{0.0};
  double cash_{0.0};
  int position_qty_{0};
  double avg_entry_price_{0.0};
  double realized_pnl_{0.0};
  bool keep_trade_history_{true};
  std::vector<TradeRecord> trades_;
};

#pragma once

#include <cmath>
#include <cstddef>

class MaxDrawdownTracker {
 public:
  void Update(double equity) {
    if (!initialized_) {
      initialized_ = true;
      peak_ = equity;
      max_drawdown_ = 0.0;
      return;
    }
    if (equity > peak_) {
      peak_ = equity;
    }
    if (peak_ > 0.0) {
      const double dd = (peak_ - equity) / peak_;
      if (dd > max_drawdown_) {
        max_drawdown_ = dd;
      }
    }
  }

  double MaxDrawdown() const { return max_drawdown_; }

 private:
  bool initialized_{false};
  double peak_{0.0};
  double max_drawdown_{0.0};
};

class SharpeTracker {
 public:
  void Update(double tick_return) {
    ++count_;
    const double delta = tick_return - mean_;
    mean_ += delta / static_cast<double>(count_);
    const double delta2 = tick_return - mean_;
    m2_ += delta * delta2;
  }

  double Mean() const { return mean_; }
  std::size_t Count() const { return count_; }

  double StdDev() const {
    if (count_ < 2) {
      return 0.0;
    }
    return std::sqrt(m2_ / static_cast<double>(count_ - 1));
  }

  double Sharpe() const {
    const double stddev = StdDev();
    if (stddev <= 0.0) {
      return 0.0;
    }
    return mean_ / stddev;
  }

 private:
  std::size_t count_{0};
  double mean_{0.0};
  double m2_{0.0};
};

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

class RollingStats {
 public:
  explicit RollingStats(std::size_t window)
      : buffer_(window, 0.0), window_(window) {}

  void Update(double value) {
    if (window_ == 0) {
      mean_ = 0.0;
      stddev_ = 0.0;
      return;
    }

    if (count_ < window_) {
      buffer_[head_] = value;
      sum_ += value;
      sumsq_ += value * value;
      ++count_;
    } else {
      const double old = buffer_[head_];
      sum_ -= old;
      sumsq_ -= old * old;
      buffer_[head_] = value;
      sum_ += value;
      sumsq_ += value * value;
    }
    head_ = (head_ + 1U) % window_;

    const std::size_t n = (count_ < window_) ? count_ : window_;
    if (n == 0) {
      mean_ = 0.0;
      stddev_ = 0.0;
      return;
    }
    mean_ = sum_ / static_cast<double>(n);
    const double variance = (sumsq_ / static_cast<double>(n)) - (mean_ * mean_);
    stddev_ = variance > 0.0 ? std::sqrt(variance) : 0.0;
  }

  bool IsReady() const { return count_ >= window_; }
  double Mean() const { return mean_; }
  double StdDev() const { return stddev_; }
  std::size_t Count() const { return count_; }

 private:
  std::vector<double> buffer_;
  std::size_t window_{0};
  std::size_t head_{0};
  std::size_t count_{0};
  double sum_{0.0};
  double sumsq_{0.0};
  double mean_{0.0};
  double stddev_{0.0};
};

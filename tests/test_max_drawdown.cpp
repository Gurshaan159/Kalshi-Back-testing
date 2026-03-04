#include "metrics.hpp"
#include "test_common.hpp"

#include <vector>

bool TestMaxDrawdown() {
  MaxDrawdownTracker tracker;
  const std::vector<double> equity{100.0, 110.0, 105.0, 90.0, 95.0, 120.0};
  for (double v : equity) {
    tracker.Update(v);
  }
  if (!NearlyEqual(tracker.MaxDrawdown(), 20.0 / 110.0, 1e-6)) {
    return ReportFailure("TestMaxDrawdown", "max drawdown mismatch");
  }
  return true;
}

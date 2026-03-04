#include "rolling_stats.hpp"
#include "test_common.hpp"

bool TestRollingStats() {
  RollingStats stats(3);
  stats.Update(1.0);
  stats.Update(2.0);
  stats.Update(3.0);
  if (!stats.IsReady()) {
    return ReportFailure("TestRollingStats", "window should be ready after 3 updates");
  }
  if (!NearlyEqual(stats.Mean(), 2.0)) {
    return ReportFailure("TestRollingStats", "mean mismatch");
  }
  const double expected_std = std::sqrt(2.0 / 3.0);
  if (!NearlyEqual(stats.StdDev(), expected_std, 1e-5)) {
    return ReportFailure("TestRollingStats", "stddev mismatch");
  }
  return true;
}

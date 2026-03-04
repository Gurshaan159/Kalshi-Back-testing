#include "rolling_stats.hpp"
#include "test_common.hpp"

#include <vector>

bool TestSpikeDetection() {
  RollingStats stats(5);
  const std::vector<double> data{50.0, 50.1, 49.9, 50.0, 50.0, 56.0, 53.0};
  bool spike_found = false;
  bool confirmed = false;
  double spike_price = 0.0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    const double price = data[i];
    const bool ready_before = stats.IsReady();
    const double mean_before = stats.Mean();
    const double std_before = stats.StdDev();
    if (!spike_found && ready_before && std_before > 0.0) {
      const double upper = mean_before + 2.5 * std_before;
      if (price > upper) {
        spike_found = true;
        spike_price = price;
      }
    } else if (spike_found) {
      confirmed = (price < spike_price);
      break;
    }
    stats.Update(price);
  }
  if (!spike_found || !confirmed) {
    return ReportFailure("TestSpikeDetection", "failed synthetic spike confirmation");
  }
  return true;
}

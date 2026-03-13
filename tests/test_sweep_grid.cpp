#include "sweep.hpp"
#include "test_common.hpp"

bool TestSweepGridRawCount() {
  const std::vector<SweepParameterSet> combos =
      GenerateSweepCombinations(SweepGridConfig{}, SweepCombinationFilter{});
  if (combos.size() != 108U) {
    return ReportFailure("TestSweepGridRawCount", "expected exactly 108 cartesian combinations");
  }
  return true;
}

bool TestSweepGridFilterBehavior() {
  const SweepCombinationFilter filter = [](const SweepParameterSet& params) {
    return params.position_size == 4 && params.max_hold_ticks == 150;
  };
  const std::vector<SweepParameterSet> combos = GenerateSweepCombinations(SweepGridConfig{}, filter);
  if (combos.size() != 12U) {
    return ReportFailure("TestSweepGridFilterBehavior", "unexpected filtered combination count");
  }
  for (const SweepParameterSet& params : combos) {
    if (params.position_size != 4 || params.max_hold_ticks != 150) {
      return ReportFailure("TestSweepGridFilterBehavior", "filter allowed invalid combo");
    }
  }
  return true;
}

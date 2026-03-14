#include <iostream>

bool TestRollingStats();
bool TestSpikeDetection();
bool TestPortfolioPartialExits();
bool TestMaxDrawdown();
bool TestBuyUsesAskPlusSlippage();
bool TestSellUsesBidMinusSlippage();
bool TestSpreadRejection();
bool TestVolumeBasedPartialFill();
bool TestFallbackToTickPriceWhenQuotesMissing();
bool TestOversizedOrderRejectedWhenPartialsDisabled();
bool TestSweepGridRawCount();
bool TestSweepGridFilterBehavior();
bool TestSweepRankingLogic();
bool TestSweepSensitivityAggregation();
bool TestSweepInteractionAggregation();
bool TestRestingOrderLifecycle();
bool TestPartialFillsOverTime();
bool TestExpiryBehavior();
bool TestImmediateFillCompatibility();
bool TestRestingOrderMetrics();

int main() {
  if (!TestRollingStats()) {
    return 1;
  }
  if (!TestSpikeDetection()) {
    return 1;
  }
  if (!TestPortfolioPartialExits()) {
    return 1;
  }
  if (!TestMaxDrawdown()) {
    return 1;
  }
  if (!TestBuyUsesAskPlusSlippage()) {
    return 1;
  }
  if (!TestSellUsesBidMinusSlippage()) {
    return 1;
  }
  if (!TestSpreadRejection()) {
    return 1;
  }
  if (!TestVolumeBasedPartialFill()) {
    return 1;
  }
  if (!TestFallbackToTickPriceWhenQuotesMissing()) {
    return 1;
  }
  if (!TestOversizedOrderRejectedWhenPartialsDisabled()) {
    return 1;
  }
  if (!TestSweepGridRawCount()) {
    return 1;
  }
  if (!TestSweepGridFilterBehavior()) {
    return 1;
  }
  if (!TestSweepRankingLogic()) {
    return 1;
  }
  if (!TestSweepSensitivityAggregation()) {
    return 1;
  }
  if (!TestSweepInteractionAggregation()) {
    return 1;
  }
  if (!TestRestingOrderLifecycle()) {
    return 1;
  }
  if (!TestPartialFillsOverTime()) {
    return 1;
  }
  if (!TestExpiryBehavior()) {
    return 1;
  }
  if (!TestImmediateFillCompatibility()) {
    return 1;
  }
  if (!TestRestingOrderMetrics()) {
    return 1;
  }
  std::cout << "All tests passed\n";
  return 0;
}

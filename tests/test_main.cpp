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
  std::cout << "All tests passed\n";
  return 0;
}

#include <iostream>

bool TestRollingStats();
bool TestSpikeDetection();
bool TestPortfolioPartialExits();
bool TestMaxDrawdown();

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
  std::cout << "All tests passed\n";
  return 0;
}

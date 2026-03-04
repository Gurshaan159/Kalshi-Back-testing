#include "portfolio.hpp"
#include "test_common.hpp"

bool TestPortfolioPartialExits() {
  Portfolio p(1000.0);
  p.ApplyFill("t0", "entry", 3, 50.0, 0.0, 0.0);
  p.ApplyFill("t1", "exit", -1, 55.0, 0.0, 0.0);
  p.ApplyFill("t2", "exit", -2, 60.0, 0.0, 0.0);
  if (p.PositionQty() != 0) {
    return ReportFailure("TestPortfolioPartialExits", "position should be flat after exits");
  }
  if (!NearlyEqual(p.RealizedPnl(), 25.0)) {
    return ReportFailure("TestPortfolioPartialExits", "realized PnL mismatch");
  }
  return true;
}

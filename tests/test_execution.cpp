#include "execution.hpp"
#include "test_common.hpp"

namespace {

BacktestConfig BaseConfig() {
  BacktestConfig cfg;
  cfg.slippage_points = 0.5;
  cfg.max_spread_points = 2.0;
  cfg.volume_fill_ratio = 0.25;
  cfg.allow_partial_fills = true;
  cfg.use_quotes_for_fills = true;
  return cfg;
}

}  // namespace

bool TestBuyUsesAskPlusSlippage() {
  BacktestConfig cfg = BaseConfig();
  Tick tick;
  tick.timestamp = "t1";
  tick.price = 50.0;
  tick.ask = 51.0;
  tick.has_ask = true;
  OrderRequest order{"t1", "entry", 4};

  const ExecutionResult r = SimulateFill(tick, order, cfg);
  if (!r.did_fill) {
    return ReportFailure("TestBuyUsesAskPlusSlippage", "expected fill");
  }
  if (r.filled_qty_signed != 4) {
    return ReportFailure("TestBuyUsesAskPlusSlippage", "unexpected fill quantity");
  }
  if (!NearlyEqual(r.fill_price, 51.5)) {
    return ReportFailure("TestBuyUsesAskPlusSlippage", "buy fill price should be ask + slippage");
  }
  return true;
}

bool TestSellUsesBidMinusSlippage() {
  BacktestConfig cfg = BaseConfig();
  Tick tick;
  tick.timestamp = "t1";
  tick.price = 50.0;
  tick.bid = 49.0;
  tick.has_bid = true;
  OrderRequest order{"t1", "exit", -3};

  const ExecutionResult r = SimulateFill(tick, order, cfg);
  if (!r.did_fill) {
    return ReportFailure("TestSellUsesBidMinusSlippage", "expected fill");
  }
  if (r.filled_qty_signed != -3) {
    return ReportFailure("TestSellUsesBidMinusSlippage", "unexpected fill quantity");
  }
  if (!NearlyEqual(r.fill_price, 48.5)) {
    return ReportFailure("TestSellUsesBidMinusSlippage", "sell fill price should be bid - slippage");
  }
  return true;
}

bool TestSpreadRejection() {
  BacktestConfig cfg = BaseConfig();
  cfg.max_spread_points = 1.0;

  Tick tick;
  tick.timestamp = "t1";
  tick.price = 50.0;
  tick.bid = 48.0;
  tick.ask = 50.0;
  tick.has_bid = true;
  tick.has_ask = true;
  OrderRequest order{"t1", "entry", 2};

  const ExecutionResult r = SimulateFill(tick, order, cfg);
  if (r.did_fill) {
    return ReportFailure("TestSpreadRejection", "expected fill rejection for wide spread");
  }
  return true;
}

bool TestVolumeBasedPartialFill() {
  BacktestConfig cfg = BaseConfig();
  cfg.volume_fill_ratio = 0.25;
  cfg.allow_partial_fills = true;

  Tick tick;
  tick.timestamp = "t1";
  tick.price = 10.0;
  tick.volume = 10.0;  // floor(10 * 0.25) = 2
  tick.has_volume = true;
  OrderRequest order{"t1", "entry", 5};

  const ExecutionResult r = SimulateFill(tick, order, cfg);
  if (!r.did_fill) {
    return ReportFailure("TestVolumeBasedPartialFill", "expected partial fill");
  }
  if (r.filled_qty_signed != 2) {
    return ReportFailure("TestVolumeBasedPartialFill", "partial fill quantity mismatch");
  }
  if (!r.was_partial) {
    return ReportFailure("TestVolumeBasedPartialFill", "expected was_partial=true");
  }
  return true;
}

bool TestFallbackToTickPriceWhenQuotesMissing() {
  BacktestConfig cfg = BaseConfig();

  Tick tick;
  tick.timestamp = "t1";
  tick.price = 42.0;
  OrderRequest order{"t1", "entry", 1};

  const ExecutionResult r = SimulateFill(tick, order, cfg);
  if (!r.did_fill) {
    return ReportFailure("TestFallbackToTickPriceWhenQuotesMissing", "expected fill");
  }
  if (!NearlyEqual(r.fill_price, 42.5)) {
    return ReportFailure("TestFallbackToTickPriceWhenQuotesMissing", "expected tick.price fallback");
  }
  return true;
}

bool TestOversizedOrderRejectedWhenPartialsDisabled() {
  BacktestConfig cfg = BaseConfig();
  cfg.allow_partial_fills = false;
  cfg.volume_fill_ratio = 0.25;

  Tick tick;
  tick.timestamp = "t1";
  tick.price = 10.0;
  tick.volume = 10.0;  // cap = 2
  tick.has_volume = true;
  OrderRequest order{"t1", "entry", 5};

  const ExecutionResult r = SimulateFill(tick, order, cfg);
  if (r.did_fill) {
    return ReportFailure("TestOversizedOrderRejectedWhenPartialsDisabled", "expected rejection");
  }
  return true;
}

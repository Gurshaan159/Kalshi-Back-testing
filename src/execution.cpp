#include "execution.hpp"

#include <cmath>

namespace {

int SignOf(int v) {
  if (v > 0) {
    return 1;
  }
  if (v < 0) {
    return -1;
  }
  return 0;
}

}  // namespace

ExecutionResult SimulateFill(const Tick& tick, const OrderRequest& order, const BacktestConfig& config) {
  ExecutionResult result;
  if (order.qty_signed == 0) {
    return result;
  }

  if (tick.has_bid && tick.has_ask) {
    const double spread = tick.ask - tick.bid;
    if (spread > config.max_spread_points) {
      return result;
    }
  }

  const int side = SignOf(order.qty_signed);
  const int requested_abs = std::abs(order.qty_signed);

  int fillable_abs = requested_abs;
  if (tick.has_volume) {
    const double capped = std::floor(tick.volume * config.volume_fill_ratio);
    if (capped < 1.0) {
      return result;
    }
    fillable_abs = std::min(fillable_abs, static_cast<int>(capped));
  }

  bool was_partial = false;
  if (fillable_abs < requested_abs) {
    if (!config.allow_partial_fills) {
      return result;
    }
    was_partial = true;
  }

  double base_price = tick.price;
  if (config.use_quotes_for_fills) {
    if (side > 0 && tick.has_ask) {
      base_price = tick.ask;
    } else if (side < 0 && tick.has_bid) {
      base_price = tick.bid;
    }
  }
  const double fill_price = base_price + static_cast<double>(side) * config.slippage_points;

  result.did_fill = true;
  result.filled_qty_signed = side * fillable_abs;
  result.fill_price = fill_price;
  result.was_partial = was_partial;
  return result;
}

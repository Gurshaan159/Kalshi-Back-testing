#pragma once

#include "csv_reader.hpp"
#include "engine.hpp"

#include <string>

struct OrderRequest {
  std::string timestamp;
  std::string action;
  int qty_signed{0};
};

struct ExecutionResult {
  bool did_fill{false};
  int filled_qty_signed{0};
  double fill_price{0.0};
  bool was_partial{false};
};

ExecutionResult SimulateFill(const Tick& tick, const OrderRequest& order, const BacktestConfig& config);

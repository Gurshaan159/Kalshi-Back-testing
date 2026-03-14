#pragma once

#include <cstddef>
#include <string>

enum class RestingOrderStatus { Active, PartiallyFilled, Filled, Expired, Canceled };

enum class RestingOrderIntent { Entry, Exit };

struct RestingOrder {
  std::string order_id;
  RestingOrderIntent intent;
  std::string action;  // "entry", "exit", "stop_loss", "max_hold"
  int desired_qty_signed;
  int remaining_qty_signed;
  std::size_t submit_tick_index;
  std::size_t expiry_tick_index;
  std::string submit_timestamp;
  double cumulative_filled_qty{0};
  double avg_fill_price{0};
  RestingOrderStatus status{RestingOrderStatus::Active};
  int fill_delay_ticks_sum{0};
  int fill_event_count{0};
};

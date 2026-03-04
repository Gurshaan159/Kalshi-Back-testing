#pragma once

struct StrategyParams {
  int rolling_window{50};
  double spike_threshold{2.5};
  int position_size{3};
  double stop_loss_points{3.0};
  int max_hold_ticks{100};
};

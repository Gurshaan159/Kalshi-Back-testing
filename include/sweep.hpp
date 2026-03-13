#pragma once

#include "engine.hpp"

#include <functional>
#include <string>
#include <vector>

struct SweepParameterSet {
  double spike_threshold{0.0};
  int position_size{0};
  double stop_loss_points{0.0};
  int max_hold_ticks{0};
};

struct SweepGridConfig {
  std::vector<double> spike_threshold_values{2.0, 2.5, 3.0, 3.5};
  std::vector<int> position_size_values{1, 2, 4};
  std::vector<double> stop_loss_points_values{1.5, 3.0, 4.5};
  std::vector<int> max_hold_ticks_values{50, 100, 150};
};

using SweepCombinationFilter = std::function<bool(const SweepParameterSet&)>;

struct SweepRunSpec {
  std::string run_id;
  SweepParameterSet params;
  BacktestConfig config;
};

struct SweepRunOptions {
  std::string sweep_id;
  std::string out_root_dir{"out/sweeps"};
  std::string log_root_dir{"logs/sweeps"};
  int concurrency{0};
  bool write_per_run_outputs{true};
  bool print_progress{true};
  bool force_sharpe{true};
};

struct ParameterValueSummary {
  double value{0.0};
  int count{0};
  double avg_total_pnl{0.0};
  double avg_sharpe{0.0};
  double avg_max_drawdown{0.0};
  double avg_win_rate{0.0};
  double avg_trade_count{0.0};
  double min_total_pnl{0.0};
  double max_total_pnl{0.0};
  double min_sharpe{0.0};
  double max_sharpe{0.0};
};

struct SensitivitySummary {
  std::string parameter_name;
  std::vector<ParameterValueSummary> buckets;
};

struct PairwiseBucketSummary {
  double first_value{0.0};
  double second_value{0.0};
  int count{0};
  double avg_total_pnl{0.0};
  double avg_sharpe{0.0};
  double avg_max_drawdown{0.0};
  double avg_trade_count{0.0};
};

struct PairwiseSummary {
  std::string first_parameter;
  std::string second_parameter;
  std::vector<PairwiseBucketSummary> buckets;
};

struct SweepAnalysis {
  std::vector<BacktestRunResult> eligible_ranked_by_sharpe;
  std::vector<BacktestRunResult> top10_by_sharpe;
  std::vector<BacktestRunResult> top10_by_total_pnl;
  std::vector<BacktestRunResult> top10_by_lowest_drawdown_profitable;
  std::vector<SensitivitySummary> sensitivities;
  std::vector<PairwiseSummary> pairwise_summaries;
};

struct SweepBatchResult {
  bool success{false};
  std::string error_message;
  std::string sweep_id;
  std::string batch_output_dir;
  std::string batch_log_dir;
  int total_generated{0};
  int total_filtered_out{0};
  int total_executed{0};
  int concurrency_used{0};
  std::vector<BacktestRunResult> runs;
  SweepAnalysis analysis;
};

bool IsValidSweepCombination(const SweepParameterSet& params);

std::vector<SweepParameterSet> GenerateSweepCombinations(const SweepGridConfig& grid,
                                                         const SweepCombinationFilter& filter);

std::vector<SweepRunSpec> BuildSweepRunSpecs(const BacktestConfig& base_config,
                                             const SweepGridConfig& grid,
                                             const SweepCombinationFilter& filter);

std::vector<BacktestRunResult> RankEligibleBySharpe(const std::vector<BacktestRunResult>& runs);

SweepAnalysis AnalyzeSweepRuns(const std::vector<BacktestRunResult>& runs);

SweepBatchResult RunSweep(const BacktestConfig& base_config,
                          const SweepRunOptions& options,
                          const SweepGridConfig& grid,
                          const SweepCombinationFilter& filter);

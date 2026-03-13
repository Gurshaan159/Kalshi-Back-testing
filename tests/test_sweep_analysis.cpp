#include "sweep.hpp"
#include "test_common.hpp"

#include <vector>

namespace {

BacktestRunResult MakeSyntheticRun(const char* run_id,
                                   double spike_threshold,
                                   int position_size,
                                   double stop_loss_points,
                                   int max_hold_ticks,
                                   double total_pnl,
                                   double sharpe,
                                   double max_drawdown,
                                   int trade_count,
                                   bool success = true) {
  BacktestRunResult run;
  run.run_id = run_id;
  run.success = success;
  run.config_used.spike_threshold = spike_threshold;
  run.config_used.position_size = position_size;
  run.config_used.stop_loss_points = stop_loss_points;
  run.config_used.max_hold_ticks = max_hold_ticks;
  run.metrics.total_pnl = total_pnl;
  run.metrics.sharpe = sharpe;
  run.metrics.max_drawdown = max_drawdown;
  run.metrics.trade_count = trade_count;
  run.metrics.win_rate = 0.5;
  run.metrics.avg_trade_pnl = trade_count > 0 ? total_pnl / static_cast<double>(trade_count) : 0.0;
  return run;
}

}  // namespace

bool TestSweepRankingLogic() {
  std::vector<BacktestRunResult> runs;
  runs.push_back(MakeSyntheticRun("run-0001", 2.0, 1, 1.5, 50, 100.0, 1.0, 0.30, 12));
  runs.push_back(MakeSyntheticRun("run-0002", 2.0, 1, 1.5, 100, 130.0, 1.0, 0.20, 12));
  runs.push_back(MakeSyntheticRun("run-0003", 2.0, 2, 1.5, 100, 200.0, 0.9, 0.10, 15));
  runs.push_back(MakeSyntheticRun("run-0004", 3.5, 4, 4.5, 150, -20.0, 2.0, 0.05, 20));  // ineligible by pnl
  runs.push_back(MakeSyntheticRun("run-0005", 2.5, 2, 3.0, 100, 50.0, 5.0, 0.10, 5));     // ineligible by trades

  const std::vector<BacktestRunResult> ranked = RankEligibleBySharpe(runs);
  if (ranked.size() != 3U) {
    return ReportFailure("TestSweepRankingLogic", "expected 3 eligible runs");
  }
  if (ranked[0].run_id != "run-0002") {
    return ReportFailure("TestSweepRankingLogic", "tie-break by drawdown failed");
  }
  if (ranked[1].run_id != "run-0001" || ranked[2].run_id != "run-0003") {
    return ReportFailure("TestSweepRankingLogic", "ranking order mismatch");
  }
  return true;
}

bool TestSweepSensitivityAggregation() {
  std::vector<BacktestRunResult> runs;
  runs.push_back(MakeSyntheticRun("run-0001", 2.0, 1, 1.5, 50, 10.0, 1.0, 0.10, 12));
  runs.push_back(MakeSyntheticRun("run-0002", 2.0, 2, 3.0, 100, 30.0, 3.0, 0.30, 12));
  runs.push_back(MakeSyntheticRun("run-0003", 3.5, 2, 3.0, 150, 50.0, 5.0, 0.20, 12));

  const SweepAnalysis analysis = AnalyzeSweepRuns(runs);
  const SensitivitySummary* spike_summary = nullptr;
  for (const SensitivitySummary& summary : analysis.sensitivities) {
    if (summary.parameter_name == "spike_threshold") {
      spike_summary = &summary;
      break;
    }
  }
  if (spike_summary == nullptr) {
    return ReportFailure("TestSweepSensitivityAggregation", "missing spike_threshold summary");
  }
  if (spike_summary->buckets.size() != 2U) {
    return ReportFailure("TestSweepSensitivityAggregation", "unexpected bucket count");
  }
  if (!NearlyEqual(spike_summary->buckets[0].value, 2.0) || spike_summary->buckets[0].count != 2) {
    return ReportFailure("TestSweepSensitivityAggregation", "bucket shape mismatch for spike=2.0");
  }
  if (!NearlyEqual(spike_summary->buckets[0].avg_total_pnl, 20.0)) {
    return ReportFailure("TestSweepSensitivityAggregation", "avg total pnl mismatch");
  }
  return true;
}

bool TestSweepInteractionAggregation() {
  std::vector<BacktestRunResult> runs;
  runs.push_back(MakeSyntheticRun("run-0001", 2.0, 1, 1.5, 50, 10.0, 1.0, 0.10, 12));
  runs.push_back(MakeSyntheticRun("run-0002", 2.0, 1, 1.5, 50, 30.0, 3.0, 0.30, 12));
  runs.push_back(MakeSyntheticRun("run-0003", 3.5, 4, 4.5, 150, 50.0, 5.0, 0.20, 12));

  const SweepAnalysis analysis = AnalyzeSweepRuns(runs);
  const PairwiseSummary* pair = nullptr;
  for (const PairwiseSummary& summary : analysis.pairwise_summaries) {
    if (summary.first_parameter == "spike_threshold" && summary.second_parameter == "position_size") {
      pair = &summary;
      break;
    }
  }
  if (pair == nullptr) {
    return ReportFailure("TestSweepInteractionAggregation", "missing pairwise summary");
  }
  if (pair->buckets.size() != 2U) {
    return ReportFailure("TestSweepInteractionAggregation", "unexpected pairwise bucket count");
  }
  if (!NearlyEqual(pair->buckets[0].first_value, 2.0) || !NearlyEqual(pair->buckets[0].second_value, 1.0)) {
    return ReportFailure("TestSweepInteractionAggregation", "unexpected first pair bucket key");
  }
  if (pair->buckets[0].count != 2 || !NearlyEqual(pair->buckets[0].avg_total_pnl, 20.0)) {
    return ReportFailure("TestSweepInteractionAggregation", "pairwise aggregation mismatch");
  }
  return true;
}

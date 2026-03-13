#include "sweep.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct AggregateState {
  int count{0};
  double sum_total_pnl{0.0};
  double sum_sharpe{0.0};
  double sum_max_drawdown{0.0};
  double sum_win_rate{0.0};
  double sum_trade_count{0.0};
  double min_total_pnl{std::numeric_limits<double>::infinity()};
  double max_total_pnl{-std::numeric_limits<double>::infinity()};
  double min_sharpe{std::numeric_limits<double>::infinity()};
  double max_sharpe{-std::numeric_limits<double>::infinity()};
};

std::string MakeRunId(int one_based_index) {
  std::ostringstream oss;
  oss << "run-" << std::setfill('0') << std::setw(4) << one_based_index;
  return oss.str();
}

std::string MakeSweepId() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t ts = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &ts);
#else
  localtime_r(&ts, &tm_buf);
#endif
  std::ostringstream oss;
  oss << "sweep-" << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
  return oss.str();
}

int ResolveConcurrency(int requested) {
  if (requested > 0) {
    return requested;
  }
  const unsigned int hw = std::thread::hardware_concurrency();
  const int detected = hw == 0U ? 1 : static_cast<int>(hw);
  return std::max(1, std::min(8, detected));
}

void UpdateAggregate(AggregateState* agg, const BacktestRunMetrics& metrics) {
  ++agg->count;
  agg->sum_total_pnl += metrics.total_pnl;
  agg->sum_sharpe += metrics.sharpe;
  agg->sum_max_drawdown += metrics.max_drawdown;
  agg->sum_win_rate += metrics.win_rate;
  agg->sum_trade_count += static_cast<double>(metrics.trade_count);
  agg->min_total_pnl = std::min(agg->min_total_pnl, metrics.total_pnl);
  agg->max_total_pnl = std::max(agg->max_total_pnl, metrics.total_pnl);
  agg->min_sharpe = std::min(agg->min_sharpe, metrics.sharpe);
  agg->max_sharpe = std::max(agg->max_sharpe, metrics.sharpe);
}

ParameterValueSummary FinalizeAggregate(double value, const AggregateState& agg) {
  ParameterValueSummary out;
  out.value = value;
  out.count = agg.count;
  if (agg.count <= 0) {
    return out;
  }
  const double denom = static_cast<double>(agg.count);
  out.avg_total_pnl = agg.sum_total_pnl / denom;
  out.avg_sharpe = agg.sum_sharpe / denom;
  out.avg_max_drawdown = agg.sum_max_drawdown / denom;
  out.avg_win_rate = agg.sum_win_rate / denom;
  out.avg_trade_count = agg.sum_trade_count / denom;
  out.min_total_pnl = agg.min_total_pnl;
  out.max_total_pnl = agg.max_total_pnl;
  out.min_sharpe = agg.min_sharpe;
  out.max_sharpe = agg.max_sharpe;
  return out;
}

PairwiseBucketSummary FinalizePairwise(double first, double second, const AggregateState& agg) {
  PairwiseBucketSummary out;
  out.first_value = first;
  out.second_value = second;
  out.count = agg.count;
  if (agg.count <= 0) {
    return out;
  }
  const double denom = static_cast<double>(agg.count);
  out.avg_total_pnl = agg.sum_total_pnl / denom;
  out.avg_sharpe = agg.sum_sharpe / denom;
  out.avg_max_drawdown = agg.sum_max_drawdown / denom;
  out.avg_trade_count = agg.sum_trade_count / denom;
  return out;
}

bool IsSuccessful(const BacktestRunResult& run) { return run.success; }

bool IsEligibleForPrimaryRank(const BacktestRunResult& run) {
  return run.success && run.metrics.trade_count >= 10 && run.metrics.total_pnl > 0.0;
}

bool SharpeRankComparator(const BacktestRunResult& a, const BacktestRunResult& b) {
  if (a.metrics.sharpe != b.metrics.sharpe) {
    return a.metrics.sharpe > b.metrics.sharpe;
  }
  if (a.metrics.max_drawdown != b.metrics.max_drawdown) {
    return a.metrics.max_drawdown < b.metrics.max_drawdown;
  }
  if (a.metrics.total_pnl != b.metrics.total_pnl) {
    return a.metrics.total_pnl > b.metrics.total_pnl;
  }
  return a.run_id < b.run_id;
}

std::vector<BacktestRunResult> TopN(std::vector<BacktestRunResult> runs, std::size_t n) {
  if (runs.size() > n) {
    runs.resize(n);
  }
  return runs;
}

bool WriteRunResultJson(const std::string& path, const BacktestRunResult& run, std::string* error) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Unable to write run result: " + path;
    }
    return false;
  }
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"run_id\": \"" << run.run_id << "\",\n";
  out << "  \"status\": \"" << (run.success ? "ok" : "failed") << "\",\n";
  out << "  \"error\": \"" << run.error_message << "\",\n";
  out << "  \"duration_ms\": " << run.duration_ms << ",\n";
  out << "  \"params\": {\n";
  out << "    \"spike_threshold\": " << run.config_used.spike_threshold << ",\n";
  out << "    \"position_size\": " << run.config_used.position_size << ",\n";
  out << "    \"stop_loss_points\": " << run.config_used.stop_loss_points << ",\n";
  out << "    \"max_hold_ticks\": " << run.config_used.max_hold_ticks << "\n";
  out << "  },\n";
  out << "  \"metrics\": {\n";
  out << "    \"final_equity\": " << run.metrics.final_equity << ",\n";
  out << "    \"total_pnl\": " << run.metrics.total_pnl << ",\n";
  out << "    \"total_return\": " << run.metrics.total_return << ",\n";
  out << "    \"trade_count\": " << run.metrics.trade_count << ",\n";
  out << "    \"win_rate\": " << run.metrics.win_rate << ",\n";
  out << "    \"avg_trade_pnl\": " << run.metrics.avg_trade_pnl << ",\n";
  out << "    \"max_drawdown\": " << run.metrics.max_drawdown << ",\n";
  out << "    \"sharpe\": " << run.metrics.sharpe << ",\n";
  out << "    \"fill_attempts\": " << run.metrics.fill_attempts << ",\n";
  out << "    \"fill_rejections\": " << run.metrics.fill_rejections << ",\n";
  out << "    \"partial_fill_count\": " << run.metrics.partial_fill_count << ",\n";
  out << "    \"requested_contracts\": " << run.metrics.requested_contracts << ",\n";
  out << "    \"filled_contracts\": " << run.metrics.filled_contracts << "\n";
  out << "  },\n";
  out << "  \"csv_stats\": {\n";
  out << "    \"rows_total\": " << run.csv_stats.rows_total << ",\n";
  out << "    \"rows_emitted\": " << run.csv_stats.rows_emitted << ",\n";
  out << "    \"rows_skipped\": " << run.csv_stats.rows_skipped << "\n";
  out << "  }\n";
  out << "}\n";
  return true;
}

bool WriteConfigJson(const std::string& path,
                     const SweepRunOptions& options,
                     const SweepGridConfig& grid,
                     const SweepBatchResult& batch,
                     std::string* error) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Unable to write config.json: " + path;
    }
    return false;
  }
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"sweep_id\": \"" << batch.sweep_id << "\",\n";
  out << "  \"concurrency\": " << batch.concurrency_used << ",\n";
  out << "  \"raw_combinations\": " << batch.total_generated << ",\n";
  out << "  \"filtered_out\": " << batch.total_filtered_out << ",\n";
  out << "  \"executed\": " << batch.total_executed << ",\n";
  out << "  \"write_per_run_outputs\": " << (options.write_per_run_outputs ? "true" : "false") << ",\n";
  out << "  \"grid\": {\n";
  out << "    \"spike_threshold\": [2.0, 2.5, 3.0, 3.5],\n";
  out << "    \"position_size\": [1, 2, 4],\n";
  out << "    \"stop_loss_points\": [1.5, 3.0, 4.5],\n";
  out << "    \"max_hold_ticks\": [50, 100, 150]\n";
  out << "  }\n";
  out << "}\n";
  (void)grid;
  return true;
}

bool WriteSummaryCsv(const std::string& path,
                     const std::vector<BacktestRunResult>& runs,
                     std::string* error) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Unable to write summary.csv: " + path;
    }
    return false;
  }
  out << "run_id,status,error,duration_ms,spike_threshold,position_size,stop_loss_points,max_hold_ticks,";
  out << "total_pnl,total_return,final_equity,trade_count,win_rate,avg_trade_pnl,max_drawdown,sharpe,";
  out << "fill_attempts,fill_rejections,partial_fill_count,requested_contracts,filled_contracts\n";
  out << std::fixed << std::setprecision(6);
  for (const BacktestRunResult& run : runs) {
    out << run.run_id << "," << (run.success ? "ok" : "failed") << "," << run.error_message << ","
        << run.duration_ms << "," << run.config_used.spike_threshold << "," << run.config_used.position_size << ","
        << run.config_used.stop_loss_points << "," << run.config_used.max_hold_ticks << ","
        << run.metrics.total_pnl << "," << run.metrics.total_return << "," << run.metrics.final_equity << ","
        << run.metrics.trade_count << "," << run.metrics.win_rate << "," << run.metrics.avg_trade_pnl << ","
        << run.metrics.max_drawdown << "," << run.metrics.sharpe << "," << run.metrics.fill_attempts << ","
        << run.metrics.fill_rejections << "," << run.metrics.partial_fill_count << ","
        << run.metrics.requested_contracts << "," << run.metrics.filled_contracts << "\n";
  }
  return true;
}

void WriteTopList(std::ostream& out, const std::string& title, const std::vector<BacktestRunResult>& runs) {
  out << "## " << title << "\n\n";
  out << "| run_id | spike_threshold | position_size | stop_loss_points | max_hold_ticks | total_pnl | sharpe | max_drawdown | trade_count |\n";
  out << "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
  out << std::fixed << std::setprecision(6);
  for (const BacktestRunResult& run : runs) {
    out << "| " << run.run_id << " | " << run.config_used.spike_threshold << " | " << run.config_used.position_size
        << " | " << run.config_used.stop_loss_points << " | " << run.config_used.max_hold_ticks << " | "
        << run.metrics.total_pnl << " | " << run.metrics.sharpe << " | " << run.metrics.max_drawdown << " | "
        << run.metrics.trade_count << " |\n";
  }
  out << "\n";
}

void WriteSensitivitySection(std::ostream& out, const SensitivitySummary& summary) {
  out << "### " << summary.parameter_name << "\n\n";
  out << "| value | count | avg_total_pnl | avg_sharpe | avg_max_drawdown | avg_win_rate | avg_trade_count | min_total_pnl | max_total_pnl | min_sharpe | max_sharpe |\n";
  out << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
  out << std::fixed << std::setprecision(6);
  for (const ParameterValueSummary& bucket : summary.buckets) {
    out << "| " << bucket.value << " | " << bucket.count << " | " << bucket.avg_total_pnl << " | "
        << bucket.avg_sharpe << " | " << bucket.avg_max_drawdown << " | " << bucket.avg_win_rate << " | "
        << bucket.avg_trade_count << " | " << bucket.min_total_pnl << " | " << bucket.max_total_pnl << " | "
        << bucket.min_sharpe << " | " << bucket.max_sharpe << " |\n";
  }
  out << "\n";
}

void WritePairwiseSection(std::ostream& out, const PairwiseSummary& summary) {
  out << "### " << summary.first_parameter << " x " << summary.second_parameter << "\n\n";
  out << "| " << summary.first_parameter << " | " << summary.second_parameter
      << " | count | avg_total_pnl | avg_sharpe | avg_max_drawdown | avg_trade_count |\n";
  out << "|---:|---:|---:|---:|---:|---:|---:|\n";
  out << std::fixed << std::setprecision(6);
  for (const PairwiseBucketSummary& bucket : summary.buckets) {
    out << "| " << bucket.first_value << " | " << bucket.second_value << " | " << bucket.count << " | "
        << bucket.avg_total_pnl << " | " << bucket.avg_sharpe << " | " << bucket.avg_max_drawdown << " | "
        << bucket.avg_trade_count << " |\n";
  }
  out << "\n";
}

double GetAvgByValue(const SensitivitySummary& summary, double value, const std::string& metric_name) {
  for (const ParameterValueSummary& bucket : summary.buckets) {
    if (std::fabs(bucket.value - value) < 1e-9) {
      if (metric_name == "trade_count") {
        return bucket.avg_trade_count;
      }
      if (metric_name == "sharpe") {
        return bucket.avg_sharpe;
      }
      if (metric_name == "total_pnl") {
        return bucket.avg_total_pnl;
      }
      if (metric_name == "max_drawdown") {
        return bucket.avg_max_drawdown;
      }
    }
  }
  return 0.0;
}

std::string BuildInterpretation(const SweepAnalysis& analysis) {
  std::ostringstream out;
  out << "- Strongest region: ";
  if (analysis.top10_by_sharpe.empty()) {
    out << "no successful eligible runs for Sharpe ranking.\n";
  } else {
    const BacktestRunResult& best = analysis.top10_by_sharpe.front();
    out << "best Sharpe at spike_threshold=" << best.config_used.spike_threshold
        << ", position_size=" << best.config_used.position_size
        << ", stop_loss_points=" << best.config_used.stop_loss_points
        << ", max_hold_ticks=" << best.config_used.max_hold_ticks << ".\n";
  }

  int same_spike_and_size = 0;
  if (!analysis.top10_by_sharpe.empty()) {
    const double base_spike = analysis.top10_by_sharpe.front().config_used.spike_threshold;
    const int base_size = analysis.top10_by_sharpe.front().config_used.position_size;
    for (const BacktestRunResult& run : analysis.top10_by_sharpe) {
      if (run.config_used.spike_threshold == base_spike && run.config_used.position_size == base_size) {
        ++same_spike_and_size;
      }
    }
  }
  out << "- Best-result robustness: ";
  if (same_spike_and_size >= 3) {
    out << "top Sharpe runs are clustered around the same spike_threshold and position_size, suggesting more robustness.\n";
  } else {
    out << "top Sharpe runs are dispersed, suggesting potentially fragile optima.\n";
  }

  const SensitivitySummary* spike_summary = nullptr;
  const SensitivitySummary* pos_summary = nullptr;
  const SensitivitySummary* stop_summary = nullptr;
  const SensitivitySummary* hold_summary = nullptr;
  for (const SensitivitySummary& s : analysis.sensitivities) {
    if (s.parameter_name == "spike_threshold") {
      spike_summary = &s;
    } else if (s.parameter_name == "position_size") {
      pos_summary = &s;
    } else if (s.parameter_name == "stop_loss_points") {
      stop_summary = &s;
    } else if (s.parameter_name == "max_hold_ticks") {
      hold_summary = &s;
    }
  }

  if (spike_summary != nullptr) {
    const double low_trade = GetAvgByValue(*spike_summary, 2.0, "trade_count");
    const double high_trade = GetAvgByValue(*spike_summary, 3.5, "trade_count");
    const double low_sharpe = GetAvgByValue(*spike_summary, 2.0, "sharpe");
    const double high_sharpe = GetAvgByValue(*spike_summary, 3.5, "sharpe");
    out << "- Spike threshold effect: avg trade_count changes " << low_trade << " -> " << high_trade
        << ", avg Sharpe changes " << low_sharpe << " -> " << high_sharpe << ".\n";
  }

  if (pos_summary != nullptr) {
    const double pnl_small = GetAvgByValue(*pos_summary, 1.0, "total_pnl");
    const double pnl_large = GetAvgByValue(*pos_summary, 4.0, "total_pnl");
    const double dd_small = GetAvgByValue(*pos_summary, 1.0, "max_drawdown");
    const double dd_large = GetAvgByValue(*pos_summary, 4.0, "max_drawdown");
    out << "- Position size effect: avg total_pnl changes " << pnl_small << " -> " << pnl_large
        << ", avg max_drawdown changes " << dd_small << " -> " << dd_large << ".\n";
  }

  if (stop_summary != nullptr) {
    const double dd_tight = GetAvgByValue(*stop_summary, 1.5, "max_drawdown");
    const double dd_loose = GetAvgByValue(*stop_summary, 4.5, "max_drawdown");
    out << "- Stop-loss effect: tighter stop (1.5) avg max_drawdown=" << dd_tight
        << " vs looser stop (4.5) avg max_drawdown=" << dd_loose << ".\n";
  }

  if (hold_summary != nullptr) {
    const double pnl_short = GetAvgByValue(*hold_summary, 50.0, "total_pnl");
    const double pnl_long = GetAvgByValue(*hold_summary, 150.0, "total_pnl");
    out << "- Max-hold effect: shorter hold (50) avg total_pnl=" << pnl_short
        << " vs longer hold (150) avg total_pnl=" << pnl_long << ".\n";
  }
  return out.str();
}

bool WriteReportMd(const std::string& path, const SweepBatchResult& batch, std::string* error) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Unable to write report.md: " + path;
    }
    return false;
  }
  out << "# Sweep Report\n\n";
  out << "- sweep_id: `" << batch.sweep_id << "`\n";
  out << "- combinations generated: " << batch.total_generated << "\n";
  out << "- filtered out: " << batch.total_filtered_out << "\n";
  out << "- executed: " << batch.total_executed << "\n";
  out << "- concurrency: " << batch.concurrency_used << "\n\n";

  WriteTopList(out, "Top 10 by Sharpe (eligible: tradeCount >= 10 and totalPnl > 0)", batch.analysis.top10_by_sharpe);
  WriteTopList(out, "Top 10 by Total PnL", batch.analysis.top10_by_total_pnl);
  WriteTopList(out, "Top 10 by Lowest Max Drawdown (Profitable Runs)", batch.analysis.top10_by_lowest_drawdown_profitable);

  out << "## Sensitivity Analysis\n\n";
  for (const SensitivitySummary& summary : batch.analysis.sensitivities) {
    WriteSensitivitySection(out, summary);
  }

  out << "## Pairwise Interactions\n\n";
  for (const PairwiseSummary& summary : batch.analysis.pairwise_summaries) {
    WritePairwiseSection(out, summary);
  }

  out << "## Interpretation\n\n";
  out << BuildInterpretation(batch.analysis) << "\n";
  return true;
}

bool WriteSummaryJson(const std::string& path, const SweepBatchResult& batch, std::string* error) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Unable to write summary.json: " + path;
    }
    return false;
  }
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"sweep_id\": \"" << batch.sweep_id << "\",\n";
  out << "  \"total_generated\": " << batch.total_generated << ",\n";
  out << "  \"total_filtered_out\": " << batch.total_filtered_out << ",\n";
  out << "  \"total_executed\": " << batch.total_executed << ",\n";
  out << "  \"concurrency_used\": " << batch.concurrency_used << ",\n";
  out << "  \"success_runs\": " << std::count_if(batch.runs.begin(), batch.runs.end(), IsSuccessful) << ",\n";
  out << "  \"failed_runs\": " << std::count_if(batch.runs.begin(), batch.runs.end(),
                                                [](const BacktestRunResult& r) { return !r.success; })
      << ",\n";
  out << "  \"top_sharpe_run_id\": \""
      << (batch.analysis.top10_by_sharpe.empty() ? "" : batch.analysis.top10_by_sharpe.front().run_id) << "\",\n";
  out << "  \"top_pnl_run_id\": \""
      << (batch.analysis.top10_by_total_pnl.empty() ? "" : batch.analysis.top10_by_total_pnl.front().run_id) << "\"\n";
  out << "}\n";
  return true;
}

}  // namespace

bool IsValidSweepCombination(const SweepParameterSet& params) {
  return params.spike_threshold > 0.0 && params.position_size > 0 && params.stop_loss_points > 0.0 &&
         params.max_hold_ticks > 0;
}

std::vector<SweepParameterSet> GenerateSweepCombinations(const SweepGridConfig& grid,
                                                         const SweepCombinationFilter& filter) {
  std::vector<SweepParameterSet> out;
  for (double spike_threshold : grid.spike_threshold_values) {
    for (int position_size : grid.position_size_values) {
      for (double stop_loss_points : grid.stop_loss_points_values) {
        for (int max_hold_ticks : grid.max_hold_ticks_values) {
          SweepParameterSet params{spike_threshold, position_size, stop_loss_points, max_hold_ticks};
          if (!IsValidSweepCombination(params)) {
            continue;
          }
          if (filter && !filter(params)) {
            continue;
          }
          out.push_back(params);
        }
      }
    }
  }
  return out;
}

std::vector<SweepRunSpec> BuildSweepRunSpecs(const BacktestConfig& base_config,
                                             const SweepGridConfig& grid,
                                             const SweepCombinationFilter& filter) {
  const std::vector<SweepParameterSet> params = GenerateSweepCombinations(grid, filter);
  std::vector<SweepRunSpec> specs;
  specs.reserve(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    BacktestConfig cfg = base_config;
    cfg.spike_threshold = params[i].spike_threshold;
    cfg.position_size = params[i].position_size;
    cfg.stop_loss_points = params[i].stop_loss_points;
    cfg.max_hold_ticks = params[i].max_hold_ticks;
    specs.push_back(SweepRunSpec{MakeRunId(static_cast<int>(i + 1)), params[i], cfg});
  }
  return specs;
}

std::vector<BacktestRunResult> RankEligibleBySharpe(const std::vector<BacktestRunResult>& runs) {
  std::vector<BacktestRunResult> ranked;
  for (const BacktestRunResult& run : runs) {
    if (IsEligibleForPrimaryRank(run)) {
      ranked.push_back(run);
    }
  }
  std::sort(ranked.begin(), ranked.end(), SharpeRankComparator);
  return ranked;
}

SweepAnalysis AnalyzeSweepRuns(const std::vector<BacktestRunResult>& runs) {
  SweepAnalysis analysis;
  analysis.eligible_ranked_by_sharpe = RankEligibleBySharpe(runs);
  analysis.top10_by_sharpe = TopN(analysis.eligible_ranked_by_sharpe, 10);

  std::vector<BacktestRunResult> successful;
  for (const BacktestRunResult& run : runs) {
    if (run.success) {
      successful.push_back(run);
    }
  }

  std::vector<BacktestRunResult> by_pnl = successful;
  std::sort(by_pnl.begin(), by_pnl.end(), [](const BacktestRunResult& a, const BacktestRunResult& b) {
    if (a.metrics.total_pnl != b.metrics.total_pnl) {
      return a.metrics.total_pnl > b.metrics.total_pnl;
    }
    return a.run_id < b.run_id;
  });
  analysis.top10_by_total_pnl = TopN(by_pnl, 10);

  std::vector<BacktestRunResult> profitable = successful;
  profitable.erase(std::remove_if(profitable.begin(), profitable.end(),
                                  [](const BacktestRunResult& run) { return run.metrics.total_pnl <= 0.0; }),
                   profitable.end());
  std::sort(profitable.begin(), profitable.end(), [](const BacktestRunResult& a, const BacktestRunResult& b) {
    if (a.metrics.max_drawdown != b.metrics.max_drawdown) {
      return a.metrics.max_drawdown < b.metrics.max_drawdown;
    }
    if (a.metrics.total_pnl != b.metrics.total_pnl) {
      return a.metrics.total_pnl > b.metrics.total_pnl;
    }
    return a.run_id < b.run_id;
  });
  analysis.top10_by_lowest_drawdown_profitable = TopN(profitable, 10);

  const std::vector<std::pair<std::string, std::function<double(const BacktestRunResult&)>>> param_extractors = {
      {"spike_threshold", [](const BacktestRunResult& run) { return run.config_used.spike_threshold; }},
      {"position_size", [](const BacktestRunResult& run) { return static_cast<double>(run.config_used.position_size); }},
      {"stop_loss_points", [](const BacktestRunResult& run) { return run.config_used.stop_loss_points; }},
      {"max_hold_ticks", [](const BacktestRunResult& run) { return static_cast<double>(run.config_used.max_hold_ticks); }},
  };

  for (const auto& entry : param_extractors) {
    std::map<double, AggregateState> buckets;
    for (const BacktestRunResult& run : successful) {
      UpdateAggregate(&buckets[entry.second(run)], run.metrics);
    }
    SensitivitySummary summary;
    summary.parameter_name = entry.first;
    for (const auto& kv : buckets) {
      summary.buckets.push_back(FinalizeAggregate(kv.first, kv.second));
    }
    analysis.sensitivities.push_back(summary);
  }

  struct PairDef {
    std::string first_name;
    std::string second_name;
    std::function<double(const BacktestRunResult&)> first;
    std::function<double(const BacktestRunResult&)> second;
  };

  const std::vector<PairDef> pair_defs = {
      {"spike_threshold",
       "position_size",
       [](const BacktestRunResult& run) { return run.config_used.spike_threshold; },
       [](const BacktestRunResult& run) { return static_cast<double>(run.config_used.position_size); }},
      {"stop_loss_points",
       "max_hold_ticks",
       [](const BacktestRunResult& run) { return run.config_used.stop_loss_points; },
       [](const BacktestRunResult& run) { return static_cast<double>(run.config_used.max_hold_ticks); }},
  };

  for (const PairDef& def : pair_defs) {
    std::map<std::pair<double, double>, AggregateState> buckets;
    for (const BacktestRunResult& run : successful) {
      buckets[{def.first(run), def.second(run)}];
      UpdateAggregate(&buckets[{def.first(run), def.second(run)}], run.metrics);
    }
    PairwiseSummary summary;
    summary.first_parameter = def.first_name;
    summary.second_parameter = def.second_name;
    for (const auto& kv : buckets) {
      summary.buckets.push_back(FinalizePairwise(kv.first.first, kv.first.second, kv.second));
    }
    analysis.pairwise_summaries.push_back(summary);
  }

  return analysis;
}

SweepBatchResult RunSweep(const BacktestConfig& base_config,
                          const SweepRunOptions& options,
                          const SweepGridConfig& grid,
                          const SweepCombinationFilter& filter) {
  SweepBatchResult batch;
  const std::vector<SweepParameterSet> raw_combos = GenerateSweepCombinations(grid, SweepCombinationFilter{});
  const std::vector<SweepRunSpec> specs = BuildSweepRunSpecs(base_config, grid, filter);

  batch.total_generated = static_cast<int>(raw_combos.size());
  batch.total_filtered_out = batch.total_generated - static_cast<int>(specs.size());
  batch.sweep_id = options.sweep_id.empty() ? MakeSweepId() : options.sweep_id;
  batch.batch_output_dir = (fs::path(options.out_root_dir) / batch.sweep_id).string();
  batch.batch_log_dir = (fs::path(options.log_root_dir) / batch.sweep_id).string();
  batch.concurrency_used = std::max(1, ResolveConcurrency(options.concurrency));

  try {
    fs::create_directories(batch.batch_output_dir);
    fs::create_directories(batch.batch_log_dir);
    fs::create_directories(fs::path(batch.batch_output_dir) / "runs");
  } catch (const std::exception& ex) {
    batch.error_message = ex.what();
    return batch;
  }

  batch.runs.resize(specs.size());
  std::atomic<std::size_t> next_index{0};
  std::mutex io_mutex;

  auto worker = [&]() {
    while (true) {
      const std::size_t idx = next_index.fetch_add(1);
      if (idx >= specs.size()) {
        return;
      }
      SweepRunSpec spec = specs[idx];
      if (options.force_sharpe) {
        spec.config.compute_sharpe = true;
      }
      if (options.write_per_run_outputs) {
        const fs::path run_dir = fs::path(batch.batch_output_dir) / "runs" / spec.run_id;
        const fs::path run_log = fs::path(batch.batch_log_dir) / (spec.run_id + ".log");
        spec.config.outdir = run_dir.string();
        spec.config.log_path = run_log.string();
      } else {
        spec.config.outdir = (fs::path(batch.batch_output_dir) / "runs" / spec.run_id).string();
        spec.config.log_path = (fs::path(batch.batch_log_dir) / (spec.run_id + ".log")).string();
      }

      const SingleRunOutputOptions output_options{options.write_per_run_outputs, false};
      BacktestRunResult run = RunSingleBacktest(spec.config, spec.run_id, output_options);
      batch.runs[idx] = run;

      const fs::path run_result_path = fs::path(batch.batch_output_dir) / "runs" / spec.run_id / "result.json";
      fs::create_directories(run_result_path.parent_path());
      std::string run_write_error;
      if (!WriteRunResultJson(run_result_path.string(), run, &run_write_error)) {
        std::lock_guard<std::mutex> lock(io_mutex);
        std::cerr << run_write_error << "\n";
      }

      if (options.print_progress) {
        std::lock_guard<std::mutex> lock(io_mutex);
        std::cout << "[" << run.run_id << "] " << (run.success ? "ok" : "failed");
        if (!run.success) {
          std::cout << " error=" << run.error_message;
        } else {
          std::cout << " pnl=" << std::fixed << std::setprecision(6) << run.metrics.total_pnl
                    << " sharpe=" << run.metrics.sharpe;
        }
        std::cout << "\n";
      }
    }
  };

  const int thread_count = std::min(batch.concurrency_used, std::max(1, static_cast<int>(specs.size())));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(thread_count));
  for (int i = 0; i < thread_count; ++i) {
    workers.emplace_back(worker);
  }
  for (std::thread& t : workers) {
    t.join();
  }

  batch.total_executed = static_cast<int>(batch.runs.size());
  batch.analysis = AnalyzeSweepRuns(batch.runs);

  std::string write_error;
  if (!WriteConfigJson((fs::path(batch.batch_output_dir) / "config.json").string(), options, grid, batch,
                       &write_error)) {
    batch.error_message = write_error;
    return batch;
  }
  if (!WriteSummaryJson((fs::path(batch.batch_output_dir) / "summary.json").string(), batch, &write_error)) {
    batch.error_message = write_error;
    return batch;
  }
  if (!WriteSummaryCsv((fs::path(batch.batch_output_dir) / "summary.csv").string(), batch.runs, &write_error)) {
    batch.error_message = write_error;
    return batch;
  }
  if (!WriteReportMd((fs::path(batch.batch_output_dir) / "report.md").string(), batch, &write_error)) {
    batch.error_message = write_error;
    return batch;
  }

  batch.success = true;
  return batch;
}

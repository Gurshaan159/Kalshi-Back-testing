# Kalshi Backtesting Engine

A C++17 command-line backtesting project for Kalshi market data, focused on a spike-fade strategy with reproducible outputs (equity curve, trade ledger, metrics, and per-tick logs).

This repository includes:
- a production-like backtest executable
- a fetch utility for pulling Kalshi candlestick data into CSV
- unit tests for core strategy math, portfolio logic, and execution fills
- PowerShell helper scripts for build, test, run, and output inspection

## What This Project Does

The engine reads tick/candle rows from CSV (streaming, not full-file in-memory), computes rolling statistics, identifies price spikes, confirms reversion on the next tick, enters positions, and exits using:
- staged profit-taking
- stop-loss
- max-hold timeout

After each run, it writes:
- `equity.csv` (equity over time)
- `trades.csv` (all fills and realized PnL events)
- `metrics.json` (aggregate performance metrics)
- tick-level log file (debug-friendly execution trace)

## Core Features

- C++17, portable build via CMake
- Streaming CSV reader for larger datasets
- Rolling mean/stddev spike detector
- Strict anti-lookahead processing flow
- Realistic execution simulation using quotes, spread guards, and volume caps
- Configurable costs (fee + directional slippage)
- Optional Sharpe ratio computation
- Kalshi candlestick fetch command with CSV export
- Unit test executable wired to `ctest`

## Repository Layout

- `src/` - CLI, engine loop, CSV reader, execution simulator, Kalshi fetch implementation
- `include/` - public headers for config, strategy primitives, execution, metrics, portfolio
- `tests/` - unit tests (`rolling_stats`, spike detection behavior, portfolio, drawdown, execution)
- `scripts/` - PowerShell helpers for quick workflows
- `docs/` - design notes and market research docs
- `data/` - input datasets and fetched CSVs
- `out/` - generated backtest outputs
- `logs/` - generated run logs and tick logs
- `build/` - CMake build directory

## Prerequisites

### Required

- CMake 3.20+
- C++ compiler with C++17 support
  - Windows: MSVC (Visual Studio Build Tools) or LLVM-MinGW/GCC
  - Linux/macOS: GCC or Clang

### Optional

- `curl` on Linux/macOS or `curl.exe` on Windows (used by `fetch`)
- PowerShell (for scripts in `scripts/`)

## Quick Start (Windows PowerShell)

From repository root:

```powershell
.\scripts\run_tests.ps1
.\scripts\run_backtest.ps1 -Csv data\benchmark_100k_market_1m.csv -OutDir out\quickstart -Log logs\quickstart.log
.\scripts\view_outputs.ps1 -OutDir out\quickstart -Log logs\quickstart.log
```

Notes:
- `run_backtest.ps1` builds first, then runs `kalshi_backtest`.
- `run_tests.ps1` builds and runs `ctest`.
- `view_outputs.ps1` prints metrics and tail rows from output files.

## Manual Build and Run (Any Platform)

### Configure + build

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Run backtest

Windows:

```powershell
.\build\kalshi_backtest.exe backtest --csv data\benchmark_100k_market_1m.csv --outdir out\manual --log logs\manual.log
```

Linux/macOS:

```bash
./build/kalshi_backtest backtest --csv data/benchmark_100k_market_1m.csv --outdir out/manual --log logs/manual.log
```

## CLI Commands

Use help:

```bash
kalshi_backtest --help
```

### 1) Backtest

```bash
kalshi_backtest backtest --csv <path> --outdir <dir> --log <path> [options]
```

Required:
- `--csv` input CSV path

Common optional flags:
- `--window <int>` rolling window (default `50`)
- `--spike-threshold <float>` spike std-dev multiplier (default `2.5`)
- `--position-size <int>` contracts per entry (default `3`)
- `--stop-loss <float>` stop-loss points (default `3.0`)
- `--max-hold <int>` max holding ticks (default `100`)
- `--fee <float>` fee per contract per fill (default `0.10`)
- `--slippage <float>` slippage points per fill (default `0.50`)
- `--initial-cash <float>` starting cash (default `10000`)
- `--sharpe` include Sharpe calculation

Example:

```bash
kalshi_backtest backtest --csv data/sample.csv --outdir out/run1 --log logs/run1.log --window 60 --spike-threshold 2.8 --position-size 2 --sharpe
```

### 2) Fetch Kalshi candles to CSV

```bash
kalshi_backtest fetch --contract <ticker> --out <path> [options]
```

Options:
- `--api-key <key>` optional API key header
- `--start-ts <unix>` start time (seconds)
- `--end-ts <unix>` end time (seconds)
- `--period <1|60|1440>` candle interval minutes (default `1`)

Example:

```bash
kalshi_backtest fetch --contract KXHIGHTDC-26MAR03-T45 --out data/fetched/kxhigh.csv --period 1
```

If `--start-ts` / `--end-ts` are omitted, fetch defaults to approximately the last 24 hours.

## Execution and Fill Model

Order fills are simulated before they reach the portfolio ledger.

Current rules:
- Buy orders use `ask` when available, otherwise `price`.
- Sell orders use `bid` when available, otherwise `price`.
- Directional slippage is applied in execution:
  - buy: `+slippage`
  - sell: `-slippage`
- If both `bid` and `ask` exist, fills are rejected when `ask - bid > max_spread_points`.
- If `volume` exists, fill size is capped to `floor(volume * volume_fill_ratio)`.
- If capped size is below requested size:
  - partially fill when partials are enabled
  - otherwise reject

Default execution controls in `BacktestConfig`:
- `max_spread_points = 2.0`
- `volume_fill_ratio = 0.25`
- `allow_partial_fills = true`
- `use_quotes_for_fills = true`

Note: these execution controls are currently code-level config fields and are not exposed as CLI flags.

## Input Data Requirements

Expected CSV columns:
- required: `timestamp`, `price`
- optional: `bid`, `ask`, `volume`

Behavior:
- invalid required fields are skipped
- out-of-order timestamps are skipped
- processing remains streaming and chronological

## Output Files and Meaning

Per run, output directory contains:

- `equity.csv`
  - columns: `timestamp,equity`
  - use this for curve plotting and drawdown inspection
- `trades.csv`
  - columns: `timestamp,action,qty,price,pnl`
  - each fill event is recorded with realized PnL contribution
- `metrics.json`
  - includes:
    - `total_return`
    - `max_drawdown`
    - `trade_count`
    - `win_rate`
    - `avg_trade_pnl`
    - optional `sharpe` when `--sharpe` is enabled
- tick log (`--log` path)
  - includes rolling stats, spike flags, confirmation status, action, position, and equity

## Strategy Overview

High-level loop:
1. ingest next tick
2. evaluate spike against rolling mean/stddev
3. confirm reversion on immediate next tick only
4. enter in reversion direction
5. manage open trade with staged exits, stop-loss, and max-hold
6. write logs, equity row, and fills

Current staged exit thresholds are hard-coded in engine logic as:
- `0.5`, `1.0`, `1.5` favorable price points

## Development Workflow

### Fast local cycle

```powershell
.\scripts\dev_run_all.ps1
```

This runs:
1. tests
2. sample backtest
3. output summary printout

### Script reference

- `scripts/run_tests.ps1` - configure/build + `ctest`
- `scripts/run_backtest.ps1` - configure/build + backtest run
- `scripts/view_outputs.ps1` - print metrics + tail of log/equity/trades
- `scripts/dev_run_all.ps1` - one-command dev smoke cycle

## Troubleshooting

- `cmake is not installed or not on PATH`
  - install CMake and reopen terminal
- executable not found after build
  - run `cmake --build build --config Release` and check `build/` or `build/Release/`
- fetch fails
  - verify contract ticker, time window, and network access
  - verify `curl` (Linux/macOS) or `curl.exe` (Windows) is installed and on `PATH`
- no rows fetched
  - widen time range, try another ticker/period, or use a settled/high-volume market
- low/zero trade counts
  - tune `--window` and `--spike-threshold`, validate dataset volatility
  - check whether spread/volume execution filters are too strict for the dataset

## Performance Notes

- Engine is designed for streaming throughput and constant-time rolling statistic updates.
- For larger datasets (100k+ rows), prefer Release builds.
- Keep per-tick logging enabled for debugging; disable heavy log inspection during benchmark comparisons.

## Important Limitations

- This is a research/backtesting tool, not a live trading system.
- Market microstructure assumptions are still simplified (no queue priority, no pending order book simulation, no maker/taker routing).
- Backtest realism depends heavily on input data quality and timestamp ordering.

## Suggested Next Improvements

- add `.gitignore` for build/log/output artifacts if cleaner repo history is desired
- expose execution controls (`max_spread_points`, `volume_fill_ratio`, partial-fill toggle) as CLI flags
- support richer execution model extensions (latency assumptions, queue priority)
- add parameter sweep/optimization runner
- add CI workflow for build + tests
- add plotting notebook or script for equity/trade analytics

## Additional Documentation

- Design notes: `docs/DESIGN.md`
- Kalshi market research notes: `docs/SPIKE_FADE_MARKETS.md`

## License

No explicit license file is currently included in this repository. Add a `LICENSE` file if you intend external use or contributions.

# Kalshi Backtesting Engine Design

## Scope

- First implementation pass is CSV backtesting only (`fetch` deferred).
- Portable C++17 across MSVC + Clang/GCC.
- Standard-library-only implementation.
- Stream ticks from CSV; do not load full file into memory.

## Architecture

- `src/main.cpp`: entrypoint and top-level error handling.
- `src/cli.cpp` + `include/cli.hpp`: CLI parsing and command execution.
- `src/csv_reader.cpp` + `include/csv_reader.hpp`: streaming CSV parser.
- `src/engine.cpp` + `include/engine.hpp`: simulation loop and outputs.
- `include/rolling_stats.hpp`: rolling mean/std dev (fixed window, ring buffer).
- `include/portfolio.hpp`: fill accounting, realized/unrealized PnL, equity fields.
- `include/metrics.hpp`: one-pass max drawdown, trade stats, optional Sharpe.

## Input CSV Schema

Required:
- `timestamp` (string)
- `price` (double, in Kalshi price points 0-100)

Optional (accepted when present):
- `bid`
- `ask`
- `volume`

Rows with invalid required fields are skipped and counted.
Rows with out-of-order timestamps are skipped to preserve strict tick-order processing.
Expected timestamp format is ISO-8601 UTC so lexical ordering matches chronological ordering.

## Strategy Defaults

- Rolling window: `50`
- Spike threshold: `2.5` standard deviations
- Confirmation: `1`-tick reversion required
- Position sizing: fixed contracts per entry
- Exit style: 3-stage gradual exit
- Stop-loss: price points
- Max hold: ticks

## Default Cost Model

Default cost model: fixed per-contract fee + fixed slippage in price points (both CLI-configurable). Advanced spread-based modeling is optional and not included in the default implementation.

Default values:
- Fee: `0.10` points per contract per fill
- Slippage: `0.50` points per fill

## Tick Processing Rules

For each tick:
1. Parse tick (`timestamp`, `price`).
2. Update rolling statistics.
3. Detect spike using `mean +/- threshold * std`.
4. If spike detected, store candidate.
5. On immediate next tick, confirm only if price reverts.
6. If confirmed, enter in reversion direction.
7. While in position, apply gradual exits, stop-loss, max-hold.
8. Update cash, position, realized/unrealized PnL, equity.
9. Append one log row for this tick.

## Anti-Lookahead Guarantee

- Rolling statistics at tick t must only use data from ticks <= t.
- Spike detection uses only current and historical values.
- Confirmation logic may only inspect the immediate next tick after spike detection.
- No future scanning or forward data access is allowed anywhere in the engine.

## Logging and Outputs

Per-tick log file columns:
- `timestamp`
- `price`
- `rolling_mean`
- `rolling_std`
- `spike_flag` (`none|up|down`)
- `confirmation_status`
- `action_taken`
- `position`
- `cash`
- `equity`
- `realized_pnl`
- `unrealized_pnl`

Output files:
- `out/equity.csv`: `timestamp,equity`
- `out/trades.csv`: `timestamp,action,qty,price,pnl`
- `out/metrics.json`: total return, max drawdown, trade count, win rate, average trade PnL, optional Sharpe

## CLI

Backtest:
- `kalshi_backtest backtest --csv data/sample.csv --outdir out --log logs/run.log`

Optional fetch scaffold:
- `kalshi_backtest fetch --market <id> --contract <id> --out data/kalshi.csv`

Main config flags (CLI-configurable):
- `--window`
- `--spike-threshold`
- `--position-size`
- `--stop-loss`
- `--max-hold`
- `--fee`
- `--slippage`
- `--initial-cash`
- `--sharpe` (optional enable)

`--help` prints usage.

## Build, Run, Test, Debug

Build:
- `cmake -S . -B build`
- `cmake --build build --config Release`

Run:
- `build/kalshi_backtest backtest --csv data/sample.csv --outdir out --log logs/run.log`

Tests:
- `ctest --test-dir build --output-on-failure`

Inspect outputs:
- `logs/run.log`
- `out/equity.csv`
- `out/trades.csv`
- `out/metrics.json`

PowerShell helpers:
- `scripts/run_backtest.ps1`
- `scripts/view_outputs.ps1`

## Runtime Target

- Target under 2 seconds in Release for 10k-200k rows on a normal laptop.
- Streamed processing and O(1) rolling-stat updates are used to meet this target.

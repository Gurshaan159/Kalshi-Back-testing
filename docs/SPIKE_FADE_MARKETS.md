# Kalshi Markets for Spike Fade Backtesting

## API Base URL

| Base URL | Auth Required | Status |
|----------|---------------|--------|
| **https://api.elections.kalshi.com/trade-api/v2** | No | Works for public market data |
| https://trading-api.kalshi.com/trade-api/v2 | Yes (401) | Requires authentication |

**Use:** `https://api.elections.kalshi.com/trade-api/v2` for unauthenticated market listing.

---

## Volume Fields in API Response

- `volume` – total lifetime volume (contracts)
- `volume_24h` – 24-hour volume (contracts)
- `volume_fp` / `volume_24h_fp` – string-formatted versions

---

## Top Markets by Volume (Spike-Sensitive Categories)

### 1. KXGDP (GDP releases – event-driven)

| Ticker | Volume | volume_24h | Status | Spike Type |
|--------|--------|------------|--------|------------|
| KXGDP-25APR30-T0.0 | 745,945 | 0 | finalized | GDP release |
| KXGDP-25JUL30-T2.0 | 647,416 | 0 | finalized | GDP release |
| KXGDP-26JAN30-T3.5 | 592,699 | 0 | finalized | GDP release |
| KXGDP-26JAN30-T3.0 | 519,578 | 0 | finalized | GDP release |
| KXGDP-26JAN30-T2.5 | 464,754 | 0 | finalized | GDP release |
| KXGDP-25JUL30-T3.0 | 457,424 | 0 | finalized | GDP release |
| KXGDP-25JUL30-T3.5 | 429,807 | 0 | finalized | GDP release |
| KXGDP-25APR30-T-1.5 | 390,218 | 0 | finalized | GDP release |
| KXGDP-26JAN30-T2.0 | 387,881 | 0 | finalized | GDP release |
| KXGDP-25JUL30-T2.5 | 369,221 | 0 | finalized | GDP release |

### 2. KXCPICOMBO (CPI + YoY CPI – news-driven)

| Ticker | Volume | volume_24h | Status | Spike Type |
|--------|--------|------------|--------|------------|
| KXCPICOMBO-25DEC-0129 | 117,964 | 0 | finalized | CPI release |
| KXCPICOMBO-25DEC-027 | 97,883 | 0 | finalized | CPI release |
| KXCPICOMBO-25NOV-B3.05B3.05 | 80,438 | 0 | finalized | CPI release |
| KXCPICOMBO-25NOV-B2.85B2.85 | 68,450 | 0 | finalized | CPI release |
| KXCPICOMBO-25NOV-B3.25B3.25 | 43,956 | 0 | finalized | CPI release |
| KXCPICOMBO-25NOV-B3.45B3.45 | 38,519 | 0 | finalized | CPI release |
| KXCPICOMBO-25DEC-0231 | 26,115 | 0 | finalized | CPI release |
| KXCPICOMBO-26JAN-0224 | 24,313 | 0 | finalized | CPI release |

### 3. KXFED (Fed funds rate – FOMC-driven)

| Ticker | Volume | volume_24h | Status | Spike Type |
|--------|--------|------------|--------|------------|
| KXFED-26JUN-T3.75 | 8,887 | 3 | active | FOMC meeting |
| KXFED-26JUN-T3.50 | 8,431 | 50 | active | FOMC meeting |
| KXFED-26DEC-T3.50 | 6,480 | 89 | active | FOMC meeting |
| KXFED-27JAN-T3.25 | 6,203 | 61 | active | FOMC meeting |
| KXFED-26JUN-T4.00 | 4,559 | 0 | active | FOMC meeting |
| KXFED-26SEP-T3.50 | 4,263 | 0 | active | FOMC meeting |
| KXFED-27APR-T3.25 | 3,612 | 25 | active | FOMC meeting |
| KXFED-26DEC-T3.25 | 3,346 | 51 | active | FOMC meeting |
| KXFED-27JAN-T3.50 | 3,069 | 84 | active | FOMC meeting |
| KXFED-26SEP-T3.75 | 2,756 | 0 | active | FOMC meeting |

### 4. KXCPI (CPI MoM – news-driven)

| Ticker | Volume | volume_24h | Status | Spike Type |
|--------|--------|------------|--------|------------|
| KXCPI-26NOV-T0.1 | 218 | 28 | active | CPI release |
| KXCPI-26APR-T0.5 | 199 | 0 | active | CPI release |
| KXCPI-26SEP-T0.5 | 96 | 0 | active | CPI release |
| KXCPI-26NOV-T0.0 | 85 | 0 | active | CPI release |
| KXCPI-26OCT-T0.0 | 73 | 0 | active | CPI release |

---

## Spike-Sensitive Market Types (Best for Spike Fade)

| Series | Event Type | Volatility Driver |
|--------|------------|-------------------|
| **KXGDP** | GDP releases | BEA release days |
| **KXCPICOMBO** | CPI + Core CPI | BLS CPI release days |
| **KXFED** | Fed funds rate | FOMC meeting days |
| **KXCPI** | CPI MoM | BLS CPI release days |

---

## API Usage for Backtesting

```bash
# List markets by series (economics)
GET https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXGDP&limit=100
GET https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXFED&limit=100
GET https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXCPI&limit=100
GET https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXCPICOMBO&limit=100

# Filter by status
?status=open    # active markets
?status=settled # historical (for backtest data)
```

---

## Recommended Tickers for Spike Fade Backtest

**Highest volume (historical):**
- KXGDP-25APR30-T0.0
- KXGDP-25JUL30-T2.0
- KXGDP-26JAN30-T3.5
- KXCPICOMBO-25DEC-0129
- KXCPICOMBO-25DEC-027

**Active with 24h volume (live tick updates):**
- KXFED-26DEC-T3.50 (v24h=89)
- KXFED-27JAN-T3.50 (v24h=84)
- KXFED-27JAN-T3.25 (v24h=61)
- KXFED-26JUN-T3.50 (v24h=50)
- KXCPI-26NOV-T0.1 (v24h=28)

---

## Best for Spike Fade: High Volatility + Spike Sensitivity

**Recommended:** **KXHIGHTDC (DC weather)** – highest candlestick density among spike-sensitive markets.

| Ticker | volume_24h | 1m rows (3d) | Spike sensitivity |
|--------|------------|-------------|-------------------|
| KXHIGHTDC-26MAR03-T45 | 16,107 | ~1,349 | Weather forecast updates |
| KXHIGHTDC-26MAR03-B47.5 | 10,477 | — | Weather forecast updates |
| KXFED-26DEC-T3.50 | 89 | ~41 | FOMC / Fed news |
| KXGDP-26JAN30-T3.5 | 0 | ~190 | GDP release (event day) |

**Trade-off:** Weather has high tick density (many 1m candles) but spikes are driven by forecast changes, not macro news. Fed/GDP have stronger news-driven spikes but sparse data. For backtesting with many ticks, use **KXHIGHTDC**; for news-spike focus, use **KXFED** or **KXGDP** around release windows.

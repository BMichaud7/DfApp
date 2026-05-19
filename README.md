# DfApp — Direction Finding Service

Subscribes to `rf.detections` from all AcquisitionApp instances (one per PlutoSDR), aggregates IQ snapshots of the same signal across antennas, and computes bearing via **MUSIC** (Multiple Signal Classification).

## Why MUSIC works with sequential snapshots

All five PlutoSDRs share a common LO reference. Because the carrier phase is locked to the same oscillator, the phase difference between antenna i and antenna j is purely geometric:

```
Δφᵢⱼ = 2π·f·(dᵢ - dⱼ) / c
```

where dᵢ is the path length from the emitter to antenna i. This relationship is **time-invariant** for a continuous signal, so snapshots collected at different moments (different sweep passes) are phase-coherent and can be directly fed into MUSIC without temporal alignment.

## Architecture

```
AcquisitionApp (scanner-0) ──RF_DETECTION (IQ snapshot)──┐
AcquisitionApp (scanner-1) ──RF_DETECTION (IQ snapshot)──┤
AcquisitionApp (scanner-2) ──RF_DETECTION (IQ snapshot)──┤──► rf.detections (AMQP)
AcquisitionApp (scanner-3) ──RF_DETECTION (IQ snapshot)──┤              │
AcquisitionApp (scanner-4) ──RF_DETECTION (IQ snapshot)──┘              │
                                                                         ▼
                                                              ┌─────────────────────┐
                                                              │  DfService           │
                                                              │  Aggregation window  │
                                                              │  (5 s, 100 kHz bins) │
                                                              └────────┬────────────┘
                                                                       │ ≥ min_elements
                                                                       ▼
                                                              ┌─────────────────────┐
                                                              │  DfEngine (MUSIC)    │
                                                              │  5×N covariance      │
                                                              │  eigendecomposition  │
                                                              │  0.5° sweep          │
                                                              └────────┬────────────┘
                                                                       │
                                                              AMQP ──► rf.df_results
                                                              PostgreSQL ── df_results
```

## MUSIC algorithm

1. Stack IQ snapshots into array matrix **X** (M antennas × N samples)
2. Compute sample covariance: **R** = (1/N) **X** **X**ᴴ
3. Eigendecompose **R** → signal subspace **Eₛ** + noise subspace **Eₙ**
4. Sweep bearing θ from 0–360°, compute pseudo-spectrum: P(θ) = 1 / ‖**Eₙ**ᴴ **a**(θ)‖²
5. Peak of P(θ) → bearing estimate; peak-to-mean ratio → confidence

Steering vector for bearing θ (clockwise from North), antenna at (x, y):
```
aᵢ(θ) = exp(−j · 2π/λ · (xᵢ·sin(θ) + yᵢ·cos(θ)))
```

## Configuration

Antenna positions are in metres, **ENU frame** (x=East, y=North, z=Up), relative to any fixed reference point:

```xml
<array>
  <antenna scanner_id="scanner-0" x="0.00" y="0.00" z="0.00"/>
  <antenna scanner_id="scanner-1" x="1.90" y="0.62" z="0.00"/>
  <antenna scanner_id="scanner-2" x="1.18" y="1.62" z="0.00"/>
  <antenna scanner_id="scanner-3" x="-1.18" y="1.62" z="0.00"/>
  <antenna scanner_id="scanner-4" x="-1.90" y="0.62" z="0.00"/>
</array>
```

The example above is a 5-element uniform circular array (UCA) with 2 m radius — a good default for 360° coverage with no preferred axis.

| Parameter | Default | Description |
|---|---|---|
| `algorithm` | `MUSIC` | Direction-finding algorithm |
| `aggregation_window_ms` | `5000` | Maximum snapshot age for aggregation |
| `min_elements` | `3` | Minimum antennas required for a bearing |
| `angle_step_deg` | `0.5` | MUSIC sweep angular resolution |
| `snr_threshold_db` | `3.0` | Minimum per-antenna SNR to include |

## Output (`rf.df_results` AMQP topic)

```json
{
  "msg_type":              "DF_RESULT",
  "scanner_id":            "df-0",
  "center_freq_hz":        99494000,
  "azimuth_deg":           127.5,
  "confidence":            0.82,
  "num_elements":          5,
  "algorithm":             "MUSIC",
  "contributing_scanners": ["scanner-0","scanner-1","scanner-2","scanner-3","scanner-4"]
}
```

`confidence` is normalised MUSIC peak prominence: 0 = no peak, 1 = sharp null in noise subspace (≥ 20 dB peak-to-mean). Values above 0.5 are reliable for most antenna spacings.

## PostgreSQL (`df_results` table)

```sql
psql -U sdr -d sdr_scanner -f schema/init.sql
```

| Column | Type | Description |
|---|---|---|
| `computed_at` | TIMESTAMPTZ | When bearing was computed |
| `center_freq_hz` | BIGINT | Signal frequency |
| `azimuth_deg` | REAL | Bearing clockwise from North |
| `confidence` | REAL | 0–1 MUSIC prominence |
| `num_elements` | INTEGER | Antennas used |
| `scanner_ids` | TEXT | Comma-separated contributing scanner IDs |

## Antenna array design tips

- **Spacing**: λ/2 at the highest frequency of interest avoids spatial aliasing. At 1 GHz, λ/2 ≈ 15 cm. At 100 MHz, λ/2 ≈ 1.5 m.
- **UCA vs ULA**: Uniform Circular Array gives uniform 360° bearing coverage. Uniform Linear Array gives better resolution but has a left/right ambiguity.
- **Baseline**: Larger baselines improve resolution but introduce grating lobes at high frequencies. A 2 m radius UCA works well from 100 MHz to ~750 MHz.
- **Calibration**: Measure actual antenna positions to ±1 cm for best accuracy. Phase calibration (rotating a known source and fitting) removes residual hardware phase offsets.

## Building

```bash
# Native
git clone https://github.com/BMichaud7/SdrSdk.git    ../SdrSdk
git clone https://github.com/BMichaud7/SdrTaskApi.git ../SdrTaskApi
cmake -B build -DWITH_DB=ON
cmake --build build --parallel $(nproc)

# PostgreSQL schema
psql -U sdr -d sdr_scanner -f schema/init.sql

# Container
podman build -t sdr-df:1.0.0 .
```

## Dependencies

| Package | Purpose |
|---|---|
| `SdrTaskApi` | Shared message types (RF_DETECTION codec) |
| `libeigen3-dev` | MUSIC covariance matrix and eigendecomposition |
| `libqpid-proton-cpp` | AMQP 1.0 subscriber + publisher |
| `libtinyxml2-dev` | Config file parser |
| `libpq-dev` + libpqxx 7.9 | PostgreSQL persistence (WITH_DB=ON) |
| `libspdlog-dev` | Structured logging |

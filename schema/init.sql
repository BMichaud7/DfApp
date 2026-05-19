-- DfApp — PostgreSQL schema
-- Run once: psql -U sdr -d sdr_scanner -f schema/init.sql

CREATE TABLE IF NOT EXISTS df_results (
    id              BIGSERIAL    PRIMARY KEY,
    computed_at     TIMESTAMPTZ  NOT NULL DEFAULT now(),
    center_freq_hz  BIGINT       NOT NULL,

    -- MUSIC output
    azimuth_deg     REAL         NOT NULL,   -- bearing clockwise from North, 0–360
    confidence      REAL         NOT NULL,   -- 0–1: MUSIC peak-to-mean prominence
    num_elements    INTEGER      NOT NULL,   -- antennas that contributed
    algorithm       TEXT         NOT NULL,   -- 'MUSIC'

    -- Which AcquisitionApp instances contributed snapshots
    scanner_ids     TEXT         NOT NULL    -- comma-separated scanner_id list
);

CREATE INDEX IF NOT EXISTS idx_df_time ON df_results (computed_at DESC);
CREATE INDEX IF NOT EXISTS idx_df_freq ON df_results (center_freq_hz);

-- ── Useful views ──────────────────────────────────────────────────────────────

-- Recent fixes with readable frequency
CREATE OR REPLACE VIEW recent_df_results AS
SELECT
    computed_at,
    round(center_freq_hz / 1e6, 3)      AS freq_mhz,
    round(azimuth_deg::numeric, 1)       AS azimuth_deg,
    round(confidence::numeric, 2)        AS confidence,
    num_elements,
    scanner_ids
FROM df_results
WHERE computed_at > now() - interval '60 seconds'
ORDER BY computed_at DESC;

-- Per-frequency bearing history (useful for spotting moving emitters)
CREATE OR REPLACE VIEW df_bearing_history AS
SELECT
    round(center_freq_hz / 1e6, 2)      AS freq_mhz,
    computed_at,
    round(azimuth_deg::numeric, 1)       AS azimuth_deg,
    round(confidence::numeric, 2)        AS confidence,
    num_elements
FROM df_results
ORDER BY center_freq_hz, computed_at DESC;

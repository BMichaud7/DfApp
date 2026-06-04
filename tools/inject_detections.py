#!/usr/bin/env python3
# ========================================================================
# Project: OpenRFStack
# Author:  Brendan Michaud
# Year:    2026
# Part of OpenRFStack (https://github.com/OpenRFStack)
#
# Licensed under the Personal Use License.
# Do not use for commercial, organizational, or military purposes.
# Contact author for permission: https://github.com/OpenRFStack
# ========================================================================

"""
inject_detections.py — send synthetic RF_DETECTION AMQP messages to test DfApp.

Generates phase-coherent IQ snapshots from a specified source bearing and
publishes one message per antenna, simulating what 5 AcquisitionApp instances
would produce for a real signal at that bearing.

Usage:
    python3 inject_detections.py --bearing 45 --freq 99.5e6
    python3 inject_detections.py --bearing 135 --freq 433e6 --snr 15
    python3 inject_detections.py --sweep             # rotate 0-360° every 5 s
"""

import argparse, base64, json, math, random, time, struct
import numpy as np
from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

SPEED_OF_LIGHT = 2.998e8
SAMPLE_RATE    = 20e6
TONE_HZ        = 1000.0   # 1 kHz baseband offset
N_SAMPLES      = 1024

# Matches config/df.xml — 5-element UCA, 2 m radius
ANTENNAS = [
    {"scanner_id": "scanner-0", "x":  0.00, "y":  0.00},
    {"scanner_id": "scanner-1", "x":  1.90, "y":  0.62},
    {"scanner_id": "scanner-2", "x":  1.18, "y":  1.62},
    {"scanner_id": "scanner-3", "x": -1.18, "y":  1.62},
    {"scanner_id": "scanner-4", "x": -1.90, "y":  0.62},
]


def make_iq_b64(ant_x, ant_y, bearing_deg, freq_hz, snr_db, rng):
    """Return base64-encoded raw float32 I,Q bytes (matches schema 1.2 iq_snapshot_b64)."""
    bearing_rad   = math.radians(bearing_deg)
    k             = 2 * math.pi * freq_hz / SPEED_OF_LIGHT
    spatial_phase = k * (ant_x * math.sin(bearing_rad)
                        + ant_y * math.cos(bearing_rad))
    noise_sigma   = 10 ** (-snr_db / 20.0)

    t     = np.arange(N_SAMPLES) / SAMPLE_RATE
    phase = spatial_phase + 2 * math.pi * TONE_HZ * t
    I     = np.cos(phase) + rng.normal(0, noise_sigma, N_SAMPLES)
    Q     = np.sin(phase) + rng.normal(0, noise_sigma, N_SAMPLES)
    raw   = np.column_stack([I, Q]).ravel().astype(np.float32)
    return base64.b64encode(raw.tobytes()).decode('ascii')


class Injector(MessagingHandler):
    def __init__(self, args):
        super().__init__()
        self.args    = args
        self.sender  = None
        self.wq      = None
        self.rng     = np.random.default_rng(seed=42)

    def on_start(self, event):
        conn = event.container.connect(
            self.args.broker,
            user=self.args.user, password=self.args.password,
            sasl_enabled=True, allowed_mechs="PLAIN",
            allow_insecure_mechs=True)
        self.sender = event.container.create_sender(conn, self.args.topic)

    def on_sendable(self, event):
        if self.args.sweep:
            self._run_sweep(event)
        else:
            self._send_bearing(self.args.bearing)
            event.connection.close()

    def _run_sweep(self, event):
        bearing = 0.0
        while True:
            self._send_bearing(bearing)
            print(f"Injected bearing={bearing:.1f}°  freq={self.args.freq/1e6:.3f} MHz")
            time.sleep(self.args.interval)
            bearing = (bearing + self.args.step) % 360.0

    def _send_bearing(self, bearing_deg):
        ts_ms = int(time.time() * 1000)
        for ant in ANTENNAS:
            if self.args.drop and ant["scanner_id"] in self.args.drop:
                continue
            iq_b64 = make_iq_b64(ant["x"], ant["y"],
                                  bearing_deg, self.args.freq,
                                  self.args.snr, self.rng)
            body = json.dumps({
                "msg_type":               "RF_DETECTION",
                "schema_version":         "1.2",
                "timestamp_ms":           ts_ms,
                "scanner_id":             ant["scanner_id"],
                "center_freq_hz":         self.args.freq,
                "bandwidth_hz":           200_000,
                "power_db":               -40.0,
                "snr_db":                 self.args.snr,
                "iq_snapshot_b64":        iq_b64,
                "snapshot_sample_rate_sps": SAMPLE_RATE,
            })
            msg = Message(body=body, content_type="application/json")
            self.sender.send(msg)
        print(f"Injected {len(ANTENNAS) - len(self.args.drop or [])} snapshots "
              f"bearing={bearing_deg:.1f}°  freq={self.args.freq/1e6:.3f} MHz  "
              f"snr={self.args.snr} dB")


def main():
    p = argparse.ArgumentParser(description="Inject synthetic DF detections")
    p.add_argument("--bearing",  type=float, default=45.0,
                   help="Source bearing clockwise from North (degrees)")
    p.add_argument("--freq",     type=float, default=99.5e6,
                   help="Carrier frequency (Hz, e.g. 99.5e6)")
    p.add_argument("--snr",      type=float, default=20.0,
                   help="Per-antenna SNR (dB)")
    p.add_argument("--sweep",    action="store_true",
                   help="Rotate bearing continuously (Ctrl+C to stop)")
    p.add_argument("--step",     type=float, default=10.0,
                   help="Bearing step for --sweep (degrees)")
    p.add_argument("--interval", type=float, default=3.0,
                   help="Seconds between sweep steps")
    p.add_argument("--drop",     nargs="*", metavar="SCANNER_ID",
                   help="Omit these scanners (test min_elements handling)")
    p.add_argument("--broker",   default="amqp://localhost:5672")
    p.add_argument("--topic",    default="rf.detections")
    p.add_argument("--user",     default="sdr_ctrl")
    p.add_argument("--password", default="sdr_hw_test")
    args = p.parse_args()

    Container(Injector(args)).run()


if __name__ == "__main__":
    main()

# ========================================================================
# End of file — OpenRFStack
# Subject to Personal Use License
# https://github.com/OpenRFStack
# ========================================================================

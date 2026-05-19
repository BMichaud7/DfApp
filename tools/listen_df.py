#!/usr/bin/env python3
"""
listen_df.py — subscribe to rf.df_results and display bearing estimates.

Usage:
    python3 listen_df.py
    python3 listen_df.py --known 45.0          # show error vs known bearing
    python3 listen_df.py --freq 99.5e6         # filter to one frequency
    python3 listen_df.py --broker amqp://remote:5672
"""

import argparse, json, math, sys, time
from proton.handlers import MessagingHandler
from proton.reactor import Container

RESET  = "\033[0m"
GRN    = "\033[32m"
YLW    = "\033[33m"
RED    = "\033[31m"
BLU    = "\033[34m"
BOLD   = "\033[1m"


def bearing_arrow(deg):
    arrows = "↑↗→↘↓↙←↖"
    idx = round(deg / 45) % 8
    return arrows[idx]


def confidence_bar(c, width=20):
    filled = round(c * width)
    color  = GRN if c > 0.6 else (YLW if c > 0.3 else RED)
    return color + "█" * filled + "░" * (width - filled) + RESET


def angle_diff(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


class DfListener(MessagingHandler):
    def __init__(self, args):
        super().__init__()
        self.args   = args
        self.count  = 0
        self.errors = []

    def on_start(self, event):
        conn = event.container.connect(
            self.args.broker,
            user=self.args.user, password=self.args.password,
            sasl_enabled=True, allowed_mechs="PLAIN",
            allow_insecure_mechs=True)
        event.container.create_receiver(conn, self.args.topic)
        print(f"{BLU}DfApp listener — {self.args.topic}  Ctrl+C to stop{RESET}\n")
        if self.args.known is not None:
            print(f"  Known bearing: {BOLD}{self.args.known:.1f}°{RESET}\n")

    def on_message(self, event):
        try:
            j = json.loads(event.message.body)
        except Exception:
            return
        if j.get("msg_type") != "DF_RESULT":
            return

        freq_hz   = j.get("center_freq_hz", 0)
        if self.args.freq and abs(freq_hz - self.args.freq) > 200e3:
            return

        az        = j.get("azimuth_deg", 0.0)
        conf      = j.get("confidence",  0.0)
        n_el      = j.get("num_elements", 0)
        algo      = j.get("algorithm",  "?")
        scanners  = ", ".join(j.get("contributing_scanners", []))
        self.count += 1

        err_str = ""
        if self.args.known is not None:
            err = angle_diff(az, self.args.known)
            self.errors.append(err)
            color = GRN if err < 5 else (YLW if err < 15 else RED)
            err_str = f"  {color}err={err:.1f}°{RESET}"

        print(f"[{self.count:4d}] {freq_hz/1e6:8.3f} MHz  "
              f"{bearing_arrow(az)} {BOLD}{az:6.1f}°{RESET}  "
              f"{confidence_bar(conf)} {conf:.2f}  "
              f"M={n_el} {algo}{err_str}")
        if self.args.verbose:
            print(f"       scanners: {scanners}")

        if self.args.count and self.count >= self.args.count:
            if self.errors:
                mean_err = sum(self.errors) / len(self.errors)
                print(f"\n  Mean bearing error: {mean_err:.2f}°  "
                      f"({len(self.errors)} fixes)")
            event.connection.close()

    def on_transport_error(self, event):
        print(f"{RED}Transport error: {event.transport.condition}{RESET}")


def main():
    p = argparse.ArgumentParser(description="Display DfApp bearing results")
    p.add_argument("--broker",  default="amqp://localhost:5672")
    p.add_argument("--topic",   default="rf.df_results")
    p.add_argument("--user",    default="sdr_ctrl")
    p.add_argument("--password",default="sdr_hw_test")
    p.add_argument("--known",   type=float, default=None,
                   help="Known bearing (deg) to compute error against")
    p.add_argument("--freq",    type=float, default=None,
                   help="Filter to this frequency (Hz, e.g. 99.5e6)")
    p.add_argument("--count",   type=int,   default=None,
                   help="Stop after this many fixes and print stats")
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    try:
        Container(DfListener(args)).run()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

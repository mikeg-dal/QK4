#!/usr/bin/env python3
"""
tcimonitor -- a live, top-style view of everything QK4's TCI server reports.

WHY THIS EXISTS.  tciclient.py --audit measures what the server ANSWERS when asked.  It is
structurally blind to the other half of the protocol: the messages a server sends unprompted.
That blind spot is where the transmit-status bug lived - every command involved was
"implemented", the audit said so, and the status message still never arrived.

This tool watches the unprompted half.  It subscribes to the sensors, then sits and renders
whatever turns up.  If a meter is not moving on screen, the server is not sending it.

    python3 scripts/tcimonitor.py                    # localhost, 200 ms sensors
    python3 scripts/tcimonitor.py --interval 50      # faster meters
    python3 scripts/tcimonitor.py --host 192.168.1.5
    python3 scripts/tcimonitor.py --raw              # also log every message, unrendered

SAFE AGAINST A LIVE RADIO.  It sends exactly two commands, both subscriptions
(rx_sensors_enable, tx_sensors_enable), and never a SET.  Nothing here can move the radio or key
the transmitter.

Ctrl-C to quit.  Shares the WebSocket plumbing in tciclient.py rather than reimplementing it.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tciclient import Connection, DEFAULT_PORT  # noqa: E402

# S9 = -73 dBm, 6 dB per S-unit below it. The bar spans S0 to roughly S9+60.
S9_DBM = -73.0
DB_PER_S_UNIT = 6.0
BAR_MIN_DBM = S9_DBM - 9 * DB_PER_S_UNIT  # S0, -127
BAR_MAX_DBM = S9_DBM + 60.0               # S9+60, -13

ESC = "\x1b["


def s_unit_text(dbm):
    """dBm rendered the way an operator reads a meter."""
    if dbm is None:
        return "--"
    if dbm >= S9_DBM:
        over = int(round((dbm - S9_DBM) / 10.0) * 10)
        return "S9" if over <= 0 else "S9+%d" % over
    units = 9 - (S9_DBM - dbm) / DB_PER_S_UNIT
    return "S%d" % max(0, int(round(units)))


def bar(dbm, width=34):
    if dbm is None:
        return "░" * width
    span = BAR_MAX_DBM - BAR_MIN_DBM
    frac = (dbm - BAR_MIN_DBM) / span
    frac = 0.0 if frac < 0 else (1.0 if frac > 1 else frac)
    filled = int(frac * width)
    return "█" * filled + "░" * (width - filled)


def hz(value):
    if value is None:
        return "-- --- ---"
    s = "%09d" % int(value)
    return "%s.%s.%s" % (s[:-6].lstrip("0") or "0", s[-6:-3], s[-3:])


class State:
    """Everything the server has told us, updated as messages arrive."""

    def __init__(self):
        self.fields = {}
        self.rx_dbm = {0: None, 1: None}
        self.tx = {"mic": None, "power": None, "peak": None, "swr": None}
        self.counts = {"total": 0, "sensor": 0}
        self.last_line = ""
        self.sensor_times = []
        self.started = time.time()

    def feed(self, line):
        self.counts["total"] += 1
        self.last_line = line
        body = line.rstrip(";")
        name, _, argstr = body.partition(":")
        name = name.lower()
        args = argstr.split(",") if argstr else []

        def arg(i, default=None):
            return args[i] if len(args) > i else default

        if name in ("rx_sensors", "rx_channel_sensors", "tx_sensors"):
            self.counts["sensor"] += 1
            now = time.time()
            self.sensor_times.append(now)
            # Keep a one-second window for the rate readout.
            self.sensor_times = [t for t in self.sensor_times if now - t <= 1.0]

        try:
            if name == "vfo":
                self.fields["vfo%s" % arg(1)] = int(arg(2))
            elif name == "dds":
                self.fields["dds"] = int(arg(1))
            elif name == "tx_frequency":
                self.fields["tx_freq"] = int(arg(0))
            elif name == "modulation":
                self.fields["mode"] = arg(1)
            elif name == "trx":
                self.fields["tx"] = arg(1) == "true"
            elif name == "split_enable":
                self.fields["split"] = arg(1) == "true"
            elif name == "rx_channel_enable":
                self.fields["chan%s" % arg(1)] = arg(2) == "true"
            elif name in ("rit_enable", "xit_enable"):
                self.fields[name] = arg(1) == "true"
            elif name in ("rit_offset", "xit_offset"):
                self.fields[name] = int(arg(1))
            elif name == "agc_mode":
                self.fields["agc"] = arg(1)
            elif name == "sql_enable":
                self.fields["sql_on"] = arg(1) == "true"
            elif name == "sql_level":
                self.fields["sql"] = arg(1)
            elif name in ("drive", "tune_drive"):
                self.fields[name] = arg(1)
            elif name == "mic_level":
                self.fields["mic_level"] = arg(0)
            elif name in ("device", "protocol"):
                self.fields[name] = argstr
            elif name == "rx_sensors":
                self.rx_dbm[0] = float(arg(1))
            elif name == "rx_channel_sensors":
                self.rx_dbm[int(arg(1))] = float(arg(2))
            elif name == "tx_sensors":
                self.tx["mic"] = float(arg(1))
                self.tx["power"] = float(arg(2))
                self.tx["peak"] = float(arg(3))
                self.tx["swr"] = float(arg(4))
        except (TypeError, ValueError, IndexError):
            # A malformed message is data about the server, not a reason to die mid-session.
            self.fields["last_bad"] = line


def render(state, host, port, raw_lines):
    f = state.fields
    up = int(time.time() - state.started)
    rate = len(state.sensor_times)

    tx_on = f.get("tx")
    # Pad the PLAIN text before colouring: escape sequences count toward a %-width field but
    # occupy no columns, so colouring first misaligns every column to the right of it.
    badge_text = "● TRANSMIT" if tx_on else "● receive"
    badge_colour = "1;31" if tx_on else "1;32"
    tx_badge = "\x1b[%sm%-24s\x1b[0m" % (badge_colour, badge_text)
    sub_on = f.get("chan1")

    out = []
    out.append("\x1b[1m QK4 TCI Monitor\x1b[0m  %s:%d%s" % (host, port, " " * 8))
    out.append(" up %02d:%02d:%02d   messages %-7d sensors %d/s (%d total)"
               % (up // 3600, (up // 60) % 60, up % 60, state.counts["total"], rate,
                  state.counts["sensor"]))
    out.append(" " + "─" * 72)
    out.append(" device   %-24s protocol  %s"
               % (f.get("device", "--"), f.get("protocol", "--")))
    out.append(" state    %s split     %s"
               % (tx_badge, "ON" if f.get("split") else "off"))
    out.append("")
    out.append(" VFO A    %-16s %-8s  agc       %s"
               % (hz(f.get("vfo0")), f.get("mode", "--"), f.get("agc", "--")))
    out.append(" VFO B    %-16s %-8s  sub rx    %s"
               % (hz(f.get("vfo1")), "", "ON" if sub_on else "off"))
    out.append(" TX freq  %-16s"
               % hz(f.get("tx_freq")))
    out.append("")
    out.append(" rit      %-4s %+6d Hz            xit       %-4s %+6d Hz"
               % ("ON" if f.get("rit_enable") else "off", f.get("rit_offset", 0),
                  "ON" if f.get("xit_enable") else "off", f.get("xit_offset", 0)))
    out.append(" drive    %-4s %%                  sql       %-4s %s dBm"
               % (f.get("drive", "--"), "ON" if f.get("sql_on") else "off", f.get("sql", "--")))
    out.append("")
    out.append(" \x1b[1mRX signal\x1b[0m")
    a = state.rx_dbm[0]
    out.append("   A  %s  %s  %s"
               % (bar(a), ("%8.1f dBm" % a) if a is not None else "      -- dBm", s_unit_text(a)))
    if sub_on:
        b = state.rx_dbm[1]
        out.append("   B  %s  %s  %s"
                   % (bar(b), ("%8.1f dBm" % b) if b is not None else "      -- dBm", s_unit_text(b)))
    else:
        out.append("   B  %s  (sub rx off)%s" % ("░" * 34, " " * 12))
    out.append("")
    out.append(" \x1b[1mTX\x1b[0m")
    t = state.tx
    out.append("   power %s W    peak %s W    swr %s    mic %s dBm"
               % (("%6.1f" % t["power"]) if t["power"] is not None else "    --",
                  ("%6.1f" % t["peak"]) if t["peak"] is not None else "    --",
                  ("%5.2f" % t["swr"]) if t["swr"] is not None else "   --",
                  ("%6.1f" % t["mic"]) if t["mic"] is not None else "    --"))
    out.append("")
    out.append(" last  \x1b[2m%s\x1b[0m" % state.last_line[:66])
    if raw_lines:
        out.append("")
        for line in raw_lines[-8:]:
            out.append("   \x1b[2m%s\x1b[0m" % line[:70])
    out.append("")
    out.append(" \x1b[2mCtrl-C to quit. This tool sends only sensor subscriptions - never a SET.\x1b[0m")

    # Home the cursor and clear each line, rather than clearing the screen: a full clear flickers.
    sys.stdout.write(ESC + "H")
    for line in out:
        sys.stdout.write(line + ESC + "K\n")
    sys.stdout.write(ESC + "J")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--interval", type=int, default=200,
                    help="sensor reporting interval in ms (spec allows 30-1000)")
    ap.add_argument("--raw", action="store_true", help="also show the last few raw messages")
    args = ap.parse_args()

    try:
        conn = Connection(args.host, args.port, timeout=5.0)
    except OSError as e:
        print("cannot connect to %s:%d -- %s" % (args.host, args.port, e))
        print("is QK4 running with the TCI server enabled?")
        return 1

    state = State()
    for line in conn.drain_burst():
        state.feed(line)

    conn.send("rx_sensors_enable:true,%d;" % args.interval)
    conn.send("tx_sensors_enable:true,%d;" % args.interval)

    raw = []
    sys.stdout.write(ESC + "2J" + ESC + "?25l")  # clear once, hide the cursor
    try:
        last_draw = 0.0
        while True:
            line = conn.recv_text(timeout=0.1)
            if line is not None:
                state.feed(line)
                if args.raw:
                    raw.append(line)
                    raw[:] = raw[-40:]
            now = time.time()
            if now - last_draw >= 0.1:   # 10 Hz is smooth and costs nothing
                render(state, args.host, args.port, raw if args.raw else None)
                last_draw = now
    except KeyboardInterrupt:
        pass
    finally:
        sys.stdout.write(ESC + "?25h\n")  # show the cursor again
        sys.stdout.flush()
        try:
            conn.send("rx_sensors_enable:false;")
            conn.send("tx_sensors_enable:false;")
        except OSError:
            pass
        conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

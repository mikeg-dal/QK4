#!/usr/bin/env python3
"""
catclient -- a conformance auditor for QK4's CAT server, driven over a real socket.

WHY THIS EXISTS.  tests/test_catserver.cpp constructs CatServer and RadioState directly, in one
process, on one thread.  That proves the handler logic and nothing else: it cannot see a build
that shipped the wrong binary, a server that never started because the setting was off, a
framing bug that only appears when TCP splits a command across two packets, or state that is
correct in a fixture and wrong in the assembled application.  This tool talks to the CAT server
the way a logger or contest program does, and reports what the built QK4 actually answers.

    python3 scripts/catclient.py --audit         # read-only conformance sweep
    python3 scripts/catclient.py --selftest      # offline, no radio, no QK4
    python3 scripts/catclient.py --audit --json  # machine-readable, for CI

SAFETY.  This is meant to be run against a QK4 connected to a REAL RADIO, so the default posture
is strictly read-only and everything that touches the radio is opt-in:

  * By default the audit sends only GET forms.  It never sets a frequency, mode, split or power
    level, and it never keys the transmitter.
  * --allow-set adds SET round-trips.  Each one reads the current value first, changes it, and
    restores it from the radio's own reply, so the restore cannot drift from what was really
    there.  Only receive-side settings are touched: VFO A frequency and keyer speed.
  * --allow-tx adds the TX;/RX; gate.  CatServer does not forward these to the radio - it emits
    pttRequested, and QK4's audio stream is what keys the K4 - but the radio DOES end up keyed,
    so only run it into a dummy load, with the transmitter disabled, or with the K4 in TX Test
    mode.  The sequence ends in RX; unconditionally, including after a failure.
  * --stress exercises the framing overflow guard, and only ever sends semicolon-free filler
    that the server is documented to discard.  It runs last because it provokes a disconnect.

WHAT IT CANNOT TELL YOU.  A GET answers from RadioState, which is populated by the radio.  With
no radio connected the values are defaults, so value assertions would be testing the default
rather than the path.  The audit therefore splits its checks:

  * FIXED    - the answer is a constant in catserver.cpp and must match exactly, radio or not.
  * SHAPE    - the value depends on the radio, but the wire format must hold regardless.
  * FRAMING  - protocol behaviour: multi-command packets, split writes, silence on nonsense.
  * SESSION  - per-client state and multi-client independence.

Expected formats were read out of src/network/catframes.cpp and src/network/catserver.cpp, and
are restated here deliberately.  A second independent statement of the contract is the point: if
someone changes a format, this file disagrees with the code and the audit fails, which is the
alarm.  It is not a mirror of the C++ and must not be regenerated from it.

NO DEPENDENCIES.  Standard library only, same as scripts/tciclient.py.
"""

import argparse
import json
import re
import socket
import sys
import threading
import time

DEFAULT_PORT = 9299
DEFAULT_HOST = "127.0.0.1"

# Read timeout for a single reply.  Generous on purpose: this tool must never report a failure
# that is really just a slow machine, which is the failure mode of a fixed-wait read.
REPLY_TIMEOUT = 2.0

# How long to wait before concluding the server is deliberately silent.  Shorter, because every
# silence check pays it, but still an order of magnitude above a loopback round trip.
SILENCE_TIMEOUT = 0.6


class CatConnection:
    """One CAT client session.  Reads are terminator-driven, never fixed-wait."""

    def __init__(self, host, port, timeout=REPLY_TIMEOUT):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""

    def send(self, text):
        self.sock.sendall(text.encode("ascii"))

    def read_reply(self, timeout=REPLY_TIMEOUT):
        """Return one semicolon-terminated reply, or None if none arrives in time.

        WHY a terminator loop and not a sleep: a reply can be split across TCP segments, and the
        time it takes is a property of the machine, not of the server being correct.  Waiting a
        fixed interval and reading whatever landed turns load into a test failure.
        """
        deadline = time.monotonic() + timeout
        while True:
            idx = self.buf.find(b";")
            if idx != -1:
                reply = self.buf[: idx + 1]
                self.buf = self.buf[idx + 1 :]
                return reply.decode("ascii", "replace")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                return None
            except OSError:
                return None
            if not chunk:
                return None
            self.buf += chunk

    def ask(self, command, timeout=REPLY_TIMEOUT):
        self.send(command)
        return self.read_reply(timeout)

    def expect_silence(self, command, timeout=SILENCE_TIMEOUT):
        """Send a command that must draw no reply.  Returns what arrived, or None if silent."""
        self.send(command)
        return self.read_reply(timeout)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class Audit:
    def __init__(self, quiet=False):
        self.results = []
        self.quiet = quiet

    def record(self, group, name, ok, detail):
        self.results.append({"group": group, "name": name, "ok": ok, "detail": detail})
        if not self.quiet:
            mark = "PASS" if ok else "FAIL"
            print(f"  [{mark}] {group}/{name}: {detail}")

    def check_exact(self, conn, group, command, expected):
        got = conn.ask(command)
        if got is None:
            self.record(group, command, False, "no reply within %.1fs" % REPLY_TIMEOUT)
        elif got == expected:
            self.record(group, command, True, f"{got}")
        else:
            self.record(group, command, False, f"expected {expected!r}, got {got!r}")

    def check_shape(self, conn, group, command, pattern, extra=None):
        got = conn.ask(command)
        if got is None:
            self.record(group, command, False, "no reply within %.1fs" % REPLY_TIMEOUT)
            return None
        if not re.fullmatch(pattern, got):
            self.record(group, command, False, f"{got!r} does not match /{pattern}/")
            return got
        if extra is not None:
            ok, why = extra(got)
            if not ok:
                self.record(group, command, False, f"{got!r}: {why}")
                return got
        self.record(group, command, True, f"{got}")
        return got

    @property
    def failures(self):
        return [r for r in self.results if not r["ok"]]


# --- FIXED: constants in catserver.cpp; these must hold with or without a radio ----------------
# Each is a literal QByteArray return in CatServer::handleCommand().
FIXED = [
    ("PS;", "PS1;"),      # power status - always on while QK4 is running
    ("ID;", "ID017;"),    # K4 identifier
    ("K2;", "K22;"),      # K2 extended mode level 2
    ("K3;", "K31;"),      # K3 extended mode level 1
    ("FR;", "FR0;"),      # receive VFO is always A
    ("TB;", "TB000;"),    # no CW messages queued
    ("SB;", "SB0;"),      # sub RX reported off
    ("AG;", "AG000;"),    # AF gain placeholder
    ("SQ;", "SQ000;"),    # squelch placeholder
]

# --- SHAPE: value comes from the radio, format does not -----------------------------------------
# Formats restated from src/network/catframes.cpp.
SHAPE = [
    ("FA;", r"FA\d{11};"),        # frequencyA: 11 zero-padded digits
    ("FB;", r"FB\d{11};"),        # frequencyB
    ("MD;", r"MD\d;"),            # modeA: single mode digit
    ("MD$;", r"MD\$\d;"),         # modeB via the $ suffix query
    ("TQ;", r"TQ[01];"),          # transmit state
    ("FT;", r"FT[01];"),          # split
    ("RT;", r"RT[01];"),          # RIT on/off
    ("XT;", r"XT[01];"),          # XIT on/off
    ("RO;", r"RO[+-]\d{4};"),     # RIT/XIT offset, signed, 4 digits
    ("PC;", r"PC\d{3};"),         # RF power, 3 digits
    ("PCX;", r"PCX\d{3}[LHX];"),  # extended power: QRP / high / XVTR suffix
    ("GT;", r"GT\d{3};"),         # AGC speed
    ("KS;", r"KS\d{3};"),         # keyer speed in WPM
    ("NB;", r"NB[01];"),          # noise blanker
    ("NR;", r"NR[01];"),          # noise reduction
    ("VX;", r"VX[01];"),          # VOX rollup
    ("DV;", r"DV[01];"),          # diversity
    ("BW;", r"BW\d{4};"),         # filter bandwidth, 4 digits
    ("FW;", r"FW\d{8};"),         # extended filter width, 8 digits
    ("DT;", r"DT\d;"),            # data sub-mode
    ("SM;", r"SM\d{4};"),         # S-meter as a bar count, 4 digits
    ("TM;", r"TM\d{12};"),        # TX meter: 4 fields x 3 digits
    ("OM;", r"OM .{12};"),        # option modules: note the space after OM
]


def if_frame_layout(got):
    """The IF frame is byte-exact for K2/K3 parser compatibility; check the fixed literals."""
    if len(got) != 38:
        return False, f"IF must be 38 characters, got {len(got)}"
    # IF[freq:11][5 spaces][+/-][offset:4][rit][xit][space]00[tx][mode]00[split]001[space];
    if not re.fullmatch(r"IF\d{11} {5}[+-]\d{4}[01]{2} 00[01]\d00[01]001 ;", got):
        return False, "fixed literal positions do not match the documented K4 layout"
    return True, ""


def audit_fixed(conn, audit):
    for command, expected in FIXED:
        audit.check_exact(conn, "fixed", command, expected)


def audit_shape(conn, audit):
    for command, pattern in SHAPE:
        audit.check_shape(conn, "shape", command, pattern)
    audit.check_shape(conn, "shape", "IF;", r"IF.*;", extra=if_frame_layout)


def audit_framing(conn, audit):
    """Protocol behaviour that a single-process unit test cannot reach."""

    # Two commands in one packet must produce two replies, in order.
    conn.send("FA;MD;")
    first = conn.read_reply()
    second = conn.read_reply()
    ok = (
        first is not None
        and second is not None
        and first.startswith("FA")
        and second.startswith("MD")
    )
    audit.record(
        "framing",
        "two commands in one write",
        ok,
        f"{first!r} then {second!r}",
    )

    # A command split across two writes must still be answered once the terminator arrives.
    conn.send("FA")
    stray = conn.read_reply(SILENCE_TIMEOUT)
    if stray is not None:
        audit.record("framing", "no reply before terminator", False, f"answered early with {stray!r}")
    else:
        audit.record("framing", "no reply before terminator", True, "silent until ';' arrives")
    conn.send(";")
    got = conn.read_reply()
    audit.record(
        "framing",
        "command split across writes",
        got is not None and got.startswith("FA"),
        f"{got!r}",
    )

    # An unrecognised command draws no reply, and must not desynchronise the stream.
    stray = conn.expect_silence("ZZ;")
    audit.record("framing", "unknown command is silent", stray is None, f"{stray!r}")

    # An empty command (bare terminator) is skipped.
    stray = conn.expect_silence(";")
    audit.record("framing", "bare terminator is silent", stray is None, f"{stray!r}")

    # After all that nonsense the session must still work.
    got = conn.ask("ID;")
    audit.record(
        "framing",
        "session survives malformed input",
        got == "ID017;",
        f"{got!r}",
    )

    # A burst must not lose or reorder replies.
    n = 25
    conn.send("FA;" * n)
    replies = [conn.read_reply() for _ in range(n)]
    good = sum(1 for r in replies if r is not None and r.startswith("FA"))
    audit.record("framing", f"burst of {n} GETs", good == n, f"{good}/{n} answered")


def audit_session(conn, host, port, audit):
    """Per-client state and multi-client independence."""

    # A fresh client starts at AI0.
    audit.check_exact(conn, "session", "AI;", "AI0;")

    # AI is a SET with no echo, and the new level must be visible to the client that set it.
    conn.send("AI2;")
    stray = conn.read_reply(SILENCE_TIMEOUT)
    if stray is not None:
        audit.record("session", "AI SET does not echo", False, f"echoed {stray!r}")
    else:
        audit.record("session", "AI SET does not echo", True, "no echo, per K4 spec")
    got = conn.ask("AI;")
    audit.record("session", "AI level is remembered", got == "AI2;", f"{got!r}")

    # An invalid level is rejected and leaves the previous one in place.
    conn.send("AI9;")
    conn.read_reply(SILENCE_TIMEOUT)
    got = conn.ask("AI;")
    audit.record("session", "invalid AI level rejected", got == "AI2;", f"{got!r}")

    # A second client is independent: it must still be at the default.
    other = CatConnection(host, port)
    try:
        got = other.ask("AI;")
        audit.record("session", "AI mode is per-client", got == "AI0;", f"second client: {got!r}")
        got = other.ask("ID;")
        audit.record("session", "second client is served", got == "ID017;", f"{got!r}")
    finally:
        other.close()

    # The first client is unaffected by the second one leaving.
    got = conn.ask("AI;")
    audit.record("session", "unaffected by peer disconnect", got == "AI2;", f"{got!r}")

    # Leave the session as we found it.
    conn.send("AI0;")
    conn.read_reply(SILENCE_TIMEOUT)


def audit_set(conn, audit):
    """Opt-in: SET round-trips, which are the only way to test the forward-to-K4 path.

    Everything else in this file is a GET answered from RadioState's cache. A SET leaves QK4
    entirely - CatServer emits catCommandReceived and the K4 answers - so a round-trip is the only
    check that the whole path is connected, and the one thing a unit test with a fixture radio can
    never do.

    Every value read here is restored afterwards, and the restore is verified. Nothing keys the
    transmitter: frequency, mode and keyer speed are receive-side settings.
    """

    def settles_to(get_cmd, wanted, budget=3.0):
        """Poll until the radio reports `wanted`. A SET leaves QK4, reaches the K4 and comes back
        as an unsolicited update, so the only honest wait is for the value itself."""
        deadline = time.monotonic() + budget
        got = None
        while time.monotonic() < deadline:
            got = conn.ask(get_cmd)
            if got == wanted:
                return True, got
            time.sleep(0.1)
        return False, got

    def round_trip(name, get_cmd, pattern, make_change):
        """Read a value, change it, confirm the radio took the change, then put it back.

        The GET reply is itself a valid SET on the K4 - "FA00014030000;" sets what "FA;" reports -
        so the original reply is the restore command, which means the restore cannot drift from
        whatever was actually there.
        """
        original = conn.ask(get_cmd)
        if original is None or not re.fullmatch(pattern, original):
            audit.record("set", name, False, f"no starting value to work from: {original!r}")
            return

        changed = make_change(original)
        conn.send(changed)
        ok, got = settles_to(get_cmd, changed)
        audit.record("set", name, ok, f"{original} -> {got}" if ok else f"asked for {changed}, got {got}")

        conn.send(original)
        back_ok, back = settles_to(get_cmd, original)
        audit.record("set", f"{name} restored", back_ok, f"{back}")

    # Frequency: move VFO A up 1 kHz and put it back.
    round_trip(
        "FA frequency",
        "FA;",
        r"FA\d{11};",
        lambda reply: "FA%011d;" % (int(reply[2:13]) + 1000),
    )

    # Keyer speed: a receive-side setting with a narrow, well-defined range.
    round_trip(
        "KS keyer speed",
        "KS;",
        r"KS\d{3};",
        lambda reply: "KS%03d;" % (int(reply[2:5]) + 1 if int(reply[2:5]) < 50 else int(reply[2:5]) - 1),
    )


def audit_tx(conn, audit):
    """Opt-in: the transmit-owner gate.

    WHY this is safer than it looks: CatServer does NOT forward TX;/RX; to the radio. It emits
    pttRequested, which MainWindow turns into TransmitController::engage(CatClient,
    StreamedFromHere) - QK4 opens its audio gate and the audio stream itself keys the K4, which is
    what makes FT8 timing work. With no audio being streamed by this tool, nothing should key.
    The sequence still ends in RX; unconditionally.
    """
    def tq_settles_to(wanted, budget=4.0):
        """Poll TQ until it reports `wanted`.

        Deliberately not a sleep: keying travels QK4 -> radio -> back as an unsolicited update, and
        how long that takes is a property of the link, not of the server being correct. A fixed
        wait here would report a defect on a slow round trip - the exact failure this project just
        removed from tests/test_catserver.cpp.
        """
        deadline = time.monotonic() + budget
        got = None
        while time.monotonic() < deadline:
            got = conn.ask("TQ;")
            if got == wanted:
                return True, got
            time.sleep(0.1)
        return False, got

    before = conn.ask("TQ;")
    audit.record("tx", "TQ readable before keying", before in ("TQ0;", "TQ1;"), f"{before!r}")

    # TX; is a SET with no echo.
    stray = conn.expect_silence("TX;")
    audit.record("tx", "TX does not echo", stray is None, f"{stray!r}")

    keyed, got = tq_settles_to("TQ1;")
    audit.record("tx", "TX keys the radio", keyed, f"{got}")

    stray = conn.expect_silence("RX;")
    audit.record("tx", "RX does not echo", stray is None, f"{stray!r}")

    released, got = tq_settles_to("TQ0;")
    audit.record("tx", "RX returns to receive", released, f"{got}")

    # The toggle form keys from receive, and must release the same way.
    conn.send("TX/;")
    toggled, got = tq_settles_to("TQ1;")
    audit.record("tx", "TX/ toggles into transmit", toggled, f"{got}")

    conn.send("RX;")
    final_ok, got = tq_settles_to("TQ0;")
    audit.record("tx", "toggle form ends in receive", final_ok, f"{got}")

    # Belt and braces: whatever happened above, do not leave the radio keyed.
    conn.send("RX;")
    tq_settles_to("TQ0;")


def audit_stress(conn, audit):
    """Opt-in: the 1MB framing guard in catserver.cpp disconnects a client that never terminates."""
    filler = "x" * 65536
    try:
        for _ in range(20):  # 1.25MB, past K4Protocol::MAX_BUFFER_SIZE
            conn.send(filler)
        time.sleep(0.5)
        got = conn.ask("ID;", timeout=1.0)
        # Either the server dropped us (correct) or it answered, which means the guard never fired.
        audit.record(
            "stress",
            "unterminated flood is cut off",
            got is None,
            "disconnected as documented" if got is None else f"still serving: {got!r}",
        )
    except OSError as exc:
        audit.record("stress", "unterminated flood is cut off", True, f"disconnected: {exc}")


# --- selftest: proves the harness itself, with no QK4 and no radio ------------------------------

class FakeCatServer:
    """A deliberately awkward stand-in: replies in fragments, to prove the reader is terminator
    driven rather than lucky.  It implements only what the selftest asserts."""

    REPLIES = {
        "PS": "PS1;",
        "ID": "ID017;",
        "FA": "FA00014074000;",
        "MD": "MD1;",
    }

    def __init__(self):
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(4)
        self.port = self.sock.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while True:
            try:
                client, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self._session, args=(client,), daemon=True).start()

    def _session(self, client):
        buf = b""
        try:
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    return
                buf += chunk
                while b";" in buf:
                    idx = buf.index(b";")
                    command = buf[:idx].decode("ascii", "replace")
                    buf = buf[idx + 1 :]
                    reply = self.REPLIES.get(command)
                    if reply is None:
                        continue
                    # Answer one byte at a time: a fixed-wait reader would tear this in half.
                    for byte in reply.encode("ascii"):
                        client.sendall(bytes([byte]))
                        time.sleep(0.002)
        except OSError:
            return
        finally:
            client.close()

    def close(self):
        self.sock.close()


def selftest():
    print("selftest: checking the harness against a local stand-in (no QK4, no radio)")
    server = FakeCatServer()
    audit = Audit()
    conn = CatConnection(DEFAULT_HOST, server.port)
    try:
        audit.check_exact(conn, "selftest", "PS;", "PS1;")
        audit.check_exact(conn, "selftest", "ID;", "ID017;")
        audit.check_shape(conn, "selftest", "FA;", r"FA\d{11};")

        # Fragmented replies must reassemble.
        conn.send("FA;MD;")
        first, second = conn.read_reply(), conn.read_reply()
        audit.record(
            "selftest",
            "fragmented replies reassemble in order",
            first == "FA00014074000;" and second == "MD1;",
            f"{first!r} then {second!r}",
        )

        # Silence must be reported as silence, not as an empty string.
        stray = conn.expect_silence("ZZ;")
        audit.record("selftest", "silence detected", stray is None, f"{stray!r}")
    finally:
        conn.close()
        server.close()

    return report(audit, "selftest")


def report(audit, what):
    total = len(audit.results)
    failed = len(audit.failures)
    print()
    print(f"{what}: {total - failed}/{total} checks passed")
    if failed:
        print()
        print("Failures:")
        for r in audit.failures:
            print(f"  {r['group']}/{r['name']}: {r['detail']}")
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--audit", action="store_true", help="read-only conformance sweep, then exit")
    ap.add_argument("--selftest", action="store_true", help="offline check, no QK4 and no radio")
    ap.add_argument("--stress", action="store_true", help="also test the 1MB framing guard")
    ap.add_argument(
        "--allow-set",
        action="store_true",
        help="also test SET round-trips against the radio (changes and restores frequency and keyer speed)",
    )
    ap.add_argument(
        "--allow-tx",
        action="store_true",
        help="also test the TX/RX transmit gate (CatServer does not forward these to the radio)",
    )
    ap.add_argument("--json", action="store_true", help="emit results as JSON")
    ap.add_argument("--quiet", action="store_true", help="summary only")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not args.audit:
        ap.print_help()
        return 2

    audit = Audit(quiet=args.quiet or args.json)
    try:
        conn = CatConnection(args.host, args.port)
    except OSError as exc:
        print(f"could not connect to the CAT server at {args.host}:{args.port}: {exc}")
        print()
        print("Check that QK4 is running, and that the CAT server is enabled in")
        print("Settings -> Rig Control.  It binds to localhost only, so this must")
        print("run on the same machine as QK4.")
        return 2

    try:
        if not args.json:
            print(f"auditing the CAT server at {args.host}:{args.port}")
            print()
        audit_fixed(conn, audit)
        audit_shape(conn, audit)
        audit_framing(conn, audit)
        audit_session(conn, args.host, args.port, audit)
        if args.allow_set:
            audit_set(conn, audit)
        if args.allow_tx:
            audit_tx(conn, audit)
        # Stress runs last: it deliberately provokes a disconnect, so nothing can follow it.
        if args.stress:
            audit_stress(conn, audit)
    finally:
        conn.close()

    if args.json:
        print(json.dumps({"results": audit.results, "failed": len(audit.failures)}, indent=2))
        return 1 if audit.failures else 0

    return report(audit, "audit")


if __name__ == "__main__":
    sys.exit(main())

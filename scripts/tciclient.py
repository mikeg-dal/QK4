#!/usr/bin/env python3
"""
tciclient -- a hand-driven TCI client and conformance auditor for QK4's TCI server.

WHY THIS EXISTS.  QK4's unit tests drive TciServer directly on one thread, so they cannot see a
marshalling mistake in TciController, and they cannot tell you whether a reply is TRUE -- only
that it was sent.  This tool talks to the assembled application over a real socket, and its audit
mode walks the published TCI 2.0 command inventory and reports what QK4 actually answers.

    python3 scripts/tciclient.py --audit           # read-only conformance sweep
    python3 scripts/tciclient.py                   # interactive, prints all server traffic
    python3 scripts/tciclient.py --selftest        # offline, no radio, no QK4

SAFETY.  This is meant to be run against an ACTIVE RADIO, so the default posture is read-only:

  * The audit sends only reads.  It never sets a frequency, mode, split or power level.
  * Nothing keys the transmitter unless you pass --allow-tx.  In interactive mode a command that
    would key is refused with an explanation rather than silently dropped.
  * `trx;` with no arguments is a READ in the protocol and QK4 answers it without keying
    (tests/test_tciservertransmit.cpp::doesNotKeyOnAMalformedBoolean).  It is still skippable
    with --no-trx if you would rather not send it at all near a live transmitter.

NO DEPENDENCIES.  RFC 6455 framing is implemented here over a raw socket rather than pulling in a
websockets library: the framing is fifty lines, QK4's own server is the thing under test, and a
second independent implementation of the same spec is a better check than the same library on
both ends.

The WebSocket plumbing follows the same shape as TR4W-D12/scripts/tciclient.py, which was written
for the same job against a different server.

Command inventory is from TCI Protocol Ver. 2.0 (Expert Electronics, 12 January 2024).
See docs/tci-command-coverage.md for what the gaps mean.
"""

import argparse
import base64
import hashlib
import os
import socket
import struct
import sys
import threading
import time

WS_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DEFAULT_PORT = 50001

# ---------------------------------------------------------------------------------------------
# The published command inventory, by spec section.
#
# `read` is the exact text to send to READ the command, or None when the protocol defines no read
# form (notification-only, or client-to-server only).  Reads are the only thing the audit sends.
# ---------------------------------------------------------------------------------------------

INVENTORY = [
    # (section, name, read form or None, note)
    ("4.1 init", "vfo_limits", None, "server->client only"),
    ("4.1 init", "if_limits", None, "server->client only"),
    ("4.1 init", "trx_count", "trx_count;", ""),
    ("4.1 init", "channel_count", "channel_count;", "QK4 sends the PLURAL channels_count"),
    ("4.1 init", "channels_count", "channels_count;", "what real servers accept"),
    ("4.1 init", "device", "device;", ""),
    ("4.1 init", "receive_only", "receive_only;", ""),
    ("4.1 init", "modulations_list", "modulations_list;", ""),
    ("4.1 init", "protocol", "protocol;", ""),
    ("4.1 init", "ready", None, "server->client only"),

    ("4.2 bidir", "start", None, "device start"),
    ("4.2 bidir", "stop", None, "device stop"),
    ("4.2 bidir", "dds", "dds:0;", ""),
    ("4.2 bidir", "if", "if:0,0;", "IF filter tuning in panorama"),
    ("4.2 bidir", "vfo", "vfo:0,0;", ""),
    ("4.2 bidir", "modulation", "modulation:0;", ""),
    ("4.2 bidir", "trx", "trx:0;", "READ form -- must not key"),
    ("4.2 bidir", "tune", "tune:0;", "KEYS THE RADIO if set"),
    ("4.2 bidir", "drive", "drive:0;", ""),
    ("4.2 bidir", "tune_drive", "tune_drive:0;", ""),
    ("4.2 bidir", "rit_enable", "rit_enable:0;", ""),
    ("4.2 bidir", "xit_enable", "xit_enable:0;", ""),
    ("4.2 bidir", "split_enable", "split_enable:0;", ""),
    ("4.2 bidir", "rit_offset", "rit_offset:0;", ""),
    ("4.2 bidir", "xit_offset", "xit_offset:0;", "K4 shares one offset with RIT"),
    ("4.2 bidir", "rx_channel_enable", "rx_channel_enable:0,1;", "Sub RX"),
    ("4.2 bidir", "rx_filter_band", "rx_filter_band:0;", ""),
    ("4.2 bidir", "cw_macros_speed", "cw_macros_speed;", ""),
    ("4.2 bidir", "cw_macros_delay", "cw_macros_delay;", ""),
    ("4.2 bidir", "cw_keyer_speed", None, "client->server only"),
    ("4.2 bidir", "volume", "volume;", ""),
    ("4.2 bidir", "mute", "mute;", ""),
    ("4.2 bidir", "rx_mute", "rx_mute:0;", ""),
    ("4.2 bidir", "rx_volume", "rx_volume:0,0;", ""),
    ("4.2 bidir", "rx_balance", "rx_balance:0,0;", ""),
    ("4.2 bidir", "mon_volume", "mon_volume;", ""),
    ("4.2 bidir", "mon_enable", "mon_enable;", ""),
    ("4.2 bidir", "agc_mode", "agc_mode:0;", "spec vocabulary: normal/fast/off"),
    ("4.2 bidir", "agc_gain", "agc_gain:0;", ""),
    ("4.2 bidir", "rx_nb_enable", "rx_nb_enable:0;", ""),
    ("4.2 bidir", "rx_nb_param", "rx_nb_param:0;", ""),
    ("4.2 bidir", "rx_bin_enable", "rx_bin_enable:0;", "no K4 equivalent"),
    ("4.2 bidir", "rx_nr_enable", "rx_nr_enable:0;", ""),
    ("4.2 bidir", "rx_anc_enable", "rx_anc_enable:0;", "no K4 equivalent"),
    ("4.2 bidir", "rx_anf_enable", "rx_anf_enable:0;", ""),
    ("4.2 bidir", "rx_apf_enable", "rx_apf_enable:0;", ""),
    ("4.2 bidir", "rx_dse_enable", "rx_dse_enable:0;", "no K4 equivalent"),
    ("4.2 bidir", "rx_nf_enable", "rx_nf_enable:0;", ""),
    ("4.2 bidir", "lock", "lock:0;", ""),
    ("4.2 bidir", "sql_enable", "sql_enable:0;", ""),
    ("4.2 bidir", "sql_level", "sql_level:0;", "spec range: -140..0 dBm"),
    ("4.2 bidir", "digl_offset", "digl_offset;", ""),
    ("4.2 bidir", "digu_offset", "digu_offset;", ""),

    ("4.3 unidir", "tx_enable", "tx_enable:0;", ""),
    ("4.3 unidir", "cw_macros_speed_up", None, "client->server only"),
    ("4.3 unidir", "cw_macros_speed_down", None, "client->server only"),
    ("4.3 unidir", "spot", None, "client->server only"),
    ("4.3 unidir", "spot_delete", None, "client->server only"),
    ("4.3 unidir", "spot_clear", None, "client->server only"),
    ("4.3 unidir", "iq_samplerate", "iq_samplerate;", ""),
    ("4.3 unidir", "audio_samplerate", "audio_samplerate;", ""),
    ("4.3 unidir", "iq_start", None, "starts a stream"),
    ("4.3 unidir", "iq_stop", None, "stops a stream"),
    ("4.3 unidir", "audio_start", None, "starts a stream"),
    ("4.3 unidir", "audio_stop", None, "stops a stream"),
    ("4.3 unidir", "line_out_start", None, "starts a stream"),
    ("4.3 unidir", "line_out_stop", None, "stops a stream"),
    ("4.3 unidir", "line_out_recorder_start", None, "server-side recording"),
    ("4.3 unidir", "line_out_recorder_save", None, "server-side recording"),
    ("4.3 unidir", "line_out_recorder_break", None, "server-side recording"),
    ("4.3 unidir", "audio_stream_sample_type", "audio_stream_sample_type;", ""),
    ("4.3 unidir", "audio_stream_channels", "audio_stream_channels;", ""),
    ("4.3 unidir", "audio_stream_samples", "audio_stream_samples;", ""),
    ("4.3 unidir", "tx_stream_audio_buffering", "tx_stream_audio_buffering;", ""),

    ("4.4 notify", "clicked_on_spot", None, "server->client only"),
    ("4.4 notify", "rx_clicked_on_spot", None, "server->client only"),
    ("4.4 notify", "tx_footswitch", None, "server->client only"),
    ("4.4 notify", "tx_frequency", None, "server->client only"),
    ("4.4 notify", "app_focus", None, "server->client only"),
    ("4.4 notify", "set_in_focus", None, "client->server only"),
    ("4.4 notify", "keyer", None, "client->server only"),
    ("4.4 notify", "rx_sensors_enable", None, "client->server only"),
    ("4.4 notify", "tx_sensors_enable", None, "client->server only"),
    ("4.4 notify", "rx_sensors", None, "server->client only"),
    ("4.4 notify", "tx_sensors", None, "server->client only"),

    ("4.5 new 2.0", "vfo_lock", "vfo_lock:0,0;", "new in TCI 2.0"),
    ("4.5 new 2.0", "rx_channel_sensors", None, "server->client only, new in 2.0"),

    ("3.2 CW", "cw_macros", None, "SENDS CW ON THE AIR"),
    ("3.2 CW", "cw_msg", None, "SENDS CW ON THE AIR"),
    ("3.2 CW", "cw_terminal", None, "holds TX between macros"),
    ("3.2 CW", "cw_macros_stop", None, "client->server only"),
    ("3.2 CW", "cw_macros_empty", None, "server->client only"),
    ("3.2 CW", "callsign_send", None, "server->client only"),
]

# Commands that key the transmitter or put RF out. Refused in interactive mode without --allow-tx.
TX_COMMANDS = {"trx", "tune", "cw_macros", "cw_msg", "cw_terminal"}


# ---------------------------------------------------------------------------------------------
# RFC 6455
# ---------------------------------------------------------------------------------------------

def accept_key(client_key: bytes) -> str:
    return base64.b64encode(hashlib.sha1(client_key + WS_GUID).digest()).decode()


def handshake(sock, host, port, resource="/"):
    key = base64.b64encode(os.urandom(16))
    req = (
        f"GET {resource} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key.decode()}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).encode()
    sock.sendall(req)

    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("server closed during handshake")
        buf += chunk
    head, _, rest = buf.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    if b"101" not in lines[0]:
        raise RuntimeError("handshake refused: %s" % lines[0].decode(errors="replace"))

    got = ""
    for ln in lines[1:]:
        name, _, value = ln.partition(b":")
        if name.strip().lower() == b"sec-websocket-accept":
            got = value.strip().decode()
    want = accept_key(key)
    if got != want:
        # Not pedantry: a proxy or a plain HTTP server can answer 101 without being a WebSocket
        # peer. Verifying the digest is what makes the handshake mean something.
        raise RuntimeError("Sec-WebSocket-Accept mismatch: got %r, want %r" % (got, want))
    return rest


def encode_text(payload: bytes) -> bytes:
    """A client MUST mask every frame it sends (RFC 6455 5.3)."""
    header = bytearray([0x81])
    n = len(payload)
    if n <= 125:
        header.append(0x80 | n)
    elif n <= 0xFFFF:
        header.append(0x80 | 126)
        header += struct.pack(">H", n)
    else:
        header.append(0x80 | 127)
        header += struct.pack(">Q", n)
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return bytes(header) + mask + masked


def decode_frames(buf: bytes):
    """Yield (opcode, payload) for every COMPLETE frame; return the remainder."""
    out = []
    while True:
        if len(buf) < 2:
            break
        b0, b1 = buf[0], buf[1]
        opcode = b0 & 0x0F
        masked = bool(b1 & 0x80)
        ln = b1 & 0x7F
        i = 2
        if ln == 126:
            if len(buf) < 4:
                break
            ln = struct.unpack(">H", buf[2:4])[0]
            i = 4
        elif ln == 127:
            if len(buf) < 10:
                break
            ln = struct.unpack(">Q", buf[2:10])[0]
            i = 10
        if masked:
            # A server must not mask. Say so rather than silently coping.
            raise RuntimeError("server sent a MASKED frame -- protocol violation")
        if len(buf) < i + ln:
            break
        out.append((opcode, buf[i:i + ln]))
        buf = buf[i + ln:]
    return out, buf


class Connection:
    """A TCI connection with a blocking, frame-at-a-time read."""

    def __init__(self, host, port, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = handshake(self.sock, host, port)
        self.timeout = timeout
        # decode_frames() consumes EVERY complete frame in the buffer, and the init burst arrives
        # as many frames in one TCP segment. Without somewhere to park the surplus, returning the
        # first would silently discard the rest.
        self.pending = []

    def send(self, text):
        self.sock.sendall(encode_text(text.encode()))

    def recv_text(self, timeout=None):
        """Next TEXT payload as str, or None on timeout."""
        deadline = time.time() + (self.timeout if timeout is None else timeout)
        while True:
            while self.pending:
                op, payload = self.pending.pop(0)
                if op == 0x1:
                    return payload.decode(errors="replace")
                # opcode 2 is binary audio; ignore here.
            frames, self.buf = decode_frames(self.buf)
            if frames:
                self.pending.extend(frames)
                continue
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(65536)
            except (socket.timeout, TimeoutError):
                return None
            if not chunk:
                return None
            self.buf += chunk

    def drain_burst(self, terminator="ready;", timeout=5.0):
        """Collect the init burst up to and including `terminator`."""
        out = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = self.recv_text(timeout=max(0.1, deadline - time.time()))
            if m is None:
                break
            out.append(m)
            if m == terminator:
                break
        return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------------------------
# Audit
# ---------------------------------------------------------------------------------------------

def audit(conn, include_trx=True, quiet=False):
    """Send every READ the spec defines and report what comes back. Sends no SETs."""
    burst = conn.drain_burst()
    if not burst or burst[-1] != "ready;":
        print("!! init burst did not terminate in ready; -- got %d commands" % len(burst))
    # A name can appear in the burst MORE THAN ONCE - vfo is sent for channel 0 and channel 1 -
    # so collect every advertised string per name. Collapsing them to one made a channel-0 reply
    # look like it disagreed with the channel-1 entry that happened to be last.
    advertised = {}
    for c in burst:
        advertised.setdefault(c.split(":", 1)[0].rstrip(";"), set()).add(c)
    print("init burst: %d commands, %d distinct names\n" % (len(burst), len(advertised)))

    results = []
    for section, name, read, note in INVENTORY:
        if read is None:
            results.append((section, name, "n/a", "", note))
            continue
        if name == "trx" and not include_trx:
            results.append((section, name, "skipped", "", "--no-trx"))
            continue
        conn.send(read)
        # The reply MUST carry the name we asked about. Taking whatever arrives next counts a
        # straggler from the previous command as an answer to this one - which is exactly how
        # vfo_lock, which is genuinely silent, was reported as implemented once enough other
        # commands started answering and the traffic increased.
        #
        # dds is the one legitimate exception: the server answers it with a vfo reply, because
        # dds is an alias for the receive VFO.
        accepted = {name, "vfo"} if name == "dds" else {name}
        reply = None
        deadline = time.time() + 0.5
        while time.time() < deadline:
            candidate = conn.recv_text(timeout=0.25)
            if candidate is None:
                break
            if candidate.split(":", 1)[0].rstrip(";") in accepted:
                reply = candidate
                break
        if reply is None:
            status = "SILENT"
            value = ""
        else:
            status = "answered"
            value = reply
        results.append((section, name, status, value, note))
        if not quiet:
            mark = "  " if status == "answered" else "!!"
            print("%s %-28s %-9s %s" % (mark, name, status, value))

    # ---- report -----------------------------------------------------------------------------
    print("\n" + "=" * 92)
    print("CONFORMANCE SUMMARY  (TCI Protocol Ver. 2.0)")
    print("=" * 92)

    by_section = {}
    for section, name, status, value, note in results:
        by_section.setdefault(section, []).append((name, status, value, note))

    total_readable = total_answered = 0
    for section in sorted(by_section):
        rows = by_section[section]
        readable = [r for r in rows if r[1] in ("answered", "SILENT")]
        answered = [r for r in readable if r[1] == "answered"]
        total_readable += len(readable)
        total_answered += len(answered)
        print("\n%s -- %d/%d readable commands answered" % (section, len(answered), len(readable)))
        for name, status, value, note in rows:
            if status == "SILENT":
                suffix = ("   (%s)" % note) if note else ""
                print("    NOT IMPLEMENTED  %-28s%s" % (name, suffix))

    print("\n" + "-" * 92)
    print("readable commands answered: %d/%d" % (total_answered, total_readable))

    # Consistency: a reply must agree with what the burst advertised.
    mismatches = []
    for section, name, status, value, note in results:
        if status != "answered":
            continue
        # Compare against the burst entries for the name the SERVER replied with, not the one we
        # sent: dds is an alias for the receive VFO and is legitimately answered with a vfo reply.
        replied_name = value.split(":", 1)[0].rstrip(";")
        entries = advertised.get(replied_name)
        if entries and value not in entries:
            mismatches.append((name, replied_name, sorted(entries), value))
    if mismatches:
        print("\n!! replies that DISAGREE with the init burst:")
        for name, replied_name, entries, reply in mismatches:
            alias = "" if name == replied_name else " (answered as %s)" % replied_name
            print("     %-20s%s burst=%s reply=%s" % (name, alias, ",".join(entries), reply))
    else:
        print("every reply agrees with the init burst")

    # Spec conformance checks that a bare "did it answer" sweep would miss.
    print("\nvalue checks:")
    checked = 0
    for section, name, status, value, note in results:
        if status != "answered":
            continue
        parts = value.rstrip(";").split(":", 1)
        arg = parts[1] if len(parts) > 1 else ""
        if name == "agc_mode":
            checked += 1
            mode = arg.split(",")[-1]
            ok = mode in ("normal", "fast", "off")
            print("    %-28s %-12s %s" % (name, mode, "OK" if ok else "NOT a spec value (normal/fast/off)"))
        elif name == "sql_level":
            checked += 1
            try:
                lvl = float(arg.split(",")[-1])
                ok = -140.0 <= lvl <= 0.0
            except ValueError:
                ok = False
                lvl = arg
            print("    %-28s %-12s %s" % (name, lvl, "OK" if ok else "OUT OF RANGE (spec: -140..0 dBm)"))
        elif name == "volume":
            checked += 1
            try:
                lvl = float(arg.split(",")[-1])
                ok = -60.0 <= lvl <= 0.0
            except ValueError:
                ok = False
                lvl = arg
            print("    %-28s %-12s %s" % (name, lvl, "OK" if ok else "OUT OF RANGE (spec: -60..0 dB)"))
        elif name in ("drive", "tune_drive"):
            checked += 1
            fields = arg.split(",")
            # A bare "drive:0;" crashes ESDR3-mode WSJT-X and JTDX, which index args[1]
            # unconditionally.
            ok = len(fields) == 2
            print("    %-28s %-12s %s" % (name, arg, "OK" if ok else "MUST be <trx>,<power>"))
    if checked == 0:
        print("    (none of the checked commands answered)")

    return results


# ---------------------------------------------------------------------------------------------
# Interactive
# ---------------------------------------------------------------------------------------------

def interactive(conn, allow_tx):
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            m = conn.recv_text(timeout=0.5)
            if m is not None:
                sys.stdout.write("\r< %s\n> " % m)
                sys.stdout.flush()

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    print("Type a TCI command (trailing ';' optional). 'quit' to exit.")
    if not allow_tx:
        print("Transmit commands are REFUSED without --allow-tx: %s" % ", ".join(sorted(TX_COMMANDS)))
    try:
        while True:
            line = input("> ").strip()
            if not line:
                continue
            if line in ("quit", "exit"):
                break
            name = line.split(":", 1)[0].rstrip(";").lower()
            # A bare read of trx is safe; a set with arguments is what keys.
            keys_tx = name in TX_COMMANDS and ":" in line and "," in line
            if keys_tx and not allow_tx:
                print("  REFUSED: '%s' would key the transmitter. Re-run with --allow-tx." % name)
                continue
            if not line.endswith(";"):
                line += ";"
            conn.send(line)
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        stop.set()


# ---------------------------------------------------------------------------------------------
# Self-test -- no radio, no QK4
# ---------------------------------------------------------------------------------------------

def selftest():
    """Run the client against a minimal in-process server: handshake plus both directions."""
    import http.server  # noqa: F401  (kept local; only the socket layer is used)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    failures = []

    def server():
        conn, _ = srv.accept()
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += conn.recv(4096)
        key = ""
        for ln in buf.split(b"\r\n"):
            n, _, v = ln.partition(b":")
            if n.strip().lower() == b"sec-websocket-key":
                key = v.strip()
        conn.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept_key(key).encode() + b"\r\n\r\n"
        )
        # Unmasked server frames, as RFC 6455 requires of a server.
        def frame(text):
            p = text.encode()
            return bytes([0x81, len(p)]) + p
        for cmd in ("device:QK4;", "drive:0,100;", "ready;"):
            conn.sendall(frame(cmd))
        # Echo one client command back so the send path is exercised too.
        data = conn.recv(4096)
        b1 = data[1]
        ln = b1 & 0x7F
        mask = data[2:6]
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(data[6:6 + ln]))
        conn.sendall(frame("echo:" + payload.decode().rstrip(";") + ";"))
        time.sleep(0.2)
        conn.close()

    t = threading.Thread(target=server, daemon=True)
    t.start()

    conn = Connection("127.0.0.1", port, timeout=3.0)
    burst = conn.drain_burst()
    if burst != ["device:QK4;", "drive:0,100;", "ready;"]:
        failures.append("burst mismatch: %r" % burst)

    conn.send("protocol;")
    reply = conn.recv_text(timeout=2.0)
    if reply != "echo:protocol;":
        failures.append("echo mismatch: %r" % reply)
    conn.close()
    srv.close()

    # Framing round-trip, including the two extended length forms.
    for n in (5, 200, 70000):
        text = b"x" * n
        encoded = encode_text(text)
        # Unmask it the way a server would, then check the payload survived.
        b1 = encoded[1] & 0x7F
        i = 2 + (2 if b1 == 126 else 8 if b1 == 127 else 0)
        mask = encoded[i:i + 4]
        body = encoded[i + 4:]
        if bytes(b ^ mask[j % 4] for j, b in enumerate(body)) != text:
            failures.append("masking round-trip failed at n=%d" % n)

    if failures:
        for f in failures:
            print("FAIL: %s" % f)
        return 1
    print("selftest: OK (handshake, burst, echo, masking round-trip at 3 length forms)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--audit", action="store_true", help="read-only conformance sweep, then exit")
    ap.add_argument("--selftest", action="store_true", help="offline check, no radio and no QK4")
    ap.add_argument("--allow-tx", action="store_true", help="permit commands that key the transmitter")
    ap.add_argument("--no-trx", action="store_true", help="skip even the READ form of trx during --audit")
    ap.add_argument("--quiet", action="store_true", help="audit: summary only")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    try:
        conn = Connection(args.host, args.port)
    except OSError as e:
        print("cannot connect to %s:%d -- %s" % (args.host, args.port, e))
        print("is QK4 running with the TCI server enabled?")
        return 1

    try:
        if args.audit:
            audit(conn, include_trx=not args.no_trx, quiet=args.quiet)
        else:
            for line in conn.drain_burst():
                print("< %s" % line)
            interactive(conn, args.allow_tx)
    finally:
        conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

# K4 Protocol Quirks — Canonical Reference

This document catalogs non-obvious K4 CAT behaviors that shape the QK4 code base. Each quirk has a *symptom*, the *K4 behavior* that causes it, and *where QK4 encodes the workaround* (with file:line so a maintainer can trace it).

Sources: every behaviour here was verified by driving a K4/0 server directly over a Python socket and reading what came back — QK4's own code is never the source of truth for this document. Raw session transcripts are kept in the maintainer's private notes; the findings, not the transcripts, are what this file records.

---

## 1. `$` suffix convention

**Symptom.** Same command, two variants: `MD` / `MD$`, `BW` / `BW$`, `RO` / `RO$`, `RT` / `RT$`, `#REF` / `#REF$`, etc.

**K4 behavior.** Commands with a `$` suffix act on the **sub receiver (VFO B)**; without the suffix, they act on the main receiver (VFO A). This applies to both queries and set commands.

**QK4 encoding.**
- `src/models/radiostate.cpp` — the handler registry. See preamble comment in `registerCommandHandlers()` (file top of that function). Registry sorts **longest prefix first** so dispatch correctly prefers `RO$` over `RO`. Each `$` variant typically updates a `*B` member and emits a `*BChanged` signal.
- `src/models/radiostate.h` — class-level Doxygen comment documents the convention.

**Rules for new parser work.** Add the `$` variant's entry **before** the base variant in the registry (longer-prefix-first), and update/test both.

---

## 2. `RO` vs `RO$` RIT/XIT offset routing

**Symptom.** Setting an XIT offset sometimes ends up in `RO`, sometimes in `RO$`, depending on split / BSET state. Raw RU/RD adjust a register whose identity depends on the current mode.

**K4 behavior (verified by direct socket session):**
- No split, RIT or XIT active → offset lives in `RO`  (VFO A).
- Split + XIT (TX on VFO B)  → offset lives in `RO$` (VFO B).
- BSET + RIT                 → offset lives in `RO$` (VFO B).

`RU;` / `RD;` (increment/decrement) adjust *whichever register is currently active* and echo that register's name back (`RO` or `RO$`). `XT/;` (toggle) echoes `XT1;`/`XT0;`, but `XT1;` / `XT0;` (set) do *not* echo. `RC;` clears `RO`; to clear `RO$` send `RO$+0000;`.

**QK4 encoding.**
- `src/models/radiostate.cpp` — `RO$` handler at the inline lambda near L1095 (documented inline); `RT$` at L1112; base `RO`/`RT` handlers later in the registry.
- UI consumption: see MainWindow's RIT/XIT box + BSET-aware routing.

**Gotcha.** When testing via direct socket, make sure QK4 is not also connected to the same K4 — interleaved commands confuse the test.

---

## 3. `SL` (streaming latency) is not echoed

**Symptom.** You send `SL3;` to the K4; the K4 accepts it silently; the UI never updates from any response.

**K4 behavior.** The K4 applies the new SL tier but does not echo an `SL<n>;` response. There is no query form that returns the current tier either. The tier → packet-size map was verified by socket session: four distinct tiers, bundling 20 / 40 / 60 / 120 ms of audio per packet.

**QK4 encoding.**
- `src/mainwindow.cpp::onRadioReady` — after the `RDY;` dump, QK4 sends its configured `SL` tier *and* calls `m_radioState->parseCATCommand("SL<n>;")` optimistically so RadioState (and everything downstream — AudioEngine frame sizing, the UI menu) matches what we just requested. Long comment block there explains why this post-RDY override is necessary.
- Tier changes at runtime (from the radio manager dialog) go through `ConnectionController` → `RadioState::streamingLatencyChanged`, which AudioController listens to for TX Opus frame-size reconfiguration.

---

## 4. `TX;` / `RX;` control

**Symptom.** `TX;` and `RX;` from WSJT-X / MacLoggerDX need to drive QK4's internal PTT state and TX audio gate — not just forward to the K4.

**K4 behavior.** `TX;` puts the radio in transmit; `RX;` returns it to receive. These are the canonical PTT commands over CAT.

**QK4 encoding.**
- `src/network/catserver.h` — `CatServer::pttRequested(bool)` signal emitted on parsed `TX;`/`RX;` from external CAT clients.
- `src/mainwindow.cpp` — `sendCAT("TX;"|"RX;")` for internal PTT button flows at `:2601`; the disconnect path sends `RX;` (`:93`) to unlock both the K4 and QK4's internal gate.
- Tests: `tests/test_catserver.cpp` covers the `TX;` / `RX;` → `pttRequested` pathway end-to-end over a real TCP loopback socket.

---

## 5. Iambic latch semantics (internal keyer quirk)

**Symptom.** Brief Iambic-A paddle taps would drop the character if naïve latch handling were used.

**K4 behavior.** Not a K4 quirk — a keyer-state-machine quirk. Iambic A terminates the character immediately when both paddles are released; Iambic B emits one more element. With atomic paddle state read off the hardware thread, the keyer's element-timer fire can race a paddle-release that already happened.

**QK4 encoding.**
- `src/hardware/iambickeyer.cpp::enterElement` (~L96) — clears **only** the just-consumed paddle's latch, preserves the opposite-paddle latch. Long WHY comment there explains the Iambic-A correctness argument.
- `src/hardware/iambickeyer.h` — class-level Doxygen points readers at this quirk.

See section 8 below for the KZ keying protocol that carries these elements to the K4.

---

## 6. Packet framing: 4-byte marker + 4-byte length + 4-byte end

**Symptom.** TCP reads can land mid-packet; the parser has to reassemble across boundaries.

**K4 behavior (K4/0 server side).** All audio / spectrum / CAT data is wrapped in fixed frames:
`[START_MARKER (0xFE 0xFD 0xFC 0xFB)] [big-endian u32 payload length] [payload] [END_MARKER (0xFB 0xFC 0xFD 0xFE)]`.
Markers are mirror-imaged so corruption can't alias one for the other.

**QK4 encoding.**
- `src/network/protocol.h` — class/namespace header comment documents the frame format.
- `src/network/protocol.cpp::parse` — the **"keep last 3 bytes"** branch when no marker is found is load-bearing: `START_MARKER` is 4 bytes; if a TCP read delivered 1–3 of those bytes, dropping them would lose sync on the next read. Inline WHY comment.
- Buffer overflow recovery: `K4Protocol::MAX_BUFFER_SIZE = 1 MB` cap at `protocol.h:32`; overflow clears the buffer and warns.

---

## 7. ARP cold-start retry (transport layer, not protocol — keep in mind)

Not a protocol quirk but a startup-reliability one worth mentioning alongside:

**Symptom.** First connect from a freshly-launched QK4 (after Finder launch / `open` command) fails synchronously with `EHOSTUNREACH` on macOS.

**Cause.** macOS `connect(2)` returns `EHOSTUNREACH` immediately when the ARP cache has no MAC for the destination IP. The ARP request *is* sent; the reply just lands too late for that first `connect()`.

**QK4 encoding.** `src/network/tcpclient.cpp`, in the socket-error handler — retry after 500 ms, up to 2 attempts, via the named constants `kArpRetryIntervalMs` and `kArpMaxRetries`. Not a workaround; correct handling of a real network transient.

---

## 8. KZ keying protocol

Sidebar — not directly a protocol *quirk*, but non-obvious enough to belong here.

| Command | Meaning |
|---|---|
| `KZ.;` | dit element (1 dit on) |
| `KZ-;` | dah element (3 dits on) |
| `KZ ;` | letter space |
| `KZP####;` | pause N ms before the next element (fist preservation) |
| `KZL##;` | element length in ms (`1200 / WPM`) |
| `KZD####;` / `KZU####;` | raw key-down / key-up for N ms. K4/0 uses this form for real-time paddle forwarding; QK4 does not |

**The byte between `Z` and `;` in the letter-space form is 0x20, a literal SPACE.** Confirmed by
hexdump of EP02 traffic from a live KPOD+. Elecraft's `KPodKeyerInterface.pdf` v1.00 renders it as
`KZ_;`, which looks like an underscore and is a typographic artifact — sending a real underscore
(0x5F) does **not** work.

Undocumented contest behaviour (confirmed by N6TV): inside an element-form string such as
`KZ-.-. --.-  -.;`, one blank is a 3-dit letter space and **two blanks are a 6-dit word space** —
tighter than the ITU 7-dit default.

QK4 emits these from `IambicKeyer` on its own high-priority thread, wired to the socket in
`CwController`; see `src/hardware/README.md` for the keying chain.

---

## 9. PAN spectrum tiers — the radio picks the bandwidth, not the span

The K4 always sends **1024 bins**, whatever span you ask for. What changes is the bandwidth those
bins cover, chosen by the radio from five tiers. `sampleRate` in the PAN packet is in **kHz**, and
`tierSpanHz = sampleRate * 1000`.

| Tier | `sampleRate` (kHz) | Tier span | Switches up at | Switches down at |
|---|---|---|---|---|
| 1 | 24 | 24 kHz | — | ≤ 19 kHz |
| 2 | 48 | 48 kHz | 20 kHz | ≤ 36 kHz |
| 3 | 96 | 96 kHz | 37 kHz | ≤ 82 kHz |
| 4 | 192 | 192 kHz | 83 kHz | ≤ 172 kHz |
| 5 | 384 | 384 kHz | 176 kHz | — |

The up and down thresholds differ deliberately — that hysteresis stops the tier oscillating when you
sit on a boundary. All 1024 bins are usable; there is no edge taper or filter rolloff to mask.

**Why it matters to QK4.** A stored waterfall row is only meaningful alongside the tier it was
captured at: the same 1024 bins mean 24 kHz of spectrum in tier 1 and 384 kHz in tier 5. The
renderer windows every row with one tier value, so `PanadapterRhiWidget` clears the waterfall when
`sampleRate` changes — otherwise a tier change would silently redraw all history at the wrong
frequency scale.

## 10. Span dial skips 6 kHz going up

Stepping **up** from 5 kHz the K4 jumps to 7 kHz. Stepping **down** it walks 7 → 6 → 5 in normal
1 kHz steps, so 6 kHz is reachable descending but skipped ascending. The K4 advertises 6 kHz as its
minimum span. Step size is 1 kHz from 5–144 kHz and 4 kHz from 144–368 kHz.

**QK4 does not currently replicate the skip** — `RadioUtils::getNextSpanUp` applies a flat 1 kHz
step below the threshold, so it lands on 6 kHz where the radio would not.

---

## How to extend this document

Add a new section only when a behavior satisfies **all** of:
1. A direct socket-session test confirms the behavior (QK4 alone cannot be the source of truth).
2. QK4 encodes a workaround or decision based on it (file:line reference required).
3. The behavior is non-obvious from reading the code alone.

Each section must cite the file where the workaround lives. Prefer naming the function or constant
over a line number — line numbers rot silently, and a stale one sends the reader to unrelated code.

Do **not** add sections for generic CAT commands whose behavior is documented in Elecraft's official programmer's reference.

# CW by CAT — bench log

Raw evidence behind §5 and §12.7 of [tci-command-coverage.md](tci-command-coverage.md). Kept
verbatim because every line of it cost transmitter time on a real K4, and because the conclusions
it supports are the kind that get quietly reversed later by someone who has only read the summary.

**Date:** 2026-09-16 · **Radio:** K4 serial 278 at 192.168.73.108 · **Operator:** NY4I ·
**Keyer speed:** 20–21 WPM

Two instruments, deliberately:

- **through QK4** — QLog or `tcitester.py --cw` → TCI → QK4 → CAT. Tests QK4's translation.
- **direct to the radio** — `k4kytest.py` on port 9200, plain CAT, no QK4 and no TCI in between.
  Tests the *radio's* behaviour, so a result cannot be confused with a bug in ours.

Lines marked `>>` and `<<` are the wire. Anything about how it *sounded* is the operator's ear and
is labelled as such — the log cannot hear.

---

## 1. Through QK4 — plain text

TCI in:

```
net.tci: client 1 CW: "TEST" in 1 speed segment(s)
```

CAT out:

```
CAT.TX: sent: "KY TEST                  ;" ( 41 bytes)
```

26 characters: `KY` + blank flag + 22-character body (`TEST` + 18 pad) + `;`. One command, no `KS`,
blank flag rather than `W` — all three correct for a macro with no speed markers.

**Heard:** keyed correctly.

---

## 2. Through QK4 — speed markers and a prosign

TCI in:

```
net.tci: client 2 CW: ">TU >599 004 |SK|" in 2 speed segment(s)
```

CAT out:

```
CAT.TX: sent: "KS026;"                     base 21 + 5
CAT.TX: sent: "KYWTU                    ;"  W, because a KS follows
CAT.TX: sent: "KS031;"                     +5 more, cumulative
CAT.TX: sent: "KYW599 004 *             ;"  |SK| -> *
CAT.TX: sent: "KS021;"                     restored to base
```

**Heard:** "speed stepped up and SK was clean" — so `*` keyed as the SK prosign rather than as a
literal character, and the cumulative +5/+5 is audible.

Note the base was 21 WPM, not 25: the markers are relative to whatever the radio is set to.

---

## 3. Through QK4 — QLog, the real client

QLog's UI reported sending one message. TCI confirms one command:

```
net.tci: client 4 CW: "CQ CQ CQ DE NY4I NY4I NY4I K" in 1 speed segment(s)
```

CAT out — **QK4** split it, not QLog:

```
CAT.TX: sent: "KY CQ CQ CQ DE NY4I NY4I;" ( 40 bytes)
CAT.TX: sent: "KY  NY4I K               ;" ( 41 bytes)
```

28 characters, cut at the last space at or before 22 (index 21). Chunk 1 is 21 characters and
unpadded; chunk 2 is ` NY4I K` padded to 22. The **two** spaces after `KY` in the second command are
the blank flag plus the chunk's own leading space — the word gap carried forward rather than left
at the end of chunk 1, where the radio would trim it.

**Heard:** "sounded fine, no run-together" — which is the only proof that carrying the space works.

---

## 4. Direct to the radio — how much fits in one KY

| Text | Length | `--chunk` | Result |
|---|---|---|---|
| `CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TES` | 60 | 0 (unsplit) | keyed in full |
| `CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TEST NY4I K` | 68 | 0 (unsplit) | keyed in full |

The manual says "0 to 60 characters". **68 worked.** That is undefined behaviour which happens to
work on this firmware, not a licence to exceed the documented limit — but it does show 60 is a soft
boundary with headroom behind it rather than a cliff.

---

## 5. Direct to the radio — four commands back-to-back, no throttle

**The test that decided a design question.** The worry: consecutive `KY` commands might overflow the
radio's buffer and silently drop text. The `KY;` → `KY1;` (buffer full) query exists precisely
because that is possible. The proposed guard was `KYW` on every chunk but the last, making the
sends self-throttling.

```
python k4kytest.py --wait 10 --chunk 22 --pad 22 --kyw none \
  "CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TEST NY4I NY4I CQ TEST NY4I K"

connected to 192.168.73.108:9200, keyer speed 20 WPM
68 characters -> 4 KY command(s), 4 command(s) in all:
    'KY CQ TEST NY4I NY4I CQ;'                     (20 chars of text)
    'KY  TEST NY4I NY4I CQ;'                       (18 chars of text)
    'KY  TEST NY4I NY4I CQ;'                       (18 chars of text)
    'KY  TEST NY4I K          ;'                   (22 chars of text)
  starting now
+  0.00s  >> KY CQ TEST NY4I NY4I CQ;
+  0.00s  >> KY  TEST NY4I NY4I CQ;
+  0.00s  >> KY  TEST NY4I NY4I CQ;
+  0.00s  >> KY  TEST NY4I K          ;
+  0.05s  << TQ0;
+  0.05s  << TB906AO E B;
+  0.12s  << TQ1;
+  0.12s  << TB900;
+  2.02s  << TQ0;
+  2.14s  << TQ1;
+  3.77s  << TQ0;
+  3.89s  << TQ1;
+  6.64s  << TQ0;
+  6.75s  << TQ1;
+  9.54s  << TQ0;
+  9.65s  << TQ1;
+ 11.57s  << TQ0;
+ 11.71s  << TQ1;
+ 13.26s  << TQ0;
+ 13.39s  << TQ1;
+ 16.06s  << TQ0;
+ 16.18s  << TQ1;
+ 21.06s  << TQ0;
+ 21.18s  << TQ1;
+ 22.70s  << TQ0;
+ 22.81s  << TQ1;
+ 25.61s  << TQ0;
+ 25.73s  << TQ1;
+ 28.44s  << TQ0;
+ 28.55s  << TQ1;
+ 30.55s  << TQ0;
+ 30.67s  << TQ1;
+ 31.81s  << TB800;
+ 32.16s  << TQ0;
+ 32.16s  << TB700;
+ 32.27s  << TQ1;
+ 32.27s  << TB600;
+ 32.75s  << TB500;
+ 33.90s  << TB400;
+ 34.59s  << TB300;
+ 34.97s  << TB200;
+ 35.07s  << TQ0;
+ 35.18s  << TQ1;
+ 35.30s  << TB100;
+ 35.89s  << TB000;
+ 36.00s  << TQ0;

============================================================
  transmitted for 35.9s
```

**Heard:** "sent as if I sent it as a single message. no interruption at all. I could not even tell."

### What this shows

- **All four commands were accepted at +0.00s.** No `KY1;` anywhere in the responses, so the buffer
  never reported full.
- **`TB` drains cleanly** from its high-water reading to `TB000`, then `TQ0` settles. Nothing was
  left stranded.
- **The chunks reassemble exactly** to the 68 characters sent, each later chunk carrying its leading
  space.
- **No throttle is needed.** `KYW` stays reserved for the case the manual names — a following `KS`.

### The second argument, which nobody predicted

That message **transmitted for 35.9 seconds**. `KYW` delays the radio's processing of every
following host command until the current one has been keyed. The proposed guard would therefore have
blocked QK4's polling, meter reads and any operator-triggered CAT for the whole 36 seconds — a long
blackout, bought to prevent a failure the radio had just demonstrated it does not have.

### Not a defect

`TQ` toggles between 0 and 1 about fourteen times during the message, roughly 110–130 ms each. That
is QSK dropping to receive between words. It matches what the operator heard, which was one
uninterrupted message.

---

## 6. Does `KS` reach text already in the buffer? — yes, and it settles `KYW`

The question behind the whole `KYW` policy: if a speed command cannot affect text the radio has
already accepted, then `KYW` is unnecessary everywhere and could come out — taking its costs with
it. Tested by sending the identical command sequence twice, once with the wait flags and once
without (`--kyw none` overrides the plan).

```
python k4kytest.py --wait 2 --qk4            "TEST >FAST <AGAIN"    5 commands, KYW where a KS follows
python k4kytest.py --wait 2 --qk4 --kyw none "TEST >FAST <AGAIN"    the same 5, no W anywhere
```

| Run | Heard |
|---|---|
| with `KYW` | **"worked perfectly"** — 20, then FAST at 25, then AGAIN back at 20 |
| `--kyw none` | **"the speed never changed"** — all three segments at one speed |

Re-run after the chunk length went to 60 and the padding was dropped, the commands became
`KYWTEST ;` / `KS025;` / `KYWFAST ;` / `KS020;` / `KY AGAIN;` — five characters of text each,
unpadded — and it still keyed **20 / 25 / 20**. So neither the larger chunk nor the loss of padding
disturbs the speed handling.

**`KS` DOES reach buffered text.** Without the wait flag every speed command was processed the
moment it arrived, so by the time any text was keyed the final `KS020;` had already landed and the
whole message came out at 20 WPM. The markers silently did nothing.

So `KYW` is load-bearing and the rule stands: **wait flag exactly when a `KS` follows this text,
never otherwise.** It also disposes of an appealing simplification — making the LAST text always a
plain `KY ` — because when a macro ends away from base, the restore is a following `KS` and would
land early, keying the final segment at base speed.

### The cost, measured

`KYW` holds *every* following host command, including the `TQ;`/`TB;` polls used to watch the
radio. The blind window is visible as a gap at the start of each timeline:

| Macro | Commands held for | Effect |
|---|---|---|
| `TEST >FAST <AGAIN` | 1.74 s | last segment is plain `KY `, so polling resumes for it |
| `>TU >599 004 \|SK\|` | 6.67 s | both texts carry `KYW`, so nothing was visible until the end |

In the second run the tool saw a single `TQ1;` right at the end and reported **"transmitted for
0.2s"** for a transmission of about 6.8 seconds. That is the instrument being blinded, not the
radio misbehaving — and it is why `k4kytest.py` now says so instead of printing a number it cannot
support.

**This is a third argument for using `KYW` as sparingly as possible**, alongside the stall and the
36-second measurement in §5: while it is in force, the radio's transmit state is not observable at
all. A macro with no speed markers — which is every macro QLog sends — carries no `KYW` and none of
this applies.

## 7. The 60-character build, through QK4 and TCI

Run with `tcitester.py --cw`, so the whole path is exercised: TCI → QK4 → CAT → radio.

| Sent | On the wire | Result |
|---|---|---|
| `TEST DE NY4I` | `KY TEST DE NY4I;` | one command, unpadded, no `KS`. Keyed correctly |
| 55-character CQ | `KY CQ CQ CQ DE NY4I ... K;` | **one** command — three on the old 22-character build |
| `cw_macros_stop` after 3 s | `KY <0x04>;RX;` | **"the abort happened mid message"** |

The abort is the last command in the implemented set to be confirmed, and it had never been sent to
a K4 before — only to a K3, by TR4W, whose bytes we copied.

Two supporting observations from the same runs:

- The TCI client received `trx:0,true;` / `trx:0,false;` alternating during keying, which is QK4
  broadcasting the QSK transmit cycle, and a final `trx:0,false;` immediately after the abort. So
  the radio left transmit cleanly rather than being cut off mid-carrier.
- `KY TEST DE NY4I;` is 16 characters where the old build sent `KY TEST DE NY4I` plus six spaces of
  pad. Nothing is padded any more.

## Still untested

- A message long enough to exceed whatever the buffer really holds. 68 characters did not reach it,
  so the ceiling is still unknown.
- ~~`KY` while the radio is NOT in CW mode~~ — established. With the K4 in **LSB**, QLog sent
  `cw_macros`, QK4 sent `KY CQ CQ CQ DE NY4I NY4I NY4I K;`, and the radio produced **nothing**: no
  keying (0 `TX;` responses), no error, no reply. QLog sent no mode command of its own and showed
  the operator nothing.

  So the silence is total and unexplainable from anywhere the operator can see. QK4 now warns in
  its log and still sends — it does not switch the mode, which would be a surprising side effect of
  a text command, and in the DATA modes the radio sends the text as data, which refusing would
  break. The mode is the operator's to manage.

## Repeating any of it

`k4kytest.py` lives in the `utilities` repo. It keys the transmitter — dummy load or low power.

```
python3 k4kytest.py 'TEST' --dry-run                  # print the commands, connect to nothing
python3 k4kytest.py --wait 10 --qk4 '>TU >599 004 |SK|'  # replay a TCI macro exactly as QK4 sends it
```

Its defaults are NOT QK4's (`--chunk 60 --pad 0 --kyw none`); `--qk4` is the shorthand that matches
what QK4 actually emits.

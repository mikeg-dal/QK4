# CW by CAT over TCI — what a client author needs to know

Written for the TR4W side of the conversation, from a bench session on 2026-09-16/17 that put
CW through TCI to a real Elecraft K4 and watched what came out. Everything here was observed, not
inferred; the raw traces are in [cw-by-cat-bench-log.md](cw-by-cat-bench-log.md).

**The short version: a TCI client should send TCI, not CAT.** Do no chunking, no padding, no
prosign spelling, no `KY` framing. All of that is the server's job and the server knows the radio;
a client that does it too will fight it.

---

## 1. There is an interop bug between TR4W and QK4 today, and it is silent

`uRadioTCI.pas:996` sends

```pascal
SendToRadio(Format('cw_macros:%s;', [FCWBuffer]));     // cw_macros:CQ TEST NY4I;
```

The TCI 2.0 spec defines the command as **`cw_macros:arg1,arg2;`** where arg1 is the receiver
index and arg2 the text — so the conforming form is `cw_macros:0,CQ TEST NY4I;`.

Sent to QK4 as it stood, `cw_macros:CQ TEST NY4I;` produced **nothing**: no command to the radio,
no line in the log, no reply. Verified by sending it to the running server and counting zero `KY`
commands out the far side.

**QK4 has been changed to accept both forms** (commit `7d316fc`) — a lone argument is taken as the
text, because there is no other one-argument form of the command and silence is the worst possible
answer. So TR4W will work against QK4 either way from now on.

**But other servers may not be so accommodating**, and `uRadioTCI.pas:994` already flags this as
`[VERIFY]`. The receiver-indexed form is what the spec says. If TR4W sends `cw_macros:0,<text>;` it
is conforming, and QK4 accepts that too — tested, first-class, not a fallback.

Related, and worth checking against the same spec text: `cw_macros_speed` really *is* a global
taking one argument (`cw_macros_speed:25;`), so the comment at `uRadioTCI.pas:227` is right and
that command should **not** gain a receiver index.

---

## 2. The macro text has a grammar. Three things are embedded in it

From the TCI 2.0 spec, section "CW macro". A client must **encode** these; a server must decode
them.

### 2.1 Reserved characters, which the client MUST escape

TCI frames its own protocol with `:` `,` and `;`, so a macro cannot carry them literally:

| Literal | On the wire |
|---|---|
| `:` | `^` |
| `,` | `~` |
| `;` | `*` |

A logger sending `TNX, 73` unescaped will have its message split at the comma into two arguments.
QK4 rejoins them defensively, but that is QK4 being forgiving, not the protocol working.

### 2.2 Speed markers

`>` raises the sending speed by 5 WPM, `<` lowers it, **cumulatively**, relative to the radio's
current keyer speed. `>TU >599` means the second run is 10 WPM above where it started.

### 2.3 Prosigns

Letters between vertical bars are run together: `TEXT |SK| TEXT`.

**Send `|SK|`, never the Elecraft spelling.** This one bites. In a K4 `KY` command the prosigns are
`(`=KN, `+`=AR, `=`=BT, `%`=AS, `*`=SK, `!`=VE — so a client that helpfully pre-translates and
sends a bare `*` has, per §2.1, just sent an escaped semicolon. The server will decode it to `;`
and strip it. The prosign vanishes.

`uRadioElecraftBase.pas:101` declares exactly those Elecraft spellings via `CWProsigns`. **That
table is correct for the serial/network Elecraft radios and wrong for `TTCIRadio`** — over TCI the
prosign grammar is `|XX|`, and the server maps it to whatever its radio wants.
`uRadioTCI.pas:255`'s note that "nobody has established what its cw_macros does with a prosign" now
has an answer: QK4 translates `|SK|` to `*` and the K4 keys it as SK, confirmed by ear.

---

## 3. Do not chunk, and do not pad. `CWFrameRule(0, False)` is right

`uRadioTCI.pas:253` already sets `CWFrameRule(0, False)` — no limit, no padding — with the comment
that `cw_macros` states no length limit. **That is correct and should stay.**

Everything below is the server's problem, and QK4 now does it:

- **Chunking at 60 characters**, the K4's documented `KY` maximum. Split on a word boundary, with
  the space carried to the *start* of the next chunk — the radio trims trailing spaces, so a chunk
  ending on a real word gap loses it and keys `NY4I NY4I` as `NY4INY4I`.
- **No padding.** TR4W's `CWFrameRule(22, True)` for the direct Elecraft radios is a *minimum*
  guarding against a short `KY` being swallowed after the keyer abort TR4W sends before every
  message. QK4 does not send that abort, so it needs neither the minimum nor the padding. (QK4 had
  this wrong for a day — it read the 22 as a maximum and chunked a 28-character CQ into two
  commands.)
- **`KS`/`KYW` sequencing** for speed markers — see §4.

A TCI client that chunks would produce several `cw_macros` commands, each of which the server would
then chunk again. Harmless but pointless, and it defeats word-boundary splitting because the client
has already cut the text somewhere arbitrary.

---

## 4. If you implement a TCI *server*, `KYW` is load-bearing

Relevant if TR4W's own TCI server (`uTCIServer.pas`) ever keys a radio.

The K4's `KY*[text];` has a flag in the third character: blank normally, `W` for wait, which delays
the radio's processing of following host commands until the message has been keyed.

**A `KS` reaches text already sitting in the buffer.** Tested by sending the identical command
sequence twice: with the wait flags, `TEST >FAST <AGAIN` keyed 20 / 25 / 20; without them, "the
speed never changed" — every `KS` was acted on as it arrived, so by the time anything keyed the
final restore had already landed and the whole message came out at one speed.

So the rule is: **`KYW` exactly when a `KS` follows this text, never otherwise.** Not defensively.
It costs more than it looks:

- It stalls every later command until the message has been keyed. A 68-character message was
  measured at **35.9 seconds**.
- It makes the transmit state **unobservable** — `TQ;` polls are host commands and get held too, so
  a tool watching `TQ` goes blind for 1.7 to 6.7 seconds and can conclude the radio never
  transmitted.

An appealing simplification that is *wrong*: "always end with a plain `KY `". When a macro ends away
from its starting speed the restore is a following `KS`, so the last text needs the flag after all.

Related: consecutive `KY` commands **do not** need throttling. Four 22-character commands sent
back-to-back with no flag keyed as one continuous message, nothing dropped, no `KY1;` (buffer full)
anywhere. TR4W's existing no-flow-control approach in `uCWKeyerCAT.pas` is correct.

---

## 5. The operator's mode is the operator's problem, but say something

The K4 keys `KY` in CW, CW-REVERSE and the DATA modes. In SSB, AM and FM it **discards it in total
silence** — no keying, no error, no response. Confirmed: with the radio in LSB, QLog sent
`cw_macros`, QK4 sent a correct `KY`, and the radio produced nothing at all.

QLog sends no mode command of its own and shows the operator nothing, so pressing a macro key gives
no CW and no explanation from anywhere.

QK4's decision, and the station owner's: **warn in the log, still send, do not switch the mode.**
Switching is a surprising side effect of a text command, and in the DATA modes the radio sends the
text as data — a legitimate use that refusing would break.

Whatever TR4W does, the thing to avoid is what everyone did before: nothing.

---

## 6. What no client has asked for

QK4 logs every TCI command it does not implement. Across three clients — QLog, RumLogNG and a test
harness — over a full session, that log is **empty**. Nothing requested `cw_msg`, `cw_terminal`,
`keyer` or `callsign_send`.

`CW_MSG`'s editable-callsign protocol and `KEYER`'s element timing exist because ExpertSDR3
generates CW in its own DSP and can splice into a message already sending. The K4 keys from a
firmware buffer; reproducing that means abort-and-restart, which stutters. Three clients have now
declined to contradict that reasoning, which is weak evidence but is the only evidence there is.

---

## 7. Commands worth supporting, in the order they earn their place

| TCI | What it does | Notes |
|---|---|---|
| `cw_macros:<trx>,<text>` | Send CW text | The only one QLog and RumLogNG use |
| `cw_macros_stop` | Abort | Confirmed cutting mid-message on a K4 |
| `cw_macros_speed:<wpm>` | Set keyer speed | **Global**, one argument, no receiver index |

---

## Verifying any of this

Two tools, both in the `ny4i/utilities` repo:

- **`k4kytest.py`** — talks to the radio directly on port 9200, no TCI and no QK4 in between, so a
  result is about the *radio*. `--qk4` replays exactly what QK4 sends; `--kyw none|last|all`
  overrides the wait flags, which is how the `KS`-reaches-the-buffer question was settled.
- **`tcitester.py --cw "TEXT"`** — drives a TCI server. `--cw-stop-after N` tests the abort.

Both key the transmitter. Dummy load or low power.

Every claim in this document is either a wire trace or a listening result from a K4 operator at the
radio. Where it says "confirmed", someone heard it.

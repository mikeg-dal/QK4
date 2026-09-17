# CW by CAT over TCI — what a client author needs to know

Written for the TR4W side of the conversation, from a bench session on 2026-09-16/17 that put
CW through TCI to a real Elecraft K4 and watched what came out. Everything here was observed, not
inferred; the raw traces are in [cw-by-cat-bench-log.md](cw-by-cat-bench-log.md).

**What will and will not move.** QK4's TCI *command handling* can change to accommodate a client —
accepting `cw_macros` without a receiver index (§1) is exactly that, and more of it is possible.
Its **CW handling is settled**: the prosign table, the chunking, the padding decision and the
`KS`/`KYW` sequencing were each established on the radio and are not moving. Where this document
records a divergence from another implementation, treat it as a fact to code against rather than an
open negotiation. Station owner's call.

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
QK4 rejoins them defensively, but that is QK4 being forgiving, not the protocol working. Escaped
properly, `cw_macros:0,TNX~ 73;` produced `KY TNX, 73;`.

The escaping applies to the **whole payload**, and decoding happens before anything else. A bare `*`
becomes `;`, which cannot appear inside a CAT command and is then stripped: `A*B` produced `KY AB;`.

### 2.2 Speed markers

`>` raises the sending speed by 5 WPM, `<` lowers it, **cumulatively**, relative to the radio's
current keyer speed. `>TU >599` means the second run is 10 WPM above where it started.

### 2.2a Using the speed markers deliberately

All measured against QK4 with the radio at 20 WPM.

**The step is a fixed 5 WPM.** `kSpeedStepWpm = 5` in `cwmacro.h`, straight from the spec
("The speed step is 5 wpm"). Not a percentage, and not the K4's own KEYER SPEED step setting. A
client can ask for "one step", never for a particular WPM — and since this is one server's
reading of the spec, another server could size it differently.

**Markers accumulate, and reverse.** `A<B>C` from 20 gave
`KYWA; KS015; KYWB; KS020; KY C;` — A at 20, B at 15, C back at 20. Note the last command is a
plain `KY `: the macro ended where it started, so nothing follows it and no wait flag is needed.

**QK4 restores the speed, the radio does not.** `A<B` ended at 15 and produced a trailing
`KS020;`. That is the server putting the operator's keyer back, because the markers are documented
as changing speed WITHIN a text. It is QK4's behaviour, not the protocol's — a client should not
assume another server does it.

**The speed changes are broadcast.** During the macro the client received `cw_keyer_speed:15;`
and `cw_macros_speed:15;`, then the same pair at 20 on restore. A client tracking the radio's
keyer speed will see the excursion, not just the endpoints.

**`cw_macros_speed` and `cw_keyer_speed` now SET as well as report.** They were read-only until
2026-09-17, which left the markers as the only way a TCI client could change sending speed — and
since their step is fixed at 5, a logger whose increment was configurable and not 5 could not
express it by any route at all. Confirmed on the radio:

```
        cw_macros_speed:28;   ->   cw_macros_speed:20;    the MODEL's value, not the request
                                   KS028;                 reaches the K4
                                   cw_keyer_speed:28;     the radio's own change, broadcast
```

Both names drive the one setting — the spec's client-to-server `CW_KEYER_SPEED` and the contest
logger's `CW_MACROS_SPEED` are the same thing, and the K4 has one keyer, so neither takes a
receiver index. A bare `cw_keyer_speed;` is still a query and sets nothing.

The immediate reply carries **what QK4 holds, not what was asked for**. The radio has not moved
yet; its own change follows as a broadcast and is the authoritative echo. Confirming an unapplied
value is the stale-confirmation bug that had the reference server telling WSJT-X it was on a
frequency it was not.

Out-of-range requests are clamped to the K4's documented 8..100 rather than sent and ignored.

### 2.3 Prosigns — send `|XX|`, never a single-character token

Letters between vertical bars are run together: `TEXT |SK| TEXT`.

**What QK4 accepts**, all six confirmed on the wire:

| Send | QK4 emits | Prosign |
|---|---|---|
| `\|KN\|` | `(` | KN |
| `\|AR\|` | `+` | AR |
| `\|BT\|` | `=` | BT |
| `\|AS\|` | `%` | AS |
| `\|SK\|` | `*` | SK |
| `\|VE\|` | `!` | VE |

Those are the K4's own `KY` spellings, from the manual. The translation happens **server-side, after
escape decoding**, so a client sending `|SK|` never has to know that the K4 spells it `*` — and in
particular never has to worry that `*` is also TCI's escape for `;`. Verified:
`cw_macros:0,|KN| |AR| |BT| |AS| |SK| |VE|;` produced `KY ( + = % * !;`.

**`|SN|` is not in the table.** QK4 keys unknown prosigns as their bare letters rather than deleting
them: `A |SN| B` produced `KY A SN B;`. TR4W's Elecraft base *consumes* SN instead (keys nothing),
on the grounds that Elecraft has no such prosign — a deliberate divergence, and arguably the better
call — but QK4's CW handling is settled and will not be changed to match. A client wanting the
consume behaviour should simply not send `|SN|`.

#### What single-character tokens actually do

Measured on the K4, sending each of TR4W's five `CWProsign` tokens raw through TCI as
`uRadioTCI` does today:

| Token | Intended | Reached the radio as | Effect |
|---|---|---|---|
| `^` | half space | `KY A:B;` | a literal colon |
| `!` | SN | `KY A!B;` | **keys VE** — `!` is VE in the K4 table |
| `+` | AR | `KY A+B;` | AR — correct, by coincidence |
| `<` | SK | `KYWA;` `KS015;` `KYWB;` `KS020;` | **message split in two and re-timed 5 WPM slower**; prosign gone |
| `=` | BT | `KY A=B;` | BT — correct, by coincidence |

Two work by accident because the token happens to match the K4's own spelling. The other three
fail silently and differently: one becomes punctuation, one keys **a different prosign**, and `<`
is not a character at all to the server — it is a speed marker, so it silently slows everything
after it and splits the message around itself.

That last row is the argument for `|XX|` in one line. A prosign token that re-times the rest of the
transmission is not a rendering problem, and nothing anywhere reports it.

**The danger is a client that pre-translates.** Any single-character prosign token sent raw lands in
one of two traps: `*` is TCI's escape for `;` and will be decoded then stripped, and `<` is a speed
marker (§2.2) that the server consumes. Neither reaches the radio as a prosign.

#### A literal `<` or `>` is not representable either

Three ways of asking, all measured:

| Sent | Reached the radio as | Reading |
|---|---|---|
| `A>B` | `KYWA;` `KS025;` `KYWB;` `KS020;` | `>` is symmetric with `<` — same split, same re-timing, upward |
| `A<<B` | `KYWA;` `KS010;` `KYWB;` `KS020;` | doubling does **not** escape; two markers is simply −10 |
| `A\|LT\|B` | `KY ALTB;` | no token spelling; unknown tokens key their letters |

So there is no escape, no doubling convention, and no `|XX|` form. And even if one existed at the
grammar layer, the builder strips `<` and `>` from `KY` text independently, because the K4 uses them
for TX TEST mode — an escape would need adding in two places, not one.

**Unlike the half space, nothing is actually lost.** `<` is not a CW character; on the K4 it is a
mode control. There is no message an operator could legitimately want keyed that contains one. The
right client behaviour is to treat `<` and `>` as grammar everywhere and never let operator-typed
text carry them into a payload — which is a stricter rule than escaping would have been, and
simpler.

### 2.4 The `^` collision: half space is not representable

`^` is TCI's escape for `:` and QK4 decodes it **unconditionally, before any prosign handling**.
Confirmed: `cw_macros:0,A^B;` produced `KY A:B;`.

So a client using `^` as a half-space token — as TR4W does internally,
`TCWProsignSet.HalfSpace = '^'` — will have it silently become a colon. There is no escape for the
escape.

**Map half space to a plain space over TCI.** That is what `uRadioElecraftBase.pas:90` already does
for the direct Elecraft radios, with the right reason attached: *"The half space is a whole space: a
KY string has no half space."* The K4 has no half-space character either way, so nothing is lost by
sending `' '`, and the collision disappears.

(Unverified, and probably not worth verifying: whether the K4 keys a literal `:` at all. It is not
in the prosign table and not a standard CW character, so `A:B` may key as two letters and nothing,
or as something odd. Avoid sending one.)

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

## 6a. Identifying the server — and the trap in `protocol:`

A client that gates behaviour on which server it is talking to needs the handshake. QK4's, captured
verbatim from a live connect:

```
device:QK4 0.7.0;
protocol:ExpertSDR3,1.5;
receive_only:false;
trx_count:2;
channels_count:2;
vfo_limits:100000,54000000;
if_limits:-48000,48000;
ready;                        (76 commands in the burst overall)
```

**Do not identify QK4 by `protocol:`.** It says `ExpertSDR3,1.5` — deliberately, and it is not
going to change: WSJT-X matches on that string and a different one makes it halve transmit
amplitude (`tciserver_internal.h:20`). So `protocol:` identifies the dialect QK4 speaks, not the
program. Match on it and you will both misidentify QK4 as ExpertSDR3 *and* misidentify a real
ExpertSDR3 as whatever you decided `ExpertSDR3,1.5` meant.

**`device:` is the discriminator, and it carries the version.** `device:QK4 0.7.0;` — the program
name, a space, then the version, added 2026-09-17 so a client can gate on a *capability* rather than
just a program.

**Match the `QK4` prefix, never the whole token.** The version moves; the name does not. An equality
match was going to break on the first release regardless. (Seven of QK4's own tests used the bare
`device:QK4;` as a sentinel and all seven broke when the version landed — the same mistake, caught
at home.)

**Unknown, and stated as unknown:** what ExpertSDR2 and Thetis put in these fields. QK4's
`protocol:` string was chosen to satisfy WSJT-X's expectation of ExpertSDR3, which *implies* the
real thing sends something of that shape — but that is inference from one client's behaviour, not
something anyone here has seen on a wire.

## 7. Commands worth supporting, in the order they earn their place

| TCI | What it does | Notes |
|---|---|---|
| `cw_macros:<trx>,<text>` | Send CW text | The only one QLog and RumLogNG use |
| `cw_macros_stop` | Abort | Confirmed cutting mid-message on a K4 |
| `cw_macros_speed:<wpm>` | Set keyer speed | **Global**, one argument, no receiver index. Sets and reports |
| `cw_keyer_speed:<wpm>` | The same setting | The spec's client-to-server name for it |

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

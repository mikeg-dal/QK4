# TCI client compatibility notes

Things learned from other implementations that a QK4 change could break, or that explain why a
client behaves oddly against QK4 through no fault of either.

Companion to [tci-cw-for-client-authors.md](tci-cw-for-client-authors.md), which is advice *to*
client authors. This file is the reverse: what QK4 should know about *them*.

---

## 1. A client may have withdrawn a capability because of a different server

**The trap:** AetherSDR (`src/core/TciProtocol.cpp:454`) decides set-versus-get by argument count:

```cpp
bool isSet = (args.size() >= 2);
```

That assumes every command is receiver-addressed. A **global command taking one argument** is
therefore unreachable as a set on that server — a correctly formed set parses as a GET and is
answered with the server's own unchanged value.

**Why QK4 cares.** A client burned by this may disable the feature *for every TCI server*, QK4
included, because it cannot tell servers apart. TR4W did exactly that: it withdrew
`rcCWSpeedSync` in August 2026 with log evidence of ten `cw_macros_speed` sets answered ten times
with the server's unchanged value. QK4 then looked like it lacked a feature it could have had.

So: **a missing capability in a client is not evidence about QK4.** Ask which server taught them.

### Commands known to be affected

From TR4W's audit of its own 18 outbound sites — *what one client sends, not the spec's full set of
global single-argument commands*:

| Command | Shape | Status |
|---|---|---|
| `cw_macros_speed:<wpm>` | global, 1 arg | **Known casualty.** Fixed in QK4 2026-09-17 (sets as well as reports) |
| `drive:<pct>` | global, 1 arg | Known casualty upstream — AetherSDR issue #1764 |
| `volume:<db>` | global, 1 arg | Same root cause, **fixed upstream while `drive` was left broken** |
| `cw_macros_stop;` | zero args | **Unknown.** A bare action may never reach the `isSet` branch |

The `volume`/`drive` pair is the part worth remembering: the bug was analysed upstream and the fix
applied to one instance rather than to the parse. A protocol-level trap that gets patched
per-command will keep producing new casualties.

`cw_macros_stop` is untested and deliberately not guessed at. It matters more than its size
suggests — it is the Escape key, so if it is broken it is broken on the path an operator reaches for
when something has *already* gone wrong.

### The quiet one

`cw_macros` itself was a casualty and nobody noticed. TR4W sent `cw_macros:<text>;` — one argument —
which by the `isSet` rule parses as a GET. Their source notes "AetherSDR takes the raw text", so
something evidently worked, which means either that command is special-cased there or the note
describes a behaviour nobody verified.

**That is the shape of the hazard: it does not announce which commands it has eaten.** One client
lost CW speed loudly enough to withdraw a whole capability, and may have been losing CW *sending*
silently the entire time.

---

## 2. Probing for a capability, and why it beats the banner

A client can gate a capability on the handshake (`device:QK4 0.7.0;` — see the other document) or
probe for it behaviourally. TR4W prefers the probe, correctly: it works against servers nobody has
seen, needs no allow-list, and cannot be wrong about a version.

**The rule that works against QK4:** send `cw_macros_speed:<n>;` and treat the capability as live
only when a **`cw_keyer_speed:<n>;`** broadcast comes back carrying your number.

Measured, from a radio at 28 WPM:

| Probe | What came back |
|---|---|
| set **28** (the value already in force) | `cw_macros_speed:28;` — one reply, **no broadcast** |
| set **31** (a different value) | `cw_macros_speed:28;` then `cw_keyer_speed:31;` and `cw_macros_speed:31;` |

Two traps in that, both avoidable:

1. **Probe with a value that is already set and you get a false negative.** QK4 broadcasts on
   *change*, so a no-op set produces no broadcast at all. The single reply you do get is the
   immediate confirmation, which carries the model's value — identical in that case to your number.
2. **Watch the OTHER name.** The immediate confirmation echoes the name you used; the broadcast
   carries *both* names. So sending `cw_macros_speed` and watching for `cw_keyer_speed` separates
   confirmation from broadcast by name alone, with no timing assumption.

**Better than either: don't probe at all.** Observe the first time the operator genuinely changes
speed, and record whether the broadcast followed. No artificial transmission, no speed moved behind
the operator's back, and the answer arrives before it is needed.

---

## 3. What QK4 should not break

- **`protocol:` must stay `ExpertSDR3,1.5`.** WSJT-X matches on it; a different string makes it
  halve transmit amplitude.
- **`device:` should stay prefix-matchable.** It is `QK4 <version>` and clients are advised to match
  the `QK4` prefix. Appending is safe; reordering is not.
- **The immediate reply to a set carries the model's value, not the request.** A client may be
  relying on the difference to tell confirmation from broadcast, as above.

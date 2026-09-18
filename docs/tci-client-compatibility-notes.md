# TCI client compatibility notes

Things a QK4 change could break in a client, and things that explain why a client behaves oddly
against QK4 through no fault of either.

Companion to [tci-cw-for-client-authors.md](tci-cw-for-client-authors.md), which is advice *to*
client authors. This file is the reverse: what QK4 should know about *them*.

---

## 1. A client may have withdrawn a capability because of a different server

**QK4 can look like it lacks a feature it has, because a client turned that feature off for
everyone.**

That is the whole of what QK4 needs to know here, and it really happened. TR4W disabled CW speed
sync for *every* TCI server in August 2026 — QK4 included — after a different server answered ten
correctly-formed `cw_macros_speed` sets with its own unchanged value. The client had no way to tell
servers apart, so it withdrew the capability globally. QK4 then looked to lack something it could
have had, and nothing appeared in any log to say so.

**So: a missing capability in a client is not evidence about QK4.** Before concluding nobody wants a
feature, ask which server taught them not to ask for it. QK4's `cw_macros_speed` was read-only for
months partly because no client ever tried to set it — and one of them had stopped trying for
reasons that had nothing to do with us.

The specifics of the other server's bug are not recorded here: it is not a client of QK4, its
parsing cannot reach us, and cataloguing another project's defects is not this document's job.
TR4W's own source carries that analysis for anyone who needs it.

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

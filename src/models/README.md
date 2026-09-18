# models/

Radio state model. One `QObject` façade (`RadioState`) over 11 plain-struct subsystems.

## Files

- `radiostate.{cpp,h}` — `RadioState` façade: CAT handler registry (longest-prefix-first), public getters/setters/signals. The only `QObject` in this layer.
- `radiostate/` — 11 subsystem structs. One `.h` + `.cpp` pair per subsystem. See `radiostate/README.md` for the inventory.
- `menumodel.{cpp,h}` — K4 MEDF menu tree model. Used by `MenuController`.
- `macroids.h` — Shared enum for macro identifiers (F1–F12, PF1–PF4).
- `transmitowner.h` — Header-only transmit arbiter: who owns the transmitter (`Owner`), how the radio was keyed (`Route`), and the `Effects` needed to move between states. Pure logic, no Qt — `test_transmitowner` links it alone. Route is the load-bearing idea: `TX;` keys the K4 expecting its OWN mic input, while streamed packets carry a tunnel header, so recording which mechanism was used is what lets a release be the exact inverse of its engage. Closes the G1 violation in `AUDIT.md`.

## Pattern C — plain-struct subsystems

Internal state is partitioned across plain `struct` subsystems, each with a matching `*Handlers` namespace of free functions. The façade composes them and emits signals. See `PATTERNS.md` → "Subsystem State" for the full rules.

Why:
- One `QObject`, one thread affinity, one `Q_ASSERT(currentThread() == thread())` check.
- No moc storm when adding a subsystem.
- Public API (getters + signals) stays on `RadioState` — external callers unaffected when internals reorganize.

## Invariants (CI-enforced)

- **`test_radiostate`** — parser correctness per CAT prefix.
- **`test_radiostate_registry`** — every registered prefix resolves; longest-first dispatch ordering.
- **`test_radiostate_golden`** — byte-for-byte replay of a captured K4 CAT session. Any refactor-induced emit-order shift fails.
- **`test_catserver`** — pins the public API that `network/catserver.cpp` depends on. Contract in `docs/radiostate-catserver-api-contract.md`.

## Architecture Rule 4

`RadioState::parseCATCommand()` is main-thread only, enforced by `Q_ASSERT`. All callers must be on the GUI thread. Cross-thread parsing requires `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

## See also

- `PATTERNS.md` → Subsystem State, Adding RadioState Property / CAT Command.
- `docs/radiostate-catserver-api-contract.md` — pinned public getters.
- `docs/k4-protocol-quirks.md` — K4 CAT oddities.

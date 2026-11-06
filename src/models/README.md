# models/

Radio state model. One `QObject` façade (`RadioState`) over plain-struct subsystems (`ls src/models/radiostate/*.h`), plus the K4 menu model and the transmit arbiter.

## Files

- `radiostate.{cpp,h}` — `RadioState` façade: CAT handler registry (longest-prefix-first), public getters/setters/signals.
- `radiostate/` — subsystem structs. One `.h` + `.cpp` pair per subsystem. See `radiostate/README.md` for the inventory.
- `menumodel.{cpp,h}` — `MenuModel`, a `QObject` holding the K4 MEDF menu tree. Used by `MenuController`.
- `transmitowner.h` — Header-only transmit arbiter: who owns the transmitter (`Owner`), how the radio was keyed (`Route`), and the `Effects` needed to move between states. Pure logic, no Qt — `test_transmitowner` links it alone. Route is the load-bearing idea: `TX;` keys the K4 expecting its OWN mic input, while streamed packets carry a tunnel header, so recording which mechanism was used is what lets a release be the exact inverse of its engage.

## Pattern C — plain-struct subsystems

Internal state is partitioned across plain `struct` subsystems, each with a matching `*Handlers` namespace of free functions. The façade composes them and emits signals. See `PATTERNS.md` → "Subsystem State" for the full rules.

Why:
- One `QObject` for all radio state: one thread affinity, one `Q_ASSERT(QThread::currentThread() == thread())` check.
- No moc storm when adding a subsystem.
- Public API (getters + signals) stays on `RadioState` — external callers unaffected when internals reorganize.

## Invariants (CI-enforced)

- **`test_radiostate`** — parser correctness per CAT prefix.
- **`test_radiostate_registry`** — every registered prefix resolves; longest-first dispatch ordering.
- **`test_radiostate_golden`** — byte-for-byte replay of a captured K4 CAT session. Any refactor-induced emit-order shift fails.
- **`test_catserver`** — pins the public API that `network/catserver.cpp` depends on. Contract in `docs/radiostate-catserver-api-contract.md`.

## Architecture Rule 4

`RadioState::parseCATCommand()` is main-thread only, enforced by `Q_ASSERT(QThread::currentThread() == thread())`. `Q_ASSERT` is compiled out of Release builds, so it fires only in Debug builds (in CI, the sanitizer job). All callers must be on the GUI thread. Cross-thread parsing requires `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

## See also

- `PATTERNS.md` → Subsystem State, Adding RadioState Property / CAT Command.
- `docs/radiostate-catserver-api-contract.md` — pinned public getters.
- `docs/k4-protocol-quirks.md` — K4 CAT oddities.

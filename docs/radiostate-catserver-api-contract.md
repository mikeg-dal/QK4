# RadioState → CatServer Public-API Contract

This document fixes the surface of `RadioState` that `src/network/catserver.cpp`
depends on. Treat it as the **frozen public API** for the duration of the
Phase 1 RadioState subsystem split — every getter listed below must remain on
`RadioState` itself (not migrated to an internal subsystem struct) with the
same signature and the same semantics.

The regression gate is `tests/test_catserver.cpp` (33 test cases). `test_catserver`
is pinned in the CI build target list (`.github/workflows/ci-checks.yml`) so any
break of this contract fails the build.

## Getters consumed by CatServer

Enumerated from `grep 'm_radioState->' src/network/catserver.cpp` on the
commit that introduced this contract:

| Getter | Return type | CAT response it feeds |
|---|---|---|
| `firmwareVersions()` | `QMap<QString,QString>` | `RV.FP…`, `RV.DSP…` |
| `mode()` | `RadioState::Mode` | `MD`, `IF` |
| `modeB()` | `RadioState::Mode` | `MD$` |
| `frequency()` | `quint64` | `FA`, `IF` |
| `vfoB()` | `quint64` | `FB` |
| `isTransmitting()` | `bool` | `TQ`, `IF` |
| `splitEnabled()` | `bool` | `FT`, `IF` |
| `ritEnabled()` | `bool` | `RT`, `IF` |
| `xitEnabled()` | `bool` | `XT`, `IF` |
| `ritXitOffset()` | `int` | `IF` (offset field) |
| `rfPower()` | `double` | `PC` |
| `isQrpMode()` | `bool` | `PC` (L/H/X flag) |
| `agcSpeed()` | `RadioState::AGCSpeed` | `GT` |
| `keyerSpeed()` | `int` | `KS` |
| `noiseBlankerEnabled()` | `bool` | `NB` |
| `noiseReductionEnabled()` | `bool` | `NR` |
| `voxEnabled()` | `bool` (rollup) | `VX` |
| `filterBandwidth()` | `int` | `BW` |
| `dataSubMode()` | `int` | `DT` |
| `optionModules()` | `QString` | `OM` |
| `diversityEnabled()` | `bool` | `DV` |
| `sMeter()` | `double` | `SM` |

## Phase 1 rules

1. **No getter may move off `RadioState`.** If internal state for one of these
   fields lives in a subsystem struct after the split, `RadioState` must still
   expose the getter as a thin delegator.
2. **No signature change** — not `int` → `qint64`, not non-`const`, not
   anything. `CatServer::handleCommand()` is pinned to the exact current
   call sites; a signature shift is a break.
3. **Same semantics** — especially for rollups (`voxEnabled()` is *any-of*
   the three per-mode flags, not a dedicated boolean). Reimplementing as a
   stored field instead of a computed rollup is a break even if the return
   value is ultimately the same.
4. **If a Phase 1 commit touches any row in the table above**, run
   `ctest --test-dir build -R CatServerTests --output-on-failure` explicitly
   before committing. CI catches it too, but catching it locally saves a
   round trip.

## Revalidation

If CatServer gains or drops getter usage, refresh this table. The command
that generated it:

```bash
grep 'm_radioState->' src/network/catserver.cpp | grep -oE '->[a-zA-Z]+\(' | sort -u
```

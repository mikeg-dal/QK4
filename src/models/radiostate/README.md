# models/radiostate/

11 subsystem structs composing `RadioState`. Each is a plain `struct` + a `*Handlers` namespace of free functions.

## Subsystem inventory

| File | Topic | CAT commands (primary) |
|---|---|---|
| `frequencyvfostate.{h,cpp}` | VFO A/B freq, split, RIT/XIT, VFO link/lock | FA, FB, FT, RT, RT$, XT, RO, RO$, LN, LK, LK$ |
| `modefilterstate.{h,cpp}` | Operating mode, filter, CW keyer | MD, MD$, BW, BW$, IS, IS$, FP, FP$, CW, KS, KP |
| `processingstate.{h,cpp}` | NB / NR / PA / RA / GT / NA / NM (per VFO) | NB, NB$, NR, NR$, PA, PA$, RA, RA$, GT, GT$, NA, NA$, NM, NM$ |
| `antennastate.{h,cpp}` | Antenna selection + per-band masks + ATU | AN, AT, ACN, ACM, ACS, ACT, AR, AR$ |
| `audioeffectsstate.{h,cpp}` | VOX, FX, APF, ESSB, EQ, line in/out, mic, ML, MX, BL | FX, AP, AP$, VX, VG, VI, ES, RE, TE, LO, LI, MI, MS, ML, MX, BL |
| `datacontrolstate.{h,cpp}` | Data sub-mode, rate, tuning step, streaming latency | DT, DT$, DR, DR$, TD, TD$, TB, TB$, VT, VT$, SL |
| `spectrumdisplaystate.{h,cpp}` | All `#`-prefix panadapter / waterfall display state | #REF, #SPN, #SCL, #MP, #DPM, #DSM, #FPS, #WFC, #WFH, #AVG, #PKM, #FXT, #FXA, #FRZ, #VFA, #VFB, #AR, #NB$, #NBL$ (+ EXT variants) |
| `textdecodestate.{h,cpp}` | Text decoder mode / threshold / lines | TD, TD$, TB, TB$ |
| `rxtxmeterstate.{h,cpp}` | S-meter, TX meter, TX/RX transition, supply V/A, radio identity, SB/DV/TS/BS | SM, SM$, PO, TM, TX, RX, ID, OM, RV., ER, MN, SIFP, SB, DV, TS, BS |
| `levelsstate.{h,cpp}` | RF power, QRP/QRO, mic gain, compression, RF gain A/B, squelch A/B | PC, MG, CP, RG, RG$, SQ, SQ$ |
| `qskcontrolstate.{h,cpp}` | QSK enable + per-mode TX→RX delay | SD |
| `powerstate.{h,cpp}` | K4 remote power on/off — `PS1` = on, `PS0` = off | PS |
| `xvtrbandstate.{h,cpp}` | Per-XVTR-band transverter configuration (12 bands) | XV\* |

## File layout per subsystem

```
xxxstate.h   // struct XxxState { ... void reset(); };
             // namespace XxxHandlers { handleXX(...); setXX(...); }
xxxstate.cpp // handler + setter bodies
```

Façade (`radiostate.cpp`) holds one pass-through line per handler:

```cpp
void RadioState::handleMD(const QString &cmd) {
    ModeFilterHandlers::handleMD(m_modeFilterState, *this, cmd);
}
```

## Rules

1. **Plain structs, not `QObject`.** Only `RadioState` is a `QObject`.
2. **RadioState is the only emitter.** Handler fns take `RadioState &owner` and emit via `emit owner.xxxChanged(...)`.
3. **Each subsystem owns a reset().** Called from `RadioState::reset()` on disconnect. Note: reset() does NOT emit signals — controllers wanting reset-state labels must implement their own `reset()`.
4. **Each subsystem registers its handlers** into the prefix-keyed dispatch table in `RadioState::registerCommandHandlers()`.
5. **Public API (getters + signals) stays on `RadioState`.** External callers see a stable surface.

## Adding a subsystem

Rare (existing 11 cover the K4's CAT surface well). If you need to:

1. Create `xxxstate.{h,cpp}` — mirror the shape of an existing simple one (e.g., `qskcontrolstate.{h,cpp}`).
2. Add `XxxState m_xxxState;` to `RadioState` in `radiostate.h`.
3. Delegate getters on `RadioState` read `m_xxxState.field`.
4. Register CAT handler prefixes in `RadioState::registerCommandHandlers()`.
5. Call `m_xxxState.reset()` from `RadioState::reset()`.
6. Add `.cpp` + `.h` entries to `CMakeLists.txt` (main target SOURCES/HEADERS) and `tests/CMakeLists.txt` (each `test_radiostate*` target).
7. Add tests covering new prefixes in `tests/test_radiostate.cpp`.

## See also

- `PATTERNS.md` → "Subsystem State", "Adding RadioState Property / CAT Command".
- `docs/radiostate-catserver-api-contract.md` — pinned public getters for CatServer.

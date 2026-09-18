# QK4 Controller Map

> **Purpose:** if something is broken in the UI, find the owning controller here first.
> Each entry maps a user-visible feature → the controller that owns it → the file path.

Last updated: TransmitController added — one owner of transmit (31 controllers).

---

## Quick reference table

| If the user says… | Look at | Source |
|---|---|---|
| "My KPA1500 amp isn't showing status" | KPA1500UiController | `src/controllers/kpa1500uicontroller.cpp` |
| "The KPA1500 mini panel buttons don't work" | KPA1500UiController | same |
| "KPA1500 ANT button doesn't reach sub-antennas / LCD shows the wrong antenna" | KPA1500UiController → KPA1500Client::selectNextAntenna / antennaConnector | `src/network/kpa1500client.cpp`, `src/network/kpa1500antennas.cpp` |
| "Mode popup won't open / wrong VFO" | ModePopupController | `src/controllers/modepopupcontroller.cpp` |
| "Mode label shows wrong text (AFSK, DATA-A, +)" | ModeLabelController | `src/controllers/modelabelcontroller.cpp` |
| "VFO frequency display off by RIT offset" | VfoFrequencyController | `src/controllers/vfofrequencycontroller.cpp` |
| "Band selector doesn't switch bands / SAME band tap" | BandNavigationController | `src/controllers/bandnavigationcontroller.cpp` |
| "RIT/XIT label or wheel not working" | RitXitController | `src/controllers/ritxitcontroller.cpp` |
| "Main RX / Sub RX / TX popup buttons do wrong thing" | ButtonRowDispatcher | `src/controllers/buttonrowdispatcher.cpp` |
| "TX row buttons 5/6 wrong for CW vs SSB" | ButtonRowDispatcher | same |
| "AFX/AGC/APF button label wrong on Main/Sub RX row" | PopupManager::wireRxRowButtonLabels | `src/controllers/popupmanager.cpp` |
| "SUB / DIV indicator color wrong" | SubDivIndicatorController | `src/controllers/subdivindicatorcontroller.cpp` |
| "VFO B dim color wrong (SUB RX off state)" | SubDivIndicatorController | same |
| "SPLIT / VOX / QSK / TEST / ATU / MSG bank label wrong" | VfoRowIndicatorController | `src/controllers/vforowindicatorcontroller.cpp` |
| "TX triangle color not red on transmit" | TxStateController | `src/controllers/txstatecontroller.cpp` |
| "TX meters (Po / ALC / COMP / SWR / Id) not updating" | TxStateController | same |
| "VFO AGC / preamp / attenuator / NB / NR indicator wrong" | ProcessingDisplayController | `src/controllers/processingdisplaycontroller.cpp` |
| "Antenna label text (TX / RX Main / RX Sub) wrong" | AntennaDisplayController | `src/controllers/antennadisplaycontroller.cpp` |
| "Antenna config popup (ANT CFG button) not working" | AntennaConfigController | `src/controllers/antennaconfigcontroller.cpp` |
| "Filter indicator shape / position wrong" | FilterIndicatorWidget (Direct Observation — no controller) | `src/ui/widgets/filterindicatorwidget.cpp` |
| "Clicking FIL1/2/3 under a VFO doesn't change the filter preset" | RightSideController::cycleFilterPreset | `src/controllers/rightsidecontroller.cpp` |
| "Side panel knob values (BW/SHFT, power, mic gain) wrong" | SideControlDisplayController | `src/controllers/sidecontroldisplaycontroller.cpp` |
| "Side panel shows CW knobs (WPM/PITCH) instead of voice (MIC/CMP) or vice versa" | SideControlDisplayController | same |
| "Side panel scroll wheels (WPM/Power/BW/HI/LO/RFGain/etc.) wrong" | SideControlScrollController | `src/controllers/sidecontrolscrollcontroller.cpp` |
| "Right side panel button (PRE/NB/NR/NTCH/FIL/AB/REV/SPOT/MODE/PF1-4/RATE/LOCK/SUB) wrong" | RightSideController | `src/controllers/rightsidecontroller.cpp` |
| "Clicking a frequency digit doesn't change the tuning rate (underline)" | VfoFrequencyController::setTuningRateFromDigit | `src/controllers/vfofrequencycontroller.cpp` |
| "FREQ ENT doesn't open frequency entry / opens it on the wrong VFO" | RightSideController (frequencyEntryRequested) → VfoFrequencyController::toggleFrequencyEntry | `src/controllers/vfofrequencycontroller.cpp` |
| "Memory buttons M1-M4 / REC / STORE / RCL left/right click wrong" | MemoryButtonsController | `src/controllers/memorybuttonscontroller.cpp` |
| "B-SET label visibility wrong (still showing SPLIT when B-SET on)" | VfoRowIndicatorController | `src/controllers/vforowindicatorcontroller.cpp` |
| "Side panel BW/SHFT color wrong on Sub-RX active (B-SET)" | SideControlDisplayController | `src/controllers/sidecontroldisplaycontroller.cpp` |
| "Menu overlay / MEDF system not working" | MenuController | `src/controllers/menucontroller.cpp` |
| "Feature menu bar (ATTN/NB/NR/NOTCH) not working" | FeatureMenuController | `src/controllers/featuremenucontroller.cpp` |
| "Text decode window not opening / updating" | TextDecodeController | `src/controllers/textdecodecontroller.cpp` |
| "Band/Display/Fn/Main RX/Sub RX/TX popup won't open or close" | PopupManager | `src/controllers/popupmanager.cpp` |
| "Fn popup macro buttons not sending CAT" | MacroController | `src/controllers/macrocontroller.cpp` |
| "PF1-PF4 / F1-F12 keyboard shortcuts / KPOD macro buttons" | MacroController | same |
| "Connection to K4 / status indicator wrong" | ConnectionController + MainWindow::updateConnectionState | `src/controllers/connectioncontroller.cpp` + `src/mainwindow.cpp` |
| "Top status bar label / KPA1500 badge wrong" | StatusBarController | `src/controllers/statusbarcontroller.cpp` |
| "Audio (RX/TX, PTT) not working" | AudioController | `src/controllers/audiocontroller.cpp` |
| "Radio stays keyed after I stop transmitting" | TransmitController | `src/controllers/transmitcontroller.cpp` (decision: `src/models/transmitowner.h`) |
| "PTT indicator lit but nothing is transmitting (or the reverse)" | TransmitController | same |
| "WSJT-X keyed while I was already transmitting" | TransmitController | same |
| "KPOD knob / KPOD+ / HaliKey device not detected" | HardwareController | `src/controllers/hardwarecontroller.cpp` |
| "Enable K-Pod does nothing / device opens with the box unchecked" | HardwareController, applying `src/hardware/usbdevicelifecycle.h` | same |
| "Unplugging a K-Pod raised two popups, or unchecking the box raised one" | same — effects come from state EDGES | same |
| "CW keying / paddle / sidetone wrong" | CwController | `src/controllers/cwcontroller.cpp` |
| "My foot pedal on the HaliKey does nothing" | Withdrawn on purpose — see the note in `src/controllers/cwcontroller.h` | n/a |
| "Spectrum / panadapter / click-tune not working" | SpectrumController | `src/controllers/spectrumcontroller.cpp` |
| "DX cluster spots not appearing / cluster connect fail" | DxClusterController | `src/controllers/dxclustercontroller.cpp` |
| "WSJT-X / QLog / RumLogNG over TCI: no connect, no audio, PTT or CW wrong" | TciController | `src/controllers/tcicontroller.cpp` (server: `src/network/tciserver.cpp`) |
| "TCI Server options tab status or client list wrong" | TciServerPage | `src/ui/pages/tciserverpage.cpp` |

---

## What's still in MainWindow (and why)

MainWindow is now ~1,460 LOC. What remains is genuinely MainWindow responsibility:

- **Widget construction** — `setupUi()` and `setupVfoSection()` build the window layout tree. This is MainWindow's job; controllers observe widgets they don't own.
- **Setup helpers** — `setupControllers()`, `setupNotificationWidget()`, `setupConnectionWiring()`, `setupRadioStateWiring()`, `setupSpectrumDataRouting()`, `setupHardwareController()`, `setupCatServer()`, `setupMenuBar()`. Construction orchestration.
- **Event filter dispatch** — `eventFilter()` routes widget-level mouse events to the right controller. MainWindow is the natural dispatcher because it owns the top-level event filter installation.
- **Connection lifecycle** — `onRadioReady()`, `onCatResponse()`, `onConnectionStateChanged()`, `onConnectionError()`, `onAuthFailed()` orchestrate startup / teardown across multiple controllers. This is coordination, not feature logic.
- **resetUiForDisconnect()** — single place that calls each controller's / widget's reset on K4 disconnect. Also coordination.
- **Window chrome** — `closeEvent`, `moveEvent`, `keyPressEvent`. Native QMainWindow overrides.
- **closeAllPopups / closeNonPopupManagerPopups / toggleXPopup** — cross-controller popup coordination.

---

## The 30 controllers at a glance

Grouped by concern:

### Core (exist before refactor)
- **ConnectionController** — TCP connection state machine, network I/O thread, CAT send
- **AudioController** — audio engine, Opus codecs, the PTT gate, audio thread
- **TransmitController** — the single owner of "are we transmitting". Every producer (PTT button, XMIT, Esc, CAT client, TCI client, and the radio itself) asks it instead of writing the audio gate and the indicator directly. Records HOW each transmission was keyed so the release is the exact inverse. Decision logic is header-only in `src/models/transmitowner.h` and unit-tested standalone.
- **SpectrumController** — panadapters, spectrum data routing, click-tune, passband overlays
- **HardwareController** — constructs + owns KPOD, KPOD+, HaliKey, IambicKeyer, SidetoneGenerator + their threads; KPOD tuning-knob → CAT; device-config push; signal forwarding. **Sole owner of USB device lifecycle policy**: holds one `UsbDeviceLifecycle::State` per device and applies the returned effects. The workers report arrival and loss; they do not decide to open. It also emits `kpodPlusOwnsCw(bool)` from that state, which is what the CW gate follows — detection is not ownership.
- **CwController** — CW keying orchestration across the HardwareController-owned devices: IambicKeyer↔CAT/sidetone wiring, the single HaliKey line handler (both levers passed to the keyer as one sample), V1.4 pedal demux, KPOD+ keyer-active gate. Paddle input is gated on CW/CW_R for both levers, and both are released on any mode change. See `cwcontroller.h` for the threading-invariant doc.
- **DxClusterController** — DX cluster client (multi-instance; spot cache)
- **TciController** — TCI 2.0 server for external apps: owns the TCI thread, `TciServer` and `TciAudioBridge`; maps client SETs to `CatFrames`, pushes `RadioState` snapshots to the server, routes TCI PTT/TX audio to `AudioController`. See `docs/tci-server-design.md`.

### Popup-family
- **PopupManager** — owns 14 popups: band, display, Fn, macro dialog, 3 button rows (Main/Sub/Tx), RX EQ, TX EQ, line in/out, mic input/config, VOX, SSB BW, keying weight, software list. Also wires RX-row button label state observers.
- **ModePopupController** — mode selector popup
- **FeatureMenuController** — ATTN/NB/NR/NOTCH feature popup
- **AntennaConfigController** — three antenna-config popups
- **MenuController** — K4 MEDF menu system + overlay

### Signal dispatch (plumbing)
- **ButtonRowDispatcher** — Main RX / Sub RX / TX button-row click → CAT / popup / mode-aware
- **MacroController** — Fn popup / PF1-4 / F1-F12 / KPOD macro dispatch
- **BandNavigationController** — band-popup selection → BN CAT + band-stack logic
- **SideControlScrollController** — SideControlPanel scroll-wheel handlers (WPM/pitch/mic/comp/power QRP-QRO/delay/BW/HI/LO/SHIFT/RF gain/squelch); shared HI/LO filter-edge math
- **RightSideController** — RightSidePanel button click → CAT / FeatureMenu / ModePopup / Macro dispatch. B-SET-aware APF + RATE + KHZ routing.
- **MemoryButtonsController** — M1-M4 / REC / STORE / RCL message-memory button dispatch. Primary left-click sends SW17/51/18/52/19/20/34; right-click on REC/STORE/RCL sends alt SW137/138/139 (BANK / AF REC / AF PLAY) via installed event filter.

### Display / indicator (RadioState → widget)
- **StatusBarController** — top status bar (clock, readings, K4+KPA status)
- **ModeLabelController** — VFO A/B mode label text (ESSB "+" suffix included)
- **VfoFrequencyController** — VFO A/B frequency display (RIT/XIT offset applied)
- **AntennaDisplayController** — TX / RX Main / RX Sub antenna label text
- **ProcessingDisplayController** — AGC/preamp/attenuator/NB/NR indicators on VFOs
- **SideControlDisplayController** — side panel knob values (BW/SHFT/HI/LO, power, mic gain, etc.), CW↔voice display swap
- **VfoRowIndicatorController** — SPLIT/B-SET/VOX/QSK/TEST/ATU/MSG-bank labels (SPLIT and B-SET share the same screen slot — mutually exclusive visibility)
- **SubDivIndicatorController** — SUB/DIV badges + VFO B dim state
- **TxStateController** — TX triangles + indicator colors + VFO meter mode flip + PA current calc
- **RitXitController** — RIT/XIT label state + wheel accumulator + click handlers + BSet-aware display routing
- **KPA1500UiController** — KPA1500 mini panel + amp status + client lifecycle
- **TextDecodeController** — text decode window lifecycle + CAT dispatch

---

## When you extend

Adding a new user-visible feature?
1. If it's a popup → likely belongs in **PopupManager** (or a new popup controller)
2. If it's a status indicator → one of the `*DisplayController` or `*IndicatorController` classes
3. If it's a CAT-dispatching action → dispatcher / config controller
4. If it's a widget that needs RadioState reactions → new small presentation controller

Controllers should generally be 50-300 LOC. If yours grows past that, split by responsibility.
See `PATTERNS.md` → Controller Pattern for the dependency-injection / lifecycle rules.

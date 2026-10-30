# Patterns

How-to recipes for QK4. The rules these recipes follow are in `CONVENTIONS.md`; this file shows how
to meet them. Examples use the real class names and include paths.

Contents: Wiring a Widget to Radio State (incl. Direct Observation, High-DPI drawing) · Adding Radio
State or a CAT Command · Subsystem State · Controller Pattern · Adding a Popup Menu · Adding a
Protocol Packet Type · Anti-patterns.

What each source directory contains is in that directory's `README.md`; this file does not repeat it.

---

## Wiring a Widget to Radio State

Pick one of two shapes. Never route a value through a `MainWindow` slot (anti-pattern 2).

| Choose | When the widget |
|---|---|
| **Direct Observation** | shows one or a few `RadioState` properties, coordinates with no other widget, sends no CAT |
| **Controller** | coordinates several widgets, sends CAT, or behaves differently by mode |

Rule of thumb: if the slot body would be `m_target->setValue(incoming)`, the widget observes directly.

### Direct Observation

The widget connects its own setters to `RadioState`. `FilterIndicatorWidget` is the reference:

```cpp
// src/ui/widgets/filterindicatorwidget.h
class RadioState;

class FilterIndicatorWidget : public QWidget {
    Q_OBJECT
public:
    enum class Vfo { A, B };
    explicit FilterIndicatorWidget(QWidget *parent = nullptr);

    // Call once after construction.
    void observe(RadioState *state, Vfo vfo);

    void setFilterPosition(int position);
    void setBandwidth(int bandwidthHz);
    // ...
};

// src/ui/widgets/filterindicatorwidget.cpp
void FilterIndicatorWidget::observe(RadioState *state, Vfo vfo) {
    if (!state)
        return;
    if (vfo == Vfo::A) {
        connect(state, &RadioState::filterPositionChanged, this, &FilterIndicatorWidget::setFilterPosition);
        connect(state, &RadioState::filterBandwidthChanged, this, &FilterIndicatorWidget::setBandwidth);
        connect(state, &RadioState::modeChanged, this,
                [this](RadioState::Mode mode) { setMode(RadioState::modeToString(mode)); });
    } else {
        connect(state, &RadioState::filterPositionBChanged, this, &FilterIndicatorWidget::setFilterPosition);
        // ...
    }
}
```

`MainWindow` makes one call per widget: `m_filterAWidget->observe(m_radioState, FilterIndicatorWidget::Vfo::A);`.

Every lambda connection names a context object (`this` above), so Qt disconnects it when the widget
is destroyed (CONVENTIONS Rule 14).

### Custom widget signals

A widget reports user actions as signals with values; it never sends CAT itself. The owning
controller connects the signal and calls `ConnectionController::sendCAT()`:

```cpp
// In the widget
signals:
    void frequencyClicked(qint64 frequency);

// In the controller's constructor
connect(m_panadapter, &PanadapterRhiWidget::frequencyClicked, this,
        [this](qint64 freq) { m_connection->sendCAT(QString("FA%1;").arg(freq, 11, 10, QChar('0'))); });
```

### GPU widgets

Spectrum and waterfall widgets subclass `QRhiWidget` (Qt Widgets, since Qt 6.7). See
`src/dsp/panadapter_rhi.cpp`. Shaders are compiled with `qt6_add_shaders()` in `CMakeLists.txt`.
Size GPU buffers and textures in device pixels (`size() * devicePixelRatio()`), not widget pixels.

Unit tests do not create a GPU context, and an offscreen `QRhiWidget` has nothing to render to.
Keep any logic you want tested out of the widget, in a plain class (`spectrumgeometry`,
`waterfallgeometry`, `dashgeometry`), and test that.

### High-DPI drawing

Widget geometry and `QPainter` calls are in device-independent pixels, and Qt scales them by the
device pixel ratio for you. Image buffers are not: "Image buffers such as QImage and QPixmap represent
the raw pixels, and as a result do not operate in the device independent coordinate system"
(*High DPI*, Drawing). A pixmap drawn at 1× and shown on a 2× or 2.5× screen is stretched and blurred.

- Prefer painting in `paintEvent()`; it is sharp at every ratio with no extra work.
- For an icon or glyph, return a `QIcon` backed by a `QIconEngine` that paints at the size and ratio
  it is asked for. `K4Glyphs` (`src/ui/styling/k4glyphs.cpp`) is the reference;
  `WidgetStateTests::glyphsRenderAtTheDevicePixelRatio` holds it at 1×, 2× and 2.5×.
- **Never draw into a `QPixmap` without setting its device pixel ratio.** Allocate it at
  `logicalSize * dpr` device pixels, call `setDevicePixelRatio(dpr)`, then paint in logical
  coordinates. Take `dpr` from the widget (`devicePixelRatioF()`) or the icon request, never assume 1.

---

## Adding Radio State or a CAT Command

State lives in plain-struct **subsystems** under `src/models/radiostate/`. `RadioState` is the
`QObject` façade that owns them, dispatches CAT and emits every signal.

1. **Pick the subsystem** whose topic fits (Subsystem State, below). A new subsystem is rare.
2. **Add the field** to the struct and restore its sentinel in the struct's `reset()`.
3. **Add a handler** in the subsystem's `*Handlers` namespace:
   `void handleXX(XxxState &state, RadioState &owner, const QString &cmd)`. Mutate the struct, then
   `emit owner.myThingChanged(value);`.
4. **Declare the signal** on `RadioState`. Subsystems are not `QObject`s, so signals live on the
   façade.
5. **Add a getter** on `RadioState` that delegates: `Type myThing() const { return m_xxxState.myThing; }`.
6. **Register the prefix** in `RadioState::registerCommandHandlers()`. Register `XX$` before `XX`.
7. **Add a test** to `tests/test_radiostate.cpp` (CONVENTIONS Rule 6).
8. **Connect the UI** by direct observation or in the owning controller (`docs/controllers.md`).

## Subsystem State

The topic → CAT map for step 1. `ls src/models/radiostate/*.h` is authoritative.

| Subsystem | Topic | Main CAT commands |
|-----------|-------|-------------------|
| `FrequencyVfoState` | VFO A/B frequency, split, RIT/XIT offsets | FA, FB, FT, RT, RT$, XT, RO, RO$ |
| `ModeFilterState` | Mode, filter, CW keyer | MD, MD$, BW, BW$, IS, IS$, FP, FP$, CW, KS, KP |
| `ProcessingState` | NB / NR / PA / RA / GT / NA / NM per VFO | NB, NR, PA, RA, GT, NA, NM (+ `$`) |
| `AntennaState` | Antenna selection, per-band masks, ATU | AN, AT, ACN, ACM, ACS, ACT, AR, AR$ |
| `AudioEffectsState` | VOX, effects, APF, ESSB, EQ, line in/out, mic, monitor, mix, balance | FX, AP, AP$, VX, VG, VI, ES, RE, TE, LO, LI, MI, MS, ML, MX, BL |
| `DataControlState` | Data sub-mode, rate, tuning step, streaming latency | DT, DT$, DR, DR$, VT, VT$, SL |
| `SpectrumDisplayState` | `#` panadapter/waterfall display state | #REF, #SPN, #SCL, #MP, #DPM, #FPS, #WFC, #AVG, #FXT, #FXA, #AR … |
| `TextDecodeState` | Text decoder mode, threshold, lines | TD, TD$, TB, TB$ |
| `RxTxMeterState` | S-meter, TX meters, RX/TX, supply, identity, message bank, toggles | SM, SM$, SMH, SMH$, PO, TM, TX, RX, ID, OM, RV., ER, MN, SIFP, SB, DV, TS, BS |
| `LevelsState` | TX power, mic gain, compression, RF gain, squelch | PC, MG, CP, RG, RG$, SQ, SQ$ |
| `QskControlState` | Full break-in and TX-to-RX delay | SD |
| `PowerState` | Radio power state | PS |
| `XvtrBandState` | Transverter bands | XVN, XVM, XVR, XVI, XVO |

`RadioState` and `MenuModel` are the two `QObject`s in `src/models/`.

### Registering a handler

Most prefixes go through a one-line façade method:

```cpp
// registerCommandHandlers()
m_commandHandlers.append({"MD", [this](const QString &c) { handleMD(c); }});

// radiostate.cpp
void RadioState::handleMD(const QString &cmd) {
    ModeFilterHandlers::handleMD(m_modeFilterState, *this, cmd);
}
```

Newer subsystems call the handler straight from the lambda, with no façade method. Either form is
fine; prefer the direct one for new code:

```cpp
m_commandHandlers.append({"PS", [this](const QString &c) { PowerHandlers::handlePS(m_powerState, *this, c); }});
```

### Why plain structs

- One thread-affinity check. Only `RadioState` is a `QObject`; the assert in `parseCATCommand()`
  covers every subsystem (CONVENTIONS Rule 4).
- No moc and no child-destruction order to manage per subsystem.
- The public API does not change. Callers use `radioState->frequency()`. The part CatServer depends on
  is pinned by `docs/radiostate-catserver-api-contract.md`.
- Each subsystem contributes its own prefixes to the longest-first dispatch table.

Gates: `test_radiostate_registry` (CI) checks prefix order; `test_radiostate_golden` replays a local
K4 trace when the fixture exists (it skips in CI).

---

## Controller Pattern

A controller in `src/controllers/` owns one slice of UI or domain: its widgets, their wiring, the CAT
it sends and any state only that slice needs. `MainWindow` constructs controllers and coordinates
them; it does not reach into them. Every controller is listed in `docs/controllers.md` with the
user-visible symptoms that lead to it; add yours there.

### Template

```cpp
// src/controllers/mycontroller.h
#ifndef MYCONTROLLER_H
#define MYCONTROLLER_H

#include <QObject>

class ConnectionController;
class QLabel;
class QWidget;
class RadioState;

class MyController : public QObject {
    Q_OBJECT

public:
    MyController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                 QObject *parent = nullptr);

    // Task-level API only. No getters that return owned widgets or workers.
    void reset();

signals:
    void somethingRequested(int value);

private:
    void onFrequencyChanged(quint64 freq);

    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    QLabel *m_label = nullptr;          // owned through its Qt parent
};

#endif // MYCONTROLLER_H

// src/controllers/mycontroller.cpp
#include "controllers/mycontroller.h"

#include "controllers/connectioncontroller.h"
#include "models/radiostate.h"

#include <QLabel>

MyController::MyController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                           QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_label(new QLabel(parentWidget)) {
    connect(m_radioState, &RadioState::frequencyChanged, this, &MyController::onFrequencyChanged);
}
```

### Constructor

1. **Inject dependencies.** Take `RadioState*`, `ConnectionController*` and any peers as pointers.
   Store them; never delete them.
2. **Widget parent and QObject parent are separate.** Widgets need a `QWidget` parent for paint and
   layout; the controller's `QObject` parent (usually MainWindow) handles its own cleanup.
3. **No singleton lookups** for new dependencies (CONVENTIONS Rule 15). `RadioSettings::instance()` is
   the one existing exception.
4. **Wire your own signals here.** Do not hand objects back to MainWindow to connect.

### Destructor

Write one only when something can still call into the object while it is being destroyed
(CONVENTIONS Rule 11). Most controllers need none; child widgets and `QObject` children are deleted by
their parents. A controller that owns a thread stops it in dependency order (Rule 11) and deletes its
worker with `QThread::finished → deleteLater` (Rule 14).

### Public API

1. Task-level operations: `popupManager->toggleBand()`, not a getter that returns the band popup.
2. Reads return values or `const Type&` (Rule 3).
3. State changes are signals.
4. Do not chain signals A → B → C just to pass a value along. Re-emit only when you transform it.

### Variants

- **Injected-widget controller.** MainWindow's layout code creates the widgets and passes them in;
  the controller owns only the wiring. `MemoryButtonsController` takes the seven M1–M4 / REC / STORE /
  RCL `QPushButton*`s and a `ConnectionController*`, sends their CAT, and installs its own event filter
  for right-click actions. `SubDivIndicatorController` is the same shape for indicator labels.
- **Orchestration controller.** Owns no widgets or threads; wires already-built objects together.
  `CwController` wires the CW devices that `HardwareController` constructs and owns.

### Testing

`tests/test_dxclustercontroller.cpp` is the reference for a controller test. It constructs a real
`DxClusterController` against a local fake node and checks only the public API and signals. Use real
`RadioState` instances, not mocks (`tests/test_radiostate.cpp`). Do not poke internal widgets. Most
controllers have no test yet; they are covered by manual testing on the radio.

---

## Adding a Popup Menu

All popups inherit `K4PopupBase` (`src/ui/popups/k4popupbase.h`). List the current subclasses with:

```bash
rg -o 'class\s+(\w+)\s*:\s*public\s+K4PopupBase' -r '$1' src
```

### What K4PopupBase does

- Window flags `Qt::Tool | Qt::FramelessWindowHint`, translucent background, strong focus.
- Draws the drop shadow (`K4Styles::drawDropShadow`, `ShadowLayers` layers) and the
  `PopupBackground` fill.
- Positions itself above a trigger and keeps itself on screen.
- Closes on Escape and on a mouse press outside its window (app-wide event filter on `qApp`).
- Emits `closed()` when hidden.

### API

| Member | Purpose |
|---|---|
| `K4PopupBase(QWidget *parent)` | Sets flags, translucency, focus policy |
| `showAboveButton(QWidget *trigger)` | Show centred above the trigger's parent |
| `showAboveWidget(QWidget *ref)` | Show above a reference widget |
| `hidePopup()` | Hide and emit `closed()` |
| `isVisibleOrJustHidden()` | True while visible or just closed by the click-away filter. Toggle handlers read this, not `isVisible()`, so the dismissing click does not reopen the popup |
| signal `closed()` | Hidden by `hidePopup()`, Escape or click-away |
| `contentSize()` *(protected, pure virtual)* | Size of the content area, without shadow margins |
| `contentMargins()` *(protected)* | Layout margins: `ShadowMargin + PopupContentMargin` on each side |
| `contentRect()` *(protected)* | Content rectangle for custom painting |
| `initPopup()` *(protected)* | Sizes the widget from `contentSize()`. Call last in the constructor |
| `paintContent(QPainter&, const QRect&)` *(protected, virtual)* | Custom drawing after shadow and background |
| `paintCompactRowFrame(QPainter&, const QRect&, …)` *(protected)* | Gradient frame and delimiters for single-row popups. Call from `paintContent()` |

### Copy from

| Popup type | Start from |
|---|---|
| Single row | `src/ui/popups/buttonrowpopup.cpp` (simplest) |
| Grid with selection | `src/ui/popups/bandpopupwidget.cpp` |
| Settings panel (+/- controls, toggles) | `src/ui/popups/displaypopupwidget.cpp` |
| Mode grid with sub-modes | `src/ui/popups/modepopupwidget.cpp` |

### Step 1: header

```cpp
// src/ui/popups/mypopup.h
#ifndef MYPOPUP_H
#define MYPOPUP_H

#include "ui/popups/k4popupbase.h"
#include <QList>

class QPushButton;

class MyPopup : public K4PopupBase {
    Q_OBJECT

public:
    explicit MyPopup(QWidget *parent = nullptr);

signals:
    void itemSelected(int index); // closed() is inherited

protected:
    QSize contentSize() const override;

private:
    QList<QPushButton *> m_buttons;
};

#endif // MYPOPUP_H
```

### Step 2: implementation

```cpp
// src/ui/popups/mypopup.cpp
#include "ui/popups/mypopup.h"
#include "ui/styling/k4styles.h"

#include <QHBoxLayout>
#include <QPushButton>

namespace {
constexpr int kButtonCount = 7;
constexpr int kButtonWidth = 70;
constexpr int kButtonSpacing = 8;
} // namespace

MyPopup::MyPopup(QWidget *parent) : K4PopupBase(parent) {
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(contentMargins());
    row->setSpacing(kButtonSpacing);

    for (int i = 0; i < kButtonCount; ++i) {
        auto *btn = new QPushButton(QString::number(i + 1), this);
        btn->setFixedSize(kButtonWidth, K4Styles::Dimensions::ButtonHeightLarge);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(K4Styles::popupButtonNormal());
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            emit itemSelected(i);
            hidePopup();
        });
        m_buttons.append(btn);
        row->addWidget(btn);
    }

    initPopup(); // last
}

QSize MyPopup::contentSize() const {
    const int cm = K4Styles::Dimensions::PopupContentMargin;
    return QSize(kButtonCount * kButtonWidth + (kButtonCount - 1) * kButtonSpacing + 2 * cm,
                 K4Styles::Dimensions::ButtonHeightLarge + 2 * cm);
}
```

### Step 3: build and README

Add `src/ui/popups/mypopup.cpp` to `SOURCES` and `src/ui/popups/mypopup.h` to `HEADERS` in
`CMakeLists.txt`. Add the file to `src/ui/popups/README.md`, which owns that directory's inventory.

### Step 4: own it from a controller

Never on MainWindow (CONVENTIONS Rule 12). A popup in the shared bottom-bar group goes in
`PopupManager`: add a member, a `toggleMy()` method shaped like `toggleBand()`, and the close in
`closeOwnedPopups()`. A popup tied to one feature goes on that feature's controller
(`ModePopupController`, `AntennaConfigController`):

```cpp
MyPopupController::MyPopupController(RadioState *state, ConnectionController *conn, QPushButton *trigger,
                                     QObject *parent)
    : QObject(parent), m_radioState(state), m_connection(conn), m_trigger(trigger),
      m_popup(new MyPopup(trigger->window())) {
    connect(m_popup, &MyPopup::itemSelected, this, &MyPopupController::onItemSelected);
    connect(m_trigger, &QPushButton::clicked, this, [this]() {
        if (m_popup->isVisibleOrJustHidden())
            m_popup->hidePopup();
        else
            m_popup->showAboveButton(m_trigger);
    });
}
```

### Custom painting

```cpp
void MyPopup::paintContent(QPainter &painter, const QRect &contentRect) {
    painter.setPen(K4Styles::borderColor());
    painter.drawLine(contentRect.left() + 10, contentRect.center().y(), contentRect.right() - 10,
                     contentRect.center().y());
}
```

### Selection styling

Existing popups swap sheets per state:

```cpp
btn->setStyleSheet(btn == m_selected ? K4Styles::popupButtonSelected() : K4Styles::popupButtonNormal());
```

For new code the target is one sheet with a state selector (`docs/styling.md` → Qt style-sheet
rules): make the button checkable, give it a sheet with a `QPushButton:checked` rule, and call
`setChecked()`. Qt re-applies `:checked` by itself. If you select on a dynamic property instead, re-apply the sheet after
`setProperty()`, because a property selector does not re-evaluate on its own.

### Checklist

1. Inherit `K4PopupBase`.
2. Implement `contentSize()`; the base adds shadow margins.
3. Lay out with `contentMargins()`.
4. Call `initPopup()` last.
5. Use `K4Styles` tokens and helpers; never draw a shadow yourself.

---

## Adding a Protocol Packet Type

1. `src/network/protocol.h` — add the value to `PayloadType` and a signal for it.
2. `src/network/protocol.cpp` — handle it in `processPacket()`.
3. `src/controllers/connectioncontroller.cpp` — re-emit the signal from `ConnectionController`, as the
   existing `Protocol` signals are, so no caller reaches into `tcpClient()->protocol()`.
4. Connect the re-emitted signal in the controller that owns the feature.
5. Add a case to `tests/test_protocol.cpp`.

`Protocol` lives on the I/O thread. Its signals reach main-thread receivers queued; keep the payload
an implicitly shared value type (`QByteArray`, `QString`).

---

## Anti-patterns

These shapes created the 2026-04 god object. Reject them in review, including your own.

### 1. New functional code in MainWindow

MainWindow is window chrome, top-level layout and controller coordination. A new widget member, setup
method or business-logic slot on MainWindow is a regression.

### 2. MainWindow as signal middleman

```cpp
void MainWindow::onFooChanged(int v) { m_fooLabel->setText(QString::number(v)); }
```

The widget should observe `RadioState` directly.

### 3. Long inline lambdas or connect clusters

More than ~5 lines in a lambda, or more than ~5 `connect()` calls at one site: extract a named helper.
Past ~30 lines with a cohesive scope, make it a controller.

### 4. Cross-controller reach-in

`controllerA->someGetter()->doThing()` couples two controllers through internals. Controllers talk
through signals or task-level methods passed in by injection (`m_connection->sendCAT(…)` is fine). The
accessors that exist anyway are listed in CONVENTIONS Rule 2.

### 5. Hidden state in globals or singletons

Shared radio state flows through `RadioState`, network state through `ConnectionController`.
Controllers receive what they need in their constructor (CONVENTIONS Rule 15).

### 6. Bare `TODO` / `FIXME` / `HACK`

Use `// TODO(gh#NNN): <summary>` with a filed issue, or delete the comment (CONVENTIONS → Comment Conventions).

// State-aware sheets (K4Styles::setStyleFlag(), setStyleState(), whenFlag(), whenState()): a widget keeps
// one sheet and a dynamic property picks its look, replacing a setStyleSheet() swap on every state
// change. Each case holds that the change is invisible: the one sheet with the state's properties set
// renders exactly the pixels the sheet the old code swapped in for that state rendered.
//
// The old sheets below are frozen copies of what the swapping code built, so a later edit to a helper
// cannot move both sides at once. Each area adds its cases in its own function.

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTest>

#include <memory>

#include "ui/styling/k4styles.h"

namespace {

enum class Kind : quint8 { Label, Button, LineEdit };
enum class Interaction : quint8 { None, Hover, Pressed };

struct StyleProp {
    const char *name;
    QVariant value; // bool → setStyleFlag(), string → setStyleState()
};

struct StateCase {
    QString name; // "<area>/<widget>/<state>"
    Kind kind = Kind::Label;
    QString oldSheet;       // what the old code set for this state
    QString newSheet;       // the one sheet the widget now keeps
    QList<StyleProp> props; // the properties this state sets (empty = the widget's resting state)
    QString text = QStringLiteral("NB");
    QSize size = QSize(96, 32);
    bool checkable = false; // a checkable button in this state is checked
    bool enabled = true;
};

QWidget *makeWidget(const StateCase &c) {
    switch (c.kind) {
    case Kind::Label:
        return new QLabel(c.text);
    case Kind::LineEdit:
        return new QLineEdit(c.text);
    case Kind::Button: {
        auto *button = new QPushButton(c.text);
        if (c.checkable) {
            button->setCheckable(true);
            button->setChecked(true);
        }
        return button;
    }
    }
    return nullptr;
}

// WHY: a push button reports :hover only after a mouse move inside it while it is under the mouse
// (QPushButton::mouseMoveEvent sets its hovering flag), so the attribute alone is not enough.
void applyInteraction(QWidget *widget, Interaction interaction) {
    widget->ensurePolished();
    if (interaction != Interaction::None) {
        widget->setAttribute(Qt::WA_UnderMouse, true);
        const QPointF centre = QRectF(widget->rect()).center();
        QMouseEvent move(QEvent::MouseMove, centre, widget->mapToGlobal(centre), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &move);
    }
    if (auto *button = qobject_cast<QPushButton *>(widget))
        button->setDown(interaction == Interaction::Pressed);
}

QImage render(QWidget *widget) {
    widget->ensurePolished();
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // WHY: without DrawWindowBackground, as a child widget draws. A top-level window fills its corners
    // outside the border radius with the palette the sheet set at polish time, which no child shows.
    widget->render(&image, {}, {}, QWidget::DrawChildren);
    return image;
}

QImage renderWith(const StateCase &c, const QString &sheet, bool applyProps, Interaction interaction) {
    std::unique_ptr<QWidget> widget(makeWidget(c));
    widget->setFixedSize(c.size);
    widget->setEnabled(c.enabled);
    widget->setStyleSheet(sheet);
    if (applyProps) {
        for (const StyleProp &prop : c.props) {
            if (prop.value.typeId() == QMetaType::Bool)
                K4Styles::setStyleFlag(widget.get(), prop.name, prop.value.toBool());
            else
                K4Styles::setStyleState(widget.get(), prop.name, prop.value.toString());
        }
    }
    applyInteraction(widget.get(), interaction);
    return render(widget.get());
}

// =============================================================================
// Cases: indicators (VFO, status bar, RIT/XIT, TX, SUB/DIV, VFO row)
// =============================================================================

void addIndicatorCases(QList<StateCase> &cases) {
    namespace C = K4Styles::Colors;
    namespace D = K4Styles::Dimensions;
    const StyleProp selected{K4Styles::SelectedProperty, true};
    const auto label = [&cases](const QString &name, const QString &oldSheet, const QString &newSheet,
                                QList<StyleProp> props, const QString &text, QSize size) {
        StateCase c;
        c.name = name;
        c.kind = Kind::Label;
        c.oldSheet = oldSheet;
        c.newSheet = newSheet;
        c.props = std::move(props);
        c.text = text;
        c.size = size;
        cases.append(c);
    };

    // VFOWidget feature row (setAGC, setPreamp, setAtt, setNB, setNR, setNotch, setApf).
    const auto feature = [](const char *color) {
        return QString("color: %1; font-size: %2px;").arg(color).arg(D::FontSizeBody);
    };
    label("indicators/vfoFeature/off", feature(C::TextSecondary), K4Styles::vfoFeatureLabel(), {}, "NTCH-A",
          QSize(64, 20));
    label("indicators/vfoFeature/on", feature(C::TextPrimary), K4Styles::vfoFeatureLabel(), {selected}, "NTCH-A",
          QSize(64, 20));

    // VfoRowIndicatorController: VOX and ATU amber when on, QSK white.
    const auto largeBold = [](const char *color) {
        return QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(D::FontSizeBody);
    };
    const QSize rowSize(48, 24);
    label("indicators/vox/off", largeBold(C::TextSecondary), K4Styles::vfoRowIndicatorLabel(C::AccentAmber), {}, "VOX",
          rowSize);
    label("indicators/vox/on", largeBold(C::AccentAmber), K4Styles::vfoRowIndicatorLabel(C::AccentAmber), {selected},
          "VOX", rowSize);
    label("indicators/atu/off", largeBold(C::TextSecondary), K4Styles::vfoRowIndicatorLabel(C::AccentAmber), {}, "ATU",
          rowSize);
    label("indicators/atu/on", largeBold(C::AccentAmber), K4Styles::vfoRowIndicatorLabel(C::AccentAmber), {selected},
          "ATU", rowSize);
    label("indicators/qsk/off", largeBold(C::TextSecondary), K4Styles::vfoRowIndicatorLabel(C::TextPrimary), {}, "QSK",
          rowSize);
    label("indicators/qsk/on", largeBold(C::TextPrimary), K4Styles::vfoRowIndicatorLabel(C::TextPrimary), {selected},
          "QSK", rowSize);

    // SubDivIndicatorController: VFO B mode label, SUB and DIV badges.
    const QSize modeSize(D::VfoSquareSize, D::MinClickTarget);
    label("indicators/vfoBMode/subOn", largeBold(C::TextPrimary), K4Styles::vfoBModeLabel(), {}, "USB", modeSize);
    label("indicators/vfoBMode/dimmed", largeBold(C::TextSecondary), K4Styles::vfoBModeLabel(),
          {{K4Styles::DimmedProperty, true}}, "USB", modeSize);
    const auto badge = [](const char *background, const char *color) {
        return QString("background-color: %1; color: %2; font-size: %3px; font-weight: bold; border-radius: 2px;")
            .arg(background, color)
            .arg(D::FontSizeCaption);
    };
    const QSize badgeSize(36, 14);
    for (const char *text : {"SUB", "DIV"}) {
        const QString base = QString("indicators/%1/").arg(QString::fromLatin1(text).toLower());
        label(base + "off", badge(C::BadgeOffBackground, C::TextSecondary), K4Styles::subDivBadge(), {}, text,
              badgeSize);
        label(base + "on", badge(C::StatusGreen, C::TextOnAccent), K4Styles::subDivBadge(), {selected}, text,
              badgeSize);
    }

    // TxStateController: TX label and the two TX triangles.
    const auto txIndicator = [](const char *color) {
        return QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(D::FontSizeIndicator);
    };
    const auto txTriangle = [](const char *color) {
        return QString("color: %1; font-size: %2px;").arg(color).arg(D::FontSizeIndicator);
    };
    label("indicators/txIndicator/rx", txIndicator(C::AccentAmber), K4Styles::txIndicatorLabel(), {}, "TX",
          QSize(32, D::ButtonHeightMini));
    label("indicators/txIndicator/tx", txIndicator(C::TextRed), K4Styles::txIndicatorLabel(), {selected}, "TX",
          QSize(32, D::ButtonHeightMini));
    const QSize triangleSize(D::ButtonHeightMini, D::ButtonHeightMini);
    label("indicators/txTriangle/rx", txTriangle(C::AccentAmber), K4Styles::txTriangleLabel(), {},
          QString::fromUtf8("◀"), triangleSize);
    label("indicators/txTriangle/tx", txTriangle(C::TextRed), K4Styles::txTriangleLabel(), {selected},
          QString::fromUtf8("◀"), triangleSize);

    // RitXitController::applyLabels.
    const QString ritActive = QString("color: %1; font-size: %2px; font-weight: bold; border: none;")
                                  .arg(C::TextPrimary)
                                  .arg(D::FontSizeCaption);
    const QString ritInactive =
        QString("color: %1; font-size: %2px; border: none;").arg(C::TextSecondary).arg(D::FontSizeCaption);
    const QSize ritSize(28, 16);
    label("indicators/rit/off", ritInactive, K4Styles::ritXitLabel(), {}, "RIT", ritSize);
    label("indicators/rit/on", ritActive, K4Styles::ritXitLabel(), {selected}, "RIT", ritSize);
    label("indicators/xit/off", ritInactive, K4Styles::ritXitLabel(), {}, "XIT", ritSize);
    label("indicators/xit/on", ritActive, K4Styles::ritXitLabel(), {selected}, "XIT", ritSize);
    const auto ritValue = [](const char *color) {
        return QString("color: %1; font-size: %2px; font-weight: bold; border: none; padding: 0 11px;")
            .arg(color)
            .arg(D::FontSizeHeading);
    };
    label("indicators/ritXitValue/off", ritValue(C::TextSecondary), K4Styles::ritXitValueLabel(), {}, "+0.00",
          QSize(78, 20));
    label("indicators/ritXitValue/on", ritValue(C::TextPrimary), K4Styles::ritXitValueLabel(), {selected}, "-1.25",
          QSize(78, 20));

    // StatusBarController: K4 connection label and KPA1500 label.
    const auto readout = [](const char *color) {
        return QString("color: %1; font-size: %2px;").arg(color).arg(D::FontSizeReadout);
    };
    const auto readoutBold = [](const char *color) {
        return QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(D::FontSizeReadout);
    };
    const QSize k4Size(28, 20);
    label("indicators/connection/disconnected", readout(C::TextSecondary), K4Styles::connectionStatusLabel(), {}, "K4",
          k4Size);
    label("indicators/connection/connecting", readoutBold(C::AccentAmber), K4Styles::connectionStatusLabel(),
          {{K4Styles::StateProperty, QStringLiteral("connecting")}}, "K4", k4Size);
    label("indicators/connection/connected", readoutBold(C::StatusGreen), K4Styles::connectionStatusLabel(),
          {{K4Styles::StateProperty, QStringLiteral("connected")}}, "K4", k4Size);
    label("indicators/connection/error", readoutBold(C::TextRed), K4Styles::connectionStatusLabel(),
          {{K4Styles::StateProperty, QStringLiteral("error")}}, "K4", k4Size);
    label("indicators/kpa1500/disconnected", readout(C::TextSecondary), K4Styles::kpa1500StatusLabel(), {}, "KPA1500",
          QSize(72, 20));
    label("indicators/kpa1500/connected", readoutBold(C::StatusGreen), K4Styles::kpa1500StatusLabel(), {selected},
          "KPA1500", QSize(72, 20));
}

// =============================================================================
// Cases: dialogs and Options pages
// =============================================================================

void addDialogCases(QList<StateCase> &cases) {
    using namespace K4Styles;
    const auto label = [&cases](const QString &name, const QString &oldSheet, const QString &newSheet,
                                QList<StyleProp> props, const QString &text) {
        StateCase c;
        c.name = name;
        c.kind = Kind::Label;
        c.oldSheet = oldSheet;
        c.newSheet = newSheet;
        c.props = std::move(props);
        c.text = text;
        c.size = QSize(260, 28);
        cases.append(c);
    };
    const int heading = Dimensions::FontSizeHeading;

    // Options page status labels (CW keyer, DX cluster, KPA1500, K-Pod, CAT server, TCI server):
    // the old code swapped Dialog::statusLabel(colour), frozen here.
    const auto oldStatus = [heading](const char *color) {
        return QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(heading);
    };
    const QString status = Dialog::statusLabelByState();
    const QString statusText = QStringLiteral("Listening on port 9299");
    label("dialogs/statusLabel/resting", oldStatus(Colors::TextRed), status, {}, statusText);
    label("dialogs/statusLabel/ok", oldStatus(Colors::StatusGreen), status,
          {{StateProperty, QString(Dialog::StatusOk)}}, statusText);
    label("dialogs/statusLabel/idle", oldStatus(Colors::TextSecondary), status,
          {{StateProperty, QString(Dialog::StatusIdle)}}, statusText);

    // K-Pod device-summary values: a value, or a dimmed italic "N/A".
    const QString value = Dialog::valueLabelByState();
    label("dialogs/kpodValue/resting", Dialog::labelText(Colors::TextPrimary, heading), value, {},
          QStringLiteral("Elecraft KPOD"));
    label("dialogs/kpodValue/dimmed",
          Dialog::labelText(Colors::TextSecondary, heading) + QStringLiteral(" font-style: italic;"), value,
          {{DimmedProperty, true}}, QStringLiteral("N/A"));

    // Server Manager host field: RadioManagerDialog::lineEditStyle(border) as it was.
    const auto serverEdit = [](const char *border) {
        return QString("QLineEdit { "
                       "  background-color: %1; "
                       "  color: %2; "
                       "  border: 1px solid %3; "
                       "  border-radius: 4px; "
                       "  padding: %4px; "
                       "  min-width: 150px; "
                       "  font-size: %5px; "
                       "}")
            .arg(Colors::DarkBackground, Colors::TextPrimary, border)
            .arg(Dimensions::PaddingSmall)
            .arg(Dimensions::FontSizeBody);
    };
    for (const bool error : {false, true}) {
        StateCase c;
        c.name = error ? "dialogs/serverHostEdit/error" : "dialogs/serverHostEdit/resting";
        c.kind = Kind::LineEdit;
        c.oldSheet = serverEdit(error ? Colors::TextRed : Colors::DialogBorder);
        c.newSheet = Dialog::serverLineEdit();
        if (error)
            c.props = {{ErrorProperty, true}};
        c.text = QStringLiteral("192.168.1.");
        c.size = QSize(200, 30);
        cases.append(c);
    }

    // Macro dialog rows: function, label and command columns.
    const QString plain = QStringLiteral("color: %1; font-size: %2px;");
    const QString function = Dialog::macroFunctionLabel();
    label("dialogs/macroFunction/resting", plain.arg(Colors::TextSecondary).arg(heading), function, {},
          QStringLiteral("Keyboard-F1"));
    label("dialogs/macroFunction/selected",
          QString("color: %1; font-size: %2px; font-weight: bold;").arg(Colors::TextPrimary).arg(heading), function,
          {{SelectedProperty, true}}, QStringLiteral("Keyboard-F1"));

    const QString labelDisplay = Dialog::macroLabelDisplay();
    label("dialogs/macroLabel/resting", plain.arg(Colors::TextSecondary).arg(heading), labelDisplay, {},
          QStringLiteral("Unused"));
    label("dialogs/macroLabel/unused",
          QString("color: %1; font-size: %2px; font-style: italic;").arg(Colors::TextSecondary).arg(heading),
          labelDisplay, {{StateProperty, QString(Dialog::MacroUnused)}}, QStringLiteral("Unused"));
    label("dialogs/macroLabel/mapped", plain.arg(Colors::AccentAmber).arg(heading), labelDisplay,
          {{StateProperty, QString(Dialog::MacroMapped)}}, QStringLiteral("Mapped"));
    label("dialogs/macroLabel/named",
          QString("color: %1; font-size: %2px; font-weight: bold;").arg(Colors::AccentAmber).arg(heading), labelDisplay,
          {{StateProperty, QString(Dialog::MacroNamed)}}, QStringLiteral("CQ TEST"));
    label("dialogs/macroLabel/selected", plain.arg(Colors::TextPrimary).arg(heading), labelDisplay,
          {{StateProperty, QString(Dialog::MacroSelected)}}, QStringLiteral("CQ TEST"));

    const QString command = Dialog::macroCommandDisplay();
    const QString commandOld = QStringLiteral("color: %1; font-size: %2px; font-family: '%3';");
    label("dialogs/macroCommand/resting", commandOld.arg(Colors::TextSecondary).arg(heading).arg(Fonts::Data), command,
          {}, QStringLiteral("KY CQ TEST;"));
    label("dialogs/macroCommand/selected", commandOld.arg(Colors::TextPrimary).arg(heading).arg(Fonts::Data), command,
          {{SelectedProperty, true}}, QStringLiteral("KY CQ TEST;"));

    // Text decode window ON/OFF and AUTO/MANUAL keys: controlButton(selected) swapped, enabled and not.
    // The old selected sheet, frozen (generated from controlButton(true) before it was removed).
    const QString oldControlSelected = QStringLiteral(R"(
            QPushButton {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #E0E0E0, stop:0.4 #D0D0D0, stop:0.6 #C8C8C8, stop:1 #B8B8B8);
                color: #333333;
                border: 1px solid #AAAAAA;
                border-radius: 4px;
                font-size: 10px;
                font-weight: bold;
                padding: 2px 4px;
            }
            QPushButton:hover {
                border: 1px solid #808080;
            }
            QPushButton:pressed {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #B8B8B8, stop:0.4 #C8C8C8, stop:0.6 #D0D0D0, stop:1 #E0E0E0);
            }
        )");
    for (const bool selected : {false, true}) {
        for (const bool enabled : {true, false}) {
            StateCase c;
            c.name = QStringLiteral("dialogs/decodeToggle/%1%2")
                         .arg(selected ? "selected" : "resting", enabled ? "" : "-disabled");
            c.kind = Kind::Button;
            c.oldSheet = selected ? oldControlSelected : controlButton();
            c.newSheet = controlButtonSelectable();
            if (selected)
                c.props = {{SelectedProperty, true}};
            c.text = QStringLiteral("AUTO");
            c.size = QSize(48, Dimensions::ButtonHeightMini);
            c.enabled = enabled;
            cases.append(c);
        }
    }
}

// =============================================================================
// Cases: popups, menu bars and the menu overlay
// =============================================================================

void addPopupCases(QList<StateCase> &cases) {
    using namespace K4Styles;
    using K4Styles::Colors::TextPrimary;
    const auto button = [](const QString &name, const QString &oldSheet, const QString &newSheet,
                           const QList<StyleProp> &props, const QString &text, QSize size, bool checkable = false) {
        StateCase c;
        c.name = name;
        c.kind = Kind::Button;
        c.oldSheet = oldSheet;
        c.newSheet = newSheet;
        c.props = props;
        c.text = text;
        c.size = size;
        c.checkable = checkable;
        return c;
    };
    const QList<StyleProp> dimmed{{DimmedProperty, true}};
    const QList<StyleProp> selected{{SelectedProperty, true}};

    // Bottom menu bar PTT (BottomMenuBar::setPttActive): menuBarButton() ↔ menuBarButtonPttPressed(),
    // frozen here.
    const QSize pttSize(Dimensions::MenuBarButtonWidth, Dimensions::ButtonHeightMedium);
    const QString oldPttPressed =
        QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: 2px solid %1;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %3px;
            font-weight: 600;
        }
    )")
            .arg(Colors::TxActiveFill, TextPrimary, QString::number(Dimensions::FontSizeBody));
    cases << button("popups/ptt/resting", menuBarButton(), menuBarButtonPtt(), {}, "PTT", pttSize)
          << button("popups/ptt/transmitting", oldPttPressed, menuBarButtonPtt(), selected, "PTT", pttSize);

    // LINE OUT RIGHT key and value (LineOutPopupWidget::updateButtonStyles): greyed while R=L is on.
    const QSize lineOutKey(Dimensions::PopupButtonWidth, Dimensions::ButtonHeightMedium);
    const QSize lineOutValue(Dimensions::NavButtonWidth, Dimensions::ButtonHeightMedium);
    cases << button("popups/lineout-right/resting", popupButton(), popupButtonDimmable(), {}, "RIGHT", lineOutKey)
          << button("popups/lineout-right/checked", popupButton(), popupButtonDimmable(), {}, "RIGHT", lineOutKey, true)
          << button("popups/lineout-right/r-equals-l",
                    popupButton() + QString("QPushButton { color: %1; }").arg(Colors::TextDisabled),
                    popupButtonDimmable(), dimmed, "RIGHT", lineOutKey)
          << button("popups/lineout-right-value/resting", popupValueLabel(TextPrimary), popupValueLabelDimmable(), {},
                    "40", lineOutValue)
          << button("popups/lineout-right-value/r-equals-l", popupValueLabel(Colors::TextSecondary),
                    popupValueLabelDimmable(), dimmed, "40", lineOutValue);

    // RX EQ preset load key (EqPresetRowWidget): constructor sheet, named preset, empty preset.
    const QSize presetKey(48, 26);
    const QString oldPresetNamed =
        menuBarButtonSmall() +
        QString("QPushButton { font-size: %1px; padding: 2px; }").arg(K4Styles::Dimensions::FontSizeCaption);
    const QString oldPresetEmpty =
        menuBarButtonSmall() + QString("QPushButton { font-size: %1px; padding: 2px; color: %2; }")
                                   .arg(K4Styles::Dimensions::FontSizeCaption)
                                   .arg(K4Styles::Colors::TextSecondaryOnRaised);
    cases << button("popups/eq-preset-load/constructed", oldPresetNamed, eqPresetLoadButton(), {}, "---", presetKey)
          << button("popups/eq-preset-load/named", oldPresetNamed, eqPresetLoadButton(), {}, "VOICE", presetKey)
          << button("popups/eq-preset-load/empty", oldPresetEmpty, eqPresetLoadButton(), dimmed, "---", presetKey);

    // Menu overlay NORM key (MenuOverlayWidget::updateNormButton).
    const QSize normKey(34, Dimensions::PopupButtonHeight);
    const QString oldNormDisabled = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                            "border-radius: %3px; font-size: %4px; font-weight: bold; }")
                                        .arg(K4Styles::Colors::OverlayNavButton)
                                        .arg(K4Styles::Colors::TextDisabled)
                                        .arg(K4Styles::Dimensions::BorderRadius)
                                        .arg(K4Styles::Dimensions::FontSizeCaption);
    const QString oldNormDefault = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                           "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                           "QPushButton:pressed { background-color: %5; color: %6; }")
                                       .arg(K4Styles::Colors::OverlayNavButton)
                                       .arg(K4Styles::Colors::TextSecondaryOnRaised)
                                       .arg(K4Styles::Dimensions::BorderRadius)
                                       .arg(K4Styles::Dimensions::FontSizeCaption)
                                       .arg(K4Styles::Colors::OverlayNavButtonPressed)
                                       .arg(K4Styles::Colors::TextPrimary);
    const QString oldNormActive = QString("QPushButton { background-color: %1; color: %2; border: none; "
                                          "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                                          "QPushButton:pressed { background-color: %5; }")
                                      .arg(K4Styles::Colors::SelectionLight)
                                      .arg(K4Styles::Colors::TextOnLight)
                                      .arg(K4Styles::Dimensions::BorderRadius)
                                      .arg(K4Styles::Dimensions::FontSizeCaption)
                                      .arg(K4Styles::Colors::White);
    cases << button("popups/menu-norm/not-default", oldNormDefault, menuNormButton(), {}, "NORM", normKey)
          << button("popups/menu-norm/at-default", oldNormActive, menuNormButton(), selected, "NORM", normKey)
          << button("popups/menu-norm/greyed", oldNormDisabled, menuNormButton(), dimmed, "NORM", normKey);

    // Menu overlay row labels (MenuItemWidget::updateLabelColors): unselected, browsing, editing.
    const auto label = [](const QString &name, const QString &oldSheet, const QString &newSheet, const char *state,
                          const QString &text) {
        StateCase c;
        c.name = name;
        c.oldSheet = oldSheet;
        c.newSheet = newSheet;
        if (state[0] != '\0')
            c.props = {{StateProperty, QString::fromLatin1(state)}};
        c.text = text;
        c.size = QSize(160, Dimensions::MenuItemHeight - 10);
        return c;
    };
    const auto oldName = [](const char *color) {
        return QString("color: %1; font-size: %2px;").arg(color).arg(K4Styles::Dimensions::FontSizeHeading);
    };
    const auto oldValue = [](const char *color) {
        return QString("color: %1; font-size: %2px; font-weight: bold;")
            .arg(color)
            .arg(K4Styles::Dimensions::FontSizeHeading);
    };
    cases << label("popups/menu-row-name/unselected", oldName(Colors::TextSecondary), menuItemNameLabel(), "",
                   "VOX Gain")
          << label("popups/menu-row-name/browse", oldName(Colors::TextOnLight), menuItemNameLabel(), MenuRowBrowse,
                   "VOX Gain")
          << label("popups/menu-row-name/edit", oldName(Colors::SelectionLight), menuItemNameLabel(), MenuRowEdit,
                   "VOX Gain")
          << label("popups/menu-row-value/unselected", oldValue(Colors::TextSecondary), menuItemValueLabel(), "", "12")
          << label("popups/menu-row-value/browse", oldValue(Colors::TextPrimary), menuItemValueLabel(), MenuRowBrowse,
                   "12")
          << label("popups/menu-row-value/edit", oldValue(Colors::TextOnLight), menuItemValueLabel(), MenuRowEdit,
                   "12");
}

// =============================================================================

QList<StateCase> allCases() {
    QList<StateCase> cases;
    addIndicatorCases(cases);
    addDialogCases(cases);
    addPopupCases(cases);
    return cases;
}

} // namespace

class StateSheetsTests : public QObject {
    Q_OBJECT

private slots:
    void oneSheetMatchesTheSwappedSheet_data() {
        QTest::addColumn<int>("caseIndex");
        QTest::addColumn<int>("interaction");
        const QList<StateCase> cases = allCases();
        if (cases.isEmpty())
            QSKIP("no state-sheet cases yet");
        for (int i = 0; i < cases.size(); ++i) {
            const StateCase &c = cases.at(i);
            QTest::addRow("%s/idle", qPrintable(c.name)) << i << int(Interaction::None);
            if (c.kind == Kind::Button && c.enabled) {
                QTest::addRow("%s/hover", qPrintable(c.name)) << i << int(Interaction::Hover);
                QTest::addRow("%s/pressed", qPrintable(c.name)) << i << int(Interaction::Pressed);
            }
        }
    }

    void oneSheetMatchesTheSwappedSheet() {
        QFETCH(int, caseIndex);
        QFETCH(int, interaction);
        const StateCase c = allCases().at(caseIndex);
        const auto in = static_cast<Interaction>(interaction);
        const QImage expected = renderWith(c, c.oldSheet, false, in);
        const QImage actual = renderWith(c, c.newSheet, true, in);
        if (!qEnvironmentVariableIsEmpty("QK4_DUMP_DIR") && actual != expected) {
            const QString base = qEnvironmentVariable("QK4_DUMP_DIR") + '/' +
                                 QString::fromLatin1(QTest::currentDataTag()).replace('/', '_');
            expected.save(base + "_expected.png");
            actual.save(base + "_actual.png");
        }
        QCOMPARE(actual, expected);
    }

    // Guard for the case above: a state's properties must change the picture, or a misspelt property
    // name would leave the resting look in place and still pass when the resting case passes.
    void propertiesChangeThePicture_data() {
        QTest::addColumn<int>("caseIndex");
        const QList<StateCase> cases = allCases();
        int rows = 0;
        for (int i = 0; i < cases.size(); ++i) {
            if (!cases.at(i).props.isEmpty()) {
                QTest::addRow("%s", qPrintable(cases.at(i).name)) << i;
                ++rows;
            }
        }
        if (rows == 0)
            QSKIP("no state-sheet cases yet");
    }

    void propertiesChangeThePicture() {
        QFETCH(int, caseIndex);
        const StateCase c = allCases().at(caseIndex);
        QVERIFY(renderWith(c, c.newSheet, true, Interaction::None) !=
                renderWith(c, c.newSheet, false, Interaction::None));
    }

    void setStyleFlagAndStateRestyleOnlyOnChange() {
        QLabel label(QStringLiteral("NB"));
        label.setFixedSize(96, 32);
        const QString sheet =
            QStringLiteral("QLabel { color: %1; } %2 { color: %3; } %4 { color: %5; }")
                .arg(K4Styles::Colors::TextSecondary, K4Styles::whenFlag("QLabel", K4Styles::SelectedProperty),
                     K4Styles::Colors::TextPrimary, K4Styles::whenState("QLabel", K4Styles::StateProperty, "error"),
                     K4Styles::Colors::TextRed);
        label.setStyleSheet(sheet);
        const QImage resting = render(&label);
        K4Styles::setStyleFlag(&label, K4Styles::SelectedProperty, true);
        const QImage lit = render(&label);
        QVERIFY(lit != resting);
        K4Styles::setStyleFlag(&label, K4Styles::SelectedProperty, false);
        QCOMPARE(render(&label), resting);
        K4Styles::setStyleState(&label, K4Styles::StateProperty, QStringLiteral("error"));
        QVERIFY(render(&label) != resting);
        K4Styles::setStyleState(&label, K4Styles::StateProperty, QString());
        QCOMPARE(render(&label), resting);
        QCOMPARE(label.styleSheet(), sheet);
    }
};

QTEST_MAIN(StateSheetsTests)
#include "test_statesheets.moc"

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
    Q_UNUSED(cases);
}

// =============================================================================
// Cases: dialogs and Options pages
// =============================================================================

void addDialogCases(QList<StateCase> &cases) {
    Q_UNUSED(cases);
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

    // Bottom menu bar PTT (BottomMenuBar::setPttActive): menuBarButton() ↔ menuBarButtonPttPressed().
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
    // The standalone reference must still be the old sheet's look.
    cases << button("popups/ptt/standalone", oldPttPressed, menuBarButtonPttPressed(), {}, "PTT", pttSize);

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

// Selectable button sheets (K4Styles::popupButton(), menuBarButtonSelectable(),
// sidePanelButtonSelectable(), setSelected()): one sheet per button replaces swapping
// popupButtonNormal()/popupButtonSelected(), menuBarButton()/menuBarButtonActive() and
// sidePanelButton()/sidePanelButtonActive(). These cases hold that the change is invisible: in every state
// the single sheet renders exactly the pixels the swapped sheet rendered. popupButtonSelected() and
// sidePanelButtonActive() are gone from the app; their sheets are frozen below, as generated before.

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QTest>

#include "ui/styling/k4styles.h"

namespace {

enum class Interaction { None, Hover, Pressed };

struct Sheets {
    const char *name;
    QString unselected;
    QString selected;
    QString selectable;
};

// Frozen: K4Styles::popupButtonSelected() as it was.
const char *const kOldPopupSelected = R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #E0E0E0, stop:0.4 #D0D0D0, stop:0.6 #C8C8C8, stop:1 #B8B8B8);
            color: #333333;
            border: 2px solid #AAAAAA;
            border-radius: 6px;
            font-size: 11px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #F0F0F0, stop:0.4 #E0E0E0, stop:0.6 #D8D8D8, stop:1 #C8C8C8);
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #B8B8B8, stop:0.4 #C8C8C8, stop:0.6 #D0D0D0, stop:1 #E0E0E0);
        }
    )";

// Frozen: K4Styles::sidePanelButtonActive() as it was.
const char *const kOldSidePanelActive = R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #5a5a5a, stop:0.4 #4a4a4a, stop:0.6 #454545, stop:1 #3a3a3a);
            color: #FFFFFF;
            border: 2px solid #FFB000;
            border-radius: 6px;
            font-size: 10px;
            font-weight: bold;
            padding: 0px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #5a5a5a, stop:0.4 #4a4a4a, stop:0.6 #454545, stop:1 #3a3a3a);
            border: 2px solid #FFB000;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #3a3a3a, stop:0.4 #454545, stop:0.6 #4a4a4a, stop:1 #5a5a5a);
            border: 2px solid #FFB000;
        }
    )";

QList<Sheets> allSheets() {
    return {
        {"popup", K4Styles::popupButtonNormal(), QString::fromLatin1(kOldPopupSelected), K4Styles::popupButton()},
        {"menuBar", K4Styles::menuBarButton(), K4Styles::menuBarButtonActive(), K4Styles::menuBarButtonSelectable()},
        {"sidePanel", K4Styles::sidePanelButton(), QString::fromLatin1(kOldSidePanelActive),
         K4Styles::sidePanelButtonSelectable()}};
}

// WHY: a push button reports :hover only after a mouse move inside it while it is under the mouse
// (QPushButton::mouseMoveEvent sets its hovering flag), so the attribute alone is not enough.
void applyInteraction(QPushButton &button, Interaction interaction) {
    button.ensurePolished();
    if (interaction != Interaction::None) {
        button.setAttribute(Qt::WA_UnderMouse, true);
        const QPointF centre = QRectF(button.rect()).center();
        QMouseEvent move(QEvent::MouseMove, centre, button.mapToGlobal(centre), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(&button, &move);
    }
    button.setDown(interaction == Interaction::Pressed);
}

QImage render(QPushButton &button) {
    button.ensurePolished();
    QImage image(button.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // WHY: without DrawWindowBackground, as a button inside a popup draws. A top-level window fills its
    // corners outside the border radius with the palette the sheet set at polish time, which no button
    // in the app shows.
    button.render(&image, {}, {}, QWidget::DrawChildren);
    return image;
}

enum class Selection { Off, Checked, Property };

// The look the old code produced: the selected or unselected sheet set on its own, on the same kind of
// button (the swapped checkable keys were checked while they carried the selected sheet).
QImage renderSwapped(const QString &sheet, Selection selection, Interaction interaction) {
    QPushButton button(QStringLiteral("USB"));
    button.setFixedSize(K4Styles::Dimensions::MenuBarButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    button.setStyleSheet(sheet);
    if (selection == Selection::Checked) {
        button.setCheckable(true);
        button.setChecked(true);
    }
    applyInteraction(button, interaction);
    return render(button);
}

QImage renderSelectable(const QString &sheet, Selection selection, Interaction interaction) {
    QPushButton button(QStringLiteral("USB"));
    button.setFixedSize(K4Styles::Dimensions::MenuBarButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    button.setStyleSheet(sheet);
    if (selection == Selection::Checked) {
        button.setCheckable(true);
        button.setChecked(true);
    } else if (selection == Selection::Property) {
        K4Styles::setSelected(&button, true);
    }
    applyInteraction(button, interaction);
    return render(button);
}

} // namespace

class ButtonSheetsTests : public QObject {
    Q_OBJECT

private slots:
    void selectableSheetMatchesTheSwappedSheet_data() {
        QTest::addColumn<int>("sheetIndex");
        QTest::addColumn<int>("selection");
        QTest::addColumn<int>("interaction");
        const auto sheets = allSheets();
        const char *selections[] = {"unselected", "checked", "selectedProperty"};
        const char *interactions[] = {"idle", "hover", "pressed"};
        for (int s = 0; s < sheets.size(); ++s)
            for (int sel = 0; sel < 3; ++sel)
                for (int in = 0; in < 3; ++in)
                    QTest::addRow("%s/%s/%s", sheets[s].name, selections[sel], interactions[in]) << s << sel << in;
    }

    void selectableSheetMatchesTheSwappedSheet() {
        QFETCH(int, sheetIndex);
        QFETCH(int, selection);
        QFETCH(int, interaction);
        const Sheets sheets = allSheets().at(sheetIndex);
        const auto sel = static_cast<Selection>(selection);
        const auto in = static_cast<Interaction>(interaction);
        const QImage expected = renderSwapped(sel == Selection::Off ? sheets.unselected : sheets.selected, sel, in);
        const QImage actual = renderSelectable(sheets.selectable, sel, in);
        if (!qEnvironmentVariableIsEmpty("QK4_DUMP_DIR") && actual != expected) {
            const QString base = qEnvironmentVariable("QK4_DUMP_DIR") + '/' +
                                 QString::fromLatin1(QTest::currentDataTag()).replace('/', '_');
            expected.save(base + "_expected.png");
            actual.save(base + "_actual.png");
        }
        QCOMPARE(actual, expected);
    }

    // Guard for the case above: hover, press and selection must each change the picture, or the
    // comparison could pass on states the offscreen render never shows.
    void statesAreDistinguishable() {
        const Sheets popup = allSheets().at(0);
        const QImage idle = renderSwapped(popup.unselected, Selection::Off, Interaction::None);
        QVERIFY(renderSwapped(popup.unselected, Selection::Off, Interaction::Hover) != idle);
        QVERIFY(renderSwapped(popup.unselected, Selection::Off, Interaction::Pressed) != idle);
        QVERIFY(renderSwapped(popup.selected, Selection::Property, Interaction::None) != idle);
        QVERIFY(renderSwapped(popup.selected, Selection::Property, Interaction::Hover) !=
                renderSwapped(popup.selected, Selection::Property, Interaction::None));
    }

    // setSelected() takes effect at once, both ways, on a button that is already polished.
    void setSelectedRestylesAPolishedButton() {
        const Sheets popup = allSheets().at(0);
        QPushButton button(QStringLiteral("USB"));
        button.setFixedSize(K4Styles::Dimensions::MenuBarButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
        button.setStyleSheet(popup.selectable);
        const QImage unselected = render(button);
        K4Styles::setSelected(&button, true);
        QCOMPARE(render(button), renderSwapped(popup.selected, Selection::Property, Interaction::None));
        K4Styles::setSelected(&button, false);
        QCOMPARE(render(button), unselected);
        QCOMPARE(button.styleSheet(), popup.selectable);
    }
};

QTEST_MAIN(ButtonSheetsTests)
#include "test_buttonsheets.moc"

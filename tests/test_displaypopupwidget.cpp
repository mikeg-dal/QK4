#include "ui/popups/displaypopupwidget.h"
#include "ui/widgets/displaymenubutton.h"

#include <QSignalSpy>
#include <QTest>

class TestDisplayPopupWidget : public QObject {
    Q_OBJECT

private:
    DisplayMenuButton *cursorButton(DisplayPopupWidget &popup) {
        const auto buttons = popup.findChildren<DisplayMenuButton *>();
        return buttons.size() == 7 ? buttons.at(6) : nullptr;
    }

private slots:
    void vfoBCursorRightClickTurnsOffModeBackOn() {
        DisplayPopupWidget popup;
        popup.setVfoBCursor(0);
        QSignalSpy spy(&popup, &DisplayPopupWidget::catCommandRequested);
        QSignalSpy modeSpy(&popup, &DisplayPopupWidget::cursorModeChanged);

        auto *button = cursorButton(popup);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::RightButton);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("#VFB1;"));
        QCOMPARE(modeSpy.count(), 1);
        QCOMPARE(modeSpy.at(0).at(0).toBool(), true);
        QCOMPARE(modeSpy.at(0).at(1).toInt(), 1);
    }

    void vfoBCursorRightClickTurnsVisibleModeOff() {
        DisplayPopupWidget popup;
        popup.setVfoBCursor(1);
        QSignalSpy spy(&popup, &DisplayPopupWidget::catCommandRequested);

        auto *button = cursorButton(popup);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::RightButton);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("#VFB0;"));
    }

    void vfoBCursorRightClickUpdatesOptimistically() {
        DisplayPopupWidget popup;
        popup.setVfoBCursor(0);
        QSignalSpy spy(&popup, &DisplayPopupWidget::catCommandRequested);
        auto *button = cursorButton(popup);
        QVERIFY(button);

        QTest::mouseClick(button, Qt::RightButton);
        QTest::mouseClick(button, Qt::RightButton);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QString("#VFB1;"));
        QCOMPARE(spy.at(1).at(0).toString(), QString("#VFB0;"));
    }
};

QTEST_MAIN(TestDisplayPopupWidget)
#include "test_displaypopupwidget.moc"

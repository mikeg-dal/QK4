// The RCL / STO list overlay against a real MemoryModel, offscreen.
//
// The shape of these cases follows test_menuoverlay.cpp: the model is cleared on every disconnect
// and the rows outlive it, so the rows hold channel numbers rather than pointers into the model.
// That is #124 — a menu row holding a MenuItem * crashed on reconnect — and this list would fail
// the same way.

#include "models/memorymodel.h"
#include "ui/overlays/memoryoverlay.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

const char *kChannel1 = "MCDF001,,2.8,1.5,14.227,USB,14.221,50,100,,,,,,,;";
const char *kChannel21 = "MCDF021,*60m Ch1,,0,5.3305,USB,,,,,0,5.332,CW,,,;";

// The row for a channel, found by the number its name label starts with.
MemoryRowWidget *rowFor(MemoryOverlayWidget *overlay, int channel) {
    for (MemoryRowWidget *row : overlay->findChildren<MemoryRowWidget *>()) {
        if (row->channel() == channel)
            return row;
    }
    return nullptr;
}

QStringList labelTexts(MemoryRowWidget *row) {
    QStringList out;
    for (QLabel *label : row->findChildren<QLabel *>())
        out.append(label->text());
    return out;
}

QPushButton *buttonWithText(QWidget *parent, const QString &text) {
    for (QPushButton *b : parent->findChildren<QPushButton *>()) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

} // namespace

class MemoryOverlayTests : public QObject {
    Q_OBJECT

private slots:
    void everyChannelHasARowEvenWhenTheRadioHasSaidNothing() {
        // The radio lists only non-blank channels, but STO is how an empty one is filled: the free
        // slots have to be on screen, showing their number alone, as the radio's own list does.
        MemoryModel model;
        MemoryOverlayWidget overlay(&model);
        QCOMPARE(overlay.findChildren<MemoryRowWidget *>().size(), 200);

        MemoryRowWidget *blank = rowFor(&overlay, 2);
        QVERIFY(blank);
        QVERIFY(labelTexts(blank).contains("002:"));
    }

    void aChannelsRowShowsBothReceivers() {
        MemoryModel model;
        MemoryOverlayWidget overlay(&model);
        model.parseMCDF(kChannel1);
        model.parseMCDF(kChannel21);

        // Unnamed: the number alone in the name column, frequency and mode beside it.
        const QStringList ch1 = labelTexts(rowFor(&overlay, 1));
        QVERIFY(ch1.contains("001:"));
        QVERIFY(ch1.contains("A:  14.227.0  USB"));

        // Named, and with VFO B set - the K4 groups the digits this way.
        const QStringList ch21 = labelTexts(rowFor(&overlay, 21));
        QVERIFY(ch21.contains("021: *60m Ch1"));
        QVERIFY(ch21.contains("A:  5.330.5  USB"));
        QVERIFY(ch21.contains("B:  5.332.0  CW"));
    }

    void frequenciesAreGroupedTheWayTheRadioShowsThem() {
        QCOMPARE(MemoryRowWidget::formatFrequency(14227000), QString("14.227.0"));
        QCOMPARE(MemoryRowWidget::formatFrequency(5330500), QString("5.330.5"));
        QCOMPARE(MemoryRowWidget::formatFrequency(5332000), QString("5.332.0"));
        QCOMPARE(MemoryRowWidget::formatFrequency(7000000), QString("7.000.0"));
        QVERIFY(MemoryRowWidget::formatFrequency(0).isEmpty());
    }

    void theModeDecidesTheTitleAndTheKey() {
        MemoryModel model;
        MemoryOverlayWidget overlay(&model);

        overlay.showForMode(MemoryOverlayWidget::Recall);
        QCOMPARE(overlay.mode(), MemoryOverlayWidget::Recall);
        QVERIFY(buttonWithText(&overlay, "RCL"));
        QVERIFY(!buttonWithText(&overlay, "STO"));

        overlay.showForMode(MemoryOverlayWidget::Store);
        QVERIFY(buttonWithText(&overlay, "STO"));
        QVERIFY(!buttonWithText(&overlay, "RCL"));
        overlay.hide();
    }

    void theActionKeyActsOnTheSelectedChannelAndCloses() {
        MemoryModel model;
        model.parseMCDF(kChannel21);
        MemoryOverlayWidget overlay(&model);

        QSignalSpy recalls(&overlay, &MemoryOverlayWidget::recallRequested);
        QSignalSpy stores(&overlay, &MemoryOverlayWidget::storeRequested);

        overlay.showForMode(MemoryOverlayWidget::Recall);
        QTest::mouseClick(rowFor(&overlay, 21), Qt::LeftButton);
        QCOMPARE(overlay.selectedChannel(), 21);
        QVERIFY(rowFor(&overlay, 21)->isSelected());
        QVERIFY(!rowFor(&overlay, 1)->isSelected()); // one row at a time

        buttonWithText(&overlay, "RCL")->click();
        QCOMPARE(recalls.count(), 1);
        QCOMPARE(recalls.at(0).at(0).toInt(), 21);
        QCOMPARE(stores.count(), 0);
        QVERIFY(!overlay.isVisible()); // acting closes the list, as the radio's does

        overlay.showForMode(MemoryOverlayWidget::Store);
        buttonWithText(&overlay, "STO")->click();
        QCOMPARE(stores.count(), 1);
        QCOMPARE(stores.at(0).at(0).toInt(), 21); // the selection survived the mode change
        QCOMPARE(recalls.count(), 1);
    }

    void theListOpensWhereTheRadioIsAndFollowsIt() {
        MemoryModel model;
        MemoryOverlayWidget overlay(&model);
        model.parseSelection("MC002;");
        overlay.showForMode(MemoryOverlayWidget::Recall);
        QCOMPARE(overlay.selectedChannel(), 2);

        // Another client, or the radio's front panel, moving the channel moves the list too.
        model.parseSelection("MCNU021;");
        QCOMPARE(overlay.selectedChannel(), 21);
        overlay.hide();
    }

    void arrowsAndEscapeWork() {
        MemoryModel model;
        MemoryOverlayWidget overlay(&model);
        overlay.showForMode(MemoryOverlayWidget::Recall);
        overlay.setFocus();

        const int start = overlay.selectedChannel();
        QTest::keyClick(&overlay, Qt::Key_Down);
        QCOMPARE(overlay.selectedChannel(), start + 1);
        QTest::keyClick(&overlay, Qt::Key_Up);
        QCOMPARE(overlay.selectedChannel(), start);
        QTest::keyClick(&overlay, Qt::Key_Up); // already at the first channel: clamped, not wrapped
        QCOMPARE(overlay.selectedChannel(), start);
        QTest::keyClick(&overlay, Qt::Key_PageDown);
        QCOMPARE(overlay.selectedChannel(), start + 10);

        QTest::keyClick(&overlay, Qt::Key_Escape);
        QVERIFY(!overlay.isVisible());
    }

    void aDisconnectEmptiesTheRowsWithoutTouchingFreedChannels() {
        // The reconnect path: clear the model as a disconnect does, then refill it. A row reading
        // a freed MemoryChannel here is the ASan heap-use-after-free that #124 was.
        MemoryModel model;
        model.parseMCDF(kChannel1);
        model.parseMCDF(kChannel21);
        MemoryOverlayWidget overlay(&model);
        overlay.showForMode(MemoryOverlayWidget::Recall);
        QVERIFY(labelTexts(rowFor(&overlay, 21)).contains("021: *60m Ch1"));

        model.clear();
        QVERIFY(labelTexts(rowFor(&overlay, 21)).contains("021:"));
        QVERIFY(!labelTexts(rowFor(&overlay, 21)).contains("021: *60m Ch1"));

        // Reconnect: the same overlay refills from the new dump without being reopened.
        model.parseMCDF(kChannel21);
        QVERIFY(labelTexts(rowFor(&overlay, 21)).contains("021: *60m Ch1"));
        overlay.hide();
    }
};

QTEST_MAIN(MemoryOverlayTests)
#include "test_memoryoverlay.moc"

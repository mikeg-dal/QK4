// The memory UI against a real MemoryModel, offscreen: the RCL / STO list overlay and the band
// popup's MEM and GEN pages.
//
// The shape of these cases follows test_menuoverlay.cpp: the model is cleared on every disconnect
// and the rows outlive it, so the rows hold channel numbers rather than pointers into the model.
// That is #124 — a menu row holding a MenuItem * crashed on reconnect — and this list would fail
// the same way.

#include "models/memorymodel.h"
#include "models/radiostate.h"
#include "ui/overlays/memoryoverlay.h"
#include "ui/popups/bandpopupwidget.h"

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
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

    // --- The band popup's MEM and GEN pages ---------------------------------------------------

    void memAndGenPagesShowTheMemoriesColumnMajor() {
        MemoryModel model;
        model.parseMCDF(kChannel1);  // 14.227, in a ham band
        model.parseMCDF(kChannel21); // 5.3305, 60 m - also a ham band
        model.parseMCDF("MCDF022,*60m Ch2,,0,5.3465,USB,,,,,0,5.348,CW,,,;");
        model.parseMCDF("MCDF030,,2.8,1.5,9.58,AM,9.58,50,100,,,,,,,;"); // shortwave: a GEN entry

        BandPopupWidget popup;
        popup.setMemoryModel(&model);

        // The band grid is showing until MEM is tapped.
        QVERIFY(buttonWithText(&popup, "14"));
        buttonWithText(&popup, "MEM")->click();
        QTest::qWait(1); // setMode is queued: the tap deletes the button that is delivering it
        QCoreApplication::processEvents();

        // The twelve cells replace the band labels; MEM stays lit with HF under it.
        QVERIFY(!buttonWithText(&popup, "14"));
        QVERIFY(buttonWithText(&popup, "MEM"));
        QVERIFY(buttonWithText(&popup, "HF"));
        QVERIFY(buttonWithText(&popup, "#001\n14.227"));
        QVERIFY(buttonWithText(&popup, "*60m Ch1\n5.3305"));
        QVERIFY(buttonWithText(&popup, "*60m Ch2\n5.3465"));
        QVERIFY(!buttonWithText(&popup, "#030\n9.58")); // that one belongs to GEN

        // Column-major: #001 top-left, the next memory directly below it. The layout has to have
        // run for the cells to have real positions.
        popup.adjustSize();
        popup.layout()->activate();
        QPushButton *first = buttonWithText(&popup, "#001\n14.227");
        QPushButton *second = buttonWithText(&popup, "*60m Ch1\n5.3305");
        QVERIFY(second->y() > first->y());
        QCOMPARE(second->x(), first->x());

        // Tapping a cell recalls that channel.
        QSignalSpy recalls(&popup, &BandPopupWidget::memoryRecallRequested);
        first->click();
        QCOMPARE(recalls.count(), 1);
        QCOMPARE(recalls.at(0).at(0).toInt(), 1);
    }

    void theBandPopupOpensOnThePageHoldingTheCurrentBand() {
        // On a transverter band the popup showed the HF keys with the PREVIOUS band still lit -
        // it claimed 20 m while the radio sat on XVTR5 (BN15).
        RadioState radio;
        radio.parseCATCommand("XVN5;");
        radio.parseCATCommand("XVM1;");
        radio.parseCATCommand("XVR10368;");
        BandPopupWidget popup(&radio);

        popup.setSelectedBandByNumber(5); // 20 m
        QCoreApplication::processEvents();
        QPushButton *twenty = buttonWithText(&popup, "14");
        QVERIFY(twenty && twenty->property("selected").toBool());

        popup.setSelectedBandByNumber(15); // XVTR5
        QCoreApplication::processEvents();
        // The transverter page is showing, its cell is lit, and 20 m is not on screen claiming to
        // be the current band.
        QVERIFY(!buttonWithText(&popup, "14"));
        QPushButton *xvtr5 = buttonWithText(&popup, "XVTR5\n10368");
        QVERIFY(xvtr5 && xvtr5->property("selected").toBool());

        popup.setSelectedBandByNumber(5); // back to an HF band
        QCoreApplication::processEvents();
        twenty = buttonWithText(&popup, "14");
        QVERIFY(twenty && twenty->property("selected").toBool());
    }

    void genPageHoldsWhatIsOutsideTheHamBands() {
        MemoryModel model;
        model.parseMCDF(kChannel1);
        model.parseMCDF("MCDF030,,2.8,1.5,9.58,AM,9.58,50,100,,,,,,,;");

        BandPopupWidget popup;
        popup.setMemoryModel(&model);
        buttonWithText(&popup, "GEN")->click();
        QCoreApplication::processEvents();

        QVERIFY(buttonWithText(&popup, "#030\n9.58"));
        QVERIFY(!buttonWithText(&popup, "#001\n14.227"));
        QVERIFY(buttonWithText(&popup, "GEN"));
        QVERIFY(buttonWithText(&popup, "HF"));

        // HF flips back to the bands.
        buttonWithText(&popup, "HF")->click();
        QCoreApplication::processEvents();
        QVERIFY(buttonWithText(&popup, "14"));
    }

    void aPageWithNoMemoriesShowsEmptyCells() {
        MemoryModel model; // the radio has reported nothing yet
        BandPopupWidget popup;
        popup.setMemoryModel(&model);
        buttonWithText(&popup, "MEM")->click();
        QCoreApplication::processEvents();

        int empty = 0;
        for (QPushButton *b : popup.findChildren<QPushButton *>()) {
            if (b->text() == "--") {
                QVERIFY(!b->isEnabled()); // nothing to recall
                ++empty;
            }
        }
        QCOMPARE(empty, 12);
    }

    void aSecondClickOnAStoredChannelNamesIt() {
        MemoryModel model;
        model.parseMCDF(kChannel1); // in use, unnamed
        MemoryOverlayWidget overlay(&model);
        overlay.showForMode(MemoryOverlayWidget::Recall);

        QSignalSpy renames(&overlay, &MemoryOverlayWidget::renameRequested);
        MemoryRowWidget *row = rowFor(&overlay, 1);
        MemoryRowWidget *blank = rowFor(&overlay, 2);

        // The layout has to have run for the columns to have real geometry.
        overlay.resize(1200, 500);
        overlay.layout()->activate();
        const QPoint inFrequencies(overlay.width() - 300, row->height() / 2);
        const QPoint inNameCell(20, row->height() / 2);

        // A click in the frequency columns selects and must NOT open an editor.
        QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, inFrequencies);
        QCOMPARE(overlay.selectedChannel(), 1);
        QVERIFY(!row->isRenaming());

        // A click in the name cell opens it.
        QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, inNameCell);
        QVERIFY(row->isRenaming());
        QLineEdit *editor = row->findChild<QLineEdit *>();
        QVERIFY(editor);

        editor->setText("Net");
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(renames.count(), 1);
        QCOMPARE(renames.at(0).at(0).toInt(), 1);
        QCOMPARE(renames.at(0).at(1).toString(), QString("Net"));
        QVERIFY(!row->isRenaming());

        // A blank channel cannot be named: there is nothing to store the name against.
        QTest::mouseClick(blank, Qt::LeftButton, Qt::NoModifier, inNameCell);
        QVERIFY(!blank->isRenaming());
        QCOMPARE(renames.count(), 1);
        overlay.hide();
    }

    void searchFiltersTheListAndClearsOnClose() {
        MemoryModel model;
        model.parseMCDF(kChannel1);  // #001, 14.227
        model.parseMCDF(kChannel21); // *60m Ch1, 5.3305
        MemoryOverlayWidget overlay(&model);
        overlay.showForMode(MemoryOverlayWidget::Recall);

        QPushButton *search = buttonWithText(&overlay, QString::fromUtf8("\xF0\x9F\x94\x8D"));
        QVERIFY(search);
        QLineEdit *field = overlay.findChild<QLineEdit *>();
        QVERIFY(field && !field->isVisible());

        search->click();
        QVERIFY(field->isVisible());

        // By name.
        field->setText("60m");
        QVERIFY(rowFor(&overlay, 21)->isVisibleTo(&overlay));
        QVERIFY(!rowFor(&overlay, 1)->isVisibleTo(&overlay));
        QVERIFY(!rowFor(&overlay, 2)->isVisibleTo(&overlay)); // a blank channel matches nothing

        // By frequency, and by channel number.
        field->setText("14.227");
        QVERIFY(rowFor(&overlay, 1)->isVisibleTo(&overlay));
        QVERIFY(!rowFor(&overlay, 21)->isVisibleTo(&overlay));
        field->setText("021");
        QVERIFY(rowFor(&overlay, 21)->isVisibleTo(&overlay));

        // Closing the search brings the whole list back.
        search->click();
        QVERIFY(!field->isVisible());
        QVERIFY(rowFor(&overlay, 1)->isVisibleTo(&overlay));
        QVERIFY(rowFor(&overlay, 2)->isVisibleTo(&overlay));
        overlay.hide();
    }

    void theNavigationKeysStepTheSelection() {
        MemoryModel model;
        MemoryOverlayWidget overlay(&model);
        overlay.showForMode(MemoryOverlayWidget::Recall);

        QPushButton *down = buttonWithText(&overlay, QString::fromUtf8("\xE2\x96\xBC"));
        QPushButton *up = buttonWithText(&overlay, QString::fromUtf8("\xE2\x96\xB2"));
        QVERIFY(down && up);

        const int start = overlay.selectedChannel();
        down->click();
        QCOMPARE(overlay.selectedChannel(), start + 1);
        up->click();
        QCOMPARE(overlay.selectedChannel(), start);
        overlay.hide();
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

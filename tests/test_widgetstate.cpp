// What a widget shows must match the state behind it. Each case reproduces a review finding where a
// widget's screen drifted from its own model: typed frequency digits overwritten by the radio, a DLY
// label replaced after the PWR/DLY swap, an NB filter shown as NONE, a refused LINE OUT click left
// checked, a reused confirm popup that stopped reporting cancels, and a macro editor that Tab closed
// behind the dialog's back. Rendered offscreen.

#include "ui/dialogs/macrodialog.h"
#include "ui/dialogs/textdecodewindow.h"
#include "ui/overlays/baloverlay.h"
#include "ui/overlays/monoverlay.h"
#include "ui/overlays/dxspotoverlay.h"
#include <QDateTime>
#include <QSet>
#include "ui/popups/confirmpopup.h"
#include "ui/popups/displaypopupwidget.h"
#include "ui/styling/k4glyphs.h"
#include "ui/styling/k4styles.h"
#include "ui/popups/lineoutpopup.h"
#include "ui/widgets/controlgroupwidget.h"
#include "ui/widgets/displaymenubutton.h"
#include "ui/widgets/dualcontrolbutton.h"
#include "ui/widgets/featuremenubar.h"
#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/widgets/sidecontrolpanel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QtMath>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class WidgetStateTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    static QPushButton *buttonWithText(QWidget *parent, const QString &text) {
        for (QPushButton *b : parent->findChildren<QPushButton *>()) {
            if (b->text() == text)
                return b;
        }
        return nullptr;
    }

private slots:
    void glyphsRenderAtTheDevicePixelRatio() {
        // The glyphs were drawn into a 1x pixmap and stretched, so at 125% on a Retina screen (2.5x) the
        // power, thermometer and lightning icons were blurred. Each must come back at the ratio asked for.
        const QColor green(Qt::green);
        for (const K4Glyphs::Glyph &glyph : {K4Glyphs::power(22), K4Glyphs::thermometer(16), K4Glyphs::lightning(16)}) {
            const QIcon icon = glyph(green);
            for (const qreal dpr : {1.0, 2.0, 2.5}) {
                const QPixmap pm = icon.pixmap(QSize(20, 20), dpr);
                QCOMPARE(pm.devicePixelRatio(), dpr);
                QCOMPARE(pm.size(), QSize(qCeil(20 * dpr), qCeil(20 * dpr)));
                const QImage img = pm.toImage();
                int opaque = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        opaque += qAlpha(img.pixel(x, y)) > 128;
                QVERIFY2(opaque > 0, "glyph drew nothing at this ratio");
            }
        }
    }

    void initTestCase() {
        // Macro and side-panel code read saved settings; keep them off the real preferences.
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    }

    // --- FrequencyDisplayWidget ---

    void frequencyUpdateDuringEntryKeepsTypedDigits() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        w.beginEntry();
        QVERIFY(w.isEditing());

        QTest::keyClicks(&w, "14");
        w.setFrequency("7025000"); // radio report arrives mid-entry
        QCOMPARE(w.frequency(), QString("14"));
        QCOMPARE(w.displayText(), QString("14"));
    }

    void frequencyEntrySendsTypedDigitsNotRadioUpdate() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        w.beginEntry();
        QTest::keyClicks(&w, "14074000");
        w.setFrequency("7025000");
        QTest::keyClick(&w, Qt::Key_Return);

        QCOMPARE(entered.count(), 1);
        // Always all 11 digits, in Hz: the K4 reads 1-5 digits as MHz or kHz (D12, FA).
        QCOMPARE(entered.at(0).at(0).toString(), QString("00014074000"));
    }

    void frequencyCancelShowsLatestRadioValue() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        w.beginEntry();
        QTest::keyClicks(&w, "999");
        w.setFrequency("7025000");
        QTest::keyClick(&w, Qt::Key_Escape);

        QVERIFY(!w.isEditing());
        QCOMPARE(w.displayText(), QString("7.025.000"));
    }

    // A locked VFO (LK / LK$) takes no typed frequency: FREQ ENT, with or without B SET, must not open
    // entry, and no keys reach it. Bench 2026-10-30: B SET + FREQ ENT accepted digits on a locked VFO B.
    void lockedVfoDoesNotOpenFrequencyEntry() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        w.setEntryLocked(true);
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        w.beginEntry();
        QVERIFY(!w.isEditing());
        QTest::keyClicks(&w, "14074000");
        QTest::keyClick(&w, Qt::Key_Return);
        QCOMPARE(entered.count(), 0);
        QCOMPARE(w.displayText(), QString("7.024.980"));

        w.setEntryLocked(false); // unlocking allows entry again
        w.beginEntry();
        QVERIFY(w.isEditing());
    }

    // Locking while an entry is open cancels it and shows the radio's frequency, typed digits gone.
    void lockingDuringEntryCancelsIt() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        w.beginEntry();
        QTest::keyClicks(&w, "999");
        w.setEntryLocked(true);
        QVERIFY(!w.isEditing());
        QCOMPARE(w.displayText(), QString("7.024.980"));
        QTest::keyClick(&w, Qt::Key_Return);
        QCOMPARE(entered.count(), 0);
    }

    void frequencyUpdateOutsideEntryShowsImmediately() {
        FrequencyDisplayWidget w;
        w.setFrequency("14.074.000");
        QCOMPARE(w.displayText(), QString("14.074.000"));
    }

    // 10 GHz needs 11 digits, the most the K4 takes. With 10 positions the radio's 10.368 GHz showed as
    // 368.000.000, and typing it sent 1.0368 GHz (the 11th key overwrote the last digit).
    void tenGigahertzDisplaysAndEnters() {
        FrequencyDisplayWidget w;
        w.setFrequency("10368000000");
        QCOMPARE(w.displayText(), QString("10.368.000.000"));
        w.show();
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        w.beginEntry();
        QCOMPARE(w.displayText(), QString());  // opens empty
        QTest::keyClicks(&w, "0103680000009"); // a leading zero is ignored, a 12th digit too
        QCOMPARE(w.displayText(), QString("10.368.000.000"));
        QTest::keyClick(&w, Qt::Key_Return);
        QCOMPARE(entered.size(), 1);
        QCOMPARE(entered.at(0).at(0).toString(), QString("10368000000"));
        QCOMPARE(w.displayText(), QString("10.368.000.000"));
    }

    // Typing builds from the right, Backspace takes the last digit back, Enter on nothing just closes.
    void frequencyEntryTypesInFromTheRight() {
        FrequencyDisplayWidget w;
        w.setFrequency("7024980");
        w.show();
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        w.beginEntry();
        QTest::keyClicks(&w, "1407");
        QCOMPARE(w.displayText(), QString("1.407"));
        QTest::keyClicks(&w, "4000");
        QCOMPARE(w.displayText(), QString("14.074.000"));
        QTest::keyClick(&w, Qt::Key_Backspace);
        QCOMPARE(w.displayText(), QString("1.407.400"));
        for (int i = 0; i < 7; ++i)
            QTest::keyClick(&w, Qt::Key_Backspace);
        QCOMPARE(w.displayText(), QString());
        QTest::keyClick(&w, Qt::Key_Return);
        QVERIFY(!w.isEditing());
        QCOMPARE(entered.size(), 0);
        QCOMPARE(w.displayText(), QString("7.024.980"));
    }

    // An 11-digit frequency fits the widget's usual width by setting the characters closer: the last
    // digit (1 Hz) is still inside the widget and still clickable as a tuning rate, and the widget did
    // not grow. A frequency that fits keeps its natural spacing.
    void elevenDigitsFitByTighterSpacing() {
        FrequencyDisplayWidget w;
        const int width = w.minimumWidth();
        w.resize(width, w.height());
        w.setFrequency("14074000");
        w.show();
        QSignalSpy rate(&w, &FrequencyDisplayWidget::tuningDigitClicked);
        const QFontMetrics fm(K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeDisplay));
        const int natural = fm.horizontalAdvance(QStringLiteral("14.074.000"));
        QTest::mouseClick(&w, Qt::LeftButton, {}, QPoint(natural - fm.horizontalAdvance('0') / 2, w.height() / 2));
        QCOMPARE(rate.size(), 1);
        QCOMPARE(rate.at(0).at(0).toInt(), 0); // the 1 Hz digit, at its natural place
        w.setFrequency("10368000000");
        QCOMPARE(w.minimumWidth(), width);
        QTest::mouseClick(&w, Qt::LeftButton, {}, QPoint(width - 4, w.height() / 2));
        QCOMPARE(rate.size(), 2);
        QCOMPARE(rate.at(1).at(0).toInt(), 0); // the 1 Hz digit, pulled inside the widget
    }

    // --- SideControlPanel PWR/DLY ---

    void powerUpdateAfterSwapKeepsDelayLabel() {
        QWidget host;
        auto *panel = new SideControlPanel(&host);
        host.show();

        DualControlButton *pwr = nullptr;
        for (auto *b : panel->findChildren<DualControlButton *>()) {
            if (b->primaryLabel() == "PWR")
                pwr = b;
        }
        QVERIFY(pwr);

        // First click activates the button, the second swaps PWR and DLY.
        QTest::mouseClick(pwr, Qt::LeftButton);
        pwr->setShowIndicator(true);
        QTest::mouseClick(pwr, Qt::LeftButton);
        QCOMPARE(pwr->primaryLabel(), QString("DLY"));

        panel->setPower(50.0, LevelsState::PowerRange::Qro);
        QCOMPARE(pwr->primaryLabel(), QString("DLY"));
        QCOMPARE(pwr->alternateLabel(), QString("PWR"));
        QCOMPARE(pwr->alternateValue(), QString("50"));

        panel->setPower(1.5, LevelsState::PowerRange::Xvtr);
        QCOMPARE(pwr->primaryLabel(), QString("DLY"));
        QCOMPARE(pwr->alternateLabel(), QString("mW"));
    }

    // --- SideControlPanel MON / BAL overlays ---

    // MON and BAL close like every popup (PopupDismissal): a press anywhere else, Escape, the window
    // resizing. They used to close only on their own key. Pressing that key again must still close
    // them: the press closes the overlay by click-away before the key's clicked() fires, which would
    // otherwise open it again.
    void monBalOverlaysCloseOnAClickElsewhere() {
        QWidget host;
        host.resize(400, 900);
        auto *panel = new SideControlPanel(&host);
        panel->move(0, 0);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        auto *mon = panel->findChild<MonOverlay *>();
        auto *bal = panel->findChild<BalOverlay *>();
        QPushButton *monKey = buttonWithText(panel, "MON");
        QPushButton *balKey = buttonWithText(panel, "BAL");
        QPushButton *normKey = buttonWithText(panel, "NORM");
        QVERIFY(mon && bal && monKey && balKey && normKey);
        const auto nextGesture = []() { QTest::qWait(300); }; // past the reopen guard

        // Its own key opens it, and pressing the key again closes it rather than reopening it.
        QTest::mouseClick(monKey, Qt::LeftButton);
        QVERIFY(mon->isVisible());
        QTest::mouseClick(monKey, Qt::LeftButton);
        QTest::qWait(50);
        QVERIFY2(!mon->isVisible(), "the second press on MON reopened the overlay");

        // A press inside keeps it (the wheel adjusts it); a press on any other key closes it.
        nextGesture();
        QTest::mouseClick(monKey, Qt::LeftButton);
        QVERIFY(mon->isVisible());
        QTest::mouseClick(mon, Qt::LeftButton, {}, mon->rect().center());
        QVERIFY(mon->isVisible());
        QTest::mouseClick(normKey, Qt::LeftButton);
        QVERIFY2(!mon->isVisible(), "a press outside MON left it open");

        // Escape anywhere in the window.
        nextGesture();
        QTest::mouseClick(balKey, Qt::LeftButton);
        QVERIFY(bal->isVisible());
        QTest::keyClick(&host, Qt::Key_Escape);
        QVERIFY(!bal->isVisible());

        // The other overlay's key switches from one to the other in a single press.
        nextGesture();
        QTest::mouseClick(balKey, Qt::LeftButton);
        QVERIFY(bal->isVisible());
        QTest::mouseClick(monKey, Qt::LeftButton);
        QVERIFY(!bal->isVisible());
        QVERIFY(mon->isVisible());

        // The window resizing may move the keys it covers.
        host.resize(420, 900);
        QTRY_VERIFY(!mon->isVisible());
    }

    // --- FeatureMenuBar NB filter ---

    void nbLevelOpensWithCurrentFilter() {
        QWidget host;
        auto *bar = new FeatureMenuBar(&host);
        bar->setNbFilter(2);
        bar->showForFeature(FeatureMenuBar::NbLevel, &host);
        QVERIFY(buttonWithText(bar, "FILTER\nWIDE"));
        QVERIFY(!buttonWithText(bar, "FILTER\nNONE"));
    }

    // --- LineOutPopupWidget ---

    void lineOutRightRefusedWhileLinked() {
        LineOutPopupWidget popup;
        QPushButton *left = buttonWithText(&popup, "LEFT");
        QPushButton *right = buttonWithText(&popup, "RIGHT");
        QVERIFY(left && right);

        popup.setRightEqualsLeft(true);
        right->click(); // refused: R=L forces the left channel
        QVERIFY(left->isChecked());
        QVERIFY(!right->isChecked());

        popup.setRightEqualsLeft(false);
        QVERIFY(left->isChecked());
        QVERIFY(!right->isChecked());

        right->click();
        QVERIFY(!left->isChecked());
        QVERIFY(right->isChecked());
    }

    void lineOutLinkButtonFollowsState() {
        LineOutPopupWidget popup;
        QPushButton *link = buttonWithText(&popup, "RIGHT\n=LEFT");
        QVERIFY(link);
        link->click();
        QVERIFY(popup.rightEqualsLeft());
        QVERIFY(link->isChecked());
        link->click();
        QVERIFY(!popup.rightEqualsLeft());
        QVERIFY(!link->isChecked());
    }

    // --- DisplayPopupWidget ---

    void displayNbKeyAsksForTheNextPanadapterNbMode() {
        // The NB key sent "NB;" - a GET of the receiver noise blanker - so tapping it changed nothing.
        // It must ask for the next panadapter (DDC, #NB$) mode, the one its control page shows.
        DisplayPopupWidget popup;
        DisplayMenuButton *nbKey = nullptr;
        for (DisplayMenuButton *b : popup.findChildren<DisplayMenuButton *>()) {
            if (b->alternateText() == QStringLiteral("WTR CLRS"))
                nbKey = b;
        }
        QVERIFY(nbKey);
        QSignalSpy cycle(&popup, &DisplayPopupWidget::ddcNbModeCycleRequested);
        QSignalSpy cat(&popup, &DisplayPopupWidget::catCommandRequested);
        QTest::mouseClick(nbKey, Qt::LeftButton);
        QCOMPARE(cycle.count(), 1);
        QCOMPARE(cat.count(), 0);
    }

    // WTR CLRS (right-click on NB / WTR CLRS, as on the K4) opens a page for the radio's waterfall
    // colour range (#WBS): "--" until the radio reports, then its value as the K4 shows it (0.5-3.0),
    // and +/- ask to step it.
    void displayWtrClrsPageStepsTheColourRange() {
        DisplayPopupWidget popup;
        DisplayMenuButton *nbKey = nullptr;
        for (DisplayMenuButton *b : popup.findChildren<DisplayMenuButton *>()) {
            if (b->alternateText() == QStringLiteral("WTR CLRS"))
                nbKey = b;
        }
        QVERIFY(nbKey);
        ControlGroupWidget *wtrClrs = nullptr;
        for (ControlGroupWidget *g : popup.findChildren<ControlGroupWidget *>()) {
            if (g->label() == QStringLiteral("WTR CLRS"))
                wtrClrs = g;
        }
        QVERIFY(wtrClrs);
        QCOMPARE(wtrClrs->value(), QStringLiteral("--"));
        popup.setWaterfallColorRange(14);
        QCOMPARE(wtrClrs->value(), QStringLiteral("1.4")); // shown as the K4 shows it: #WBS / 10

        QTest::mouseClick(nbKey, Qt::RightButton);
        auto *stack = popup.findChild<QStackedWidget *>();
        QVERIFY(stack && stack->currentWidget()->isAncestorOf(wtrClrs));

        QSignalSpy up(&popup, &DisplayPopupWidget::waterfallColorRangeIncrementRequested);
        QSignalSpy down(&popup, &DisplayPopupWidget::waterfallColorRangeDecrementRequested);
        emit wtrClrs->incrementClicked();
        emit wtrClrs->decrementClicked();
        QCOMPARE(up.count(), 1);
        QCOMPARE(down.count(), 1);
    }

    // --- ConfirmPopup ---

    void confirmPopupReportsCancelAfterEarlierConfirm() {
        ConfirmPopup popup("TITLE", "Body", "YES", "NO");
        QSignalSpy confirmed(&popup, &ConfirmPopup::confirmed);
        QSignalSpy cancelled(&popup, &ConfirmPopup::cancelled);

        popup.show();
        buttonWithText(&popup, "YES")->click();
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(cancelled.count(), 0);

        popup.show();
        buttonWithText(&popup, "NO")->click();
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(cancelled.count(), 1);
    }

    // --- MacroDialog ---

    // Deselecting a macro row returns every column to its unselected look (the command column used to
    // stay white once a row had been selected).
    void macroRowDeselectRestoresEveryColumn() {
        MacroItemWidget row(QStringLiteral("PF1"), QStringLiteral("PF1"));
        row.setLabel(QStringLiteral("CQ"));
        row.setCommand(QStringLiteral("KY CQ TEST;"));
        row.resize(600, 40);
        row.show();
        QVERIFY(QTest::qWaitForWindowExposed(&row));
        const QImage unselected = row.grab().toImage();
        row.setSelected(true);
        QVERIFY(row.grab().toImage() != unselected);
        row.setSelected(false);
        QCOMPARE(row.grab().toImage(), unselected);
    }

    // --- TextDecodeWindow ---

    // In every mode the title row's keys sit side by side, each wide enough for its text, inside the
    // window, even when the window is asked to be smaller than they need. The window minimum used to be
    // a fixed 350 px, below the CW row's width, and the keys were drawn over each other.
    void textDecodeTitleRowFitsInEveryMode_data() {
        QTest::addColumn<int>("receiver");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("autoThreshold");
        QTest::addColumn<int>("dataRate");
        for (int rx : {int(TextDecodeWindow::MainRx), int(TextDecodeWindow::SubRx)}) {
            const char *rxName = rx == TextDecodeWindow::MainRx ? "main" : "sub";
            QTest::addRow("%s/cw-auto", rxName) << rx << int(TextDecodeWindow::ModeCW) << true << 0;
            QTest::addRow("%s/cw-manual", rxName) << rx << int(TextDecodeWindow::ModeCW) << false << 0;
            QTest::addRow("%s/rtty45", rxName) << rx << int(TextDecodeWindow::ModeFSK) << true << 0;
            QTest::addRow("%s/rtty75", rxName) << rx << int(TextDecodeWindow::ModeAFSK) << true << 1;
            QTest::addRow("%s/psk63", rxName) << rx << int(TextDecodeWindow::ModePSK) << true << 1;
            QTest::addRow("%s/data", rxName) << rx << int(TextDecodeWindow::ModeData) << true << 0;
        }
    }

    void textDecodeTitleRowFitsInEveryMode() {
        QFETCH(int, receiver);
        QFETCH(int, mode);
        QFETCH(bool, autoThreshold);
        QFETCH(int, dataRate);
        TextDecodeWindow window(static_cast<TextDecodeWindow::Receiver>(receiver));
        window.setOperatingMode(static_cast<TextDecodeWindow::OperatingMode>(mode));
        window.setAutoThreshold(autoThreshold);
        window.setDataRate(dataRate);
        window.resize(200, 100); // smaller than any mode needs
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QList<QWidget *> row;
        for (QWidget *child : window.findChildren<QWidget *>()) {
            if (child->isVisible() && (qobject_cast<QPushButton *>(child) || qobject_cast<QLabel *>(child)))
                row.append(child);
        }
        QVERIFY(row.size() >= 6);
        std::sort(row.begin(), row.end(), [](QWidget *a, QWidget *b) { return a->x() < b->x(); });
        for (int i = 0; i < row.size(); ++i) {
            QWidget *w = row.at(i);
            const QString text = qobject_cast<QPushButton *>(w) ? qobject_cast<QPushButton *>(w)->text()
                                                                : qobject_cast<QLabel *>(w)->text();
            QVERIFY2(w->width() >= w->sizeHint().width(), qPrintable(text + " is narrower than its text"));
            QVERIFY2(w->geometry().right() < window.width(), qPrintable(text + " is outside the window"));
            if (i > 0)
                QVERIFY2(row.at(i - 1)->geometry().right() < w->x(), qPrintable(text + " overlaps its neighbour"));
        }
    }

    void macroTabSwitchesFieldAndStaysInEditMode() {
        MacroDialog dialog;
        dialog.resize(800, 600);
        dialog.show();
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        QTest::keyClick(&dialog, Qt::Key_Return); // edit the first macro's label
        QPushButton *done = buttonWithText(&dialog, "DONE");
        QVERIFY(done);

        auto *labelEdit = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        QVERIFY(labelEdit);
        QCOMPARE(labelEdit->maxLength(), 12);

        QTest::keyClick(labelEdit, Qt::Key_Tab);
        QCOMPARE(done->text(), QString("DONE"));
        auto *commandEdit = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        QVERIFY(commandEdit);
        QCOMPARE(commandEdit->maxLength(), 64);
        QVERIFY(commandEdit->isVisible());
        QVERIFY(!labelEdit->isVisible());

        QTest::keyClick(commandEdit, Qt::Key_Backtab);
        QCOMPARE(done->text(), QString("DONE"));
        QCOMPARE(QApplication::focusWidget(), labelEdit);
        QVERIFY(labelEdit->isVisible());
        QVERIFY(!commandEdit->isVisible());
    }

    void spotLabelsKeepOutOfTheAmplitudeAxisColumn() {
        // A spot whose frequency falls under the dBm/S-unit labels used to be drawn on top of them.
        // Its label now starts just right of the reserved column, and still tunes to the spot.
        DxSpotOverlay overlay;
        overlay.resize(400, 200);
        overlay.setFrequencyRange(14'000'000, 10'000); // 14.000 MHz +/- 5 kHz across 400 px
        overlay.setLeftReservedPx(70);
        DxSpot spot;
        spot.spottedCall = QStringLiteral("EA1EYL");
        spot.frequencyHz = 13'995'500; // x = 20 px, inside the column
        overlay.setSpots({spot});

        QSignalSpy clicked(&overlay, &DxSpotOverlay::spotClicked);
        const int rowY = 200 / 2 - 8; // inside row 0, which sits just above the midpoint
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, QPoint(20, rowY));
        QCOMPARE(clicked.count(), 0);
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, QPoint(75, rowY));
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.at(0).at(0).toLongLong(), 13'995'500LL);
    }

    void busyFrequencyShowsItsNewestSpots() {
        // Seven spots 1 Hz apart share one pixel; five are shown, stacked a row each, plus "+2". The
        // five shown are the newest - here the lowest frequencies - not the five highest.
        DxSpotOverlay overlay;
        overlay.resize(400, 200);
        overlay.setFrequencyRange(14'000'000, 10'000);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVector<DxSpot> spots;
        for (int k = 0; k < 7; ++k) {
            DxSpot spot;
            spot.spottedCall = QStringLiteral("K%1AA").arg(k);
            spot.frequencyHz = 14'000'000 + k;
            spot.timestamp = now.addSecs(-60 * k); // k = 0 newest
            spots.append(spot);
        }
        overlay.setSpots(spots);

        QSignalSpy clicked(&overlay, &DxSpotOverlay::spotClicked);
        const int rowHeight = 11 + 4; // default spot font + 4, DxSpotOverlay::rowHeight()
        QSet<qint64> shown;
        for (int row = 0; row < 5; ++row) {
            QTest::mouseClick(&overlay, Qt::LeftButton, {}, QPoint(205, 200 / 2 - rowHeight / 2 - row * rowHeight));
            if (!clicked.isEmpty())
                shown.insert(clicked.takeFirst().at(0).toLongLong() - 14'000'000);
        }
        QCOMPARE(shown, (QSet<qint64>{0, 1, 2, 3, 4}));
    }
};

QTEST_MAIN(WidgetStateTests)
#include "test_widgetstate.moc"

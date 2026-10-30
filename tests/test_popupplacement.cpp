// Every popup opens directly above what opened it, centred on it, and never stays on screen once
// it should have gone. Run three times by CMake, at QT_SCALE_FACTOR 1, 1.5 and 2 on a 4K offscreen
// screen (1.5 is a 4K monitor at QK4's 150 % interface scale, where the bench saw an offset popup).
//
// The host window copies MainWindow's middle grid: a left panel the full height, the bottom menu
// bar beside it under the content and the right panel. That is the layout that put the bar's
// centre half a panel width right of its parent's — and of the main window's.

#include "controllers/antennaconfigcontroller.h"
#include "controllers/connectioncontroller.h"
#include "controllers/featuremenucontroller.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "ui/popups/antennacfgpopup.h"
#include "ui/popups/buttonrowpopup.h"
#include "ui/popups/confirmpopup.h"
#include "ui/popups/displaypopupwidget.h"
#include "ui/popups/k4popupbase.h"
#include "ui/popups/modepopupwidget.h"
#include "ui/popups/rxeqpopupwidget.h"
#include "ui/popups/voxpopup.h"
#include "ui/styling/k4constants.h"
#include "ui/widgets/bottommenubar.h"
#include "ui/widgets/featuremenubar.h"
#include "ui/widgets/nethealthwidget.h"
#include "ui/widgets/rightsidepanel.h"

#include <QApplication>
#include <QGridLayout>
#include <QPushButton>
#include <QScreen>
#include <QTest>

#include <memory>

// FeatureMenuController and AntennaConfigController only send CAT; nothing here is connected.
QStringList g_sentCat;
void ConnectionController::sendCAT(const QString &command) {
    g_sentCat.append(command);
}
bool ConnectionController::isConnected() const {
    return false;
}

namespace {

constexpr int Sm = K4Styles::Dimensions::ShadowMargin;

QRect globalRect(const QWidget *w) {
    return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
}

// The visible card: the popup's window less its transparent shadow margin.
QRect cardOf(const QWidget *popup) {
    return popup->geometry().marginsRemoved(QMargins(Sm, Sm, Sm, Sm));
}

QString describe(const QRect &r) {
    return QStringLiteral("(%1,%2 %3x%4)").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

// Centred on the anchor within a logical pixel, card bottom one shadow margin above its top.
bool centredDirectlyAbove(const QRect &card, const QRect &anchor, QString *why) {
    const double dx = (card.x() + card.width() / 2.0) - (anchor.x() + anchor.width() / 2.0);
    const int gap = anchor.y() - (card.y() + card.height());
    *why = QStringLiteral("card %1 anchor %2: centre off by %3 px, gap %4 px (want %5)")
               .arg(describe(card), describe(anchor))
               .arg(dx)
               .arg(gap)
               .arg(Sm);
    return qAbs(dx) <= 1.0 && qAbs(gap - Sm) <= 1;
}

#define VERIFY_CENTRED_ABOVE(card, anchor)                                                                             \
    do {                                                                                                               \
        QString why;                                                                                                   \
        const bool ok = centredDirectlyAbove((card), (anchor), &why);                                                  \
        QVERIFY2(ok, qPrintable(why));                                                                                 \
    } while (false)

// MainWindow's middle section, reduced to the widgets that decide where a popup goes.
struct Host {
    std::unique_ptr<NetworkMetrics> metrics = std::make_unique<NetworkMetrics>(); // outlives the LED
    QWidget window;
    QWidget *leftPanel;
    RightSidePanel *rightPanel;
    BottomMenuBar *bar;
    NetHealthWidget *led;
    QPushButton *powerKey;

    explicit Host(const QPoint &pos = QPoint(333, 211)) {
        window.setWindowFlag(Qt::FramelessWindowHint);
        auto *outer = new QVBoxLayout(&window);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto *statusBar = new QWidget(&window);
        auto *statusLayout = new QHBoxLayout(statusBar);
        statusLayout->addStretch();
        led = new NetHealthWidget(metrics.get(), statusBar);
        statusLayout->addWidget(led);
        powerKey = new QPushButton(QStringLiteral("PWR"), statusBar);
        statusLayout->addWidget(powerKey);
        outer->addWidget(statusBar);

        auto *middle = new QWidget(&window);
        auto *grid = new QGridLayout(middle);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(0);
        leftPanel = new QWidget(middle);
        leftPanel->setFixedWidth(K4Styles::Dimensions::LeftSidePanelWidth);
        grid->addWidget(leftPanel, 0, 0, 2, 1);
        grid->addWidget(new QWidget(middle), 0, 1);
        grid->setColumnStretch(1, 1);
        grid->setRowStretch(0, 1);
        rightPanel = new RightSidePanel(middle);
        grid->addWidget(rightPanel, 0, 2);
        bar = new BottomMenuBar(middle);
        grid->addWidget(bar, 1, 1, 1, 2);
        outer->addWidget(middle, 1);

        window.resize(1300, 760);
        window.move(pos);
        window.show();
    }

    bool exposed() { return QTest::qWaitForWindowExposed(&window); }
};

// Records where a widget was when it was shown.
class ShowSpy : public QObject {
public:
    ShowSpy(QWidget *watched, QPoint *at) : m_at(at) { watched->installEventFilter(this); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Show)
            *m_at = static_cast<QWidget *>(watched)->pos();
        return QObject::eventFilter(watched, event);
    }

private:
    QPoint *m_at;
};

QPushButton *buttonWithText(QWidget *parent, const QString &text) {
    for (QPushButton *b : parent->findChildren<QPushButton *>()) {
        if (b->text() == text)
            return b;
    }
    return nullptr;
}

} // namespace

class PopupPlacementTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // The ratio this run was registered for, so a run that silently fell back to 1.0 fails.
        const QByteArray factor = qgetenv("QT_SCALE_FACTOR");
        const qreal expected = factor.isEmpty() ? 1.0 : factor.toDouble();
        QCOMPARE(qApp->primaryScreen()->devicePixelRatio(), expected);
        QVERIFY2(qApp->primaryScreen()->availableGeometry().width() >= 1900,
                 "the 4K offscreen screen config was not loaded");
    }

    void init() { g_sentCat.clear(); }

    // --- The geometry rule itself -------------------------------------------------------------

    void placeCardCentresAboveAndClampsOnlyAtTheScreenEdge() {
        const QRect screen(0, 0, 1920, 1080);
        const QSize card(400, 100);

        // Mid-screen: centred, one shadow margin above, nothing clamped.
        const QRect anchor(700, 900, 200, 40);
        QCOMPARE(K4PopupBase::placeCard(anchor, card, screen), QPoint(600, 900 - Sm - 100));

        // Odd widths: centred to the half pixel (the card's centre within 0.5 px of the anchor's).
        const QPoint odd = K4PopupBase::placeCard(QRect(700, 900, 91, 40), QSize(401, 100), screen);
        QVERIFY(qAbs((odd.x() + 200.5) - (700 + 45.5)) <= 0.5);

        // Left and right screen edges: shifted only far enough to stay on screen.
        QCOMPARE(K4PopupBase::placeCard(QRect(10, 900, 40, 40), card, screen).x(), 0);
        QCOMPARE(K4PopupBase::placeCard(QRect(1900, 900, 20, 40), card, screen).x(), 1920 - 400);

        // No room above: the same gap below the anchor.
        QCOMPARE(K4PopupBase::placeCard(QRect(700, 30, 200, 40), card, screen).y(), 30 + 40 + Sm);
        // Room neither above nor below: held on screen.
        QCOMPARE(K4PopupBase::placeCard(QRect(700, 30, 200, 1000), card, screen).y(), 1080 - 100);

        // A screen that does not start at 0 (second monitor, or a menu bar above the work area).
        const QRect second(1920, 25, 2560, 1415);
        QCOMPARE(K4PopupBase::placeCard(QRect(1925, 900, 10, 40), card, second).x(), 1920);
        QCOMPARE(K4PopupBase::placeCard(QRect(2000, 60, 100, 40), card, second).y(), 60 + 40 + Sm);
    }

    // --- Where each popup opens ----------------------------------------------------------------

    // The bottom menu bar's family (MAIN RX / SUB RX / TX rows, DISPLAY, the mode grid and the
    // ATTN / NB / NR / NOTCH bar) is centred on the bar itself. The mode popup was centred on the
    // bar's parent, which also holds the left panel: 65 px left of every other popup.
    void bottomBarPopupsSitCentredDirectlyAboveTheBar() {
        Host host;
        QVERIFY(host.exposed());
        const QRect bar = globalRect(host.bar);

        ButtonRowPopup row(&host.window);
        row.showAboveWidget(host.bar);
        VERIFY_CENTRED_ABOVE(cardOf(&row), bar);
        row.hidePopup();

        ModePopupWidget mode(&host.window);
        mode.showAboveWidget(host.bar);
        VERIFY_CENTRED_ABOVE(cardOf(&mode), bar);
        mode.hidePopup();

        DisplayPopupWidget display(&host.window);
        display.showAboveWidget(host.bar);
        VERIFY_CENTRED_ABOVE(cardOf(&display), bar);
        display.hidePopup();

        // Every feature: the bar is a different width for each (NB's FILTER key, NR's engine key).
        FeatureMenuBar feature(&host.window);
        for (auto f : {FeatureMenuBar::Attenuator, FeatureMenuBar::NbLevel, FeatureMenuBar::NrAdjust,
                       FeatureMenuBar::ManualNotch}) {
            feature.showForFeature(f, host.bar);
            QVERIFY(feature.isVisible());
            VERIFY_CENTRED_ABOVE(cardOf(&feature), bar);
            // The card is the row of keys, not a strip of empty frame: its last key ends inside it.
            QPushButton *plus = buttonWithText(&feature, QStringLiteral("+"));
            QVERIFY(plus);
            QVERIFY(cardOf(&feature).contains(globalRect(plus)));
            feature.hidePopup();
        }

        // The NB FILTER key changes its text under the open bar: the card follows the row.
        feature.setNbFilter(0);
        feature.showForFeature(FeatureMenuBar::NbLevel, host.bar);
        for (int filter : {1, 2, 0}) {
            feature.setNbFilter(filter);
            VERIFY_CENTRED_ABOVE(cardOf(&feature), bar);
            QPushButton *plus = buttonWithText(&feature, QStringLiteral("+"));
            QVERIFY(cardOf(&feature).contains(globalRect(plus)));
        }
        feature.hidePopup();
    }

    // A row's sub-popups (RX EQ, LINE OUT, VOX, ANT CFG ...) sit on the row's card. They were
    // centred on the row's parent (the main window: 65 px off) and stacked on the row's window,
    // shadow and all, leaving two shadow margins between the cards.
    void rowSubPopupsSitCentredDirectlyAboveTheRow() {
        Host host;
        QVERIFY(host.exposed());
        ButtonRowPopup row(&host.window);
        row.showAboveWidget(host.bar);
        const QRect rowCard = cardOf(&row);

        VoxPopupWidget vox(&host.window);
        vox.showAboveWidget(&row);
        VERIFY_CENTRED_ABOVE(cardOf(&vox), rowCard);
        vox.hidePopup();

        RxEqPopupWidget eq(QStringLiteral("RX GRAPHIC EQUALIZER"), K4Styles::Colors::VfoACyan, &host.window);
        eq.showAboveWidget(&row);
        VERIFY_CENTRED_ABOVE(cardOf(&eq), rowCard);
        eq.hidePopup();

        AntennaCfgPopupWidget ant(AntennaCfgVariant::MainRx, &host.window);
        ant.showAboveWidget(&row);
        VERIFY_CENTRED_ABOVE(cardOf(&ant), rowCard);
    }

    // A lone key's popup is centred on the key; at the screen's edge it moves only far enough to
    // stay on it, and with no room above it opens below.
    void keyPopupsCentreOnTheKeyAndClampOnlyAtTheScreenEdge() {
        // The window's top on the screen's top: no room above the status bar.
        const QRect avail = qApp->primaryScreen()->availableGeometry();
        Host host(QPoint(avail.right() - 1300 + 1, avail.top()));
        QVERIFY(host.exposed());

        ConfirmPopup confirm(QStringLiteral("Power off K4?"), QStringLiteral("Continue?"), QStringLiteral("Power Off"),
                             QStringLiteral("Cancel"), &host.window);
        confirm.showAboveWidget(host.powerKey);
        const QRect card = cardOf(&confirm);
        const QRect key = globalRect(host.powerKey);
        // The PWR key is at the right screen edge: the card ends there instead of centring.
        QCOMPARE(card.x() + card.width(), avail.x() + avail.width());
        // Below the key, one shadow margin down.
        QCOMPARE(card.y(), key.y() + key.height() + Sm);
        confirm.hidePopup();

        // Same window lower on the screen: centred directly above the key, nothing clamped.
        host.window.move(avail.center().x() - 650, avail.bottom() - 760 - 50);
        QTest::qWait(1);
        confirm.showAboveWidget(host.powerKey);
        VERIFY_CENTRED_ABOVE(cardOf(&confirm), globalRect(host.powerKey));
    }

    // --- What closes them ----------------------------------------------------------------------

    // A second right-click on ATTN must close the bar. As a Qt::Popup it was closed by the press,
    // which Qt then replayed to the ATTN key (no Qt::WA_NoMouseReplay), and the key opened it again.
    // Qt replays only where the platform's ReplayMousePressOutsidePopup hint is set: Windows and
    // X11, not macOS, and in practice not offscreen either (the replay finds no widget there). So
    // besides the clicks, the bar must be the Qt::Tool popup kind, which never replays a press.
    void featureBarSecondPressOnItsKeyCloses() {
        Host host;
        QVERIFY(host.exposed());
        RadioState radioState;
        FeatureMenuController controller(&radioState, nullptr, &host.window);
        // RightSidePanel emits attnClicked on the right-button press, as RightSideController wires it.
        connect(host.rightPanel, &RightSidePanel::attnClicked, &controller,
                [&]() { controller.toggleFeature(FeatureMenuBar::Attenuator, host.bar); });
        connect(host.rightPanel, &RightSidePanel::levelClicked, &controller,
                [&]() { controller.toggleFeature(FeatureMenuBar::NbLevel, host.bar); });
        QPushButton *pre = buttonWithText(host.rightPanel, QStringLiteral("PRE"));
        QPushButton *nb = buttonWithText(host.rightPanel, QStringLiteral("NB"));
        QVERIFY(pre && nb);
        auto *bar = host.window.findChild<FeatureMenuBar *>();
        QVERIFY(bar);
        QCOMPARE(bar->windowType(), Qt::Tool);

        // The first open must show once, already in place: it used to show at the default
        // position and only then move above the menu bar.
        QPoint shownAt;
        ShowSpy spy(bar, &shownAt);
        QTest::mouseClick(pre, Qt::RightButton);
        QTRY_VERIFY(controller.isVisible());
        QCOMPARE(shownAt, bar->pos());
        VERIFY_CENTRED_ABOVE(cardOf(bar), globalRect(host.bar));

        QTest::mouseClick(pre, Qt::RightButton);
        QTest::qWait(50);
        QVERIFY2(!controller.isVisible(), "the second press on ATTN reopened the bar");

        // Another feature's key switches the bar rather than closing it.
        QTest::qWait(300); // past the reopen guard: a separate gesture
        QTest::mouseClick(pre, Qt::RightButton);
        QTRY_VERIFY(controller.isVisible());
        QTest::qWait(300); // past the reopen guard: a separate gesture
        QTest::mouseClick(nb, Qt::RightButton);
        QTest::qWait(50);
        QVERIFY(controller.isVisible());
        QVERIFY(g_sentCat.isEmpty());
    }

    // The ANT CFG key on a row toggles its popup too: the press closed it by click-away and the key
    // then opened it again.
    void rowKeyPressedAgainClosesItsSubPopup() {
        Host host;
        QVERIFY(host.exposed());
        RadioState radioState;
        AntennaConfigController controller(&radioState, nullptr, &host.window);
        ButtonRowPopup row(&host.window);
        connect(&row, &ButtonRowPopup::buttonClicked, &controller, [&](int index) {
            if (index == 0)
                controller.showMainRxPopupAbove(&row);
        });
        row.showAboveWidget(host.bar);
        QWidget *antKey = row.findChildren<RxMenuButton *>().value(0);
        QVERIFY(antKey);

        auto antPopupVisible = [&]() {
            for (auto *p : host.window.findChildren<AntennaCfgPopupWidget *>()) {
                if (p->isVisible())
                    return true;
            }
            return false;
        };
        QTest::mouseClick(antKey, Qt::LeftButton);
        QTRY_VERIFY(antPopupVisible());
        QTest::mouseClick(antKey, Qt::LeftButton);
        QTest::qWait(50);
        QVERIFY2(!antPopupVisible(), "the second press on ANT CFG reopened its popup");
        QVERIFY(row.isVisible());
    }

    // A sub-popup goes with its row, whatever closed the row.
    void subPopupClosesWithItsRow() {
        Host host;
        QVERIFY(host.exposed());
        ButtonRowPopup row(&host.window);
        row.showAboveWidget(host.bar);
        VoxPopupWidget vox(&host.window);
        vox.showAboveWidget(&row);
        QVERIFY(vox.isVisible());

        row.hidePopup();
        QVERIFY2(!vox.isVisible(), "the VOX popup stayed up with nothing under it");
    }

    // Pressing - / + on a row's sub-popup is not a click away from the row: both stay open
    // (bench 2026-10-30, SSB TX BANDWIDTH closed itself and the TX row on every press).
    void pressInASubPopupKeepsItsRowOpen() {
        Host host;
        QVERIFY(host.exposed());
        ButtonRowPopup row(&host.window);
        row.showAboveWidget(host.bar);
        VoxPopupWidget vox(&host.window);
        vox.showAboveWidget(&row);
        QVERIFY(vox.isVisible());
        QPushButton *plus = nullptr;
        for (QPushButton *b : vox.findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("+"))
                plus = b;
        }
        QVERIFY(plus);
        QTest::mouseClick(plus, Qt::LeftButton);
        QVERIFY2(row.isVisible(), "the row closed on a press inside its own sub-popup");
        QVERIFY2(vox.isVisible(), "the sub-popup closed on a press on its own + key");
    }

    // A Qt::Tool window does not follow its parent: after a move it would hang where the key was.
    void popupClosesWhenTheWindowMovesResizesOrChangesState() {
        Host host;
        QVERIFY(host.exposed());
        ModePopupWidget mode(&host.window);

        mode.showAboveWidget(host.bar);
        host.window.move(host.window.pos() + QPoint(40, 30));
        QTRY_VERIFY2(!mode.isVisible(), "stayed up after the main window moved");

        mode.showAboveWidget(host.bar);
        host.window.resize(host.window.size() + QSize(60, 40));
        QTRY_VERIFY2(!mode.isVisible(), "stayed up after the main window resized");

        mode.showAboveWidget(host.bar);
        host.window.setWindowState(Qt::WindowMaximized);
        QTRY_VERIFY2(!mode.isVisible(), "stayed up after the main window changed state");
    }

    void popupClosesOnEscapeAndWhenTheAppGoesInactive() {
        Host host;
        QVERIFY(host.exposed());
        DisplayPopupWidget fn(&host.window);

        // Escape typed into the main window, the popup not holding the keyboard.
        fn.showAboveWidget(host.bar);
        host.window.activateWindow();
        host.bar->setFocus();
        QTest::keyClick(host.bar, Qt::Key_Escape);
        QVERIFY(!fn.isVisible());

        // macOS hides a Qt::Tool window while the app is inactive and shows it again on return.
        fn.showAboveWidget(host.bar);
        emit qApp->applicationStateChanged(Qt::ApplicationInactive);
        QVERIFY2(!fn.isVisible(), "would come back when QK4 is next activated");
    }

    void clickOutsideClosesAndAShowPlacesOnce() {
        Host host;
        QVERIFY(host.exposed());
        ButtonRowPopup row(&host.window);
        row.showAboveWidget(host.bar);
        QVERIFY(row.isVisible());
        QTest::mouseClick(host.leftPanel, Qt::LeftButton);
        QVERIFY(!row.isVisible());
        QVERIFY(row.isVisibleOrJustHidden()); // the guard the toggles read
    }

    void netHealthPopupTogglesOnItsLedAndClosesOutside() {
        Host host;
        QVERIFY(host.exposed());
        QTest::mouseClick(host.led, Qt::LeftButton);
        auto popupVisible = [&]() {
            for (auto *p : host.led->findChildren<K4PopupBase *>()) {
                if (p->isVisible())
                    return true;
            }
            return false;
        };
        QTRY_VERIFY(popupVisible());
        QTest::mouseClick(host.led, Qt::LeftButton);
        QTest::qWait(50);
        QVERIFY2(!popupVisible(), "the second click on the LED reopened the metrics popup");

        QTest::qWait(300);
        QTest::mouseClick(host.led, Qt::LeftButton);
        QTRY_VERIFY(popupVisible());
        QTest::mouseClick(host.leftPanel, Qt::LeftButton);
        QVERIFY2(!popupVisible(), "a click elsewhere left the metrics popup up");

        QTest::qWait(300);
        QTest::mouseClick(host.led, Qt::LeftButton);
        QTRY_VERIFY(popupVisible());
        host.led->closeMetricsPopup(); // the disconnect reset's call
        QVERIFY(!popupVisible());
    }

    void featureBarClosesOnTheResetCall() {
        Host host;
        QVERIFY(host.exposed());
        RadioState radioState;
        FeatureMenuController controller(&radioState, nullptr, &host.window);
        controller.toggleFeature(FeatureMenuBar::NrAdjust, host.bar);
        QVERIFY(controller.isVisible());
        controller.close(); // closeAllPopups() and MainWindow::resetUiForDisconnect()
        QVERIFY(!controller.isVisible());
    }
};

QTEST_MAIN(PopupPlacementTests)
#include "test_popupplacement.moc"

#include "models/menumodel.h"
#include "ui/overlays/menuoverlay.h"

#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QtTest>

#include <algorithm>

// The menu overlay against the disconnect → reconnect sequence that crashed on Linux (#124): the K4
// pushes its MEDF menu at connect, MainWindow clears the model on disconnect, and the radio pushes
// the menu again on the next connect.
class TestMenuOverlay : public QObject {
    Q_OBJECT

private slots:
    void reconnectAfterTheMenuWasOpenedReadsNoFreedItems();
    void anOpenMenuFollowsTheMenuResentAfterReconnect();
    void aClearDuringEditLeavesEditMode();
    void rowsFollowValueUpdatesAndTheXvtrSelector();
    void onlyReadOnlyRowsShowTheLock();
    void searchKeyPressedAgainClosesTheSearchBox();

private:
    static void sendMenu(MenuModel &model);
    static QPushButton *buttonLabelled(const MenuOverlayWidget &overlay, const QString &text);
    static QStringList rowTexts(const MenuOverlayWidget &overlay);
    static bool hasButtonLabelled(const MenuOverlayWidget &overlay, const QString &text);
};

void TestMenuOverlay::sendMenu(MenuModel &model) {
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0086,XVTR Band # Select,XV,XVTR,1,1,12,1,1,1;"));
    QVERIFY(model.parseMEDF("MEDF0078,XVTR Band <n> I.F.,XV,XVTR,1,0,1,0,0,1;"));
}

// "name=value" per row, in display order.
QStringList TestMenuOverlay::rowTexts(const MenuOverlayWidget &overlay) {
    QStringList rows;
    const auto widgets = overlay.findChildren<MenuItemWidget *>();
    for (const MenuItemWidget *row : widgets) {
        const auto labels = row->findChildren<QLabel *>(Qt::FindDirectChildrenOnly);
        if (labels.size() == 3) {
            rows << labels.first()->text() + QLatin1Char('=') + labels.last()->text();
        }
    }
    return rows;
}

QPushButton *TestMenuOverlay::buttonLabelled(const MenuOverlayWidget &overlay, const QString &text) {
    const auto buttons = overlay.findChildren<QPushButton *>();
    const auto it =
        std::find_if(buttons.cbegin(), buttons.cend(), [&](const QPushButton *b) { return b->text() == text; });
    return it == buttons.cend() ? nullptr : *it;
}

bool TestMenuOverlay::hasButtonLabelled(const MenuOverlayWidget &overlay, const QString &text) {
    const auto buttons = overlay.findChildren<QPushButton *>();
    return std::any_of(buttons.cbegin(), buttons.cend(), [&](const QPushButton *b) { return b->text() == text; });
}

// Regression for #124: open the menu, disconnect, reconnect, and the radio's next menu value
// change walked rows built from items the disconnect had destroyed. XVTR Band # Select is the
// change in the reported stack: it makes the overlay read every row's name.
void TestMenuOverlay::reconnectAfterTheMenuWasOpenedReadsNoFreedItems() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);

    overlay.show();
    QCOMPARE(rowTexts(overlay).size(), 3);
    overlay.hide();

    model.clear();
    sendMenu(model);
    QVERIFY(model.parseME("ME0086.0005;"));
    QVERIFY(model.parseME("ME0007.0123;"));

    overlay.show();
    QCOMPARE(rowTexts(overlay), (QStringList{"AGC Hold Time=123", "XVTR Band # Select=5", "XVTR Band 5 I.F.=0"}));
}

// Left open across the disconnect, the menu empties with the model and fills again when the radio
// resends it, without the operator closing and reopening it.
void TestMenuOverlay::anOpenMenuFollowsTheMenuResentAfterReconnect() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.show();
    QCOMPARE(rowTexts(overlay).size(), 3);

    model.clear();
    QVERIFY(rowTexts(overlay).isEmpty());

    sendMenu(model);
    QVERIFY(model.parseME("ME0007.0042;"));
    QTRY_COMPARE(rowTexts(overlay), (QStringList{"AGC Hold Time=42", "XVTR Band # Select=1", "XVTR Band 1 I.F.=0"}));
    QVERIFY(overlay.isVisible());
}

void TestMenuOverlay::aClearDuringEditLeavesEditMode() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.show();

    QTest::keyClick(&overlay, Qt::Key_Return);
    QVERIFY(hasButtonLabelled(overlay, QStringLiteral("+")));

    model.clear();
    QVERIFY(!hasButtonLabelled(overlay, QStringLiteral("+")));
    QVERIFY(hasButtonLabelled(overlay, QString::fromUtf8("\xE2\x96\xB2")));
}

void TestMenuOverlay::rowsFollowValueUpdatesAndTheXvtrSelector() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.show();

    QVERIFY(model.parseME("ME0007.0150;"));
    QVERIFY(model.parseME("ME0086.0009;"));
    QCOMPARE(rowTexts(overlay), (QStringList{"AGC Hold Time=150", "XVTR Band # Select=9", "XVTR Band 9 I.F.=0"}));
}

void TestMenuOverlay::onlyReadOnlyRowsShowTheLock() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0001,Serial Number,INFO,DEC,2,0,99999,0,12345,1;"));
    overlay.show();

    QStringList locks;
    const auto rows = overlay.findChildren<MenuItemWidget *>();
    for (const MenuItemWidget *row : rows) {
        locks << row->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).at(1)->text();
    }
    QCOMPARE(locks, (QStringList{QString(), QString::fromUtf8("\xF0\x9F\x94\x92")}));
}

// The search box is a Qt::Popup. A press outside it closes it, and where the platform's
// ReplayMousePressOutsidePopup hint is set (Windows, X11) Qt then replays that press to the widget
// under it - here the search key itself, which opened the box again. A press on the search key
// must close the box without that replay (Qt::WA_NoMouseReplay on the popup as it closes); a press
// anywhere else keeps the replay, so it still works whatever it landed on.
//
// The offscreen platform answers true to the hint but cannot grab the mouse for a popup ("This
// plugin does not support grabbing the keyboard"), and Qt replays only after a successful grab, so
// the reopen itself cannot be seen here. The press is delivered to the popup, as the grab delivers
// it on Windows and X11, and the test reads the attribute that decides the replay as the box closes.
void TestMenuOverlay::searchKeyPressedAgainClosesTheSearchBox() {
    MenuModel model;
    MenuOverlayWidget overlay(&model);
    sendMenu(model);
    overlay.resize(800, 600);
    overlay.show();
    QVERIFY(QTest::qWaitForWindowExposed(&overlay));
    QPushButton *search = buttonLabelled(overlay, QString::fromUtf8("\xF0\x9F\x94\x8D")); // the magnifier key
    QVERIFY(search);
    QWidget *box = nullptr;
    for (QWidget *w : overlay.findChildren<QWidget *>()) {
        if (w->windowType() == Qt::Popup)
            box = w;
    }
    QVERIFY(box);

    // Whether Qt will replay the closing press, read the moment the box closes.
    struct ReplayAtClose : QObject {
        bool closed = false;
        bool noReplay = false;
        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::Hide) {
                closed = true;
                noReplay = static_cast<QWidget *>(watched)->testAttribute(Qt::WA_NoMouseReplay);
            }
            return false;
        }
    };
    const auto pressOnBoxAt = [box](const QPoint &global) {
        QTest::mousePress(box, Qt::LeftButton, {}, box->mapFromGlobal(global));
    };

    // The search key, pressed while the box is open: closed, and not replayed to the key.
    QTest::mouseClick(search, Qt::LeftButton);
    QTRY_VERIFY(box->isVisible());
    ReplayAtClose onKey;
    box->installEventFilter(&onKey);
    pressOnBoxAt(search->mapToGlobal(search->rect().center()));
    QTest::mouseRelease(search, Qt::LeftButton, {}, search->rect().center());
    box->removeEventFilter(&onKey);
    QVERIFY(onKey.closed);
    QVERIFY2(onKey.noReplay, "the press on the search key would be replayed to it and reopen the box");
    QTest::qWait(50);
    QVERIFY(!box->isVisible());

    // Anywhere else: closed, and replayed as usual.
    QTest::qWait(300);
    QTest::mouseClick(search, Qt::LeftButton);
    QTRY_VERIFY(box->isVisible());
    ReplayAtClose elsewhere;
    box->installEventFilter(&elsewhere);
    pressOnBoxAt(overlay.mapToGlobal(QPoint(20, 20)));
    box->removeEventFilter(&elsewhere);
    QVERIFY(elsewhere.closed);
    QVERIFY2(!elsewhere.noReplay, "a press away from the search key lost its replay");
}

QTEST_MAIN(TestMenuOverlay)
#include "test_menuoverlay.moc"

// Options dialog page layout: no page may be wider than the dialog, the DX cluster console takes the
// spare height, and the page list repaints all of itself on a hover or selection change.
//
// Runs offscreen against a sandboxed settings file; it never reads or writes the real preferences.

#include "controllers/dxclustercontroller.h"
#include "settings/radiosettings.h"
#include "ui/pages/displaypage.h"
#include "ui/pages/dxclusterpage.h"
#include "ui/pages/optionspagelayout.h"
#include "ui/pages/stationpage.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <memory>

namespace D = K4Styles::Dimensions;

namespace {

// Width a page gets at the dialog's minimum size, with the vertical scrollbar showing.
int narrowestPageWidth() {
    constexpr int kScrollBarWidth = 8;
    return D::OptionsDialogMinWidth - D::TabListWidth - kScrollBarWidth;
}

class PaintRecorder : public QObject {
public:
    QRegion painted;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Paint)
            painted += static_cast<QPaintEvent *>(event)->region();
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

class TestOptionsPageLayout : public QObject {
    Q_OBJECT

    QTemporaryDir m_tempDir;

private slots:
    void initTestCase() {
        QVERIFY2(m_tempDir.isValid(), "no temporary directory; refusing to run against real settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
        const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                              QStringLiteral("QK4"));
        QVERIFY2(probe.fileName().startsWith(m_tempDir.path()),
                 qPrintable(QStringLiteral("settings not redirected; would have written to ") + probe.fileName()));

        DxClusterEntry entry;
        entry.host = QStringLiteral("a-cluster-host-name-long-enough-to-matter.example.org");
        entry.port = 7300;
        RadioSettings::instance()->addDxCluster(entry);
    }

    void helpTextWraps() {
        const QString sentence = QString("A help sentence that keeps going ").repeated(12);
        std::unique_ptr<QLabel> help(OptionsPageLayout::helpText(sentence, nullptr));
        QVERIFY(help->wordWrap());
        QVERIFY(help->minimumSizeHint().width() < narrowestPageWidth() / 2);

        // The check bites: the same text unwrapped is far wider than any page.
        QLabel plain(sentence);
        QVERIFY(plain.minimumSizeHint().width() > narrowestPageWidth());
    }

    void deviceListIgnoresLongNames() {
        const QString longName = QString("USB Audio CODEC (Very Long Vendor Device Name) ").repeated(4);
        QComboBox fitted;
        fitted.setStyleSheet(K4Styles::Dialog::comboBox());
        OptionsPageLayout::fitDeviceCombo(&fitted);
        fitted.addItem(longName);
        QVERIFY(fitted.minimumSizeHint().width() <= D::OptionsFieldMaxWidth);
        QVERIFY(fitted.maximumWidth() <= D::OptionsFieldMaxWidth);

        // The default policy sizes to the longest item, which is what overflowed the Audio Input page.
        QComboBox plain;
        plain.setStyleSheet(K4Styles::Dialog::comboBox());
        plain.addItem(longName);
        QVERIFY(plain.minimumSizeHint().width() > narrowestPageWidth());
    }

    void pagesFitTheNarrowestDialog_data() {
        QTest::addColumn<int>("which");
        QTest::newRow("Station") << 0;
        QTest::newRow("Display") << 1;
        QTest::newRow("DX Cluster") << 2;
    }

    void pagesFitTheNarrowestDialog() {
        QFETCH(int, which);
        DxClusterController controller;
        QWidget *page = nullptr;
        if (which == 0)
            page = new StationPage();
        else if (which == 1)
            page = new DisplayPage();
        else
            page = new DxClusterPage(&controller);

        std::unique_ptr<QScrollArea> scroll(OptionsPageLayout::wrapInScrollArea(page, nullptr));
        scroll->resize(narrowestPageWidth(), D::OptionsDialogMinHeight);
        scroll->show();
        QVERIFY(QTest::qWaitForWindowExposed(scroll.get()));

        QVERIFY2(page->minimumSizeHint().width() <= narrowestPageWidth(),
                 qPrintable(QString("page needs %1 px, the dialog gives it %2")
                                .arg(page->minimumSizeHint().width())
                                .arg(narrowestPageWidth())));
        QVERIFY(page->width() <= scroll->viewport()->width());
    }

    void overflowIsDetected() {
        // Guard on the guard: a page with an unwrapped sentence must fail the width check above.
        auto *page = new QWidget();
        auto *layout = OptionsPageLayout::createPage(page);
        layout->addWidget(new QLabel(QString("An unwrapped sentence ").repeated(10), page));
        std::unique_ptr<QScrollArea> scroll(OptionsPageLayout::wrapInScrollArea(page, nullptr));
        QVERIFY(page->minimumSizeHint().width() > narrowestPageWidth());
    }

    void dxConsoleTakesTheSpareHeight() {
        DxClusterController controller;
        auto *page = new DxClusterPage(&controller);
        std::unique_ptr<QScrollArea> scroll(OptionsPageLayout::wrapInScrollArea(page, nullptr));
        auto *console = page->findChild<QPlainTextEdit *>();
        QVERIFY(console);
        QCOMPARE(console->lineWrapMode(), QPlainTextEdit::NoWrap);

        scroll->resize(650, 650);
        scroll->show();
        QVERIFY(QTest::qWaitForWindowExposed(scroll.get()));
        QApplication::processEvents();
        const int shortHeight = console->height();
        QVERIFY(shortHeight >= D::OptionsConsoleMinHeight);

        scroll->resize(650, 1000);
        QApplication::processEvents();
        // Every extra pixel goes to the console; nothing else on the page grows.
        QCOMPARE(console->height() - shortHeight, 350);
    }

    void navListRepaintsWholeViewport() {
        // WHY skipped offscreen: that platform repaints the whole window on every update, so a list
        // that repaints only item rectangles - the cause of the stale amber edges - passes there
        // anyway. On macOS it fails without the fix. Run natively:
        //   build/tests/test_optionspagelayout navListRepaintsWholeViewport
        // Hover is not covered: synthetic mouse moves produce no hover events on Cocoa.
        if (QGuiApplication::platformName() == QLatin1String("offscreen"))
            QSKIP("offscreen repaints whole windows; run this case on a native platform");

        QListWidget list;
        OptionsPageLayout::configureNavList(&list);
        for (int i = 0; i < 11; ++i)
            list.addItem(QString("Page %1").arg(i));
        list.setCurrentRow(0);
        list.resize(D::TabListWidth, 600);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        QApplication::processEvents();

        PaintRecorder recorder;
        list.viewport()->installEventFilter(&recorder);
        list.setCurrentRow(5);
        QTRY_VERIFY(!recorder.painted.isEmpty());
        const QRect whole = list.viewport()->rect();
        QVERIFY2((QRegion(whole) - recorder.painted).isEmpty(),
                 qPrintable(QString("a selection change repainted %1 px of the %2 px list")
                                .arg(recorder.painted.boundingRect().height())
                                .arg(whole.height())));
    }
};

QTEST_MAIN(TestOptionsPageLayout)
#include "test_optionspagelayout.moc"

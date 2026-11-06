// Left side panel layout (src/ui/widgets/sidecontrolpanel.cpp), rendered offscreen at the smallest
// size its layout allows. That is the size the main window gives it when the window is at its own
// minimum, so nothing in the panel may overlap there.
//
// WHY: testers on Windows and Linux saw the TX buttons' orange sub-labels painted over the buttons.
// The window's hand-written minimum was smaller than this panel needed, and the layout squeezed each
// button-and-label pair from 49 px to 42. This holds the panel to its own minimum and checks the
// pairs, and every other control, stay apart.

#include "ui/widgets/rightsidepanel.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/bottommenubar.h"
#include "ui/widgets/dualcontrolbutton.h"
#include "ui/widgets/kpa1500minipanel.h"
#include "ui/widgets/memorykeyrow.h"
#include "ui/widgets/vforowwidget.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "ui/widgets/sidepanelbutton.h"
#include "utils/versiontext.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFontMetricsF>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtMath>
#include <QSettings>
#include <QTemporaryDir>
#include <climits>
#include <QTest>

class SideControlPanelLayoutTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

    // Rect in the panel's coordinates.
    static QRect inPanel(const QWidget *panel, const QWidget *w) {
        return QRect(w->mapTo(panel, QPoint(0, 0)), w->size());
    }

private slots:
    void initTestCase() {
        // The panel reads the saved volumes; keep it off the real preferences.
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
        // The app runs with this sheet installed (main.cpp); it changes how every widget is laid out.
        qApp->setStyleSheet(K4Styles::applicationStyleSheet());
    }

    // The first overlap found with the panel at `height`, or an empty string.
    static QString firstOverlap(int height, int *pairsOut = nullptr) {
        // WHY: hosted as a child, the way MainWindow holds it. A top-level widget takes its minimum
        // from its layout and would refuse the squeeze this has to be able to reproduce.
        QWidget host;
        auto &panel = *new SideControlPanel(&host);
        panel.setGeometry(0, 0, panel.minimumSizeHint().width(), height);
        host.resize(panel.size());
        host.show();
        QCoreApplication::processEvents();

        // Each TX button shares a container with its sub-label.
        int pairs = 0;
        QString found;
        for (QLabel *label : panel.findChildren<QLabel *>()) {
            QWidget *container = label->parentWidget();
            if (container == &panel || !label->isVisible())
                continue;
            const auto buttons = container->findChildren<QPushButton *>(Qt::FindDirectChildrenOnly);
            if (buttons.size() != 1)
                continue;
            ++pairs;
            if (found.isEmpty() && inPanel(&panel, buttons.first()).intersects(inPanel(&panel, label)))
                found = QString("'%1' sub-label overlaps its button").arg(label->text());
        }
        if (pairsOut)
            *pairsOut = pairs;

        // And no two of the panel's own controls overlap each other. Overlays are hidden here.
        QList<QWidget *> children;
        for (QWidget *w : panel.findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            if (w->isVisible())
                children << w;
        for (int i = 0; i < children.size() && found.isEmpty(); ++i)
            for (int j = i + 1; j < children.size() && found.isEmpty(); ++j)
                if (children[i]->geometry().intersects(children[j]->geometry()))
                    found = QString("%1 overlaps %2")
                                .arg(children[i]->metaObject()->className(), children[j]->metaObject()->className());
        return found;
    }

    void nothingOverlapsAtMinimumSize() {
        SideControlPanel probe;
        int pairs = 0;
        const QString overlap = firstOverlap(probe.minimumSizeHint().height(), &pairs);
        QCOMPARE(pairs, 6);
        QVERIFY2(overlap.isEmpty(), qPrintable(overlap));
    }

    void squeezeOfTheReportedBugIsDetected() {
        // 22 px short is what the window's old 1340x840 minimum left the panel once a Windows or
        // Linux menu bar took its share. If this stops detecting an overlap the check above has
        // gone blind, not the bug away.
        SideControlPanel probe;
        QVERIFY(!firstOverlap(probe.minimumSizeHint().height() - 22).isEmpty());
    }

    // The first orange sub-label in `panel` whose text does not fit its width, or an empty string.
    // kGapPx keeps neighbours (REM ANT | SUB ANT) from touching even when each one just fits.
    static QString firstClippedSubLabel(QWidget &panel) {
        static constexpr qreal kGapPx = 2.0;
        QWidget host;
        panel.setParent(&host);
        panel.setGeometry(0, 0, panel.minimumSizeHint().width(), panel.minimumSizeHint().height());
        host.resize(panel.size());
        host.show();
        QCoreApplication::processEvents();
        for (QLabel *label : panel.findChildren<QLabel *>()) {
            if (!label->isVisible() || label->text().isEmpty() || label->text().contains('\n'))
                continue;
            // A centred sub-label shares its edges with its neighbour's; other labels only need their text.
            const qreal gap = (label->alignment() & Qt::AlignHCenter) ? kGapPx : 0.0;
            const qreal needed = QFontMetricsF(label->font(), label).horizontalAdvance(label->text()) + gap;
            if (label->width() < needed)
                return QString("\"%1\" needs %2 px, has %3").arg(label->text()).arg(needed).arg(label->width());
        }
        return {};
    }

    // Buttons in `panel` that break the side-panel button rules, or an empty string.
    static QString firstBadButton(QWidget &panel) {
        QWidget host;
        panel.setParent(&host);
        panel.setGeometry(0, 0, panel.minimumSizeHint().width(), panel.minimumSizeHint().height());
        host.resize(panel.size());
        host.show();
        QCoreApplication::processEvents();
        for (QPushButton *button : panel.findChildren<QPushButton *>()) {
            if (!button->isVisible())
                continue;
            const QString name = QString(button->text()).replace('\n', ' ');
            // WHY: on macOS the native style's smaller layout rect let the sub-label be laid over
            // the button's bottom edge; the offscreen style used here cannot show that, so the rule
            // that prevents it is asserted directly.
            if (!button->testAttribute(Qt::WA_LayoutUsesWidgetRect))
                return QString("'%1' lacks Qt::WA_LayoutUsesWidgetRect").arg(name);
            const QSize need = button->sizeHint();
            if (!button->text().isEmpty() && need.height() > button->height())
                return QString("'%1' needs %2 px of height, has %3").arg(name).arg(need.height()).arg(button->height());
            if (!button->text().isEmpty() && need.width() > button->width())
                return QString("'%1' needs %2 px of width, has %3").arg(name).arg(need.width()).arg(button->width());
        }
        return {};
    }

    void leftPanelButtonsKeepTheirFullHeight() {
        auto *panel = new SideControlPanel;
        const QString bad = firstBadButton(*panel);
        QVERIFY2(bad.isEmpty(), qPrintable(bad));
    }

    void rightPanelButtonsKeepTheirFullHeight() {
        auto *panel = new RightSidePanel;
        const QString bad = firstBadButton(*panel);
        QVERIFY2(bad.isEmpty(), qPrintable(bad));
    }

    void everyLeftPanelSubLabelFits() {
        // At the 10 px text floor REM ANT and SUB ANT were drawn edge to edge.
        auto *panel = new SideControlPanel;
        const QString clipped = firstClippedSubLabel(*panel);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }

    void showingAReservedKpaCardDoesNotGrowThePanel() {
        // The window jumped taller when the amplifier connected: the card, shown only while
        // connected, raised the right column's minimum by ~190 px. With the amplifier enabled the
        // controller reserves its space, so showing it must not change the panel's minimum.
        RightSidePanel panel;
        auto *card = new Kpa1500MiniPanel(&panel);
        panel.embedKpa1500Panel(card);
        card->setVisible(false);
        panel.setKpa1500SlotReserved(true);
        const int hidden = panel.minimumSizeHint().height();
        card->setVisible(true);
        QCOMPARE(panel.minimumSizeHint().height(), hidden);
    }

    void reservingTheCardReachesTheLayoutAbove() {
        // The window sized itself, and chose its Auto interface scale, without the card: the card is
        // explicitly hidden, so its own size-policy change never reached the layouts above it. Both
        // ways, before anything is shown, the panel's parent must see the change.
        QWidget host;
        auto *column = new QVBoxLayout(&host);
        column->setContentsMargins(0, 0, 0, 0);
        auto *panel = new RightSidePanel;
        column->addWidget(panel);
        auto *card = new Kpa1500MiniPanel(panel);
        card->setVisible(false);
        panel->embedKpa1500Panel(card);
        panel->setKpa1500SlotReserved(false);
        const QSize without = host.minimumSizeHint();
        QCOMPARE(panel->width(), K4Styles::Dimensions::RightSidePanelWidth);

        panel->setKpa1500SlotReserved(true);
        QVERIFY2(host.minimumSizeHint().height() >= without.height() + card->minimumSizeHint().height(),
                 qPrintable(QString("%1 -> %2").arg(without.height()).arg(host.minimumSizeHint().height())));
        QCOMPARE(panel->width(), K4Styles::Dimensions::RightSidePanelKpaWidth);

        panel->setKpa1500SlotReserved(false);
        QCOMPARE(host.minimumSizeHint(), without);
        QCOMPARE(panel->width(), K4Styles::Dimensions::RightSidePanelWidth);
    }

    void everyRightPanelSubLabelFits() {
        // At the 10 px text floor DIVERSITY, under SUB, was cut to "IVERSITY".
        auto *panel = new RightSidePanel;
        const QString clipped = firstClippedSubLabel(*panel);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }
};

// The side-panel button standard (K4Styles::Dimensions::Side*, built by SidePanelButton): both panels and
// the KPA1500 mini-panel, laid out together at their minimum height as MainWindow holds them.
class SidePanelButtonStandardTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_tempDir;
    QWidget m_host;
    SideControlPanel *m_left = nullptr;
    RightSidePanel *m_right = nullptr;
    Kpa1500MiniPanel *m_kpa = nullptr;

    // Function buttons: a button sharing its container with an orange sub-label.
    static QList<QPushButton *> functionButtons(QWidget *panel) {
        QList<QPushButton *> found;
        for (QPushButton *button : panel->findChildren<QPushButton *>()) {
            QWidget *cell = button->parentWidget();
            if (cell != panel && cell->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).size() == 1)
                found << button;
        }
        return found;
    }

    static QRect inPanel(const QWidget *panel, const QWidget *w) { return QRect(w->mapTo(panel, QPoint()), w->size()); }

    static QLabel *subLabelOf(const QPushButton *button) {
        return button->parentWidget()->findChildren<QLabel *>(Qt::FindDirectChildrenOnly).first();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
        qApp->setStyleSheet(K4Styles::applicationStyleSheet());

        auto *row = new QHBoxLayout(&m_host);
        row->setContentsMargins(0, 0, 0, 0);
        m_left = new SideControlPanel;
        m_right = new RightSidePanel;
        m_kpa = new Kpa1500MiniPanel;
        m_kpa->setConnected(true);
        m_right->embedKpa1500Panel(m_kpa);
        m_right->setKpa1500SlotReserved(true);
        row->addWidget(m_left);
        row->addWidget(m_right);
        m_host.resize(m_host.minimumSizeHint());
        m_host.show();
        QCoreApplication::processEvents();
    }

    void everyFunctionButtonHasTheOneSize() {
        // Light (TX, PF) and dark keys, both panels and the KPA1500 card: one size.
        QList<QPushButton *> buttons = functionButtons(m_left) + functionButtons(m_right);
        QCOMPARE(buttons.size(), 6 + 10 + 4 + 4);
        buttons += m_kpa->findChildren<QPushButton *>();
        for (QPushButton *button : buttons)
            QVERIFY2(button->size() == SidePanelButton::kFunctionSize, qPrintable(button->text()));
    }

    void lightAndDarkDifferOnlyInColour() {
        QWidget parent;
        for (const QString &text : {QStringLiteral("TUNE"), QStringLiteral("ATU\nTUNE"), QStringLiteral("DIVERSITY")}) {
            QPushButton *light = SidePanelButton::create(text, SidePanelButton::Shade::Light, &parent);
            QPushButton *dark = SidePanelButton::create(text, SidePanelButton::Shade::Dark, &parent);
            QCOMPARE(light->size(), dark->size());
            QCOMPARE(light->sizeHint(), dark->sizeHint());
            QCOMPARE(light->minimumSizeHint(), dark->minimumSizeHint());
            QVERIFY(light->styleSheet() != dark->styleSheet());
        }
    }

    void subLabelsSitTheSameGapBelowEveryButton() {
        for (QWidget *panel : {static_cast<QWidget *>(m_left), static_cast<QWidget *>(m_right)}) {
            for (QPushButton *button : functionButtons(panel)) {
                const QRect b = inPanel(panel, button);
                const QRect l = inPanel(panel, subLabelOf(button));
                QCOMPARE(l.top() - b.bottom() - 1, K4Styles::Dimensions::SideButtonSubLabelGap);
                QCOMPARE(l.height(), SidePanelButton::subLabelHeight(subLabelOf(button)));
            }
        }
    }

    void columnsAndRowsKeepOneRhythm() {
        const int pitch = K4Styles::Dimensions::SideButtonHeight + K4Styles::Dimensions::SideButtonSubLabelGap +
                          SidePanelButton::subLabelHeight(m_left) + K4Styles::Dimensions::SideButtonRowGap;
        int grids = 0;
        for (QWidget *panel : {static_cast<QWidget *>(m_left), static_cast<QWidget *>(m_right)}) {
            for (QGridLayout *grid : panel->findChildren<QGridLayout *>()) {
                if (grid->parentWidget() != panel)
                    continue;
                ++grids;
                auto buttonAt = [&](int row, int column) {
                    return inPanel(panel, grid->itemAtPosition(row, column)->widget()->findChild<QPushButton *>());
                };
                for (int row = 0; row < grid->rowCount(); ++row) {
                    QCOMPARE(buttonAt(row, 1).left() - buttonAt(row, 0).right() - 1,
                             K4Styles::Dimensions::SideButtonColumnGap);
                    if (row > 0)
                        QCOMPARE(buttonAt(row, 0).top() - buttonAt(row - 1, 0).top(), pitch);
                }
            }
        }
        QCOMPARE(grids, 4); // TX grid; main, PF and bottom grids
    }

    void panelsLineUp() {
        // Left: the grid fills the panel inside its margins. Right: the same grid, centred, so both
        // insets are equal, and the KPA1500 keys stand in the same two columns.
        const QRect leftFirst = inPanel(m_left, functionButtons(m_left).first());
        QCOMPARE(leftFirst.left(), K4Styles::Dimensions::SidePanelMarginH);
        QCOMPARE(leftFirst.top(), K4Styles::Dimensions::SidePanelMarginV);

        int minX = INT_MAX;
        int maxX = 0;
        for (QPushButton *button : functionButtons(m_right)) {
            minX = qMin(minX, inPanel(m_right, button).left());
            maxX = qMax(maxX, inPanel(m_right, button).right() + 1);
        }
        QCOMPARE(maxX - minX, K4Styles::Dimensions::SideButtonGridWidth);
        QCOMPARE(minX, m_right->width() - maxX);
        QCOMPARE(minX, K4Styles::Dimensions::SidePanelMarginH + K4Styles::Dimensions::SidePanelMeterAllowance);
        QCOMPARE(inPanel(m_right, functionButtons(m_right).first()).top(), leftFirst.top());
        for (QPushButton *button : m_kpa->findChildren<QPushButton *>()) {
            const int x = inPanel(m_right, button).left();
            QVERIFY2(x == minX ||
                         x == minX + K4Styles::Dimensions::SideButtonWidth + K4Styles::Dimensions::SideButtonColumnGap,
                     qPrintable(button->text()));
        }
    }

    void kpaCardKeepsAGroupGapAboveThePfKeys() {
        // The stretch below the card is zero at the window's minimum height, which put B SET/CLR
        // straight under the card.
        QPushButton *bset = nullptr;
        for (QPushButton *button : functionButtons(m_right))
            if (button->text() == QLatin1String("B SET"))
                bset = button;
        QVERIFY(bset);
        const int gap = inPanel(m_right, bset).top() - inPanel(m_right, m_kpa).bottom() - 1;
        QVERIFY2(gap >= K4Styles::Dimensions::SidePanelGroupGap, qPrintable(QString::number(gap)));
    }

    void withoutTheKpa1500TheRightPanelMatchesTheLeft() {
        QWidget host;
        auto *panel = new RightSidePanel(&host);
        panel->embedKpa1500Panel(new Kpa1500MiniPanel(panel));
        panel->setKpa1500SlotReserved(false);
        panel->setGeometry(0, 0, panel->width(), panel->minimumSizeHint().height());
        host.resize(panel->size());
        host.show();
        QCoreApplication::processEvents();
        QCOMPARE(panel->width(), m_left->width());
        int minX = INT_MAX;
        for (QPushButton *button : functionButtons(panel))
            minX = qMin(minX, inPanel(panel, button).left());
        QCOMPARE(minX, K4Styles::Dimensions::SidePanelMarginH);
    }

    void knobKeysKeepTheirSize() {
        // The knob keys are 90x44, left-aligned under the key grid; an earlier change stretched them to
        // the grid's width and shrank them to their text, which the maintainer rejected.
        const auto knobs = m_left->findChildren<DualControlButton *>();
        QCOMPARE(knobs.size(), 6);
        for (DualControlButton *knob : knobs) {
            QCOMPARE(knob->x(), K4Styles::Dimensions::SidePanelMarginH);
            QCOMPARE(knob->size(),
                     QSize(K4Styles::Dimensions::KnobButtonWidth, K4Styles::Dimensions::KnobButtonHeight));
            QVERIFY(knob->height() >= DualControlButton::measuredHeight(knob));
        }
    }

    void gapsAreDrawnAtTheirTokens() {
        // QBoxLayout draws its spacing between two items a spacer separates, so addSpacing(16) drew 20.
        namespace D = K4Styles::Dimensions;
        const auto knobs = m_left->findChildren<DualControlButton *>();
        auto gapBelow = [](const QWidget *upper, const QWidget *lower) {
            return lower->mapTo(lower->window(), QPoint()).y() - upper->mapTo(upper->window(), QPoint()).y() -
                   upper->height();
        };
        QList<QLabel *> txLabels;
        for (QPushButton *button : functionButtons(m_left))
            txLabels << subLabelOf(button);
        QCOMPARE(gapBelow(txLabels.last(), knobs[0]), D::SidePanelGroupGap);
        for (int i = 1; i < knobs.size(); ++i)
            QCOMPARE(gapBelow(knobs[i - 1], knobs[i]), i % 2 ? D::SidePanelItemSpacing : D::SidePanelGroupGap);

        QPushButton *mon = nullptr;
        for (QPushButton *button : m_left->findChildren<QPushButton *>())
            if (button->text() == QLatin1String("MON"))
                mon = button;
        const auto sliders = m_left->findChildren<QSlider *>();
        QCOMPARE(sliders.size(), 2);
        QCOMPARE(gapBelow(knobs.last(), mon), D::SidePanelMinorGap);
        QCOMPARE(gapBelow(mon, sliders[0]), D::SidePanelMinorGap);
        QCOMPARE(gapBelow(sliders[0], sliders[1]), D::SidePanelMinorGap);

        // Right: main grid to the KPA1500 card, PF keys to the VFO keys.
        const auto right = functionButtons(m_right);
        QCOMPARE(gapBelow(subLabelOf(right[9]), m_kpa), D::SidePanelGroupGap);
        QCOMPARE(gapBelow(subLabelOf(right[13]), right[14]), D::SidePanelSectionGap);
    }

    void volumeSlidersShareOneUsableRow() {
        // MAIN and SUB each sit on one row with their name; both sliders start and end together and
        // keep a groove a mouse and the arrow keys can work.
        const auto sliders = m_left->findChildren<QSlider *>();
        QCOMPARE(sliders.size(), 2);
        QCOMPARE(sliders[0]->x(), sliders[1]->x());
        QCOMPARE(sliders[0]->width(), sliders[1]->width());
        QVERIFY2(sliders[0]->width() >= 80, qPrintable(QString::number(sliders[0]->width())));
        QCOMPARE(sliders[0]->geometry().right(), m_left->width() - K4Styles::Dimensions::SidePanelMarginH - 1);
        for (QSlider *slider : sliders)
            QVERIFY(slider->focusPolicy() & Qt::TabFocus);
    }

    void kpaButtonsSitDirectlyOnTheCard() {
        // WHY: the app's background rule paints every plain QWidget; a container behind each key drew
        // an opaque square over the card's lighter tint.
        for (QPushButton *button : m_kpa->findChildren<QPushButton *>())
            QVERIFY2(button->parentWidget() == m_kpa, qPrintable(button->text()));
    }

    void statusGroupSitsOnTheBottomEdge() {
        // Taller than its minimum, the spare height goes above the clock, not below the icon row.
        QWidget host;
        auto *panel = new SideControlPanel(&host);
        const int extra = 260;
        panel->setGeometry(0, 0, panel->width(), panel->minimumSizeHint().height() + extra);
        host.resize(panel->size());
        host.show();
        QCoreApplication::processEvents();
        QPushButton *icon = nullptr;
        for (QPushButton *button : panel->findChildren<QPushButton *>(Qt::FindDirectChildrenOnly))
            if (button->size() ==
                QSize(K4Styles::Dimensions::SideIconButtonSize, K4Styles::Dimensions::SideIconButtonSize))
                icon = button;
        QVERIFY(icon);
        QCOMPARE(icon->geometry().bottom() + 1, panel->height() - K4Styles::Dimensions::SidePanelMarginV);
        QCOMPARE(functionButtons(panel).first()->mapTo(panel, QPoint()).y(), K4Styles::Dimensions::SidePanelMarginV);
    }

    void versionLabelText_data() {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("label");
        QTest::newRow("none") << QString() << QString();
        QTest::newRow("release") << "0.8.0" << "V0.8.0";
        QTest::newRow("pre-release") << "0.10.0-beta.12" << "V0.10.0-beta.12";
        QTest::newRow("signed tester build") << "development-262ba2b-0.8.1-rc1" << "V0.8.1-rc1 262ba2b";
        QTest::newRow("signed, branch with dashes")
            << "fix-qt-review-findings-a7fefb1-0.8.0-rc3" << "V0.8.0-rc3 a7fefb1";
        QTest::newRow("unsigned CI build") << "development-262ba2b" << "development 262ba2b";
        QTest::newRow("pull request build") << "pr190-abc1234" << "pr190 abc1234";
        QTest::newRow("local dev string") << "dev" << "Vdev";
    }

    void versionLabelText() {
        QFETCH(QString, version);
        QFETCH(QString, label);
        QCOMPARE(VersionText::panelLabel(version), label);
    }

    void versionSitsOnOneLineUnderTheIcons_data() {
        QTest::addColumn<QString>("version");
        QTest::newRow("pre-release") << "0.10.0-beta.12";
        QTest::newRow("signed tester build") << "development-262ba2b-0.8.1-rc1";
        QTest::newRow("unsigned CI build") << "development-262ba2b";
        QTest::newRow("long branch, elided") << "feature-a-very-long-branch-name-for-testing-262ba2b";
    }

    void versionSitsOnOneLineUnderTheIcons() {
        // One readable line under the ? and globe keys, left-aligned with them and on the panel's
        // bottom edge, costing the panel exactly that line. The stacked form beside the icons made a
        // tester build four clipped lines.
        QFETCH(QString, version);
        const QString saved = QCoreApplication::applicationVersion();
        QCoreApplication::setApplicationVersion(QString());
        const int heightWithout = SideControlPanel().minimumSizeHint().height();
        QCoreApplication::setApplicationVersion(version);
        QWidget host;
        auto *panel = new SideControlPanel(&host);
        panel->setGeometry(0, 0, panel->width(), panel->minimumSizeHint().height());
        host.resize(panel->size());
        host.show();
        QCoreApplication::processEvents();
        QCoreApplication::setApplicationVersion(saved);

        const QString shown = VersionText::panelLabel(version);
        QLabel *label = nullptr;
        for (QLabel *candidate : panel->findChildren<QLabel *>())
            if (candidate->text() == shown || candidate->text().endsWith(QChar(0x2026)))
                label = candidate;
        QVERIFY2(label, qPrintable(shown));
        QVERIFY(!label->text().contains(QLatin1Char('\n')));
        QPushButton *icon = nullptr;
        for (QPushButton *button : panel->findChildren<QPushButton *>(Qt::FindDirectChildrenOnly))
            if (button->size() ==
                QSize(K4Styles::Dimensions::SideIconButtonSize, K4Styles::Dimensions::SideIconButtonSize))
                icon = button;
        QVERIFY(icon);

        const int textWidth = qCeil(QFontMetricsF(label->font()).horizontalAdvance(label->text()));
        QVERIFY2(label->geometry().left() + textWidth <= panel->width() - K4Styles::Dimensions::SidePanelMarginH,
                 qPrintable(QString("\"%1\" needs %2 px from x=%3 in a %4 px panel")
                                .arg(label->text())
                                .arg(textWidth)
                                .arg(label->geometry().left())
                                .arg(panel->width())));
        QCOMPARE(label->geometry().left(), K4Styles::Dimensions::SidePanelMarginH);
        QVERIFY(label->geometry().top() > icon->geometry().bottom());
        QCOMPARE(label->geometry().bottom() + 1, panel->height() - K4Styles::Dimensions::SidePanelMarginV);
        QCOMPARE(panel->minimumSizeHint().height(),
                 heightWithout + label->sizeHint().height() + K4Styles::Dimensions::SidePanelItemSpacing);
    }
};

// Every key's text stands K4Styles::Dimensions::ButtonTextClearance inside its border, measured in the
// font the key is drawn with: both panels, the KPA1500 card, the memory row and the bottom menu bar.
class KeyTextClearanceTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_tempDir;
    QWidget m_host;
    QWidget *m_memoryRow = nullptr;
    MemoryKeyRow::Keys m_memoryKeys;

    // The row width MainWindow gives the memory keys: the centre column inside its margins.
    static constexpr int kMemoryRowWidth = 322;

    static int borderOf(const QPushButton *key) {
        return key->styleSheet() == K4Styles::compactButton() ? K4Styles::Dimensions::SideCompactBorderWidth
                                                              : K4Styles::Dimensions::BorderWidth;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
        qApp->setStyleSheet(K4Styles::applicationStyleSheet());

        auto *grid = new QGridLayout(&m_host);
        grid->setContentsMargins(0, 0, 0, 0);
        auto *left = new SideControlPanel;
        auto *right = new RightSidePanel;
        auto *kpa = new Kpa1500MiniPanel;
        right->embedKpa1500Panel(kpa);
        right->setKpa1500SlotReserved(true);
        m_memoryRow = new QWidget;
        m_memoryRow->setFixedWidth(kMemoryRowWidth);
        m_memoryRow->setLayout(MemoryKeyRow::create(m_memoryRow, kMemoryRowWidth, m_memoryKeys));
        grid->addWidget(left, 0, 0, 2, 1);
        grid->addWidget(m_memoryRow, 0, 1);
        grid->addWidget(right, 0, 2);
        grid->addWidget(new BottomMenuBar, 1, 1, 1, 2);
        m_host.resize(m_host.minimumSizeHint());
        m_host.show();
        QCoreApplication::processEvents();
    }

    void everyKeyClearsItsText() {
        int checked = 0;
        for (QPushButton *key : m_host.findChildren<QPushButton *>()) {
            if (!key->isVisible() || key->text().isEmpty())
                continue;
            const QFontMetricsF metrics(key->font(), key);
            qreal widest = 0;
            for (const QString &line : key->text().split('\n'))
                widest = qMax(widest, metrics.horizontalAdvance(line));
            const qreal clearance = (key->width() - widest) / 2 - borderOf(key);
            QVERIFY2(clearance >= K4Styles::Dimensions::ButtonTextClearance,
                     qPrintable(QString("'%1': %2 px each side, %3 wide")
                                    .arg(QString(key->text()).replace('\n', ' '))
                                    .arg(clearance)
                                    .arg(key->width())));
            ++checked;
        }
        // 6 + 3 left, 18 + 4 right, 7 memory, 8 menu bar.
        QCOMPARE(checked, 46);
    }

    void memoryKeysAreTwoSetsThatSpanTheRow() {
        const QList<QPushButton *> message{m_memoryKeys.m1, m_memoryKeys.m2, m_memoryKeys.m3, m_memoryKeys.m4};
        const QList<QPushButton *> recall{m_memoryKeys.rec, m_memoryKeys.store, m_memoryKeys.rcl};
        for (QPushButton *key : message)
            QCOMPARE(key->size(), message.first()->size());
        for (QPushButton *key : recall)
            QCOMPARE(key->size(), recall.first()->size());
        const int left = m_memoryKeys.m1->mapTo(m_memoryRow, QPoint()).x();
        const int right = m_memoryKeys.rcl->mapTo(m_memoryRow, QPoint()).x() + m_memoryKeys.rcl->width();
        QVERIFY2(right - left <= kMemoryRowWidth && right - left > kMemoryRowWidth - 4,
                 qPrintable(QString("%1..%2").arg(left).arg(right)));
    }

    void memorySubLabelsDoNotTouch() {
        // AF REC and AF PLAY under neighbouring keys: each fits its own key's width.
        for (QPushButton *key : {m_memoryKeys.rec, m_memoryKeys.store, m_memoryKeys.rcl}) {
            QLabel *label = key->parentWidget()->findChild<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
            QVERIFY(label);
            const qreal needed = QFontMetricsF(label->font(), label).horizontalAdvance(label->text());
            // 2 px: the same allowance the panel sub-labels get, so neighbours never touch.
            QVERIFY2(needed + 2 <= label->width(), qPrintable(label->text()));
        }
    }

    void modeLabelsAreFullSizeClickTargets() {
        VfoRowWidget row;
        for (QLabel *mode : {row.modeALabel(), row.modeBLabel()}) {
            QVERIFY(mode->height() >= K4Styles::Dimensions::MinClickTarget ||
                    mode->minimumHeight() >= K4Styles::Dimensions::MinClickTarget);
            QVERIFY(mode->width() >= K4Styles::Dimensions::MinClickTarget);
        }
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    int status = 0;
    {
        SideControlPanelLayoutTests layout;
        status |= QTest::qExec(&layout, argc, argv);
    }
    {
        SidePanelButtonStandardTests standard;
        status |= QTest::qExec(&standard, argc, argv);
    }
    {
        KeyTextClearanceTests clearance;
        status |= QTest::qExec(&clearance, argc, argv);
    }
    return status;
}
#include "test_sidecontrolpanel.moc"

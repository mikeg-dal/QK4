// Graphic-equalizer popup: the band column's behaviour, and the opacity invariant that keeps a
// popup's repaints from leaving seams.
//
// WHY the invariant is a structural check and not a pixel diff: QWidget::grab() renders into a
// fresh pixmap and never touches the backing store the defect lives in, so a rendered comparison
// passes whether or not the bug is present. What can be checked is the rule that prevents it -
// every widget inside a popup paints all of its own rect - and that is what these cases assert.

#include "ui/popups/k4popupbase.h"
#include "ui/popups/rxeqpopupwidget.h"
#include "ui/styling/k4constants.h"
#include "ui/widgets/featuremenubar.h"

#include <QLayout>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

using namespace K4Styles::Dimensions;

namespace {

// Reaches the two protected members a popup's own code uses, so the tests can check them.
class EqProbe : public RxEqPopupWidget {
public:
    EqProbe() : RxEqPopupWidget(QStringLiteral("RX GRAPHIC EQUALIZER"), K4Styles::Colors::VfoACyan) {}
    QWidget *panel() { return contentPanel(); }
    QSize declaredContentSize() const { return contentSize(); }
};

class BarProbe : public FeatureMenuBar {
public:
    QWidget *panel() { return contentPanel(); }
};

// The band column's zones, recomputed here from the tokens rather than read off the widget, so a
// change to either side has to be made deliberately in both.
int bodyTop() {
    return EqValueHeight + EqColumnGap + EqCapHeight;
}
int bodyBottom() {
    return EqValueHeight + EqColumnGap + EqTrackHeight - EqCapHeight - 1;
}
QPoint inPlusCap() {
    return QPoint(EqBandWidth / 2, EqValueHeight + EqColumnGap + EqCapHeight / 2);
}
QPoint inMinusCap() {
    return QPoint(EqBandWidth / 2, EqValueHeight + EqColumnGap + EqTrackHeight - EqCapHeight / 2);
}
QPoint onKey() {
    return QPoint(EqBandWidth / 2, EqValueHeight + EqColumnGap + EqTrackHeight + EqColumnGap + EqKeyHeight / 2);
}

} // namespace

class EqPopupTests : public QObject {
    Q_OBJECT

private slots:
    // --- the opacity invariant: the regression gate on the repaint seams -----------------------

    void eqPanelAndEveryBandPaintAllTheirPixels() {
        EqProbe eq;
        QVERIFY(eq.panel() != nullptr);
        QVERIFY2(eq.panel()->testAttribute(Qt::WA_OpaquePaintEvent),
                 "the content panel is the opaque ancestor a child's repaint is painted against");

        const QList<EqBandWidget *> bands = eq.panel()->findChildren<EqBandWidget *>();
        QCOMPARE(bands.size(), 8);
        for (const EqBandWidget *band : bands)
            QVERIFY2(band->testAttribute(Qt::WA_OpaquePaintEvent), "a band column must own every pixel it covers");
    }

    void everyEqWidgetLivesOnThePanelAndNotOnThePopup() {
        EqProbe eq;
        // Direct children of the popup: the panel and the dismissal helper, nothing that paints.
        for (QObject *child : eq.children()) {
            auto *widget = qobject_cast<QWidget *>(child);
            if (widget)
                QCOMPARE(widget, eq.panel());
        }
    }

    void theFeatureBarPanelIsOpaqueToo() {
        BarProbe bar;
        QVERIFY(bar.panel() != nullptr);
        QVERIFY(bar.panel()->testAttribute(Qt::WA_OpaquePaintEvent));
        // Its value key's sheet is `background: transparent`; on the panel that is harmless.
        QVERIFY(!bar.panel()->findChildren<QPushButton *>().isEmpty());
        for (QObject *child : bar.children()) {
            auto *widget = qobject_cast<QWidget *>(child);
            if (widget)
                QCOMPARE(widget, bar.panel());
        }
    }

    void contentSizeMatchesTheLayoutItDescribes() {
        EqProbe eq;
        // A hand-counted content size that drifts from the layout leaves ground inside the card
        // that no widget paints - the second way to grow the seams.
        QCOMPARE(eq.panel()->layout()->sizeHint(), eq.declaredContentSize());
        QCOMPARE(eq.panel()->size(), eq.declaredContentSize());
    }

    // --- the band column ------------------------------------------------------------------------

    void aPressOnTheBodyTakesTheGripThere() {
        EqProbe eq;
        auto *band = eq.panel()->findChildren<EqBandWidget *>().at(0);
        QSignalSpy spy(&eq, &RxEqPopupWidget::bandValueChanged);

        QTest::mousePress(band, Qt::LeftButton, {}, QPoint(EqBandWidth / 2, bodyTop()));
        QCOMPARE(band->value(), 16);
        QTest::mouseRelease(band, Qt::LeftButton, {}, QPoint(EqBandWidth / 2, bodyTop()));

        QTest::mousePress(band, Qt::LeftButton, {}, QPoint(EqBandWidth / 2, bodyBottom()));
        QCOMPARE(band->value(), -16);
        QTest::mouseRelease(band, Qt::LeftButton, {}, QPoint(EqBandWidth / 2, bodyBottom()));

        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(0).at(1).toInt(), 16);
        QCOMPARE(spy.at(1).at(1).toInt(), -16);
    }

    void aPressOnTheMiddleOfTheBodyReadsAboutZero() {
        EqProbe eq;
        auto *band = eq.panel()->findChildren<EqBandWidget *>().at(0);
        QTest::mouseClick(band, Qt::LeftButton, {}, QPoint(EqBandWidth / 2, (bodyTop() + bodyBottom()) / 2));
        QVERIFY2(qAbs(band->value()) <= 1, qPrintable(QStringLiteral("middle read %1 dB").arg(band->value())));
    }

    void theCapsStepByOneAndStopAtTheEnds() {
        EqProbe eq;
        auto *band = eq.panel()->findChildren<EqBandWidget *>().at(3);

        QTest::mouseClick(band, Qt::LeftButton, {}, inPlusCap());
        QCOMPARE(band->value(), 1);
        QTest::mouseClick(band, Qt::LeftButton, {}, inMinusCap());
        QTest::mouseClick(band, Qt::LeftButton, {}, inMinusCap());
        QCOMPARE(band->value(), -1);

        QSignalSpy spy(&eq, &RxEqPopupWidget::bandValueChanged);
        for (int i = 0; i < 20; ++i)
            QTest::mouseClick(band, Qt::LeftButton, {}, inPlusCap());
        QCOMPARE(band->value(), 16);
        // 17 presses move it from -1 to +16; the last three find it already there and say nothing.
        QCOMPARE(spy.size(), 17);
    }

    void theWheelStepsTheBandUnderIt() {
        EqProbe eq;
        auto *band = eq.panel()->findChildren<EqBandWidget *>().at(5);
        const QPoint pos(EqBandWidth / 2, bodyTop() + 10);

        QWheelEvent up(QPointF(pos), band->mapToGlobal(pos), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(band, &up);
        QCOMPARE(band->value(), 1);

        QWheelEvent down(QPointF(pos), band->mapToGlobal(pos), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
        QApplication::sendEvent(band, &down);
        QCOMPARE(band->value(), 0);
    }

    void setAllBandsHoldsTheRange() {
        EqProbe eq;
        eq.setAllBands({40, -40, 5, 0, 0, 0, 0, 0});
        const QList<EqBandWidget *> bands = eq.panel()->findChildren<EqBandWidget *>();
        QCOMPARE(bands.at(0)->value(), 16);
        QCOMPARE(bands.at(1)->value(), -16);
        QCOMPARE(bands.at(2)->value(), 5);
    }

    // --- the lit frequency key --------------------------------------------------------------------

    void theLitKeyFollowsTheBandLastTouched() {
        EqProbe eq;
        const QList<EqBandWidget *> bands = eq.panel()->findChildren<EqBandWidget *>();
        QVERIFY2(bands.at(0)->isSelected(), "the first band is lit before anything is touched");

        QTest::mouseClick(bands.at(4), Qt::LeftButton, {}, inPlusCap());
        QVERIFY(bands.at(4)->isSelected());
        QVERIFY(!bands.at(0)->isSelected());

        // Exactly one at a time: the radio lights one key, never two.
        int lit = 0;
        for (const EqBandWidget *band : bands)
            lit += band->isSelected() ? 1 : 0;
        QCOMPARE(lit, 1);
    }

    void aPressOnTheKeySelectsWithoutMovingTheValue() {
        EqProbe eq;
        const QList<EqBandWidget *> bands = eq.panel()->findChildren<EqBandWidget *>();
        bands.at(6)->setValue(7);
        QSignalSpy spy(&eq, &RxEqPopupWidget::bandValueChanged);

        QTest::mouseClick(bands.at(6), Qt::LeftButton, {}, onKey());
        QVERIFY(bands.at(6)->isSelected());
        QCOMPARE(bands.at(6)->value(), 7);
        QCOMPARE(spy.size(), 0);
    }

    // --- FLAT ---------------------------------------------------------------------------------

    void flatZeroesEveryBandAndAsksOnce() {
        EqProbe eq;
        eq.setAllBands({3, -4, 5, -6, 7, -8, 9, -10});
        QSignalSpy flat(&eq, &RxEqPopupWidget::flatRequested);
        QSignalSpy perBand(&eq, &RxEqPopupWidget::bandValueChanged);

        QPushButton *flatBtn = nullptr;
        for (QPushButton *btn : eq.panel()->findChildren<QPushButton *>())
            if (btn->text() == QLatin1String("FLAT"))
                flatBtn = btn;
        QVERIFY(flatBtn != nullptr);
        QTest::mouseClick(flatBtn, Qt::LeftButton);

        for (const EqBandWidget *band : eq.panel()->findChildren<EqBandWidget *>())
            QCOMPARE(band->value(), 0);
        QCOMPARE(flat.size(), 1);
        // FLAT is one command to the radio, not eight.
        QCOMPARE(perBand.size(), 0);
    }
};

QTEST_MAIN(EqPopupTests)
#include "test_eqpopup.moc"

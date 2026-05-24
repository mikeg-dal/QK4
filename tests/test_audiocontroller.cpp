#include "controllers/audiocontroller.h"

#include <QtTest/QtTest>

class TestAudioController : public QObject {
    Q_OBJECT

private slots:
    void testMonoMixCommandSelection() {
        QCOMPARE(AudioController::monoMixCommand(true), QString("MXAB.AB;"));
        QCOMPARE(AudioController::monoMixCommand(false), QString("MXA.B;"));
    }

    void testAudioMixCommandSelection() {
        QCOMPARE(AudioController::audioMixCommand(0, 1), QString("MXA.B;"));
        QCOMPARE(AudioController::audioMixCommand(2, 2), QString("MXAB.AB;"));
        QCOMPARE(AudioController::audioMixCommand(0, 3), QString("MXA.-A;"));
        QCOMPARE(AudioController::audioMixCommand(1, 0), QString("MXB.A;"));
    }
};

QTEST_MAIN(TestAudioController)
#include "test_audiocontroller.moc"

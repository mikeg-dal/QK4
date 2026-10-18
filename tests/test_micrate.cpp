#include <QtTest>

#include "audio/micrate.h"

// The wording an operator reads when a microphone cannot be captured from, and the rates that
// decide it. See audio/micrate.h for why this is worth testing: the message IS the fix - the rate
// was already refused and logged before any of this existed.
class TestMicRate : public QObject {
    Q_OBJECT

private slots:
    void theRatesQk4CanCaptureAreAccepted();
    void theRatesFromTheFieldReportsAreRejected();
    void aRejectionNamesTheRateAndWhatToSetItTo();
    void anAcceptedRateSaysNothingAtAll();
    void aMissingRateIsRejectedWithoutPrintingZero();
    void theBadgeMarksOnlyUnusableDevices();
    void theSilentMicMessageNamesBothCausesAndWhatToDo();
};

void TestMicRate::theSilentMicMessageNamesBothCausesAndWhatToDo() {
    const QString m = MicRate::micDeliveringNothing();
    QVERIFY(!m.isEmpty());

    // The symptom as the operator experiences it, so they recognise the message as being about
    // them: a PTT that does nothing looks like a radio fault, not an audio-device fault.
    QVERIFY2(m.contains(QStringLiteral("not producing any audio")), qUtf8Printable(m));
    QVERIFY2(m.contains(QStringLiteral("transmitted")), qUtf8Printable(m));

    // Cause one: a sample-rate change. QK4 cannot recover from it - Qt caches the device format for
    // the life of the process - so the restart has to be stated outright.
    QVERIFY2(m.contains(QStringLiteral("sample rate")), qUtf8Printable(m));
    QVERIFY2(m.contains(QStringLiteral("restart")), qUtf8Printable(m));

    // Cause two: INT-005's Bluetooth profile conflict. Named because from the operator's chair the
    // two are indistinguishable.
    QVERIFY2(m.contains(QStringLiteral("Bluetooth")), qUtf8Printable(m));
}

void TestMicRate::theRatesQk4CanCaptureAreAccepted() {
    // Exact multiples of the K4's 12 kHz TX rate. 24000 is the one that matters in the field: an
    // AirPods Pro in HFP runs there, and INT-005 was caused by assuming 48000.
    QVERIFY(MicRate::isUsable(48000));
    QVERIFY(MicRate::isUsable(24000));
    QVERIFY(MicRate::isUsable(12000));
    QVERIFY(MicRate::isUsable(96000));
}

void TestMicRate::theRatesFromTheFieldReportsAreRejected() {
    // 44100 is the Mac Studio display microphone and the Windows report; 16000 is older AirPods,
    // called out in audiodecimator.h as needing 4:3 rational resampling.
    QVERIFY(!MicRate::isUsable(44100));
    QVERIFY(!MicRate::isUsable(16000));
    QVERIFY(!MicRate::isUsable(22050));
    QVERIFY(!MicRate::isUsable(8000));
}

void TestMicRate::aRejectionNamesTheRateAndWhatToSetItTo() {
    const QString reason = MicRate::problem(44100);
    QVERIFY(!reason.isEmpty());
    // The rate the operator has, so they can match it to what the OS is showing them.
    QVERIFY2(reason.contains(QStringLiteral("44100")), qUtf8Printable(reason));
    // The rate to change it to. Without this the message says what is wrong and not what to do.
    QVERIFY2(reason.contains(QStringLiteral("48000")), qUtf8Printable(reason));
    // PTT is the symptom that gets reported, so the message has to connect itself to it - otherwise
    // an operator with a dead PTT does not realise this message is about them.
    QVERIFY2(reason.contains(QStringLiteral("PTT")), qUtf8Printable(reason));
}

void TestMicRate::anAcceptedRateSaysNothingAtAll() {
    // An empty string is the signal that there is nothing to show. A working device must not get a
    // reassuring message, or the real one stops standing out.
    QVERIFY(MicRate::problem(48000).isEmpty());
    QVERIFY(MicRate::problem(24000).isEmpty());
}

void TestMicRate::aMissingRateIsRejectedWithoutPrintingZero() {
    // A device that reports no rate at all. Rejected, but the message must not read "runs at 0 Hz",
    // which sounds like a different fault and sends the operator looking for the wrong thing.
    const QString reason = MicRate::problem(0);
    QVERIFY(!reason.isEmpty());
    QVERIFY2(!reason.contains(QStringLiteral("0 Hz")), qUtf8Printable(reason));
    QVERIFY(!MicRate::problem(-1).isEmpty());
}

void TestMicRate::theBadgeMarksOnlyUnusableDevices() {
    // A usable device's name is left exactly as the OS gives it.
    QVERIFY(MicRate::badge(48000).isEmpty());
    QVERIFY(MicRate::badge(24000).isEmpty());

    const QString badge = MicRate::badge(44100);
    QVERIFY(!badge.isEmpty());
    QVERIFY2(badge.contains(QStringLiteral("44100")), qUtf8Printable(badge));

    // Still marked when there is no rate to print.
    QVERIFY(!MicRate::badge(0).isEmpty());
}

QTEST_MAIN(TestMicRate)
#include "test_micrate.moc"

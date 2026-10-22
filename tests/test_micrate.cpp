#include <QtTest>

#include "audio/micrate.h"

// The wording an operator reads when a microphone cannot be captured from, and the rates that
// decide it. See audio/micrate.h for why this is worth testing: the message IS the fix - the rate
// was already refused and logged before any of this existed.
class TestMicRate : public QObject {
    Q_OBJECT

private slots:
    void theRatesQk4CanCaptureAreAccepted();
    void theRatesFromTheFieldReportsAreAccepted();
    void aRejectionNamesTheRateAndWhatToSetItTo();
    void anAcceptedRateSaysNothingAtAll();
    void aMissingRateIsRejectedWithoutPrintingZero();
    void theOtherDeadMicCausesSayWhatToDo();
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
    // Exact multiples of the K4's 12 kHz TX rate, decimated. 24000 is an AirPods Pro in HFP, and
    // INT-005 was caused by assuming 48000.
    QVERIFY(MicRate::isUsable(48000));
    QVERIFY(MicRate::isUsable(24000));
    QVERIFY(MicRate::isUsable(12000));
    QVERIFY(MicRate::isUsable(96000));
}

void TestMicRate::theRatesFromTheFieldReportsAreAccepted() {
    // The regression this file exists for. 44100 is #180 and the Mac Studio display microphone;
    // 16000 and 8000 are Bluetooth headsets in call mode. All were refused before the resampler, and
    // a message that still called them unusable would send an operator to change a setting that is
    // fine.
    for (const int rate : {44100, 16000, 22050, 8000, 11025, 32000, 88200, 176400}) {
        QVERIFY2(MicRate::isUsable(rate), qUtf8Printable(QString::number(rate)));
        QVERIFY2(MicRate::problem(rate).isEmpty(), qUtf8Printable(QString::number(rate)));
    }
}

void TestMicRate::aRejectionNamesTheRateAndWhatToSetItTo() {
    // 44101 Hz is a rate the resampler declines (see MAX_INTERPOLATION) - the only kind still refused.
    QVERIFY(!MicRate::isUsable(44101));
    const QString reason = MicRate::problem(44101);
    QVERIFY(!reason.isEmpty());
    // The rate the operator has, so they can match it to what the OS is showing them.
    QVERIFY2(reason.contains(QStringLiteral("44101")), qUtf8Printable(reason));
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

void TestMicRate::theOtherDeadMicCausesSayWhatToDo() {
    // Both end in "nothing is transmitted", so an operator with a dead PTT recognises them, and
    // both point at the one setting that fixes them.
    const QString none = MicRate::noMicrophone();
    QVERIFY2(none.contains(QStringLiteral("transmitted")), qUtf8Printable(none));
    QVERIFY2(none.contains(QStringLiteral("Audio Input")), qUtf8Printable(none));

    const QString named = MicRate::micWouldNotOpen(QStringLiteral("Koss SB45"));
    QVERIFY2(named.contains(QStringLiteral("\"Koss SB45\"")), qUtf8Printable(named));
    QVERIFY2(named.contains(QStringLiteral("transmitted")), qUtf8Printable(named));
    QVERIFY2(named.contains(QStringLiteral("Audio Input")), qUtf8Printable(named));

    // A device that vanished has no name to print; the message must not show empty quotes.
    const QString unnamed = MicRate::micWouldNotOpen(QString());
    QVERIFY2(!unnamed.contains(QStringLiteral("\"\"")), qUtf8Printable(unnamed));
    QVERIFY2(unnamed.startsWith(QStringLiteral("The microphone would not open")), qUtf8Printable(unnamed));
}

QTEST_MAIN(TestMicRate)
#include "test_micrate.moc"

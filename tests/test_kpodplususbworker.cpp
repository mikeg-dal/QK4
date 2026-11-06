// Unit tests for KpodPlusUsbWorker pure logic — command builders, EP02 trim,
// EP01 response decoder state machine. Does NOT exercise libusb I/O (no
// hardware required); libusb is linked because the worker's translation unit
// includes <libusb-1.0/libusb.h>.

#include "hardware/kpodplususbworker.h"
#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>
#include <cstring>
#include <thread>

class TestKpodPlusUsbWorker : public QObject {
    Q_OBJECT

private slots:
    // --- Command builders ----------------------------------------------------
    void buildKeyerSpeed_clampsBelowMin();
    void buildKeyerSpeed_passthroughMid();
    void buildKeyerSpeed_clampsAboveMax();
    void buildCwPitch_clampsAndScales();
    void buildKeyerParams_iambicAandReversed();
    void buildKeyerParams_iambicB();
    void buildEncodeMode_boundary();
    void buildStuckTimeout_lowHighBytes();

    // --- EP02 trim -----------------------------------------------------------
    void trimEp02_dropsTrailingNuls();
    void trimEp02_emptyAndAllNul();
    void trimEp02_noTrailing();

    // --- EP01 response decoder state machine ---------------------------------
    void decode_noEventNoState();
    void decode_encoderTicksPositive();
    void decode_encoderTicksNegative();
    void decode_buttonPressThenReleaseEmitsTap();
    void decode_buttonHoldTransition();
    void decode_rockerChangeEmitsOnce();
    void decode_rockerErrorStateIgnored();

    // --- EP02 reader with no device ------------------------------------------
    void ep02IdleReaderStopsAtOnce();
};

// =============================================================================
// Builders
// =============================================================================

void TestKpodPlusUsbWorker::buildKeyerSpeed_clampsBelowMin() {
    unsigned char buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    KpodPlusUsbWorker::buildKeyerSpeedCmd(3, buf);
    QCOMPARE(buf[0], static_cast<unsigned char>('K'));
    QCOMPARE(buf[1], static_cast<unsigned char>(0x03));
    QCOMPARE(buf[2], static_cast<unsigned char>(8)); // clamp lower bound
    for (int i = 3; i < 8; ++i)
        QCOMPARE(buf[i], static_cast<unsigned char>(0));
}

void TestKpodPlusUsbWorker::buildKeyerSpeed_passthroughMid() {
    unsigned char buf[8];
    KpodPlusUsbWorker::buildKeyerSpeedCmd(25, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(25));
}

void TestKpodPlusUsbWorker::buildKeyerSpeed_clampsAboveMax() {
    unsigned char buf[8];
    KpodPlusUsbWorker::buildKeyerSpeedCmd(200, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(100));
}

void TestKpodPlusUsbWorker::buildCwPitch_clampsAndScales() {
    unsigned char buf[8];
    // 600 Hz → 60 (tens-of-Hz). Within range.
    KpodPlusUsbWorker::buildCwPitchCmd(600, buf);
    QCOMPARE(buf[0], static_cast<unsigned char>('K'));
    QCOMPARE(buf[1], static_cast<unsigned char>(0x04));
    QCOMPARE(buf[2], static_cast<unsigned char>(60));

    // 200 Hz → 20, clamped up to 40 (= 400 Hz lower bound).
    KpodPlusUsbWorker::buildCwPitchCmd(200, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(40));

    // 1500 Hz → 150, clamped down to 100 (= 1000 Hz upper bound).
    KpodPlusUsbWorker::buildCwPitchCmd(1500, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(100));
}

void TestKpodPlusUsbWorker::buildKeyerParams_iambicAandReversed() {
    unsigned char buf[8];
    KpodPlusUsbWorker::buildKeyerParamsCmd(0, true, buf);
    QCOMPARE(buf[0], static_cast<unsigned char>('K'));
    QCOMPARE(buf[1], static_cast<unsigned char>(0x01));
    QCOMPARE(buf[2], static_cast<unsigned char>(0)); // iambic A
    QCOMPARE(buf[3], static_cast<unsigned char>(1)); // reversed
}

void TestKpodPlusUsbWorker::buildKeyerParams_iambicB() {
    unsigned char buf[8];
    KpodPlusUsbWorker::buildKeyerParamsCmd(1, false, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(1));
    QCOMPARE(buf[3], static_cast<unsigned char>(0));
}

void TestKpodPlusUsbWorker::buildEncodeMode_boundary() {
    unsigned char buf[8];
    KpodPlusUsbWorker::buildEncodeModeCmd(0, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(0));
    KpodPlusUsbWorker::buildEncodeModeCmd(1, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(1));
    KpodPlusUsbWorker::buildEncodeModeCmd(7, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(1)); // clamped
    KpodPlusUsbWorker::buildEncodeModeCmd(-5, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(0)); // clamped
}

void TestKpodPlusUsbWorker::buildStuckTimeout_lowHighBytes() {
    unsigned char buf[8];
    // 300 → lo=0x2C, hi=0x01 (300 = 0x012C)
    KpodPlusUsbWorker::buildStuckTimeoutCmd(300, buf);
    QCOMPARE(buf[0], static_cast<unsigned char>('K'));
    QCOMPARE(buf[1], static_cast<unsigned char>(0x05));
    QCOMPARE(buf[2], static_cast<unsigned char>(0x2C));
    QCOMPARE(buf[3], static_cast<unsigned char>(0x01));

    // Clamp lower: 1 → 5
    KpodPlusUsbWorker::buildStuckTimeoutCmd(1, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(5));
    QCOMPARE(buf[3], static_cast<unsigned char>(0));

    // Clamp upper: 5000 → 600
    KpodPlusUsbWorker::buildStuckTimeoutCmd(5000, buf);
    QCOMPARE(buf[2], static_cast<unsigned char>(600 & 0xFF));
    QCOMPARE(buf[3], static_cast<unsigned char>((600 >> 8) & 0xFF));
}

// =============================================================================
// EP02 trim
// =============================================================================

void TestKpodPlusUsbWorker::trimEp02_dropsTrailingNuls() {
    // Build a 32-byte buffer that mirrors what the device delivers on EP02:
    // an 8-byte payload followed by 24 NUL bytes of zero-padding.
    unsigned char raw[32] = {};
    const char prefix[] = "KZ.;KZ-;";
    memcpy(raw, prefix, 8);
    QCOMPARE(KpodPlusUsbWorker::ep02PayloadLength(raw, 32), 8);
}

void TestKpodPlusUsbWorker::trimEp02_emptyAndAllNul() {
    const unsigned char zeros[32] = {};
    QCOMPARE(KpodPlusUsbWorker::ep02PayloadLength(zeros, 0), 0);
    QCOMPARE(KpodPlusUsbWorker::ep02PayloadLength(zeros, 32), 0);
}

void TestKpodPlusUsbWorker::trimEp02_noTrailing() {
    const unsigned char raw[] = {'K', 'Z', '.', ';'};
    QCOMPARE(KpodPlusUsbWorker::ep02PayloadLength(raw, 4), 4);
}

// =============================================================================
// Response decoder state machine
// =============================================================================

void TestKpodPlusUsbWorker::decode_noEventNoState() {
    KpodPlusUsbWorker w;
    const unsigned char buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const auto ev = w.decodeResponse(buf);
    QVERIFY(!ev.emitEncoder);
    QVERIFY(!ev.emitButtonTap);
    QVERIFY(!ev.emitButtonHold);
    QVERIFY(!ev.emitRocker);
}

void TestKpodPlusUsbWorker::decode_encoderTicksPositive() {
    KpodPlusUsbWorker w;
    // cmd='u', ticks = 5 (LE), no controls
    const unsigned char buf[8] = {'u', 0x05, 0x00, 0x00, 0, 0, 0, 0};
    const auto ev = w.decodeResponse(buf);
    QVERIFY(ev.emitEncoder);
    QCOMPARE(ev.encoderTicks, 5);
}

void TestKpodPlusUsbWorker::decode_encoderTicksNegative() {
    KpodPlusUsbWorker w;
    // -3 in int16 LE: 0xFD 0xFF
    const unsigned char buf[8] = {'u', 0xFD, 0xFF, 0x00, 0, 0, 0, 0};
    const auto ev = w.decodeResponse(buf);
    QVERIFY(ev.emitEncoder);
    QCOMPARE(ev.encoderTicks, -3);
}

void TestKpodPlusUsbWorker::decode_buttonPressThenReleaseEmitsTap() {
    KpodPlusUsbWorker w;
    // Press button 2 (tap, not hold). controls = 0x02
    const unsigned char press[8] = {'u', 0, 0, 0x02, 0, 0, 0, 0};
    auto ev = w.decodeResponse(press);
    QVERIFY(!ev.emitButtonHold);
    QVERIFY(!ev.emitButtonTap); // tap only fires on release

    // No event next cycle: implicit release.
    const unsigned char idle[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    ev = w.decodeResponse(idle);
    QVERIFY(ev.emitButtonTap);
    QCOMPARE(ev.buttonNum, 2);
}

void TestKpodPlusUsbWorker::decode_buttonHoldTransition() {
    KpodPlusUsbWorker w;
    // Press button 3 (tap state).
    const unsigned char press[8] = {'u', 0, 0, 0x03, 0, 0, 0, 0};
    auto ev = w.decodeResponse(press);
    QVERIFY(!ev.emitButtonHold);

    // Now hold flag flips on (bit 4). controls = 0x13 = button 3 + hold bit.
    const unsigned char hold[8] = {'u', 0, 0, 0x13, 0, 0, 0, 0};
    ev = w.decodeResponse(hold);
    QVERIFY(ev.emitButtonHold);
    QCOMPARE(ev.buttonNum, 3);

    // Release after hold: no tap (because we already emitted hold).
    const unsigned char idle[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    ev = w.decodeResponse(idle);
    QVERIFY(!ev.emitButtonTap);
}

void TestKpodPlusUsbWorker::decode_rockerChangeEmitsOnce() {
    KpodPlusUsbWorker w;
    // controls bits 6-5 = rocker. 0b01_0_0000 = 0x20 → rocker=1 (right)
    const unsigned char right1[8] = {'u', 0, 0, 0x20, 0, 0, 0, 0};
    auto ev = w.decodeResponse(right1);
    QVERIFY(ev.emitRocker);
    QCOMPARE(ev.rockerPosition, 1);

    // Same rocker value again → no emission.
    const unsigned char right2[8] = {'u', 0, 0, 0x20, 0, 0, 0, 0};
    ev = w.decodeResponse(right2);
    QVERIFY(!ev.emitRocker);

    // Switch to left (rocker=2). 0b10_0_0000 = 0x40.
    const unsigned char left[8] = {'u', 0, 0, 0x40, 0, 0, 0, 0};
    ev = w.decodeResponse(left);
    QVERIFY(ev.emitRocker);
    QCOMPARE(ev.rockerPosition, 2);
}

void TestKpodPlusUsbWorker::decode_rockerErrorStateIgnored() {
    KpodPlusUsbWorker w;
    // controls = 0b11_0_0000 = 0x60 → rocker=3 (error). Should not emit.
    const unsigned char err[8] = {'u', 0, 0, 0x60, 0, 0, 0, 0};
    const auto ev = w.decodeResponse(err);
    QVERIFY(!ev.emitRocker);
}

// Without a KPOD+ the EP02 reader waits rather than polling (#92): a handle cleared again keeps it
// waiting, and requestStop() ends the wait at once instead of after a poll period.
void TestKpodPlusUsbWorker::ep02IdleReaderStopsAtOnce() {
    KpodPlusEp02Worker worker;
    std::thread reader([&worker] { worker.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    worker.setDeviceHandle(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    QElapsedTimer t;
    t.start();
    worker.requestStop();
    reader.join();
    QVERIFY2(t.elapsed() < 15, qPrintable(QString::number(t.elapsed()) + " ms"));
}

QTEST_GUILESS_MAIN(TestKpodPlusUsbWorker)
#include "test_kpodplususbworker.moc"

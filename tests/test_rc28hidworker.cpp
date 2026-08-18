// Unit tests for Rc28HidWorker::decodeReport — the pure RC-28 report decoder.
// No hidapi I/O is exercised (no hardware required); hidapi is linked only
// because the worker's translation unit includes <hidapi/hidapi.h>.

#include "hardware/rc28hidworker.h"
#include <QtTest/QtTest>
#include <cstring>

class TestRc28HidWorker : public QObject {
    Q_OBJECT

private slots:
    void encoder_clockwise();
    void encoder_counterClockwise();
    void encoder_speedScales();
    void encoder_speedClamped();
    void encoder_stoppedNoEvent();
    void button_f1();
    void button_f2();
    void button_tx();
    void version_parsed();
    void shortBufferSafe();
};

static void makeFrame(unsigned char *buf, unsigned char b1, unsigned char b3, unsigned char b5) {
    memset(buf, 0, 64);
    buf[0] = 0x01;
    buf[1] = b1;
    buf[3] = b3;
    buf[5] = b5;
}

void TestRc28HidWorker::encoder_clockwise() {
    unsigned char buf[64];
    makeFrame(buf, 1, 0x02, Rc28HidWorker::BTN_NONE);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QVERIFY(ev.emitEncoder);
    QCOMPARE(ev.encoderTicks, -1);
    QCOMPARE(ev.buttonDown, 0);
}

void TestRc28HidWorker::encoder_counterClockwise() {
    unsigned char buf[64];
    makeFrame(buf, 2, 0x01, Rc28HidWorker::BTN_NONE);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QVERIFY(ev.emitEncoder);
    QCOMPARE(ev.encoderTicks, 2);
}

void TestRc28HidWorker::encoder_speedScales() {
    unsigned char buf[64];
    makeFrame(buf, 4, 0x02, Rc28HidWorker::BTN_NONE);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QCOMPARE(ev.encoderTicks, -4);
}

void TestRc28HidWorker::encoder_speedClamped() {
    unsigned char buf[64];
    makeFrame(buf, 9, 0x02, Rc28HidWorker::BTN_NONE); // out-of-range speed
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QCOMPARE(ev.encoderTicks, -4); // clamped to max accel
}

void TestRc28HidWorker::encoder_stoppedNoEvent() {
    unsigned char buf[64];
    makeFrame(buf, 1, 0x00, Rc28HidWorker::BTN_NONE);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QVERIFY(!ev.emitEncoder);
    QCOMPARE(ev.buttonDown, 0);
}

void TestRc28HidWorker::button_f1() {
    unsigned char buf[64];
    makeFrame(buf, 0, 0, Rc28HidWorker::BTN_F1);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QCOMPARE(ev.buttonDown, 1);
    QVERIFY(!ev.emitEncoder);
}

void TestRc28HidWorker::button_f2() {
    unsigned char buf[64];
    makeFrame(buf, 0, 0, Rc28HidWorker::BTN_F2);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QCOMPARE(ev.buttonDown, 2);
}

void TestRc28HidWorker::button_tx() {
    unsigned char buf[64];
    makeFrame(buf, 0, 0, Rc28HidWorker::BTN_TX);
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QCOMPARE(ev.buttonDown, 3);
}

void TestRc28HidWorker::version_parsed() {
    unsigned char buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02;
    buf[1] = '1';
    buf[2] = '0';
    buf[3] = '2';
    buf[4] = ' ';
    auto ev = Rc28HidWorker::decodeReport(buf, 64);
    QVERIFY(ev.isVersion);
    QCOMPARE(ev.firmwareVersion, QStringLiteral("1.02"));
}

void TestRc28HidWorker::shortBufferSafe() {
    unsigned char buf[4] = {0x01, 0x02, 0x00, 0x02};
    // len < 6 → no byte5; treated as an idle/encoder frame with no button.
    auto ev = Rc28HidWorker::decodeReport(buf, 4);
    QCOMPARE(ev.buttonDown, 0);
    // With byte5 absent (defaults to BTN_NONE), byte3 == 0x02 → CW tick.
    QVERIFY(ev.emitEncoder);
}

QTEST_MAIN(TestRc28HidWorker)
#include "test_rc28hidworker.moc"

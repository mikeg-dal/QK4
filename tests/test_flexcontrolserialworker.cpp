// Unit tests for FlexControlSerialWorker::decodeToken — the pure FlexControl
// ASCII token decoder. No serial I/O is exercised (no hardware required).

#include "hardware/flexcontrolserialworker.h"
#include <QtTest/QtTest>

using Event = FlexControlSerialWorker::Event;

class TestFlexControlSerialWorker : public QObject {
    Q_OBJECT

private slots:
    void encoder_singleUp();
    void encoder_singleDown();
    void encoder_multiUp();
    void encoder_multiDown();
    void button_aux1Short();
    void button_aux2Double();
    void button_aux3Long();
    void button_bareMainKnob();
    void ignore_resetBanner();
    void ignore_empty();
    void ignore_badButton();
    void ignore_badPressType();
};

void TestFlexControlSerialWorker::encoder_singleUp() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("U", ev));
    QCOMPARE(ev.type, Event::Encoder);
    QCOMPARE(ev.ticks, 1);
}

void TestFlexControlSerialWorker::encoder_singleDown() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("D", ev));
    QCOMPARE(ev.type, Event::Encoder);
    QCOMPARE(ev.ticks, -1);
}

void TestFlexControlSerialWorker::encoder_multiUp() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("U04", ev));
    QCOMPARE(ev.ticks, 4);
}

void TestFlexControlSerialWorker::encoder_multiDown() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("D06", ev));
    QCOMPARE(ev.ticks, -6);
}

void TestFlexControlSerialWorker::button_aux1Short() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("X1S", ev));
    QCOMPARE(ev.type, Event::Button);
    QCOMPARE(ev.buttonNumber, 1);
    QCOMPARE(ev.pressType, 0);
}

void TestFlexControlSerialWorker::button_aux2Double() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("X2C", ev));
    QCOMPARE(ev.buttonNumber, 2);
    QCOMPARE(ev.pressType, 1);
}

void TestFlexControlSerialWorker::button_aux3Long() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("X3L", ev));
    QCOMPARE(ev.buttonNumber, 3);
    QCOMPARE(ev.pressType, 2);
}

void TestFlexControlSerialWorker::button_bareMainKnob() {
    Event ev;
    QVERIFY(FlexControlSerialWorker::decodeToken("S", ev));
    QCOMPARE(ev.type, Event::Button);
    QCOMPARE(ev.buttonNumber, 1);
    QCOMPARE(ev.pressType, 0);
    QVERIFY(FlexControlSerialWorker::decodeToken("L", ev));
    QCOMPARE(ev.pressType, 2);
}

void TestFlexControlSerialWorker::ignore_resetBanner() {
    Event ev;
    QVERIFY(!FlexControlSerialWorker::decodeToken("F0304", ev));
}

void TestFlexControlSerialWorker::ignore_empty() {
    Event ev;
    QVERIFY(!FlexControlSerialWorker::decodeToken("", ev));
}

void TestFlexControlSerialWorker::ignore_badButton() {
    Event ev;
    QVERIFY(!FlexControlSerialWorker::decodeToken("X4S", ev));
}

void TestFlexControlSerialWorker::ignore_badPressType() {
    Event ev;
    QVERIFY(!FlexControlSerialWorker::decodeToken("X1Z", ev));
}

QTEST_MAIN(TestFlexControlSerialWorker)
#include "test_flexcontrolserialworker.moc"

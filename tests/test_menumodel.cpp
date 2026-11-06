#include "models/menumodel.h"

#include <QObject>
#include <QtTest>

class TestMenuModel : public QObject {
    Q_OBJECT

private slots:
    void testResolvedNamePassesThrough();
    void testResolvedNameDefaultsToOneWhenSelectorMissing();
    void testResolvedNameUpdatesWithSelector();
    void testResolvedNameAllAffectedXvtrIds();
    void testDisplayValueUnitFormatters();
    void testClearAnnouncesItselfAndEmptiesTheModel();
    void testItemIdsOrderedByNameAndFiltered();
    void testTypedValuesBecomeStoredValues();
};

void TestMenuModel::testResolvedNamePassesThrough() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));
    const MenuItem *item = model.getMenuItem(7);
    QVERIFY(item);
    QCOMPARE(model.resolvedName(*item), QStringLiteral("AGC Hold Time"));
}

void TestMenuModel::testResolvedNameDefaultsToOneWhenSelectorMissing() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0078,XVTR Band <n> I.F.,XV,XVTR,1,0,1,0,0,1;"));
    const MenuItem *item = model.getMenuItem(78);
    QVERIFY(item);
    QCOMPARE(model.resolvedName(*item), QStringLiteral("XVTR Band 1 I.F."));
}

void TestMenuModel::testResolvedNameUpdatesWithSelector() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0086,XVTR Band # Select,XV,XVTR,1,1,12,1,1,1;"));
    QVERIFY(model.parseMEDF("MEDF0079,XVTR Band <n> Offset,XV,XVTR,1,0,1,0,0,1;"));

    const MenuItem *offset = model.getMenuItem(79);
    QVERIFY(offset);
    QCOMPARE(model.resolvedName(*offset), QStringLiteral("XVTR Band 1 Offset"));

    QVERIFY(model.parseME("ME0086.0005;"));
    QCOMPARE(model.resolvedName(*offset), QStringLiteral("XVTR Band 5 Offset"));

    QVERIFY(model.parseME("ME0086.0012;"));
    QCOMPARE(model.resolvedName(*offset), QStringLiteral("XVTR Band 12 Offset"));
}

void TestMenuModel::testResolvedNameAllAffectedXvtrIds() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0086,XVTR Band # Select,XV,XVTR,1,1,12,1,1,1;"));
    QVERIFY(model.parseMEDF("MEDF0076,XVTR Band <n> Mode,XV,XVTR,1,0,1,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0077,XVTR Band <n> R.F.,XV,XVTR,1,0,1,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0078,XVTR Band <n> I.F.,XV,XVTR,1,0,1,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0079,XVTR Band <n> Offset,XV,XVTR,1,0,1,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0098,XVTR Band <n> Power Out,XV,XVTR,1,0,1,0,0,1;"));

    QVERIFY(model.parseME("ME0086.0007;"));
    QCOMPARE(model.resolvedName(*model.getMenuItem(76)), QStringLiteral("XVTR Band 7 Mode"));
    QCOMPARE(model.resolvedName(*model.getMenuItem(77)), QStringLiteral("XVTR Band 7 R.F."));
    QCOMPARE(model.resolvedName(*model.getMenuItem(78)), QStringLiteral("XVTR Band 7 I.F."));
    QCOMPARE(model.resolvedName(*model.getMenuItem(79)), QStringLiteral("XVTR Band 7 Offset"));
    QCOMPARE(model.resolvedName(*model.getMenuItem(98)), QStringLiteral("XVTR Band 7 Power Out"));
}

void TestMenuModel::testDisplayValueUnitFormatters() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0079,XVTR Band <n> Offset,XV,Hz,1,-99999,99999,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0077,XVTR Band <n> R.F.,XV,MHz,1,0,99999,144,144,1;"));
    QVERIFY(model.parseMEDF("MEDF0098,XVTR Band <n> Power Out,XV,D10mW,1,0,50,10,10,1;"));
    QVERIFY(model.parseMEDF("MEDF0050,QSK Delay,CW,ms,1,0,200,15,15,1;"));

    QCOMPARE(model.getMenuItem(79)->displayValue(), QStringLiteral("+0 Hz"));
    QCOMPARE(model.getMenuItem(77)->displayValue(), QStringLiteral("144 MHz"));
    QCOMPARE(model.getMenuItem(98)->displayValue(), QStringLiteral("1.0 mW"));
    QCOMPARE(model.getMenuItem(50)->displayValue(), QStringLiteral("15 ms"));

    QVERIFY(model.parseME("ME0079.-0250;"));
    QCOMPARE(model.getMenuItem(79)->displayValue(), QStringLiteral("-250 Hz"));

    QVERIFY(model.parseME("ME0098.0050;"));
    QCOMPARE(model.getMenuItem(98)->displayValue(), QStringLiteral("5.0 mW"));

    QVERIFY(model.parseMEDF("MEDF0088,FM Deviation,TX,M50Hz,1,50,100,90,90,1;"));
    QVERIFY(model.parseMEDF("MEDF0089,FM Tone,TX,M50Hz,1,3,14,7,7,1;"));
    QCOMPARE(model.getMenuItem(88)->displayValue(), QStringLiteral("4500 Hz"));
    QCOMPARE(model.getMenuItem(89)->displayValue(), QStringLiteral("350 Hz"));

    QVERIFY(model.parseMEDF("MEDF0101,TX Monitor Line Out,TX,ZMON,1,0,50,1,1,1;"));
    QVERIFY(model.parseMEDF("MEDF0103,TX Noise Gate Threshold,TX,ZOFF,1,0,25,0,0,1;"));
    QCOMPARE(model.getMenuItem(101)->displayValue(), QStringLiteral("1"));
    QCOMPARE(model.getMenuItem(103)->displayValue(), QStringLiteral("OFF"));

    QVERIFY(model.parseME("ME0101.0000;"));
    QVERIFY(model.parseME("ME0103.0005;"));
    QCOMPARE(model.getMenuItem(101)->displayValue(), QStringLiteral("OFF"));
    QCOMPARE(model.getMenuItem(103)->displayValue(), QStringLiteral("5"));
}

// clear() runs on every disconnect; views drop their rows when it announces itself (#124).
void TestMenuModel::testClearAnnouncesItselfAndEmptiesTheModel() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0008,AGC Decay,RX AGC,DEC,1,0,200,0,0,1;"));
    QCOMPARE(model.itemIds().size(), 2);

    QSignalSpy cleared(&model, &MenuModel::modelCleared);
    model.clear();

    QCOMPARE(cleared.count(), 1);
    QCOMPARE(model.itemIds().size(), 0);
    QVERIFY(model.getMenuItem(7) == nullptr);

    // A value update for an item that no longer exists must not resurrect one or emit.
    QSignalSpy changed(&model, &MenuModel::menuValueChanged);
    model.parseME("ME0007.0005;");
    QCOMPARE(changed.count(), 0);
    QCOMPARE(model.itemIds().size(), 0);
}

void TestMenuModel::testItemIdsOrderedByNameAndFiltered() {
    MenuModel model;
    QVERIFY(model.parseMEDF("MEDF0086,XVTR Band # Select,XV,XVTR,1,1,12,1,1,1;"));
    QVERIFY(model.parseMEDF("MEDF0008,agc Decay,RX AGC,DEC,1,0,200,0,0,1;"));
    QVERIFY(model.parseMEDF("MEDF0007,AGC Hold Time,RX AGC,DEC,1,0,200,0,0,1;"));

    QCOMPARE(model.itemIds(), (QList<int>{8, 7, 86}));
    QCOMPARE(model.itemIds(QStringLiteral("AGC")), (QList<int>{8, 7}));
    QCOMPARE(model.itemIds(QStringLiteral("hold")), (QList<int>{7}));
    QVERIFY(model.itemIds(QStringLiteral("nothing")).isEmpty());
}

// Type-in: the operator types in the units the row shows; the K4 stores its own units, on its step,
// inside its range.
void TestMenuModel::testTypedValuesBecomeStoredValues() {
    using namespace MenuValueEntry;
    MenuItem rf;
    rf.type = "MHz";
    rf.minValue = 0;
    rf.maxValue = 99999;
    QVERIFY(acceptsTyping(rf));
    QVERIFY(!allowsNegative(rf));
    QCOMPARE(unitSuffix(rf), QString(" MHz"));
    QCOMPARE(rawFromTyped(rf, "10368"), std::optional<int>(10368)); // 10 GHz band start
    // Out of range is refused, never clamped: Mike typed past 99999 and the menu sent 99999.
    QCOMPARE(rawFromTyped(rf, "123456"), std::optional<int>());
    QVERIFY(withinTypingBounds(rf, "99999"));
    QVERIFY(!withinTypingBounds(rf, "100000"));
    QCOMPARE(rawFromTyped(rf, "abc"), std::optional<int>());

    MenuItem offset;
    offset.type = "Hz";
    offset.minValue = -99999;
    offset.maxValue = 99999;
    QVERIFY(allowsNegative(offset));
    QCOMPARE(rawFromTyped(offset, "-1234"), std::optional<int>(-1234));
    QVERIFY(!withinTypingBounds(offset, "-100000"));

    MenuItem power;
    power.type = "D10mW"; // tenths of a milliwatt
    power.minValue = 1;
    power.maxValue = 50;
    QVERIFY(allowsDecimal(power));
    QCOMPARE(rawFromTyped(power, "1.5"), std::optional<int>(15));
    QCOMPARE(rawFromTyped(power, "9"), std::optional<int>());
    QCOMPARE(rawFromTyped(power, "0.0"), std::optional<int>()); // below the 0.1 mW minimum
    QVERIFY(withinTypingBounds(power, "0."));                   // on the way to 0.5

    MenuItem deviation;
    deviation.type = "M50Hz"; // 50 Hz steps
    deviation.minValue = 0;
    deviation.maxValue = 200;
    QCOMPARE(rawFromTyped(deviation, "2510"), std::optional<int>(50));

    MenuItem stepped;
    stepped.type = "DEC";
    stepped.minValue = 0;
    stepped.maxValue = 100;
    stepped.step = 5;
    QCOMPARE(rawFromTyped(stepped, "12"), std::optional<int>(10));
    QCOMPARE(rawFromTyped(stepped, "13"), std::optional<int>(15));

    MenuItem choice = rf;
    choice.options = {"OFF", "External"};
    QVERIFY(!acceptsTyping(choice));
    MenuItem binary = rf;
    binary.type = "BIN";
    QVERIFY(!acceptsTyping(binary));
    MenuItem locked = rf;
    locked.flag = 2;
    QVERIFY(!acceptsTyping(locked));
}

QTEST_MAIN(TestMenuModel)
#include "test_menumodel.moc"

#include <QtTest/QtTest>
#include <string>

#include "audio/audiodeviceselection.h"

using AudioDeviceSelection::Action;
using AudioDeviceSelection::onDeviceListChanged;

// Which device an audio stream belongs on when the device list changes.
//
// WHY this suite exists. The engine followed the OS default when nothing was pinned and did
// NOTHING when something was. That second branch loses the case the operator actually hits:
//
//   pin a Bluetooth headset -> quit -> come back with it in its case -> setup silently falls back
//   to the OS default -> put the headset on -> it appears in the list -> nothing happens.
//
// The dropdown then shows the headset, because the saved id matches a real device again, while the
// audio is still on the fallback. Interface says one thing, audio path does another - the same
// shape as INT-005 and just as invisible. The `pinnedReappears` case below is that bug.
class TestAudioDeviceSelection : public QObject {
    Q_OBJECT

private slots:
    // --- following the system default (nothing pinned) ---
    void followsSystemDefaultWhenNothingPinned();
    void staysPutWhenDefaultDidNotMove();
    void ignoresAnEmptySystemDefault();

    // --- the regression this was written for ---
    void pinnedDeviceIsClaimedWhenItReappears();
    void pinnedDeviceAbsentKeepsTheFallback();
    void pinnedDeviceAlreadyActiveDoesNothing();

    // --- a pin must never be overridden by the OS ---
    void neverFollowsTheDefaultAwayFromAPinnedDevice();

    // --- nothing open yet ---
    void doesNothingBeforeAStreamExists();

    // --- no decision may leave the stream somewhere nobody asked for ---
    void everySwitchTargetWasEitherPinnedOrTheDefault();
};

void TestAudioDeviceSelection::followsSystemDefaultWhenNothingPinned() {
    const auto d = onDeviceListChanged("", "brio", "airpods", false, true);
    QCOMPARE(d.action, Action::SwitchTo);
    QCOMPARE(d.deviceId, std::string("airpods"));
}

void TestAudioDeviceSelection::staysPutWhenDefaultDidNotMove() {
    const auto d = onDeviceListChanged("", "brio", "brio", false, true);
    QCOMPARE(d.action, Action::Keep);
}

void TestAudioDeviceSelection::ignoresAnEmptySystemDefault() {
    // Every device unplugged at once: there is nowhere better to go, so do not tear down what
    // is playing on the strength of a momentary gap.
    const auto d = onDeviceListChanged("", "brio", "", false, true);
    QCOMPARE(d.action, Action::Keep);
}

void TestAudioDeviceSelection::pinnedDeviceIsClaimedWhenItReappears() {
    // THE regression. Pinned to the AirPods, running on the BRIO fallback because the AirPods were
    // absent at startup, and they have just appeared. Before this, the handler returned on sight of
    // a non-empty pin and the operator stayed on the fallback indefinitely.
    const auto d = onDeviceListChanged("airpods", "brio", "brio", /*pinnedPresent=*/true, true);
    QCOMPARE(d.action, Action::SwitchTo);
    QCOMPARE(d.deviceId, std::string("airpods"));
}

void TestAudioDeviceSelection::pinnedDeviceAbsentKeepsTheFallback() {
    // Still not plugged in. Stay on whatever is working rather than tearing down for nothing.
    const auto d = onDeviceListChanged("airpods", "brio", "brio", /*pinnedPresent=*/false, true);
    QCOMPARE(d.action, Action::Keep);
}

void TestAudioDeviceSelection::pinnedDeviceAlreadyActiveDoesNothing() {
    // An unrelated device appearing must not churn a stream that is already where it belongs.
    const auto d = onDeviceListChanged("airpods", "airpods", "brio", true, true);
    QCOMPARE(d.action, Action::Keep);
}

void TestAudioDeviceSelection::neverFollowsTheDefaultAwayFromAPinnedDevice() {
    // The OS default moving to some third device must NOT drag a pinned stream with it. Pinning is
    // the operator overriding the OS, and the whole point is that it sticks.
    const auto d = onDeviceListChanged("airpods", "airpods", "usb-headset", true, true);
    QCOMPARE(d.action, Action::Keep);

    // Even when the pinned device is absent and the default moves, we do not chase it: we are
    // already on a fallback and hopping between fallbacks would be worse than staying put.
    const auto d2 = onDeviceListChanged("airpods", "brio", "usb-headset", false, true);
    QCOMPARE(d2.action, Action::Keep);
}

void TestAudioDeviceSelection::doesNothingBeforeAStreamExists() {
    // Nothing to move; whatever opens next resolves the current state correctly on its own.
    for (bool pinnedPresent : {false, true}) {
        QCOMPARE(onDeviceListChanged("", "", "airpods", pinnedPresent, false).action, Action::Keep);
        QCOMPARE(onDeviceListChanged("airpods", "", "brio", pinnedPresent, false).action, Action::Keep);
    }
}

void TestAudioDeviceSelection::everySwitchTargetWasEitherPinnedOrTheDefault() {
    // Sweep every combination and assert the invariant that matters: we may only ever move the
    // stream to a device the operator pinned or the OS nominated. Anything else would be QK4
    // choosing a microphone on its own, which it has no business doing.
    const std::string ids[] = {"", "a", "b", "c"};
    for (const auto &pinned : ids)
        for (const auto &active : ids)
            for (const auto &sysDefault : ids)
                for (bool present : {false, true})
                    for (bool haveStream : {false, true}) {
                        const auto d = onDeviceListChanged(pinned, active, sysDefault, present, haveStream);
                        if (d.action != Action::SwitchTo)
                            continue;
                        QVERIFY2(haveStream, "switched with no stream to switch");
                        QVERIFY2(!d.deviceId.empty(), "switched to an empty device id");
                        QVERIFY2(d.deviceId != active, "switched to the device already active");
                        const bool legitimate = (d.deviceId == pinned) || (pinned.empty() && d.deviceId == sysDefault);
                        QVERIFY2(legitimate, "switched to a device that was neither pinned nor the system default");
                        QVERIFY2(d.reason && d.reason[0] != '\0', "a device move must be able to explain itself");
                    }
}

QTEST_APPLESS_MAIN(TestAudioDeviceSelection)
#include "test_audiodeviceselection.moc"

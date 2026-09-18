// Tests for the USB device lifecycle policy — the one owner of "should this device be open, and
// should the operator be told about it".
//
// Every assertion below is a rule that was NOT true of the KPOD/KPOD+ code before this policy
// existed, so they read as a specification rather than as coverage. The three that matter most:
// a disabled device is never opened (USB-002), a duplicated removal reports once (the defect that
// defeated the one-shot "expected stop" flag), and switching a device off is not reported as a
// disconnection.
//
// Pure logic — no device, no libusb, no hidapi, no event loop.
#include <QtTest/QtTest>
#include "hardware/usbdevicelifecycle.h"

using namespace UsbDeviceLifecycle;

class TestUsbDeviceLifecycle : public QObject {
    Q_OBJECT

private slots:
    // ---- USB-002: the setting actually disables the device --------------------------------------

    // The defect verbatim: a KPOD+ plugged in with "Enable K-Pod" off was opened and polled anyway,
    // because the worker's presence timer called openDevice() itself and only HardwareController
    // knew about the setting.
    void aDisabledDeviceIsNeverOpened() {
        State s; // enabled defaults to false, which is also the setting's default
        const Effects e = apply(s, Event::Detected);
        QVERIFY2(!e.open, "a detected device must not be opened while the operator has it switched off");
        QVERIFY(!e.reportArrived);
        QVERIFY(!e.notifyConnected);
        QVERIFY(s.present);
        QVERIFY(!s.open);
    }

    // Enabling it afterwards is what opens it — the operator's action, not the cable's.
    void enablingAPresentDeviceOpensIt() {
        State s;
        apply(s, Event::Detected);
        const Effects e = apply(s, Event::Enabled);
        QVERIFY(e.open);
        QVERIFY(s.enabled);
    }

    // And enabling it with nothing plugged in opens nothing.
    void enablingWithNoDevicePresentOpensNothing() {
        State s;
        const Effects e = apply(s, Event::Enabled);
        QVERIFY(!e.open);
        QVERIFY(s.enabled);
        QVERIFY(!s.present);
    }

    // The ordinary happy path, both orders, must reach the same place.
    void enabledThenPluggedInIsTheSameAsPluggedInThenEnabled() {
        State a;
        apply(a, Event::Enabled);
        QVERIFY(apply(a, Event::Detected).open);

        State b;
        apply(b, Event::Detected);
        QVERIFY(apply(b, Event::Enabled).open);

        QCOMPARE(a.present, b.present);
        QCOMPARE(a.enabled, b.enabled);
    }

    // ---- Duplicate removals report once ---------------------------------------------------------

    // One physical unplug produces TWO removal events on both devices: on the KPOD+ from
    // closeDevice() and again on the next line; on the KPOD from the 20 ms poll failing and then the
    // 2 s presence timer noticing seconds later. The second must be silent.
    //
    // Regression gate: downstream code defended this with a one-shot flag, which the FIRST duplicate
    // consumed — so the second raised a disconnection notification anyway.
    void aSecondLossForOneUnplugReportsNothing() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);
        apply(s, Event::Opened);

        const Effects first = apply(s, Event::Lost);
        QVERIFY(first.close);
        QVERIFY(first.reportRemoved);
        QVERIFY(first.notifyDisconnected);

        const Effects second = apply(s, Event::Lost);
        QVERIFY2(!second.close, "a duplicate loss must not close anything");
        QVERIFY2(!second.reportRemoved, "a duplicate loss must not report a second removal");
        QVERIFY2(!second.notifyDisconnected, "a duplicate loss must not raise a second notification");

        const Effects third = apply(s, Event::Lost);
        QVERIFY(!third.close && !third.reportRemoved && !third.notifyDisconnected);
    }

    // Losing a device that was present but never opened — because it was switched off — is not a
    // disconnection to report. Nothing was running.
    void losingADisabledDeviceReportsNothing() {
        State s;
        apply(s, Event::Detected); // present, not enabled, so never opened
        const Effects e = apply(s, Event::Lost);
        QVERIFY(!e.reportRemoved);
        QVERIFY(!e.notifyDisconnected);
        QVERIFY(!e.close);
        QVERIFY(!s.present);
    }

    // ---- Switching off is not a disconnection ---------------------------------------------------

    // The defect shipped in 5016ea6: unchecking the box raised "KPOD+ disconnected — CW keying has
    // returned to QK4's keyer", as if the cable had been pulled.
    void disablingALiveDeviceClosesItWithoutCryingDisconnected() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);
        apply(s, Event::Opened);

        const Effects e = apply(s, Event::Disabled);
        QVERIFY2(e.close, "switching it off must close it");
        QVERIFY2(e.reportRemoved, "the page must still follow it");
        QVERIFY2(!e.notifyDisconnected, "the operator just did this; do not report it as a disconnection");
        QVERIFY(!s.open);
        QVERIFY(!s.enabled);
        QVERIFY2(s.present, "switching it off does not unplug it");
    }

    // ...and it can be switched straight back on, because it never stopped being present.
    void reEnablingAfterDisableReopens() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);
        apply(s, Event::Opened);
        apply(s, Event::Disabled);

        const Effects e = apply(s, Event::Enabled);
        QVERIFY(e.open);
    }

    // A redundant toggle — the setting written with the value it already had — changes nothing.
    void redundantEnableOrDisableDoesNothing() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);
        apply(s, Event::Opened);

        const Effects again = apply(s, Event::Enabled);
        QVERIFY(!again.open && !again.close && !again.reportArrived && !again.reportRemoved);

        apply(s, Event::Disabled);
        const Effects off = apply(s, Event::Disabled);
        QVERIFY(!off.close && !off.reportRemoved && !off.notifyDisconnected);
    }

    // ---- Open results ----------------------------------------------------------------------------

    void openingReportsArrivalOnce() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);

        const Effects first = apply(s, Event::Opened);
        QVERIFY(first.reportArrived);
        QVERIFY(first.notifyConnected);

        const Effects again = apply(s, Event::Opened);
        QVERIFY2(!again.reportArrived, "the KPOD+ emits deviceInfoReady twice per plug-in; arrival is once");
        QVERIFY(!again.notifyConnected);
    }

    // USB-003: an open failure was silent, so a KPOD+ that could not be claimed left the operator
    // with no CW and no explanation.
    void anOpenFailureIsReported() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);

        const Effects e = apply(s, Event::OpenFailed);
        QVERIFY(e.notifyOpenFailed);
        QVERIFY(!s.open);
        QVERIFY2(!e.reportArrived, "a failed open is not an arrival");
    }

    // ---- Shutdown -------------------------------------------------------------------------------

    // Quitting closes the device and tells nobody: the window is going away.
    void shutdownClosesSilently() {
        State s;
        apply(s, Event::Enabled);
        apply(s, Event::Detected);
        apply(s, Event::Opened);

        const Effects e = apply(s, Event::ShuttingDown);
        QVERIFY(e.close);
        QVERIFY(!e.reportRemoved);
        QVERIFY(!e.notifyDisconnected);
        QVERIFY(!s.open);
    }

    void shutdownWithNothingOpenDoesNothing() {
        State s;
        const Effects e = apply(s, Event::ShuttingDown);
        QVERIFY(!e.close && !e.reportRemoved && !e.notifyDisconnected);
    }

    // ---- Invariants over every reachable state ---------------------------------------------------

    // The one that matters: no sequence of events can ever ask for a device to be opened while the
    // operator has it switched off. That is the whole of USB-002, stated as a property rather than
    // as a case.
    void noSequenceEverOpensADisabledDevice() {
        const Event all[] = {Event::Detected, Event::Lost,     Event::Opened,      Event::OpenFailed,
                             Event::Enabled,  Event::Disabled, Event::ShuttingDown};
        // Exhaustive over every ordered triple of events from a fresh state.
        for (Event a : all) {
            for (Event b : all) {
                for (Event c : all) {
                    State s;
                    for (Event ev : {a, b, c}) {
                        const Effects e = apply(s, ev);
                        if (e.open) {
                            QVERIFY2(s.enabled, "opened a device the operator switched off");
                            QVERIFY2(s.present, "opened a device that is not plugged in");
                        }
                        QVERIFY2(!(e.open && e.close), "a single decision cannot both open and close");
                        QVERIFY2(!(e.notifyConnected && e.notifyDisconnected), "cannot both connect and disconnect");
                        if (e.notifyDisconnected)
                            QVERIFY2(e.reportRemoved, "a notification without the matching state change");
                    }
                }
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestUsbDeviceLifecycle)
#include "test_usbdevicelifecycle.moc"

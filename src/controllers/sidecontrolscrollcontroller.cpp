#include "sidecontrolscrollcontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "network/catframes.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "utils/radioutils.h"

#include <QChar>
#include <QString>
#include <QtGlobal>

SideControlScrollController::SideControlScrollController(RadioState *radioState, ConnectionController *connection,
                                                         SideControlPanel *panel, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_panel(panel) {
    // Connect side control panel scroll signals to CAT commands.
    // After sending CAT, update RadioState optimistically — the K4 doesn't
    // echo most of these SET commands.

    // Group 1: WPM/PTCH (CW mode) and MIC/CMP (Voice mode)
    // Every knob steps from the value the radio reported, within the K4's own limits
    // (RadioUtils), and sends nothing until the radio has reported it.
    connect(m_panel, &SideControlPanel::wpmChanged, this, [this](int delta) {
        const auto wpm = RadioUtils::nextKeyerSpeed(m_radioState->keyerSpeed(), delta);
        if (!wpm)
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::keyerSpeed(*wpm)));
        m_radioState->setKeyerSpeed(*wpm);
    });
    connect(m_panel, &SideControlPanel::pitchChanged, this, [this](int delta) {
        const auto pitchHz = RadioUtils::nextCwPitch(m_radioState->cwPitch(), delta);
        if (!pitchHz)
            return;
        m_connection->sendCAT(QString("CW%1;").arg(*pitchHz / RadioUtils::CW_PITCH_STEP_HZ, 2, 10, QChar('0')));
        m_radioState->setCwPitch(*pitchHz);
    });
    connect(m_panel, &SideControlPanel::micGainChanged, this, [this](int delta) {
        const auto gain = RadioUtils::stepWithin(m_radioState->micGain(), delta, 0, RadioUtils::MIC_GAIN_MAX);
        if (!gain)
            return;
        m_connection->sendCAT(QString("MG%1;").arg(*gain, 3, 10, QChar('0')));
        m_radioState->setMicGain(*gain);
    });
    connect(m_panel, &SideControlPanel::compressionChanged, this, [this](int delta) {
        const auto comp = RadioUtils::stepWithin(m_radioState->compression(), delta, 0, RadioUtils::COMPRESSION_MAX);
        if (!comp)
            return;
        m_connection->sendCAT(QString("CP%1;").arg(*comp, 3, 10, QChar('0')));
        m_radioState->setCompression(*comp);
    });

    // Group 1: PWR/DLY
    connect(m_panel, &SideControlPanel::powerChanged, this, [this](int delta) {
        const auto next = RadioUtils::nextRfPower(m_radioState->rfPower(), m_radioState->powerRange(), delta);
        if (!next)
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::setRfPower(next->value, next->range)));
        m_radioState->setRfPower(next->value, next->range);
    });

    connect(m_panel, &SideControlPanel::delayChanged, this, [this](int delta) {
        const auto delay =
            RadioUtils::stepWithin(m_radioState->delayForCurrentMode(), delta, 0, RadioUtils::QSK_DELAY_MAX);
        if (!delay)
            return;
        const int newDelay = *delay;
        m_radioState->setDelayForCurrentMode(newDelay);

        // SD command format: SDxyzzz where x=QSK flag, y=mode (C/V/D), zzz=delay in 10ms
        // Determine mode character based on current operating mode
        QChar modeChar = 'V'; // Default to Voice
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            modeChar = 'C';
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            modeChar = 'D';
        }
        // x=0 means use specified delay (not full QSK)
        m_connection->sendCAT(QString("SD0%1%2;").arg(modeChar).arg(newDelay, 3, 10, QChar('0')));
    });

    // Group 2: BW/HI and SHFT/LO
    // BW command uses 10Hz units (divide by 10)
    connect(m_panel, &SideControlPanel::bandwidthChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        int currentBw = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();

        // Mode-specific BW limits (Hz)
        int bwMin = 50, bwMax = 5000;
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D
                bwMin = 150;
                bwMax = 800;
            } else if (subMode == 3) { // PSK-D
                bwMax = 200;
            }
        }

        int newBw = qBound(bwMin, currentBw + (delta * 50), bwMax);
        QString cmd = bSet ? "BW$" : "BW";
        m_connection->sendCAT(QString("%1%2;").arg(cmd).arg(newBw / 10, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setFilterBandwidthB(newBw);
        } else {
            m_radioState->setFilterBandwidth(newBw);
        }
    });

    connect(m_panel, &SideControlPanel::highCutChanged, this,
            [this](int delta) { applyFilterEdgeChange(/*adjustHi=*/true, delta); });

    connect(m_panel, &SideControlPanel::shiftChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();

        // IS is locked in certain modes — ignore scroll
        if (mode == RadioState::AM || mode == RadioState::FM)
            return;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2 || subMode == 3)
                return; // FSK-D, PSK-D: IS locked
        }

        int currentShift = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();
        int isMax = (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300;
        int newShift = qBound(30, currentShift + delta, isMax);
        QString prefix = bSet ? "IS$" : "IS";
        m_connection->sendCAT(QString("%1+%2;").arg(prefix).arg(newShift, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setIfShiftB(newShift);
        } else {
            m_radioState->setIfShift(newShift);
        }
    });

    connect(m_panel, &SideControlPanel::lowCutChanged, this,
            [this](int delta) { applyFilterEdgeChange(/*adjustHi=*/false, delta); });

    // Group 3: M.RF/M.SQL and S.RF/S.SQL
    // RF Gain uses RG-nn; format where nn is 00-60 (representing -0 to -60 dB attenuation)
    // Scroll up = less attenuation = decrease value, scroll down = more attenuation = increase value
    connect(m_panel, &SideControlPanel::mainRfGainChanged, this, [this](int delta) {
        const auto gain = RadioUtils::stepWithin(m_radioState->rfGain(), -delta, 0, RadioUtils::RF_GAIN_MAX);
        if (!gain)
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::rfGain(*gain)));
        m_radioState->setRfGain(*gain);
    });
    connect(m_panel, &SideControlPanel::mainSquelchChanged, this, [this](int delta) {
        const auto sql = RadioUtils::nextSquelch(m_radioState->squelchLevel(), delta);
        if (!sql)
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::squelch(*sql)));
        m_radioState->setSquelchLevel(*sql);
    });
    connect(m_panel, &SideControlPanel::subRfGainChanged, this, [this](int delta) {
        const auto gain = RadioUtils::stepWithin(m_radioState->rfGainB(), -delta, 0, RadioUtils::RF_GAIN_MAX);
        if (!gain)
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::subRx(CatFrames::rfGain(*gain))));
        m_radioState->setRfGainB(*gain);
    });
    connect(m_panel, &SideControlPanel::subSquelchChanged, this, [this](int delta) {
        const auto sql = RadioUtils::nextSquelch(m_radioState->squelchLevelB(), delta);
        if (!sql)
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::subRx(CatFrames::squelch(*sql))));
        m_radioState->setSquelchLevelB(*sql);
    });
}

void SideControlScrollController::applyFilterEdgeChange(bool adjustHi, int delta) {
    // HI and LO adjust filter edges while keeping the OPPOSITE edge fixed.
    // Both BW and IS must change. Work in decahertz (dah) to avoid rounding
    // drift. Step is 2 dah (20 Hz) per scroll tick — keeps IS on-grid.
    bool bSet = m_radioState->bSetEnabled();
    RadioState::Mode mode = m_radioState->mode();
    int bwDah = (bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth()) / 10;
    int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();

    // Mode-specific BW limits (dah) and IS-locked flag
    int bwMinDah = 5, bwMaxDah = 500;
    bool isLocked = false;
    if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
        int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
        if (subMode == 2) { // FSK-D: BW 150-800Hz, IS locked
            bwMinDah = 15;
            bwMaxDah = 80;
            isLocked = true;
        } else if (subMode == 3) { // PSK-D: BW 50-200Hz, IS locked
            bwMaxDah = 20;
            isLocked = true;
        }
    }

    const int isMaxDah = (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300;
    const auto next =
        RadioUtils::filterAfterEdgeStep({bwDah, isDah}, adjustHi, delta, {bwMinDah, bwMaxDah, 30, isMaxDah, isLocked});
    if (!next)
        return;
    const int newBwDah = next->bwDah;

    QString bwCmd = bSet ? "BW$" : "BW";
    m_connection->sendCAT(QString("%1%2;").arg(bwCmd).arg(newBwDah, 4, 10, QChar('0')));

    if (isLocked) {
        // IS stays fixed — only BW changes
        if (bSet)
            m_radioState->setFilterBandwidthB(newBwDah * 10);
        else
            m_radioState->setFilterBandwidth(newBwDah * 10);
    } else {
        const int newIsDah = next->isDah;
        QString isPrefix = bSet ? "IS$" : "IS";
        m_connection->sendCAT(QString("%1+%2;").arg(isPrefix).arg(newIsDah, 4, 10, QChar('0')));

        if (bSet) {
            m_radioState->setFilterBandwidthB(newBwDah * 10);
            m_radioState->setIfShiftB(newIsDah);
        } else {
            m_radioState->setFilterBandwidth(newBwDah * 10);
            m_radioState->setIfShift(newIsDah);
        }
    }
}

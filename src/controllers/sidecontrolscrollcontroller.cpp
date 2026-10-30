#include "sidecontrolscrollcontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "network/catframes.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "utils/radioutils.h"

#include <QChar>
#include <QString>

namespace {
// The K4 front panel's BW knob step.
constexpr int kBandwidthStepHz = 50;
} // namespace

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

        // SDxyzzz: x=0 keeps the delay (not full QSK), y is the mode class letter, zzz in 10 ms.
        const char modeLetter = RadioUtils::modeClassLetter(m_radioState->modeClass());
        m_connection->sendCAT(QString("SD0%1%2;").arg(QLatin1Char(modeLetter)).arg(newDelay, 3, 10, QChar('0')));
    });

    // Group 2: BW/HI and SHFT/LO. With B SET on they adjust VFO B, within VFO B's limits.
    connect(m_panel, &SideControlPanel::bandwidthChanged, this, [this](int delta) {
        const bool bSet = m_radioState->bSetEnabled();
        const RadioUtils::FilterEdgeLimits limits = targetFilterLimits();
        const auto bwHz =
            RadioUtils::stepWithin(bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth(),
                                   delta * kBandwidthStepHz, limits.bwMinDah * 10, limits.bwMaxDah * 10);
        if (!bwHz)
            return;
        QString cmd = bSet ? "BW$" : "BW";
        m_connection->sendCAT(QString("%1%2;").arg(cmd).arg(*bwHz / 10, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setFilterBandwidthB(*bwHz);
        } else {
            m_radioState->setFilterBandwidth(*bwHz);
        }
    });

    connect(m_panel, &SideControlPanel::highCutChanged, this,
            [this](int delta) { applyFilterEdgeChange(/*adjustHi=*/true, delta); });

    connect(m_panel, &SideControlPanel::shiftChanged, this, [this](int delta) {
        const bool bSet = m_radioState->bSetEnabled();
        const RadioUtils::FilterEdgeLimits limits = targetFilterLimits();
        if (limits.isLocked)
            return;
        const auto shift = RadioUtils::stepWithin(bSet ? m_radioState->ifShiftB() : m_radioState->ifShift(), delta,
                                                  limits.isMinDah, limits.isMaxDah);
        if (!shift)
            return;
        QString prefix = bSet ? "IS$" : "IS";
        m_connection->sendCAT(QString("%1+%2;").arg(prefix).arg(*shift, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setIfShiftB(*shift);
        } else {
            m_radioState->setIfShift(*shift);
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

RadioUtils::FilterEdgeLimits SideControlScrollController::targetFilterLimits() const {
    return RadioUtils::filterLimits(m_radioState->filterTargetMode(), m_radioState->filterTargetDataSubMode());
}

void SideControlScrollController::applyFilterEdgeChange(bool adjustHi, int delta) {
    // HI and LO adjust filter edges while keeping the OPPOSITE edge fixed.
    // Both BW and IS must change. Work in decahertz (dah) to avoid rounding
    // drift. Step is 2 dah (20 Hz) per scroll tick — keeps IS on-grid.
    const bool bSet = m_radioState->bSetEnabled();
    const int bwHz = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();
    const int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();
    if (bwHz < 0 || isDah < 0)
        return; // not reported yet
    const RadioUtils::FilterEdgeLimits limits = targetFilterLimits();
    const bool isLocked = limits.isLocked;
    const auto next = RadioUtils::filterAfterEdgeStep({bwHz / 10, isDah}, adjustHi, delta, limits);
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

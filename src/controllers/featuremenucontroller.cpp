#include "featuremenucontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"

#include <QString>
#include <QWidget>

namespace {

// K4 CAT limits for the feature bar's +/- steps.
constexpr int AttStepDb = 3; // RA+ / RA- move the attenuator 3 dB
constexpr int AttMaxDb = 21;
constexpr int NbLevelMax = 15;
constexpr int NrLevelMax = 10;
constexpr int SsnrLevelMax = 20;
constexpr int NotchStepHz = 10;
constexpr int NotchMinHz = 150;
constexpr int NotchMaxHz = 5000;

} // namespace

FeatureMenuController::FeatureMenuController(RadioState *radioState, ConnectionController *connection,
                                             QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_bar(new FeatureMenuBar(parentWidget)) {

    // === User-driven events → CAT + optimistic RadioState updates ===

    connect(m_bar, &FeatureMenuBar::toggleRequested, this, [this]() {
        const bool bSet = m_radioState->bSetEnabled();
        switch (m_bar->currentFeature()) {
        case FeatureMenuBar::Attenuator: {
            const bool newState = bSet ? !m_radioState->attenuatorEnabledB() : !m_radioState->attenuatorEnabled();
            m_bar->setFeatureEnabled(newState);
            m_connection->sendCAT(bSet ? "RA$/;" : "RA/;");
            break;
        }
        case FeatureMenuBar::NbLevel: {
            const bool curState = bSet ? m_radioState->noiseBlankerEnabledB() : m_radioState->noiseBlankerEnabled();
            m_bar->setFeatureEnabled(!curState);
            m_connection->sendCAT(bSet ? "NB$/;" : "NB/;");
            break;
        }
        case FeatureMenuBar::NrAdjust: {
            const bool ssnr = (m_bar->currentNrEngine() == FeatureMenuBar::Ssnr);
            const bool curEnabled =
                ssnr ? (bSet ? m_radioState->ssnrEnabledB() : m_radioState->ssnrEnabled())
                     : (bSet ? m_radioState->noiseReductionEnabledB() : m_radioState->noiseReductionEnabled());
            m_bar->setFeatureEnabled(!curEnabled);
            const QString prefix = ssnr ? (bSet ? "NRS$" : "NRS") : (bSet ? "NR$" : "NR");
            m_connection->sendCAT(prefix + "/;");
            break;
        }
        case FeatureMenuBar::ManualNotch: {
            const bool curState = bSet ? m_radioState->manualNotchEnabledB() : m_radioState->manualNotchEnabled();
            m_bar->setFeatureEnabled(!curState);
            m_connection->sendCAT(bSet ? "NM$/;" : "NM/;");
            break;
        }
        }
    });

    connect(m_bar, &FeatureMenuBar::incrementRequested, this, [this]() { stepCurrentFeature(+1); });
    connect(m_bar, &FeatureMenuBar::decrementRequested, this, [this]() { stepCurrentFeature(-1); });

    connect(m_bar, &FeatureMenuBar::extraButtonClicked, this, [this]() {
        // Extra button on NB level view cycles filter: NONE(0) → NARROW(1) → WIDE(2) → NONE(0).
        if (m_bar->currentFeature() != FeatureMenuBar::NbLevel)
            return;
        const bool bSet = m_radioState->bSetEnabled();
        const int curFilter = bSet ? m_radioState->noiseBlankerFilterWidthB() : m_radioState->noiseBlankerFilterWidth();
        const int newFilter = (curFilter + 1) % 3;
        const int level = bSet ? m_radioState->noiseBlankerLevelB() : m_radioState->noiseBlankerLevel();
        const int enabled =
            bSet ? (m_radioState->noiseBlankerEnabledB() ? 1 : 0) : (m_radioState->noiseBlankerEnabled() ? 1 : 0);
        if (bSet)
            m_radioState->setNoiseBlankerFilterB(newFilter);
        else
            m_radioState->setNoiseBlankerFilter(newFilter);
        m_bar->setNbFilter(newFilter);
        const QString prefix = bSet ? "NB$" : "NB";
        m_connection->sendCAT(
            QString("%1%2%3%4;").arg(prefix).arg(level, 2, 10, QChar('0')).arg(enabled).arg(newFilter));
    });

    connect(m_bar, &FeatureMenuBar::nrEngineToggleRequested, this, [this]() {
        const bool bSet = m_radioState->bSetEnabled();
        const auto newEngine =
            (m_bar->currentNrEngine() == FeatureMenuBar::Lms) ? FeatureMenuBar::Ssnr : FeatureMenuBar::Lms;
        m_bar->setNrEngine(newEngine);
        if (newEngine == FeatureMenuBar::Ssnr) {
            m_bar->setFeatureEnabled(bSet ? m_radioState->ssnrEnabledB() : m_radioState->ssnrEnabled());
            m_bar->setValue(bSet ? m_radioState->ssnrLevelB() : m_radioState->ssnrLevel());
        } else {
            m_bar->setFeatureEnabled(bSet ? m_radioState->noiseReductionEnabledB()
                                          : m_radioState->noiseReductionEnabled());
            m_bar->setValue(bSet ? m_radioState->noiseReductionLevelB() : m_radioState->noiseReductionLevel());
        }
        // WHY: K4 enforces mutual exclusion between LMS NR and SSNR — sending `/` to the
        // destination engine enables it (at its last K4-side level) and the radio auto-
        // disables the other one. We trust the echo to reconcile state.
        const QString prefix = (newEngine == FeatureMenuBar::Ssnr) ? (bSet ? "NRS$" : "NRS") : (bSet ? "NR$" : "NR");
        m_connection->sendCAT(prefix + "/;");
    });

    // === RadioState echoes → refresh the bar while it's visible ===
    connect(m_radioState, &RadioState::processingChanged, this, &FeatureMenuController::refreshCurrentFeature);
    connect(m_radioState, &RadioState::processingChangedB, this, &FeatureMenuController::refreshCurrentFeature);
    connect(m_radioState, &RadioState::notchChanged, this, &FeatureMenuController::refreshCurrentFeature);
    connect(m_radioState, &RadioState::notchBChanged, this, &FeatureMenuController::refreshCurrentFeature);
    // B SET toggle also triggers a refresh — the displayed VFO side flips.
    connect(m_radioState, &RadioState::bSetChanged, this, &FeatureMenuController::refreshCurrentFeature);
}

void FeatureMenuController::toggleFeature(FeatureMenuBar::Feature feature, QWidget *anchor) {
    // WHY isVisibleOrJustHidden: the press on the feature's key reaches the bar's click-away filter
    // first and closes it, and only then does the key fire. Reading isVisible() here would reopen
    // the bar on every second press. Another feature's key switches the bar to that feature.
    if (m_bar->isVisibleOrJustHidden() && m_bar->currentFeature() == feature) {
        m_bar->hidePopup();
        return;
    }
    if (!anchor)
        return;
    syncBarFromState(feature);
    m_bar->showForFeature(feature, anchor);
}

void FeatureMenuController::close() {
    m_bar->hidePopup();
}

bool FeatureMenuController::isVisible() const {
    return m_bar->isVisible();
}

void FeatureMenuController::refreshCurrentFeature() {
    if (!m_bar->isVisible())
        return;
    syncBarFromState(m_bar->currentFeature());
}

void FeatureMenuController::syncBarFromState(FeatureMenuBar::Feature feature) {
    const bool bSet = m_radioState->bSetEnabled();
    switch (feature) {
    case FeatureMenuBar::Attenuator:
        if (bSet) {
            m_bar->setFeatureEnabled(m_radioState->attenuatorEnabledB());
            m_bar->setValue(m_radioState->attenuatorLevelB());
        } else {
            m_bar->setFeatureEnabled(m_radioState->attenuatorEnabled());
            m_bar->setValue(m_radioState->attenuatorLevel());
        }
        break;
    case FeatureMenuBar::NbLevel:
        if (bSet) {
            m_bar->setFeatureEnabled(m_radioState->noiseBlankerEnabledB());
            m_bar->setValue(m_radioState->noiseBlankerLevelB());
            m_bar->setNbFilter(m_radioState->noiseBlankerFilterWidthB());
        } else {
            m_bar->setFeatureEnabled(m_radioState->noiseBlankerEnabled());
            m_bar->setValue(m_radioState->noiseBlankerLevel());
            m_bar->setNbFilter(m_radioState->noiseBlankerFilterWidth());
        }
        break;
    case FeatureMenuBar::NrAdjust: {
        // Engine-pick: enabled-wins. If exactly one of LMS/SSNR is on, snap to that engine.
        // If both off or (briefly) both on, keep the bar's current engine.
        const bool lmsOn = bSet ? m_radioState->noiseReductionEnabledB() : m_radioState->noiseReductionEnabled();
        const bool ssnrOn = bSet ? m_radioState->ssnrEnabledB() : m_radioState->ssnrEnabled();
        FeatureMenuBar::NrEngine engine = m_bar->currentNrEngine();
        if (lmsOn && !ssnrOn)
            engine = FeatureMenuBar::Lms;
        else if (ssnrOn && !lmsOn)
            engine = FeatureMenuBar::Ssnr;
        m_bar->setNrEngine(engine);
        if (engine == FeatureMenuBar::Ssnr) {
            m_bar->setFeatureEnabled(bSet ? m_radioState->ssnrEnabledB() : m_radioState->ssnrEnabled());
            m_bar->setValue(bSet ? m_radioState->ssnrLevelB() : m_radioState->ssnrLevel());
        } else {
            m_bar->setFeatureEnabled(bSet ? m_radioState->noiseReductionEnabledB()
                                          : m_radioState->noiseReductionEnabled());
            m_bar->setValue(bSet ? m_radioState->noiseReductionLevelB() : m_radioState->noiseReductionLevel());
        }
        break;
    }
    case FeatureMenuBar::ManualNotch:
        if (bSet) {
            m_bar->setFeatureEnabled(m_radioState->manualNotchEnabledB());
            m_bar->setValue(m_radioState->manualNotchPitchB());
        } else {
            m_bar->setFeatureEnabled(m_radioState->manualNotchEnabled());
            m_bar->setValue(m_radioState->manualNotchPitch());
        }
        break;
    }
}

void FeatureMenuController::stepCurrentFeature(int direction) {
    const bool bSet = m_radioState->bSetEnabled();
    const bool up = direction > 0;
    switch (m_bar->currentFeature()) {
    case FeatureMenuBar::Attenuator: {
        const int curLevel = bSet ? m_radioState->attenuatorLevelB() : m_radioState->attenuatorLevel();
        m_bar->setValue(qBound(0, curLevel + direction * AttStepDb, AttMaxDb));
        m_connection->sendCAT(QString("%1%2;").arg(bSet ? "RA$" : "RA").arg(up ? '+' : '-'));
        break;
    }
    case FeatureMenuBar::NbLevel: {
        const int curLevel = bSet ? m_radioState->noiseBlankerLevelB() : m_radioState->noiseBlankerLevel();
        const int newLevel = qBound(0, curLevel + direction, NbLevelMax);
        const int enabled =
            bSet ? (m_radioState->noiseBlankerEnabledB() ? 1 : 0) : (m_radioState->noiseBlankerEnabled() ? 1 : 0);
        const int filter = bSet ? m_radioState->noiseBlankerFilterWidthB() : m_radioState->noiseBlankerFilterWidth();
        if (bSet)
            m_radioState->setNoiseBlankerLevelB(newLevel);
        else
            m_radioState->setNoiseBlankerLevel(newLevel);
        m_bar->setValue(newLevel);
        const QString prefix = bSet ? "NB$" : "NB";
        m_connection->sendCAT(
            QString("%1%2%3%4;").arg(prefix).arg(newLevel, 2, 10, QChar('0')).arg(enabled).arg(filter));
        break;
    }
    case FeatureMenuBar::NrAdjust: {
        const bool ssnr = (m_bar->currentNrEngine() == FeatureMenuBar::Ssnr);
        const int curLevel = ssnr ? (bSet ? m_radioState->ssnrLevelB() : m_radioState->ssnrLevel())
                                  : (bSet ? m_radioState->noiseReductionLevelB() : m_radioState->noiseReductionLevel());
        const int newLevel = qBound(0, curLevel + direction, ssnr ? SsnrLevelMax : NrLevelMax);
        const int enabled =
            ssnr ? (bSet ? (m_radioState->ssnrEnabledB() ? 1 : 0) : (m_radioState->ssnrEnabled() ? 1 : 0))
                 : (bSet ? (m_radioState->noiseReductionEnabledB() ? 1 : 0)
                         : (m_radioState->noiseReductionEnabled() ? 1 : 0));
        if (ssnr) {
            if (bSet)
                m_radioState->setSsnrLevelB(newLevel);
            else
                m_radioState->setSsnrLevel(newLevel);
        } else {
            if (bSet)
                m_radioState->setNoiseReductionLevelB(newLevel);
            else
                m_radioState->setNoiseReductionLevel(newLevel);
        }
        m_bar->setValue(newLevel);
        const QString prefix = ssnr ? (bSet ? "NRS$" : "NRS") : (bSet ? "NR$" : "NR");
        m_connection->sendCAT(QString("%1%2%3;").arg(prefix).arg(newLevel, 2, 10, QChar('0')).arg(enabled));
        break;
    }
    case FeatureMenuBar::ManualNotch: {
        const int curPitch = bSet ? m_radioState->manualNotchPitchB() : m_radioState->manualNotchPitch();
        const int newPitch = qBound(NotchMinHz, curPitch + direction * NotchStepHz, NotchMaxHz);
        const int enabled =
            bSet ? (m_radioState->manualNotchEnabledB() ? 1 : 0) : (m_radioState->manualNotchEnabled() ? 1 : 0);
        if (bSet)
            m_radioState->setManualNotchPitchB(newPitch);
        else
            m_radioState->setManualNotchPitch(newPitch);
        m_bar->setValue(newPitch);
        const QString prefix = bSet ? "NM$" : "NM";
        m_connection->sendCAT(QString("%1%2%3;").arg(prefix).arg(newPitch, 4, 10, QChar('0')).arg(enabled));
        break;
    }
    }
}

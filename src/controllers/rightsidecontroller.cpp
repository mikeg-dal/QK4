#include "rightsidecontroller.h"

#include "connectioncontroller.h"
#include "controllers/featuremenucontroller.h"
#include "controllers/macrocontroller.h"
#include "controllers/modepopupcontroller.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "ui/widgets/featuremenubar.h"
#include "ui/widgets/rightsidepanel.h"
#include "utils/macroids.h"
#include "utils/radioutils.h"

#include <QString>

RightSideController::RightSideController(RadioState *radioState, ConnectionController *connection,
                                         RightSidePanel *panel, ModePopupController *modePopup,
                                         FeatureMenuController *featureMenu, MacroController *macroController,
                                         QWidget *featureAnchor, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_panel(panel), m_modePopup(modePopup),
      m_featureMenu(featureMenu), m_macroController(macroController), m_featureAnchor(featureAnchor) {

    // ---------------------------------------------------------------------
    // 5x2 main grid — primary (left-click) signals
    // ---------------------------------------------------------------------
    connect(m_panel, &RightSidePanel::preClicked, this, [this]() { m_connection->sendCAT("SW61;"); });
    connect(m_panel, &RightSidePanel::nbClicked, this, [this]() { m_connection->sendCAT("SW32;"); });
    connect(m_panel, &RightSidePanel::nrClicked, this, [this]() { m_connection->sendCAT("SW62;"); });
    connect(m_panel, &RightSidePanel::ntchClicked, this, [this]() { m_connection->sendCAT("SW31;"); });
    connect(m_panel, &RightSidePanel::filClicked, this, [this]() { m_connection->sendCAT("SW33;"); });
    connect(m_panel, &RightSidePanel::abClicked, this, [this]() { m_connection->sendCAT("SW41;"); });
    connect(m_panel, &RightSidePanel::revPressed, this, [this]() { m_connection->sendCAT("SW160;"); });
    connect(m_panel, &RightSidePanel::revReleased, this, [this]() { m_connection->sendCAT("SW161;"); });
    connect(m_panel, &RightSidePanel::atobClicked, this, [this]() { m_connection->sendCAT("SW72;"); });
    connect(m_panel, &RightSidePanel::spotClicked, this, [this]() { m_connection->sendCAT("SW42;"); });
    connect(m_panel, &RightSidePanel::modeClicked, this, [this]() { m_modePopup->toggleForBSet(m_featureAnchor); });

    // ---------------------------------------------------------------------
    // 5x2 main grid — secondary (right-click) signals
    // Feature-menu right-clicks toggle the per-feature menu overlay anchored
    // at the bottom menu bar. APF right-click toggles AP on Main/Sub RX.
    // ---------------------------------------------------------------------
    auto toggleFeatureMenu = [this](FeatureMenuBar::Feature feature) {
        m_featureMenu->toggleFeature(feature, m_featureAnchor);
    };
    connect(m_panel, &RightSidePanel::attnClicked, this, [=]() { toggleFeatureMenu(FeatureMenuBar::Attenuator); });
    connect(m_panel, &RightSidePanel::levelClicked, this, [=]() { toggleFeatureMenu(FeatureMenuBar::NbLevel); });
    connect(m_panel, &RightSidePanel::adjClicked, this, [=]() { toggleFeatureMenu(FeatureMenuBar::NrAdjust); });
    connect(m_panel, &RightSidePanel::manualClicked, this, [=]() { toggleFeatureMenu(FeatureMenuBar::ManualNotch); });
    connect(m_panel, &RightSidePanel::apfClicked, this, [this]() {
        // Toggle APF on/off for Main RX or Sub RX based on B SET state
        if (m_radioState->bSetEnabled()) {
            m_connection->sendCAT("AP$/;"); // Sub RX toggle
        } else {
            m_connection->sendCAT("AP/;"); // Main RX toggle
        }
    });
    connect(m_panel, &RightSidePanel::splitClicked, this, [this]() { m_connection->sendCAT("SW145;"); });
    connect(m_panel, &RightSidePanel::btoaClicked, this, [this]() { m_connection->sendCAT("SW147;"); });
    connect(m_panel, &RightSidePanel::autoClicked, this, [this]() { m_connection->sendCAT("SW146;"); });
    connect(m_panel, &RightSidePanel::altClicked, this, [this]() { m_connection->sendCAT("SW148;"); });

    // ---------------------------------------------------------------------
    // PF row — primary (left-click) signals
    // ---------------------------------------------------------------------
    connect(m_panel, &RightSidePanel::bsetClicked, this, [this]() { m_connection->sendCAT("SW44;"); });
    connect(m_panel, &RightSidePanel::clrClicked, this, [this]() { m_connection->sendCAT("SW64;"); });
    connect(m_panel, &RightSidePanel::ritClicked, this, [this]() { m_connection->sendCAT("SW54;"); });
    connect(m_panel, &RightSidePanel::xitClicked, this, [this]() { m_connection->sendCAT("SW74;"); });

    // ---------------------------------------------------------------------
    // PF row — secondary (right-click) signals — user-configured macros
    // PF1-PF4 execute user-configured macros if set, otherwise fall back to
    // the K4's default PF functions.
    // ---------------------------------------------------------------------
    auto pfHandler = [this](const QString &macroId, const char *defaultCat) {
        const MacroEntry macro = RadioSettings::instance()->macro(macroId);
        if (!macro.command.isEmpty()) {
            m_macroController->executeMacro(macroId);
        } else {
            m_connection->sendCAT(defaultCat);
        }
    };
    connect(m_panel, &RightSidePanel::pf1Clicked, this, [=]() { pfHandler(MacroIds::PF1, "SW153;"); });
    connect(m_panel, &RightSidePanel::pf2Clicked, this, [=]() { pfHandler(MacroIds::PF2, "SW154;"); });
    connect(m_panel, &RightSidePanel::pf3Clicked, this, [=]() { pfHandler(MacroIds::PF3, "SW155;"); });
    connect(m_panel, &RightSidePanel::pf4Clicked, this, [=]() { pfHandler(MacroIds::PF4, "SW156;"); });

    // ---------------------------------------------------------------------
    // Bottom row — SUB, DIVERSITY, RATE, LOCK A/B
    // RATE / KHZ are B-SET aware: target VFO B (VT$) when B SET is engaged.
    // ---------------------------------------------------------------------
    connect(m_panel, &RightSidePanel::subClicked, this, [this]() { m_connection->sendCAT("SW83;"); });
    connect(m_panel, &RightSidePanel::diversityClicked, this, [this]() { m_connection->sendCAT("SW152;"); });
    connect(m_panel, &RightSidePanel::freqEntClicked, this, &RightSideController::frequencyEntryRequested);
    connect(m_panel, &RightSidePanel::rateClicked, this, [this]() {
        // Cycle fine rates: 1 Hz → 10 Hz → 100 Hz → 1 Hz
        const bool bSet = m_radioState->bSetEnabled();
        const int current = bSet ? m_radioState->tuningStepB() : m_radioState->tuningStep();
        const int next = (current >= 0 && current < 2) ? current + 1 : 0;
        const QString cmd = QString("%1%2;").arg(bSet ? "VT$" : "VT").arg(next);
        m_connection->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
    connect(m_panel, &RightSidePanel::khzClicked, this, [this]() {
        // Set tuning step to 1 kHz (VT3)
        const bool bSet = m_radioState->bSetEnabled();
        const QString cmd = bSet ? QStringLiteral("VT$3;") : QStringLiteral("VT3;");
        m_connection->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
    connect(m_panel, &RightSidePanel::lockAClicked, this, [this]() { m_connection->sendCAT("SW63;"); });
    connect(m_panel, &RightSidePanel::lockBClicked, this, [this]() { m_connection->sendCAT("SW151;"); });
}

RightSideController::~RightSideController() {
    disconnect(this);
}

void RightSideController::cycleFilterPreset(bool vfoB) {
    const int current = vfoB ? m_radioState->filterPositionB() : m_radioState->filterPosition();
    const int next = RadioUtils::nextFilterPreset(current);
    if (next < 0 || !m_connection->isConnected())
        return;
    const QString cmd = QString("%1%2;").arg(vfoB ? "FP$" : "FP").arg(next);
    m_connection->sendCAT(cmd);
    // Optimistic, like RATE: the indicator changes now rather than on the radio's echo.
    m_radioState->parseCATCommand(cmd);
}

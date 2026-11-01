#include "modepopupcontroller.h"
#include "logging/logcategories.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "ui/popups/modepopupwidget.h"

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QWidget>

ModePopupController::ModePopupController(RadioState *radioState, ConnectionController *connection,
                                         QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_popup(new ModePopupWidget(parentWidget)) {

    // === User mode selection → CAT dispatch + optimistic DT update ===
    connect(m_popup, &ModePopupWidget::modeSelected, this, [this](const QString &catCmd) {
        if (!m_connection->isConnected())
            return;
        m_connection->sendCAT(catCmd);

        // WHY: K4 does not echo DT SET commands, so RadioState never learns
        // about a submode change issued by us. Mirror the change optimistically
        // to keep mode-label rendering in sync without waiting for a roundtrip
        // that will never come.
        static const QRegularExpression dtRegex("DT(\\$?)(\\d)");
        QRegularExpressionMatch match = dtRegex.match(catCmd);
        if (match.hasMatch()) {
            const bool isSubRx = !match.captured(1).isEmpty(); // DT$ = VFO B
            const int subMode = match.captured(2).toInt();
            qCDebug(lcUiModePopup) << "Optimistic DT update: isSubRx=" << isSubRx << "subMode=" << subMode;
            if (isSubRx)
                m_radioState->setDataSubModeB(subMode);
            else
                m_radioState->setDataSubMode(subMode);
        }
    });

    // === RadioState → popup sync ===
    // The popup only tracks one VFO at a time; the currently-targeted side
    // is picked by bSetEnabled. Mode/dataSubMode for the OTHER side are
    // silently ignored here (and repopulated when the popup is next opened
    // via showForVfoX).
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        if (!m_radioState->bSetEnabled())
            m_popup->setCurrentMode(static_cast<int>(mode));
    });
    connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode mode) {
        if (m_radioState->bSetEnabled())
            m_popup->setCurrentMode(static_cast<int>(mode));
    });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int subMode) {
        if (!m_radioState->bSetEnabled())
            m_popup->setCurrentDataSubMode(subMode);
    });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this](int subMode) {
        if (m_radioState->bSetEnabled())
            m_popup->setCurrentDataSubMode(subMode);
    });

    // BSet toggle — update popup's BSet flag and re-seed its display values
    // from the newly-targeted VFO.
    connect(m_radioState, &RadioState::bSetChanged, this, [this](bool enabled) {
        m_popup->setBSetEnabled(enabled);
        if (enabled) {
            m_popup->setFrequency(m_radioState->vfoB());
            m_popup->setCurrentMode(static_cast<int>(m_radioState->modeB()));
            m_popup->setCurrentDataSubMode(m_radioState->dataSubModeB());
        } else {
            m_popup->setFrequency(m_radioState->vfoA());
            m_popup->setCurrentMode(static_cast<int>(m_radioState->mode()));
            m_popup->setCurrentDataSubMode(m_radioState->dataSubMode());
        }
    });
}

void ModePopupController::toggleForVfoA(QWidget *anchor) {
    if (m_popup->isVisibleOrJustHidden())
        m_popup->hidePopup();
    else
        showForVfoA(anchor);
}

void ModePopupController::toggleForVfoB(QWidget *anchor) {
    if (m_popup->isVisibleOrJustHidden())
        m_popup->hidePopup();
    else
        showForVfoB(anchor);
}

void ModePopupController::toggleForBSet(QWidget *anchor) {
    if (m_popup->isVisibleOrJustHidden())
        m_popup->hidePopup();
    else if (m_radioState->bSetEnabled())
        showForVfoB(anchor);
    else
        showForVfoA(anchor);
}

void ModePopupController::close() {
    if (m_popup->isVisible())
        m_popup->hidePopup();
}

bool ModePopupController::isVisible() const {
    return m_popup->isVisible();
}

void ModePopupController::showForVfoA(QWidget *anchor) {
    m_popup->setFrequency(m_radioState->vfoA());
    m_popup->setCurrentMode(static_cast<int>(m_radioState->mode()));
    m_popup->setCurrentDataSubMode(m_radioState->dataSubMode());
    m_popup->setBSetEnabled(false);
    m_popup->showAboveWidget(anchor);
}

void ModePopupController::showForVfoB(QWidget *anchor) {
    m_popup->setFrequency(m_radioState->vfoB());
    m_popup->setCurrentMode(static_cast<int>(m_radioState->modeB()));
    m_popup->setCurrentDataSubMode(m_radioState->dataSubModeB());
    m_popup->setBSetEnabled(true);
    m_popup->showAboveWidget(anchor);
}

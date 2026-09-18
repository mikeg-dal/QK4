#include "macrocontroller.h"

#include "connectioncontroller.h"
#include "controllers/popupmanager.h"
#include "utils/macroids.h"
#include "settings/radiosettings.h"

#include <QLoggingCategory>
#include <QMessageBox>
#include <QString>

Q_LOGGING_CATEGORY(qk4Macro, "qk4.macro")

MacroController::MacroController(ConnectionController *connection, PopupManager *popupManager, QWidget *dialogParent,
                                 QObject *parent)
    : QObject(parent), m_connection(connection), m_popupManager(popupManager), m_dialogParent(dialogParent) {
    connect(m_popupManager, &PopupManager::macroFunctionTriggered, this, &MacroController::onFunctionTriggered);
}

MacroController::~MacroController() {
    disconnect(this);
}

void MacroController::executeMacro(const QString &functionId) {
    const MacroEntry macro = RadioSettings::instance()->macro(functionId);
    if (macro.command.isEmpty()) {
        qCDebug(qk4Macro) << "No macro configured for" << functionId;
        return;
    }
    qCDebug(qk4Macro) << "Executing macro" << functionId << ":" << macro.command;
    if (!m_connection->isConnected())
        return;

    m_connection->sendCAT(macro.command);

    // WHY: RT1 / RT0 / RT/ / SW54 and XT1 / XT0 / XT/ / SW74 are silent SET
    // commands — the K4 does not echo the resulting RT/XT state. Follow up
    // with a query so the UI reflects the change.
    const QString &cmd = macro.command;
    if (cmd.contains("RT1") || cmd.contains("RT0") || cmd.contains("RT/") || cmd.contains("SW54")) {
        m_connection->sendCAT("RT;");
        m_connection->sendCAT("RT$;");
    }
    if (cmd.contains("XT1") || cmd.contains("XT0") || cmd.contains("XT/") || cmd.contains("SW74")) {
        m_connection->sendCAT("XT;");
    }
}

void MacroController::onFunctionTriggered(const QString &functionId) {
    qCDebug(qk4Macro) << "Fn function triggered:" << functionId;

    if (functionId == MacroIds::ScrnCap) {
        // SS0; tells the K4 to write a screenshot to its internal SD card.
        if (m_connection->isConnected()) {
            m_connection->sendCAT("SS0;");
            qCDebug(qk4Macro) << "Screenshot captured (SS0;)";
        }
    } else if (functionId == MacroIds::Macros) {
        emit macroDialogRequested();
    } else if (functionId == MacroIds::SwList) {
        emit softwareListRequested();
    } else if (functionId == MacroIds::Update) {
        QMessageBox::information(m_dialogParent, "Coming Soon", "Update check is not yet implemented.");
    } else if (functionId == MacroIds::DxList) {
        QMessageBox::information(m_dialogParent, "Coming Soon", "DX list is not yet implemented.");
    } else if (functionId == MacroIds::Ft8) {
        emit ftxRequested();
    } else if (functionId == MacroIds::Sstv) {
        emit sstvRequested();
    } else if (functionId == MacroIds::Log) {
        emit logbookRequested();
    } else {
        // User-configurable macro — look up + dispatch via executeMacro.
        executeMacro(functionId);
    }
}

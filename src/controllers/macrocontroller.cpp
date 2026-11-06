#include "macrocontroller.h"
#include "logging/logcategories.h"

#include "connectioncontroller.h"
#include "controllers/popupmanager.h"
#include "utils/macroids.h"
#include "settings/radiosettings.h"

#include <QLoggingCategory>
#include <QMessageBox>
#include <QString>
#include <QStringView>

MacroController::MacroController(ConnectionController *connection, PopupManager *popupManager, QWidget *dialogParent,
                                 QObject *parent)
    : QObject(parent), m_connection(connection), m_popupManager(popupManager), m_dialogParent(dialogParent) {
    connect(m_popupManager, &PopupManager::macroFunctionTriggered, this, &MacroController::onFunctionTriggered);
}

void MacroController::executeMacro(const QString &functionId) {
    const MacroEntry macro = RadioSettings::instance()->macro(functionId);
    if (macro.command.isEmpty()) {
        qCDebug(lcUiMacro) << "No macro configured for" << functionId;
        return;
    }
    qCDebug(lcUiMacro) << "Executing macro" << functionId << ":" << macro.command;
    if (!m_connection->isConnected())
        return;

    m_connection->sendCAT(macro.command);

    // WHY: RT1 / RT0 / RT/ / SW54 and XT1 / XT0 / XT/ / SW74 are silent SET
    // commands — the K4 does not echo the resulting RT/XT state. Follow up
    // with a query so the UI reflects the change. Matched as whole commands of
    // the macro, so SW540 or SW741 does not count.
    bool ritChanged = false;
    bool xitChanged = false;
    for (const QStringView part : QStringView(macro.command).split(u';', Qt::SkipEmptyParts)) {
        const QStringView c = part.trimmed();
        ritChanged = ritChanged || c == u"RT1" || c == u"RT0" || c == u"RT/" || c == u"SW54";
        xitChanged = xitChanged || c == u"XT1" || c == u"XT0" || c == u"XT/" || c == u"SW74";
    }
    if (ritChanged) {
        m_connection->sendCAT("RT;");
        m_connection->sendCAT("RT$;");
    }
    if (xitChanged)
        m_connection->sendCAT("XT;");
}

void MacroController::onFunctionTriggered(const QString &functionId) {
    qCDebug(lcUiMacro) << "Fn function triggered:" << functionId;

    if (functionId == MacroIds::ScrnCap) {
        // SS0; tells the K4 to write a screenshot to its internal SD card.
        if (m_connection->isConnected()) {
            m_connection->sendCAT("SS0;");
            qCDebug(lcUiMacro) << "Screenshot captured (SS0;)";
        }
    } else if (functionId == MacroIds::Macros) {
        emit macroDialogRequested();
    } else if (functionId == MacroIds::SwList) {
        emit softwareListRequested();
    } else if (functionId == MacroIds::Update) {
        QMessageBox::information(m_dialogParent, "Coming Soon", "Update check is not yet implemented.");
    } else if (functionId == MacroIds::DxList) {
        QMessageBox::information(m_dialogParent, "Coming Soon", "DX list is not yet implemented.");
    } else {
        // User-configurable macro — look up + dispatch via executeMacro.
        executeMacro(functionId);
    }
}

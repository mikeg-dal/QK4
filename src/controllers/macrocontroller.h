#ifndef MACROCONTROLLER_H
#define MACROCONTROLLER_H

#include <QObject>
#include <QPointer>

class ConnectionController;
class PopupManager;
class QWidget;

// Dispatches macro invocations from the Fn popup, KPOD hardware buttons,
// and the PF1-PF4 right-side-panel buttons. Translates built-in function
// IDs (ScrnCap, Macros, SwList, Update, DxList) into their specific
// behavior and looks up user-configured macros from RadioSettings for
// CAT dispatch.
//
// Emits macroDialogRequested (Macros built-in) and softwareListRequested
// (SwList built-in) so MainWindow can close its own popups (antenna/mode)
// in addition to PopupManager-owned ones before showing the dialog/popup —
// MacroController does not have a direct handle on those.
class MacroController : public QObject {
    Q_OBJECT

public:
    // dialogParent: widget to anchor stub QMessageBox dialogs to (typically MainWindow).
    // Stored as QPointer so cleanup is safe if the parent widget is destroyed first.
    MacroController(ConnectionController *connection, PopupManager *popupManager, QWidget *dialogParent,
                    QObject *parent = nullptr);
    ~MacroController() override;

    // Public entry point for PF1-PF4 clicks and HardwareController's
    // macroRequested signal. Looks up the CAT command for the given
    // functionId in RadioSettings and dispatches it. Silently no-ops if
    // the function has no configured macro.
    void executeMacro(const QString &functionId);

signals:
    // Emitted when the user invokes the built-in "Macros" function from
    // the Fn popup. MainWindow listens and opens the macro-config dialog
    // after closing its own popups.
    void macroDialogRequested();

    // Emitted when the user invokes the built-in "SW LIST" function from
    // the Fn popup. MainWindow listens and opens the read-only Software
    // List popup after closing its own popups.
    void softwareListRequested();
    void ftxRequested();
    void sstvRequested();
    void logbookRequested();

private slots:
    // Connected to PopupManager::macroFunctionTriggered in constructor.
    void onFunctionTriggered(const QString &functionId);

private:
    ConnectionController *m_connection; // injected, not owned
    PopupManager *m_popupManager;       // injected, not owned
    QPointer<QWidget> m_dialogParent;   // injected, not owned
};

#endif // MACROCONTROLLER_H

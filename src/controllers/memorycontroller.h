#ifndef MEMORYCONTROLLER_H
#define MEMORYCONTROLLER_H

#include <QObject>
#include <QString>

class ConnectionController;
class MemoryModel;
class MemoryOverlayWidget;
class SpectrumController;
class QWidget;

// The K4's frequency memories: owns the MemoryModel and the RCL / STO list overlay, and turns the
// operator's choice into CAT.
//
// WHY QK4 shows its own list: STO and RCL used to send the radio's switch codes (SW20 / SW34),
// which open the list on the K4's front panel — where an operator sitting at a remote client
// cannot see it, let alone choose from it.
//
// See PATTERNS.md → Controller Pattern, and docs/controllers.md.
class MemoryController : public QObject {
    Q_OBJECT

public:
    MemoryController(ConnectionController *connection, SpectrumController *spectrum, QWidget *parentWidget,
                     QObject *parent = nullptr);

    // CAT routing from MainWindow, mirroring MenuController's ingest methods.
    void ingestMcdf(const QString &cmd);      // MCDF list/one-channel responses
    void ingestSelection(const QString &cmd); // MC / MCNF / MCNU

    // Ask the radio for the channel list and the selected channel. Called once the radio is ready:
    // the RDY dump carries neither.
    void requestMemories();

    // Drop every channel — the radio is the only source, so a reconnect starts empty.
    void clearModel();

    // Recall a channel by number: the overlay's RCL key, and a cell on the band popup's MEM or
    // GEN page.
    void recallChannel(int channel);

    void openRecall();
    void openStore();
    void closeOverlay();
    void repositionOverlay();

    MemoryModel *model() const { return m_memoryModel; }

private:
    void store(int channel);

    ConnectionController *m_connection; // injected, not owned
    SpectrumController *m_spectrum;     // injected, not owned
    QWidget *m_parentWidget;            // injected, not owned
    MemoryModel *m_memoryModel;         // owned (QObject child)
    MemoryOverlayWidget *m_overlay;     // owned by m_parentWidget
};

#endif // MEMORYCONTROLLER_H

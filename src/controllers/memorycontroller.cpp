#include "memorycontroller.h"

#include "connectioncontroller.h"
#include "models/memorymodel.h"
#include "spectrumcontroller.h"
#include "ui/overlays/memoryoverlay.h"

#include <QPoint>
#include <QWidget>

MemoryController::MemoryController(ConnectionController *connection, SpectrumController *spectrum,
                                   QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_connection(connection), m_spectrum(spectrum), m_parentWidget(parentWidget),
      m_memoryModel(new MemoryModel(this)), m_overlay(new MemoryOverlayWidget(m_memoryModel, parentWidget)) {
    m_overlay->hide();
    connect(m_overlay, &MemoryOverlayWidget::recallRequested, this, &MemoryController::recallChannel);
    connect(m_overlay, &MemoryOverlayWidget::storeRequested, this, &MemoryController::store);
}

void MemoryController::ingestMcdf(const QString &cmd) {
    m_memoryModel->parseMCDF(cmd);
}

void MemoryController::ingestSelection(const QString &cmd) {
    m_memoryModel->parseSelection(cmd);
}

void MemoryController::requestMemories() {
    if (!m_connection->isConnected())
        return;
    // Neither is in the RDY dump, measured on a K4D at R41.FT1. MCDF with no channel number lists
    // every channel that is not blank, which on a stock radio is a handful of lines.
    m_connection->sendCAT(QStringLiteral("MCDF;"));
    m_connection->sendCAT(QStringLiteral("MC;"));
}

void MemoryController::clearModel() {
    m_memoryModel->clear();
}

void MemoryController::openRecall() {
    refreshBeforeShowing();
    m_overlay->showForMode(MemoryOverlayWidget::Recall);
}

void MemoryController::openStore() {
    refreshBeforeShowing();
    m_overlay->showForMode(MemoryOverlayWidget::Store);
}

void MemoryController::refreshBeforeShowing() {
    repositionOverlay();
    // WHY ask every time: the radio never pushes MCDF. A channel named or stored on the front
    // panel, or by another client, while QK4 was connected would otherwise never reach the list -
    // it would show whatever was true when the connection came up. The answer is a handful of
    // lines, one per channel in use.
    requestMemories();
}

void MemoryController::closeOverlay() {
    if (m_overlay->isVisible())
        m_overlay->hide();
}

void MemoryController::repositionOverlay() {
    // Over the panadapter, which is where the radio draws its own list.
    if (!m_spectrum || !m_spectrum->spectrumContainer() || !m_parentWidget)
        return;
    const QPoint pos = m_spectrum->spectrumContainer()->mapTo(m_parentWidget, QPoint(0, 0));
    m_overlay->setGeometry(pos.x(), pos.y(), m_spectrum->spectrumContainer()->width(),
                           m_spectrum->spectrumContainer()->height());
}

void MemoryController::recallChannel(int channel) {
    if (!m_connection->isConnected())
        return;
    // The radio applies the channel and reports the frequency, mode and the rest as its own
    // changes, so nothing is predicted here.
    m_connection->sendCAT(QStringLiteral("MCR%1;").arg(channel, 3, 10, QChar('0')));
}

void MemoryController::store(int channel) {
    if (!m_connection->isConnected())
        return;
    // MCS stores into the SELECTED channel, so the selection has to move first. Reading the
    // channel back afterwards is what updates the list: the radio does not report what it stored.
    const QString number = QStringLiteral("%1").arg(channel, 3, 10, QChar('0'));
    m_connection->sendCAT(QStringLiteral("MC%1;").arg(number));
    m_connection->sendCAT(QStringLiteral("MCS;"));
    m_connection->sendCAT(QStringLiteral("MCDF%1;").arg(number));
}

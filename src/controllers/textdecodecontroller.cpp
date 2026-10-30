#include "textdecodecontroller.h"
#include "utils/radioutils.h"
#include "logging/logcategories.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "ui/dialogs/textdecodewindow.h"

#include <QLoggingCategory>
#include <QWidget>

namespace {

TextDecodeWindow::OperatingMode operatingModeFor(RadioState::Mode mode, int dataSubMode) {
    if (mode == RadioState::CW || mode == RadioState::CW_R)
        return TextDecodeWindow::ModeCW;
    if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
        switch (dataSubMode) {
        case 1:
            return TextDecodeWindow::ModeAFSK;
        case 2:
            return TextDecodeWindow::ModeFSK;
        case 3:
            return TextDecodeWindow::ModePSK;
        default:
            return TextDecodeWindow::ModeData;
        }
    }
    if (mode == RadioState::LSB || mode == RadioState::USB)
        return TextDecodeWindow::ModeSSB;
    return TextDecodeWindow::ModeOther;
}

} // namespace

TextDecodeController::TextDecodeController(RadioState *radioState, ConnectionController *connection,
                                           QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection),
      m_mainWindow(new TextDecodeWindow(TextDecodeWindow::MainRx, parentWidget)),
      m_subWindow(new TextDecodeWindow(TextDecodeWindow::SubRx, parentWidget)) {

    // MAIN RX window — user-driven events trigger CAT send.
    connect(m_mainWindow, &TextDecodeWindow::enabledChanged, this,
            [this](bool) { sendTextDecodeCmd(m_mainWindow, true); });
    connect(m_mainWindow, &TextDecodeWindow::wpmRangeChanged, this, [this](int) {
        if (m_mainWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::thresholdModeChanged, this, [this](bool) {
        if (m_mainWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::thresholdChanged, this, [this](int) {
        if (m_mainWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_mainWindow, true);
    });
    connect(m_mainWindow, &TextDecodeWindow::closeRequested, this, [this]() {
        m_mainWindow->setDecodeEnabled(false);
        sendTextDecodeCmd(m_mainWindow, true);
        m_mainWindow->clearText();
        m_mainWindow->hide();
    });

    // SUB RX window — symmetric wiring.
    connect(m_subWindow, &TextDecodeWindow::enabledChanged, this,
            [this](bool) { sendTextDecodeCmd(m_subWindow, false); });
    connect(m_subWindow, &TextDecodeWindow::wpmRangeChanged, this, [this](int) {
        if (m_subWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::thresholdModeChanged, this, [this](bool) {
        if (m_subWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::thresholdChanged, this, [this](int) {
        if (m_subWindow->isDecodeEnabled())
            sendTextDecodeCmd(m_subWindow, false);
    });
    connect(m_subWindow, &TextDecodeWindow::closeRequested, this, [this]() {
        m_subWindow->setDecodeEnabled(false);
        sendTextDecodeCmd(m_subWindow, false);
        m_subWindow->clearText();
        m_subWindow->hide();
    });

    // User-driven data rate changes → send DR / DR$ command.
    connect(m_mainWindow, &TextDecodeWindow::dataRateChanged, this, [this](int rate) {
        if (m_connection->isConnected()) {
            m_radioState->setDataRate(rate);
            m_connection->sendCAT(QString("DR%1;").arg(rate));
        }
    });
    connect(m_subWindow, &TextDecodeWindow::dataRateChanged, this, [this](int rate) {
        if (m_connection->isConnected()) {
            m_radioState->setDataRateB(rate);
            m_connection->sendCAT(QString("DR$%1;").arg(rate));
        }
    });

    // Radio-driven data rate echoes → update window display.
    connect(m_radioState, &RadioState::dataRateChanged, this, [this](int rate) { m_mainWindow->setDataRate(rate); });
    connect(m_radioState, &RadioState::dataRateBChanged, this, [this](int rate) { m_subWindow->setDataRate(rate); });

    // Radio text-decode config echoes → sync window state.
    connect(m_radioState, &RadioState::textDecodeChanged, this, [this]() {
        int mode = m_radioState->textDecodeMode();
        bool enabled = (mode > 0);
        m_mainWindow->setDecodeEnabled(enabled);
        if (mode >= 2 && mode <= 4)
            m_mainWindow->setWpmRange(mode - 2);
        int threshold = m_radioState->textDecodeThreshold();
        m_mainWindow->setAutoThreshold(threshold == 0);
        if (threshold > 0)
            m_mainWindow->setThreshold(threshold);
        m_mainWindow->setMaxLines(m_radioState->textDecodeLines());
    });
    connect(m_radioState, &RadioState::textDecodeBChanged, this, [this]() {
        int mode = m_radioState->textDecodeModeB();
        bool enabled = (mode > 0);
        m_subWindow->setDecodeEnabled(enabled);
        if (mode >= 2 && mode <= 4)
            m_subWindow->setWpmRange(mode - 2);
        int threshold = m_radioState->textDecodeThresholdB();
        m_subWindow->setAutoThreshold(threshold == 0);
        if (threshold > 0)
            m_subWindow->setThreshold(threshold);
        m_subWindow->setMaxLines(m_radioState->textDecodeLinesB());
    });

    // Mode changes while a window is open → refresh its operating mode so
    // the title bar / controls reflect the new mode (CW WPM vs data rate).
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        if (m_mainWindow->isVisible())
            m_mainWindow->setOperatingMode(operatingModeFor(mode, m_radioState->dataSubMode()));
    });
    connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode mode) {
        if (m_subWindow->isVisible())
            m_subWindow->setOperatingMode(operatingModeFor(mode, m_radioState->dataSubModeB()));
    });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int subMode) {
        if (m_mainWindow->isVisible())
            m_mainWindow->setOperatingMode(operatingModeFor(m_radioState->mode(), subMode));
    });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this](int subMode) {
        if (m_subWindow->isVisible())
            m_subWindow->setOperatingMode(operatingModeFor(m_radioState->modeB(), subMode));
    });

    // Decoded text buffer from radio → route to the correct window.
    connect(m_radioState, &RadioState::textBufferReceived, this, [this](const QString &text, bool isSubRx) {
        if (isSubRx) {
            m_subWindow->appendText(text);
        } else {
            m_mainWindow->appendText(text);
        }
    });
}

void TextDecodeController::showMainRx() {
    RadioState::Mode mode = m_radioState->mode();
    m_mainWindow->setOperatingMode(operatingModeFor(mode, m_radioState->dataSubMode()));
    if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
        int dr = m_radioState->dataRate();
        if (dr >= 0)
            m_mainWindow->setDataRate(dr);
    }
    m_mainWindow->show();
    if (!m_mainWindow->isDecodeEnabled()) {
        m_mainWindow->setDecodeEnabled(true);
        sendTextDecodeCmd(m_mainWindow, true);
    }
}

void TextDecodeController::showSubRx() {
    RadioState::Mode modeB = m_radioState->modeB();
    m_subWindow->setOperatingMode(operatingModeFor(modeB, m_radioState->dataSubModeB()));
    if (modeB == RadioState::DATA || modeB == RadioState::DATA_R) {
        int dr = m_radioState->dataRateB();
        if (dr >= 0)
            m_subWindow->setDataRate(dr);
    }
    m_subWindow->show();
    if (!m_subWindow->isDecodeEnabled()) {
        m_subWindow->setDecodeEnabled(true);
        sendTextDecodeCmd(m_subWindow, false);
    }
}

void TextDecodeController::sendTextDecodeCmd(TextDecodeWindow *window, bool isMainRx) {
    if (!m_connection->isConnected())
        return;
    // TRANSMIT SAFETY: TD is a decode command only in CW and DATA. In SSB its 1 arms the 2-TONE
    // generator, so the next PTT would send two tones instead of the microphone. Decided from the
    // radio's own mode for this receiver, not the window's label, and nothing is sent otherwise -
    // not even TD0, which in SSB would switch off a 2-TONE the operator set.
    const RadioState::Mode rxMode = isMainRx ? m_radioState->mode() : m_radioState->modeB();
    if (!RadioUtils::textDecodeSetAllowed(static_cast<int>(rxMode))) {
        qCDebug(lcUiTextDecode) << "not sending TD: no text decoder in mode" << rxMode;
        return;
    }
    int mode = 0;
    int threshold = 0;
    if (window->isDecodeEnabled()) {
        auto opMode = window->operatingMode();
        if (opMode == TextDecodeWindow::ModeCW) {
            mode = 2 + window->wpmRange();
            threshold = window->autoThreshold() ? 0 : window->threshold();
        } else {
            mode = 1;
        }
    }
    QString cmdPrefix = isMainRx ? "TD" : "TD$";
    QString cmd = QString("%1%2%3%4;").arg(cmdPrefix).arg(mode).arg(threshold).arg(window->maxLines());
    qCDebug(lcUiTextDecode) << "sending" << cmd;
    m_connection->sendCAT(cmd);
}

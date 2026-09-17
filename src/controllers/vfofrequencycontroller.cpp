#include "vfofrequencycontroller.h"

#include "controllers/connectioncontroller.h"
#include "models/radiostate.h"
#include "ui/widgets/vfowidget.h"
#include "utils/radioutils.h"

#include <QString>

namespace {

// Format a Hz frequency as "XX.XXX.XXX" grouped-3-from-right.
QString formatFrequency(quint64 freq) {
    QString freqStr = QString::number(freq);
    while (freqStr.length() < 8)
        freqStr.prepend('0');
    QString formatted;
    const int len = freqStr.length();
    for (int i = 0; i < len; i++) {
        formatted.append(freqStr[i]);
        const int posFromEnd = len - i - 1;
        if (posFromEnd > 0 && posFromEnd % 3 == 0)
            formatted.append('.');
    }
    // Frequencies < 10 MHz (160m-40m) render without a leading zero.
    if (formatted.startsWith('0'))
        formatted = formatted.mid(1);
    return formatted;
}

} // namespace

VfoFrequencyController::VfoFrequencyController(RadioState *radioState, ConnectionController *connection,
                                               VFOWidget *vfoA, VFOWidget *vfoB, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_vfoA(vfoA), m_vfoB(vfoB) {
    connect(m_radioState, &RadioState::frequencyChanged, this, &VfoFrequencyController::onFrequencyChanged);
    connect(m_radioState, &RadioState::frequencyBChanged, this, &VfoFrequencyController::onFrequencyBChanged);
    connect(m_vfoA, &VFOWidget::tuningDigitClicked, this,
            [this](int digitFromRight) { setTuningRateFromDigit(false, digitFromRight); });
    connect(m_vfoB, &VFOWidget::tuningDigitClicked, this,
            [this](int digitFromRight) { setTuningRateFromDigit(true, digitFromRight); });
}

VfoFrequencyController::~VfoFrequencyController() {
    disconnect(this);
}

void VfoFrequencyController::refresh() {
    refreshVfoA();
    refreshVfoB();
}

void VfoFrequencyController::refreshVfoA() {
    onFrequencyChanged(m_radioState->vfoA());
}

void VfoFrequencyController::refreshVfoB() {
    onFrequencyBChanged(m_radioState->vfoB());
}

void VfoFrequencyController::setTuningRateFromDigit(bool vfoB, int digitFromRight) {
    const int step = RadioUtils::tuningStepForDigit(digitFromRight);
    if (step < 0 || !m_connection->isConnected())
        return;
    const QString cmd = QString("%1%2;").arg(vfoB ? "VT$" : "VT").arg(step);
    m_connection->sendCAT(cmd);
    // Optimistic, like the RATE button: the underline moves now rather than on the radio's echo.
    m_radioState->parseCATCommand(cmd);
}

void VfoFrequencyController::toggleFrequencyEntry() {
    for (VFOWidget *vfo : {m_vfoA, m_vfoB}) {
        if (vfo->isFrequencyEntryActive()) {
            vfo->cancelFrequencyEntry();
            return;
        }
    }
    (m_radioState->bSetEnabled() ? m_vfoB : m_vfoA)->beginFrequencyEntry();
}

void VfoFrequencyController::onFrequencyChanged(quint64 freq) {
    // WHY: the shown frequency is the *dial* plus whichever offset is
    // currently active. When transmitting with XIT (no split), VFO A is
    // the TX VFO and its display shows dial+XIT. With RIT, displayed RX
    // frequency = dial + RO.
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && !m_radioState->splitEnabled()) {
        const qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabled()) {
        const qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoA->setFrequency(formatFrequency(freq));
}

void VfoFrequencyController::onFrequencyBChanged(quint64 freq) {
    // WHY: in split mode, VFO B is the TX VFO and XIT offset lives in
    // RO$. With BSET + RIT (VFO B as receive VFO), the RIT offset also
    // lives in RO$.
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && m_radioState->splitEnabled()) {
        const qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabledB()) {
        const qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoB->setFrequency(formatFrequency(freq));
}

#include "catpushbroadcaster.h"

#include "catframes.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(netCatPush, "net.catpush")

CatPushBroadcaster::CatPushBroadcaster(RadioState *state, QObject *parent) : QObject(parent), m_radioState(state) {
    connect(m_radioState, &RadioState::frequencyChanged, this, &CatPushBroadcaster::onFrequencyChanged);
    connect(m_radioState, &RadioState::frequencyBChanged, this, &CatPushBroadcaster::onFrequencyBChanged);
    connect(m_radioState, &RadioState::modeChanged, this, &CatPushBroadcaster::onModeChanged);
    connect(m_radioState, &RadioState::modeBChanged, this, &CatPushBroadcaster::onModeBChanged);
    connect(m_radioState, &RadioState::transmitStateChanged, this, &CatPushBroadcaster::onTransmitStateChanged);
    connect(m_radioState, &RadioState::splitChanged, this, &CatPushBroadcaster::onSplitChanged);
    connect(m_radioState, &RadioState::ritXitChanged, this, &CatPushBroadcaster::onRitXitChanged);
    connect(m_radioState, &RadioState::rfPowerChanged, this, &CatPushBroadcaster::onRfPowerChanged);
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, &CatPushBroadcaster::onFilterBandwidthChanged);
    connect(m_radioState, &RadioState::keyerSpeedChanged, this, &CatPushBroadcaster::onKeyerSpeedChanged);
    connect(m_radioState, &RadioState::voxChanged, this, &CatPushBroadcaster::onVoxChanged);
    connect(m_radioState, &RadioState::diversityChanged, this, &CatPushBroadcaster::onDiversityChanged);
    connect(m_radioState, &RadioState::dataSubModeChanged, this, &CatPushBroadcaster::onDataSubModeChanged);
    connect(m_radioState, &RadioState::processingChanged, this, &CatPushBroadcaster::onProcessingChanged);
}

CatPushBroadcaster::~CatPushBroadcaster() = default;

void CatPushBroadcaster::addClient(QTcpSocket *client) {
    m_clientAiModes.insert(client, 0);
    qCDebug(netCatPush) << "client added" << client << "subscribers" << m_clientAiModes.size();
}

void CatPushBroadcaster::removeClient(QTcpSocket *client) {
    m_clientAiModes.remove(client);
    qCDebug(netCatPush) << "client removed" << client << "subscribers" << m_clientAiModes.size();
}

void CatPushBroadcaster::setClientAiMode(QTcpSocket *client, int mode) {
    m_clientAiModes.insert(client, mode);
    qCDebug(netCatPush) << "client" << client << "AI mode set to" << mode;
}

int CatPushBroadcaster::clientAiMode(QTcpSocket *client) const {
    return m_clientAiModes.value(client, 0);
}

int CatPushBroadcaster::subscriberCount() const {
    return m_clientAiModes.size();
}

void CatPushBroadcaster::onFrequencyChanged(quint64 freq) {
    broadcast(CatFrames::frequencyA(freq));
}

void CatPushBroadcaster::onFrequencyBChanged(quint64 freq) {
    broadcast(CatFrames::frequencyB(freq));
}

void CatPushBroadcaster::onModeChanged(RadioState::Mode m) {
    broadcast(CatFrames::modeA(m));
    // WHY: K3/K4-era loggers (RumlogNG, N1MM) parse mode out of the IF composite,
    // not the standalone MD frame. Matches real-K4 AI2 behavior (IF on every change).
    broadcast(CatFrames::ifFrame(*m_radioState));
}

void CatPushBroadcaster::onModeBChanged(RadioState::Mode m) {
    broadcast(CatFrames::modeB(m));
}

void CatPushBroadcaster::onTransmitStateChanged(bool tx) {
    broadcast(CatFrames::ptt(tx));
    broadcast(CatFrames::ifFrame(*m_radioState));
}

void CatPushBroadcaster::onSplitChanged(bool on) {
    broadcast(CatFrames::split(on));
}

void CatPushBroadcaster::onRitXitChanged(bool rit, bool xit, int offset) {
    broadcast(CatFrames::ritOffset(offset));
    broadcast(CatFrames::ritEnabled(rit));
    broadcast(CatFrames::xitEnabled(xit));
}

void CatPushBroadcaster::onRfPowerChanged(double value, LevelsState::PowerRange range) {
    // XVTR is still skipped: a bare PC reply carries no range suffix, so a subscriber has no way
    // to tell milliwatts from watts and would read 5 mW as 5 W. The K4-direct PC echo still
    // reaches them by passive forwarding. rfPower() now scales the value for the range it is
    // given (CAT-005), so QRP no longer goes out ten times low.
    if (range == LevelsState::PowerRange::Xvtr)
        return;
    broadcast(CatFrames::rfPower(value, range));
}

void CatPushBroadcaster::onFilterBandwidthChanged(int bw) {
    broadcast(CatFrames::filterBandwidth(bw));
}

void CatPushBroadcaster::onKeyerSpeedChanged(int wpm) {
    broadcast(CatFrames::keyerSpeed(wpm));
}

void CatPushBroadcaster::onVoxChanged(bool on) {
    broadcast(CatFrames::vox(on));
}

void CatPushBroadcaster::onDiversityChanged(bool on) {
    broadcast(CatFrames::diversity(on));
}

void CatPushBroadcaster::onDataSubModeChanged(int subMode) {
    broadcast(CatFrames::dataSubMode(subMode));
}

void CatPushBroadcaster::onProcessingChanged() {
    const bool nb = m_radioState->noiseBlankerEnabled();
    const bool nr = m_radioState->noiseReductionEnabled();
    const int agc = static_cast<int>(m_radioState->agcSpeed());

    if (!m_processingCacheInitialized) {
        // WHY: First fire after construction seeds the cache. Clients
        // subscribed to AI2 expect future *changes*, not the initial state.
        m_lastNB = nb;
        m_lastNR = nr;
        m_lastAGC = agc;
        m_processingCacheInitialized = true;
        return;
    }
    if (nb != m_lastNB) {
        broadcast(CatFrames::noiseBlanker(nb));
        m_lastNB = nb;
    }
    if (nr != m_lastNR) {
        broadcast(CatFrames::noiseReduction(nr));
        m_lastNR = nr;
    }
    if (agc != m_lastAGC) {
        broadcast(CatFrames::agcSpeed(agc));
        m_lastAGC = agc;
    }
}

void CatPushBroadcaster::broadcast(const QByteArray &frame) {
    int sent = 0;
    for (auto it = m_clientAiModes.constBegin(); it != m_clientAiModes.constEnd(); ++it) {
        QTcpSocket *sock = it.key();
        const int aiMode = it.value();
        if (aiMode == 0) {
            continue;
        }
        // WHY: socket may have disconnected between the *Changed signal firing
        // and removeClient() being called from CatServer's disconnect lambda.
        if (sock->state() != QAbstractSocket::ConnectedState) {
            continue;
        }
        sock->write(frame);
        ++sent;
    }
    qCDebug(netCatPush) << "push" << frame << "to" << sent << "subscribers";
}

#include "ft8session.h"

QString Ft8Session::directed(const QString &payload) const {
    return dxCall + " " + myCall + " " + payload;
}
void Ft8Session::halt() {
    armed = false;
}
void Ft8Session::clear() {
    halt();
    dxCall.clear();
    dxGrid.clear();
    nextMessage.clear();
    sentReport.clear();
    receivedReport.clear();
    callingCq = complete = false;
    retries = 0;
    started = {};
    ended = {};
    m_lastSent.clear();
    m_lastReceivedId.clear();
}
bool Ft8Session::select(const Ft8::Decode &decode) {
    auto m = Ft8::parseMessage(decode.message);
    if (!m.valid || m.from == myCall || decode.mode != mode)
        return false;
    clear();
    dxCall = m.from;
    dxGrid = m.grid;
    rxHz = decode.audioHz;
    if (!holdTx)
        txHz = rxHz;
    even = (Ft8::slot(decode.utc.toMSecsSinceEpoch(), mode) % 2) != 0;
    if (decode.snr)
        sentReport = Ft8::reportText(*decode.snr);
    nextMessage = directed(myGrid.left(4));
    if (m.to == myCall && !m.grid.isEmpty() && !sentReport.isEmpty())
        nextMessage = directed(sentReport);
    if (m.to == myCall && (m.payload.startsWith('+') || m.payload.startsWith('-'))) {
        receivedReport = m.payload;
        if (!sentReport.isEmpty())
            nextMessage = directed("R" + sentReport);
    }
    return true;
}
bool Ft8Session::callCq(const QString &direction) {
    if (!Ft8::validCall(myCall) || !Ft8::validGrid(myGrid))
        return false;
    clear();
    callingCq = true;
    nextMessage = QString("CQ %1%2 %3").arg(direction.isEmpty() ? "" : direction + " ", myCall, myGrid.left(4));
    return true;
}
bool Ft8Session::arm() {
    if (!Ft8::validCall(myCall) || !Ft8::validGrid(myGrid) || nextMessage.isEmpty() || complete)
        return false;
    retries = 0;
    m_lastSent.clear();
    armed = true;
    return true;
}
bool Ft8Session::setReport(int report) {
    if (report < -50 || report > 49 || dxCall.isEmpty())
        return false;
    sentReport = Ft8::reportText(report);
    if (!receivedReport.isEmpty())
        nextMessage = directed("R" + sentReport);
    return true;
}
bool Ft8Session::receive(const Ft8::Decode &decode) {
    if (!autoSequence || !armed || complete || decode.mode != mode || decode.id() == m_lastReceivedId)
        return false;
    auto m = Ft8::parseMessage(decode.message);
    if (!m.valid || m.to != myCall)
        return false;
    if (dxCall.isEmpty() && callingCq && callFirst) {
        const bool wasArmed = armed;
        if (!select(decode))
            return false;
        armed = wasArmed;
    }
    if (m.from != dxCall)
        return false;
    m_lastReceivedId = decode.id();
    QString next;
    if (decode.snr && sentReport.isEmpty())
        sentReport = Ft8::reportText(*decode.snr);
    if (!m.grid.isEmpty()) {
        dxGrid = m.grid;
        if (!sentReport.isEmpty())
            next = directed(sentReport);
    } else if (m.payload.startsWith("R+") || m.payload.startsWith("R-")) {
        if (!m_lastSent.endsWith(sentReport) || sentReport.isEmpty())
            return false;
        receivedReport = m.payload.mid(1);
        next = directed(useRr73 ? "RR73" : "RRR");
    } else if (m.payload.startsWith('+') || m.payload.startsWith('-')) {
        receivedReport = m.payload;
        if (!sentReport.isEmpty())
            next = directed("R" + sentReport);
    } else if (m.payload == "RRR" || m.payload == "RR73") {
        if (receivedReport.isEmpty() || sentReport.isEmpty() || m_lastSent != directed("R" + sentReport))
            return false;
        next = directed("73");
    } else if (m.payload == "73" && (m_lastSent.endsWith(" RRR") || m_lastSent.endsWith(" RR73"))) {
        complete = true;
        ended = decode.utc;
        halt();
        return true;
    }
    if (next.isEmpty())
        return false;
    if (next != nextMessage)
        retries = 0;
    nextMessage = next;
    return true;
}
bool Ft8Session::sent(const QDateTime &utc) {
    if (!armed || complete || nextMessage.isEmpty())
        return false;
    if (!started.isValid() && !callingCq)
        started = utc;
    retries = (m_lastSent == nextMessage) ? retries + 1 : 1;
    m_lastSent = nextMessage;
    if ((nextMessage.endsWith(" 73") || nextMessage.endsWith(" RR73")) && !receivedReport.isEmpty()) {
        complete = true;
        ended = utc;
        halt();
    } else if (retries >= maxRetries)
        halt();
    return true;
}

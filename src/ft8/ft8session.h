#pragma once
#include "ft8types.h"

// Pure QSO state shared by live scheduled transmission and no-RF practice.
// Only a completed live waveform advances sent() and its retry/QSO state.
class Ft8Session {
public:
    QString myCall, myGrid, dxCall, dxGrid;
    QString nextMessage, receivedReport, sentReport;
    Ft8::Mode mode = Ft8::Mode::FT8;
    QDateTime started, ended;
    int rxHz = 1500, txHz = 1500;
    bool holdTx = true, even = true, autoSequence = true, callFirst = false;
    bool armed = false, callingCq = false, complete = false, useRr73 = true;
    int maxRetries = 6, retries = 0;
    bool select(const Ft8::Decode &decode);
    bool callCq(const QString &direction = {});
    bool receive(const Ft8::Decode &decode);
    bool arm();
    void halt();
    void clear();
    bool sent(const QDateTime &utc);
    bool setReport(int report);

private:
    QString directed(const QString &payload) const;
    QString m_lastSent;
    QString m_lastReceivedId;
};

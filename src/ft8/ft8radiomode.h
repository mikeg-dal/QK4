#pragma once
#include <QString>

// Own only the temporary main-VFO mode selected by entering FT8/FT4.
class Ft8RadioMode {
public:
    void enter() { m_active = true; }
    void leave() {
        m_active = false;
        m_dataRequestPending = false;
    }
    void connectionLost() {
        m_originalMode = 0;
        m_originalSubMode = -1;
        m_dataRequestPending = false;
    }
    QString update(bool connected, int mode, int subMode, bool busy) {
        if (!connected) {
            connectionLost();
            return {};
        }
        if (busy)
            return {};
        if (m_active) {
            if (mode == 0 || ((mode == 6 || mode == 9) && subMode < 0))
                return {};
            if (!m_originalMode) {
                m_originalMode = mode;
                m_originalSubMode = subMode;
                m_dataRequestPending = true;
                // Same DATA-A selection and readback as the existing mode/audio setup.
                return dataModeCommand();
            }
            if (mode == 6 && subMode == 0) {
                m_dataRequestPending = false;
                return {};
            }
            // A K4 band change may recall that band's previous mode. Keep the
            // digital screen in DATA-A without replacing the mode saved on entry.
            if (!m_dataRequestPending) {
                m_dataRequestPending = true;
                return dataModeCommand();
            }
            return {};
        }
        if (!m_originalMode)
            return {};
        QString command = QString("MD%1;").arg(m_originalMode);
        if (m_originalSubMode >= 0)
            command += QString("DT%1;").arg(m_originalSubMode);
        connectionLost(); // Clear before sending: readback must not restore twice.
        return command + "MD;DT;LI;MG;CP;TE;";
    }

private:
    static QString dataModeCommand() { return "MD6;DT0;MD;DT;LI;MG;CP;TE;"; }
    bool m_active = false;
    bool m_dataRequestPending = false;
    int m_originalMode = 0, m_originalSubMode = -1;
};

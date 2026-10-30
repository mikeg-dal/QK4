#ifndef MODELS_RADIOSTATE_REPORTEDFLAG_H
#define MODELS_RADIOSTATE_REPORTEDFLAG_H

#include <cstdint>

// An on/off setting the radio reports (FT, RT, LK, NB on/off, VX …). Unknown (-1) until the radio's
// first report, and again after reset().
//
// WHY not a plain bool: every handler emits only on a change. A bool reset to false swallows the
// radio's first report whenever that report is also "off" (FT0, BS0, VXC0 in the RDY; dump), so no
// signal fires and every consumer keeps the previous session's value. From unknown, any report is a
// change, so the connection dump is always applied.
//
// No conversion to bool on purpose: a read must say what unknown means — isOn() treats it as off,
// valueOr() names another default.
struct ReportedFlag {
    static constexpr std::int8_t Unknown = -1;

    std::int8_t state = Unknown;

    bool isKnown() const { return state != Unknown; }
    bool isOn() const { return state == 1; }
    bool valueOr(bool fallback) const { return isKnown() ? isOn() : fallback; }

    // Stores `on`. Returns true when that is a change, which it always is from unknown.
    bool set(bool on) {
        const std::int8_t next = on ? 1 : 0;
        if (next == state)
            return false;
        state = next;
        return true;
    }

    void reset() { state = Unknown; }
};

#endif // MODELS_RADIOSTATE_REPORTEDFLAG_H

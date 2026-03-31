#include "radioutils.h"
#include <QtGlobal>

namespace RadioUtils {

int tuningStepToHz(int step) {
    static const int table[] = {1, 10, 100, 1000, 10000, 100};
    return (step >= 0 && step <= 5) ? table[step] : 1000;
}

int getBandFromFrequency(quint64 freq) {
    if (freq >= 1800000 && freq <= 2000000)
        return 0; // 160m
    if (freq >= 3500000 && freq <= 4000000)
        return 1; // 80m
    if (freq >= 5330500 && freq <= 5405500)
        return 2; // 60m
    if (freq >= 7000000 && freq <= 7300000)
        return 3; // 40m
    if (freq >= 10100000 && freq <= 10150000)
        return 4; // 30m
    if (freq >= 14000000 && freq <= 14350000)
        return 5; // 20m
    if (freq >= 18068000 && freq <= 18168000)
        return 6; // 17m
    if (freq >= 21000000 && freq <= 21450000)
        return 7; // 15m
    if (freq >= 24890000 && freq <= 24990000)
        return 8; // 12m
    if (freq >= 28000000 && freq <= 29700000)
        return 9; // 10m
    if (freq >= 50000000 && freq <= 54000000)
        return 10; // 6m
    if (freq >= 144000000)
        return 16; // XVTR (transverter bands 16-25)
    return -1;     // Out of band / GEN coverage
}

int getNextSpanUp(int currentSpan) {
    if (currentSpan >= SPAN_MAX)
        return SPAN_MAX;
    int increment = (currentSpan < SPAN_THRESHOLD_UP) ? 1000 : 4000;
    int newSpan = currentSpan + increment;
    return qMin(newSpan, SPAN_MAX);
}

int getNextSpanDown(int currentSpan) {
    if (currentSpan <= SPAN_MIN)
        return SPAN_MIN;
    int decrement = (currentSpan > SPAN_THRESHOLD_DOWN) ? 4000 : 1000;
    int newSpan = currentSpan - decrement;
    return qMax(newSpan, SPAN_MIN);
}

} // namespace RadioUtils

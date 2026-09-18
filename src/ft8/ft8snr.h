#pragma once
#include "ft8types.h"
#include <memory>

// WSJT-X 2.7.0 (b4f9a4) power estimators, in the 2500 Hz reference bandwidth.
// One instance per decode pass shares the spectrum and FFT across stations.
class Ft8Snr {
public:
    Ft8Snr(const QVector<float> &samples, Ft8::Mode mode);
    ~Ft8Snr();
    std::optional<int> estimate(const uint8_t *payload, double frequency, double startSeconds);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

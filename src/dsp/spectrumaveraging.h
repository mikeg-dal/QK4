#pragma once

#include <algorithm>

namespace SpectrumAveraging {

inline float update(float previous, float sample, int level) {
    const int count = std::clamp(level, 1, 20);
    // Equal attack and decay keep averaging from raising the measured noise floor.
    return previous + (sample - previous) / static_cast<float>(count);
}

} // namespace SpectrumAveraging

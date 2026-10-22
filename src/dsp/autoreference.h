#pragma once

#include <algorithm>
#include <cmath>

namespace AutoReference {

struct State {
    bool initialized = false;
    float smoothedFloor = 0.0f;
    float level = 0.0f;
    int pendingFrames = 0;
};

inline float reference(const State &state, bool enabled, float manualReference) {
    return enabled && state.initialized ? state.level : manualReference;
}

inline bool update(State &state, float noiseFloor, bool enabled, int averaging, int debounce, int offsetDb) {
    if (!enabled || !std::isfinite(noiseFloor) || noiseFloor < -200.0f || noiseFloor > 50.0f)
        return false;

    const float offset = static_cast<float>(std::clamp(offsetDb, -8, 8));
    if (!state.initialized) {
        state.smoothedFloor = noiseFloor;
        state.level = noiseFloor - offset;
        state.initialized = true;
        state.pendingFrames = 0;
        return true;
    }

    const float alpha = 1.0f / static_cast<float>(std::clamp(averaging, 1, 30));
    state.smoothedFloor = alpha * noiseFloor + (1.0f - alpha) * state.smoothedFloor;
    if (++state.pendingFrames < std::clamp(debounce, 1, 18))
        return false;

    state.level = state.smoothedFloor - offset;
    state.pendingFrames = 0;
    return true;
}

} // namespace AutoReference

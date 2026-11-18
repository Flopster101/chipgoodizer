#pragma once

#include <cmath>

namespace chipgoodizer
{

inline float softClip(float x, float threshold = 0.80f)
{
    float absX = std::abs(x);
    if (absX <= threshold)
        return x;

    float span = 1.0f - threshold;
    float excess = (absX - threshold) / span;
    float compressed = threshold + span * std::tanh(excess);
    return (x > 0.0f) ? compressed : -compressed;
}

} // namespace chipgoodizer

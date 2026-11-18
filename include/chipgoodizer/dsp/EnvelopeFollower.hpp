#pragma once

#include <cmath>

namespace chipgoodizer
{

class EnvelopeFollower
{
public:
    void init(float sampleRate, float attackMs, float releaseMs)
    {
        fs = sampleRate;
        setTimes(attackMs, releaseMs);
        reset();
    }

    void setTimes(float attackMs, float releaseMs)
    {
        attCoef = (attackMs > 0.0f) ? std::exp(-1.0f / (attackMs * 0.001f * fs)) : 0.0f;
        relCoef = (releaseMs > 0.0f) ? std::exp(-1.0f / (releaseMs * 0.001f * fs)) : 0.0f;
    }

    inline float process(float input)
    {
        const float target = std::abs(input);
        if (target > env)
            env = attCoef * env + (1.0f - attCoef) * target;
        else
            env = relCoef * env + (1.0f - relCoef) * target;
        return env;
    }

    void reset()
    {
        env = 0.0f;
    }

private:
    float fs{44100.0f};
    float attCoef{0.0f};
    float relCoef{0.0f};
    float env{0.0f};
};

} // namespace chipgoodizer

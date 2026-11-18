#pragma once

#include "MathConstants.hpp"

#include <cmath>

namespace chipgoodizer
{

class PhaseRotator
{
public:
    void init(float fc, float fs)
    {
        float w0 = 2.0f * M_PI * fc / fs;
        float t = std::tan(w0 * 0.5f);
        a = (t - 1.0f) / (t + 1.0f);
        reset();
    }

    inline void process(float &sampleL, float &sampleR)
    {
        sampleL = processChannel(sampleL, xL, yL);
        sampleR = processChannel(sampleR, xR, yR);
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i)
        {
            xL[i] = yL[i] = 0.0f;
            xR[i] = yR[i] = 0.0f;
        }
    }

private:
    inline float processChannel(float in, float x[4], float y[4])
    {
        float s = in;
        for (int i = 0; i < 4; ++i)
        {
            float out = a * (s - y[i]) + x[i];
            x[i] = s;
            y[i] = out;
            s = out;
        }
        return s;
    }

    float a{0.0f};
    float xL[4]{0.0f}, yL[4]{0.0f};
    float xR[4]{0.0f}, yR[4]{0.0f};
};

} // namespace chipgoodizer

#pragma once

#include "Biquad.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chipgoodizer
{

// Schroeder room: four damped combs on the mono sum, 250 Hz send high pass.
class StereoAmbience
{
public:
    void init(float sampleRate)
    {
        static const float kDelayTimes[4] = {0.0123f, 0.0157f, 0.0191f, 0.0233f};

        for (int i = 0; i < 4; ++i)
        {
            combLengths[i] = static_cast<size_t>(std::round(kDelayTimes[i] * sampleRate));
            combBuffers[i].assign(combLengths[i], 0.0f);
        }

        highPass.setHighPass(250.0f, sampleRate, 0.707f);
        reset();
    }

    inline void process(float inL, float inR, float &wetL, float &wetR)
    {
        float monoIn = 0.5f * (inL + inR);
        float filtered = highPass.process(monoIn);

        float outComb[4];
        for (int i = 0; i < 4; ++i)
        {
            const size_t index = combIndex[i];
            const float delayed = combBuffers[i][index];

            dampState[i] = delayed * (1.0f - kDamp) + dampState[i] * kDamp;
            combBuffers[i][index] = filtered + dampState[i] * kFeedback;
            combIndex[i] = (index + 1) % combLengths[i];

            outComb[i] = delayed;
        }

        // Left takes combs 0/2, right 1/3.
        wetL = outComb[0] - outComb[2];
        wetR = outComb[1] - outComb[3];
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i)
        {
            std::fill(combBuffers[i].begin(), combBuffers[i].end(), 0.0f);
            combIndex[i] = 0;
            dampState[i] = 0.0f;
        }
        highPass.reset();
    }

private:
    // ~0.32 s tight room decay with high-frequency damping.
    static constexpr float kFeedback = 0.40f;
    static constexpr float kDamp = 0.35f;

    size_t combLengths[4]{};
    std::vector<float> combBuffers[4];
    size_t combIndex[4]{};
    float dampState[4]{};
    Biquad highPass;
};

} // namespace chipgoodizer

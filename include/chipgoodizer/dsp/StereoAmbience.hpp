#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace chipgoodizer
{

class StereoAmbience
{
public:
    void init(float sampleRate)
    {
        fs = sampleRate;
        combLengths[0] = static_cast<size_t>(0.0062f * fs);
        combLengths[1] = static_cast<size_t>(0.0078f * fs);
        combLengths[2] = static_cast<size_t>(0.0094f * fs);
        combLengths[3] = static_cast<size_t>(0.0112f * fs);

        apLengths[0] = static_cast<size_t>(0.0021f * fs);
        apLengths[1] = static_cast<size_t>(0.0033f * fs);

        for (int i = 0; i < 4; ++i)
        {
            combBuf[i].assign(combLengths[i], 0.0f);
            combIdx[i] = 0;
            combFilter[i] = 0.0f;
        }

        for (int i = 0; i < 2; ++i)
        {
            apBuf[i].assign(apLengths[i], 0.0f);
            apIdx[i] = 0;
        }
    }

    inline void process(float inL, float inR, float &wetL, float &wetR)
    {
        float monoIn = 0.5f * (inL + inR);

        float outComb[4];
        for (int i = 0; i < 4; ++i)
        {
            float delayed = combBuf[i][combIdx[i]];
            combFilter[i] = delayed * 0.70f + combFilter[i] * 0.30f;
            combBuf[i][combIdx[i]] = monoIn + combFilter[i] * 0.65f;
            combIdx[i] = (combIdx[i] + 1) % combLengths[i];
            outComb[i] = delayed;
        }

        float rawL = outComb[0] - outComb[1] + 0.5f * (outComb[2] - outComb[3]);
        float rawR = outComb[2] - outComb[3] + 0.5f * (outComb[0] - outComb[1]);

        wetL = processAllpass(rawL, 0);
        wetR = processAllpass(rawR, 1);
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i)
        {
            std::fill(combBuf[i].begin(), combBuf[i].end(), 0.0f);
            combIdx[i] = 0;
            combFilter[i] = 0.0f;
        }
        for (int i = 0; i < 2; ++i)
        {
            std::fill(apBuf[i].begin(), apBuf[i].end(), 0.0f);
            apIdx[i] = 0;
        }
    }

private:
    inline float processAllpass(float in, int idx)
    {
        float delayed = apBuf[idx][apIdx[idx]];
        float out = -in + delayed;
        apBuf[idx][apIdx[idx]] = in + delayed * 0.60f;
        apIdx[idx] = (apIdx[idx] + 1) % apLengths[idx];
        return out;
    }

    float fs{44100.0f};
    size_t combLengths[4]{};
    std::vector<float> combBuf[4];
    size_t combIdx[4]{};
    float combFilter[4]{};

    size_t apLengths[2]{};
    std::vector<float> apBuf[2];
    size_t apIdx[2]{};
};

} // namespace chipgoodizer

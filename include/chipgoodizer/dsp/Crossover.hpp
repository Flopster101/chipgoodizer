#pragma once

#include "Biquad.hpp"

namespace chipgoodizer
{

class LinkwitzRiley4
{
public:
    void init(float fc, float fs, bool isHighPass)
    {
        if (isHighPass)
        {
            stage1.setHighPass(fc, fs);
            stage2.setHighPass(fc, fs);
        }
        else
        {
            stage1.setLowPass(fc, fs);
            stage2.setLowPass(fc, fs);
        }
    }

    inline float process(float x)
    {
        return stage2.process(stage1.process(x));
    }

    void reset()
    {
        stage1.reset();
        stage2.reset();
    }

private:
    Biquad stage1;
    Biquad stage2;
};

class Crossover3Band
{
public:
    void init(float lowCutoff, float highCutoff, float fs)
    {
        lpLow.init(lowCutoff, fs, false);
        hpLow.init(lowCutoff, fs, true);

        lpHigh.init(highCutoff, fs, false);
        hpHigh.init(highCutoff, fs, true);
    }

    inline void process(float in, float &low, float &mid, float &high)
    {
        low = lpLow.process(in);
        float highPassLow = hpLow.process(in);

        mid = lpHigh.process(highPassLow);
        high = hpHigh.process(highPassLow);
    }

    void reset()
    {
        lpLow.reset();
        hpLow.reset();
        lpHigh.reset();
        hpHigh.reset();
    }

private:
    LinkwitzRiley4 lpLow;
    LinkwitzRiley4 hpLow;
    LinkwitzRiley4 lpHigh;
    LinkwitzRiley4 hpHigh;
};

} // namespace chipgoodizer

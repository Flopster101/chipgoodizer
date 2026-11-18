#pragma once

#include "MathConstants.hpp"

#include <algorithm>
#include <cmath>

namespace chipgoodizer
{

class Biquad
{
public:
    void setCoefficients(float b0_in, float b1_in, float b2_in, float a1_in, float a2_in)
    {
        b0 = b0_in;
        b1 = b1_in;
        b2 = b2_in;
        a1 = a1_in;
        a2 = a2_in;
    }

    void setLowPass(float fc, float fs, float Q = 0.70710678f)
    {
        fc = clampToNyquist(fc, fs);

        float w0 = 2.0f * kPi * fc / fs;
        float cos_w0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;

        setCoefficients(((1.0f - cos_w0) * 0.5f) / a0,
                        (1.0f - cos_w0) / a0,
                        ((1.0f - cos_w0) * 0.5f) / a0,
                        (-2.0f * cos_w0) / a0,
                        (1.0f - alpha) / a0);
    }

    void setHighPass(float fc, float fs, float Q = 0.70710678f)
    {
        fc = clampToNyquist(fc, fs);

        float w0 = 2.0f * kPi * fc / fs;
        float cos_w0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;

        setCoefficients(((1.0f + cos_w0) * 0.5f) / a0,
                        (-(1.0f + cos_w0)) / a0,
                        ((1.0f + cos_w0) * 0.5f) / a0,
                        (-2.0f * cos_w0) / a0,
                        (1.0f - alpha) / a0);
    }

    void setLowShelf(float fc, float fs, float gainDb, float S = 1.0f)
    {
        fc = clampShelf(fc, fs);

        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * kPi * fc / fs;
        float cos_w0 = std::cos(w0);
        float sin_w0 = std::sin(w0);
        float alpha = sin_w0 * 0.5f * std::sqrt((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);

        float b0_raw = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha);
        float b1_raw = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
        float b2_raw = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha);
        float a0_raw = (A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha;
        float a1_raw = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0);
        float a2_raw = (A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha;

        setCoefficients(b0_raw / a0_raw, b1_raw / a0_raw, b2_raw / a0_raw,
                        a1_raw / a0_raw, a2_raw / a0_raw);
    }

    void setHighShelf(float fc, float fs, float gainDb, float S = 1.0f)
    {
        fc = clampShelf(fc, fs);

        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * kPi * fc / fs;
        float cos_w0 = std::cos(w0);
        float sin_w0 = std::sin(w0);
        float alpha = sin_w0 * 0.5f * std::sqrt((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);

        float b0_raw = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha);
        float b1_raw = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
        float b2_raw = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha);
        float a0_raw = (A + 1.0f) - (A - 1.0f) * cos_w0 + 2.0f * std::sqrt(A) * alpha;
        float a1_raw = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0);
        float a2_raw = (A + 1.0f) - (A - 1.0f) * cos_w0 - 2.0f * std::sqrt(A) * alpha;

        setCoefficients(b0_raw / a0_raw, b1_raw / a0_raw, b2_raw / a0_raw,
                        a1_raw / a0_raw, a2_raw / a0_raw);
    }

    void setPeaking(float fc, float fs, float gainDb, float Q = 0.70710678f)
    {
        fc = clampToNyquist(fc, fs);

        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * kPi * fc / fs;
        float cos_w0 = std::cos(w0);
        float sin_w0 = std::sin(w0);
        float alpha = sin_w0 / (2.0f * Q);

        float b0_raw = 1.0f + alpha * A;
        float b1_raw = -2.0f * cos_w0;
        float b2_raw = 1.0f - alpha * A;
        float a0_raw = 1.0f + alpha / A;
        float a1_raw = -2.0f * cos_w0;
        float a2_raw = 1.0f - alpha / A;

        setCoefficients(b0_raw / a0_raw, b1_raw / a0_raw, b2_raw / a0_raw,
                        a1_raw / a0_raw, a2_raw / a0_raw);
    }

    inline float process(float x)
    {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    void reset()
    {
        x1 = x2 = y1 = y2 = 0.0f;
    }

private:
    // Above Nyquist RBJ folds back: 16.5 kHz lands near 1 kHz at 8 kHz.
    static float clampToNyquist(float fc, float fs)
    {
        return std::min(fc, 0.45f * fs);
    }

    // Shelves degrade sooner: past 0.25 * fs they tilt the wrong way.
    static float clampShelf(float fc, float fs)
    {
        return std::min(fc, 0.25f * fs);
    }

    float b0{1.0f}, b1{0.0f}, b2{0.0f}, a1{0.0f}, a2{0.0f};
    float x1{0.0f}, x2{0.0f}, y1{0.0f}, y2{0.0f};
};

} // namespace chipgoodizer

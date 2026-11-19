#pragma once

#include "dsp/Biquad.hpp"
#include "dsp/Crossover.hpp"
#include "dsp/EnvelopeFollower.hpp"
#include "dsp/PhaseRotator.hpp"
#include "dsp/SlewLimiter.hpp"
#include "dsp/StereoAmbience.hpp"
#include "dsp/SoftClip.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace chipgoodizer
{

enum class Preset
{
    Commercial,
    ScopeSafe,
    WarmChiptune,
    PunchyClub
};

struct ChipgoodizerConfig
{
    // Primary macro knobs (0.0 to 1.0)
    float amount{0.95f};
    float rubber{0.90f};
    float punch{1.00f};
    float air{0.85f};
    float deharsh{0.40f};
    float warmth{0.88f};
    float width{0.75f};
    float ambience{0.15f};

    // Gain staging (dB)
    float inputDriveDb{0.50f};
    float ceilingDb{-1.0f};

    // Tunables
    float xoverLowHz{220.0f};
    float xoverHighHz{3800.0f};
    bool phaseRotation{true};

    static ChipgoodizerConfig getPreset(Preset preset)
    {
        ChipgoodizerConfig cfg;
        switch (preset)
        {
        case Preset::Commercial:
            cfg.amount = 0.85f;
            cfg.rubber = 0.85f;
            cfg.punch = 0.85f;
            cfg.air = 0.70f;
            cfg.deharsh = 0.50f;
            cfg.warmth = 1.00f;
            cfg.width = 0.60f;
            cfg.ambience = 0.10f;
            cfg.inputDriveDb = 0.0f;
            cfg.ceilingDb = -1.2f;
            break;

        case Preset::ScopeSafe:
            cfg = getPreset(Preset::Commercial);
            cfg.ceilingDb = -6.2f;
            break;

        case Preset::WarmChiptune:
            cfg.amount = 0.85f;
            cfg.rubber = 0.90f;
            cfg.punch = 0.70f;
            cfg.air = 0.50f;
            cfg.deharsh = 0.60f;
            cfg.warmth = 1.00f;
            cfg.width = 0.50f;
            cfg.ambience = 0.12f;
            cfg.inputDriveDb = 0.0f;
            cfg.ceilingDb = -1.5f;
            break;

        case Preset::PunchyClub:
            cfg.amount = 0.95f;
            cfg.rubber = 0.90f;
            cfg.punch = 1.00f;
            cfg.air = 0.85f;
            cfg.deharsh = 0.40f;
            cfg.warmth = 0.88f;
            cfg.width = 0.75f;
            cfg.ambience = 0.15f;
            cfg.inputDriveDb = 0.5f;
            cfg.ceilingDb = -1.0f;
            break;
        }
        return cfg;
    }
};

class Chipgoodizer
{
public:
    Chipgoodizer() = default;

    void init(float sampleRate, const ChipgoodizerConfig &cfg = {});

    // Safe from any thread; picked up at the next sample.
    void setConfig(const ChipgoodizerConfig &cfg);

    void reset();

    void processStereo(float inL, float inR, float &outL, float &outR);
    void process(const float *inL, const float *inR, float *outL, float *outR, size_t numFrames);
    void processInterleaved(float *interleavedStereo, size_t numFrames, int channels = 2);

private:
    void updateFilters();
    void applyRequestedConfig();

    void applyPreEq(float &left, float &right);
    float processLowBand(float lowLeft, float lowRight);
    void processMidBand(float &left, float &right);
    void processHighBand(float &left, float &right);
    void applyAmbienceAndWidth(float &left, float &right);

    float fs{44100.0f};
    ChipgoodizerConfig config;

    // Seqlock handoff from the writer to the audio thread.
    ChipgoodizerConfig requestedConfig;
    std::atomic<uint32_t> requestSeq{0};
    uint32_t appliedSeq{0};

    // Derived gains, refreshed by updateFilters() so the audio path stays free
    // of per-sample pow() calls.
    float driveLin{1.0f};
    float ceilingLin{0.87f};
    float lowPreGain{1.0f};
    float midPreGain{1.0f};
    float highPreGain{1.0f};
    float lowUpCeiling{1.36f};

    PhaseRotator rotator;

    Biquad eqHighPassL, eqHighPassR;
    Biquad eqBassWarmthL, eqBassWarmthR;
    Biquad eqMudCutL, eqMudCutR;
    Biquad eqLeadBodyL, eqLeadBodyR;
    Biquad eqTameHarshL, eqTameHarshR;
    Biquad eqLeadAirL, eqLeadAirR;
    Biquad eqShelfAirL, eqShelfAirR;

    SlewLimiter slewL;
    SlewLimiter slewR;

    Crossover3Band xoverL;
    Crossover3Band xoverR;

    Biquad lowSubShelf;
    Biquad lowBodyPeaking;
    Biquad lowSmooth;
    EnvelopeFollower envLowFast;
    EnvelopeFollower envLowSlow;

    EnvelopeFollower envLowImbL;
    EnvelopeFollower envLowImbR;

    EnvelopeFollower envMidFastL;
    EnvelopeFollower envMidFastR;
    EnvelopeFollower envMidSlowL;
    EnvelopeFollower envMidSlowR;

    EnvelopeFollower envHighFastL;
    EnvelopeFollower envHighFastR;
    EnvelopeFollower envHighL;
    EnvelopeFollower envHighR;
    Biquad highShelfL;
    Biquad highShelfR;
    Biquad highSmoothL;
    Biquad highSmoothR;

    StereoAmbience ambienceEngine;
};

} // namespace chipgoodizer

#include "chipgoodizer/Chipgoodizer.hpp"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <cstdint>
#endif

namespace chipgoodizer
{

namespace
{

constexpr int kConfigRetries = 4;

// 0.14 per sample at 48 kHz, held as a slope so other rates match.
constexpr float kSlewPerSecond = 0.14f * 48000.0f;

// Filter state decays into denormals on quiet passages, where each following
// multiply costs ~100x more.
void enableDenormalFlushing()
{
#if defined(_M_X64) || defined(_M_IX86)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__x86_64__) || defined(__i386__)
    // FTZ | DAZ.
    _mm_setcsr(_mm_getcsr() | 0x8040u);
#elif defined(__aarch64__) || defined(_M_ARM64)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    // Flush-to-zero.
    fpcr |= (uint64_t{1} << 24);
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
}

} // namespace

void Chipgoodizer::init(float sampleRate, const ChipgoodizerConfig &cfg)
{
    fs = sampleRate;
    enableDenormalFlushing();

    config = cfg;
    requestedConfig = cfg;
    appliedSeq = requestSeq.load(std::memory_order_acquire);

    rotator.init(140.0f, fs);
    const float maxSlew = kSlewPerSecond / sampleRate;
    slewL.init(maxSlew);
    slewR.init(maxSlew);

    xoverL.init(config.xoverLowHz, config.xoverHighHz, fs);
    xoverR.init(config.xoverLowHz, config.xoverHighHz, fs);

    envLowFast.init(fs, 2.0f, 18.0f);
    envLowSlow.init(fs, 15.0f, 80.0f);

    envMidFastL.init(fs, 1.0f, 15.0f);
    envMidFastR.init(fs, 1.0f, 15.0f);
    envMidSlowL.init(fs, 25.0f, 80.0f);
    envMidSlowR.init(fs, 25.0f, 80.0f);

    envHighL.init(fs, 3.0f, 50.0f);
    envHighR.init(fs, 3.0f, 50.0f);

    ambienceEngine.init(fs);

    updateFilters();
    reset();
}

void Chipgoodizer::setConfig(const ChipgoodizerConfig &cfg)
{
    const uint32_t seq = requestSeq.load(std::memory_order_relaxed);
    // Odd sequence number: a write is in progress.
    requestSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    requestedConfig = cfg;

    requestSeq.store(seq + 2, std::memory_order_release);
}

void Chipgoodizer::applyRequestedConfig()
{
    for (int attempt = 0; attempt < kConfigRetries; ++attempt)
    {
        const uint32_t before = requestSeq.load(std::memory_order_acquire);
        if (before == appliedSeq)
            return;
        // Writer is mid-update, try again below.
        if ((before & 1u) != 0u)
            continue;

        const ChipgoodizerConfig snapshot = requestedConfig;
        std::atomic_thread_fence(std::memory_order_acquire);

        if (requestSeq.load(std::memory_order_relaxed) != before)
            continue;

        const bool xoverChanged = (snapshot.xoverLowHz != config.xoverLowHz ||
                                   snapshot.xoverHighHz != config.xoverHighHz);
        config = snapshot;

        if (xoverChanged)
        {
            xoverL.init(config.xoverLowHz, config.xoverHighHz, fs);
            xoverR.init(config.xoverLowHz, config.xoverHighHz, fs);
        }

        updateFilters();
        appliedSeq = before;
        return;
    }
}

void Chipgoodizer::updateFilters()
{
    eqHighPassL.setHighPass(30.0f, fs, 0.7071f);
    eqHighPassR.setHighPass(30.0f, fs, 0.7071f);

    eqBassWarmthL.setLowShelf(95.0f, fs, 2.2f * config.rubber);
    eqBassWarmthR.setLowShelf(95.0f, fs, 2.2f * config.rubber);

    eqMudCutL.setPeaking(240.0f, fs, -1.8f, 1.2f);
    eqMudCutR.setPeaking(240.0f, fs, -1.8f, 1.2f);

    eqLeadBodyL.setPeaking(420.0f, fs, 2.0f * config.warmth, 1.0f);
    eqLeadBodyR.setPeaking(420.0f, fs, 2.0f * config.warmth, 1.0f);

    eqTameHarshL.setPeaking(1750.0f, fs, -4.8f * config.deharsh, 1.4f);
    eqTameHarshR.setPeaking(1750.0f, fs, -4.8f * config.deharsh, 1.4f);

    eqLeadAirL.setPeaking(3200.0f, fs, 3.2f * config.air, 1.2f);
    eqLeadAirR.setPeaking(3200.0f, fs, 3.2f * config.air, 1.2f);

    eqShelfAirL.setHighShelf(8500.0f, fs, 3.0f * config.air);
    eqShelfAirR.setHighShelf(8500.0f, fs, 3.0f * config.air);

    lowSubShelf.setLowShelf(95.0f, fs, 4.0f * config.rubber);
    lowBodyPeaking.setPeaking(190.0f, fs, 2.2f * config.rubber, 1.2f);
    lowSmooth.setLowPass(480.0f, fs, 0.707f);

    lowPreGain = std::pow(10.0f, (2.5f * config.rubber) / 20.0f);
    midPreGain = std::pow(10.0f, 1.5f / 20.0f);
    highPreGain = std::pow(10.0f, (2.0f * config.air) / 20.0f);
    driveLin = std::pow(10.0f, config.inputDriveDb / 20.0f);
    ceilingLin = std::pow(10.0f, config.ceilingDb / 20.0f);
    masterScale = std::pow(10.0f, -2.8f / 20.0f);
    lowUpCeiling = 1.0f + 0.6f * config.rubber;

    highShelfL.setHighShelf(8500.0f, fs, 5.0f * config.air);
    highShelfR.setHighShelf(8500.0f, fs, 5.0f * config.air);
    highSmoothL.setLowPass(16500.0f, fs, 0.707f);
    highSmoothR.setLowPass(16500.0f, fs, 0.707f);
}

void Chipgoodizer::reset()
{
    rotator.reset();
    slewL.reset();
    slewR.reset();

    eqHighPassL.reset();
    eqHighPassR.reset();
    eqBassWarmthL.reset();
    eqBassWarmthR.reset();
    eqMudCutL.reset();
    eqMudCutR.reset();
    eqLeadBodyL.reset();
    eqLeadBodyR.reset();
    eqTameHarshL.reset();
    eqTameHarshR.reset();
    eqLeadAirL.reset();
    eqLeadAirR.reset();
    eqShelfAirL.reset();
    eqShelfAirR.reset();

    xoverL.reset();
    xoverR.reset();

    lowSubShelf.reset();
    lowBodyPeaking.reset();
    lowSmooth.reset();
    envLowFast.reset();
    envLowSlow.reset();

    envMidFastL.reset();
    envMidFastR.reset();
    envMidSlowL.reset();
    envMidSlowR.reset();

    envHighL.reset();
    envHighR.reset();
    highShelfL.reset();
    highShelfR.reset();
    highSmoothL.reset();
    highSmoothR.reset();

    ambienceEngine.reset();
}

void Chipgoodizer::processStereo(float inL, float inR, float &outL, float &outR)
{
    if (requestSeq.load(std::memory_order_relaxed) != appliedSeq)
        applyRequestedConfig();

    float rotL = inL;
    float rotR = inR;
    if (config.phaseRotation)
        rotator.process(rotL, rotR);

    float drivenL = rotL * driveLin;
    float drivenR = rotR * driveLin;

    drivenL = eqHighPassL.process(drivenL);
    drivenR = eqHighPassR.process(drivenR);
    drivenL = eqBassWarmthL.process(drivenL);
    drivenR = eqBassWarmthR.process(drivenR);
    drivenL = eqMudCutL.process(drivenL);
    drivenR = eqMudCutR.process(drivenR);
    drivenL = eqLeadBodyL.process(drivenL);
    drivenR = eqLeadBodyR.process(drivenR);
    drivenL = eqTameHarshL.process(drivenL);
    drivenR = eqTameHarshR.process(drivenR);
    drivenL = eqLeadAirL.process(drivenL);
    drivenR = eqLeadAirR.process(drivenR);
    drivenL = eqShelfAirL.process(drivenL);
    drivenR = eqShelfAirR.process(drivenR);

    float lowL, midL, highL;
    float lowR, midR, highR;
    xoverL.process(drivenL, lowL, midL, highL);
    xoverR.process(drivenR, lowR, midR, highR);

    // Low band (rubbery bass transformation)
    float lowMono = 0.5f * (lowL + lowR);
    lowMono *= lowPreGain;

    lowMono = lowSubShelf.process(lowMono);
    lowMono = lowBodyPeaking.process(lowMono);
    float lowSine = lowSmooth.process(lowMono);

    float lowFastEnv = envLowFast.process(lowSine);
    float lowSlowEnv = envLowSlow.process(lowSine);

    float threshLowUp = 0.12f;
    float lowUpGain = 1.0f;
    if (lowSlowEnv > 1e-4f && lowSlowEnv < threshLowUp)
    {
        float ratio = threshLowUp / lowSlowEnv;
        lowUpGain = std::min(lowUpCeiling, std::pow(ratio, 0.30f));
    }

    float threshLowDown = 0.40f;
    float lowDownGain = 1.0f;
    if (lowFastEnv > threshLowDown)
        lowDownGain = std::pow(threshLowDown / lowFastEnv, 0.60f);

    float procLow = lowSine * lowUpGain * lowDownGain * 1.06f;
    procLow = std::tanh(1.10f * procLow) / 1.10f;

    // Mid band (punch & dynamic soft body)
    midL *= midPreGain;
    midR *= midPreGain;

    float fastL = envMidFastL.process(midL);
    float fastR = envMidFastR.process(midR);
    float slowL = envMidSlowL.process(midL);
    float slowR = envMidSlowR.process(midR);

    float transL = std::max(0.0f, fastL - slowL);
    float transR = std::max(0.0f, fastR - slowR);

    float punchL = 1.0f + 0.60f * config.punch * (transL / (fastL + 1e-4f));
    float punchR = 1.0f + 0.60f * config.punch * (transR / (fastR + 1e-4f));

    float midThresh = 0.22f;
    float compExponent = 0.35f * config.deharsh;
    float bodyL = (slowL > midThresh) ? std::pow(midThresh / slowL, compExponent) : 1.0f;
    float bodyR = (slowR > midThresh) ? std::pow(midThresh / slowR, compExponent) : 1.0f;

    float procMidL = midL * punchL * bodyL;
    float procMidR = midR * punchR * bodyR;

    procMidL = std::tanh(1.15f * procMidL) / 1.15f;
    procMidR = std::tanh(1.15f * procMidR) / 1.15f;

    // High band (upward air expansion + downward de-harshing compressor)
    highL *= highPreGain;
    highR *= highPreGain;

    float highEnvL = envHighL.process(highL);
    float highEnvR = envHighR.process(highR);

    float threshHighUp = 0.05f;
    float highUpGainL = (highEnvL < threshHighUp && highEnvL > 1e-4f) ? std::min(1.4f, std::pow(threshHighUp / highEnvL, 0.25f * config.air)) : 1.0f;
    float highUpGainR = (highEnvR < threshHighUp && highEnvR > 1e-4f) ? std::min(1.4f, std::pow(threshHighUp / highEnvR, 0.25f * config.air)) : 1.0f;

    float threshHighDown = 0.16f;
    float highDownGainL = (highEnvL > threshHighDown) ? std::pow(threshHighDown / highEnvL, 0.50f * config.deharsh) : 1.0f;
    float highDownGainR = (highEnvR > threshHighDown) ? std::pow(threshHighDown / highEnvR, 0.50f * config.deharsh) : 1.0f;

    float highPostGain = 1.06f;
    float procHighL = highSmoothL.process(highShelfL.process(highL * highUpGainL * highDownGainL)) * highPostGain;
    float procHighR = highSmoothR.process(highShelfR.process(highR * highUpGainR * highDownGainR)) * highPostGain;

    procHighL = std::tanh(1.2f * procHighL) / 1.2f;
    procHighR = std::tanh(1.2f * procHighR) / 1.2f;

    // Ambience & stereo separation
    float dryMidHighL = procMidL + procHighL;
    float dryMidHighR = procMidR + procHighR;

    float ambL = 0.0f, ambR = 0.0f;
    ambienceEngine.process(dryMidHighL, dryMidHighR, ambL, ambR);

    float spatialMidHighL = dryMidHighL + config.ambience * ambL;
    float spatialMidHighR = dryMidHighR + config.ambience * ambR;

    float midChan = 0.5f * (spatialMidHighL + spatialMidHighR);
    float sideChan = 0.5f * (spatialMidHighL - spatialMidHighR);
    float widthFactor = 1.0f + 0.35f * config.width;
    sideChan *= widthFactor;

    float outMidHighL = midChan + sideChan;
    float outMidHighR = midChan - sideChan;

    // Master scaling & limiting
    float wetL = (procLow + outMidHighL) * masterScale;
    float wetR = (procLow + outMidHighR) * masterScale;

    wetL = slewL.process(wetL);
    wetR = slewR.process(wetR);

    float mixL = (1.0f - config.amount) * (drivenL * masterScale) + config.amount * wetL;
    float mixR = (1.0f - config.amount) * (drivenR * masterScale) + config.amount * wetR;

    float masterL = softClip(mixL, 0.78f);
    float masterR = softClip(mixR, 0.78f);

    outL = masterL * ceilingLin;
    outR = masterR * ceilingLin;
}

void Chipgoodizer::process(const float *inL, const float *inR, float *outL, float *outR, size_t numFrames)
{
    enableDenormalFlushing();

    for (size_t i = 0; i < numFrames; ++i)
    {
        processStereo(inL[i], inR[i], outL[i], outR[i]);
    }
}

void Chipgoodizer::processInterleaved(float *interleavedStereo, size_t numFrames, int channels)
{
    enableDenormalFlushing();

    if (channels < 2)
    {
        // Mono: same signal both sides, average back.
        for (size_t i = 0; i < numFrames; ++i)
        {
            const float in = interleavedStereo[i];
            float outL = 0.0f;
            float outR = 0.0f;
            processStereo(in, in, outL, outR);
            interleavedStereo[i] = 0.5f * (outL + outR);
        }
        return;
    }

    const size_t stride = static_cast<size_t>(channels);
    for (size_t i = 0; i < numFrames; ++i)
    {
        float *frame = interleavedStereo + i * stride;
        float outL = 0.0f;
        float outR = 0.0f;
        processStereo(frame[0], frame[1], outL, outR);
        frame[0] = outL;
        frame[1] = outR;
    }
}

} // namespace chipgoodizer

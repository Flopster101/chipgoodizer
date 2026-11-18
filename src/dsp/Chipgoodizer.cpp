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

    float drivenL = inL;
    float drivenR = inR;
    if (config.phaseRotation)
        rotator.process(drivenL, drivenR);

    drivenL *= driveLin;
    drivenR *= driveLin;
    applyPreEq(drivenL, drivenR);

    float lowL, midL, highL;
    float lowR, midR, highR;
    xoverL.process(drivenL, lowL, midL, highL);
    xoverR.process(drivenR, lowR, midR, highR);

    const float procLow = processLowBand(lowL, lowR);
    processMidBand(midL, midR);
    processHighBand(highL, highR);

    float midHighL = midL + highL;
    float midHighR = midR + highR;
    applyAmbienceAndWidth(midHighL, midHighR);

    float wetL = (procLow + midHighL) * masterScale;
    float wetR = (procLow + midHighR) * masterScale;

    wetL = slewL.process(wetL);
    wetR = slewR.process(wetR);

    // Dry is post-EQ, so this blends rather than bypasses.
    const float mixL = (1.0f - config.amount) * (drivenL * masterScale) + config.amount * wetL;
    const float mixR = (1.0f - config.amount) * (drivenR * masterScale) + config.amount * wetR;

    outL = softClip(mixL, 0.78f) * ceilingLin;
    outR = softClip(mixR, 0.78f) * ceilingLin;
}

void Chipgoodizer::applyPreEq(float &left, float &right)
{
    left = eqHighPassL.process(left);
    right = eqHighPassR.process(right);
    left = eqBassWarmthL.process(left);
    right = eqBassWarmthR.process(right);
    left = eqMudCutL.process(left);
    right = eqMudCutR.process(right);
    left = eqLeadBodyL.process(left);
    right = eqLeadBodyR.process(right);
    left = eqTameHarshL.process(left);
    right = eqTameHarshR.process(right);
    left = eqLeadAirL.process(left);
    right = eqLeadAirR.process(right);
    left = eqShelfAirL.process(left);
    right = eqShelfAirR.process(right);
}

float Chipgoodizer::processLowBand(float lowLeft, float lowRight)
{
    float lowMono = 0.5f * (lowLeft + lowRight);
    lowMono *= lowPreGain;

    lowMono = lowSubShelf.process(lowMono);
    lowMono = lowBodyPeaking.process(lowMono);
    const float lowSine = lowSmooth.process(lowMono);

    const float lowFastEnv = envLowFast.process(lowSine);
    const float lowSlowEnv = envLowSlow.process(lowSine);

    constexpr float kThreshLowUp = 0.12f;
    float lowUpGain = 1.0f;
    if (lowSlowEnv > 1e-4f && lowSlowEnv < kThreshLowUp)
        lowUpGain = std::min(lowUpCeiling, std::pow(kThreshLowUp / lowSlowEnv, 0.30f));

    constexpr float kThreshLowDown = 0.40f;
    float lowDownGain = 1.0f;
    if (lowFastEnv > kThreshLowDown)
        lowDownGain = std::pow(kThreshLowDown / lowFastEnv, 0.60f);

    float procLow = lowSine * lowUpGain * lowDownGain * 1.06f;
    return std::tanh(1.10f * procLow) / 1.10f;
}

void Chipgoodizer::processMidBand(float &left, float &right)
{
    left *= midPreGain;
    right *= midPreGain;

    const float fastL = envMidFastL.process(left);
    const float fastR = envMidFastR.process(right);
    const float slowL = envMidSlowL.process(left);
    const float slowR = envMidSlowR.process(right);

    const float transL = std::max(0.0f, fastL - slowL);
    const float transR = std::max(0.0f, fastR - slowR);

    const float punchL = 1.0f + 0.60f * config.punch * (transL / (fastL + 1e-4f));
    const float punchR = 1.0f + 0.60f * config.punch * (transR / (fastR + 1e-4f));

    constexpr float kMidThresh = 0.22f;
    const float compExponent = 0.35f * config.deharsh;
    const float bodyL = (slowL > kMidThresh) ? std::pow(kMidThresh / slowL, compExponent) : 1.0f;
    const float bodyR = (slowR > kMidThresh) ? std::pow(kMidThresh / slowR, compExponent) : 1.0f;

    float procL = left * punchL * bodyL;
    float procR = right * punchR * bodyR;

    left = std::tanh(1.15f * procL) / 1.15f;
    right = std::tanh(1.15f * procR) / 1.15f;
}

void Chipgoodizer::processHighBand(float &left, float &right)
{
    left *= highPreGain;
    right *= highPreGain;

    const float highEnvL = envHighL.process(left);
    const float highEnvR = envHighR.process(right);

    constexpr float kThreshHighUp = 0.05f;
    const float highUpGainL = (highEnvL < kThreshHighUp && highEnvL > 1e-4f)
                                  ? std::min(1.4f, std::pow(kThreshHighUp / highEnvL, 0.25f * config.air))
                                  : 1.0f;
    const float highUpGainR = (highEnvR < kThreshHighUp && highEnvR > 1e-4f)
                                  ? std::min(1.4f, std::pow(kThreshHighUp / highEnvR, 0.25f * config.air))
                                  : 1.0f;

    constexpr float kThreshHighDown = 0.16f;
    const float highDownGainL = (highEnvL > kThreshHighDown)
                                    ? std::pow(kThreshHighDown / highEnvL, 0.50f * config.deharsh)
                                    : 1.0f;
    const float highDownGainR = (highEnvR > kThreshHighDown)
                                    ? std::pow(kThreshHighDown / highEnvR, 0.50f * config.deharsh)
                                    : 1.0f;

    constexpr float kHighPostGain = 1.06f;
    float procHighL = highSmoothL.process(highShelfL.process(left * highUpGainL * highDownGainL)) * kHighPostGain;
    float procHighR = highSmoothR.process(highShelfR.process(right * highUpGainR * highDownGainR)) * kHighPostGain;

    left = std::tanh(1.2f * procHighL) / 1.2f;
    right = std::tanh(1.2f * procHighR) / 1.2f;
}

void Chipgoodizer::applyAmbienceAndWidth(float &left, float &right)
{
    float ambL = 0.0f;
    float ambR = 0.0f;
    ambienceEngine.process(left, right, ambL, ambR);

    const float spatialL = left + config.ambience * ambL;
    const float spatialR = right + config.ambience * ambR;

    const float midChan = 0.5f * (spatialL + spatialR);
    float sideChan = 0.5f * (spatialL - spatialR);
    sideChan *= 1.0f + 0.35f * config.width;

    left = midChan + sideChan;
    right = midChan - sideChan;
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

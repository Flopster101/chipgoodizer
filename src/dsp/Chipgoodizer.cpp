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

// Gentle ultrasonic step guard (~0.60 per sample at 48 kHz) so drum attack
// slopes pass intact while extreme 1-sample digital glitches stay bounded.
constexpr float kSlewPerSecond = 0.60f * 48000.0f;

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

    // Low band: smooth kick onset taper (1.5 ms / 38 ms) and sustain follower
    // (15 ms / 40 ms) so bass note tails stay full without post-attack ducking.
    envLowFast.init(fs, 1.5f, 38.0f);
    envLowSlow.init(fs, 15.0f, 40.0f);

    // Mid band: smooth transient taper (0.8 ms / 28 ms) and quick-recovering
    // body detector (16 ms / 35 ms) so note releases stay full after attacks.
    envMidFastL.init(fs, 0.8f, 28.0f);
    envMidFastR.init(fs, 0.8f, 28.0f);
    envMidSlowL.init(fs, 16.0f, 35.0f);
    envMidSlowR.init(fs, 16.0f, 35.0f);

    // High band: smooth transient taper (0.8 ms / 25 ms) and quick-recovering
    // harshness detector (14 ms / 32 ms) to preserve note and cymbal tails.
    envHighFastL.init(fs, 0.8f, 25.0f);
    envHighFastR.init(fs, 0.8f, 25.0f);
    envHighL.init(fs, 3.5f, 42.0f);
    envHighR.init(fs, 3.5f, 42.0f);

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
    // Tighten subsonic rolloff to 42 Hz so 20-45 Hz sub-bass stays controlled.
    eqHighPassL.setHighPass(42.0f, fs, 0.7071f);
    eqHighPassR.setHighPass(42.0f, fs, 0.7071f);

    // Focused bass punch bell at 82 Hz instead of a DC-extending low shelf.
    eqBassWarmthL.setPeaking(80.0f, fs, 1.1f * config.rubber, 0.85f);
    eqBassWarmthR.setPeaking(80.0f, fs, 1.1f * config.rubber, 0.85f);

    // Gentle mud dip at 205 Hz matching commercial low-mid cleanup.
    eqMudCutL.setPeaking(240.0f, fs, -1.6f, 1.0f);
    eqMudCutR.setPeaking(240.0f, fs, -1.6f, 1.0f);

    // Subtle warmth fill around 420 Hz for hollow tracker pulse leads.
    eqLeadBodyL.setPeaking(420.0f, fs, 0.25f * config.warmth, 1.0f);
    eqLeadBodyR.setPeaking(420.0f, fs, 0.25f * config.warmth, 1.0f);

    // Gentle anti-fatigue dip at 2.2 kHz to keep square-wave upper mids smooth.
    eqTameHarshL.setPeaking(2200.0f, fs, -0.8f * config.deharsh, 1.1f);
    eqTameHarshR.setPeaking(2200.0f, fs, -0.8f * config.deharsh, 1.1f);

    // Broad presence lift centered at 4.8 kHz; trimmed slightly so 4-5 kHz
    // does not poke ahead of upper-air sparkle.
    eqLeadAirL.setPeaking(4800.0f, fs, 2.8f * config.air, 0.70f);
    eqLeadAirR.setPeaking(4800.0f, fs, 2.8f * config.air, 0.70f);

    // Positive air shelf driven by air rather than statically cut by deharsh.
    eqShelfAirL.setHighShelf(9000.0f, fs, 1.8f * config.air);
    eqShelfAirR.setHighShelf(9000.0f, fs, 1.8f * config.air);

    // Low band internal shaping: punchy 78 Hz weight bell + 135 Hz body
    // instead of a second low shelf that over-boosted 20-50 Hz sub-bass.
    lowSubShelf.setPeaking(78.0f, fs, 1.6f * config.rubber, 0.80f);
    lowBodyPeaking.setPeaking(135.0f, fs, 0.7f * config.rubber, 1.1f);
    lowSmooth.setLowPass(480.0f, fs, 0.707f);

    lowPreGain = std::pow(10.0f, (0.5f * config.rubber) / 20.0f);
    midPreGain = std::pow(10.0f, 0.8f / 20.0f);
    highPreGain = std::pow(10.0f, (3.6f * config.air) / 20.0f);
    driveLin = std::pow(10.0f, config.inputDriveDb / 20.0f);
    ceilingLin = std::pow(10.0f, config.ceilingDb / 20.0f);
    // Calibrated master trim: matches commercial loudness without losing volume
    // relative to the dry tracker render, while keeping clean headroom.
    masterScale = std::pow(10.0f, 1.4f / 20.0f);
    lowUpCeiling = 1.0f + 0.62f * config.rubber;

    // Keep top-end air sparkle on the air knob and let deharsh tilt only gently
    // while dynamic de-essing catches bright bursts.
    const float highShelfDb = 1.2f * config.air - 1.0f * config.deharsh;
    highShelfL.setHighShelf(8500.0f, fs, highShelfDb);
    highShelfR.setHighShelf(8500.0f, fs, highShelfDb);
    const float smoothCornerHz = 18500.0f - 4000.0f * config.deharsh;
    highSmoothL.setLowPass(smoothCornerHz, fs, 0.707f);
    highSmoothR.setLowPass(smoothCornerHz, fs, 0.707f);
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

    envHighFastL.reset();
    envHighFastR.reset();
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

    outL = softClip(mixL, 0.86f) * ceilingLin;
    outR = softClip(mixR, 0.86f) * ceilingLin;
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

    // Upward density across the normal bass decay range keeps rubbery tails full.
    constexpr float kThreshLowUp = 0.26f;
    float lowUpGain = 1.0f;
    if (lowSlowEnv > 1e-4f && lowSlowEnv < kThreshLowUp)
        lowUpGain = std::min(lowUpCeiling, std::pow(kThreshLowUp / lowSlowEnv, 0.28f));

    // High safety threshold (0.72) avoids post-attack downward ducking on normal
    // kick and bass note tails; odd-symmetric tanh handles musical peak rounding.
    constexpr float kThreshLowDown = 0.72f;
    float lowDownGain = 1.0f;
    if (lowSlowEnv > kThreshLowDown)
        lowDownGain = std::pow(kThreshLowDown / lowSlowEnv, 0.25f);

    // Subtle, deadzoned kick transient lift: triggers only on sharp hits so
    // melodic bass note tails never feel ducked after the attack.
    const float lowRatio = std::max(0.0f, lowFastEnv - lowSlowEnv) / (lowFastEnv + 1e-3f);
    const float lowPunch = 1.0f + 0.25f * config.punch * std::max(0.0f, lowRatio - 0.34f);

    // Warm odd-symmetric rubbery saturation on the body, with kick punch applied
    // after saturation so the transient crest stays sharp.
    float body = lowSine * lowUpGain * lowDownGain;
    body = std::tanh(1.18f * body) / 1.18f;
    return body * lowPunch;
}

void Chipgoodizer::processMidBand(float &left, float &right)
{
    left *= midPreGain;
    right *= midPreGain;

    const float fastL = envMidFastL.process(left);
    const float fastR = envMidFastR.process(right);
    const float slowL = envMidSlowL.process(left);
    const float slowR = envMidSlowR.process(right);

    // Subtle, deadzoned transient lift: fires only on sharp snare/percussion
    // onsets so melodic notes do not spike and drop into a ducked tail.
    const float ratioL = std::max(0.0f, fastL - slowL) / (fastL + 1e-3f);
    const float ratioR = std::max(0.0f, fastR - slowR) / (fastR + 1e-3f);
    const float punchL = 1.0f + 0.42f * config.punch * std::max(0.0f, ratioL - 0.36f);
    const float punchR = 1.0f + 0.42f * config.punch * std::max(0.0f, ratioR - 0.36f);

    // Upward sustain support across the musical decay range keeps note tails
    // full and commercial-sounding without pumping.
    constexpr float kThreshMidUp = 0.24f;
    const float upL = (slowL > 1e-4f && slowL < kThreshMidUp)
                          ? std::min(1.36f, std::pow(kThreshMidUp / slowL, 0.24f))
                          : 1.0f;
    const float upR = (slowR > 1e-4f && slowR < kThreshMidUp)
                          ? std::min(1.36f, std::pow(kThreshMidUp / slowR, 0.24f))
                          : 1.0f;

    // High threshold and fast 35 ms release tame only loud sustained square
    // leads without holding down normal note decays.
    constexpr float kMidThresh = 0.38f;
    const float compExponent = 0.18f * config.deharsh;
    const float bodyL = (slowL > kMidThresh) ? std::pow(kMidThresh / slowL, compExponent) : 1.0f;
    const float bodyR = (slowR > kMidThresh) ? std::pow(kMidThresh / slowR, compExponent) : 1.0f;

    // Gentle saturation on the sustained body, then transient punch on top
    // so snare/drum snap is never shaved off by mid-band saturation.
    const float satL = std::tanh(1.06f * (left * upL * bodyL)) / 1.06f;
    const float satR = std::tanh(1.06f * (right * upR * bodyR)) / 1.06f;

    left = satL * punchL;
    right = satR * punchR;
}

void Chipgoodizer::processHighBand(float &left, float &right)
{
    left *= highPreGain;
    right *= highPreGain;

    const float fastL = envHighFastL.process(left);
    const float fastR = envHighFastR.process(right);
    const float slowL = envHighL.process(left);
    const float slowR = envHighR.process(right);

    // Adaptive target-level high band:
    // - Dark sample-based tracker tracks (< 0.055) get upward air sheen.
    // - Bright synth/AHX tracks with heavy white-noise drums (> 0.075) get
    //   smooth downward de-essing instead of stacking boost on top of noise.
    // No high-band transient boost here: spiking >3.8 kHz makes white-noise
    // snares/hi-hats stab the ear; drum punch comes from Low and Mid bands.
    constexpr float kThreshHighUp = 0.068f;
    const float highUpGainL = (slowL < kThreshHighUp && slowL > 1e-4f)
                                  ? std::min(1.65f, std::pow(kThreshHighUp / slowL, 0.34f * config.air))
                                  : 1.0f;
    const float highUpGainR = (slowR < kThreshHighUp && slowR > 1e-4f)
                                  ? std::min(1.65f, std::pow(kThreshHighUp / slowR, 0.34f * config.air))
                                  : 1.0f;

    // Combine fast and slow envelopes for downward de-essing so sudden
    // white-noise drum bursts are caught immediately without pumping.
    const float detL = std::max(slowL, 0.75f * fastL);
    const float detR = std::max(slowR, 0.75f * fastR);
    constexpr float kThreshHighDown = 0.115f;
    const float highDownGainL = (detL > kThreshHighDown)
                                    ? std::pow(kThreshHighDown / detL, 0.56f * config.deharsh)
                                    : 1.0f;
    const float highDownGainR = (detR > kThreshHighDown)
                                    ? std::pow(kThreshHighDown / detR, 0.56f * config.deharsh)
                                    : 1.0f;

    // No un-oversampled tanh here: nonlinear waveshaping above 3.8 kHz folds
    // aliasing harmonics back into the 2-10 kHz presence band as metallic grit.
    left = highSmoothL.process(highShelfL.process(left * highUpGainL * highDownGainL));
    right = highSmoothR.process(highShelfR.process(right * highUpGainR * highDownGainR));
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
    sideChan *= 1.0f + 0.72f * config.width;

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

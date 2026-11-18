// Plain asserts and a plain main, so this runs under ctest anywhere.

#include "chipgoodizer/Chipgoodizer.hpp"
#include "chipgoodizer/Params.hpp"
#include "chipgoodizer/dsp/MathConstants.hpp"

#ifdef CHIPGOODIZER_HAVE_CAPI
#include "chipgoodizer/chipgoodizer_c.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace
{

using chipgoodizer::Chipgoodizer;
using chipgoodizer::ChipgoodizerConfig;
using chipgoodizer::ParamId;

int gFailures = 0;

void check(bool condition, const char *what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++gFailures;
    }
}

constexpr float kSampleRate = 48000.0f;

// Deterministic input, so the hash cannot drift with the standard library.
std::vector<float> testSignal(size_t frames)
{
    std::vector<float> out(frames);
    uint32_t state = 0x12345678u;
    for (size_t i = 0; i < frames; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const float noise = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
        const double phase = 2.0 * chipgoodizer::kPi * 440.0 * static_cast<double>(i) / kSampleRate;
        out[i] = 0.4f * noise + 0.3f * static_cast<float>(std::sin(phase));
    }
    return out;
}

void render(const std::vector<float> &inL, const std::vector<float> &inR,
            std::vector<float> &outL, std::vector<float> &outR)
{
    Chipgoodizer dsp;
    dsp.init(kSampleRate);
    outL.assign(inL.size(), 0.0f);
    outR.assign(inR.size(), 0.0f);
    dsp.process(inL.data(), inR.data(), outL.data(), outR.data(), inL.size());
}

void testFiniteOutputAndCeiling()
{
    const size_t frames = static_cast<size_t>(kSampleRate);
    std::vector<float> inL(frames);
    std::vector<float> inR(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        const double phase = 2.0 * chipgoodizer::kPi * 440.0 * static_cast<double>(i) / kSampleRate;
        inL[i] = 0.95f * static_cast<float>(std::sin(phase));
        inR[i] = inL[i];
    }

    std::vector<float> outL;
    std::vector<float> outR;
    render(inL, inR, outL, outR);

    float peak = 0.0f;
    bool finite = true;
    for (size_t i = 0; i < frames; ++i)
    {
        finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
        peak = std::max(peak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
    }

    check(finite, "hot tone stays finite");
    check(peak > 0.2f, "hot tone is not silent");
    check(peak < 1.0f, "soft ceiling keeps the peak below full scale");
}

// Silence in, silence out, including the tail after a loud passage.
void testSilenceAndTail()
{
    const size_t toneFrames = static_cast<size_t>(kSampleRate) / 2;
    const size_t tailFrames = static_cast<size_t>(kSampleRate);
    std::vector<float> inL(toneFrames + tailFrames, 0.0f);
    std::vector<float> inR(toneFrames + tailFrames, 0.0f);
    for (size_t i = 0; i < toneFrames; ++i)
    {
        const double phase = 2.0 * chipgoodizer::kPi * 220.0 * static_cast<double>(i) / kSampleRate;
        inL[i] = inR[i] = 0.8f * static_cast<float>(std::sin(phase));
    }

    std::vector<float> outL;
    std::vector<float> outR;
    render(inL, inR, outL, outR);

    bool finite = true;
    float tailPeak = 0.0f;
    for (size_t i = toneFrames; i < inL.size(); ++i)
    {
        finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
        tailPeak = std::max(tailPeak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
    }

    // The release spills into the start of the tail; measure its end.
    float latePeak = 0.0f;
    const size_t lateStart = toneFrames + tailFrames - static_cast<size_t>(kSampleRate) / 10;
    for (size_t i = lateStart; i < inL.size(); ++i)
        latePeak = std::max(latePeak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));

    check(finite, "silence tail stays finite");
    check(latePeak < 1.0e-5f, "silence tail decays to zero");

    std::vector<float> zeros(static_cast<size_t>(kSampleRate), 0.0f);
    render(zeros, zeros, outL, outR);

    bool silent = true;
    for (size_t i = 0; i < zeros.size(); ++i)
        silent = silent && outL[i] == 0.0f && outR[i] == 0.0f;

    check(silent, "silence in gives silence out");
}

// Includes 8 kHz, where two fixed corners sit above Nyquist.
void testSampleRates()
{
    for (float rate : {8000.0f, 22050.0f, 44100.0f, 96000.0f, 192000.0f})
    {
        Chipgoodizer dsp;
        dsp.init(rate);

        const size_t frames = static_cast<size_t>(rate) / 4;
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < frames; ++i)
        {
            const double phase = 2.0 * chipgoodizer::kPi * 440.0 * static_cast<double>(i) / rate;
            float outL = 0.0f;
            float outR = 0.0f;
            dsp.processStereo(0.9f * static_cast<float>(std::sin(phase)), 0.0f, outL, outR);
            finite = finite && std::isfinite(outL) && std::isfinite(outR);
            peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
        }

        char message[64];
        std::snprintf(message, sizeof(message), "rate %.0f Hz is finite and bounded", static_cast<double>(rate));
        check(finite && peak < 1.0f, message);
    }
}

void testParameterClamping()
{
    ChipgoodizerConfig cfg;

    check(chipgoodizer::setParam(cfg, ParamId::Amount, 5.0f), "out-of-range value is accepted and clamped");
    check(cfg.amount == 1.0f, "amount clamps to its maximum");

    check(chipgoodizer::setParam(cfg, ParamId::Air, -3.0f), "negative value is accepted and clamped");
    check(cfg.air == 0.0f, "air clamps to its minimum");

    const float before = cfg.punch;
    check(!chipgoodizer::setParam(cfg, ParamId::Punch, std::numeric_limits<float>::quiet_NaN()),
          "NaN is rejected");
    check(cfg.punch == before, "rejected value leaves the parameter untouched");

    check(!chipgoodizer::setParam(cfg, ParamId::Punch, std::numeric_limits<float>::infinity()),
          "infinity is rejected");
    check(!chipgoodizer::setParam(cfg, ParamId::Count, 0.5f), "out-of-range id is rejected");
    check(chipgoodizer::findParamSpec("notAParameter") == nullptr, "unknown parameter id is not found");
    check(chipgoodizer::paramSpec(ParamId::Count) == nullptr, "paramSpec rejects the sentinel id");
}

// Update the expected hash only for an intended, auditioned change.
void testFixedInputHash()
{
    const size_t frames = static_cast<size_t>(kSampleRate) / 2;
    const std::vector<float> inL = testSignal(frames);
    std::vector<float> inR(frames);
    for (size_t i = 0; i < frames; ++i)
        inR[i] = 0.8f * inL[i];

    std::vector<float> outL;
    std::vector<float> outR;
    render(inL, inR, outL, outR);

    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < frames; ++i)
    {
        uint32_t bitsL = 0;
        uint32_t bitsR = 0;
        std::memcpy(&bitsL, &outL[i], sizeof(bitsL));
        std::memcpy(&bitsR, &outR[i], sizeof(bitsR));
        hash = (hash ^ bitsL) * 1099511628211ull;
        hash = (hash ^ bitsR) * 1099511628211ull;
    }

    constexpr uint64_t kExpectedHash = 0xd26e651a726bda1bull;
    std::printf("fixed-input hash: 0x%016llx (expected 0x%016llx)\n",
                static_cast<unsigned long long>(hash), static_cast<unsigned long long>(kExpectedHash));
    check(hash == kExpectedHash, "fixed input hashes to the expected value");
}

#ifdef CHIPGOODIZER_HAVE_CAPI
// The C ABI must match the engine exactly.
void testCApi()
{
    const size_t frames = static_cast<size_t>(kSampleRate) / 4;
    const std::vector<float> inL = testSignal(frames);
    std::vector<float> inR(frames);
    for (size_t i = 0; i < frames; ++i)
        inR[i] = 0.8f * inL[i];

    std::vector<float> outL;
    std::vector<float> outR;
    render(inL, inR, outL, outR);

    std::vector<float> interleaved(frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        interleaved[2 * i] = inL[i];
        interleaved[2 * i + 1] = inR[i];
    }

    cg_effect *effect = cg_create();
    check(effect != nullptr, "cg_create returns an instance");
    cg_prepare(effect, kSampleRate, 2);
    cg_process_interleaved(effect, interleaved.data(), static_cast<int>(frames), 2);

    bool identical = true;
    for (size_t i = 0; i < frames; ++i)
        identical = identical && interleaved[2 * i] == outL[i] && interleaved[2 * i + 1] == outR[i];
    check(identical, "C ABI output matches the C++ engine bit for bit");

    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i)
        mono[i] = inL[i];
    cg_prepare(effect, kSampleRate, 1);
    cg_process_interleaved(effect, mono.data(), static_cast<int>(frames), 1);
    cg_destroy(effect);

    bool monoFinite = true;
    float monoPeak = 0.0f;
    for (size_t i = 0; i < frames; ++i)
    {
        monoFinite = monoFinite && std::isfinite(mono[i]);
        monoPeak = std::max(monoPeak, std::fabs(mono[i]));
    }
    check(monoFinite && monoPeak > 0.05f && monoPeak < 1.0f, "mono path renders a bounded signal");

    cg_effect *nullEffect = nullptr;
    cg_prepare(nullptr, kSampleRate, 2);
    cg_process_interleaved(nullptr, nullptr, 0, 0);
    cg_set_param(nullptr, 0, 0.5f);
    cg_destroy(nullEffect);
    check(cg_param_count() == chipgoodizer::kParamCount, "C ABI exposes the full parameter table");
    check(cg_param_index("air") == static_cast<int>(ParamId::Air), "C ABI resolves parameter ids");
    check(cg_param_index("notAParameter") == -1, "C ABI rejects unknown parameter ids");
}

// A parameter written between blocks must not corrupt the output.
void testConcurrentParameterChanges()
{
    cg_effect *effect = cg_create();
    cg_prepare(effect, kSampleRate, 2);

    std::vector<float> block(256 * 2);
    bool finite = true;
    for (int pass = 0; pass < 40; ++pass)
    {
        const float value = static_cast<float>(pass % 11) / 10.0f;
        cg_set_param_by_id(effect, "air", value);
        cg_set_param_by_id(effect, "rubber", 1.0f - value);
        cg_set_param_by_id(effect, "xoverLow", 150.0f + 200.0f * value);

        for (int i = 0; i < 256; ++i)
        {
            const double phase = 2.0 * chipgoodizer::kPi * 330.0 * (pass * 256 + i) / kSampleRate;
            block[2 * i] = 0.5f * static_cast<float>(std::sin(phase));
            block[2 * i + 1] = block[2 * i];
        }

        cg_process_interleaved(effect, block.data(), 256, 2);
        for (float sample : block)
            finite = finite && std::isfinite(sample) && std::fabs(sample) < 2.0f;
    }
    cg_destroy(effect);

    check(finite, "parameter changes between blocks stay finite");
}
#endif

} // namespace

int main()
{
    testFiniteOutputAndCeiling();
    testSilenceAndTail();
    testSampleRates();
    testParameterClamping();
    testFixedInputHash();
#ifdef CHIPGOODIZER_HAVE_CAPI
    testCApi();
    testConcurrentParameterChanges();
#endif

    if (gFailures == 0)
    {
        std::printf("all tests passed\n");
        return 0;
    }

    std::printf("%d check(s) failed\n", gFailures);
    return 1;
}

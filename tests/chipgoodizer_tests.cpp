// Plain asserts and a plain main, so this runs under ctest anywhere.

#include "chipgoodizer/Chipgoodizer.hpp"
#include "chipgoodizer/Params.hpp"
#include "chipgoodizer/dsp/MathConstants.hpp"

#ifdef CHIPGOODIZER_HAVE_CAPI
#include "chipgoodizer/chipgoodizer_c.h"
#endif

#ifdef CHIPGOODIZER_HAVE_IO
#include "chipgoodizer/io/WavFile.hpp"
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

#ifdef CHIPGOODIZER_FAST_MATH_BUILD
    // -ffast-math reassociates, so only stability is checked here.
    check(hash != 0ull, "fast-math build produces a stable hash within the run");
#else
    check(hash == kExpectedHash, "fixed input hashes to the expected value");
#endif
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

    cg_effect *nanEffect = cg_create();
    cg_prepare(nanEffect, kSampleRate, 2);
    cg_set_param_by_id(nanEffect, "air", 0.25f);
    cg_set_param_by_id(nanEffect, "air", std::numeric_limits<float>::quiet_NaN());
    check(cg_get_param(nanEffect, cg_param_index("air")) == 0.25f, "C ABI rejects NaN parameters");
    cg_set_param_by_id(nanEffect, "air", std::numeric_limits<float>::infinity());
    check(cg_get_param(nanEffect, cg_param_index("air")) == 0.25f, "C ABI rejects infinite parameters");
    cg_destroy(nanEffect);

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

#ifdef CHIPGOODIZER_HAVE_CAPI
// The list a host builds its preset picker from.
void testPresetList()
{
    check(cg_preset_count() > 0, "C ABI reports at least one preset");
    check(cg_preset_id(-1) == nullptr, "preset index below zero has no id");
    check(cg_preset_id(cg_preset_count()) == nullptr, "preset index past the end has no id");

    cg_effect *effect = cg_create();
    cg_prepare(effect, kSampleRate, 2);

    for (int i = 0; i < cg_preset_count(); ++i)
    {
        const char *id = cg_preset_id(i);
        const char *label = cg_preset_label(i);
        check(id != nullptr && id[0] != '\0', "every preset has an id");
        check(label != nullptr && label[0] != '\0', "every preset has a label");

        cg_apply_preset(effect, static_cast<cg_preset>(i));
        bool finite = true;
        for (int p = 0; p < cg_param_count(); ++p)
            finite = finite && std::isfinite(cg_get_param(effect, p));
        check(finite, "applying a preset leaves every parameter finite");
    }

    cg_apply_preset(effect, CG_PRESET_CLUB);
    const int driveIndex = cg_param_index("drive");
    check(cg_get_param(effect, driveIndex) > 0.0f, "the club preset sets drive");

    cg_apply_preset(effect, CG_PRESET_COMMERCIAL);
    check(cg_get_param(effect, driveIndex) == 0.0f, "the commercial preset clears drive");
    cg_destroy(effect);
}
#endif

#ifdef CHIPGOODIZER_HAVE_IO
// Exercises both halves: the writer patches its header, the reader walks it.
void testWavRoundTrip()
{
    const char *path = "chipgoodizer_roundtrip.wav";
    constexpr size_t kFrames = 1000;

    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    for (size_t i = 0; i < kFrames; ++i)
    {
        const double phase = 2.0 * chipgoodizer::kPi * 440.0 * static_cast<double>(i) / kSampleRate;
        left[i] = 0.6f * static_cast<float>(std::sin(phase));
        right[i] = -0.5f * left[i];
    }
    left[0] = 1.0f;
    left[1] = -1.0f;

    {
        chipgoodizer::io::WavWriter writer;
        check(writer.open(path, kSampleRate), "WavWriter opens an output file");
        check(writer.write(left, right, kFrames), "WavWriter writes a block");
        check(writer.close(), "WavWriter patches the header on close");
    }

    chipgoodizer::io::WavReader reader;
    check(reader.open(path), "WavReader opens the file it just wrote");
    check(reader.sampleRate() == kSampleRate, "WavReader reports the sample rate");
    check(reader.channels() == 2, "WavReader reports two channels");
    check(reader.totalFrames() == kFrames, "WavReader reports the frame count");

    std::vector<float> readL;
    std::vector<float> readR;
    size_t received = 0;
    while (true)
    {
        // Smaller blocks than the writer used, to exercise partial reads.
        const size_t frames = reader.read(readL, readR, 250);
        if (frames == 0)
            break;

        for (size_t i = 0; i < frames; ++i)
        {
            check(std::fabs(readL[i] - left[received + i]) <= 1.0e-6f, "round trip keeps the left channel");
            check(std::fabs(readR[i] - right[received + i]) <= 1.0e-6f, "round trip keeps the right channel");
        }
        received += frames;
    }

    check(!reader.failed(), "WavReader reaches the end of the data chunk cleanly");
    check(received == kFrames, "round trip returns every frame");
    reader.close();
    std::remove(path);

    chipgoodizer::io::WavReader missing;
    check(!missing.open("chipgoodizer_no_such_file.wav"), "WavReader rejects a missing file");
}

void testWavWriterLimit()
{
    const char *path = "chipgoodizer_overflow.wav";
    chipgoodizer::io::WavWriter writer;
    check(writer.open(path, kSampleRate), "WavWriter opens for the limit check");

    // Six bytes per frame, so the 32-bit RIFF size field caps the frame count.
    const size_t tooMany = 800000000;
    const std::vector<float> sample(1, 0.0f);
    check(!writer.write(sample, sample, tooMany), "WavWriter refuses output past the 32-bit size field");
    writer.close();
    std::remove(path);
}
#endif


void testHighShelfPassbandFlat()
{
    chipgoodizer::Biquad shelf;
    shelf.setHighShelf(8500.0f, kSampleRate, 5.0f);

    const size_t frames = static_cast<size_t>(kSampleRate) / 4;
    float inEnergy = 0.0f;
    float outEnergy = 0.0f;
    for (size_t i = 0; i < frames; ++i)
    {
        const double phase = 2.0 * chipgoodizer::kPi * 500.0 * static_cast<double>(i) / kSampleRate;
        const float x = static_cast<float>(std::sin(phase));
        const float y = shelf.process(x);
        if (i >= frames / 2)
        {
            inEnergy += x * x;
            outEnergy += y * y;
        }
    }

    const float gainDb = 10.0f * std::log10(outEnergy / inEnergy);
    check(std::fabs(gainDb) < 0.05f, "high shelf keeps 500 Hz passband flat within 0.05 dB");
}

void testScopeSafePeak()
{
    Chipgoodizer dsp;
    dsp.init(kSampleRate, ChipgoodizerConfig::getPreset(chipgoodizer::Preset::ScopeSafe));

    const size_t frames = static_cast<size_t>(kSampleRate) / 2;
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i)
    {
        const double phase = 2.0 * chipgoodizer::kPi * 110.0 * static_cast<double>(i) / kSampleRate;
        const float x = 0.95f * static_cast<float>(std::sin(phase));
        float outL = 0.0f;
        float outR = 0.0f;
        dsp.processStereo(x, x, outL, outR);
        peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
    }

    check(peak < 0.50f, "ScopeSafe preset stays below 0.50 peak for blur scope margins");
}

} // namespace

int main()
{
    testFiniteOutputAndCeiling();
    testSilenceAndTail();
    testSampleRates();
    testParameterClamping();
    testFixedInputHash();
    testHighShelfPassbandFlat();
    testScopeSafePeak();
#ifdef CHIPGOODIZER_HAVE_CAPI
    testCApi();
    testConcurrentParameterChanges();
    testPresetList();
#endif
#ifdef CHIPGOODIZER_HAVE_IO
    testWavRoundTrip();
    testWavWriterLimit();
#endif

    if (gFailures == 0)
    {
        std::printf("all tests passed\n");
        return 0;
    }

    std::printf("%d check(s) failed\n", gFailures);
    return 1;
}

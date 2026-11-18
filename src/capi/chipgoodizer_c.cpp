#include "chipgoodizer/chipgoodizer_c.h"

#include "chipgoodizer/Params.hpp"

#include <array>
#include <atomic>
#include <new>

using chipgoodizer::Chipgoodizer;
using chipgoodizer::ChipgoodizerConfig;
using chipgoodizer::ParamId;
using chipgoodizer::ParamSpec;
using chipgoodizer::Preset;

struct cg_effect
{
    cg_effect()
    {
        const ChipgoodizerConfig defaults = ChipgoodizerConfig::getPreset(Preset::Commercial);
        for (int i = 0; i < chipgoodizer::kParamCount; ++i)
            values[i].store(chipgoodizer::getParam(defaults, static_cast<ParamId>(i)), std::memory_order_relaxed);
    }

    ChipgoodizerConfig buildConfig() const
    {
        ChipgoodizerConfig cfg;
        for (int i = 0; i < chipgoodizer::kParamCount; ++i)
            chipgoodizer::setParam(cfg, static_cast<ParamId>(i), values[i].load(std::memory_order_relaxed));
        return cfg;
    }

    void publish()
    {
        dsp.setConfig(buildConfig());
    }

    std::atomic<float> values[chipgoodizer::kParamCount];
    Chipgoodizer dsp;
};

namespace
{

bool validIndex(int index)
{
    return index >= 0 && index < chipgoodizer::kParamCount;
}

void setParam(cg_effect *effect, int index, float value)
{
    if (!effect || !validIndex(index))
        return;

    const ParamSpec *spec = chipgoodizer::paramSpec(static_cast<ParamId>(index));
    if (!chipgoodizer::isFiniteValue(value))
        return;

    if (value < spec->minValue)
        value = spec->minValue;
    if (value > spec->maxValue)
        value = spec->maxValue;

    effect->values[index].store(value, std::memory_order_relaxed);
    effect->publish();
}

void loadPreset(cg_effect *effect, const ChipgoodizerConfig &cfg)
{
    for (int i = 0; i < chipgoodizer::kParamCount; ++i)
        effect->values[i].store(chipgoodizer::getParam(cfg, static_cast<ParamId>(i)), std::memory_order_relaxed);
    effect->publish();
}

} // namespace

extern "C"
{

cg_effect *cg_create(void)
{
    return new (std::nothrow) cg_effect();
}

void cg_destroy(cg_effect *effect)
{
    delete effect;
}

void cg_prepare(cg_effect *effect, double sample_rate, int channels)
{
    if (!effect || !chipgoodizer::isFiniteValue(sample_rate) || sample_rate <= 0.0)
        return;

    (void)channels;
    effect->dsp.init(static_cast<float>(sample_rate), effect->buildConfig());
}

void cg_reset(cg_effect *effect)
{
    if (!effect)
        return;
    effect->dsp.reset();
}

void cg_process_interleaved(cg_effect *effect, float *interleaved, int frames, int channels)
{
    if (!effect || !interleaved || frames <= 0 || channels <= 0)
        return;

    effect->dsp.processInterleaved(interleaved, static_cast<size_t>(frames), channels);
}

int cg_preset_count(void)
{
    return chipgoodizer::kPresetCount;
}

const char *cg_preset_id(int index)
{
    const chipgoodizer::PresetSpec *spec = chipgoodizer::presetSpecAt(index);
    return spec ? spec->id : nullptr;
}

const char *cg_preset_label(int index)
{
    const chipgoodizer::PresetSpec *spec = chipgoodizer::presetSpecAt(index);
    return spec ? spec->label : nullptr;
}

int cg_param_count(void)
{
    return chipgoodizer::kParamCount;
}

const cg_param_info *cg_param_at(int index)
{
    static const std::array<cg_param_info, chipgoodizer::kParamCount> table = []() {
        std::array<cg_param_info, chipgoodizer::kParamCount> out{};
        for (int i = 0; i < chipgoodizer::kParamCount; ++i)
        {
            const ParamSpec &spec = chipgoodizer::kParamSpecs[i];
            out[i] = {spec.id, spec.label, static_cast<cg_param_type>(spec.type),
                      spec.minValue, spec.maxValue, spec.defaultValue, spec.step};
        }
        return out;
    }();

    if (!validIndex(index))
        return nullptr;
    return &table[index];
}

int cg_param_index(const char *id)
{
    const ParamSpec *spec = chipgoodizer::findParamSpec(id);
    if (!spec)
        return -1;
    return static_cast<int>(spec - chipgoodizer::kParamSpecs);
}

float cg_get_param(const cg_effect *effect, int index)
{
    if (!effect || !validIndex(index))
        return 0.0f;
    return effect->values[index].load(std::memory_order_relaxed);
}

void cg_set_param(cg_effect *effect, int index, float value)
{
    setParam(effect, index, value);
}

void cg_set_param_by_id(cg_effect *effect, const char *id, float value)
{
    setParam(effect, cg_param_index(id), value);
}

void cg_apply_preset(cg_effect *effect, cg_preset preset)
{
    if (!effect)
        return;

    switch (preset)
    {
    case CG_PRESET_SCOPE_SAFE:
        loadPreset(effect, ChipgoodizerConfig::getPreset(Preset::ScopeSafe));
        break;

    case CG_PRESET_WARM:
        loadPreset(effect, ChipgoodizerConfig::getPreset(Preset::WarmChiptune));
        break;

    case CG_PRESET_CLUB:
        loadPreset(effect, ChipgoodizerConfig::getPreset(Preset::PunchyClub));
        break;

    case CG_PRESET_COMMERCIAL:
    default:
        loadPreset(effect, ChipgoodizerConfig::getPreset(Preset::Commercial));
        break;
    }
}

double cg_latency_frames(const cg_effect *effect)
{
    (void)effect;
    return 0.0;
}

} // extern "C"

#pragma once

#include "Chipgoodizer.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace chipgoodizer
{

// Numeric values are part of the C ABI; keep them stable.
enum class ParamType
{
    Bool = 0,
    Int = 1,
    Float = 2,
    Enum = 3
};

struct ParamSpec
{
    const char *id;
    const char *label;
    ParamType type;
    float minValue;
    float maxValue;
    float defaultValue;
    float step;
};

enum class ParamId : int
{
    Amount = 0,
    Rubber,
    Punch,
    Air,
    Deharsh,
    Warmth,
    Width,
    Ambience,
    Drive,
    Ceiling,
    XoverLow,
    XoverHigh,
    PhaseRotation,
    Count
};

constexpr int kParamCount = static_cast<int>(ParamId::Count);

inline const ParamSpec kParamSpecs[kParamCount] = {
    {"amount", "Mix", ParamType::Float, 0.0f, 1.0f, 0.85f, 0.0f},
    {"rubber", "Rubber", ParamType::Float, 0.0f, 1.0f, 0.85f, 0.0f},
    {"punch", "Punch", ParamType::Float, 0.0f, 1.0f, 0.85f, 0.0f},
    {"air", "Air", ParamType::Float, 0.0f, 1.0f, 0.70f, 0.0f},
    {"deharsh", "De-Harsh", ParamType::Float, 0.0f, 1.0f, 1.00f, 0.0f},
    {"warmth", "Warmth", ParamType::Float, 0.0f, 1.0f, 1.00f, 0.0f},
    {"width", "Width", ParamType::Float, 0.0f, 1.0f, 0.60f, 0.0f},
    {"ambience", "Ambience", ParamType::Float, 0.0f, 0.30f, 0.10f, 0.0f},
    {"drive", "Drive (dB)", ParamType::Float, -12.0f, 12.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling (dB)", ParamType::Float, -12.0f, 0.0f, -1.2f, 0.0f},
    {"xoverLow", "Low X-Over (Hz)", ParamType::Float, 120.0f, 400.0f, 220.0f, 0.0f},
    {"xoverHigh", "High X-Over (Hz)", ParamType::Float, 2500.0f, 6000.0f, 3800.0f, 0.0f},
    {"phaseRotation", "Kick Phase Rotation", ParamType::Bool, 0.0f, 1.0f, 1.0f, 1.0f},
};

inline const ParamSpec *paramSpec(ParamId id)
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kParamCount)
        return nullptr;
    return &kParamSpecs[index];
}

inline const ParamSpec *findParamSpec(const char *id)
{
    if (!id)
        return nullptr;
    for (int i = 0; i < kParamCount; ++i)
    {
        if (std::strcmp(kParamSpecs[i].id, id) == 0)
            return &kParamSpecs[i];
    }
    return nullptr;
}

inline float getParam(const ChipgoodizerConfig &cfg, ParamId id)
{
    switch (id)
    {
    case ParamId::Amount:
        return cfg.amount;

    case ParamId::Rubber:
        return cfg.rubber;

    case ParamId::Punch:
        return cfg.punch;

    case ParamId::Air:
        return cfg.air;

    case ParamId::Deharsh:
        return cfg.deharsh;

    case ParamId::Warmth:
        return cfg.warmth;

    case ParamId::Width:
        return cfg.width;

    case ParamId::Ambience:
        return cfg.ambience;

    case ParamId::Drive:
        return cfg.inputDriveDb;

    case ParamId::Ceiling:
        return cfg.ceilingDb;

    case ParamId::XoverLow:
        return cfg.xoverLowHz;

    case ParamId::XoverHigh:
        return cfg.xoverHighHz;

    case ParamId::PhaseRotation:
        return cfg.phaseRotation ? 1.0f : 0.0f;

    case ParamId::Count:
        break;
    }
    return 0.0f;
}

// Clamps to the spec range and rejects non-finite input. Returns false when the
// value was ignored.
inline bool setParam(ChipgoodizerConfig &cfg, ParamId id, float value)
{
    const ParamSpec *spec = paramSpec(id);
    if (!spec || !std::isfinite(value))
        return false;

    if (value < spec->minValue)
        value = spec->minValue;
    if (value > spec->maxValue)
        value = spec->maxValue;

    switch (id)
    {
    case ParamId::Amount:
        cfg.amount = value;
        break;

    case ParamId::Rubber:
        cfg.rubber = value;
        break;

    case ParamId::Punch:
        cfg.punch = value;
        break;

    case ParamId::Air:
        cfg.air = value;
        break;

    case ParamId::Deharsh:
        cfg.deharsh = value;
        break;

    case ParamId::Warmth:
        cfg.warmth = value;
        break;

    case ParamId::Width:
        cfg.width = value;
        break;

    case ParamId::Ambience:
        cfg.ambience = value;
        break;

    case ParamId::Drive:
        cfg.inputDriveDb = value;
        break;

    case ParamId::Ceiling:
        cfg.ceilingDb = value;
        break;

    case ParamId::XoverLow:
        cfg.xoverLowHz = value;
        break;

    case ParamId::XoverHigh:
        cfg.xoverHighHz = value;
        break;

    case ParamId::PhaseRotation:
        cfg.phaseRotation = (value >= 0.5f);
        break;

    case ParamId::Count:
        return false;
    }
    return true;
}

} // namespace chipgoodizer

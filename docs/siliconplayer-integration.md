# Using Chipgoodizer in Silicon Player

Chipgoodizer ships a C ABI (`include/chipgoodizer/chipgoodizer_c.h`) that maps
directly onto Silicon Player's `siliconplayer::effects::IAudioEffect` contract:
`prepare(sampleRate, channels)`, `reset()`, `process(interleaved, frames, channels)`,
`setParam(index, value)`, `latencyFrames()`. The parameter table is index-addressed
and its type enum matches `ParamType` (`Bool=0, Int=1, Float=2, Enum=3`).

Parameters are stored atomically and the filter bank is rebuilt once at the next
block boundary, so `setParam()` is safe to call from the UI thread while audio
runs. The engine is zero-latency and allocation-free once `cg_prepare()` has run.

## 1. Add the sources

Vendor the repository (submodule or copy) somewhere the native build can reach, e.g.
`external/chipgoodizer`, and add it to `app/src/main/cpp/CMakeLists.txt`:

```cmake
# Options must be set before add_subdirectory() reads them.
set(CHIPGOODIZER_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CHIPGOODIZER_BUILD_IO OFF CACHE BOOL "" FORCE)
set(CHIPGOODIZER_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(
        ../../../../external/chipgoodizer
        ${CMAKE_CURRENT_BINARY_DIR}/chipgoodizer
        EXCLUDE_FROM_ALL
)

target_link_libraries(siliconplayer PRIVATE chipgoodizer::capi)
```

`chipgoodizer::capi` pulls in `chipgoodizer::dsp` and publishes its include
directory.

## 2. Node wrapper

`app/src/main/cpp/effects/nodes/ChipgoodizerEffect.h`:

```cpp
#ifndef SILICONPLAYER_EFFECTS_NODES_CHIPGOODIZER_EFFECT_H
#define SILICONPLAYER_EFFECTS_NODES_CHIPGOODIZER_EFFECT_H

#include "../AudioEffect.h"

struct cg_effect;

namespace siliconplayer::effects {

class ChipgoodizerEffect final : public IAudioEffect {
public:
    static const EffectDescriptor& descriptor();

    ChipgoodizerEffect();
    ~ChipgoodizerEffect() override;

    void prepare(int sampleRate, int channels) override;
    void reset() override;
    void process(float* interleaved, int frames, int channels) override;
    void setParam(int index, float value) override;

private:
    cg_effect* fx;
};

} // namespace siliconplayer::effects

#endif // SILICONPLAYER_EFFECTS_NODES_CHIPGOODIZER_EFFECT_H
```

`app/src/main/cpp/effects/nodes/ChipgoodizerEffect.cpp`:

```cpp
#include "ChipgoodizerEffect.h"

#include "chipgoodizer/chipgoodizer_c.h"

#include <vector>

namespace siliconplayer::effects {

namespace {

const ParamDescriptor* paramTable(int& count) {
    static std::vector<ParamDescriptor> table;
    if (table.empty()) {
        const int n = cg_param_count();
        table.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const cg_param_info* info = cg_param_at(i);
            if (!info) continue;
            table.push_back({
                    info->id,
                    info->label,
                    static_cast<ParamType>(info->type),
                    info->min_value,
                    info->max_value,
                    info->default_value,
                    info->step,
                    0,
            });
        }
    }
    count = static_cast<int>(table.size());
    return table.data();
}

IAudioEffect* createChipgoodizerEffect() {
    return new ChipgoodizerEffect();
}

} // namespace

const EffectDescriptor& ChipgoodizerEffect::descriptor() {
    static int count = 0;
    const ParamDescriptor* params = paramTable(count);
    static const EffectDescriptor desc {
            "chipgoodizer.bus",
            "Chipgoodizer",
            1,
            params,
            count,
            false, // the engine has its own "Mix" parameter
            &createChipgoodizerEffect,
    };
    return desc;
}

ChipgoodizerEffect::ChipgoodizerEffect() : fx(cg_create()) {}

ChipgoodizerEffect::~ChipgoodizerEffect() {
    cg_destroy(fx);
}

void ChipgoodizerEffect::prepare(int sampleRate, int channels) {
    cg_prepare(fx, static_cast<double>(sampleRate), channels);
}

void ChipgoodizerEffect::reset() {
    cg_reset(fx);
}

void ChipgoodizerEffect::process(float* interleaved, int frames, int channels) {
    cg_process_interleaved(fx, interleaved, frames, channels);
}

void ChipgoodizerEffect::setParam(int index, float value) {
    cg_set_param(fx, index, value);
}

} // namespace siliconplayer::effects
```

Register it in `effects/EffectRegistry.cpp`:

```cpp
#include "nodes/ChipgoodizerEffect.h"
// ...
registerEffect(ChipgoodizerEffect::descriptor());
```

and add the .cpp to the `siliconplayer` source list. The effect then shows up in
`getEffectDescriptorsJson()` automatically, so `EffectChain.kt` needs no change
(the Kotlin `DefaultEffectDescriptors` fallback is only used if the native query
fails).

## 3. Notes

- `process()` handles mono (`channels == 1`) by feeding the same sample to both
  internal channels and writing the average back, and passes through any channel
  above 2. Silicon Player's stereo pipeline uses the `channels == 2` path.
- Presets are available via `cg_apply_preset(fx, CG_PRESET_COMMERCIAL |
  CG_PRESET_SCOPE_SAFE | CG_PRESET_WARM | CG_PRESET_CLUB)`; wire them to a node
  context menu if wanted.
- `CG_PRESET_SCOPE_SAFE` keeps peaks below the ±0.5 display range used by
  Audacious' blur scope.

# Chipgoodizer

A 2-bus mastering effect for chiptune and tracker mixes: three-band dynamics and
saturation, kick phase rotation, de-harshing, stereo width, room ambience and a
soft-knee ceiling.

Made primarily for use with [Silicon Player](https://github.com/SiliconPlayer/SiliconPlayer).

## Build

```sh
make     # ./chipgoodizer, the CLI on its own
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # libraries and tests
cmake --build build
ctest --test-dir build --output-on-failure
```

Targets: `chipgoodizer_dsp` (engine), `chipgoodizer_capi` (C ABI),
`chipgoodizer_io` (WAV file I/O), `chipgoodizer` (CLI).

## CLI

```sh
./chipgoodizer in.wav out.wav --preset commercial
./chipgoodizer in.wav out.wav --air 0.5 --width 0.8 --ceiling -2
./chipgoodizer --describe    # parameter table
./chipgoodizer --help
```

Presets: `commercial`, `scope-safe`, `warm`, `club`. Input is 16/24/32-bit PCM
or 32-bit float, output is 24-bit PCM.

## Integrate

```c
#include "chipgoodizer/chipgoodizer_c.h"

cg_effect *fx = cg_create();          /* commercial preset */
cg_prepare(fx, 48000.0, 2);
cg_set_param_by_id(fx, "ceiling", -1.2f);
cg_process_interleaved(fx, interleaved, frames, 2);
cg_destroy(fx);
```

`cg_set_param()` is safe to call from another thread. C++ callers can use
`chipgoodizer::Chipgoodizer` from `chipgoodizer::dsp` instead. Host wrapper:
`docs/siliconplayer-integration.md`.

## License

MIT, see [LICENSE](LICENSE).

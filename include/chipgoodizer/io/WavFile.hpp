#pragma once

#include <string>
#include <vector>

namespace chipgoodizer::io
{

// Loads a RIFF/WAVE file into planar stereo floats. Supports 16/24/32-bit PCM,
// 32-bit IEEE float, and WAVE_FORMAT_EXTENSIBLE wrapping either of them. Mono
// files are duplicated to both channels. Returns false on unsupported input.
bool loadWav(const std::string &path, std::vector<float> &outLeft, std::vector<float> &outRight,
             float &outSampleRate);

// Writes planar stereo as a 24-bit PCM RIFF/WAVE file. Samples are clamped to
// [-1, 1].
bool writeWav24(const std::string &path, const std::vector<float> &left, const std::vector<float> &right,
                float sampleRate);

} // namespace chipgoodizer::io

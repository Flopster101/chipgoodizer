#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace chipgoodizer::io
{

// Streams a RIFF/WAVE file in blocks. 16/24/32-bit PCM or 32-bit float,
// including WAVE_FORMAT_EXTENSIBLE; mono is duplicated, extra channels dropped.
class WavReader
{
public:
    bool open(const std::string &path);
    void close();

    float sampleRate() const
    {
        return sampleRateHz;
    }

    int channels() const
    {
        return channelCount;
    }

    uint64_t totalFrames() const
    {
        return totalFrameCount;
    }

    // Returns frames read: 0 at the end, failed() after a truncated read.
    size_t read(std::vector<float> &left, std::vector<float> &right, size_t maxFrames);
    bool failed() const
    {
        return failedRead;
    }

private:
    std::ifstream file;
    std::vector<uint8_t> scratch;
    float sampleRateHz{0.0f};
    int channelCount{0};
    int bytesPerSample{0};
    bool floatSamples{false};
    uint64_t totalFrameCount{0};
    uint64_t framesRemaining{0};
    bool failedRead{false};
};

// Streams planar stereo to a 24-bit PCM file, patching the header on close().
class WavWriter
{
public:
    bool open(const std::string &path, float sampleRate);
    bool write(const std::vector<float> &left, const std::vector<float> &right, size_t frames);
    bool close();

private:
    std::ofstream file;
    std::vector<uint8_t> scratch;
    uint64_t framesWritten{0};
    uint64_t capacityFrames{0};
    bool openFailed{false};
};

// Whole-file wrapper around WavReader.
bool loadWav(const std::string &path, std::vector<float> &outLeft, std::vector<float> &outRight,
             float &outSampleRate);

// Whole-file wrapper around WavWriter.
bool writeWav24(const std::string &path, const std::vector<float> &left, const std::vector<float> &right,
                float sampleRate);

} // namespace chipgoodizer::io

#include "chipgoodizer/io/WavFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace chipgoodizer::io
{

namespace
{

#pragma pack(push, 1)
struct WavHeader
{
    char riffTag[4];
    uint32_t riffLength;
    char waveTag[4];
};

struct ChunkHeader
{
    char chunkId[4];
    uint32_t chunkSize;
};

struct FmtChunk
{
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)

constexpr uint16_t kFmtPcm = 1;
constexpr uint16_t kFmtIeeeFloat = 3;
constexpr uint16_t kFmtExtensible = 0xFFFE;

// Largest data chunk the 32-bit RIFF size field can describe.
constexpr size_t kMaxPayloadBytes = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - 36;
constexpr size_t kOutputFramesLimit = kMaxPayloadBytes / (2 * 3);

bool isSupportedFormat(const FmtChunk &fmt)
{
    if (fmt.numChannels < 1 || fmt.sampleRate == 0)
        return false;

    switch (fmt.audioFormat)
    {
    case kFmtPcm:
        return fmt.bitsPerSample == 16 || fmt.bitsPerSample == 24 || fmt.bitsPerSample == 32;

    case kFmtIeeeFloat:
        return fmt.bitsPerSample == 32;
    }

    return false;
}

// Leaves the stream at the first byte of audio. Chunk sizes are checked against
// the real file size, so a lying header cannot read past the end.
bool parseHeader(std::ifstream &file, std::streamoff fileSize, FmtChunk &outFmt, uint64_t &outDataBytes)
{
    WavHeader waveHeader{};
    file.read(reinterpret_cast<char *>(&waveHeader), sizeof(waveHeader));
    if (!file || std::memcmp(waveHeader.riffTag, "RIFF", 4) != 0 || std::memcmp(waveHeader.waveTag, "WAVE", 4) != 0)
        return false;

    bool foundFmt = false;

    while (file)
    {
        ChunkHeader ch{};
        file.read(reinterpret_cast<char *>(&ch), sizeof(ch));
        if (!file)
            break;

        const std::streamoff remaining = fileSize - static_cast<std::streamoff>(file.tellg());
        if (remaining < 0 || static_cast<std::streamoff>(ch.chunkSize) > remaining)
            return false;

        const size_t chunkSize = ch.chunkSize;
        const size_t paddedSize = chunkSize + (chunkSize & 1);

        if (std::memcmp(ch.chunkId, "fmt ", 4) == 0)
        {
            uint8_t fmtBody[40]{};
            const size_t readSize = std::min(sizeof(fmtBody), chunkSize);
            file.read(reinterpret_cast<char *>(fmtBody), static_cast<std::streamsize>(readSize));
            if (static_cast<size_t>(file.gcount()) != readSize)
                return false;
            if (readSize < paddedSize)
                file.seekg(static_cast<std::streamoff>(paddedSize - readSize), std::ios::cur);

            std::memcpy(&outFmt, fmtBody, std::min(sizeof(FmtChunk), readSize));
            // WAVE_FORMAT_EXTENSIBLE keeps the real format in the SubFormat GUID.
            if (outFmt.audioFormat == kFmtExtensible && readSize >= 26)
            {
                uint16_t subFormat = 0;
                std::memcpy(&subFormat, fmtBody + 24, sizeof(subFormat));
                outFmt.audioFormat = subFormat;
            }
            foundFmt = true;
        }
        else if (std::memcmp(ch.chunkId, "data", 4) == 0)
        {
            outDataBytes = chunkSize;
            return foundFmt && isSupportedFormat(outFmt);
        }
        else
        {
            file.seekg(static_cast<std::streamoff>(paddedSize), std::ios::cur);
        }
    }

    return false;
}

float decodeSample(const uint8_t *src, int bytesPerSample, bool floatSamples)
{
    if (floatSamples)
    {
        float value = 0.0f;
        std::memcpy(&value, src, sizeof(value));
        return value;
    }

    if (bytesPerSample == 2)
    {
        int16_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        return value / 32768.0f;
    }

    if (bytesPerSample == 3)
    {
        int32_t value = (src[0]) | (src[1] << 8) | (src[2] << 16);
        if (value & 0x800000)
            value |= ~0xFFFFFF;
        return value / 8388608.0f;
    }

    int32_t value = 0;
    std::memcpy(&value, src, sizeof(value));
    return value / 2147483648.0f;
}

// Decodes interleaved input into planar stereo, duplicating mono and dropping
// anything past the second channel.
void decodeFrames(const uint8_t *src, size_t frames, int channels, int bytesPerSample, bool floatSamples,
                  std::vector<float> &left, std::vector<float> &right)
{
    left.resize(frames);
    right.resize(frames);

    const size_t bytesPerFrame = static_cast<size_t>(bytesPerSample) * static_cast<size_t>(channels);
    for (size_t i = 0; i < frames; ++i)
    {
        const uint8_t *frame = src + i * bytesPerFrame;
        const float first = decodeSample(frame, bytesPerSample, floatSamples);
        left[i] = first;
        right[i] = (channels > 1) ? decodeSample(frame + bytesPerSample, bytesPerSample, floatSamples) : first;
    }
}

} // namespace

bool WavReader::open(const std::string &path)
{
    close();

    file.open(path, std::ios::binary);
    if (!file)
        return false;

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    FmtChunk fmt{};
    uint64_t dataBytes = 0;
    if (!parseHeader(file, fileSize, fmt, dataBytes) || dataBytes == 0)
    {
        close();
        return false;
    }

    sampleRateHz = static_cast<float>(fmt.sampleRate);
    channelCount = fmt.numChannels;
    bytesPerSample = fmt.bitsPerSample / 8;
    floatSamples = fmt.audioFormat == kFmtIeeeFloat;
    totalFrameCount = dataBytes / (static_cast<uint64_t>(bytesPerSample) * static_cast<uint64_t>(channelCount));
    framesRemaining = totalFrameCount;
    failedRead = false;
    return true;
}

void WavReader::close()
{
    if (file.is_open())
        file.close();
    file.clear();
    scratch.clear();
    sampleRateHz = 0.0f;
    channelCount = 0;
    bytesPerSample = 0;
    floatSamples = false;
    totalFrameCount = 0;
    framesRemaining = 0;
    failedRead = false;
}

size_t WavReader::read(std::vector<float> &left, std::vector<float> &right, size_t maxFrames)
{
    if (!file.is_open() || failedRead || framesRemaining == 0 || maxFrames == 0)
        return 0;

    const size_t frames = static_cast<size_t>(std::min<uint64_t>(maxFrames, framesRemaining));
    const size_t bytesPerFrame = static_cast<size_t>(bytesPerSample) * static_cast<size_t>(channelCount);

    scratch.resize(frames * bytesPerFrame);
    file.read(reinterpret_cast<char *>(scratch.data()), static_cast<std::streamsize>(scratch.size()));
    if (static_cast<size_t>(file.gcount()) != scratch.size())
    {
        failedRead = true;
        framesRemaining = 0;
        return 0;
    }

    framesRemaining -= frames;
    decodeFrames(scratch.data(), frames, channelCount, bytesPerSample, floatSamples, left, right);
    return frames;
}

bool WavWriter::open(const std::string &path, float sampleRate)
{
    file.open(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        openFailed = true;
        return false;
    }

    WavHeader waveHeader;
    std::memcpy(waveHeader.riffTag, "RIFF", 4);
    waveHeader.riffLength = 36;
    std::memcpy(waveHeader.waveTag, "WAVE", 4);
    file.write(reinterpret_cast<const char *>(&waveHeader), sizeof(waveHeader));

    ChunkHeader fmtHeader;
    std::memcpy(fmtHeader.chunkId, "fmt ", 4);
    fmtHeader.chunkSize = 16;
    file.write(reinterpret_cast<const char *>(&fmtHeader), sizeof(fmtHeader));

    FmtChunk fmt{};
    fmt.audioFormat = kFmtPcm;
    fmt.numChannels = 2;
    fmt.sampleRate = static_cast<uint32_t>(sampleRate);
    fmt.bitsPerSample = 24;
    fmt.blockAlign = fmt.numChannels * (fmt.bitsPerSample / 8);
    fmt.byteRate = fmt.sampleRate * fmt.blockAlign;
    file.write(reinterpret_cast<const char *>(&fmt), sizeof(fmt));

    ChunkHeader dataHeader;
    std::memcpy(dataHeader.chunkId, "data", 4);
    dataHeader.chunkSize = 0;
    file.write(reinterpret_cast<const char *>(&dataHeader), sizeof(dataHeader));

    openFailed = !file;
    framesWritten = 0;
    capacityFrames = kOutputFramesLimit;
    return !openFailed;
}

bool WavWriter::write(const std::vector<float> &left, const std::vector<float> &right, size_t frames)
{
    if (!file.is_open() || openFailed)
        return false;
    if (framesWritten > capacityFrames || frames > capacityFrames - framesWritten)
        return false;

    scratch.resize(frames * 6);
    uint8_t *dest = scratch.data();
    for (size_t i = 0; i < frames; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float sample = (ch == 0) ? left[i] : right[i];
            int32_t value = static_cast<int32_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 8388607.0f));
            *dest++ = static_cast<uint8_t>(value & 0xFF);
            *dest++ = static_cast<uint8_t>((value >> 8) & 0xFF);
            *dest++ = static_cast<uint8_t>((value >> 16) & 0xFF);
        }
    }

    file.write(reinterpret_cast<const char *>(scratch.data()), static_cast<std::streamsize>(scratch.size()));
    if (!file)
        return false;

    framesWritten += frames;
    return true;
}

bool WavWriter::close()
{
    if (!file.is_open())
        return false;

    const uint32_t dataBytes = static_cast<uint32_t>(framesWritten * 6);
    const uint32_t riffLength = 36 + dataBytes;

    file.seekp(4, std::ios::beg);
    file.write(reinterpret_cast<const char *>(&riffLength), 4);
    file.seekp(40, std::ios::beg);
    file.write(reinterpret_cast<const char *>(&dataBytes), 4);

    const bool ok = static_cast<bool>(file);
    file.close();
    return ok;
}

bool loadWav(const std::string &path, std::vector<float> &outLeft, std::vector<float> &outRight, float &outSampleRate)
{
    WavReader reader;
    if (!reader.open(path))
        return false;

    outSampleRate = reader.sampleRate();
    const size_t frames = static_cast<size_t>(reader.totalFrames());
    if (reader.read(outLeft, outRight, frames) != frames || reader.failed())
        return false;

    return true;
}

bool writeWav24(const std::string &path, const std::vector<float> &left, const std::vector<float> &right, float sampleRate)
{
    if (left.size() > kOutputFramesLimit)
        return false;

    WavWriter writer;
    if (!writer.open(path, sampleRate))
        return false;
    if (!writer.write(left, right, left.size()))
        return false;
    return writer.close();
}

} // namespace chipgoodizer::io

#include "chipgoodizer/io/WavFile.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

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

} // namespace

bool loadWav(const std::string &path, std::vector<float> &outLeft, std::vector<float> &outRight, float &outSampleRate)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    WavHeader waveHeader;
    file.read(reinterpret_cast<char *>(&waveHeader), sizeof(waveHeader));
    if (std::memcmp(waveHeader.riffTag, "RIFF", 4) != 0 || std::memcmp(waveHeader.waveTag, "WAVE", 4) != 0)
        return false;

    FmtChunk fmt{};
    bool foundFmt = false;
    std::vector<uint8_t> rawAudio;

    while (file)
    {
        ChunkHeader ch;
        file.read(reinterpret_cast<char *>(&ch), sizeof(ch));
        if (!file)
            break;

        if (std::memcmp(ch.chunkId, "fmt ", 4) == 0)
        {
            uint8_t fmtBody[40]{};
            size_t readSize = std::min(sizeof(fmtBody), static_cast<size_t>(ch.chunkSize));
            file.read(reinterpret_cast<char *>(fmtBody), readSize);
            if (ch.chunkSize > readSize)
                file.seekg(ch.chunkSize - readSize, std::ios::cur);
            std::memcpy(&fmt, fmtBody, std::min(sizeof(FmtChunk), readSize));
            // WAVE_FORMAT_EXTENSIBLE: the real format lives in the SubFormat GUID
            if (fmt.audioFormat == 0xFFFE && readSize >= 26)
            {
                uint16_t subFormat;
                std::memcpy(&subFormat, fmtBody + 24, sizeof(subFormat));
                fmt.audioFormat = subFormat;
            }
            foundFmt = true;
        }
        else if (std::memcmp(ch.chunkId, "data", 4) == 0)
        {
            rawAudio.resize(ch.chunkSize);
            file.read(reinterpret_cast<char *>(rawAudio.data()), ch.chunkSize);
            break;
        }
        else
        {
            file.seekg(ch.chunkSize, std::ios::cur);
        }
    }

    if (!foundFmt || rawAudio.empty())
        return false;

    if (fmt.audioFormat != 1 && fmt.audioFormat != 3)
        return false;

    outSampleRate = static_cast<float>(fmt.sampleRate);
    size_t numChannels = fmt.numChannels;
    size_t bytesPerSample = fmt.bitsPerSample / 8;
    size_t numSamples = rawAudio.size() / bytesPerSample;
    size_t numFrames = numSamples / numChannels;

    outLeft.resize(numFrames);
    outRight.resize(numFrames);

    const uint8_t *src = rawAudio.data();
    for (size_t i = 0; i < numFrames; ++i)
    {
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            float s = 0.0f;
            if (fmt.audioFormat == 1) // PCM
            {
                if (bytesPerSample == 2)
                {
                    int16_t val;
                    std::memcpy(&val, src, 2);
                    s = val / 32768.0f;
                    src += 2;
                }
                else if (bytesPerSample == 3)
                {
                    int32_t val = (src[0]) | (src[1] << 8) | (src[2] << 16);
                    if (val & 0x800000)
                        val |= ~0xFFFFFF;
                    s = val / 8388608.0f;
                    src += 3;
                }
                else if (bytesPerSample == 4)
                {
                    int32_t val;
                    std::memcpy(&val, src, 4);
                    s = val / 2147483648.0f;
                    src += 4;
                }
            }
            else if (fmt.audioFormat == 3) // IEEE Float
            {
                if (bytesPerSample == 4)
                {
                    std::memcpy(&s, src, 4);
                    src += 4;
                }
            }

            if (ch == 0)
                outLeft[i] = s;
            else if (ch == 1)
                outRight[i] = s;
        }

        if (numChannels == 1)
            outRight[i] = outLeft[i];
    }

    return true;
}

bool writeWav24(const std::string &path, const std::vector<float> &left, const std::vector<float> &right, float sampleRate)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;

    size_t numFrames = left.size();
    uint32_t dataBytes = static_cast<uint32_t>(numFrames * 2 * 3);

    WavHeader waveHeader;
    std::memcpy(waveHeader.riffTag, "RIFF", 4);
    waveHeader.riffLength = 36 + dataBytes;
    std::memcpy(waveHeader.waveTag, "WAVE", 4);
    file.write(reinterpret_cast<const char *>(&waveHeader), sizeof(waveHeader));

    ChunkHeader fmtHeader;
    std::memcpy(fmtHeader.chunkId, "fmt ", 4);
    fmtHeader.chunkSize = 16;
    file.write(reinterpret_cast<const char *>(&fmtHeader), sizeof(fmtHeader));

    FmtChunk fmt{};
    fmt.audioFormat = 1;
    fmt.numChannels = 2;
    fmt.sampleRate = static_cast<uint32_t>(sampleRate);
    fmt.bitsPerSample = 24;
    fmt.blockAlign = fmt.numChannels * (fmt.bitsPerSample / 8);
    fmt.byteRate = fmt.sampleRate * fmt.blockAlign;
    file.write(reinterpret_cast<const char *>(&fmt), sizeof(fmt));

    ChunkHeader dataHeader;
    std::memcpy(dataHeader.chunkId, "data", 4);
    dataHeader.chunkSize = dataBytes;
    file.write(reinterpret_cast<const char *>(&dataHeader), sizeof(dataHeader));

    for (size_t i = 0; i < numFrames; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float s = (ch == 0) ? left[i] : right[i];
            s = std::clamp(s, -1.0f, 1.0f);
            int32_t val = static_cast<int32_t>(s * 8388607.0f);
            uint8_t bytes[3];
            bytes[0] = val & 0xFF;
            bytes[1] = (val >> 8) & 0xFF;
            bytes[2] = (val >> 16) & 0xFF;
            file.write(reinterpret_cast<const char *>(bytes), 3);
        }
    }

    return true;
}

} // namespace chipgoodizer::io

// Offline renderer for auditioning presets and verifying renders.

#include "chipgoodizer/Chipgoodizer.hpp"
#include "chipgoodizer/Params.hpp"
#include "chipgoodizer/io/WavFile.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using chipgoodizer::Chipgoodizer;
using chipgoodizer::ChipgoodizerConfig;
using chipgoodizer::ParamId;
using chipgoodizer::ParamSpec;
using chipgoodizer::ParamType;
using chipgoodizer::PresetSpec;

// std::stof throws on garbage and accepts "12abc"; strtof does neither.
bool parseNumber(const char *text, float &out)
{
    if (!text || *text == '\0')
        return false;

    errno = 0;
    char *end = nullptr;
    const float value = std::strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !chipgoodizer::isFiniteValue(value))
        return false;

    out = value;
    return true;
}

void printUsage()
{
    std::cout << "Usage: chipgoodizer INPUT.wav OUTPUT.wav [options]\n\n"
              << "Presets:\n"
              << "  --preset commercial     (Default tuned commercial sound)\n"
              << "  --preset scope-safe     (Ceiling <= -4.8 dBFS for strict visual oscilloscope margins)\n"
              << "  --preset warm           (Extra 420 Hz warmth, smoother highs)\n"
              << "  --preset club           (Aggressive punch and driving low end)\n\n"
              << "Macro Knobs (0.0 - 1.0):\n"
              << "  --amount <0.0-1.0>      Wet/dry blend             (default: 0.85)\n"
              << "  --rubber <0.0-1.0>      Bass weight & sine dome   (default: 0.85)\n"
              << "  --punch <0.0-1.0>       Drum snap & mid impact    (default: 0.85)\n"
              << "  --air <0.0-1.0>         High-frequency sheen      (default: 0.70)\n"
              << "  --deharsh <0.0-1.0>     Upper-mid/high de-harsher (default: 1.00)\n"
              << "  --warmth <0.0-1.0>      420 Hz lead body fill     (default: 1.00)\n"
              << "  --width <0.0-1.0>       Stereo width              (default: 0.60)\n"
              << "  --ambience <0.0-0.3>    Studio acoustic glue      (default: 0.10)\n\n"
              << "Levels & Gain Staging:\n"
              << "  --drive <dB>            Input pre-gain            (default: 0.0)\n"
              << "  --ceiling <dB>          Master ceiling limit      (default: -1.2)\n\n"
              << "Tunables:\n"
              << "  --xover-low <Hz>        Low/Mid crossover         (default: 220.0)\n"
              << "  --xover-high <Hz>       Mid/High crossover        (default: 3800.0)\n"
              << "  --no-phase-rot          Disable kick phase rotator\n\n"
              << "Other:\n"
              << "  --describe              Print the parameter table and exit\n"
              << "  -h, --help              Show this help\n";
}

const char *paramTypeName(ParamType type)
{
    switch (type)
    {
    case ParamType::Bool:
        return "bool";

    case ParamType::Int:
        return "int";

    case ParamType::Float:
        return "float";

    case ParamType::Enum:
        return "enum";
    }
    return "float";
}

void printParameters()
{
    std::cout << std::left << std::setw(16) << "PARAM" << std::setw(22) << "LABEL" << std::setw(8) << "TYPE"
              << std::right << std::setw(9) << "MIN" << std::setw(9) << "MAX" << std::setw(9) << "DEFAULT"
              << std::setw(8) << "STEP" << "\n";
    for (int i = 0; i < chipgoodizer::kParamCount; ++i)
    {
        const ParamSpec &spec = chipgoodizer::kParamSpecs[i];
        std::cout << std::left << std::setw(16) << spec.id << std::setw(22) << spec.label << std::setw(8)
                  << paramTypeName(spec.type) << std::right << std::setw(9) << spec.minValue << std::setw(9)
                  << spec.maxValue << std::setw(9) << spec.defaultValue << std::setw(8) << spec.step << "\n";
    }
}

bool applyPreset(const std::string &name, ChipgoodizerConfig &cfg)
{
    // "scopesafe" is the historical spelling of the scope-safe preset.
    const std::string id = (name == "scopesafe") ? "scope-safe" : name;
    const PresetSpec *spec = chipgoodizer::findPresetSpec(id.c_str());
    if (!spec)
        return false;

    cfg = ChipgoodizerConfig::getPreset(spec->preset);
    return true;
}

// Accepts --xoverLow and --xover-low.
bool setNumericParam(ChipgoodizerConfig &cfg, const std::string &flag, float value)
{
    std::string id = flag;
    if (id == "xover-low")
        id = "xoverLow";
    else if (id == "xover-high")
        id = "xoverHigh";

    const ParamSpec *spec = chipgoodizer::findParamSpec(id.c_str());
    if (!spec)
        return false;
    return chipgoodizer::setParam(cfg, static_cast<ParamId>(spec - chipgoodizer::kParamSpecs), value);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc >= 2)
    {
        const std::string first = argv[1];
        if (first == "-h" || first == "--help")
        {
            printUsage();
            return 0;
        }
        if (first == "--describe")
        {
            printParameters();
            return 0;
        }
    }

    if (argc < 3)
    {
        printUsage();
        return 1;
    }

    const std::string inPath = argv[1];
    const std::string outPath = argv[2];

    ChipgoodizerConfig cfg;

    for (int i = 3; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc)
        {
            if (!applyPreset(argv[++i], cfg))
            {
                std::cerr << "Unknown preset: " << argv[i] << "\n";
                return 1;
            }
        }
        else if (arg == "--no-phase-rot")
        {
            cfg.phaseRotation = false;
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage();
            return 0;
        }
        else if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-')
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << arg << "\n";
                return 1;
            }
            const char *text = argv[++i];
            float value = 0.0f;
            if (!parseNumber(text, value))
            {
                std::cerr << "Invalid value for " << arg << ": " << text << "\n";
                return 1;
            }
            if (!setNumericParam(cfg, arg.substr(2), value))
            {
                std::cerr << "Unknown option: " << arg << "\n";
                return 1;
            }
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    chipgoodizer::io::WavReader reader;
    if (!reader.open(inPath))
    {
        std::cerr << "Failed to read input audio file: " << inPath << "\n";
        return 1;
    }

    Chipgoodizer processor;
    processor.init(reader.sampleRate(), cfg);

    chipgoodizer::io::WavWriter writer;
    if (!writer.open(outPath, reader.sampleRate()))
    {
        std::cerr << "Failed to write output audio file: " << outPath << "\n";
        return 1;
    }

    // Blocks, so a long render stays out of memory.
    constexpr size_t kBlockFrames = 8192;
    std::vector<float> inL;
    std::vector<float> inR;
    std::vector<float> outL(kBlockFrames);
    std::vector<float> outR(kBlockFrames);

    while (true)
    {
        const size_t frames = reader.read(inL, inR, kBlockFrames);
        if (frames == 0)
            break;

        processor.process(inL.data(), inR.data(), outL.data(), outR.data(), frames);
        if (!writer.write(outL, outR, frames))
        {
            std::cerr << "Failed to write output audio file: " << outPath << "\n";
            return 1;
        }
    }

    if (reader.failed())
    {
        std::cerr << "Truncated input audio file: " << inPath << "\n";
        return 1;
    }

    if (!writer.close())
    {
        std::cerr << "Failed to write output audio file: " << outPath << "\n";
        return 1;
    }

    std::cout << "Successfully rendered: " << outPath << "\n";
    return 0;
}

#pragma once

#include <filesystem>

namespace video_privacy
{

struct AppConfig
{
    std::filesystem::path modelPath;
    std::filesystem::path inputVideoPath;
    std::filesystem::path outputVideoPath;
    float confidenceThreshold{0.18F};
    float nmsThreshold{0.45F};
    int mosaicStrength{60};
    int cudaDeviceId{0};
};

} // namespace video_privacy

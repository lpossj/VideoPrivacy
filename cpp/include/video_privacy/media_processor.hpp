#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>

namespace video_privacy
{

class FaceDetector;

struct MediaProgress
{
    int completedUnits{};
    int totalUnits{};
    std::chrono::duration<double> elapsed{};
    std::chrono::duration<double> remaining{};
};

using ProgressCallback = std::function<void(const MediaProgress &)>;

struct MediaProcessingOptions
{
    std::vector<FaceDetector *> detectors;
    int mosaicStrength{60};
    bool preserveAudio{true};
    std::filesystem::path ffmpegExecutable;
};

struct MediaProcessingResult
{
    bool isVideo{};
    int processedUnits{};
    long long detections{};
    double elapsedSeconds{};
};

class MediaProcessor
{
public:
    MediaProcessingResult process(
        const std::filesystem::path &inputPath,
        const std::filesystem::path &outputPath,
        const MediaProcessingOptions &options,
        const ProgressCallback &progressCallback = {},
        const std::atomic_bool *cancelRequested = nullptr) const;

    static bool isSupportedImage(const std::filesystem::path &path);
    static bool isSupportedVideo(const std::filesystem::path &path);
};

} // namespace video_privacy

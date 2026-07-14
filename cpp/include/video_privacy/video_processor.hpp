#pragma once

#include <filesystem>

namespace video_privacy
{

class FaceDetector;

struct ProcessingStats
{
    int processedFrames{};
    long long detectedFaces{};
    double elapsedSeconds{};
};

class VideoProcessor
{
public:
    explicit VideoProcessor(int mosaicStrength);

    ProcessingStats process(
        const std::filesystem::path &inputPath,
        const std::filesystem::path &outputPath,
        FaceDetector &detector) const;

private:
    int mosaicStrength_;
};

} // namespace video_privacy

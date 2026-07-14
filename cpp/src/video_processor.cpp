#include "video_privacy/video_processor.hpp"

#include "video_privacy/face_detector.hpp"
#include "video_privacy/mosaic.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <opencv2/videoio.hpp>

namespace video_privacy
{

VideoProcessor::VideoProcessor(int mosaicStrength)
    : mosaicStrength_(mosaicStrength)
{
    if (mosaicStrength_ <= 0)
    {
        throw std::invalid_argument("Mosaic strength must be positive");
    }
}

ProcessingStats VideoProcessor::process(
    const std::filesystem::path &inputPath,
    const std::filesystem::path &outputPath,
    FaceDetector &detector) const
{
    if (!std::filesystem::is_regular_file(inputPath))
    {
        throw std::runtime_error("Input video does not exist: " + inputPath.string());
    }
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    cv::VideoCapture capture(inputPath.string());
    if (!capture.isOpened())
    {
        throw std::runtime_error("Failed to open input video: " + inputPath.string());
    }

    const int frameWidth = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int frameHeight = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = capture.get(cv::CAP_PROP_FPS);
    if (fps <= 0.0)
    {
        fps = 25.0;
    }
    const int totalFrames = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    cv::VideoWriter writer(
        outputPath.string(),
        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        fps,
        cv::Size(frameWidth, frameHeight));
    if (!writer.isOpened())
    {
        throw std::runtime_error("Failed to create output video: " + outputPath.string());
    }

    std::cout
        << "Execution provider: " << detector.executionProvider() << '\n'
        << "Input video: " << inputPath.string() << '\n'
        << "Resolution: " << frameWidth << " x " << frameHeight << '\n'
        << "FPS: " << fps << '\n'
        << "Total frames: " << totalFrames << '\n';

    ProcessingStats stats;
    cv::Mat frame;
    const auto startTime = std::chrono::steady_clock::now();
    while (capture.read(frame) && !frame.empty())
    {
        const std::vector<cv::Rect> faces = detector.detect(frame);
        stats.detectedFaces += static_cast<long long>(faces.size());
        for (const cv::Rect &face : faces)
        {
            applyMosaic(frame, face, mosaicStrength_);
        }
        writer.write(frame);
        ++stats.processedFrames;

        if (stats.processedFrames % 30 == 0)
        {
            std::cout << "\rProgress: " << stats.processedFrames;
            if (totalFrames > 0)
            {
                std::cout << " / " << totalFrames << " ("
                          << static_cast<int>(stats.processedFrames * 100.0 / totalFrames)
                          << "%)";
            }
            std::cout << std::flush;
        }
    }

    stats.elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime)
                               .count();
    std::cout
        << "\nProcessing completed\n"
        << "Processed frames: " << stats.processedFrames << '\n'
        << "Detected faces: " << stats.detectedFaces << '\n'
        << "Elapsed time: " << stats.elapsedSeconds << " seconds\n";
    if (stats.elapsedSeconds > 0.0)
    {
        std::cout << "Average speed: "
                  << stats.processedFrames / stats.elapsedSeconds << " FPS\n";
    }
    std::cout << "Saved video to: " << outputPath.string() << '\n';
    return stats;
}

} // namespace video_privacy

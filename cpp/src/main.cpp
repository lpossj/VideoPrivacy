#include "video_privacy/config.hpp"
#include "video_privacy/face_detector.hpp"
#include "video_privacy/video_processor.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

namespace
{

std::filesystem::path findProjectRoot(const char *executablePath)
{
    std::vector<std::filesystem::path> candidates{
        std::filesystem::current_path(),
        std::filesystem::absolute(executablePath).parent_path()};

    for (std::filesystem::path candidate : candidates)
    {
        while (!candidate.empty())
        {
            if (std::filesystem::is_directory(candidate / "models") &&
                std::filesystem::is_directory(candidate / "input"))
            {
                return candidate;
            }

            const std::filesystem::path parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }
            candidate = parent;
        }
    }

    return std::filesystem::current_path();
}

struct LaunchConfig
{
    video_privacy::AppConfig app;
    bool useCuda{false};
};

LaunchConfig makeConfig(int argc, char *argv[])
{
    const std::filesystem::path projectRoot = findProjectRoot(argv[0]);
    LaunchConfig config{{
        projectRoot / "models/face/face_yolo11s.onnx",
        projectRoot / "input/test.mp4",
        projectRoot / "output/cpp_gpu_face_blur.mp4"}};

    int argumentOffset = 1;
    if (argc > 1 && std::string(argv[1]) == "--gpu")
    {
        config.useCuda = true;
        argumentOffset = 2;
    }
    else if (argc > 1 && std::string(argv[1]) == "--cpu")
    {
        argumentOffset = 2;
    }

    const int remainingArguments = argc - argumentOffset;
    if (remainingArguments == 3)
    {
        config.app.modelPath = argv[argumentOffset];
        config.app.inputVideoPath = argv[argumentOffset + 1];
        config.app.outputVideoPath = argv[argumentOffset + 2];
    }
    else if (remainingArguments != 0)
    {
        throw std::invalid_argument(
            "Usage: video_privacy [--cpu|--gpu] [model.onnx input.mp4 output.mp4]");
    }
    return config;
}

} // namespace

int main(int argc, char *argv[])
{
    try
    {
        const LaunchConfig launch = makeConfig(argc, argv);
        const video_privacy::AppConfig &config = launch.app;
        video_privacy::DetectorConfig detectorConfig;
        detectorConfig.confidenceThreshold = config.confidenceThreshold;
        detectorConfig.nmsThreshold = config.nmsThreshold;
        detectorConfig.cudaDeviceId = config.cudaDeviceId;
        detectorConfig.useCuda = launch.useCuda;

        video_privacy::FaceDetector detector(config.modelPath, detectorConfig);
        std::cout << "Execution provider: " << detector.executionProvider() << '\n';
        const video_privacy::VideoProcessor processor(config.mosaicStrength);
        processor.process(
            config.inputVideoPath,
            config.outputVideoPath,
            detector);
        return 0;
    }
    catch (const Ort::Exception &error)
    {
        std::cerr << "ONNX Runtime error:\n" << error.what() << '\n';
    }
    catch (const cv::Exception &error)
    {
        std::cerr << "OpenCV error:\n" << error.what() << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "Program error:\n" << error.what() << '\n';
    }
    return 1;
}

#include "video_privacy/media_processor.hpp"

#include "video_privacy/face_detector.hpp"
#include "video_privacy/mosaic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <system_error>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace video_privacy
{
namespace
{

std::string lowercaseExtension(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

long long anonymizeFrame(
    cv::Mat &frame,
    const MediaProcessingOptions &options)
{
    long long detections = 0;
    for (FaceDetector *detector : options.detectors)
    {
        if (detector == nullptr)
        {
            continue;
        }
        const std::vector<cv::Rect> boxes = detector->detect(frame);
        detections += static_cast<long long>(boxes.size());
        for (const cv::Rect &box : boxes)
        {
            applyMosaic(frame, box, options.mosaicStrength);
        }
    }
    return detections;
}

MediaProgress makeProgress(
    int completed,
    int total,
    const std::chrono::steady_clock::time_point startTime)
{
    const auto elapsed = std::chrono::steady_clock::now() - startTime;
    std::chrono::duration<double> remaining{};
    if (completed > 0 && total > completed)
    {
        remaining = std::chrono::duration<double>(elapsed) *
                    (static_cast<double>(total - completed) / completed);
    }
    return {completed, total, elapsed, remaining};
}

#ifdef _WIN32
std::wstring quoteArgument(const std::filesystem::path &value)
{
    std::wstring argument = value.wstring();
    std::wstring quoted{L'"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
        }
        else if (character == L'"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
        }
        else
        {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

void muxOriginalAudio(
    const std::filesystem::path &ffmpeg,
    const std::filesystem::path &videoOnly,
    const std::filesystem::path &original,
    const std::filesystem::path &output)
{
    if (ffmpeg.empty())
    {
        throw std::runtime_error(
            "FFmpeg is required when Preserve original audio is enabled");
    }

    std::wstring command =
        quoteArgument(ffmpeg) + L" -hide_banner -loglevel error -y -i " +
        quoteArgument(videoOnly) + L" -i " + quoteArgument(original) +
        L" -map 0:v:0 -map 1:a? -c:v copy -c:a copy -shortest " +
        quoteArgument(output);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!created)
    {
        throw std::runtime_error("Failed to start FFmpeg");
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (exitCode != 0)
    {
        throw std::runtime_error("FFmpeg failed to preserve the original audio");
    }
}
#else
void muxOriginalAudio(
    const std::filesystem::path &,
    const std::filesystem::path &,
    const std::filesystem::path &,
    const std::filesystem::path &)
{
    throw std::runtime_error("Audio preservation is currently supported on Windows");
}
#endif

} // namespace

bool MediaProcessor::isSupportedImage(const std::filesystem::path &path)
{
    const std::string extension = lowercaseExtension(path);
    return extension == ".jpg" || extension == ".jpeg" ||
           extension == ".png" || extension == ".bmp" ||
           extension == ".webp" || extension == ".tif" ||
           extension == ".tiff";
}

bool MediaProcessor::isSupportedVideo(const std::filesystem::path &path)
{
    const std::string extension = lowercaseExtension(path);
    return extension == ".mp4" || extension == ".avi" ||
           extension == ".mov" || extension == ".mkv" ||
           extension == ".webm" || extension == ".m4v";
}

MediaProcessingResult MediaProcessor::process(
    const std::filesystem::path &inputPath,
    const std::filesystem::path &outputPath,
    const MediaProcessingOptions &options,
    const ProgressCallback &progressCallback,
    const std::atomic_bool *cancelRequested) const
{
    if (options.detectors.empty())
    {
        throw std::invalid_argument("At least one detector must be enabled");
    }
    if (!std::filesystem::is_regular_file(inputPath))
    {
        throw std::runtime_error("Input file does not exist: " + inputPath.string());
    }
    std::filesystem::create_directories(outputPath.parent_path());
    const auto startTime = std::chrono::steady_clock::now();
    MediaProcessingResult result;

    if (isSupportedImage(inputPath))
    {
        cv::Mat image = cv::imread(inputPath.string(), cv::IMREAD_COLOR);
        if (image.empty())
        {
            throw std::runtime_error("Failed to read image: " + inputPath.string());
        }
        result.detections = anonymizeFrame(image, options);
        if (cancelRequested != nullptr && cancelRequested->load())
        {
            throw std::runtime_error("Processing cancelled");
        }
        if (!cv::imwrite(outputPath.string(), image))
        {
            throw std::runtime_error("Failed to write image: " + outputPath.string());
        }
        result.processedUnits = 1;
        if (progressCallback)
        {
            progressCallback(makeProgress(1, 1, startTime));
        }
    }
    else if (isSupportedVideo(inputPath))
    {
        result.isVideo = true;
        cv::VideoCapture capture(inputPath.string());
        if (!capture.isOpened())
        {
            throw std::runtime_error("Failed to open video: " + inputPath.string());
        }

        const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
        const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        const int totalFrames = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
        double fps = capture.get(cv::CAP_PROP_FPS);
        if (fps <= 0.0)
        {
            fps = 25.0;
        }

        std::filesystem::path videoOutput = outputPath;
        if (options.preserveAudio)
        {
            videoOutput = outputPath.parent_path() /
                          (outputPath.stem().wstring() + L".video_only.tmp.mp4");
        }
        cv::VideoWriter writer(
            videoOutput.string(),
            cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
            fps,
            cv::Size(width, height));
        if (!writer.isOpened())
        {
            throw std::runtime_error("Failed to create video: " + videoOutput.string());
        }

        cv::Mat frame;
        while (capture.read(frame) && !frame.empty())
        {
            if (cancelRequested != nullptr && cancelRequested->load())
            {
                writer.release();
                capture.release();
                std::error_code ignored;
                std::filesystem::remove(videoOutput, ignored);
                throw std::runtime_error("Processing cancelled");
            }
            result.detections += anonymizeFrame(frame, options);
            writer.write(frame);
            ++result.processedUnits;
            if (progressCallback &&
                (result.processedUnits % 5 == 0 || result.processedUnits == totalFrames))
            {
                progressCallback(makeProgress(
                    result.processedUnits,
                    totalFrames,
                    startTime));
            }
        }
        writer.release();
        capture.release();

        if (options.preserveAudio)
        {
            muxOriginalAudio(
                options.ffmpegExecutable,
                videoOutput,
                inputPath,
                outputPath);
            std::error_code ignored;
            std::filesystem::remove(videoOutput, ignored);
        }
    }
    else
    {
        throw std::invalid_argument("Unsupported media type: " + inputPath.string());
    }

    result.elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime)
                                      .count();
    return result;
}

} // namespace video_privacy

#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace video_privacy
{

struct GpuRuntimeInstallResult
{
    bool success{};
    std::wstring message;
};

bool gpuRuntimeIsInstalled(const std::filesystem::path &runtimeRoot);

GpuRuntimeInstallResult installGpuRuntime(
    const std::filesystem::path &runtimeRoot,
    const std::function<void(const std::wstring &)> &statusCallback);

} // namespace video_privacy

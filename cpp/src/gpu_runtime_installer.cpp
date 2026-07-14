#include "video_privacy/gpu_runtime_installer.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#include <urlmon.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace video_privacy
{
namespace
{

const std::array<const wchar_t *, 16> RequiredGpuFiles{
    L"onnxruntime_providers_cuda.dll",
    L"gpu/cublasLt64_12.dll",
    L"gpu/cublas64_12.dll",
    L"gpu/cufft64_11.dll",
    L"gpu/cudart64_12.dll",
    L"gpu/cudnn64_9.dll",
    L"gpu/cudnn_adv64_9.dll",
    L"gpu/cudnn_cnn64_9.dll",
    L"gpu/cudnn_engines_precompiled64_9.dll",
    L"gpu/cudnn_engines_runtime_compiled64_9.dll",
    L"gpu/cudnn_graph64_9.dll",
    L"gpu/cudnn_heuristic64_9.dll",
    L"gpu/cudnn_ops64_9.dll",
    L"gpu/nvJitLink_120_0.dll",
    L"gpu/nvrtc-builtins64_128.dll",
    L"gpu/nvrtc64_120_0.dll"};

std::wstring readIniValue(
    const std::filesystem::path &manifest,
    const wchar_t *key)
{
    std::array<wchar_t, 4096> value{};
    GetPrivateProfileStringW(
        L"GPU",
        key,
        L"",
        value.data(),
        static_cast<DWORD>(value.size()),
        manifest.c_str());
    return value.data();
}

std::filesystem::path makeTemporaryZip()
{
    std::array<wchar_t, MAX_PATH> directory{};
    std::array<wchar_t, MAX_PATH> file{};
    if (!GetTempPathW(static_cast<DWORD>(directory.size()), directory.data()) ||
        !GetTempFileNameW(directory.data(), L"vpg", 0, file.data()))
    {
        throw std::runtime_error("无法创建 GPU 下载临时文件");
    }
    std::filesystem::path original = file.data();
    std::filesystem::path zip = original;
    zip.replace_extension(L".zip");
    std::filesystem::rename(original, zip);
    return zip;
}

std::string calculateSha256(const std::filesystem::path &path)
{
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD returned = 0;
    const auto cleanupAlgorithm = [&]() {
        if (algorithm)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize),
            &returned,
            0) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize),
            sizeof(hashSize),
            &returned,
            0) < 0)
    {
        cleanupAlgorithm();
        throw std::runtime_error("无法初始化 SHA-256 校验");
    }

    std::vector<unsigned char> object(objectSize);
    std::vector<unsigned char> digest(hashSize);
    if (BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            objectSize,
            nullptr,
            0,
            0) < 0)
    {
        cleanupAlgorithm();
        throw std::runtime_error("无法创建 SHA-256 校验器");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        BCryptDestroyHash(hash);
        cleanupAlgorithm();
        throw std::runtime_error("无法读取 GPU 运行包");
    }
    std::array<unsigned char, 1024 * 1024> buffer{};
    while (input)
    {
        input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        const auto count = input.gcount();
        if (count > 0 && BCryptHashData(
                             hash,
                             buffer.data(),
                             static_cast<ULONG>(count),
                             0) < 0)
        {
            BCryptDestroyHash(hash);
            cleanupAlgorithm();
            throw std::runtime_error("GPU 运行包 SHA-256 计算失败");
        }
    }
    if (BCryptFinishHash(hash, digest.data(), hashSize, 0) < 0)
    {
        BCryptDestroyHash(hash);
        cleanupAlgorithm();
        throw std::runtime_error("GPU 运行包 SHA-256 计算失败");
    }
    BCryptDestroyHash(hash);
    cleanupAlgorithm();

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const unsigned char value : digest)
    {
        output << std::setw(2) << static_cast<int>(value);
    }
    return output.str();
}

void extractArchive(
    const std::filesystem::path &archive,
    const std::filesystem::path &runtimeRoot)
{
    std::array<wchar_t, MAX_PATH> systemDirectory{};
    if (!GetSystemDirectoryW(
            systemDirectory.data(),
            static_cast<UINT>(systemDirectory.size())))
    {
        throw std::runtime_error("无法定位 Windows 系统目录");
    }
    const std::filesystem::path tar =
        std::filesystem::path(systemDirectory.data()) / L"tar.exe";
    if (!std::filesystem::is_regular_file(tar))
    {
        throw std::runtime_error("当前 Windows 缺少 tar.exe，无法解压 GPU 组件");
    }
    std::filesystem::create_directories(runtimeRoot);
    std::wstring command = L"\"" + tar.wstring() + L"\" -xf \"" +
        archive.wstring() + L"\" -C \"" + runtimeRoot.wstring() + L"\"";
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            tar.c_str(),
            writable.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process))
    {
        throw std::runtime_error("无法启动 GPU 组件解压程序");
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (exitCode != 0)
    {
        throw std::runtime_error("GPU 组件解压失败");
    }
}

std::wstring widenUtf8(const std::string &value)
{
    const int length = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (length <= 0)
    {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length);
    return result;
}

} // namespace

bool gpuRuntimeIsInstalled(const std::filesystem::path &runtimeRoot)
{
    return std::all_of(
        RequiredGpuFiles.begin(),
        RequiredGpuFiles.end(),
        [&](const wchar_t *relative) {
            return std::filesystem::is_regular_file(runtimeRoot / relative);
        });
}

GpuRuntimeInstallResult installGpuRuntime(
    const std::filesystem::path &runtimeRoot,
    const std::function<void(const std::wstring &)> &statusCallback)
{
    std::filesystem::path temporaryArchive;
    try
    {
        if (gpuRuntimeIsInstalled(runtimeRoot))
        {
            return {true, L"GPU 运行组件已经安装。"};
        }
        const std::filesystem::path manifest = runtimeRoot / L"gpu_runtime.ini";
        if (!std::filesystem::is_regular_file(manifest))
        {
            return {false, L"缺少 gpu_runtime.ini，已保持 CPU 模式。"};
        }

        std::filesystem::path archive =
            runtimeRoot / L"video_privacy_gpu_runtime.zip";
        if (!std::filesystem::is_regular_file(archive))
        {
            const std::wstring url = readIniValue(manifest, L"Url");
            if (url.empty() || url.find(L"REPLACE_ME") != std::wstring::npos)
            {
                return {false, L"GPU 下载地址尚未配置，已保持 CPU 模式。"};
            }
            statusCallback(L"正在下载 GPU 运行组件，请保持网络连接…");
            temporaryArchive = makeTemporaryZip();
            const HRESULT result = URLDownloadToFileW(
                nullptr,
                url.c_str(),
                temporaryArchive.c_str(),
                0,
                nullptr);
            if (FAILED(result))
            {
                return {false, L"GPU 组件下载失败，已保持 CPU 模式。"};
            }
            archive = temporaryArchive;
        }

        statusCallback(L"正在校验 GPU 运行组件…");
        const std::wstring expectedWide = readIniValue(manifest, L"Sha256");
        std::string expected;
        expected.reserve(expectedWide.size());
        for (const wchar_t value : expectedWide)
        {
            if (value > 0x7f)
            {
                return {false, L"GPU 运行包校验值格式无效，已保持 CPU 模式。"};
            }
            expected.push_back(static_cast<char>(value));
        }
        std::transform(
            expected.begin(),
            expected.end(),
            expected.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (expected.empty() || calculateSha256(archive) != expected)
        {
            return {false, L"GPU 运行包校验失败，已保持 CPU 模式。"};
        }

        statusCallback(L"正在解压 GPU 运行组件…");
        extractArchive(archive, runtimeRoot);
        if (!gpuRuntimeIsInstalled(runtimeRoot))
        {
            return {false, L"GPU 运行包内容不完整，已保持 CPU 模式。"};
        }
        std::error_code ignored;
        if (!temporaryArchive.empty())
        {
            std::filesystem::remove(temporaryArchive, ignored);
        }
        return {true, L"GPU 运行组件安装完成，已切换为 GPU 模式。"};
    }
    catch (const std::exception &error)
    {
        std::error_code ignored;
        if (!temporaryArchive.empty())
        {
            std::filesystem::remove(temporaryArchive, ignored);
        }
        return {false, L"GPU 组件安装失败：" + widenUtf8(error.what()) + L"。已保持 CPU 模式。"};
    }
}

} // namespace video_privacy

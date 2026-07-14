#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <urlmon.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr wchar_t WindowClassName[] = L"VideoPrivacySetupWindow";
constexpr wchar_t WindowTitle[] = L"视频隐私处理工具安装程序";
constexpr char PayloadMagic[8] = {'V', 'P', 'S', 'E', 'T', 'U', 'P', '1'};
constexpr UINT InstallFinishedMessage = WM_APP + 1;

enum ControlId
{
    IdPath = 101,
    IdBrowse,
    IdGpu,
    IdInstall,
    IdProgress,
    IdStatus
};

#pragma pack(push, 1)
struct PayloadFooter
{
    char magic[8];
    std::uint64_t payloadSize;
};
#pragma pack(pop)

struct InstallerState
{
    HWND window{};
    HWND path{};
    HWND gpu{};
    HWND install{};
    HWND progress{};
    HWND status{};
    HFONT font{};
    std::filesystem::path executable;
};

struct InstallRequest
{
    HWND window{};
    std::filesystem::path installer;
    std::filesystem::path destination;
    bool installGpu{};
};

InstallerState state;

std::wstring errorMessage(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t *>(&buffer),
        0,
        nullptr);
    std::wstring message = length > 0 ? std::wstring(buffer, length) : L"未知错误";
    if (buffer)
    {
        LocalFree(buffer);
    }
    return message;
}

void postStatus(HWND window, const std::wstring &message)
{
    auto copy = std::make_unique<std::wstring>(message);
    PostMessageW(window, WM_APP + 2, 0, reinterpret_cast<LPARAM>(copy.release()));
}

std::filesystem::path temporaryZipPath()
{
    std::array<wchar_t, MAX_PATH> tempDirectory{};
    std::array<wchar_t, MAX_PATH> tempFile{};
    if (!GetTempPathW(static_cast<DWORD>(tempDirectory.size()), tempDirectory.data()) ||
        !GetTempFileNameW(tempDirectory.data(), L"vps", 0, tempFile.data()))
    {
        throw std::runtime_error("无法创建临时文件");
    }
    std::filesystem::path result = tempFile.data();
    std::filesystem::path zip = result;
    zip.replace_extension(L".zip");
    std::filesystem::rename(result, zip);
    return zip;
}

std::filesystem::path extractEmbeddedPayload(
    const std::filesystem::path &installer,
    HWND window)
{
    postStatus(window, L"正在读取基础安装包…");
    std::ifstream input(installer, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("无法读取安装程序自身");
    }
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(sizeof(PayloadFooter)))
    {
        throw std::runtime_error("安装包不完整");
    }
    input.seekg(fileSize - static_cast<std::streamoff>(sizeof(PayloadFooter)));
    PayloadFooter footer{};
    input.read(reinterpret_cast<char *>(&footer), sizeof(footer));
    if (!input || !std::equal(
                      std::begin(footer.magic),
                      std::end(footer.magic),
                      std::begin(PayloadMagic)))
    {
        throw std::runtime_error("没有找到基础包数据");
    }
    const std::uint64_t footerSize = sizeof(PayloadFooter);
    if (footer.payloadSize > static_cast<std::uint64_t>(fileSize) - footerSize)
    {
        throw std::runtime_error("基础包长度无效");
    }
    const std::streamoff payloadOffset =
        fileSize - static_cast<std::streamoff>(footerSize + footer.payloadSize);
    input.seekg(payloadOffset);

    const std::filesystem::path zip = temporaryZipPath();
    std::ofstream output(zip, std::ios::binary | std::ios::trunc);
    std::array<char, 1024 * 1024> buffer{};
    std::uint64_t remaining = footer.payloadSize;
    while (remaining > 0)
    {
        const auto count = static_cast<std::streamsize>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        input.read(buffer.data(), count);
        if (input.gcount() != count)
        {
            throw std::runtime_error("基础包数据被截断");
        }
        output.write(buffer.data(), count);
        remaining -= static_cast<std::uint64_t>(count);
    }
    if (!output)
    {
        throw std::runtime_error("无法写入临时基础包");
    }
    return zip;
}

void runTarExtract(
    const std::filesystem::path &archive,
    const std::filesystem::path &destination)
{
    std::filesystem::create_directories(destination);
    std::array<wchar_t, MAX_PATH> systemDirectory{};
    GetSystemDirectoryW(systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
    const std::filesystem::path tar =
        std::filesystem::path(systemDirectory.data()) / L"tar.exe";
    if (!std::filesystem::is_regular_file(tar))
    {
        throw std::runtime_error("当前 Windows 缺少 tar.exe，无法解压安装包");
    }

    std::wstring command = L"\"" + tar.wstring() + L"\" -xf \"" +
        archive.wstring() + L"\" -C \"" + destination.wstring() + L"\"";
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
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
        throw std::runtime_error("无法启动解压程序");
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (exitCode != 0)
    {
        throw std::runtime_error("解压程序返回错误");
    }
}

std::string sha256(const std::filesystem::path &path)
{
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD resultSize = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize),
            &resultSize,
            0) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize),
            sizeof(hashSize),
            &resultSize,
            0) < 0)
    {
        if (algorithm)
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        throw std::runtime_error("无法初始化 SHA-256");
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
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("无法创建 SHA-256 计算器");
    }

    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 1024 * 1024> buffer{};
    while (input)
    {
        input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        const std::streamsize count = input.gcount();
        if (count > 0 && BCryptHashData(
                             hash,
                             buffer.data(),
                             static_cast<ULONG>(count),
                             0) < 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("SHA-256 计算失败");
        }
    }
    if (BCryptFinishHash(hash, digest.data(), hashSize, 0) < 0)
    {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("SHA-256 计算失败");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream value;
    value << std::hex << std::setfill('0');
    for (const unsigned char byte : digest)
    {
        value << std::setw(2) << static_cast<int>(byte);
    }
    return value.str();
}

std::wstring readIniValue(
    const std::filesystem::path &ini,
    const wchar_t *key)
{
    std::array<wchar_t, 4096> value{};
    GetPrivateProfileStringW(
        L"GPU",
        key,
        L"",
        value.data(),
        static_cast<DWORD>(value.size()),
        ini.c_str());
    return value.data();
}

std::filesystem::path acquireGpuArchive(
    const InstallRequest &request,
    const std::filesystem::path &manifest,
    bool &temporary)
{
    const std::filesystem::path adjacent =
        request.installer.parent_path() / L"video_privacy_gpu_runtime.zip";
    if (std::filesystem::is_regular_file(adjacent))
    {
        temporary = false;
        postStatus(request.window, L"正在校验安装器旁的 GPU 运行包…");
        return adjacent;
    }

    const std::wstring url = readIniValue(manifest, L"Url");
    if (url.empty() || url.find(L"REPLACE_ME") != std::wstring::npos)
    {
        throw std::runtime_error("GPU 下载地址尚未配置，CPU 版本已正常安装");
    }
    const std::filesystem::path target = temporaryZipPath();
    temporary = true;
    postStatus(request.window, L"正在下载 GPU 运行组件，请保持网络连接…");
    const HRESULT result = URLDownloadToFileW(
        nullptr,
        url.c_str(),
        target.c_str(),
        0,
        nullptr);
    if (FAILED(result))
    {
        throw std::runtime_error("GPU 组件下载失败，CPU 版本仍可使用");
    }
    return target;
}

void installWorker(InstallRequest request)
{
    std::filesystem::path baseZip;
    std::filesystem::path gpuZip;
    bool gpuZipIsTemporary = false;
    try
    {
        baseZip = extractEmbeddedPayload(request.installer, request.window);
        postStatus(request.window, L"正在安装 CPU 基础组件…");
        runTarExtract(baseZip, request.destination);
        std::filesystem::create_directories(request.destination / L"output");

        std::wstring completion = L"CPU 基础版本安装完成。";
        if (request.installGpu)
        {
            const std::filesystem::path manifest =
                request.destination / L"gpu_runtime.ini";
            gpuZip = acquireGpuArchive(
                request,
                manifest,
                gpuZipIsTemporary);
            const std::wstring expectedWide = readIniValue(manifest, L"Sha256");
            std::string expected;
            expected.reserve(expectedWide.size());
            for (const wchar_t value : expectedWide)
            {
                if (value > 0x7f)
                {
                    throw std::runtime_error("GPU SHA-256 配置格式无效");
                }
                expected.push_back(static_cast<char>(value));
            }
            std::transform(
                expected.begin(),
                expected.end(),
                expected.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            if (expected.empty() || sha256(gpuZip) != expected)
            {
                throw std::runtime_error("GPU 运行包 SHA-256 校验失败，已停止安装 GPU 组件");
            }
            postStatus(request.window, L"正在安装 GPU 运行组件…");
            runTarExtract(gpuZip, request.destination);
            completion = L"CPU 与 GPU 组件均已安装完成。";
        }
        auto result = std::make_unique<std::wstring>(completion);
        PostMessageW(
            request.window,
            InstallFinishedMessage,
            TRUE,
            reinterpret_cast<LPARAM>(result.release()));
    }
    catch (const std::exception &error)
    {
        const std::string narrow = error.what();
        auto result = std::make_unique<std::wstring>(narrow.begin(), narrow.end());
        PostMessageW(
            request.window,
            InstallFinishedMessage,
            FALSE,
            reinterpret_cast<LPARAM>(result.release()));
    }
    std::error_code ignored;
    if (!baseZip.empty())
    {
        std::filesystem::remove(baseZip, ignored);
    }
    if (gpuZipIsTemporary && !gpuZip.empty())
    {
        std::filesystem::remove(gpuZip, ignored);
    }
}

std::filesystem::path defaultInstallDirectory()
{
    std::array<wchar_t, MAX_PATH> path{};
    if (SUCCEEDED(SHGetFolderPathW(
            nullptr,
            CSIDL_LOCAL_APPDATA,
            nullptr,
            SHGFP_TYPE_CURRENT,
            path.data())))
    {
        return std::filesystem::path(path.data()) / L"VideoPrivacyPlatform";
    }
    return std::filesystem::current_path() / L"VideoPrivacyPlatform";
}

void setFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
}

HWND createControl(
    DWORD extendedStyle,
    const wchar_t *className,
    const wchar_t *text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    int id)
{
    HWND control = CreateWindowExW(
        extendedStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    setFont(control);
    return control;
}

void chooseDirectory()
{
    BROWSEINFOW info{};
    info.hwndOwner = state.window;
    info.lpszTitle = L"选择安装目录";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (!item)
    {
        return;
    }
    std::array<wchar_t, MAX_PATH> path{};
    if (SHGetPathFromIDListW(item, path.data()))
    {
        SetWindowTextW(state.path, path.data());
    }
    CoTaskMemFree(item);
}

void startInstall()
{
    const int length = GetWindowTextLengthW(state.path);
    std::wstring destination(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(state.path, destination.data(), length + 1);
    destination.resize(static_cast<std::size_t>(length));
    if (destination.empty())
    {
        MessageBoxW(state.window, L"请选择安装目录。", WindowTitle, MB_ICONWARNING);
        return;
    }
    EnableWindow(state.install, FALSE);
    EnableWindow(state.path, FALSE);
    EnableWindow(GetDlgItem(state.window, IdBrowse), FALSE);
    EnableWindow(state.gpu, FALSE);
    SendMessageW(state.progress, PBM_SETMARQUEE, TRUE, 35);
    SetWindowTextW(state.status, L"正在准备安装…");

    InstallRequest request;
    request.window = state.window;
    request.installer = state.executable;
    request.destination = destination;
    request.installGpu =
        SendMessageW(state.gpu, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::thread(installWorker, std::move(request)).detach();
}

LRESULT CALLBACK windowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        state.window = window;
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        state.font = CreateFontIndirectW(&metrics.lfMessageFont);
        createControl(0, L"STATIC", L"安装位置", 0, 24, 24, 100, 24, 0);
        state.path = createControl(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            defaultInstallDirectory().c_str(),
            ES_AUTOHSCROLL,
            24,
            50,
            500,
            30,
            IdPath);
        createControl(0, L"BUTTON", L"浏览…", BS_PUSHBUTTON, 534, 50, 86, 30, IdBrowse);
        state.gpu = createControl(
            0,
            L"BUTTON",
            L"安装 GPU 加速组件（约 2.2 GB，可稍后安装）",
            BS_AUTOCHECKBOX,
            24,
            98,
            520,
            28,
            IdGpu);
        createControl(
            0,
            L"STATIC",
            L"不勾选时安装 CPU 基础版；不会修改系统 PATH、CUDA 或 Python 环境。",
            0,
            24,
            130,
            596,
            42,
            0);
        state.progress = createControl(
            0,
            PROGRESS_CLASSW,
            L"",
            PBS_MARQUEE,
            24,
            178,
            596,
            20,
            IdProgress);
        state.status = createControl(
            0,
            L"STATIC",
            L"准备就绪",
            0,
            24,
            208,
            596,
            28,
            IdStatus);
        state.install = createControl(
            0,
            L"BUTTON",
            L"开始安装",
            BS_DEFPUSHBUTTON,
            490,
            252,
            130,
            36,
            IdInstall);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IdBrowse)
        {
            chooseDirectory();
        }
        else if (LOWORD(wParam) == IdInstall)
        {
            startInstall();
        }
        break;
    case WM_APP + 2:
    {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring *>(lParam));
        SetWindowTextW(state.status, text->c_str());
        break;
    }
    case InstallFinishedMessage:
    {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring *>(lParam));
        SendMessageW(state.progress, PBM_SETMARQUEE, FALSE, 0);
        SetWindowTextW(state.status, text->c_str());
        if (wParam)
        {
            MessageBoxW(window, text->c_str(), WindowTitle, MB_OK | MB_ICONINFORMATION);
            std::array<wchar_t, 32768> destination{};
            GetWindowTextW(state.path, destination.data(), static_cast<int>(destination.size()));
            const std::filesystem::path gui =
                std::filesystem::path(destination.data()) / L"video_privacy_gui.exe";
            ShellExecuteW(window, L"open", gui.c_str(), nullptr, gui.parent_path().c_str(), SW_SHOWNORMAL);
            DestroyWindow(window);
        }
        else
        {
            MessageBoxW(window, text->c_str(), WindowTitle, MB_OK | MB_ICONERROR);
            EnableWindow(state.install, TRUE);
            EnableWindow(state.path, TRUE);
            EnableWindow(GetDlgItem(window, IdBrowse), TRUE);
            EnableWindow(state.gpu, TRUE);
        }
        break;
    }
    case WM_DESTROY:
        if (state.font)
        {
            DeleteObject(state.font);
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::array<wchar_t, 32768> executable{};
    GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    state.executable = executable.data();

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = WindowClassName;
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(
        0,
        WindowClassName,
        WindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        660,
        340,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window)
    {
        CoUninitialize();
        return 1;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "video_privacy/face_detector.hpp"
#include "video_privacy/gpu_runtime_installer.hpp"
#include "video_privacy/media_processor.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr wchar_t WindowClassName[] = L"VideoPrivacyGuiWindow";
constexpr wchar_t SettingsClassName[] = L"VideoPrivacySettingsWindow";
constexpr UINT MessageProgress = WM_APP + 1;
constexpr UINT MessageTaskStatus = WM_APP + 2;
constexpr UINT MessageFinished = WM_APP + 3;
constexpr UINT MessageGpuInstallStatus = WM_APP + 4;
constexpr UINT MessageGpuInstallFinished = WM_APP + 5;

enum ControlId
{
    IdTaskList = 100,
    IdAddFiles,
    IdRemove,
    IdClear,
    IdSettings,
    IdOutputEdit,
    IdOutputBrowse,
    IdFaceCheck,
    IdFaceModelEdit,
    IdFaceModelBrowse,
    IdPlateCheck,
    IdPlateModelEdit,
    IdPlateModelBrowse,
    IdFfmpegEdit,
    IdFfmpegBrowse,
    IdGpuRadio,
    IdCpuRadio,
    IdAudioCheck,
    IdMosaicSlider,
    IdMosaicValue,
    IdSettingsSave,
    IdSettingsCancel,
    IdCurrentProgress,
    IdOverallProgress,
    IdStatusText,
    IdElapsedText,
    IdRemainingText,
    IdStart,
    IdCancel
};

struct MediaTask
{
    std::filesystem::path input;
    std::filesystem::path output;
    bool isVideo{};
};

struct ProgressPayload
{
    int row{};
    int currentPercent{};
    int overallPercent{};
    std::wstring status;
    std::wstring elapsed;
    std::wstring remaining;
};

struct StatusPayload
{
    int row{};
    std::wstring status;
};

struct FinishedPayload
{
    bool cancelled{};
    int succeeded{};
    int failed{};
    std::wstring message;
};

struct AppState
{
    HWND window{};
    HWND taskList{};
    HWND settingsWindow{};
    HWND settingsButton{};
    HWND currentProgress{};
    HWND overallProgress{};
    HWND statusText{};
    HWND elapsedText{};
    HWND remainingText{};
    HWND startButton{};
    HWND cancelButton{};
    HFONT normalFont{};
    HFONT titleFont{};
    HBRUSH backgroundBrush{};
    std::filesystem::path runtimeRoot;
    std::filesystem::path outputDirectory{L"output"};
    std::filesystem::path faceModel{L"models/face/face_yolo11s.onnx"};
    std::filesystem::path plateModel{L"models/license_plate/license_plate.onnx"};
    std::filesystem::path ffmpegExecutable{L"tools/ffmpeg.exe"};
    bool faceEnabled{true};
    bool plateEnabled{false};
    bool useGpu{false};
    bool preserveAudio{true};
    int mosaicStrength{60};
    std::vector<MediaTask> tasks;
    std::thread worker;
    std::thread gpuInstaller;
    std::atomic_bool cancelRequested{false};
    bool processing{};
    bool gpuInstalling{};
};

AppState state;

std::wstring widen(const std::string &value)
{
    if (value.empty())
    {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
    {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring getWindowText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0)
    {
        GetWindowTextW(control, value.data(), length + 1);
    }
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void setFont(HWND control, HFONT font = state.normalFont)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
        style | WS_CHILD | WS_VISIBLE,
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

std::filesystem::path executablePath()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return buffer;
}

std::filesystem::path findProjectRoot()
{
    std::vector<std::filesystem::path> candidates{
        std::filesystem::current_path(),
        executablePath().parent_path()};
    for (std::filesystem::path candidate : candidates)
    {
        while (!candidate.empty())
        {
            if (std::filesystem::is_directory(candidate / "models") ||
                std::filesystem::is_directory(candidate / "cpp"))
            {
                return candidate;
            }
            const auto parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }
            candidate = parent;
        }
    }
    return std::filesystem::current_path();
}

std::filesystem::path resolveRuntimePath(const std::filesystem::path &path)
{
    return path.is_absolute() ? path : state.runtimeRoot / path;
}

std::filesystem::path displayRuntimePath(const std::filesystem::path &path)
{
    std::error_code error;
    const auto relative = std::filesystem::relative(path, state.runtimeRoot, error);
    if (!error && !relative.empty() && *relative.begin() != L"..")
    {
        return relative;
    }
    return path;
}

std::filesystem::path resolveExecutable(const std::filesystem::path &path)
{
    if (path.empty())
    {
        return {};
    }
    const auto local = resolveRuntimePath(path);
    if (std::filesystem::is_regular_file(local))
    {
        return local;
    }
    if (!path.has_parent_path())
    {
        std::wstring found(MAX_PATH, L'\0');
        const DWORD length = SearchPathW(
            nullptr,
            path.c_str(),
            nullptr,
            static_cast<DWORD>(found.size()),
            found.data(),
            nullptr);
        if (length > 0 && length < found.size())
        {
            found.resize(length);
            return found;
        }
    }
    return {};
}

std::filesystem::path findFfmpeg()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = SearchPathW(
        nullptr, L"ffmpeg.exe", nullptr, static_cast<DWORD>(path.size()), path.data(), nullptr);
    if (length > 0 && length < path.size())
    {
        return L"ffmpeg.exe";
    }
    if (std::filesystem::is_regular_file(state.runtimeRoot / "ffmpeg.exe"))
    {
        return L"ffmpeg.exe";
    }
    const auto bundled = state.runtimeRoot / "tools/ffmpeg.exe";
    return std::filesystem::is_regular_file(bundled)
        ? std::filesystem::path(L"tools/ffmpeg.exe")
        : std::filesystem::path{};
}

std::wstring formatDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        return L"--:--";
    }
    const int total = static_cast<int>(seconds + 0.5);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int remainingSeconds = total % 60;
    wchar_t buffer[32]{};
    if (hours > 0)
    {
        swprintf_s(buffer, L"%02d:%02d:%02d", hours, minutes, remainingSeconds);
    }
    else
    {
        swprintf_s(buffer, L"%02d:%02d", minutes, remainingSeconds);
    }
    return buffer;
}

std::filesystem::path makeOutputPath(
    const std::filesystem::path &input,
    bool isVideo,
    const std::filesystem::path &outputDirectory,
    std::size_t taskIndex)
{
    const std::wstring extension = isVideo ? L".mp4" : input.extension().wstring();
    std::filesystem::path candidate =
        outputDirectory / (input.stem().wstring() + L"_privacy" + extension);
    for (std::size_t suffix = 1; suffix <= taskIndex + 1; ++suffix)
    {
        const bool duplicatesExisting = std::any_of(
            state.tasks.begin(),
            state.tasks.end(),
            [&](const MediaTask &task) { return task.output == candidate; });
        if (!duplicatesExisting)
        {
            break;
        }
        candidate = outputDirectory /
                    (input.stem().wstring() + L"_privacy_" +
                     std::to_wstring(suffix) + extension);
    }
    return candidate;
}

void setTaskCell(int row, int column, const std::wstring &text)
{
    ListView_SetItemText(
        state.taskList,
        row,
        column,
        const_cast<wchar_t *>(text.c_str()));
}

void refreshTaskOutputs()
{
    const std::filesystem::path outputDirectory =
        resolveRuntimePath(state.outputDirectory);
    std::vector<std::filesystem::path> assigned;
    for (std::size_t index = 0; index < state.tasks.size(); ++index)
    {
        auto &task = state.tasks[index];
        std::wstring extension = task.isVideo ? L".mp4" : task.input.extension().wstring();
        task.output = outputDirectory /
                      (task.input.stem().wstring() + L"_privacy" + extension);
        int suffix = 1;
        while (std::find(assigned.begin(), assigned.end(), task.output) != assigned.end())
        {
            task.output = outputDirectory /
                          (task.input.stem().wstring() + L"_privacy_" +
                           std::to_wstring(suffix++) + extension);
        }
        assigned.push_back(task.output);
        setTaskCell(
            static_cast<int>(index),
            2,
            displayRuntimePath(task.output).wstring());
    }
}

void addMediaFile(const std::filesystem::path &path)
{
    if (!std::filesystem::is_regular_file(path))
    {
        return;
    }
    const bool image = video_privacy::MediaProcessor::isSupportedImage(path);
    const bool video = video_privacy::MediaProcessor::isSupportedVideo(path);
    if (!image && !video)
    {
        return;
    }
    if (std::any_of(
            state.tasks.begin(), state.tasks.end(),
            [&](const MediaTask &task) { return task.input == path; }))
    {
        return;
    }

    const auto outputDirectory = resolveRuntimePath(state.outputDirectory);
    MediaTask task{path, {}, video};
    task.output = makeOutputPath(path, video, outputDirectory, state.tasks.size());
    const int row = static_cast<int>(state.tasks.size());
    state.tasks.push_back(task);

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.pszText = const_cast<wchar_t *>(task.input.wstring().c_str());
    ListView_InsertItem(state.taskList, &item);
    setTaskCell(row, 1, video ? L"视频" : L"图片");
    setTaskCell(row, 2, displayRuntimePath(task.output).wstring());
    setTaskCell(row, 3, L"等待处理");
}

std::filesystem::path browseForFolder(HWND owner)
{
    IFileDialog *dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))))
    {
        return {};
    }
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    std::filesystem::path selected;
    if (SUCCEEDED(dialog->Show(owner)))
    {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                selected = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

std::filesystem::path browseForSingleFile(
    HWND owner,
    const wchar_t *filter,
    const wchar_t *title)
{
    std::wstring buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog))
    {
        return {};
    }
    return buffer.c_str();
}

void browseForMediaFiles(HWND owner)
{
    std::vector<wchar_t> buffer(65536, L'\0');
    const wchar_t filter[] =
        L"媒体文件\0*.mp4;*.avi;*.mov;*.mkv;*.webm;*.m4v;*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.tif;*.tiff\0"
        L"所有文件\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER |
                   OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog))
    {
        return;
    }

    const std::filesystem::path first = buffer.data();
    const wchar_t *cursor = buffer.data() + first.wstring().size() + 1;
    if (*cursor == L'\0')
    {
        addMediaFile(first);
        return;
    }
    while (*cursor != L'\0')
    {
        addMediaFile(first / cursor);
        cursor += wcslen(cursor) + 1;
    }
}

void enableInputs(bool enabled)
{
    const int ids[] = {
        IdAddFiles, IdRemove, IdClear, IdSettings};
    for (const int id : ids)
    {
        EnableWindow(GetDlgItem(state.window, id), enabled);
    }
    EnableWindow(state.startButton, enabled);
    EnableWindow(state.cancelButton, !enabled);
}

void startGpuRuntimeInstall()
{
    if (state.gpuInstalling)
    {
        return;
    }
    if (state.gpuInstaller.joinable())
    {
        state.gpuInstaller.join();
    }
    state.useGpu = false;
    state.gpuInstalling = true;
    enableInputs(false);
    EnableWindow(state.cancelButton, FALSE);
    SetWindowTextW(state.statusText, L"正在准备 GPU 运行组件…");
    const HWND window = state.window;
    const std::filesystem::path runtimeRoot = state.runtimeRoot;
    state.gpuInstaller = std::thread([window, runtimeRoot]() {
        auto statusCallback = [window](const std::wstring &message) {
            auto payload = std::make_unique<std::wstring>(message);
            PostMessageW(
                window,
                MessageGpuInstallStatus,
                0,
                reinterpret_cast<LPARAM>(payload.release()));
        };
        auto result = std::make_unique<video_privacy::GpuRuntimeInstallResult>(
            video_privacy::installGpuRuntime(runtimeRoot, statusCallback));
        PostMessageW(
            window,
            MessageGpuInstallFinished,
            0,
            reinterpret_cast<LPARAM>(result.release()));
    });
}

bool validateSettings()
{
    if (state.tasks.empty())
    {
        MessageBoxW(state.window, L"请先拖入或添加图片、视频。", L"没有任务", MB_ICONINFORMATION);
        return false;
    }
    if (!state.faceEnabled && !state.plateEnabled)
    {
        MessageBoxW(state.window, L"请至少启用人脸或车牌中的一项。", L"未启用检测", MB_ICONWARNING);
        return false;
    }
    if (state.faceEnabled &&
        !std::filesystem::is_regular_file(resolveRuntimePath(state.faceModel)))
    {
        MessageBoxW(state.window, L"请选择有效的人脸 ONNX 模型。", L"模型缺失", MB_ICONWARNING);
        return false;
    }
    if (state.plateEnabled &&
        !std::filesystem::is_regular_file(resolveRuntimePath(state.plateModel)))
    {
        MessageBoxW(state.window, L"请选择有效的车牌 ONNX 模型。", L"模型缺失", MB_ICONWARNING);
        return false;
    }
    const bool containsVideo = std::any_of(
        state.tasks.begin(), state.tasks.end(),
        [](const MediaTask &task) { return task.isVideo; });
    if (containsVideo && state.preserveAudio &&
        resolveExecutable(state.ffmpegExecutable).empty())
    {
        MessageBoxW(
            state.window,
            L"当前设置要求保留音频，但内置 FFmpeg 不可用。请在设置中重新选择。",
            L"FFmpeg 缺失",
            MB_ICONWARNING);
        return false;
    }
    return true;
}

void startProcessing()
{
    if (state.processing || !validateSettings())
    {
        return;
    }
    if (state.worker.joinable())
    {
        state.worker.join();
    }
    refreshTaskOutputs();
    state.processing = true;
    state.cancelRequested.store(false);
    enableInputs(false);
    SendMessageW(state.currentProgress, PBM_SETPOS, 0, 0);
    SendMessageW(state.overallProgress, PBM_SETPOS, 0, 0);
    SetWindowTextW(state.statusText, L"正在初始化检测模型…");

    const auto tasks = state.tasks;
    const bool faceEnabled = state.faceEnabled;
    const bool plateEnabled = state.plateEnabled;
    const bool useGpu = state.useGpu;
    const bool preserveAudio = state.preserveAudio;
    const int mosaicStrength = state.mosaicStrength;
    const std::filesystem::path faceModel = resolveRuntimePath(state.faceModel);
    const std::filesystem::path plateModel = resolveRuntimePath(state.plateModel);
    const std::filesystem::path ffmpeg =
        preserveAudio ? resolveExecutable(state.ffmpegExecutable) : std::filesystem::path{};
    const HWND window = state.window;

    state.worker = std::thread([=]() {
        auto finished = std::make_unique<FinishedPayload>();
        const auto batchStart = std::chrono::steady_clock::now();
        try
        {
            video_privacy::DetectorConfig detectorConfig;
            detectorConfig.useCuda = useGpu;
            std::unique_ptr<video_privacy::FaceDetector> faceDetector;
            std::unique_ptr<video_privacy::FaceDetector> plateDetector;
            video_privacy::MediaProcessingOptions options;
            options.mosaicStrength = mosaicStrength;
            options.preserveAudio = preserveAudio;
            options.ffmpegExecutable = ffmpeg;
            if (faceEnabled)
            {
                faceDetector = std::make_unique<video_privacy::FaceDetector>(
                    faceModel, detectorConfig);
                if (useGpu && faceDetector->executionProvider() != "CUDAExecutionProvider")
                {
                    throw std::runtime_error("GPU 初始化失败，请检查 CUDA/cuDNN，或在设置中选择 CPU");
                }
                options.detectors.push_back(faceDetector.get());
            }
            if (plateEnabled)
            {
                plateDetector = std::make_unique<video_privacy::FaceDetector>(
                    plateModel, detectorConfig);
                if (useGpu && plateDetector->executionProvider() != "CUDAExecutionProvider")
                {
                    throw std::runtime_error("车牌模型未能使用 GPU，请检查 CUDA/cuDNN");
                }
                options.detectors.push_back(plateDetector.get());
            }

            video_privacy::MediaProcessor processor;
            for (std::size_t index = 0; index < tasks.size(); ++index)
            {
                if (state.cancelRequested.load())
                {
                    finished->cancelled = true;
                    break;
                }
                auto status = std::make_unique<StatusPayload>();
                status->row = static_cast<int>(index);
                status->status = L"处理中";
                PostMessageW(window, MessageTaskStatus, 0, reinterpret_cast<LPARAM>(status.release()));

                try
                {
                    processor.process(
                        tasks[index].input,
                        tasks[index].output,
                        options,
                        [=](const video_privacy::MediaProgress &progress) {
                            const double currentFraction = progress.totalUnits > 0
                                ? static_cast<double>(progress.completedUnits) / progress.totalUnits
                                : 0.0;
                            const double overallFraction =
                                (static_cast<double>(index) + currentFraction) / tasks.size();
                            const double batchElapsed = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - batchStart).count();
                            const double batchRemaining = overallFraction > 0.0
                                ? batchElapsed * (1.0 - overallFraction) / overallFraction
                                : 0.0;
                            auto payload = std::make_unique<ProgressPayload>();
                            payload->row = static_cast<int>(index);
                            payload->currentPercent = static_cast<int>(currentFraction * 100.0);
                            payload->overallPercent = static_cast<int>(overallFraction * 100.0);
                            payload->status = L"正在处理：" + tasks[index].input.filename().wstring();
                            payload->elapsed = L"已耗时  " + formatDuration(batchElapsed);
                            payload->remaining = L"预计剩余  " + formatDuration(batchRemaining);
                            PostMessageW(
                                window,
                                MessageProgress,
                                0,
                                reinterpret_cast<LPARAM>(payload.release()));
                        },
                        &state.cancelRequested);
                    ++finished->succeeded;
                    auto done = std::make_unique<StatusPayload>();
                    done->row = static_cast<int>(index);
                    done->status = L"已完成";
                    PostMessageW(window, MessageTaskStatus, 0, reinterpret_cast<LPARAM>(done.release()));
                }
                catch (const std::exception &error)
                {
                    if (state.cancelRequested.load())
                    {
                        finished->cancelled = true;
                        break;
                    }
                    ++finished->failed;
                    auto failed = std::make_unique<StatusPayload>();
                    failed->row = static_cast<int>(index);
                    failed->status = L"失败：" + widen(error.what());
                    PostMessageW(window, MessageTaskStatus, 0, reinterpret_cast<LPARAM>(failed.release()));
                }
            }
        }
        catch (const std::exception &error)
        {
            finished->message = widen(error.what());
            finished->failed = static_cast<int>(tasks.size());
        }
        PostMessageW(window, MessageFinished, 0, reinterpret_cast<LPARAM>(finished.release()));
    });
}

void initializeTaskList()
{
    ListView_SetExtendedListViewStyle(
        state.taskList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    struct Column
    {
        const wchar_t *name;
        int width;
    };
    const Column columns[] = {
        {L"输入文件（可直接拖入）", 330},
        {L"类型", 70},
        {L"输出路径", 360},
        {L"状态", 150}};
    for (int index = 0; index < 4; ++index)
    {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t *>(columns[index].name);
        column.cx = columns[index].width;
        column.iSubItem = index;
        ListView_InsertColumn(state.taskList, index, &column);
    }
}

void createUi(HWND window)
{
    state.window = window;
    state.runtimeRoot = findProjectRoot();
    state.backgroundBrush = CreateSolidBrush(RGB(246, 248, 251));
    state.normalFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    state.titleFont = CreateFontW(
        -26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    HWND title = createControl(0, L"STATIC", L"VideoPrivacy", SS_LEFT, 24, 16, 420, 34, 0);
    setFont(title, state.titleFont);
    createControl(
        0, L"STATIC",
        L"拖入媒体后按回车即可开始；输出、模型、设备和音频选项在设置中调整",
        SS_LEFT, 26, 51, 700, 24, 0);
    state.settingsButton = createControl(
        0, L"BUTTON", L"设置", BS_PUSHBUTTON,
        852, 24, 112, 36, IdSettings);

    state.taskList = createControl(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        LVS_REPORT | LVS_SHOWSELALWAYS,
        24, 82, 940, 390,
        IdTaskList);
    initializeTaskList();

    createControl(0, L"BUTTON", L"添加文件", BS_PUSHBUTTON, 24, 484, 104, 34, IdAddFiles);
    createControl(0, L"BUTTON", L"移除选中", BS_PUSHBUTTON, 138, 484, 104, 34, IdRemove);
    createControl(0, L"BUTTON", L"清空列表", BS_PUSHBUTTON, 252, 484, 104, 34, IdClear);
    createControl(0, L"STATIC", L"也可以直接从资源管理器拖入多个文件", SS_LEFT, 380, 492, 340, 24, 0);

    state.statusText = createControl(0, L"STATIC", L"等待添加任务", SS_LEFT, 24, 536, 600, 24, IdStatusText);
    state.elapsedText = createControl(0, L"STATIC", L"已耗时  00:00", SS_LEFT, 650, 536, 140, 24, IdElapsedText);
    state.remainingText = createControl(0, L"STATIC", L"预计剩余  --:--", SS_RIGHT, 800, 536, 164, 24, IdRemainingText);
    createControl(0, L"STATIC", L"当前文件", SS_LEFT, 24, 568, 74, 22, 0);
    state.currentProgress = createControl(
        0, PROGRESS_CLASSW, L"", PBS_SMOOTH,
        100, 566, 864, 20, IdCurrentProgress);
    createControl(0, L"STATIC", L"总体进度", SS_LEFT, 24, 600, 74, 22, 0);
    state.overallProgress = createControl(
        0, PROGRESS_CLASSW, L"", PBS_SMOOTH,
        100, 598, 624, 20, IdOverallProgress);
    state.startButton = createControl(0, L"BUTTON", L"开始处理", BS_DEFPUSHBUTTON, 744, 590, 104, 38, IdStart);
    state.cancelButton = createControl(0, L"BUTTON", L"取消", BS_PUSHBUTTON, 860, 590, 104, 38, IdCancel);
    EnableWindow(state.cancelButton, FALSE);

    const auto ffmpeg = findFfmpeg();
    if (!ffmpeg.empty())
    {
        state.ffmpegExecutable = ffmpeg;
    }
    SendMessageW(state.currentProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(state.overallProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    DragAcceptFiles(window, TRUE);
}

void handleDrop(HDROP drop)
{
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT index = 0; index < count; ++index)
    {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, index, path.data(), length + 1);
        path.resize(static_cast<std::size_t>(length));
        addMediaFile(path);
    }
    DragFinish(drop);
}

void removeSelectedTasks()
{
    int index = ListView_GetNextItem(state.taskList, -1, LVNI_SELECTED);
    while (index >= 0)
    {
        state.tasks.erase(state.tasks.begin() + index);
        ListView_DeleteItem(state.taskList, index);
        index = ListView_GetNextItem(state.taskList, -1, LVNI_SELECTED);
    }
    refreshTaskOutputs();
}

HWND createSettingsControl(
    HWND parent,
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
        style | WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    setFont(control);
    return control;
}

void updateMosaicStrengthLabel(HWND window)
{
    const int strength = static_cast<int>(
        SendDlgItemMessageW(window, IdMosaicSlider, TBM_GETPOS, 0, 0));
    const std::wstring text = std::to_wstring(strength) + L" / 100";
    SetWindowTextW(GetDlgItem(window, IdMosaicValue), text.c_str());
}

void createSettingsUi(HWND window)
{
    createSettingsControl(window, 0, L"STATIC", L"输出目录", SS_LEFT, 24, 26, 84, 24, 0);
    HWND outputEdit = createSettingsControl(
        window, WS_EX_CLIENTEDGE, L"EDIT", state.outputDirectory.c_str(),
        ES_AUTOHSCROLL, 112, 22, 570, 28, IdOutputEdit);
    createSettingsControl(window, 0, L"BUTTON", L"浏览…", BS_PUSHBUTTON, 694, 20, 110, 31, IdOutputBrowse);

    createSettingsControl(window, 0, L"BUTTON", L"推理设备", BS_GROUPBOX, 24, 62, 780, 58, 0);
    HWND gpuRadio = createSettingsControl(
        window, 0, L"BUTTON", L"GPU（首次选择会自动下载组件）", BS_AUTORADIOBUTTON | WS_GROUP,
        48, 84, 300, 25, IdGpuRadio);
    HWND cpuRadio = createSettingsControl(
        window, 0, L"BUTTON", L"CPU", BS_AUTORADIOBUTTON,
        370, 84, 100, 25, IdCpuRadio);
    Button_SetCheck(state.useGpu ? gpuRadio : cpuRadio, BST_CHECKED);

    createSettingsControl(
        window, 0, L"BUTTON", L"马赛克强度（数值越大，方块越大）",
        BS_GROUPBOX, 24, 130, 780, 62, 0);
    HWND mosaicSlider = createSettingsControl(
        window, 0, TRACKBAR_CLASSW, L"",
        TBS_HORZ | TBS_AUTOTICKS, 48, 151, 616, 32, IdMosaicSlider);
    SendMessageW(mosaicSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessageW(mosaicSlider, TBM_SETTICFREQ, 10, 0);
    SendMessageW(mosaicSlider, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(mosaicSlider, TBM_SETPOS, TRUE, state.mosaicStrength);
    createSettingsControl(
        window, 0, L"STATIC", L"", SS_CENTER,
        676, 153, 104, 24, IdMosaicValue);
    updateMosaicStrengthLabel(window);

    HWND faceCheck = createSettingsControl(
        window, 0, L"BUTTON", L"人脸打码", BS_AUTOCHECKBOX,
        24, 208, 90, 26, IdFaceCheck);
    createSettingsControl(
        window, WS_EX_CLIENTEDGE, L"EDIT", state.faceModel.c_str(),
        ES_AUTOHSCROLL, 118, 205, 564, 28, IdFaceModelEdit);
    createSettingsControl(window, 0, L"BUTTON", L"选择模型", BS_PUSHBUTTON, 694, 203, 110, 31, IdFaceModelBrowse);
    Button_SetCheck(faceCheck, state.faceEnabled ? BST_CHECKED : BST_UNCHECKED);

    HWND plateCheck = createSettingsControl(
        window, 0, L"BUTTON", L"车牌打码", BS_AUTOCHECKBOX,
        24, 246, 90, 26, IdPlateCheck);
    createSettingsControl(
        window, WS_EX_CLIENTEDGE, L"EDIT", state.plateModel.c_str(),
        ES_AUTOHSCROLL, 118, 243, 564, 28, IdPlateModelEdit);
    createSettingsControl(window, 0, L"BUTTON", L"选择模型", BS_PUSHBUTTON, 694, 241, 110, 31, IdPlateModelBrowse);
    Button_SetCheck(plateCheck, state.plateEnabled ? BST_CHECKED : BST_UNCHECKED);

    HWND audioCheck = createSettingsControl(
        window, 0, L"BUTTON", L"保留原视频音频", BS_AUTOCHECKBOX,
        24, 288, 150, 26, IdAudioCheck);
    Button_SetCheck(audioCheck, state.preserveAudio ? BST_CHECKED : BST_UNCHECKED);
    createSettingsControl(
        window, WS_EX_CLIENTEDGE, L"EDIT", state.ffmpegExecutable.c_str(),
        ES_AUTOHSCROLL, 178, 285, 504, 28, IdFfmpegEdit);
    createSettingsControl(window, 0, L"BUTTON", L"选择 FFmpeg", BS_PUSHBUTTON, 694, 283, 110, 31, IdFfmpegBrowse);
    EnableWindow(GetDlgItem(window, IdFfmpegEdit), state.preserveAudio);
    EnableWindow(GetDlgItem(window, IdFfmpegBrowse), state.preserveAudio);

    createSettingsControl(
        window, 0, L"STATIC",
        L"路径相对于程序目录；随附的 FFmpeg 位于 tools\\ffmpeg.exe。",
        SS_LEFT, 24, 330, 560, 24, 0);
    createSettingsControl(window, 0, L"BUTTON", L"保存", BS_DEFPUSHBUTTON, 574, 364, 110, 36, IdSettingsSave);
    createSettingsControl(window, 0, L"BUTTON", L"取消", BS_PUSHBUTTON, 694, 364, 110, 36, IdSettingsCancel);
    SetFocus(outputEdit);
}

void closeSettingsWindow(HWND window)
{
    EnableWindow(state.window, TRUE);
    state.settingsWindow = nullptr;
    DestroyWindow(window);
    SetForegroundWindow(state.window);
}

LRESULT CALLBACK settingsWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        state.settingsWindow = window;
        createSettingsUi(window);
        return 0;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == GetDlgItem(window, IdMosaicSlider))
        {
            updateMosaicStrengthLabel(window);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IdOutputBrowse:
        {
            const auto folder = browseForFolder(window);
            if (!folder.empty())
            {
                SetWindowTextW(GetDlgItem(window, IdOutputEdit), displayRuntimePath(folder).c_str());
            }
            break;
        }
        case IdFaceModelBrowse:
        case IdPlateModelBrowse:
        {
            const auto model = browseForSingleFile(
                window,
                L"ONNX 模型\0*.onnx\0所有文件\0*.*\0\0",
                L"选择检测模型");
            if (!model.empty())
            {
                const int editId = LOWORD(wParam) == IdFaceModelBrowse
                    ? IdFaceModelEdit
                    : IdPlateModelEdit;
                SetWindowTextW(GetDlgItem(window, editId), displayRuntimePath(model).c_str());
            }
            break;
        }
        case IdFfmpegBrowse:
        {
            const auto ffmpeg = browseForSingleFile(
                window,
                L"FFmpeg\0ffmpeg.exe\0可执行文件\0*.exe\0\0",
                L"选择 ffmpeg.exe");
            if (!ffmpeg.empty())
            {
                SetWindowTextW(GetDlgItem(window, IdFfmpegEdit), displayRuntimePath(ffmpeg).c_str());
            }
            break;
        }
        case IdAudioCheck:
        {
            const bool enabled = Button_GetCheck(GetDlgItem(window, IdAudioCheck)) == BST_CHECKED;
            EnableWindow(GetDlgItem(window, IdFfmpegEdit), enabled);
            EnableWindow(GetDlgItem(window, IdFfmpegBrowse), enabled);
            break;
        }
        case IdSettingsSave:
        {
            state.outputDirectory = getWindowText(GetDlgItem(window, IdOutputEdit));
            if (state.outputDirectory.empty())
            {
                state.outputDirectory = L"output";
            }
            state.faceModel = getWindowText(GetDlgItem(window, IdFaceModelEdit));
            state.plateModel = getWindowText(GetDlgItem(window, IdPlateModelEdit));
            state.ffmpegExecutable = getWindowText(GetDlgItem(window, IdFfmpegEdit));
            state.faceEnabled = Button_GetCheck(GetDlgItem(window, IdFaceCheck)) == BST_CHECKED;
            state.plateEnabled = Button_GetCheck(GetDlgItem(window, IdPlateCheck)) == BST_CHECKED;
            const bool requestedGpu =
                Button_GetCheck(GetDlgItem(window, IdGpuRadio)) == BST_CHECKED;
            state.preserveAudio = Button_GetCheck(GetDlgItem(window, IdAudioCheck)) == BST_CHECKED;
            state.mosaicStrength = std::clamp(
                static_cast<int>(SendDlgItemMessageW(
                    window, IdMosaicSlider, TBM_GETPOS, 0, 0)),
                1,
                100);
            refreshTaskOutputs();
            closeSettingsWindow(window);
            if (requestedGpu)
            {
                if (video_privacy::gpuRuntimeIsInstalled(state.runtimeRoot))
                {
                    state.useGpu = true;
                    SetWindowTextW(state.statusText, L"已选择 GPU 推理");
                }
                else
                {
                    startGpuRuntimeInstall();
                }
            }
            else
            {
                state.useGpu = false;
                SetWindowTextW(state.statusText, L"已选择 CPU 推理");
            }
            break;
        }
        case IdSettingsCancel:
            closeSettingsWindow(window);
            break;
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(246, 248, 251));
        return reinterpret_cast<LRESULT>(state.backgroundBrush);
    case WM_CLOSE:
        closeSettingsWindow(window);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

void openSettingsWindow()
{
    if (state.settingsWindow != nullptr)
    {
        SetForegroundWindow(state.settingsWindow);
        return;
    }
    RECT ownerRect{};
    GetWindowRect(state.window, &ownerRect);
    const int width = 844;
    const int height = 456;
    HWND settings = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        SettingsClassName,
        L"处理设置",
        WS_CAPTION | WS_SYSMENU,
        ownerRect.left + (ownerRect.right - ownerRect.left - width) / 2,
        ownerRect.top + (ownerRect.bottom - ownerRect.top - height) / 2,
        width,
        height,
        state.window,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (settings != nullptr)
    {
        EnableWindow(state.window, FALSE);
        ShowWindow(settings, SW_SHOW);
        UpdateWindow(settings);
    }
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        createUi(window);
        return 0;
    case WM_DROPFILES:
        if (!state.processing)
        {
            handleDrop(reinterpret_cast<HDROP>(wParam));
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IdAddFiles:
            browseForMediaFiles(window);
            break;
        case IdRemove:
            removeSelectedTasks();
            break;
        case IdClear:
            state.tasks.clear();
            ListView_DeleteAllItems(state.taskList);
            break;
        case IdSettings:
            openSettingsWindow();
            break;
        case IdStart:
            startProcessing();
            break;
        case IdCancel:
            state.cancelRequested.store(true);
            SetWindowTextW(state.statusText, L"正在取消，请等待当前推理结束…");
            EnableWindow(state.cancelButton, FALSE);
            break;
        }
        return 0;
    case MessageProgress:
    {
        std::unique_ptr<ProgressPayload> payload(reinterpret_cast<ProgressPayload *>(lParam));
        SendMessageW(state.currentProgress, PBM_SETPOS, payload->currentPercent, 0);
        SendMessageW(state.overallProgress, PBM_SETPOS, payload->overallPercent, 0);
        SetWindowTextW(state.statusText, payload->status.c_str());
        SetWindowTextW(state.elapsedText, payload->elapsed.c_str());
        SetWindowTextW(state.remainingText, payload->remaining.c_str());
        return 0;
    }
    case MessageTaskStatus:
    {
        std::unique_ptr<StatusPayload> payload(reinterpret_cast<StatusPayload *>(lParam));
        setTaskCell(payload->row, 3, payload->status);
        ListView_EnsureVisible(state.taskList, payload->row, FALSE);
        return 0;
    }
    case MessageFinished:
    {
        std::unique_ptr<FinishedPayload> payload(reinterpret_cast<FinishedPayload *>(lParam));
        state.processing = false;
        enableInputs(true);
        if (state.worker.joinable())
        {
            state.worker.join();
        }
        std::wstring summary;
        if (payload->cancelled)
        {
            summary = L"处理已取消";
        }
        else if (!payload->message.empty())
        {
            summary = L"处理失败：" + payload->message;
        }
        else
        {
            summary = L"批处理完成：成功 " + std::to_wstring(payload->succeeded) +
                      L"，失败 " + std::to_wstring(payload->failed);
            SendMessageW(state.overallProgress, PBM_SETPOS, 100, 0);
        }
        SetWindowTextW(state.statusText, summary.c_str());
        MessageBoxW(
            window,
            summary.c_str(),
            L"VideoPrivacy",
            payload->failed > 0 ? MB_ICONWARNING : MB_ICONINFORMATION);
        if (!payload->cancelled && payload->succeeded > 0)
        {
            const auto outputDirectory = resolveRuntimePath(state.outputDirectory);
            std::filesystem::create_directories(outputDirectory);
            ShellExecuteW(
                window,
                L"open",
                outputDirectory.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
        }
        return 0;
    }
    case MessageGpuInstallStatus:
    {
        std::unique_ptr<std::wstring> payload(
            reinterpret_cast<std::wstring *>(lParam));
        SetWindowTextW(state.statusText, payload->c_str());
        return 0;
    }
    case MessageGpuInstallFinished:
    {
        std::unique_ptr<video_privacy::GpuRuntimeInstallResult> payload(
            reinterpret_cast<video_privacy::GpuRuntimeInstallResult *>(lParam));
        state.gpuInstalling = false;
        state.useGpu = payload->success;
        if (state.gpuInstaller.joinable())
        {
            state.gpuInstaller.join();
        }
        enableInputs(true);
        SetWindowTextW(state.statusText, payload->message.c_str());
        MessageBoxW(
            window,
            payload->message.c_str(),
            L"GPU 运行组件",
            payload->success ? MB_ICONINFORMATION : MB_ICONWARNING);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(246, 248, 251));
        return reinterpret_cast<LRESULT>(state.backgroundBrush);
    case WM_CLOSE:
        if (state.gpuInstalling)
        {
            MessageBoxW(
                window,
                L"GPU 组件正在下载或安装，请完成后再退出。",
                L"请稍候",
                MB_ICONINFORMATION);
            return 0;
        }
        if (state.processing)
        {
            if (MessageBoxW(
                    window,
                    L"任务仍在处理，是否取消并退出？",
                    L"确认退出",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
            {
                return 0;
            }
            state.cancelRequested.store(true);
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        state.cancelRequested.store(true);
        if (state.worker.joinable())
        {
            state.worker.join();
        }
        if (state.gpuInstaller.joinable())
        {
            state.gpuInstaller.join();
        }
        DeleteObject(state.normalFont);
        DeleteObject(state.titleFont);
        DeleteObject(state.backgroundBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX commonControls{
        sizeof(commonControls),
        ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES};
    InitCommonControlsEx(&commonControls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = WindowClassName;
    RegisterClassExW(&windowClass);

    WNDCLASSEXW settingsClass = windowClass;
    settingsClass.lpfnWndProc = settingsWindowProcedure;
    settingsClass.lpszClassName = SettingsClassName;
    settingsClass.hIcon = nullptr;
    RegisterClassExW(&settingsClass);

    HWND window = CreateWindowExW(
        0,
        WindowClassName,
        L"VideoPrivacy",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1006,
        744,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr)
    {
        CoUninitialize();
        return 1;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == WM_KEYDOWN &&
            message.wParam == VK_RETURN &&
            state.settingsWindow == nullptr &&
            !state.processing &&
            (message.hwnd == window || IsChild(window, message.hwnd)))
        {
            SendMessageW(window, WM_COMMAND, IdStart, 0);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

#endif

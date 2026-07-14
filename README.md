# VideoPrivacy
A C++ video privacy protection tool with YOLO face detection, CPU/GPU inference, and mosaic anonymization.

VideoPrivacy 是一个基于 **C++、OpenCV、YOLO 和 ONNX Runtime** 开发的视频隐私保护工具。

程序能够自动检测图片或视频中的人脸，并对检测区域进行马赛克处理。默认使用 CPU 推理，同时支持按需安装 NVIDIA GPU 加速组件。

## 功能特性

- 图片人脸检测
- 视频人脸检测
- 自动人脸马赛克
- 马赛克强度调节
- CPU 推理
- NVIDIA GPU 推理
- 图形用户界面
- 命令行处理程序
- FFmpeg 音视频处理
- Windows 安装程序
- GPU 运行组件按需下载

## 技术栈

- C++17
- OpenCV 4.12
- YOLO11
- ONNX Runtime
- CUDA 12
- cuDNN 9
- FFmpeg
- CMake
- Win32 GUI
- Inno Setup

## 项目结构

```text
VideoPrivacy/
├─ cpp/
│  ├─ include/
│  │  └─ video_privacy/
│  ├─ src/
│  ├─ CMakeLists.txt
│  └─ BUILDING.md
├─ installer/
│  └─ VideoPrivacy.iss
├─ .gitattributes
├─ .gitignore
└─ README.md
```

## 推理模式

### CPU 模式

CPU 是程序的默认推理模式，不需要安装 CUDA 或 cuDNN。

适用于：

- 没有 NVIDIA 显卡的电脑
- 只需要基础人脸隐私保护功能的用户
- 不希望下载大型 GPU 运行环境的用户

### NVIDIA GPU 模式

GPU 模式能够提高模型推理速度，但需要额外安装以下组件：

- CUDA 12 Runtime
- cuDNN 9 Runtime
- ONNX Runtime CUDA Execution Provider

GPU 组件不会包含在基础安装程序中。只有用户在安装程序中主动勾选 GPU 加速功能时，安装程序才会下载这些组件。

GPU 运行组件仓库：

[VideoPrivacy Optional GPU Components](https://github.com/lpossj/VideoPrivacy_optionalGPUcomponents)

## 源码构建

详细构建说明请查看：

```text
cpp/BUILDING.md
```

基本构建命令：

```powershell
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
```

由于 OpenCV 和 ONNX Runtime 的安装位置可能不同，实际构建前需要根据本地环境修改 `cpp/CMakeLists.txt` 中的依赖路径。

## 构建项目

构建前需要安装：

- Visual Studio C++ 工具链
- CMake
- OpenCV
- ONNX Runtime

请在本机配置以下环境变量：

```text
OpenCV_DIR
ONNXRUNTIME_ROOT
```

然后在项目根目录执行：

```powershell
cmake -S cpp -B cpp/build `
  -G "Visual Studio 18 2026" `
  -A x64

cmake --build cpp/build `
  --config Release
```

## 模型文件

项目使用导出为 ONNX 格式的 YOLO11s 人脸检测模型。

程序默认查找以下模型：

```text
models/face/face_yolo11s.onnx
```

由于模型文件大小和模型许可证等原因，模型文件可能不会直接包含在源码仓库中。

## 安装程序

Windows 安装程序使用 Inno Setup 制作。

安装脚本位于：

```text
installer/VideoPrivacy.iss
```

默认安装内容包括：

- VideoPrivacy 主程序
- CPU 推理运行环境
- OpenCV 运行库
- ONNX Runtime CPU 运行环境
- FFmpeg 工具
- 人脸检测模型

用户可以在安装过程中选择是否额外下载 NVIDIA GPU 加速组件。

## 隐私说明

VideoPrivacy 在用户本地计算机上处理图片和视频。

程序本身不需要将用户的视频或图片上传到远程服务器。

请勿将包含个人隐私的测试视频、输入视频或处理结果提交到 GitHub 仓库。

## 当前版本

当前版本：

```text
v0.0.1
```

项目目前仍处于早期开发阶段。

## 后续计划

- 优化视频人脸检测稳定性
- 增加纯色遮挡模式
- 增加车牌检测与隐私保护
- 增加运行日志
- 完善异常处理
- 优化图形界面

## 免责声明

本项目仅用于学习、研究和合法的隐私保护用途。

使用者应当遵守所在地区的法律法规，不得将本项目用于侵犯他人隐私或其他违法用途。

## License

项目许可证将在后续版本中补充。
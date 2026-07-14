# C++ build configuration

The build does not depend on paths from the original developer's computer.
Supply dependency locations through CMake cache variables or environment
variables.

## Required dependencies

- CMake 3.20 or newer
- A C++17 compiler
- OpenCV with `core`, `imgproc`, `imgcodecs`, `dnn`, and `videoio`
- ONNX Runtime (CPU or GPU package)

OpenCV, ONNX Runtime, and the application must use compatible compiler
toolchains and architectures. For example, the official Windows ONNX Runtime
NuGet package and an MSVC OpenCV build should be compiled with MSVC x64, not
MinGW.

## Windows example

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -A x64 `
  -DOpenCV_DIR="<opencv-config-directory>" `
  -DONNXRUNTIME_ROOT="<onnx-runtime-package-directory>"

cmake --build cpp/build --config Release
```

For CUDA, install matching CUDA and cuDNN libraries. If their DLLs are not on
the system `PATH`, provide the directory explicitly:

```powershell
cmake -S cpp -B cpp/build `
  -DVIDEO_PRIVACY_CUDA_RUNTIME_DIR="<cuda-cudnn-dll-directory>"
```

A PyTorch installation at `.venv/Lib/site-packages/torch/lib` is detected
automatically on Windows.

The same settings can be provided as the `OpenCV_DIR`, `ONNXRUNTIME_ROOT`, and
`VIDEO_PRIVACY_CUDA_RUNTIME_DIR` environment variables.

## Run

From the repository root, use the default model, input, and output locations:

```powershell
cpp/build/Release/video_privacy.exe
```

Or pass all paths explicitly:

```powershell
cpp/build/Release/video_privacy.exe model.onnx input.mp4 output.mp4
```

The Windows GUI executable is generated as:

```powershell
cpp/build/Release/video_privacy_gui.exe
```

The packaged runtime layout is relative to the executable:

```text
video_privacy_gui.exe
models/face/face_yolo11s.onnx
models/license_plate/license_plate.onnx
output/
```

Video tasks preserve the original audio by default; this can be disabled in
Settings. The bundled `tools/ffmpeg.exe` is detected automatically, so no
system installation is required. Settings also provides explicit CPU/GPU
selection. The face and license-plate switches use separate ONNX model
paths; a compatible license-plate model must be supplied before enabling that
option.

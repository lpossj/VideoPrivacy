cmake_minimum_required(VERSION 3.20)

project(VideoPrivacyCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OpenCV is discovered from OpenCV_DIR, the OpenCV_DIR environment variable,
# CMAKE_PREFIX_PATH, or the platform's standard package locations.
if(NOT OpenCV_DIR AND DEFINED ENV{OpenCV_DIR})
    file(TO_CMAKE_PATH "$ENV{OpenCV_DIR}" OpenCV_DIR)
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs dnn videoio)

# Point ONNXRUNTIME_ROOT at either an extracted ONNX Runtime package or the
# Microsoft.ML.OnnxRuntime(.Gpu) NuGet package directory.
set(ONNXRUNTIME_ROOT "" CACHE PATH "Root directory of ONNX Runtime")
if(NOT ONNXRUNTIME_ROOT AND DEFINED ENV{ONNXRUNTIME_ROOT})
    file(TO_CMAKE_PATH "$ENV{ONNXRUNTIME_ROOT}" ONNXRUNTIME_ROOT)
endif()

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS "${ONNXRUNTIME_ROOT}"
    PATH_SUFFIXES
        include
        build/native/include
        buildTransitive/native/include
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS "${ONNXRUNTIME_ROOT}"
    PATH_SUFFIXES
        lib
        lib64
        runtimes/win-x64/native
)

if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR
        "ONNX Runtime was not found. Set ONNXRUNTIME_ROOT with "
        "-DONNXRUNTIME_ROOT=<path> or define the ONNXRUNTIME_ROOT environment variable."
    )
endif()

get_filename_component(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)

# Optional CUDA/cuDNN DLL directory. A project-local PyTorch installation is
# detected automatically; system installations can use the cache variable or
# VIDEO_PRIVACY_CUDA_RUNTIME_DIR environment variable.
set(VIDEO_PRIVACY_CUDA_RUNTIME_DIR "" CACHE PATH
    "Directory containing CUDA and cuDNN runtime DLLs"
)

if(NOT VIDEO_PRIVACY_CUDA_RUNTIME_DIR AND
   DEFINED ENV{VIDEO_PRIVACY_CUDA_RUNTIME_DIR})
    file(TO_CMAKE_PATH
        "$ENV{VIDEO_PRIVACY_CUDA_RUNTIME_DIR}"
        VIDEO_PRIVACY_CUDA_RUNTIME_DIR
    )
endif()

if(NOT VIDEO_PRIVACY_CUDA_RUNTIME_DIR)
    set(_PROJECT_TORCH_RUNTIME_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../.venv/Lib/site-packages/torch/lib"
    )
    if(EXISTS "${_PROJECT_TORCH_RUNTIME_DIR}/cudnn64_9.dll")
        set(VIDEO_PRIVACY_CUDA_RUNTIME_DIR "${_PROJECT_TORCH_RUNTIME_DIR}")
    endif()
endif()

add_library(video_privacy_core STATIC
    src/face_detector.cpp
    src/image_preprocessor.cpp
    src/media_processor.cpp
    src/mosaic.cpp
    src/video_processor.cpp
)

target_include_directories(video_privacy_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${OpenCV_INCLUDE_DIRS}
        "${ONNXRUNTIME_INCLUDE_DIR}"
)

if(WIN32 AND VIDEO_PRIVACY_CUDA_RUNTIME_DIR)
    if(EXISTS "${VIDEO_PRIVACY_CUDA_RUNTIME_DIR}/cudnn64_9.dll")
        message(STATUS
            "CUDA/cuDNN packaging source: ${VIDEO_PRIVACY_CUDA_RUNTIME_DIR}"
        )
    else()
        message(WARNING
            "cudnn64_9.dll was not found in ${VIDEO_PRIVACY_CUDA_RUNTIME_DIR}; "
            "CUDA initialization may fall back to CPU."
        )
    endif()
endif()

target_link_libraries(video_privacy_core
    PUBLIC
        ${OpenCV_LIBS}
        "${ONNXRUNTIME_LIBRARY}"
)

if(MSVC)
    target_compile_options(video_privacy_core PRIVATE /utf-8 /W4)
else()
    target_compile_options(video_privacy_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(video_privacy src/main.cpp)
target_link_libraries(video_privacy PRIVATE video_privacy_core)

if(WIN32)
    add_executable(video_privacy_gui WIN32
        src/gui_main.cpp
        src/gpu_runtime_installer.cpp
    )
    target_compile_definitions(video_privacy_gui PRIVATE UNICODE _UNICODE)
    target_link_libraries(video_privacy_gui PRIVATE
        video_privacy_core
        comctl32
        comdlg32
        shell32
        ole32
        urlmon
        bcrypt
        uuid
    )
    if(MSVC)
        target_compile_options(video_privacy_gui PRIVATE /utf-8 /W4)
    endif()

    add_executable(video_privacy_setup WIN32 src/installer_main.cpp)
    target_compile_definitions(video_privacy_setup PRIVATE UNICODE _UNICODE)
    target_link_libraries(video_privacy_setup PRIVATE
        comctl32
        shell32
        ole32
        urlmon
        bcrypt
    )
    if(MSVC)
        target_compile_options(video_privacy_setup PRIVATE /utf-8 /W4)
    endif()
endif()

# Deploy every ONNX Runtime DLL supplied by the selected package. This supports
# both CPU-only and GPU packages without hard-coding provider filenames.
function(deploy_onnxruntime target_name)
    if(NOT WIN32)
        return()
    endif()
    file(GLOB ONNXRUNTIME_RUNTIME_DLLS CONFIGURE_DEPENDS
        "${ONNXRUNTIME_NATIVE_DIR}/*.dll"
    )
    foreach(ONNXRUNTIME_RUNTIME_DLL IN LISTS ONNXRUNTIME_RUNTIME_DLLS)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM
        )
    endforeach()
endfunction()

deploy_onnxruntime(video_privacy)
if(TARGET video_privacy_gui)
    deploy_onnxruntime(video_privacy_gui)
endif()

function(deploy_gui_runtime_assets target_name)
    set(PROJECT_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/tools"
            "$<TARGET_FILE_DIR:${target_name}>/models/face"
            "$<TARGET_FILE_DIR:${target_name}>/models/license_plate"
            "$<TARGET_FILE_DIR:${target_name}>/output"
        VERBATIM
    )

    foreach(RUNTIME_TOOL
        ffmpeg.exe
        FFMPEG_LICENSE.txt
        FFMPEG_README.txt
    )
        if(EXISTS "${PROJECT_RUNTIME_ROOT}/tools/${RUNTIME_TOOL}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${PROJECT_RUNTIME_ROOT}/tools/${RUNTIME_TOOL}"
                    "$<TARGET_FILE_DIR:${target_name}>/tools/${RUNTIME_TOOL}"
                VERBATIM
            )
        endif()
    endforeach()

    if(EXISTS "${PROJECT_RUNTIME_ROOT}/gpu_runtime.ini")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_RUNTIME_ROOT}/gpu_runtime.ini"
                "$<TARGET_FILE_DIR:${target_name}>/gpu_runtime.ini"
            VERBATIM
        )
    endif()

    if(EXISTS "${PROJECT_RUNTIME_ROOT}/models/face/face_yolo11s.onnx")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_RUNTIME_ROOT}/models/face/face_yolo11s.onnx"
                "$<TARGET_FILE_DIR:${target_name}>/models/face/face_yolo11s.onnx"
            VERBATIM
        )
    endif()

    if(EXISTS "${PROJECT_RUNTIME_ROOT}/models/license_plate/license_plate.onnx")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_RUNTIME_ROOT}/models/license_plate/license_plate.onnx"
                "$<TARGET_FILE_DIR:${target_name}>/models/license_plate/license_plate.onnx"
            VERBATIM
        )
    endif()
endfunction()

if(TARGET video_privacy_gui)
    deploy_gui_runtime_assets(video_privacy_gui)
endif()

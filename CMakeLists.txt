cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(VideoImporter VERSION 1.0.0)

# Decodificador de video: un archivo por plataforma (cada uno se compila solo donde existe su API) + el respaldo de ffmpeg para PC.
set(VIDEO_SOURCES
    src/video/decode_ffmpeg_cli.cpp
    src/video/decode_win.cpp        # solo Windows   (Media Foundation)
    src/video/decode_android.cpp    # solo Android   (NDK AMediaCodec)
)
if (APPLE)
    enable_language(OBJCXX)
    list(APPEND VIDEO_SOURCES src/video/decode_apple.mm)    # macOS / iOS (AVFoundation)
endif()

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/playlog.cpp
    src/ui/VideoPopup.cpp
    ${VIDEO_SOURCES}
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# Los archivos del decodificador incluyen cabeceras del sistema (windows.h, NDK, Foundation): sin las cabeceras precompiladas de Geode
set_source_files_properties(${VIDEO_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

target_include_directories(${PROJECT_NAME} PRIVATE src src/engine src/video)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} mfplat mfreadwrite mfuuid ole32)
elseif (ANDROID)
    target_link_libraries(${PROJECT_NAME} mediandk)
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation")
endif()

setup_geode_mod(${PROJECT_NAME})

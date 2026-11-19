cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(VideoImporter VERSION 0.1.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/ui/VideoPopup.cpp
    src/stb_impl.cpp
    src/video/source_wmf.cpp      # solo Windows (Media Foundation); en las demas plataformas queda vacio
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# stb_image y el lector de Media Foundation van sin cabeceras precompiladas (no deben ver Geode.hpp ni sus macros)
set_source_files_properties(src/stb_impl.cpp src/video/source_wmf.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

target_include_directories(${PROJECT_NAME} PRIVATE include src src/engine src/video)
target_compile_definitions(${PROJECT_NAME} PRIVATE VX_WITH_STB=1)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} mfplat mfreadwrite mfuuid ole32)
endif()

setup_geode_mod(${PROJECT_NAME})

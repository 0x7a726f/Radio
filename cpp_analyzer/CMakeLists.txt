cmake_minimum_required(VERSION 3.16)
project(AstroHydrogenLineAnalyzer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Include directories
include_directories(
    include
    lib/kissfft
    lib/imgui
    lib/stb
)

# Compiler flags
if (MSVC)
    add_compile_options(/O2 /arch:AVX2 /W3 /MP)
else()
    add_compile_options(-O3 -mavx2 -mfma -msse4.1 -Wall -Wextra -Wno-unused-parameter)
endif()

# Core DSP and Scientific sources (shared between CLI and GUI)
set(CORE_SOURCES
    src/sidecar_reader.cpp
    src/streaming_processor.cpp
    src/dc_excision.cpp
    src/rfi_filter.cpp
    src/baseline_fitter.cpp
    src/astrophysics.cpp
    src/plot_renderer.cpp
    src/exporter.cpp
    lib/kissfft/kiss_fft.c
)

# ImGui sources for Desktop GUI
set(IMGUI_SOURCES
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_impl_win32.cpp
    lib/imgui/imgui_impl_dx11.cpp
)

# 1. Headless CLI executable
add_executable(astro_analyzer_cli
    ${CORE_SOURCES}
    src/main_cli.cpp
)

if (MINGW)
    target_link_options(astro_analyzer_cli PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(astro_analyzer_cli PRIVATE ole32 kernel32 user32)
elseif (MSVC)
    target_link_libraries(astro_analyzer_cli PRIVATE ole32 kernel32 user32)
endif()

# 2. Desktop GUI executable (DirectX 11 + Dear ImGui)
add_executable(astro_analyzer_gui
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
    src/app_gui.cpp
    src/main_gui.cpp
)

if (MINGW)
    target_link_options(astro_analyzer_gui PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(astro_analyzer_gui PRIVATE
        d3d11 dxgi d3dcompiler dwmapi ole32 kernel32 user32 gdi32 shell32
    )
elseif (MSVC)
    target_link_libraries(astro_analyzer_gui PRIVATE
        d3d11 dxgi d3dcompiler dwmapi ole32 kernel32 user32 gdi32 shell32
    )
endif()

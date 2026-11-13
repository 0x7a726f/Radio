cmake_minimum_required(VERSION 3.16)
project(RadioAstronomySuite VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/kissfft
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/rtlsdr
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/stb
)

# Compiler optimization flags
if (MSVC)
    add_compile_options(/O2 /arch:AVX2 /W3 /MP /utf-8)
else()
    add_compile_options(-O3 -mavx2 -mfma -msse4.1 -Wall -Wextra -Wno-unused-parameter)
endif()

# Core DSP, SDR, and Scientific sources
set(CORE_SOURCES
    src/sdr_device.cpp
    src/virtual_sdr.cpp
    src/sdr_controller.cpp
    src/dsp_pipeline.cpp
    src/cs16_writer.cpp
    src/waterfall.cpp
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

# Dear ImGui sources
set(IMGUI_SOURCES
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_impl_win32.cpp
    lib/imgui/imgui_impl_dx11.cpp
)

# Windows system libraries
set(WINDOWS_LIBS
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    ole32
    kernel32
    user32
    gdi32
    shell32
    psapi
    comdlg32
)

# ----------------------------------------------------------------------
# 1. Unified Desktop GUI Executable (radio_suite.exe)
# ----------------------------------------------------------------------
add_executable(radio_suite
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
    src/ui_record_panel.cpp
    src/ui_analyze_panel.cpp
    src/app_gui.cpp
    src/main_gui.cpp
)

if (MINGW)
    target_link_options(radio_suite PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
    target_link_libraries(radio_suite PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(radio_suite PRIVATE ${WINDOWS_LIBS})
    set_target_properties(radio_suite PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

# ----------------------------------------------------------------------
# 2. Unified Headless CLI Executable (radio_suite_cli.exe)
# ----------------------------------------------------------------------
add_executable(radio_suite_cli
    ${CORE_SOURCES}
    src/main_cli.cpp
)

if (MINGW)
    target_link_options(radio_suite_cli PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(radio_suite_cli PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(radio_suite_cli PRIVATE ${WINDOWS_LIBS})
endif()

# Post-build copy of hardware SDR dynamic libraries
add_custom_command(TARGET radio_suite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/rtlsdr.dll"
    $<TARGET_FILE_DIR:radio_suite>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/libusb-1.0.dll"
    $<TARGET_FILE_DIR:radio_suite>
)

# ----------------------------------------------------------------------
# 3. Test Targets & Automated Testing Suite
# ----------------------------------------------------------------------
enable_testing()

add_executable(test_suite_state tests/test_suite_state.cpp)
if (MINGW)
    target_link_options(test_suite_state PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(test_suite_state PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(test_suite_state PRIVATE ${WINDOWS_LIBS})
endif()
add_test(NAME test_suite_state COMMAND test_suite_state)

add_executable(test_end_to_end_bridge ${CORE_SOURCES} tests/test_end_to_end_bridge.cpp)
if (MINGW)
    target_link_options(test_end_to_end_bridge PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(test_end_to_end_bridge PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(test_end_to_end_bridge PRIVATE ${WINDOWS_LIBS})
endif()
add_test(NAME test_end_to_end_bridge COMMAND test_end_to_end_bridge)

add_executable(test_cli_commands tests/test_cli_commands.cpp)
if (MINGW)
    target_link_options(test_cli_commands PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(test_cli_commands PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(test_cli_commands PRIVATE ${WINDOWS_LIBS})
endif()
add_test(NAME test_cli_commands COMMAND test_cli_commands)

add_executable(test_performance_stress ${CORE_SOURCES} tests/test_performance_stress.cpp)
if (MINGW)
    target_link_options(test_performance_stress PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(test_performance_stress PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(test_performance_stress PRIVATE ${WINDOWS_LIBS})
endif()
add_test(NAME test_performance_stress COMMAND test_performance_stress)

add_executable(test_dsp_optimizations ${CORE_SOURCES} tests/test_dsp_optimizations.cpp)
if (MINGW)
    target_link_options(test_dsp_optimizations PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(test_dsp_optimizations PRIVATE ${WINDOWS_LIBS})
elseif (MSVC)
    target_link_libraries(test_dsp_optimizations PRIVATE ${WINDOWS_LIBS})
endif()
add_test(NAME test_dsp_optimizations COMMAND test_dsp_optimizations)


cmake_minimum_required(VERSION 3.16)
project(CppSdrRecorder VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(MSVC)
    add_compile_options(/O2 /W3 /MP /utf-8 /arch:AVX2)
else()
    add_compile_options(-O3 -Wall -Wextra -Wno-unused-parameter -mavx2 -mfma -msse4.1)
endif()

# Directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/kissfft
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/rtlsdr
)

# Core library sources (Shared between GUI and CLI)
set(CORE_SOURCES
    src/sdr_device.cpp
    src/virtual_sdr.cpp
    src/sdr_controller.cpp
    src/dsp_pipeline.cpp
    src/cs16_writer.cpp
    src/waterfall.cpp
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
    user32
    gdi32
    shell32
    ole32
    kernel32
)

# --- 1. GUI Executable ---
add_executable(sdr_recorder_gui
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
    src/app_gui.cpp
    src/main_gui.cpp
)

target_link_libraries(sdr_recorder_gui PRIVATE ${WINDOWS_LIBS})

# Set Windows GUI subsystem for release builds, console for debug
if(MSVC)
    set_target_properties(sdr_recorder_gui PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    # MinGW
    set_target_properties(sdr_recorder_gui PROPERTIES LINK_FLAGS "-mwindows")
endif()

# --- 2. Headless CLI Executable ---
add_executable(sdr_recorder_cli
    ${CORE_SOURCES}
    src/main_cli.cpp
)

target_link_libraries(sdr_recorder_cli PRIVATE ${WINDOWS_LIBS})

# Copy DLLs post-build
add_custom_command(TARGET sdr_recorder_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/rtlsdr.dll"
    $<TARGET_FILE_DIR:sdr_recorder_gui>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/libusb-1.0.dll"
    $<TARGET_FILE_DIR:sdr_recorder_gui>
)

add_custom_command(TARGET sdr_recorder_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/rtlsdr.dll"
    $<TARGET_FILE_DIR:sdr_recorder_cli>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/libusb-1.0.dll"
    $<TARGET_FILE_DIR:sdr_recorder_cli>
)

# --- 3. Automated Test Suite & Benchmarks ---
option(BUILD_TESTS "Build automated test suite and benchmarks" ON)
if(BUILD_TESTS)
    enable_testing()

    add_executable(test_tuning_coalescing ${CORE_SOURCES} tests/test_tuning_coalescing.cpp)
    target_link_libraries(test_tuning_coalescing PRIVATE ${WINDOWS_LIBS} psapi)
    add_test(NAME TuningCoalescing COMMAND test_tuning_coalescing)

    add_executable(test_dsp_pipeline ${CORE_SOURCES} tests/test_dsp_pipeline.cpp)
    target_link_libraries(test_dsp_pipeline PRIVATE ${WINDOWS_LIBS} psapi)
    add_test(NAME DspPipeline COMMAND test_dsp_pipeline)

    add_executable(test_cs16_writer ${CORE_SOURCES} tests/test_cs16_writer.cpp)
    target_link_libraries(test_cs16_writer PRIVATE ${WINDOWS_LIBS} psapi)
    add_test(NAME Cs16Writer COMMAND test_cs16_writer)

    add_executable(test_virtual_sdr ${CORE_SOURCES} tests/test_virtual_sdr.cpp)
    target_link_libraries(test_virtual_sdr PRIVATE ${WINDOWS_LIBS} psapi)
    add_test(NAME VirtualSdr COMMAND test_virtual_sdr)

    add_executable(benchmarks_stress ${CORE_SOURCES} tests/benchmarks_stress.cpp)
    target_link_libraries(benchmarks_stress PRIVATE ${WINDOWS_LIBS} psapi)
    add_test(NAME BenchmarksStress COMMAND benchmarks_stress)

    add_executable(test_simd_lockfree ${CORE_SOURCES} tests/test_simd_lockfree.cpp)
    target_link_libraries(test_simd_lockfree PRIVATE ${WINDOWS_LIBS} psapi)
    add_test(NAME SimdLockFree COMMAND test_simd_lockfree)
endif()


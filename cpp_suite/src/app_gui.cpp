#include "app_gui.h"
#include "../lib/imgui/imgui.h"
#include "constants.h"
#include "simd_dsp.h"

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

AppGui::AppGui()
    : writer_("recordings"),
      waterfall_(800, 260),
      record_panel_(sdr_, dsp_, writer_, waterfall_, state_),
      analyze_panel_(state_)
{
    // Subscribe DSP pipeline and CS16 Writer to SDR sample stream
    sdr_.add_sample_callback([this](const uint8_t* data, size_t len) {
        dsp_.feed_samples(data, len);
        writer_.feed_samples(data, len);
    });

    sdr_.initialize(false, 0);
    sdr_.start_streaming();

    setup_satdump_theme();
}

AppGui::~AppGui() {
    sdr_.stop_streaming();
    writer_.stop_recording();
    release_d3d_resources();
}

void AppGui::setup_satdump_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    // SatDump Dark Theme Roundings (Dark.json: 2.0 everywhere)
    style.WindowRounding    = 2.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;
    style.FramePadding      = ImVec2(7, 4);
    style.ItemSpacing       = ImVec2(7, 5);
    style.WindowPadding     = ImVec2(8, 8);
    style.ScrollbarSize     = 12.0f;

    // SatDump Dark Theme Palette (Dark.json)
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // #FFFFFFFF
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // #808080FF
    colors[ImGuiCol_WindowBg]              = ImVec4(0.059f, 0.059f, 0.059f, 0.941f); // #0F0F0FF0
    colors[ImGuiCol_ChildBg]               = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.078f, 0.078f, 0.078f, 0.941f); // #141414F0
    colors[ImGuiCol_Border]                = ImVec4(0.431f, 0.431f, 0.502f, 0.502f); // #6E6E8080
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.200f, 0.212f, 0.220f, 0.541f); // #3336388A
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.250f, 0.270f, 0.290f, 0.650f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.280f, 0.300f, 0.320f, 0.750f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.039f, 0.039f, 0.039f, 1.00f);  // #0A0A0AFF
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.290f, 0.290f, 0.290f, 1.00f);  // #4A4A4AFF
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.000f, 0.000f, 0.000f, 0.510f); // #00000082
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.141f, 0.141f, 0.141f, 1.00f);  // #242424FF
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.020f, 0.020f, 0.020f, 0.529f); // #05050587
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.310f, 0.310f, 0.310f, 1.00f);  // #4F4F4FFF
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.412f, 0.412f, 0.412f, 1.00f);  // #696969FF
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.510f, 0.510f, 0.510f, 1.00f);  // #828282FF
    colors[ImGuiCol_CheckMark]             = ImVec4(0.239f, 0.522f, 0.878f, 1.00f);  // #3D85E0FF (SatDump Blue)
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.239f, 0.522f, 0.878f, 1.00f);  // #3D85E0FF
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.259f, 0.588f, 0.980f, 1.00f);  // #4296FAFF
    colors[ImGuiCol_Button]                = ImVec4(0.239f, 0.522f, 0.878f, 0.500f); // #70707066 / Blue tint
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.239f, 0.522f, 0.878f, 0.800f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.180f, 0.450f, 0.800f, 1.000f);
    colors[ImGuiCol_Header]                = ImVec4(0.631f, 0.631f, 0.702f, 0.310f); // #A1A1B34F
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.631f, 0.631f, 0.702f, 0.400f); // #A1A1B366
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.631f, 0.631f, 0.702f, 0.310f); // #A1A1B34F
    colors[ImGuiCol_Separator]             = ImVec4(0.431f, 0.431f, 0.502f, 0.502f); // #6E6E8080
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.722f, 0.722f, 0.722f, 0.780f); // #B8B8B8C7
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.510f, 0.510f, 0.510f, 1.00f);  // #828282FF
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.910f, 0.910f, 0.910f, 0.251f); // #E8E8E840
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.812f, 0.812f, 0.812f, 0.671f); // #CFCFCFAB
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.459f, 0.459f, 0.459f, 0.949f); // #757575F2
    colors[ImGuiCol_PlotLines]             = ImVec4(0.400f, 0.902f, 1.000f, 1.00f);  // #66E6FFFF
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.000f, 0.431f, 0.349f, 1.00f);  // #FF6E59FF
    colors[ImGuiCol_Tab]                   = ImVec4(0.150f, 0.160f, 0.170f, 0.800f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.239f, 0.522f, 0.878f, 0.800f);
    colors[ImGuiCol_TabSelected]           = ImVec4(0.239f, 0.522f, 0.878f, 1.000f);
}

void AppGui::init_d3d_resources(ID3D11Device* device) {
    d3d_device_ = device;
    record_panel_.init_d3d_resources(device);
    analyze_panel_.init_d3d_resources(device);
}

void AppGui::release_d3d_resources() {
    record_panel_.release_d3d_resources();
    analyze_panel_.release_d3d_resources();
    d3d_device_ = nullptr;
}

size_t AppGui::get_process_memory_mb() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
    return 0;
}

void AppGui::render_top_navigation_bar(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.045f, 0.055f, 1.0f));
    ImGui::BeginChild("TopNavigationBar", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);

    // Title / Brand
    ImGui::SetCursorPos(ImVec2(12, 8));
    ImGui::TextColored(ImVec4(0.40f, 0.902f, 1.0f, 1.0f), "HYDROGEN LINE SUITE v2.0");
    ImGui::SameLine();
    ImGui::TextDisabled("| SatDump-Style Unified C++ Astronomy Suite");

    // Mode Switcher Tabs centered / aligned
    ImGui::SameLine(ImGui::GetWindowWidth() * 0.40f);

    bool is_record = (state_.get_section() == AppSection::Record);
    bool is_analyze = (state_.get_section() == AppSection::Analyze);

    // Section 1: Live SDR / Record
    if (is_record) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.239f, 0.522f, 0.878f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.21f, 0.23f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.75f, 0.80f, 1.0f));
    }
    if (ImGui::Button("  📡 1. Live SDR / Record  ", ImVec2(190, 28))) {
        state_.set_section(AppSection::Record);
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0, 8);

    // Section 2: Offline Analysis
    if (is_analyze) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.239f, 0.522f, 0.878f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.21f, 0.23f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.75f, 0.80f, 1.0f));
    }
    if (ImGui::Button("  🔬 2. Offline Analysis  ", ImVec2(190, 28))) {
        state_.set_section(AppSection::Analyze);
    }
    ImGui::PopStyleColor(2);

    // Right-hand status badges
    ImGui::SameLine(ImGui::GetWindowWidth() - 250);
    ImGui::SetCursorPosY(8);
    if (writer_.is_recording()) {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "● REC ACTIVE");
        ImGui::SameLine(0, 12);
    }
    if (sdr_.is_streaming()) {
        ImGui::TextColored(ImVec4(0.18f, 0.84f, 0.45f, 1.0f), "● SDR LIVE");
        ImGui::SameLine(0, 12);
    }
    if (analyze_panel_.is_analyzing()) {
        ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "● ANALYZING");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AppGui::render_status_bar(float width, float height) {
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - height);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.045f, 0.055f, 1.0f));
    ImGui::BeginChild("StatusBar", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);

    float fps = ImGui::GetIO().Framerate;
    double dsp_msps = dsp_.get_dsp_throughput_msps();
    double latency_us = dsp_.get_last_latency_us();
    float ring_pct = sdr_.get_ring_buffer_fill_pct();
    uint64_t drops = sdr_.get_dropped_samples_count();
    const char* simd_name = SimdDsp::get_simd_level_name();
    size_t mem_mb = get_process_memory_mb();

    ImGui::SetCursorPosY(4);
    ImGui::Text(" GUI: %.1f FPS | SIMD: %s", fps, simd_name);

    ImGui::SameLine(0, 20);
    ImGui::Text("DSP: %.2f MSPS (%.1f μs)", dsp_msps, latency_us);

    ImGui::SameLine(0, 20);
    ImVec4 ring_col = (ring_pct < 50.0f) ? ImVec4(0.0f, 1.0f, 0.82f, 1.0f) :
                      (ring_pct < 80.0f) ? ImVec4(1.0f, 0.675f, 0.071f, 1.0f) :
                                           ImVec4(0.85f, 0.2f, 0.2f, 1.0f);
    ImGui::Text("Ring Buffer: ");
    ImGui::SameLine(0, 0);
    ImGui::TextColored(ring_col, "%.1f%%", ring_pct);

    ImGui::SameLine(0, 20);
    if (drops == 0) {
        ImGui::TextColored(ImVec4(0.18f, 0.84f, 0.45f, 1.0f), "Drops: 0");
    } else {
        ImGui::TextColored(ImVec4(0.85f, 0.2f, 0.2f, 1.0f), "Drops: %llu", static_cast<unsigned long long>(drops));
    }

    ImGui::SameLine(0, 20);
    ImGui::Text("RAM: %zu MB", mem_mb);

    ImGui::SameLine(0, 24);
    const char* sec_str = (state_.get_section() == AppSection::Record) ? "Mode: Live SDR / Record" : "Mode: Offline Analysis";
    ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "[ %s ]", sec_str);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AppGui::render_frame(ID3D11DeviceContext* context, float display_w, float display_h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(display_w, display_h));
    ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("UnifiedMainLayout", nullptr, win_flags);

    // 1. TOP NAVIGATION HEADER (38 px)
    const float top_nav_h = 38.0f;
    render_top_navigation_bar(display_w, top_nav_h);

    // 2. BOTTOM STATUS BAR (24 px)
    const float status_bar_h = 24.0f;
    const float content_h = display_h - top_nav_h - status_bar_h - 10.0f;

    // 3. SECTION CONTENT CANVAS
    ImGui::SetCursorPosY(top_nav_h + 4.0f);
    if (state_.get_section() == AppSection::Record) {
        record_panel_.render_panel(context, display_w, content_h);
    } else {
        analyze_panel_.render_panel(context, display_w, content_h);
    }

    // 4. UNIFIED STATUS BAR AT BOTTOM
    render_status_bar(display_w, status_bar_h);

    ImGui::End();
}

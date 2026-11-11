#include "app_gui.h"
#include "../lib/imgui/imgui.h"
#include "constants.h"
#include "simd_dsp.h"

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

static std::string format_freq_segmented(uint64_t freq_hz) {
    uint64_t ghz = freq_hz / 1000000000ULL;
    uint64_t mhz = (freq_hz % 1000000000ULL) / 1000000ULL;
    uint64_t khz = (freq_hz % 1000000ULL) / 1000ULL;
    uint64_t hz = freq_hz % 1000ULL;
    char buf[64];
    snprintf(buf, sizeof(buf), "%03llu.%03llu.%03llu.%03llu Hz",
             (unsigned long long)ghz, (unsigned long long)mhz,
             (unsigned long long)khz, (unsigned long long)hz);
    return std::string(buf);
}

static uint64_t parse_freq_str(const std::string& input) {
    std::string s = input;
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == ',';
    }), s.end());

    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)::tolower(c); });
    if (s.empty() || s[0] == '-') return 0;

    double mul = 1.0;
    if (s.find("ghz") != std::string::npos || s.find("g") != std::string::npos) {
        mul = 1e9;
    } else if (s.find("mhz") != std::string::npos || s.find("m") != std::string::npos) {
        mul = 1e6;
    } else if (s.find("khz") != std::string::npos || s.find("k") != std::string::npos) {
        mul = 1e3;
    }

    double val = 0.0;
    if (sscanf(s.c_str(), "%lf", &val) == 1 && val > 0.0) {
        if (mul == 1.0 && val < 10000.0) {
            val *= 1e6; // Default to MHz if small number
        } else {
            val *= mul;
        }
        return static_cast<uint64_t>(std::clamp(val, 100000.0, 3000000000.0));
    }
    return 0;
}

AppGui::AppGui(SdrController& controller, DspPipeline& dsp, Cs16Writer& writer, Waterfall& waterfall)
    : sdr_(controller), dsp_(dsp), writer_(writer), waterfall_(waterfall) {
    refresh_devices();
    setup_imgui_theme();
    last_stats_time_ = std::chrono::high_resolution_clock::now();
    last_waterfall_update_time_ = std::chrono::high_resolution_clock::now();

    // Set callback so whenever frequency changes, DSP averaging smoothing is immediately flushed
    sdr_.set_freq_changed_callback([this](uint64_t /*new_freq*/) {
        dsp_.flush();
    });
}

AppGui::~AppGui() {
    release_d3d_resources();
}

void AppGui::setup_imgui_theme() {
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

    // SatDump Dark Theme Palette (C:\Code\SatDump\share\satdump\resources\themes\Dark.json)
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // #FFFFFFFF
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // #808080FF
    colors[ImGuiCol_WindowBg]              = ImVec4(0.059f, 0.059f, 0.059f, 0.941f); // #0F0F0FF0
    colors[ImGuiCol_ChildBg]               = ImVec4(0.000f, 0.000f, 0.000f, 0.000f); // #FFFFFF00
    colors[ImGuiCol_PopupBg]               = ImVec4(0.078f, 0.078f, 0.078f, 0.941f); // #141414F0
    colors[ImGuiCol_Border]                = ImVec4(0.431f, 0.431f, 0.502f, 0.502f); // #6E6E8080
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.200f, 0.212f, 0.220f, 0.541f); // #3336388A
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.200f, 0.212f, 0.220f, 0.541f); // #3336388A
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.200f, 0.212f, 0.220f, 0.541f); // #3336388A
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
    colors[ImGuiCol_Button]                = ImVec4(0.439f, 0.439f, 0.439f, 0.400f); // #70707066
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.439f, 0.439f, 0.439f, 0.451f); // #70707073
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.439f, 0.439f, 0.439f, 0.400f); // #70707066
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
}

void AppGui::init_d3d_resources(ID3D11Device* device) {
    d3d_device_ = device;
    tex_width_ = waterfall_.get_width();
    tex_height_ = waterfall_.get_height();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = tex_width_;
    desc.Height = tex_height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    device->CreateTexture2D(&desc, nullptr, &waterfall_texture_);
    if (waterfall_texture_) {
        device->CreateShaderResourceView(waterfall_texture_, nullptr, &waterfall_srv_);
    }
}

void AppGui::release_d3d_resources() {
    if (waterfall_srv_) {
        waterfall_srv_->Release();
        waterfall_srv_ = nullptr;
    }
    if (waterfall_texture_) {
        waterfall_texture_->Release();
        waterfall_texture_ = nullptr;
    }
    d3d_device_ = nullptr;
}

void AppGui::update_waterfall_texture(ID3D11DeviceContext* context) {
    if (!context || !d3d_device_) return;

    if (waterfall_.get_width() != tex_width_ || waterfall_.get_height() != tex_height_) {
        release_d3d_resources();
        init_d3d_resources(d3d_device_);
    }

    if (!waterfall_texture_) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(waterfall_texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        const uint32_t* src = waterfall_.get_pixel_data();
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        int row_bytes = tex_width_ * sizeof(uint32_t);

        for (int y = 0; y < tex_height_; ++y) {
            std::memcpy(dst + y * mapped.RowPitch, src + y * tex_width_, row_bytes);
        }
        context->Unmap(waterfall_texture_, 0);
    }
}

void AppGui::refresh_devices() {
    auto blocking = sdr_.check_blocking_apps();
    device_list_ = sdr_.scan_all_devices();

    if (!blocking.empty()) {
        std::string procs;
        for (size_t i = 0; i < blocking.size(); ++i) {
            if (i > 0) procs += ", ";
            procs += blocking[i];
        }
        device_status_text_ = "Device held by " + procs + " (Virtual mode)";
        device_status_color_ = 0xFFFFAC12; // SatDump Amber
    } else if (!sdr_.is_simulated()) {
        device_status_text_ = "Connected: " + sdr_.get_device_name();
        device_status_color_ = 0xFF00FFD2; // Electric Cyan/Green
    } else {
        device_status_text_ = "Virtual SDR Active (Multi-band simulation)";
        device_status_color_ = 0xFFFFAC12; // SatDump Amber
    }
}

void AppGui::set_frequency(uint64_t freq_hz) {
    uint64_t safe_freq = std::max(static_cast<uint64_t>(1000000ULL), freq_hz);
    sdr_.request_frequency(safe_freq);
    dsp_.flush();

    snprintf(tune_input_buf_, sizeof(tune_input_buf_), "%.3f", safe_freq / 1e6);
}

void AppGui::step_frequency(int64_t delta_hz) {
    int64_t current = static_cast<int64_t>(sdr_.get_current_frequency());
    set_frequency(static_cast<uint64_t>(std::max(static_cast<int64_t>(1000000), current + delta_hz)));
}

void AppGui::launch_offline_analyzer() {
    std::string path = writer_.get_current_filepath();
    if (path.empty()) {
        path = "recordings";
    }

    std::wstring cmd = L"python Analyzer\\run.py";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    CreateProcessW(nullptr, const_cast<LPWSTR>(cmd.c_str()), nullptr, nullptr, FALSE,
                   CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
}

void AppGui::render_frame(ID3D11DeviceContext* context, float display_w, float display_h) {
    // 1. Ingest DSP spectrum
    uint64_t fc = sdr_.get_current_frequency();
    uint32_t fs = sdr_.get_current_sample_rate();
    bool got_spec = dsp_.get_power_spectrum(spectrum_data_, peak_freq_hz_, peak_dbfs_, fc, fs);

    // 2. High-FPS Throttled Waterfall update
    auto now = std::chrono::high_resolution_clock::now();
    double wf_elapsed = std::chrono::duration<double>(now - last_waterfall_update_time_).count();
    double min_wf_interval = (waterfall_rate_fps_ > 0.0f) ? (1.0 / waterfall_rate_fps_) : 0.033;
    if (got_spec && show_waterfall_ && wf_elapsed >= min_wf_interval) {
        waterfall_.add_line(spectrum_data_, fft_min_db_, fft_max_db_);
        update_waterfall_texture(context);
        last_waterfall_update_time_ = now;
    }

    // 3. Periodic stats update
    double stats_elapsed = std::chrono::duration<double>(now - last_stats_time_).count();
    if (stats_elapsed >= 0.25) {
        last_stats_ = writer_.get_stats();
        last_stats_time_ = now;
    }

    // 4. Main layout docking / splitting
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(display_w, display_h));
    ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("MainLayout", nullptr, win_flags);

    // TOP RIBBON / TOOLBAR
    render_top_ribbon();

    // BOTTOM STATUS BAR (Calculated height: 26px)
    const float status_bar_h = 26.0f;
    const float content_h = display_h - ImGui::GetCursorPosY() - status_bar_h - 4.0f;

    // SIDEBAR & DISPLAY SPLIT
    const float sidebar_width = 345.0f;
    const float display_width = display_w - sidebar_width - 12.0f;

    // Left Column: Controls & Telemetry Sidebar
    ImGui::BeginChild("Sidebar", ImVec2(sidebar_width, content_h), true);
    render_sidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Column: Spectrum & Waterfall Display Viewport
    ImGui::BeginChild("DisplayArea", ImVec2(display_width, content_h), false);
    render_display_panel(context, display_width, content_h);
    ImGui::EndChild();

    // Bottom Ribbon Status Bar
    render_status_bar(display_w, status_bar_h);

    ImGui::End();
}

void AppGui::render_top_ribbon() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

    // 1. SDR Streaming Button
    bool streaming = sdr_.is_streaming();
    if (streaming) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.20f, 1.0f)); // Red
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
        if (ImGui::Button("■ Stop SDR", ImVec2(95, 32))) {
            sdr_.stop_streaming();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.65f, 0.40f, 1.0f)); // Green
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.75f, 0.48f, 1.0f));
        if (ImGui::Button("▶ Start SDR", ImVec2(95, 32))) {
            sdr_.start_streaming();
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();

    // 2. Device Dropdown & Refresh
    ImGui::SetNextItemWidth(170);
    const char* preview_dev = device_list_.empty() ? "None" : device_list_[selected_device_idx_].c_str();
    if (ImGui::BeginCombo("##TopDeviceSelect", preview_dev)) {
        for (int i = 0; i < static_cast<int>(device_list_.size()); ++i) {
            bool is_selected = (selected_device_idx_ == i);
            if (ImGui::Selectable(device_list_[i].c_str(), is_selected)) {
                selected_device_idx_ = i;
                sdr_.initialize(false, static_cast<uint32_t>(i));
                refresh_devices();
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("⟳##TopRefresh", ImVec2(28, 32))) {
        refresh_devices();
    }

    ImGui::SameLine();

    // 3. Digital Segmented Frequency Readout
    uint64_t cur_freq = sdr_.get_current_frequency();
    std::string seg_str = format_freq_segmented(cur_freq);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.05f, 0.06f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::BeginChild("TopDigitalFreqBox", ImVec2(230, 32), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(6);
    ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.82f, 1.0f), "%s", seg_str.c_str());

    // Mouse wheel tuning over digital frequency readout
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0) step_frequency(100000);
        else if (wheel < 0) step_frequency(-100000);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // 4. Quick Step Buttons
    const float step_w = 42.0f;
    if (ImGui::Button("-1M", ImVec2(step_w, 32))) step_frequency(-1000000);
    ImGui::SameLine(0, 3);
    if (ImGui::Button("+1M", ImVec2(step_w, 32))) step_frequency(1000000);
    ImGui::SameLine(0, 3);
    if (ImGui::Button("-100k", ImVec2(step_w, 32))) step_frequency(-100000);
    ImGui::SameLine(0, 3);
    if (ImGui::Button("+100k", ImVec2(step_w, 32))) step_frequency(100000);
    ImGui::SameLine(0, 3);
    if (ImGui::Button("-10k", ImVec2(step_w, 32))) step_frequency(-10000);
    ImGui::SameLine(0, 3);
    if (ImGui::Button("+10k", ImVec2(step_w, 32))) step_frequency(10000);

    ImGui::SameLine();

    // 5. Presets Dropdown
    const auto& presets = SdrConstants::GetPresets();
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("##TopPresetsCombo", presets[selected_preset_idx_].name)) {
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            bool is_sel = (selected_preset_idx_ == i);
            if (ImGui::Selectable(presets[i].name, is_sel)) {
                selected_preset_idx_ = i;
                set_frequency(presets[i].freq_hz);
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();

    // 6. Bias-Tee Toggle Button with Active LED Styling
    if (bias_tee_enabled_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.05f, 1.0f)); // Glowing Amber
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.65f, 0.15f, 1.0f));
        if (ImGui::Button("⚡ Bias-Tee [ON]", ImVec2(110, 32))) {
            bias_tee_enabled_ = false;
            sdr_.request_bias_tee(false);
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.24f, 0.85f));
        if (ImGui::Button("⚡ Bias-Tee [OFF]", ImVec2(110, 32))) {
            bias_tee_enabled_ = true;
            sdr_.request_bias_tee(true);
        }
        ImGui::PopStyleColor();
    }

    ImGui::PopStyleVar();
    ImGui::Separator();
}

void AppGui::render_sidebar() {
    render_device_panel();
    ImGui::Spacing();
    render_fft_panel();
    ImGui::Spacing();
    render_telemetry_panel();
    ImGui::Spacing();
    render_recording_panel();
}

void AppGui::render_device_panel() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.239f, 0.522f, 0.878f, 1.00f)); // SatDump Blue
    bool open = ImGui::CollapsingHeader("▼ Radio & Device", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    // Status note
    ImVec4 col;
    col.x = ((device_status_color_ >> 16) & 0xFF) / 255.0f;
    col.y = ((device_status_color_ >> 8) & 0xFF) / 255.0f;
    col.z = (device_status_color_ & 0xFF) / 255.0f;
    col.w = 1.0f;
    ImGui::TextColored(col, "%s", device_status_text_.c_str());

    // Direct tuning input row
    ImGui::Text("Tune:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    bool enter_pressed = ImGui::InputText("##DirectTune", tune_input_buf_, sizeof(tune_input_buf_),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::Text("MHz");
    ImGui::SameLine();
    if (ImGui::Button("Set##Tune") || enter_pressed) {
        uint64_t parsed = parse_freq_str(tune_input_buf_);
        if (parsed > 0) {
            set_frequency(parsed);
        }
    }

    // Samplerate dropdown
    const auto& rates = SdrConstants::GetSampleRates();
    ImGui::Text("Sample Rate:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##SampleRateCombo", rates[selected_sample_rate_idx_].label)) {
        for (int i = 0; i < static_cast<int>(rates.size()); ++i) {
            bool is_sel = (selected_sample_rate_idx_ == i);
            if (ImGui::Selectable(rates[i].label, is_sel)) {
                selected_sample_rate_idx_ = i;
                sdr_.request_sample_rate(rates[i].rate_hz);
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // PPM correction spin
    ImGui::Text("PPM Correction:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##PPM", &ppm_correction_, 1, 5)) {
        ppm_correction_ = std::clamp(ppm_correction_, -100, 100);
        sdr_.request_ppm(ppm_correction_);
    }

    // LNA Gain slider
    char gain_str[32];
    snprintf(gain_str, sizeof(gain_str), "%.1f dB", lna_gain_db_);
    ImGui::Text("Tuner LNA Gain:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::TextColored(ImVec4(0.239f, 0.522f, 0.878f, 1.0f), "%s", gain_str);

    if (ImGui::SliderFloat("##GainSlider", &lna_gain_db_, 0.0f, 49.6f, "%.1f dB")) {
        if (!agc_enabled_) {
            sdr_.request_gain(lna_gain_db_, false);
        }
    }

    // AGC Checkbox
    if (ImGui::Checkbox("Hardware AGC", &agc_enabled_)) {
        sdr_.request_gain(lna_gain_db_, agc_enabled_);
    }
}

void AppGui::render_fft_panel() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.239f, 0.522f, 0.878f, 1.00f));
    bool open = ImGui::CollapsingHeader("▼ FFT & Display", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    // FFT Size
    const auto& fft_sizes = SdrConstants::GetFftSizes();
    char fft_lbl[32];
    snprintf(fft_lbl, sizeof(fft_lbl), "%d", fft_sizes[selected_fft_size_idx_]);
    ImGui::Text("FFT Size:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##FftSizeCombo", fft_lbl)) {
        for (int i = 0; i < static_cast<int>(fft_sizes.size()); ++i) {
            bool is_sel = (selected_fft_size_idx_ == i);
            char item_lbl[32];
            snprintf(item_lbl, sizeof(item_lbl), "%d", fft_sizes[i]);
            if (ImGui::Selectable(item_lbl, is_sel)) {
                selected_fft_size_idx_ = i;
                dsp_.set_fft_size(fft_sizes[i]);
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // FFT Max & Min dB
    ImGui::Text("FFT Max (dB):");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%.0f dB", fft_max_db_);
    ImGui::SliderFloat("##FftMax", &fft_max_db_, -20.0f, -70.0f, "%.0f dB");

    ImGui::Text("FFT Min (dB):");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%.0f dB", fft_min_db_);
    ImGui::SliderFloat("##FftMin", &fft_min_db_, -80.0f, -130.0f, "%.0f dB");

    // Averaging
    int avg_pct = static_cast<int>(avg_factor_ * 100.0f);
    ImGui::Text("Averaging:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%d%%", avg_pct);
    if (ImGui::SliderFloat("##Averaging", &avg_factor_, 0.0f, 0.95f, "%.2f")) {
        dsp_.set_averaging(avg_factor_);
    }

    // Palette
    const auto& palettes = Waterfall::get_palette_names();
    ImGui::Text("Palette:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##PaletteCombo", palettes[selected_palette_idx_].c_str())) {
        for (int i = 0; i < static_cast<int>(palettes.size()); ++i) {
            bool is_sel = (selected_palette_idx_ == i);
            if (ImGui::Selectable(palettes[i].c_str(), is_sel)) {
                selected_palette_idx_ = i;
                waterfall_.set_palette(static_cast<WaterfallPalette>(i));
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Visual features
    ImGui::Checkbox("Show Waterfall", &show_waterfall_);
    ImGui::SameLine(160);
    ImGui::Checkbox("Spectrum Fill", &fill_spectrum_);

    // Waterfall rate
    ImGui::Text("Waterfall Rate:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%.0f FPS", waterfall_rate_fps_);
    ImGui::SliderFloat("##WfRate", &waterfall_rate_fps_, 10.0f, 60.0f, "%.0f FPS");
}

void AppGui::render_telemetry_panel() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.82f, 1.00f)); // Electric Cyan
    bool open = ImGui::CollapsingHeader("▼ Astronomy Telemetry", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    uint64_t cur_freq = sdr_.get_current_frequency();
    double offset_hz = static_cast<double>(cur_freq) - SdrConstants::HI_REST_FREQ_HZ;
    double offset_khz = offset_hz / 1e3;

    // LO Offset from HI 21cm line
    ImGui::Text("LO Offset (HI):");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
    if (std::abs(offset_hz / 1e6) < 10.0) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.82f, 1.0f), "%+.1f kHz", offset_khz);
    } else {
        ImGui::TextDisabled("N/A (>10 MHz)");
    }

    // Instantaneous Doppler Velocity estimate
    double doppler_v = SdrConstants::CalculateDopplerVelocityKmS(static_cast<double>(peak_freq_hz_));
    ImGui::Text("Doppler Velocity:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
    if (std::abs(offset_hz / 1e6) < 5.0) {
        ImGui::TextColored(ImVec4(1.0f, 0.675f, 0.071f, 1.0f), "%+.1f km/s", doppler_v);
    } else {
        ImGui::TextDisabled("Out of Band");
    }

    // Live SNR above noise floor
    float snr = dsp_.get_snr_db();
    float noise_floor = dsp_.get_noise_floor_db();

    ImGui::Text("Estimated SNR:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
    ImGui::TextColored(ImVec4(0.18f, 0.84f, 0.45f, 1.0f), "+%.1f dB", snr);

    ImGui::Text("Noise Floor:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
    ImGui::TextDisabled("%.1f dBFS", noise_floor);
}

void AppGui::render_recording_panel() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.239f, 0.522f, 0.878f, 1.00f));
    bool open = ImGui::CollapsingHeader("▼ Recording & Storage", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    // Format
    ImGui::SetNextItemWidth(100);
    const char* formats[] = {"cs16 (16-bit)", "cu8 (8-bit)"};
    if (ImGui::BeginCombo("##FormatCombo", formats[selected_format_idx_])) {
        for (int i = 0; i < 2; ++i) {
            bool is_sel = (selected_format_idx_ == i);
            if (ImGui::Selectable(formats[i], is_sel)) {
                selected_format_idx_ = i;
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text("Format");

    // Metrics
    ImGui::Text("File Size : %s", last_stats_.size_str.c_str());
    ImGui::Text("Free Disk : %s", last_stats_.free_space_str.c_str());
    ImGui::Text("Time Left : %s", last_stats_.time_left_str.c_str());
    ImGui::TextDisabled("File: %s", last_stats_.filename.c_str());

    // Recording Controls with Glowing REC Badge
    if (writer_.is_recording()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.25f, 1.0f));
        char rec_lbl[64];
        snprintf(rec_lbl, sizeof(rec_lbl), "● REC [%s] Stop", last_stats_.elapsed_str.c_str());
        if (ImGui::Button(rec_lbl, ImVec2(-1, 32))) {
            writer_.stop_recording();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.65f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.75f, 0.48f, 1.0f));
        if (ImGui::Button("▶ Start CS16 Recording", ImVec2(-1, 32))) {
            writer_.start_recording(
                sdr_.get_current_sample_rate(),
                sdr_.get_current_frequency(),
                sdr_.get_device_name(),
                sdr_.get_current_gain(),
                sdr_.is_bias_tee_enabled()
            );
        }
        ImGui::PopStyleColor(2);
    }

    // Launch Offline Analyzer
    if (ImGui::Button("⚡ Open in Offline Analyzer", ImVec2(-1, 26))) {
        launch_offline_analyzer();
    }
}

void AppGui::render_display_panel(ID3D11DeviceContext* context, float width, float height) {
    float spec_h = show_waterfall_ ? (height * 0.55f) : (height - 10.0f);
    float wf_h = show_waterfall_ ? (height - spec_h - 10.0f) : 0.0f;

    render_spectrum_plot(width, spec_h);

    if (show_waterfall_) {
        render_waterfall_plot(context, width, wf_h);
    }
}

void AppGui::render_spectrum_plot(float width, float height) {
    ImGui::BeginChild("SpectrumChild", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + width - 12, p0.y + height - 10);

    // Padding
    float pad_l = 60.0f, pad_r = 15.0f, pad_t = 25.0f, pad_b = 26.0f;
    float plot_x0 = p0.x + pad_l;
    float plot_x1 = p1.x - pad_r;
    float plot_y0 = p0.y + pad_t;
    float plot_y1 = p1.y - pad_b;
    float plot_w = plot_x1 - plot_x0;
    float plot_h = plot_y1 - plot_y0;

    if (plot_w < 50.0f || plot_h < 50.0f) {
        ImGui::EndChild();
        return;
    }

    // 1. Sleek Background & Frame
    draw_list->AddRectFilled(ImVec2(plot_x0, plot_y0), ImVec2(plot_x1, plot_y1), IM_COL32(15, 15, 18, 255));
    draw_list->AddRect(ImVec2(plot_x0, plot_y0), ImVec2(plot_x1, plot_y1), IM_COL32(51, 54, 56, 180));

    // 2. Horizontal dBFS Grid Lines
    float db_range = std::max(fft_max_db_ - fft_min_db_, 10.0f);
    for (float db = -130.0f; db <= -20.0f; db += 10.0f) {
        if (db >= fft_min_db_ && db <= fft_max_db_) {
            float y = plot_y0 + plot_h * (1.0f - (db - fft_min_db_) / db_range);
            draw_list->AddLine(ImVec2(plot_x0, y), ImVec2(plot_x1, y), IM_COL32(255, 255, 255, 25));
            char db_lbl[16];
            snprintf(db_lbl, sizeof(db_lbl), "%.0f dB", db);
            draw_list->AddText(ImVec2(p0.x + 8, y - 7), IM_COL32(110, 110, 128, 220), db_lbl);
        }
    }

    // 3. Vertical Frequency Grid Lines
    uint64_t fc = sdr_.get_current_frequency();
    uint32_t fs = sdr_.get_current_sample_rate();
    double start_f = static_cast<double>(fc) - (static_cast<double>(fs) / 2.0);
    double span_f = static_cast<double>(fs);
    int n_divs = 8;
    for (int d = 0; d <= n_divs; ++d) {
        float x = plot_x0 + (plot_w * static_cast<float>(d) / static_cast<float>(n_divs));
        draw_list->AddLine(ImVec2(x, plot_y0), ImVec2(x, plot_y1), IM_COL32(255, 255, 255, 25));
        if (show_freq_scale_) {
            double f_cur = start_f + (span_f * static_cast<double>(d) / static_cast<double>(n_divs));
            char f_lbl[32];
            snprintf(f_lbl, sizeof(f_lbl), "%.3f", f_cur / 1e6);
            draw_list->AddText(ImVec2(x - 20, plot_y1 + 6), IM_COL32(110, 110, 128, 220), f_lbl);
        }
    }

    // 4. Center LO Marker (SatDump Amber dashed line)
    float dc_x = plot_x0 + plot_w * 0.5f;
    draw_list->AddLine(ImVec2(dc_x, plot_y0), ImVec2(dc_x, plot_y1), IM_COL32(255, 172, 18, 140), 1.0f);
    draw_list->AddText(ImVec2(dc_x + 4, plot_y1 - 18), IM_COL32(255, 172, 18, 180), "LO Center");

    // 5. HI Rest Line Marker (1420.405752 MHz) (Bright Cyan)
    if (SdrConstants::HI_REST_FREQ_HZ >= start_f && SdrConstants::HI_REST_FREQ_HZ <= (start_f + span_f)) {
        float hi_x = plot_x0 + plot_w * static_cast<float>((SdrConstants::HI_REST_FREQ_HZ - start_f) / span_f);
        draw_list->AddLine(ImVec2(hi_x, plot_y0), ImVec2(hi_x, plot_y1), IM_COL32(0, 255, 255, 220), 1.5f);
        draw_list->AddText(ImVec2(hi_x + 4, plot_y0 + 4), IM_COL32(0, 255, 255, 240), "H I 1420.406 MHz");
    }

    // 6. Spectrum Curve & SatDump Gradient Area Fill
    if (!spectrum_data_.empty()) {
        const size_t n_pts = spectrum_data_.size();
        size_t step = std::max(static_cast<size_t>(1), n_pts / static_cast<size_t>(plot_w));

        ImVec2 prev_pt;
        bool first = true;

        for (size_t i = 0; i < n_pts; i += step) {
            float frac = static_cast<float>(i) / static_cast<float>(n_pts - 1);
            float x = plot_x0 + plot_w * frac;
            float val = spectrum_data_[i];
            float norm = std::clamp((val - fft_min_db_) / db_range, 0.0f, 1.0f);
            float y = plot_y0 + plot_h * (1.0f - norm);

            ImVec2 cur_pt(x, y);

            if (!first) {
                // Gradient Fill Under Curve: True trapezoid primitive with zero vertex leakage
                if (fill_spectrum_) {
                    ImVec2 p_br(cur_pt.x, plot_y1);
                    ImVec2 p_bl(prev_pt.x, plot_y1);

                    // Top color: SatDump electric blue/cyan; Bottom color: deep sleek navy/dark
                    ImU32 col_top = IM_COL32(0, 120, 215, 60);
                    ImU32 col_bot = IM_COL32(15, 18, 22, 10);

                    // True trapezoid quad: prev_pt, cur_pt, p_br, p_bl
                    draw_list->PrimReserve(6, 4);
                    ImDrawIdx idx0 = static_cast<ImDrawIdx>(draw_list->_VtxCurrentIdx);
                    draw_list->PrimWriteIdx(idx0);
                    draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(idx0 + 1));
                    draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(idx0 + 2));
                    draw_list->PrimWriteIdx(idx0);
                    draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(idx0 + 2));
                    draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(idx0 + 3));

                    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
                    draw_list->PrimWriteVtx(prev_pt, uv, col_top);
                    draw_list->PrimWriteVtx(cur_pt, uv, col_top);
                    draw_list->PrimWriteVtx(p_br, uv, col_bot);
                    draw_list->PrimWriteVtx(p_bl, uv, col_bot);
                }

                // Crisp electric cyan plot line (#66E6FFFF)
                draw_list->AddLine(prev_pt, cur_pt, IM_COL32(102, 230, 255, 255), 1.8f);
            }

            prev_pt = cur_pt;
            first = false;
        }
    }

    // 7. Interactive Spectrum Reticle Cursor & Floating HUD Tooltip
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool mouse_in_plot = (mouse_pos.x >= plot_x0 && mouse_pos.x <= plot_x1 &&
                          mouse_pos.y >= plot_y0 && mouse_pos.y <= plot_y1);

    if (mouse_in_plot && !spectrum_data_.empty()) {
        // Vertical Reticle Line
        draw_list->AddLine(ImVec2(mouse_pos.x, plot_y0), ImVec2(mouse_pos.x, plot_y1),
                           IM_COL32(255, 255, 255, 110), 1.0f);

        // Horizontal Reticle Line at cursor Y
        draw_list->AddLine(ImVec2(plot_x0, mouse_pos.y), ImVec2(plot_x1, mouse_pos.y),
                           IM_COL32(255, 255, 255, 65), 1.0f);

        // Frequency at cursor
        float cursor_frac = std::clamp((mouse_pos.x - plot_x0) / plot_w, 0.0f, 1.0f);
        double f_cursor = start_f + static_cast<double>(cursor_frac) * span_f;
        double offset_khz = (f_cursor - static_cast<double>(fc)) / 1e3;

        // dBFS at cursor Y level
        float cursor_norm_y = 1.0f - std::clamp((mouse_pos.y - plot_y0) / plot_h, 0.0f, 1.0f);
        float cursor_dbfs = fft_min_db_ + cursor_norm_y * db_range;

        // Sample power at cursor frequency bin
        size_t spec_idx = static_cast<size_t>(std::clamp(
            static_cast<int>(cursor_frac * (spectrum_data_.size() - 1)),
            0,
            static_cast<int>(spectrum_data_.size() - 1)
        ));
        float pwr_at_cursor = spectrum_data_[spec_idx];

        // Reticle intersection bead on the spectrum curve
        float curve_norm = std::clamp((pwr_at_cursor - fft_min_db_) / db_range, 0.0f, 1.0f);
        float curve_y = plot_y0 + plot_h * (1.0f - curve_norm);
        draw_list->AddCircleFilled(ImVec2(mouse_pos.x, curve_y), 4.0f, IM_COL32(0, 255, 255, 255));
        draw_list->AddCircle(ImVec2(mouse_pos.x, curve_y), 6.5f, IM_COL32(255, 255, 255, 200), 12, 1.5f);

        // Format Floating HUD Tooltip
        char tip_buf[192];
        if (std::abs(f_cursor - SdrConstants::HI_REST_FREQ_HZ) < 5e6) {
            double v_dopp = SdrConstants::CalculateDopplerVelocityKmS(f_cursor);
            snprintf(tip_buf, sizeof(tip_buf),
                     "%.4f MHz (%+.1f kHz)\nSignal: %.1f dBFS (Level: %.1f dBFS)\nDoppler: %+.1f km/s",
                     f_cursor / 1e6, offset_khz, pwr_at_cursor, cursor_dbfs, v_dopp);
        } else {
            snprintf(tip_buf, sizeof(tip_buf),
                     "%.4f MHz (%+.1f kHz)\nSignal: %.1f dBFS (Level: %.1f dBFS)",
                     f_cursor / 1e6, offset_khz, pwr_at_cursor, cursor_dbfs);
        }

        ImVec2 tip_sz = ImGui::CalcTextSize(tip_buf);
        // Anti-flicker boundary clamping: keep tooltip strictly within plot canvas
        ImVec2 tip_pos(mouse_pos.x + 14, mouse_pos.y - tip_sz.y - 10);
        if (tip_pos.x + tip_sz.x + 10 > plot_x1) {
            tip_pos.x = mouse_pos.x - tip_sz.x - 14;
        }
        if (tip_pos.x < plot_x0 + 4) {
            tip_pos.x = plot_x0 + 4;
        }
        if (tip_pos.y < plot_y0 + 4) {
            tip_pos.y = mouse_pos.y + 14;
        }
        if (tip_pos.y + tip_sz.y + 10 > plot_y1) {
            tip_pos.y = plot_y1 - tip_sz.y - 10;
        }

        draw_list->AddRectFilled(ImVec2(tip_pos.x - 6, tip_pos.y - 4),
                                ImVec2(tip_pos.x + tip_sz.x + 6, tip_pos.y + tip_sz.y + 4),
                                IM_COL32(15, 18, 22, 235), 3.0f);
        draw_list->AddRect(ImVec2(tip_pos.x - 6, tip_pos.y - 4),
                          ImVec2(tip_pos.x + tip_sz.x + 6, tip_pos.y + tip_sz.y + 4),
                          IM_COL32(0, 255, 255, 180), 3.0f);
        draw_list->AddText(tip_pos, IM_COL32(230, 240, 255, 255), tip_buf);
    }

    // 8. Top-Right Peak HUD Indicator Badge
    char hud_buf[128];
    snprintf(hud_buf, sizeof(hud_buf), "Peak: %.4f MHz | %.1f dBFS",
             peak_freq_hz_ / 1e6, peak_dbfs_);
    ImVec2 hud_size = ImGui::CalcTextSize(hud_buf);
    draw_list->AddRectFilled(ImVec2(plot_x1 - hud_size.x - 12, plot_y0 + 6),
                            ImVec2(plot_x1 - 4, plot_y0 + hud_size.y + 10),
                            IM_COL32(20, 25, 30, 200), 3.0f);
    draw_list->AddText(ImVec2(plot_x1 - hud_size.x - 8, plot_y0 + 8),
                      IM_COL32(102, 230, 255, 255), hud_buf);

    ImGui::EndChild();
}

void AppGui::render_waterfall_plot(ID3D11DeviceContext* /*context*/, float width, float height) {
    ImGui::BeginChild("WaterfallChild", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);
    if (waterfall_srv_) {
        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        ImGui::Image(reinterpret_cast<ImTextureID>(waterfall_srv_), ImVec2(width - 15, height - 10), uv0, uv1);
    }
    ImGui::EndChild();
}

void AppGui::render_status_bar(float width, float height) {
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - height);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
    ImGui::BeginChild("StatusBar", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);

    float fps = ImGui::GetIO().Framerate;
    double dsp_msps = dsp_.get_dsp_throughput_msps();
    double latency_us = dsp_.get_last_latency_us();
    float ring_pct = sdr_.get_ring_buffer_fill_pct();
    uint64_t drops = sdr_.get_dropped_samples_count();
    const char* simd_name = SimdDsp::get_simd_level_name();

    ImGui::SetCursorPosY(4);
    ImGui::Text(" GUI: %.1f FPS | SIMD: %s", fps, simd_name);

    ImGui::SameLine(0, 24);
    ImGui::Text("DSP: %.2f MSPS (%.1f μs)", dsp_msps, latency_us);

    ImGui::SameLine(0, 24);
    ImVec4 ring_col = (ring_pct < 50.0f) ? ImVec4(0.0f, 1.0f, 0.82f, 1.0f) :
                      (ring_pct < 80.0f) ? ImVec4(1.0f, 0.675f, 0.071f, 1.0f) :
                                           ImVec4(0.85f, 0.2f, 0.2f, 1.0f);
    ImGui::Text("Ring Buffer: ");
    ImGui::SameLine(0, 0);
    ImGui::TextColored(ring_col, "%.1f%%", ring_pct);

    ImGui::SameLine(0, 24);
    if (drops == 0) {
        ImGui::TextColored(ImVec4(0.18f, 0.84f, 0.45f, 1.0f), "Drops: 0");
    } else {
        ImGui::TextColored(ImVec4(0.85f, 0.2f, 0.2f, 1.0f), "Drops: %llu", static_cast<unsigned long long>(drops));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

#include "ui_record_panel.h"
#include "../lib/imgui/imgui.h"
#include "constants.h"
#include "simd_dsp.h"

#include <windows.h>
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

UiRecordPanel::UiRecordPanel(SdrController& controller, DspPipeline& dsp, Cs16Writer& writer, Waterfall& waterfall, AppState& app_state)
    : sdr_(controller), dsp_(dsp), writer_(writer), waterfall_(waterfall), app_state_(app_state) {
    refresh_devices();
    last_stats_time_ = std::chrono::high_resolution_clock::now();
    last_waterfall_update_time_ = std::chrono::high_resolution_clock::now();

    // Set callback so whenever frequency changes, DSP averaging smoothing is immediately flushed
    sdr_.set_freq_changed_callback([this](uint64_t /*new_freq*/) {
        dsp_.flush();
    });
}

UiRecordPanel::~UiRecordPanel() {
    release_d3d_resources();
}

void UiRecordPanel::init_d3d_resources(ID3D11Device* device) {
    if (!device) return;
    d3d_device_ = device;

    // Release any existing resources first to avoid D3D11 leaks
    release_d3d_resources();

    tex_width_ = waterfall_.get_width();
    tex_height_ = waterfall_.get_height();
    if (tex_width_ <= 0 || tex_height_ <= 0) return;

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

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &waterfall_texture_);
    if (SUCCEEDED(hr) && waterfall_texture_) {
        hr = device->CreateShaderResourceView(waterfall_texture_, nullptr, &waterfall_srv_);
        if (FAILED(hr)) {
            waterfall_texture_->Release();
            waterfall_texture_ = nullptr;
        }
    }
}

void UiRecordPanel::release_d3d_resources() {
    if (waterfall_srv_) {
        waterfall_srv_->Release();
        waterfall_srv_ = nullptr;
    }
    if (waterfall_texture_) {
        waterfall_texture_->Release();
        waterfall_texture_ = nullptr;
    }
    tex_width_ = 0;
    tex_height_ = 0;
}

void UiRecordPanel::update_waterfall_texture(ID3D11DeviceContext* context) {
    if (!context) return;

    // Recreate if dimensions changed
    if (waterfall_.get_width() != tex_width_ || waterfall_.get_height() != tex_height_) {
        if (d3d_device_) {
            init_d3d_resources(d3d_device_);
        }
    }

    if (!waterfall_texture_) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(waterfall_texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        const uint32_t* src = waterfall_.get_pixel_data();
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        size_t row_bytes = tex_width_ * sizeof(uint32_t);

        for (int y = 0; y < tex_height_; ++y) {
            std::memcpy(dst + y * mapped.RowPitch, src + y * tex_width_, row_bytes);
        }
        context->Unmap(waterfall_texture_, 0);
    }
}

void UiRecordPanel::refresh_devices() {
    auto blocking = sdr_.check_blocking_apps();
    device_list_ = sdr_.scan_all_devices();
    if (device_list_.empty()) {
        device_list_.push_back("Virtual SDR Simulator");
        selected_device_idx_ = 0;
    }

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

void UiRecordPanel::set_frequency(uint64_t freq_hz) {
    uint64_t safe_freq = std::max(static_cast<uint64_t>(1000000ULL), freq_hz);
    sdr_.request_frequency(safe_freq);
    dsp_.flush();

    snprintf(tune_input_buf_, sizeof(tune_input_buf_), "%.3f", safe_freq / 1e6);
}

void UiRecordPanel::step_frequency(int64_t delta_hz) {
    int64_t current = static_cast<int64_t>(sdr_.get_current_frequency());
    set_frequency(static_cast<uint64_t>(std::max(static_cast<int64_t>(1000000), current + delta_hz)));
}

void UiRecordPanel::render_panel(ID3D11DeviceContext* context, float width, float height) {
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

    // Panel Window Canvas
    ImGui::BeginChild("RecordPanelCanvas", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);

    // TOP RIBBON / TOOLBAR
    render_top_ribbon();

    const float content_h = height - ImGui::GetCursorPosY() - 2.0f;
    const float sidebar_width = 345.0f;
    const float display_width = width - sidebar_width - 12.0f;

    // Left Column: Controls & Telemetry Sidebar
    ImGui::BeginChild("RecordSidebar", ImVec2(sidebar_width, content_h), true);
    render_sidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Column: Spectrum & Waterfall Display Viewport
    ImGui::BeginChild("RecordDisplayArea", ImVec2(display_width, content_h), false);
    render_display_panel(context, display_width, content_h);
    ImGui::EndChild();

    ImGui::EndChild();
}

void UiRecordPanel::render_top_ribbon() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

    // 1. SDR Streaming Button
    bool streaming = sdr_.is_streaming();
    if (streaming) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.20f, 1.0f)); // Red
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
        if (ImGui::Button("■ Stop Streaming", ImVec2(130, 30))) {
            sdr_.stop_streaming();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.35f, 1.0f)); // Green
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.75f, 0.42f, 1.0f));
        if (ImGui::Button("▶ Start Streaming", ImVec2(130, 30))) {
            sdr_.start_streaming();
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();

    // 2. Bias-Tee Toggle
    if (bias_tee_enabled_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.60f, 0.07f, 1.0f)); // Amber ON
        if (ImGui::Button("⚡ Bias-Tee ON", ImVec2(105, 30))) {
            bias_tee_enabled_ = false;
            sdr_.request_bias_tee(false);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.28f, 0.30f, 1.0f)); // Dark OFF
        if (ImGui::Button("⚡ Bias-Tee", ImVec2(95, 30))) {
            bias_tee_enabled_ = true;
            sdr_.request_bias_tee(true);
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    // 3. Segmented Digital Frequency Readout
    uint64_t cur_freq = sdr_.get_current_frequency();
    std::string seg_text = format_freq_segmented(cur_freq);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.05f, 0.07f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.75f, 0.90f, 0.5f));
    ImGui::BeginChild("FreqDisplayFrame", ImVec2(245, 30), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(10, 5));
    ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.82f, 1.0f), "%s", seg_text.c_str()); // Electric Cyan
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    // 4. Frequency Step Buttons (Coarse & Fine)
    if (ImGui::Button("-1M", ImVec2(40, 30))) step_frequency(-1000000);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("-100k", ImVec2(48, 30))) step_frequency(-100000);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("-10k", ImVec2(42, 30))) step_frequency(-10000);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("-1k", ImVec2(36, 30))) step_frequency(-1000);
    ImGui::SameLine(0, 4);
    if (ImGui::Button("+1k", ImVec2(36, 30))) step_frequency(1000);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("+10k", ImVec2(42, 30))) step_frequency(10000);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("+100k", ImVec2(48, 30))) step_frequency(100000);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("+1M", ImVec2(40, 30))) step_frequency(1000000);

    ImGui::PopStyleVar();
    ImGui::Spacing();
}

void UiRecordPanel::render_sidebar() {
    render_device_panel();
    ImGui::Spacing();
    render_fft_panel();
    ImGui::Spacing();
    render_telemetry_panel();
    ImGui::Spacing();
    render_recording_panel();
}

void UiRecordPanel::render_device_panel() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.239f, 0.522f, 0.878f, 1.00f)); // SatDump Blue
    bool open = ImGui::CollapsingHeader("▼ Tuner & Device Settings", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    // Device selector dropdown
    ImGui::Text("Device:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65);
    const char* cur_dev_name = device_list_.empty() ? "None" : device_list_[selected_device_idx_].c_str();
    if (ImGui::BeginCombo("##DeviceCombo", cur_dev_name)) {
        for (int i = 0; i < static_cast<int>(device_list_.size()); ++i) {
            bool is_sel = (selected_device_idx_ == i);
            if (ImGui::Selectable(device_list_[i].c_str(), is_sel)) {
                selected_device_idx_ = i;
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(60, 0))) {
        refresh_devices();
    }

    // Status indicator
    ImGui::TextColored(ImColor(device_status_color_), "● %s", device_status_text_.c_str());

    // Frequency Presets
    const auto& presets = SdrConstants::GetPresets();
    ImGui::Text("Band Preset:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##PresetsCombo", presets[selected_preset_idx_].name)) {
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

    // Direct Frequency input
    ImGui::Text("Center LO:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 45);
    if (ImGui::InputText("##DirectFreq", tune_input_buf_, sizeof(tune_input_buf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        uint64_t parsed = parse_freq_str(tune_input_buf_);
        if (parsed > 0) {
            set_frequency(parsed);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Set", ImVec2(40, 0))) {
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

void UiRecordPanel::render_fft_panel() {
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

    // FFT Range dBFS
    ImGui::Text("Range Min dB:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%.1f dB", fft_min_db_);
    ImGui::SliderFloat("##MinDb", &fft_min_db_, -140.0f, -40.0f, "%.0f dB");

    ImGui::Text("Range Max dB:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%.1f dB", fft_max_db_);
    ImGui::SliderFloat("##MaxDb", &fft_max_db_, -80.0f, 0.0f, "%.0f dB");

    // Smoothing / Averaging
    int avg_pct = static_cast<int>(avg_factor_ * 100.0f);
    ImGui::Text("Averaging:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::Text("%d%%", avg_pct);
    if (ImGui::SliderFloat("##Averaging", &avg_factor_, 0.0f, 0.95f, "%.2f")) {
        dsp_.set_averaging(avg_factor_);
    }

    // Palettes
    const auto& palettes = Waterfall::get_palette_names();
    ImGui::Text("Waterfall Palette:");
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

void UiRecordPanel::render_telemetry_panel() {
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

void UiRecordPanel::render_recording_panel() {
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
            app_state_.set_recording_active(false, "");
            std::string rec = writer_.get_last_recorded_filepath();
            if (!rec.empty()) {
                app_state_.set_last_recorded_file(rec);
            }
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.65f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.75f, 0.48f, 1.0f));
        if (ImGui::Button("▶ Start CS16 Recording", ImVec2(-1, 32))) {
            std::string rec_path = writer_.start_recording(
                sdr_.get_current_sample_rate(),
                sdr_.get_current_frequency(),
                sdr_.get_device_name(),
                sdr_.get_current_gain(),
                sdr_.is_bias_tee_enabled()
            );
            if (!rec_path.empty()) {
                app_state_.set_recording_active(true, rec_path);
            }
        }
        ImGui::PopStyleColor(2);
    }

    // Seamless Record -> Analyze Bridge Button
    if (writer_.is_recording()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.40f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.50f, 0.20f, 1.0f));
        if (ImGui::Button("⏹ Stop & ⚡ Analyze Recording Now", ImVec2(-1, 34))) {
            writer_.stop_recording();
            app_state_.set_recording_active(false, "");
            std::string rec = writer_.get_last_recorded_filepath();
            if (!rec.empty()) {
                app_state_.set_last_recorded_file(rec);
                app_state_.switch_to_analyze(rec, true);
            }
        }
        ImGui::PopStyleColor(2);
    } else {
        std::string last_rec = writer_.get_last_recorded_filepath();
        if (last_rec.empty()) {
            last_rec = app_state_.get_last_recorded_file();
        }

        if (!last_rec.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.52f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.65f, 0.98f, 1.0f));
            if (ImGui::Button("⚡ Analyze This Recording Now", ImVec2(-1, 34))) {
                app_state_.switch_to_analyze(last_rec, true);
            }
            ImGui::PopStyleColor(2);
            ImGui::TextColored(ImVec4(0.11f, 0.82f, 0.63f, 1.0f), "Ready: %s", last_rec.c_str());
        }
    }
}

void UiRecordPanel::render_display_panel(ID3D11DeviceContext* context, float width, float height) {
    float spec_h = show_waterfall_ ? (height * 0.55f) : (height - 10.0f);
    float wf_h = show_waterfall_ ? (height - spec_h - 10.0f) : 0.0f;

    render_spectrum_plot(width, spec_h);

    if (show_waterfall_) {
        render_waterfall_plot(context, width, wf_h);
    }
}

void UiRecordPanel::render_spectrum_plot(float width, float height) {
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
    draw_list->AddRect(ImVec2(plot_x0, plot_y0), ImVec2(plot_x1, plot_y1), IM_COL32(50, 55, 65, 255));

    // 2. Frequency Span bounds
    uint64_t fc = sdr_.get_current_frequency();
    uint32_t fs = sdr_.get_current_sample_rate();
    double start_f = static_cast<double>(fc) - static_cast<double>(fs) / 2.0;
    double stop_f = static_cast<double>(fc) + static_cast<double>(fs) / 2.0;
    double span_f = stop_f - start_f;
    float db_range = fft_max_db_ - fft_min_db_;
    if (db_range < 5.0f) db_range = 5.0f;

    // 3. Grid Lines & Coordinate Labels (dBFS Y-axis, Frequency X-axis)
    // Horizontal dB lines
    int num_db_divs = 5;
    for (int i = 0; i <= num_db_divs; ++i) {
        float frac = static_cast<float>(i) / num_db_divs;
        float y = plot_y0 + plot_h * (1.0f - frac);
        float db_val = fft_min_db_ + frac * db_range;

        draw_list->AddLine(ImVec2(plot_x0, y), ImVec2(plot_x1, y), IM_COL32(40, 45, 52, 180), 1.0f);

        char db_text[32];
        snprintf(db_text, sizeof(db_text), "%4.0f dB", db_val);
        draw_list->AddText(ImVec2(p0.x + 5, y - 7), IM_COL32(140, 145, 155, 255), db_text);
    }

    // Vertical Frequency lines
    int num_freq_divs = 8;
    for (int i = 0; i <= num_freq_divs; ++i) {
        float frac = static_cast<float>(i) / num_freq_divs;
        float x = plot_x0 + plot_w * frac;
        double f_val = start_f + frac * span_f;

        draw_list->AddLine(ImVec2(x, plot_y0), ImVec2(x, plot_y1), IM_COL32(40, 45, 52, 180), 1.0f);

        char f_text[32];
        snprintf(f_text, sizeof(f_text), "%.3f", f_val / 1e6);
        draw_list->AddText(ImVec2(x - 18, plot_y1 + 5), IM_COL32(140, 145, 155, 255), f_text);
    }

    // 4. Center LO Marker (Red Line)
    float lo_x = plot_x0 + plot_w * 0.5f;
    draw_list->AddLine(ImVec2(lo_x, plot_y0), ImVec2(lo_x, plot_y1), IM_COL32(230, 60, 60, 120), 1.5f);

    // 5. HI 21cm Line Marker (Golden Yellow dashed/prominent line if within current passband)
    if (SdrConstants::HI_REST_FREQ_HZ >= start_f && SdrConstants::HI_REST_FREQ_HZ <= stop_f) {
        float hi_frac = static_cast<float>((SdrConstants::HI_REST_FREQ_HZ - start_f) / span_f);
        float hi_x = plot_x0 + plot_w * hi_frac;
        draw_list->AddLine(ImVec2(hi_x, plot_y0), ImVec2(hi_x, plot_y1), IM_COL32(255, 215, 0, 220), 1.5f);

        // HI Rest line label badge
        draw_list->AddRectFilled(ImVec2(hi_x - 30, plot_y0 + 2), ImVec2(hi_x + 30, plot_y0 + 18),
                                 IM_COL32(40, 35, 10, 200), 2.0f);
        draw_list->AddText(ImVec2(hi_x - 26, plot_y0 + 3), IM_COL32(255, 215, 0, 255), "HI 21cm");
    }

    // 6. Spectrum Curve Rendering & Gradient Fill
    if (!spectrum_data_.empty()) {
        size_t n_pts = spectrum_data_.size();
        ImVec2 prev_pt;
        bool first = true;

        for (size_t i = 0; i < n_pts; ++i) {
            float frac_x = static_cast<float>(i) / (n_pts - 1);
            float x = plot_x0 + plot_w * frac_x;

            float val = std::clamp(spectrum_data_[i], fft_min_db_, fft_max_db_);
            float norm_y = (val - fft_min_db_) / db_range;
            float y = plot_y0 + plot_h * (1.0f - norm_y);

            ImVec2 cur_pt(x, y);
            if (!first) {
                // Gradient Fill under curve (SatDump electric cyan glow)
                if (fill_spectrum_) {
                    ImVec2 poly[4] = {
                        prev_pt,
                        cur_pt,
                        ImVec2(cur_pt.x, plot_y1),
                        ImVec2(prev_pt.x, plot_y1)
                    };
                    draw_list->AddConvexPolyFilled(poly, 4, IM_COL32(102, 230, 255, 30));
                }

                // Spectrum Line trace (#66E6FFFF)
                draw_list->AddLine(prev_pt, cur_pt, IM_COL32(102, 230, 255, 255), 1.5f);
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

void UiRecordPanel::render_waterfall_plot(ID3D11DeviceContext* /*context*/, float width, float height) {
    ImGui::BeginChild("WaterfallChild", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);
    if (waterfall_srv_) {
        ImVec2 uv0(0.0f, 0.0f);
        ImVec2 uv1(1.0f, 1.0f);
        ImGui::Image(reinterpret_cast<ImTextureID>(waterfall_srv_), ImVec2(width - 15, height - 10), uv0, uv1);
    }
    ImGui::EndChild();
}

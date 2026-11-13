#include "ui_record_panel.h"
#include "constants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace {

static bool is_device_status_healthy(const std::string& status_detail) {
    return status_detail.find("locked") == std::string::npos &&
           status_detail.find("error") == std::string::npos;
}

static std::string build_timestamped_cs16_path(const std::string& output_dir,
                                               const std::string& prefix,
                                               uint64_t center_freq_hz) {
    std::string clean_prefix = prefix.empty() ? "capture" : prefix;
    for (char& ch : clean_prefix) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|' || ch == ' ') {
            ch = '_';
        }
    }

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &tt);
#else
    tm_now = *std::localtime(&tt);
#endif
    char stamp[128];
    double freq_mhz = static_cast<double>(center_freq_hz) / 1e6;
    std::snprintf(stamp, sizeof(stamp), "%s_%04d%02d%02d_%02d%02d%02d_%.3fMHz.cs16",
                  clean_prefix.c_str(),
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, freq_mhz);

    std::filesystem::path dir(output_dir.empty() ? "recordings" : output_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / stamp).string();
}

} // namespace

UiRecordPanel::UiRecordPanel(SdrController& controller, DspPipeline& dsp, Cs16Writer& writer,
                             Waterfall& waterfall, AppState& app_state)
    : sdr_(controller), dsp_(dsp), writer_(writer), waterfall_(waterfall), app_state_(app_state)
{
    waterfall_.set_palette(WaterfallPalette::Viridis);
    colorbar_lut_scratch_.resize(256, 0xFF000000u);
    refresh_devices();
    sync_device_selection();
}

UiRecordPanel::~UiRecordPanel() {
    release_d3d_resources();
}

void UiRecordPanel::init_d3d_resources(ID3D11Device* device) {
    d3d_device_ = device;
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

void UiRecordPanel::refresh_devices() {
    device_entries_.clear();

    auto hw_names = RtlSdrDevice::scan_devices();
    for (size_t i = 0; i < hw_names.size(); ++i) {
        DeviceEntry entry;
        entry.label = hw_names[i];
        entry.is_virtual = false;
        entry.hw_index = static_cast<uint32_t>(i);
        device_entries_.push_back(entry);
    }

    // Always append internal 21cm H I Simulator as selectable radio source
    DeviceEntry sim;
    sim.label = "Virtual 21cm H I Simulator (Internal)";
    sim.is_virtual = true;
    sim.hw_index = 0;
    device_entries_.push_back(sim);

    sync_device_selection();
}

void UiRecordPanel::sync_device_selection() {
    const bool sim_active = sdr_.is_simulated();
    selected_device_idx_ = static_cast<int>(device_entries_.size()) - 1;
    if (!sim_active) {
        for (size_t i = 0; i < device_entries_.size(); ++i) {
            if (!device_entries_[i].is_virtual) {
                selected_device_idx_ = static_cast<int>(i);
                break;
            }
        }
    }

    valid_gains_ = sdr_.get_valid_gains();
    if (valid_gains_.empty()) {
        valid_gains_ = {0.0f, 9.0f, 14.0f, 25.4f, 36.4f, 49.6f};
    }

    lna_gain_db_ = snap_gain_to_valid(sdr_.get_current_gain());
    agc_enabled_ = sdr_.is_auto_gain();
    bias_tee_enabled_ = sdr_.is_bias_tee_enabled();
    ppm_correction_ = sdr_.get_current_ppm();
    device_status_text_ = sdr_.get_status_detail();
    device_status_ok_ = !sim_active || is_device_status_healthy(device_status_text_);

    // Sync center frequency text & preset index
    uint64_t f_hz = sdr_.get_current_frequency();
    std::snprintf(tune_input_buf_, sizeof(tune_input_buf_), "%.3f", static_cast<double>(f_hz) / 1e6);
    selected_preset_idx_ = -1;
    const auto& presets = SdrConstants::GetPresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].freq_hz == f_hz) {
            selected_preset_idx_ = static_cast<int>(i);
            break;
        }
    }

    // Sync sample rate combo
    uint32_t rate_hz = sdr_.get_current_sample_rate();
    const auto& rates = SdrConstants::GetSampleRates();
    for (size_t i = 0; i < rates.size(); ++i) {
        if (rates[i].rate_hz == rate_hz) {
            selected_sample_rate_idx_ = static_cast<int>(i);
            break;
        }
    }

    // Sync FFT size combo
    int cur_fft = dsp_.get_fft_size();
    const auto& ffts = SdrConstants::GetFftSizes();
    for (size_t i = 0; i < ffts.size(); ++i) {
        if (ffts[i] == cur_fft) {
            selected_fft_size_idx_ = static_cast<int>(i);
            break;
        }
    }
    avg_factor_ = dsp_.get_averaging();
}

float UiRecordPanel::snap_gain_to_valid(float requested_db) const {
    if (valid_gains_.empty()) return requested_db;
    float best = valid_gains_[0];
    float best_diff = std::abs(best - requested_db);
    for (float g : valid_gains_) {
        float d = std::abs(g - requested_db);
        if (d < best_diff) {
            best_diff = d;
            best = g;
        }
    }
    return best;
}

void UiRecordPanel::set_frequency(uint64_t freq_hz) {
    uint64_t clamped = std::clamp<uint64_t>(freq_hz, 24000000ULL, 1766000000ULL);
    sdr_.request_frequency(clamped);
    std::snprintf(tune_input_buf_, sizeof(tune_input_buf_), "%.3f", static_cast<double>(clamped) / 1e6);

    selected_preset_idx_ = -1;
    const auto& presets = SdrConstants::GetPresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].freq_hz == clamped) {
            selected_preset_idx_ = static_cast<int>(i);
            break;
        }
    }
}

void UiRecordPanel::step_frequency(int64_t delta_hz) {
    int64_t cur = static_cast<int64_t>(sdr_.get_current_frequency());
    int64_t next = std::max<int64_t>(24000000LL, cur + delta_hz);
    set_frequency(static_cast<uint64_t>(next));
}

void UiRecordPanel::render_panel(ID3D11DeviceContext* context, float width, float height) {
    // Keep app_state recording status authoritative even after timed auto-stop
    if (!writer_.is_recording() && app_state_.is_recording_active()) {
        std::string last = writer_.get_last_recorded_filepath();
        app_state_.set_recording_active(false);
        if (!last.empty()) {
            app_state_.set_last_recorded_file(last);
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("##RecPanelRoot", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float inner_w = width - 20.0f;
    const float ribbon_h = 48.0f;
    const float content_h = std::max(120.0f, height - ribbon_h - 24.0f);

    render_top_ribbon(inner_w, ribbon_h);

    const float sidebar_w = std::min(310.0f, std::max(260.0f, inner_w * 0.24f));
    const float main_w = std::max(240.0f, inner_w - sidebar_w - 8.0f);

    render_sidebar(sidebar_w, content_h);
    ImGui::SameLine();
    render_display_area(context, main_w, content_h);

    render_bias_tee_modal();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void UiRecordPanel::render_top_ribbon(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 6.0f));
    ImGui::BeginChild("##RecTopRibbon", ImVec2(width, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const bool streaming = sdr_.is_streaming();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 1. Start / Stop RX Stream Toggle
    ImVec4 rx_col = streaming ? ui_obs::palette::Red : ui_obs::palette::Emerald;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(rx_col.x, rx_col.y, rx_col.z, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(rx_col.x, rx_col.y, rx_col.z, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_Border, rx_col);
    ui_obs::PushFontSafe(fonts_.semibold);
    const char* rx_label = streaming ? "    Stop RX" : "    Start RX";
    if (ImGui::Button(rx_label, ImVec2(112.0f, height - 14.0f))) {
        if (streaming) sdr_.stop_streaming();
        else sdr_.start_streaming();
    }
    ImVec2 bmin = ImGui::GetItemRectMin();
    ImVec2 bmax = ImGui::GetItemRectMax();
    ImVec2 ic(bmin.x + 16.0f, (bmin.y + bmax.y) * 0.5f);
    if (streaming) ui_obs::icons::draw_stop_square(dl, ic, 6.0f, ui_obs::palette::U32(ui_obs::palette::Red));
    else           ui_obs::icons::draw_play_triangle(dl, ic, 6.5f, ui_obs::palette::U32(ui_obs::palette::Emerald));
    ImGui::PopFont();
    ImGui::PopStyleColor(3);

    // 2. Interactive Monospace LCD Frequency Display + Direct Entry
    ImGui::SameLine(0.0f, 12.0f);
    ui_obs::PushFontSafe(fonts_.mono);
    ImGui::SetNextItemWidth(138.0f);
    if (ImGui::InputText("##TuneMHz", tune_input_buf_, sizeof(tune_input_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
        double mhz = std::strtod(tune_input_buf_, nullptr);
        if (mhz > 10.0) {
            set_frequency(static_cast<uint64_t>(std::llround(mhz * 1e6)));
        }
    }
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            step_frequency(static_cast<int64_t>(wheel) * 10000LL);
        }
        ImGui::SetTooltip("LO Frequency (MHz) - Type & Enter or Scroll wheel (+/-10 kHz)");
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextColored(ui_obs::palette::Cyan, "MHz");

    // Step buttons
    ImGui::SameLine(0.0f, 8.0f);
    auto step_btn = [&](const char* lbl, int64_t delta) {
        if (ImGui::Button(lbl, ImVec2(48.0f, height - 16.0f))) {
            step_frequency(delta);
        }
        ImGui::SameLine(0.0f, 3.0f);
    };
    if (width > 940.0f) {
        step_btn("-1M", -1000000LL);
        step_btn("-100k", -100000LL);
        step_btn("-10k", -10000LL);
        step_btn("+10k", 10000LL);
        step_btn("+100k", 100000LL);
        step_btn("+1M", 1000000LL);
    } else {
        step_btn("-100k", -100000LL);
        step_btn("-10k", -10000LL);
        step_btn("+10k", 10000LL);
        step_btn("+100k", 100000LL);
    }
    ImGui::PopFont();

    // 3. Band Preset Selector (with accurate Custom state sync)
    ImGui::SameLine(0.0f, 10.0f);
    const auto& presets = SdrConstants::GetPresets();
    const char* cur_preset_name = (selected_preset_idx_ >= 0 && selected_preset_idx_ < static_cast<int>(presets.size()))
        ? presets[static_cast<size_t>(selected_preset_idx_)].name
        : "Custom LO";
    ImGui::SetNextItemWidth(std::min(220.0f, std::max(130.0f, ImGui::GetContentRegionAvail().x - (bias_tee_enabled_ ? 120.0f : 8.0f))));
    if (ImGui::BeginCombo("##BandPresets", cur_preset_name)) {
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            bool is_sel = (selected_preset_idx_ == i);
            if (ImGui::Selectable(presets[static_cast<size_t>(i)].name, is_sel)) {
                set_frequency(presets[static_cast<size_t>(i)].freq_hz);
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // 4. Persistent Bias-Tee 4.5V Live Warning Badge
    if (bias_tee_enabled_) {
        ImGui::SameLine(0.0f, 10.0f);
        ImVec2 bp = ImGui::GetCursorScreenPos();
        const float bw = 96.0f;
        const float bh = height - 16.0f;
        dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), ui_obs::palette::U32(ui_obs::palette::Amber, 0.18f), 5.0f);
        dl->AddRect(bp, ImVec2(bp.x + bw, bp.y + bh), ui_obs::palette::U32(ui_obs::palette::Amber), 5.0f);
        ui_obs::icons::draw_bolt(dl, ImVec2(bp.x + 13.0f, bp.y + bh * 0.5f), 6.0f, ui_obs::palette::U32(ui_obs::palette::Amber));
        ImVec2 ts = ImGui::CalcTextSize("BIAS 4.5V");
        dl->AddText(ImVec2(bp.x + 23.0f, bp.y + (bh - ts.y) * 0.5f),
                    ui_obs::palette::U32(ui_obs::palette::Amber), "BIAS 4.5V");
        ImGui::Dummy(ImVec2(bw, bh));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void UiRecordPanel::render_sidebar(float width, float height) {
    ImGui::BeginChild("##RecSidebar", ImVec2(width, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    render_device_card();
    render_radio_card();
    render_dsp_card();
    render_recording_card();
    ImGui::EndChild();
}

void UiRecordPanel::render_device_card() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardSource", ImVec2(-1.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "SOURCE HARDWARE");
    ImGui::PopFont();
    ImGui::Separator();

    const char* cur_label = (!device_entries_.empty() && selected_device_idx_ >= 0 &&
                             selected_device_idx_ < static_cast<int>(device_entries_.size()))
        ? device_entries_[static_cast<size_t>(selected_device_idx_)].label.c_str()
        : "No Source";

    const float scan_w = 58.0f;
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - scan_w - 6.0f));
    if (ImGui::BeginCombo("##SdrDeviceCombo", cur_label)) {
        for (int i = 0; i < static_cast<int>(device_entries_.size()); ++i) {
            bool is_sel = (selected_device_idx_ == i);
            if (ImGui::Selectable(device_entries_[static_cast<size_t>(i)].label.c_str(), is_sel)) {
                if (selected_device_idx_ != i) {
                    const auto& entry = device_entries_[static_cast<size_t>(i)];
                    bool was_streaming = sdr_.is_streaming();
                    bool was_recording = writer_.is_recording();
                    if (!was_recording) {
                        sdr_.stop_streaming();
                        sdr_.initialize(entry.is_virtual, entry.hw_index);
                        if (was_streaming) sdr_.start_streaming();
                        sync_device_selection();
                    }
                }
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(scan_w, 0.0f))) {
        refresh_devices();
    }

    ImVec4 st_col = sdr_.is_simulated() ? ui_obs::palette::Amber : ui_obs::palette::Emerald;
    ImGui::TextColored(st_col, "%s", device_status_text_.c_str());

    auto blockers = SdrController::check_blocking_apps();
    if (!blockers.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui_obs::palette::Red);
        ImGui::TextWrapped("USB busy: %s", blockers[0].c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiRecordPanel::render_radio_card() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardRadio", ImVec2(-1.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "RADIO & GAIN");
    ImGui::PopFont();
    ImGui::Separator();

    const auto& rates = SdrConstants::GetSampleRates();
    const char* cur_rate = (selected_sample_rate_idx_ >= 0 && selected_sample_rate_idx_ < static_cast<int>(rates.size()))
        ? rates[static_cast<size_t>(selected_sample_rate_idx_)].label
        : "2.4 Msps";
    ImGui::TextColored(ui_obs::palette::TextMuted, "Sample Rate");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##SampleRate", cur_rate)) {
        for (int i = 0; i < static_cast<int>(rates.size()); ++i) {
            bool is_sel = (selected_sample_rate_idx_ == i);
            if (ImGui::Selectable(rates[static_cast<size_t>(i)].label, is_sel)) {
                selected_sample_rate_idx_ = i;
                sdr_.request_sample_rate(rates[static_cast<size_t>(i)].rate_hz);
                dsp_.flush();
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Discretely snapped RF LNA Gain (disabled while AGC is active)
    ImGui::TextColored(ui_obs::palette::TextMuted, "LNA Gain (%.1f dB snapped)", lna_gain_db_);
    ImGui::BeginDisabled(agc_enabled_);
    ImGui::SetNextItemWidth(-1.0f);
    float g_min = valid_gains_.empty() ? 0.0f : valid_gains_.front();
    float g_max = valid_gains_.empty() ? 49.6f : valid_gains_.back();
    if (ImGui::SliderFloat("##LnaGain", &lna_gain_db_, g_min, g_max, "%.1f dB")) {
        lna_gain_db_ = snap_gain_to_valid(lna_gain_db_);
        sdr_.request_gain(lna_gain_db_, false);
    }
    ImGui::EndDisabled();

    // Compact 2-col checkboxes + PPM
    if (ImGui::Checkbox("Auto Gain (AGC)", &agc_enabled_)) {
        sdr_.request_gain(lna_gain_db_, agc_enabled_);
    }

    bool bias_ui = bias_tee_enabled_;
    if (ImGui::Checkbox("Bias-Tee (+4.5V DC)", &bias_ui)) {
        if (bias_ui && !bias_tee_enabled_) {
            open_bias_modal_ = true;
        } else if (!bias_ui) {
            bias_tee_enabled_ = false;
            sdr_.request_bias_tee(false);
        }
    }

    ImGui::TextColored(ui_obs::palette::TextMuted, "Freq Calibration (PPM)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##PpmOffset", &ppm_correction_, -100, 100, "%d ppm")) {
        sdr_.request_ppm(ppm_correction_);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiRecordPanel::render_dsp_card() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardDsp", ImVec2(-1.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "DSP & SPECTRUM");
    ImGui::PopFont();
    ImGui::Separator();

    const auto& ffts = SdrConstants::GetFftSizes();
    char fft_lbl[32];
    std::snprintf(fft_lbl, sizeof(fft_lbl), "%d bins", ffts[static_cast<size_t>(selected_fft_size_idx_)]);
    ImGui::TextColored(ui_obs::palette::TextMuted, "FFT Resolution");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##FftBins", fft_lbl)) {
        for (int i = 0; i < static_cast<int>(ffts.size()); ++i) {
            char opt[32];
            std::snprintf(opt, sizeof(opt), "%d bins", ffts[static_cast<size_t>(i)]);
            bool is_sel = (selected_fft_size_idx_ == i);
            if (ImGui::Selectable(opt, is_sel)) {
                selected_fft_size_idx_ = i;
                dsp_.set_fft_size(ffts[static_cast<size_t>(i)]);
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::TextColored(ui_obs::palette::TextMuted, "Temporal Smoothing");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##AvgFactor", &avg_factor_, 0.0f, 0.95f, "%.2f")) {
        dsp_.set_averaging(avg_factor_);
    }

    // Guarded Y-axis floor & ceiling + Auto-fit
    ImGui::TextColored(ui_obs::palette::TextMuted, "Dynamic Range (dBFS)");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 62.0f);
    if (ImGui::SmallButton("Auto Fit")) {
        float nf = dsp_.get_noise_floor_db();
        if (std::isfinite(nf) && nf > -150.0f && nf < 10.0f) {
            fft_min_db_ = std::clamp(nf - 8.0f, -140.0f, -20.0f);
            fft_max_db_ = std::clamp(std::max(peak_dbfs_ + 8.0f, fft_min_db_ + 32.0f), fft_min_db_ + 10.0f, 5.0f);
        }
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("Min##MinDb", &fft_min_db_, -140.0f, -15.0f, "%.0f dB")) {
        if (fft_min_db_ > fft_max_db_ - 5.0f) fft_min_db_ = fft_max_db_ - 5.0f;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("Max##MaxDb", &fft_max_db_, -130.0f, 10.0f, "%.0f dB")) {
        if (fft_max_db_ < fft_min_db_ + 5.0f) fft_max_db_ = fft_min_db_ + 5.0f;
    }

    const auto& palettes = Waterfall::get_palette_names();
    const char* cur_pal = (selected_palette_idx_ >= 0 && selected_palette_idx_ < static_cast<int>(palettes.size()))
        ? palettes[static_cast<size_t>(selected_palette_idx_)].c_str()
        : "Viridis";
    ImGui::TextColored(ui_obs::palette::TextMuted, "Waterfall Palette");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##WfPalette", cur_pal)) {
        for (int i = 0; i < static_cast<int>(palettes.size()); ++i) {
            bool is_sel = (selected_palette_idx_ == i);
            if (ImGui::Selectable(palettes[static_cast<size_t>(i)].c_str(), is_sel)) {
                selected_palette_idx_ = i;
                waterfall_.set_palette(static_cast<WaterfallPalette>(i));
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Checkbox("Fill Trace", &fill_spectrum_);
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::Checkbox("Waterfall", &show_waterfall_);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiRecordPanel::render_recording_card() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardRecord", ImVec2(-1.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "IQ CAPTURE & H I BRIDGE");
    ImGui::PopFont();
    ImGui::Separator();

    // Real fixed CS16 format pill
    ImGui::TextColored(ui_obs::palette::TextMuted, "Format:");
    ImGui::SameLine();
    ImGui::TextColored(ui_obs::palette::Emerald, "CS16 (16-bit I/Q Interleaved)");

    // Update recording stats ~5 Hz
    auto now = std::chrono::steady_clock::now();
    if (last_stats_time_.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time_).count() >= 180) {
        last_stats_ = writer_.get_stats();
        last_stats_time_ = now;
    }

    const bool is_rec = writer_.is_recording();

    ImGui::BeginDisabled(is_rec);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Filename Prefix");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##RecPrefix", record_prefix_buf_, sizeof(record_prefix_buf_));

    ImGui::TextColored(ui_obs::palette::TextMuted, "Timed Duration (s, 0 = Manual)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputFloat("##RecDur", &record_duration_s_, 10.0f, 60.0f, "%.0f s")) {
        if (record_duration_s_ < 0.0f) record_duration_s_ = 0.0f;
    }
    ImGui::EndDisabled();

    // Start / Stop Capture button
    ImVec4 cap_col = is_rec ? ui_obs::palette::Red : ui_obs::palette::AccentBlue;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(cap_col.x, cap_col.y, cap_col.z, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(cap_col.x, cap_col.y, cap_col.z, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_Border, cap_col);
    ui_obs::PushFontSafe(fonts_.semibold);
    const char* rec_btn_lbl = is_rec ? "Stop Recording CS16" : "Start Recording CS16";
    if (ImGui::Button(rec_btn_lbl, ImVec2(-1.0f, 34.0f))) {
        if (!is_rec) {
            if (!sdr_.is_streaming()) sdr_.start_streaming();
            std::string custom_path = build_timestamped_cs16_path(
                writer_.get_output_dir(), record_prefix_buf_, sdr_.get_current_frequency());
            std::string path = writer_.start_recording(
                sdr_.get_current_sample_rate(),
                sdr_.get_current_frequency(),
                sdr_.get_device_name(),
                sdr_.get_current_gain(),
                sdr_.is_bias_tee_enabled(),
                custom_path,
                static_cast<double>(record_duration_s_)
            );
            if (!path.empty()) {
                app_state_.set_recording_active(true, path);
                app_state_.set_last_recorded_file(path);
            }
        } else {
            writer_.stop_recording();
            app_state_.set_recording_active(false);
            std::string last = writer_.get_last_recorded_filepath();
            if (!last.empty()) app_state_.set_last_recorded_file(last);
        }
        last_stats_ = writer_.get_stats();
    }
    ImGui::PopFont();
    ImGui::PopStyleColor(3);

    // Compact 2-column capture metrics table
    if (ImGui::BeginTable("##RecStatsTbl", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(ui_obs::palette::TextMuted, "State");
        ImGui::TableSetColumnIndex(1); ImGui::TextColored(is_rec ? ui_obs::palette::Red : ui_obs::palette::TextPrimary, "%s", last_stats_.status.c_str());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(ui_obs::palette::TextMuted, "Size");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", last_stats_.size_str.c_str());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(ui_obs::palette::TextMuted, "Elapsed");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", last_stats_.elapsed_str.c_str());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextColored(ui_obs::palette::TextMuted, "Drops");
        ImGui::TableSetColumnIndex(1); ImGui::TextColored(last_stats_.dropped_blocks > 0 ? ui_obs::palette::Red : ui_obs::palette::Emerald,
                                                          "%llu", static_cast<unsigned long long>(last_stats_.dropped_blocks));
        ImGui::EndTable();
    }

    // Bridge Action + Open Output Directory
    std::string last_file = app_state_.get_last_recorded_file();
    if (last_file.empty()) last_file = writer_.get_last_recorded_filepath();
    const bool can_bridge = (!is_rec && !last_file.empty());

    const float folder_w = 88.0f;
    ImGui::BeginDisabled(!can_bridge);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.184f, 0.506f, 0.969f, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.184f, 0.506f, 0.969f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Border, ui_obs::palette::AccentBlue);
    if (ImGui::Button("Analyze Capture ->", ImVec2(std::max(80.0f, ImGui::GetContentRegionAvail().x - folder_w - 6.0f), 30.0f))) {
        app_state_.switch_to_analyze(last_file, true);
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Folder...", ImVec2(folder_w, 30.0f))) {
        ui_obs::win_fs::open_folder_in_explorer(writer_.get_output_dir());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiRecordPanel::render_bias_tee_modal() {
    if (open_bias_modal_) {
        ImGui::OpenPopup("Confirm +4.5V Bias-Tee");
        open_bias_modal_ = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm +4.5V Bias-Tee", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ui_obs::palette::Amber, "WARNING: DC Power Injection (+4.5V)");
        ImGui::Spacing();
        ImGui::TextWrapped("Enabling the RTL-SDR Bias-Tee sends +4.5V DC directly onto the RF SMA center pin.");
        ImGui::TextWrapped("Only enable if your antenna feed has an active LNA or DC block. Shorting DC to ground can damage the tuner.");
        ImGui::Separator();

        if (ImGui::Button("Enable +4.5V Bias-Tee", ImVec2(175.0f, 30.0f))) {
            bias_tee_enabled_ = true;
            sdr_.request_bias_tee(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 30.0f))) {
            bias_tee_enabled_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void UiRecordPanel::render_display_area(ID3D11DeviceContext* context, float width, float height) {
    ImGui::BeginChild("##RecDisplayCol", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Pull latest FFT power spectrum
    uint64_t f_hz = sdr_.get_current_frequency();
    uint32_t rate_hz = sdr_.get_current_sample_rate();
    dsp_.get_power_spectrum(spectrum_data_, peak_freq_hz_, peak_dbfs_, f_hz, rate_hz);

    if (show_waterfall_) {
        const float spec_h = std::max(140.0f, height * 0.52f);
        const float wf_h = std::max(120.0f, height - spec_h - 8.0f);
        render_spectrum_card(width, spec_h);
        render_waterfall_card(context, width, wf_h);
    } else {
        render_spectrum_card(width, height);
    }

    ImGui::EndChild();
}

void UiRecordPanel::render_spectrum_card(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##RecSpectrumCard", ImVec2(width, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Inline Header Strip (zero occlusion over spectrum plot!)
    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::TextPrimary, "REAL-TIME POWER SPECTRUM");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 16.0f);
    ui_obs::PushFontSafe(fonts_.mono);
    ImGui::TextColored(ui_obs::palette::Cyan, "Peak: %.4f MHz (%.1f dBFS)", peak_freq_hz_ / 1e6, peak_dbfs_);
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Floor: %.1f dBFS  SNR: %.1f dB",
                       dsp_.get_noise_floor_db(), dsp_.get_snr_db());
    ImGui::PopFont();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 80.0f && avail.y > 60.0f) {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        double fc_mhz = static_cast<double>(sdr_.get_current_frequency()) / 1e6;
        double bw_mhz = static_cast<double>(sdr_.get_current_sample_rate()) / 1e6;
        double x_min = fc_mhz - 0.5 * bw_mhz;
        double x_max = fc_mhz + 0.5 * bw_mhz;

        ui_obs::PlotMargin m;
        m.left = 58.0f; m.right = 18.0f; m.top = 12.0f; m.bottom = 32.0f;
        auto vp = ui_obs::PlotViewport2D::create(origin, avail, x_min, x_max, fft_min_db_, fft_max_db_, m);
        vp.draw_background_and_grid(dl, 6, 5, "%.2f", "%.0f", "Frequency (MHz)", "dBFS", fonts_.mono);

        // H I Rest Frequency Reference Line if in passband
        const double hi_mhz = SdrConstants::HI_REST_FREQ_HZ / 1e6;
        if (hi_mhz >= x_min && hi_mhz <= x_max) {
            float hx = vp.map_x(hi_mhz);
            dl->AddLine(ImVec2(hx, vp.plot_min.y), ImVec2(hx, vp.plot_max.y),
                        ui_obs::palette::U32(ui_obs::palette::Amber, 0.75f), 1.5f);
            ui_obs::PushFontSafe(fonts_.mono);
            dl->AddText(ImVec2(hx + 4.0f, vp.plot_min.y + 4.0f),
                        ui_obs::palette::U32(ui_obs::palette::Amber), "H I 1420.406");
            ImGui::PopFont();
        }

        // Subtle fill polygon beneath downsampled trace (built first, behind stroke)
        if (spectrum_data_.size() >= 2) {
            if (fill_spectrum_) {
                vp.draw_decimated_trace(dl, spectrum_data_.size(), x_min, x_max,
                                        [&](size_t i) { return spectrum_data_[i]; },
                                        ui_obs::palette::U32(ui_obs::palette::Cyan, 0.14f), 5.0f, trace_scratch_);
            }
            vp.draw_decimated_trace(dl, spectrum_data_.size(), x_min, x_max,
                                    [&](size_t i) { return spectrum_data_[i]; },
                                    ui_obs::palette::U32(ui_obs::palette::Cyan), 1.5f, trace_scratch_);
        }

        // Peak Marker Diamond
        double pk_mhz = static_cast<double>(peak_freq_hz_) / 1e6;
        if (pk_mhz >= x_min && pk_mhz <= x_max && std::isfinite(peak_dbfs_)) {
            float px = vp.map_x(pk_mhz);
            float py = std::clamp(vp.map_y(peak_dbfs_), vp.plot_min.y, vp.plot_max.y);
            dl->AddCircleFilled(ImVec2(px, py), 4.0f, ui_obs::palette::U32(ui_obs::palette::Red));
        }

        // Interactive hover crosshair & tooltip
        ImGui::InvisibleButton("##SpecCanvas", avail);
        if (ImGui::IsItemHovered()) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            if (vp.contains(mp)) {
                dl->AddLine(ImVec2(mp.x, vp.plot_min.y), ImVec2(mp.x, vp.plot_max.y), ui_obs::palette::PlotCrosshair, 1.0f);
                dl->AddLine(ImVec2(vp.plot_min.x, mp.y), ImVec2(vp.plot_max.x, mp.y), ui_obs::palette::PlotCrosshair, 1.0f);
                double f_hov = vp.unmap_x(mp.x);
                double y_hov = vp.unmap_y(mp.y);
                double v_lsr = SdrConstants::CalculateDopplerVelocityKmS(f_hov * 1e6);
                ImGui::SetTooltip("%.4f MHz  |  V_LSR: %+.1f km/s  |  %.1f dBFS", f_hov, v_lsr, y_hov);
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiRecordPanel::render_waterfall_card(ID3D11DeviceContext* context, float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##RecWaterfallCard", ImVec2(width, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::TextPrimary, "LIVE WATERFALL HISTORY");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 16.0f);
    ui_obs::PushFontSafe(fonts_.mono);
    const auto& p_names = Waterfall::get_palette_names();
    const char* p_lbl = (selected_palette_idx_ >= 0 && selected_palette_idx_ < static_cast<int>(p_names.size()))
        ? p_names[static_cast<size_t>(selected_palette_idx_)].c_str()
        : "Viridis";
    ImGui::TextColored(ui_obs::palette::TextMuted, "Palette: %s  (%dx%d px)",
                       p_lbl, waterfall_.get_width(), waterfall_.get_height());
    ImGui::PopFont();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float colorbar_space = 74.0f;
    if (avail.x > 120.0f && avail.y > 48.0f) {
        const float img_w = std::max(64.0f, avail.x - colorbar_space);
        const float img_h = std::max(40.0f, avail.y - 4.0f);

        // Resize CPU waterfall buffer dynamically preserving history before uploading texture
        int target_w = std::clamp(static_cast<int>(img_w), 256, 2048);
        int target_h = std::clamp(static_cast<int>(img_h), 128, 512);
        if (std::abs(waterfall_.get_width() - target_w) >= 4 ||
            std::abs(waterfall_.get_height() - target_h) >= 4) {
            waterfall_.resize_preserving_history(target_w, target_h);
        }

        // Throttle waterfall line insertion to ~30 Hz
        auto now = std::chrono::steady_clock::now();
        if (last_wf_update_time_.time_since_epoch().count() == 0 ||
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_wf_update_time_).count() >= 33) {
            if (!spectrum_data_.empty()) {
                waterfall_.add_line(spectrum_data_, fft_min_db_, fft_max_db_);
            }
            last_wf_update_time_ = now;
        }

        update_waterfall_texture(context);

        ImVec2 img_pos = ImGui::GetCursorScreenPos();
        if (waterfall_srv_) {
            ImGui::Image(reinterpret_cast<ImTextureID>(waterfall_srv_), ImVec2(img_w, img_h));
            if (ImGui::IsItemHovered()) {
                ImVec2 mp = ImGui::GetIO().MousePos;
                float u = std::clamp((mp.x - img_pos.x) / img_w, 0.0f, 1.0f);
                double fc_mhz = static_cast<double>(sdr_.get_current_frequency()) / 1e6;
                double bw_mhz = static_cast<double>(sdr_.get_current_sample_rate()) / 1e6;
                double freq = (fc_mhz - 0.5 * bw_mhz) + static_cast<double>(u) * bw_mhz;
                float row_offset = std::clamp((mp.y - img_pos.y) / img_h, 0.0f, 1.0f) * static_cast<float>(waterfall_.get_height()) * 0.033f;
                ImGui::SetTooltip("%.4f MHz  |  -%.1f s", freq, row_offset);
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(img_pos, ImVec2(img_pos.x + img_w, img_pos.y + img_h), ui_obs::palette::PlotBorder32);

        // Right-aligned vertical dBFS colorbar using Waterfall's exact active 256-entry LUT
        const uint32_t* lut = waterfall_.get_palette_lut();
        std::copy(lut, lut + 256, colorbar_lut_scratch_.begin());
        ImVec2 bar_min(img_pos.x + img_w + 10.0f, img_pos.y + 4.0f);
        ImVec2 bar_max(bar_min.x + 14.0f, img_pos.y + img_h - 4.0f);
        ui_obs::draw_vertical_colorbar(dl, bar_min, bar_max, fft_min_db_, fft_max_db_, "%.0f",
                                       colorbar_lut_scratch_, fonts_.mono);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiRecordPanel::update_waterfall_texture(ID3D11DeviceContext* context) {
    if (!d3d_device_ || !context) return;

    const int w = waterfall_.get_width();
    const int h = waterfall_.get_height();
    if (w <= 0 || h <= 0) return;

    if (!waterfall_texture_ || w != tex_width_ || h != tex_height_) {
        release_d3d_resources();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (SUCCEEDED(d3d_device_->CreateTexture2D(&desc, nullptr, &waterfall_texture_))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.Format = desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            d3d_device_->CreateShaderResourceView(waterfall_texture_, &srv_desc, &waterfall_srv_);
            tex_width_ = w;
            tex_height_ = h;
        }
    }

    if (waterfall_texture_) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(waterfall_texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const uint32_t* src = waterfall_.get_pixel_data();
            const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint32_t);
            uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
            for (int y = 0; y < h; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * mapped.RowPitch,
                            src + static_cast<size_t>(y) * static_cast<size_t>(w),
                            row_bytes);
            }
            context->Unmap(waterfall_texture_, 0);
        }
    }
}

#include "ui_analyze_panel.h"
#include "../lib/imgui/imgui.h"
#include "constants.h"
#include "dc_excision.h"
#include "rfi_filter.h"
#include "baseline_fitter.h"
#include "exporter.h"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <iostream>

namespace astro {

static const int FFT_SIZES[] = {2048, 4096, 8192, 16384, 32768};
static const char* FFT_SIZE_NAMES[] = {"2048 bins", "4096 bins", "8192 bins (Default)", "16384 bins", "32768 bins"};
static const char* BASELINE_NAMES[] = {"Ripple Filter (Recommended)", "Asymmetric Least Squares (ALS)", "Polynomial Fit (Degree 10)"};
static const char* COLORMAP_NAMES[] = {"Magma / Inferno (Default)", "Viridis (Perceptual)", "Turbo (High Dynamic Range)", "Classic (Rainbow)"};

UiAnalyzePanel::UiAnalyzePanel(AppState& app_state)
    : app_state_(app_state) {
    scan_files();
}

UiAnalyzePanel::~UiAnalyzePanel() {
    cancel_analysis();
    if (export_thread_ && export_thread_->joinable()) {
        export_thread_->join();
    }
    release_d3d_resources();
}

void UiAnalyzePanel::cancel_analysis() {
    cancel_requested_ = true;
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
}

void UiAnalyzePanel::scan_files() {
    std::vector<std::string> search_dirs = {
        ".",
        "recordings",
        "../recordings",
        "cpp_recorder/recordings",
        "../cpp_recorder/recordings",
        "Analyzer",
        "../Analyzer"
    };
    discovered_files_ = SidecarReader::scan_cs16_files(search_dirs);
    if (!discovered_files_.empty() && selected_file_idx_ < 0) {
        selected_file_idx_ = 0;
        on_file_selected(discovered_files_[0]);
    }
}

void UiAnalyzePanel::on_file_selected(const std::string& path) {
    if (path.empty()) return;
    current_sidecar_ = SidecarReader::parse(path);
    if (current_sidecar_.found_sidecar) {
        SidecarReader::apply_to_config(current_sidecar_, config_);
        ui_center_freq_mhz_ = config_.center_freq_hz / 1e6;
        ui_sample_rate_msps_ = config_.sample_rate_hz / 1e6;
    }
    status_message_ = "Loaded: " + std::filesystem::path(path).filename().string();
}

void UiAnalyzePanel::select_and_run_file(const std::string& path, bool auto_run) {
    if (path.empty()) return;

    // Check if already in list
    int found_idx = -1;
    for (size_t i = 0; i < discovered_files_.size(); ++i) {
        if (std::filesystem::equivalent(path, discovered_files_[i])) {
            found_idx = static_cast<int>(i);
            break;
        }
    }

    if (found_idx >= 0) {
        selected_file_idx_ = found_idx;
    } else {
        discovered_files_.insert(discovered_files_.begin(), path);
        selected_file_idx_ = 0;
    }

    on_file_selected(path);

    if (auto_run) {
        start_analysis();
    }
}

void UiAnalyzePanel::init_d3d_resources(ID3D11Device* device) {
    d3d_device_ = device;
}

void UiAnalyzePanel::release_d3d_resources() {
    if (waterfall_srv_) { waterfall_srv_->Release(); waterfall_srv_ = nullptr; }
    if (waterfall_texture_) { waterfall_texture_->Release(); waterfall_texture_ = nullptr; }
    tex_width_ = 0;
    tex_height_ = 0;
}

void UiAnalyzePanel::update_waterfall_texture(ID3D11DeviceContext* ctx) {
    if (!d3d_device_ || !ctx || !has_results_ || stream_res_.waterfall_rows <= 0 || stream_res_.waterfall_cols <= 0) return;

    int rows = stream_res_.waterfall_rows;
    int cols = stream_res_.waterfall_cols;
    if (rows <= 0 || cols <= 0 || baseline_power_.size() < static_cast<size_t>(cols)) return;

    if (!waterfall_texture_dirty_ && waterfall_texture_ && ui_colormap_idx_ == last_colormap_idx_) {
        return; // Already up to date! Skip re-rendering to keep 60 FPS buttery smooth.
    }

    if (!waterfall_texture_ || tex_width_ != cols || tex_height_ != rows) {
        release_d3d_resources();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = cols;
        desc.Height = rows;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &waterfall_texture_);
        if (FAILED(hr) || !waterfall_texture_) return;

        hr = d3d_device_->CreateShaderResourceView(waterfall_texture_, nullptr, &waterfall_srv_);
        if (FAILED(hr)) {
            release_d3d_resources();
            return;
        }

        tex_width_ = cols;
        tex_height_ = rows;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(waterfall_texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    ColormapType cmap = static_cast<ColormapType>(ui_colormap_idx_);

    float min_val = 1e9f, max_val = -1e9f;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double base = std::max(1e-12, baseline_power_[c]);
            float wf = stream_res_.waterfall_data[r * cols + c];
            float rel_db = 10.0f * std::log10(std::max(1e-6f, wf / static_cast<float>(base)));
            min_val = std::min(min_val, rel_db);
            max_val = std::max(max_val, rel_db);
        }
    }
    float range = std::max(0.1f, max_val - min_val);

    uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
    for (int r = 0; r < rows; ++r) {
        int src_r = rows - 1 - r;
        uint32_t* row_pixels = reinterpret_cast<uint32_t*>(dst + r * mapped.RowPitch);

        for (int c = 0; c < cols; ++c) {
            double base = std::max(1e-12, baseline_power_[c]);
            float wf = stream_res_.waterfall_data[src_r * cols + c];
            float rel_db = 10.0f * std::log10(std::max(1e-6f, wf / static_cast<float>(base)));
            float norm = std::clamp((rel_db - min_val) / range, 0.0f, 1.0f);
            PixelColor col = sample_colormap(cmap, norm);
            row_pixels[c] = (255 << 24) | (col.b << 16) | (col.g << 8) | col.r;
        }
    }

    ctx->Unmap(waterfall_texture_, 0);

    waterfall_texture_dirty_ = false;
    last_colormap_idx_ = ui_colormap_idx_;
}

void UiAnalyzePanel::start_analysis() {
    std::string path;
    if (selected_file_idx_ >= 0 && selected_file_idx_ < static_cast<int>(discovered_files_.size())) {
        path = discovered_files_[selected_file_idx_];
    } else if (custom_file_path_[0] != '\0') {
        path = custom_file_path_;
    }

    if (path.empty() || !std::filesystem::exists(path)) {
        status_message_ = "Error: Invalid or non-existent file.";
        return;
    }

    if (app_state_.is_file_actively_recording(path)) {
        status_message_ = "Blocked: File is actively being recorded! Stop recording first.";
        return;
    }

    if (is_analyzing_) return;

    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }

    config_.center_freq_hz = ui_center_freq_mhz_ * 1e6;
    config_.sample_rate_hz = ui_sample_rate_msps_ * 1e6;
    config_.fft_size = FFT_SIZES[ui_fft_size_idx_];
    config_.baseline_method = static_cast<BaselineMethod>(ui_baseline_idx_);
    config_.system_temp_k = ui_tsys_k_;
    config_.max_duration_s = ui_duration_s_;
    config_.dc_clear_hz = ui_dc_clear_khz_ * 1e3;

    cancel_requested_ = false;
    is_analyzing_ = true;
    progress_pct_ = 0.0;
    progress_speed_ = 0.0;
    progress_eta_ = 0.0;
    status_message_ = "Running scientific analysis...";
    has_results_ = false;

    worker_thread_ = std::make_unique<std::thread>([this, path]() {
        try {
            StreamingProcessor processor(path, config_);
            processor.set_cancel_token(&cancel_requested_);
            auto prog_cb = [this](double pct, double speed, double eta) {
                progress_pct_ = pct;
                progress_speed_ = speed;
                progress_eta_ = eta;
            };

            StreamingResults stream = processor.process(prog_cb);

            if (cancel_requested_.load()) {
                status_message_ = "Analysis cancelled by user.";
                is_analyzing_ = false;
                return;
            }

            DCExcisionInfo dc_inf;
            std::vector<double> dc_cln = DCExcision::excise(stream.averaged_power, config_, dc_inf);

            int n_rfi = 0;
            std::vector<double> rfi_cln = RFIFilter::mitigate(dc_cln, stream.freq_mhz, config_, n_rfi);

            BaselineResults bl = BaselineFitter::fit(rfi_cln, stream.freq_mhz, config_);

            size_t nfft = stream.freq_mhz.size();
            std::vector<double> cal_k(nfft);
            std::vector<double> cal_db(nfft);
            std::vector<double> v_kms = Astrophysics::compute_doppler_velocity(stream.freq_hz);

            for (size_t i = 0; i < nfft; ++i) {
                double base_safe = std::max(1e-12, bl.baseline_power[i]);
                cal_k[i] = config_.system_temp_k * (rfi_cln[i] - bl.baseline_power[i]) / base_safe;
                cal_db[i] = 10.0 * std::log10(std::max(1e-6, rfi_cln[i] / base_safe));
            }

            double slope = 0.0, intercept = 0.0;
            BaselineFitter::refine_local(v_kms, cal_k, cal_db, config_, slope, intercept);

            AstrophysicalMetrics m = Astrophysics::calculate_metrics(
                stream.freq_mhz, v_kms, cal_k, cal_db, config_
            );

            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                stream_res_ = std::move(stream);
                rfi_clean_power_ = std::move(rfi_cln);
                baseline_power_ = std::move(bl.baseline_power);
                baseline_db_ = std::move(bl.baseline_db);
                calibrated_k_ = std::move(cal_k);
                calibrated_db_ = std::move(cal_db);
                vel_kms_ = std::move(v_kms);
                metrics_ = m;
                num_rfi_excised_ = n_rfi;
                dc_info_ = dc_inf;
                has_results_ = true;
                waterfall_texture_dirty_ = true;
                status_message_ = "Analysis complete! Galactic HI Line Detected (SNR: " +
                                  std::to_string(static_cast<int>(m.snr * 10) / 10.0f).substr(0, 4) + " sigma)";
            }
        } catch (const std::exception& ex) {
            status_message_ = std::string("Error: ") + ex.what();
        }
        is_analyzing_ = false;
    });
}

void UiAnalyzePanel::render_top_bar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
    ImGui::BeginChild("AnalyzeTopBar", ImVec2(0, 46), true);

    ImGui::SetCursorPosY(8);
    ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "ASTRO 21cm HYDROGEN LINE ANALYZER");
    ImGui::SameLine();
    ImGui::TextDisabled("| SatDump-Style High-Precision Astrophysics Pipeline");

    ImGui::SameLine(ImGui::GetWindowWidth() - 340);
    if (is_analyzing_) {
        float pct = static_cast<float>(progress_pct_.load() / 100.0);
        char bar_text[64];
        snprintf(bar_text, sizeof(bar_text), "%.1f%% (%.1f MB/s)", pct * 100.0f, progress_speed_.load());
        ImGui::ProgressBar(pct, ImVec2(320, 24), bar_text);
    } else {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", status_message_.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UiAnalyzePanel::render_sidebar_controls() {
    ImGui::BeginChild("AnalyzeSidebar", ImVec2(360, 0), true);

    std::string cur_sel_file;
    if (selected_file_idx_ >= 0 && selected_file_idx_ < static_cast<int>(discovered_files_.size())) {
        cur_sel_file = discovered_files_[selected_file_idx_];
    } else if (custom_file_path_[0] != '\0') {
        cur_sel_file = custom_file_path_;
    }
    bool is_active_recording = app_state_.is_file_actively_recording(cur_sel_file);

    ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "OBSERVATION CAPTURE");
    ImGui::Separator();

    ImGui::BeginDisabled(is_analyzing_);

    if (ImGui::Button("Scan Recordings", ImVec2(160, 26))) {
        scan_files();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(90, 26))) {
        scan_files();
    }

    if (!discovered_files_.empty()) {
        std::string preview = std::filesystem::path(discovered_files_[selected_file_idx_]).filename().string();
        if (ImGui::BeginCombo("Select File", preview.c_str())) {
            for (int i = 0; i < static_cast<int>(discovered_files_.size()); ++i) {
                bool is_selected = (selected_file_idx_ == i);
                std::string fname = std::filesystem::path(discovered_files_[i]).filename().string();
                if (ImGui::Selectable(fname.c_str(), is_selected)) {
                    selected_file_idx_ = i;
                    on_file_selected(discovered_files_[i]);
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::InputText("Custom Path", custom_file_path_, sizeof(custom_file_path_));

    // Sidecar metadata HUD box
    if (current_sidecar_.found_sidecar) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.16f, 0.20f, 0.8f));
        ImGui::BeginChild("SidecarBox", ImVec2(0, 105), true);
        ImGui::TextColored(ImVec4(0.11f, 0.82f, 0.63f, 1.0f), "Accompanied Sidecar: %s", current_sidecar_.sidecar_source.c_str());
        ImGui::Text("LO Center:   %.4f MHz", current_sidecar_.center_freq_hz / 1e6);
        ImGui::Text("Sample Rate: %.3f MSPS", current_sidecar_.sample_rate_hz / 1e6);
        if (current_sidecar_.has_gain) {
            ImGui::Text("RF Gain:     %.1f dB", current_sidecar_.gain_db);
        }
        ImGui::Text("Bias-Tee:    %s", current_sidecar_.bias_tee ? "Enabled" : "Disabled");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "DSP & BASELINE CONTROLS");
    ImGui::Separator();

    ImGui::InputDouble("Center LO (MHz)", &ui_center_freq_mhz_, 0.1, 1.0, "%.6f");
    ImGui::InputDouble("Sample Rate (MSPS)", &ui_sample_rate_msps_, 0.1, 0.5, "%.3f");

    ImGui::Combo("FFT Size", &ui_fft_size_idx_, FFT_SIZE_NAMES, IM_ARRAYSIZE(FFT_SIZE_NAMES));
    ImGui::Combo("Baseline Fit", &ui_baseline_idx_, BASELINE_NAMES, IM_ARRAYSIZE(BASELINE_NAMES));
    if (ImGui::Combo("Colormap", &ui_colormap_idx_, COLORMAP_NAMES, IM_ARRAYSIZE(COLORMAP_NAMES))) {
        waterfall_texture_dirty_ = true;
    }

    ImGui::SliderFloat("Tsys (K)", &ui_tsys_k_, 20.0f, 300.0f, "%.1f K");
    ImGui::InputFloat("Max Duration (s)", &ui_duration_s_, 10.0f, 60.0f, "%.1f");
    ImGui::SliderFloat("DC Notch (kHz)", &ui_dc_clear_khz_, 1.0f, 25.0f, "%.1f kHz");

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (is_active_recording) {
        ImGui::TextColored(ImVec4(1.0f, 0.675f, 0.071f, 1.0f), "⚠️ File is currently being recorded!\n   Stop recording before analyzing.");
        ImGui::Spacing();
    }

    // Action Buttons
    if (is_analyzing_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Cancel Processing", ImVec2(-1, 38))) {
            cancel_requested_ = true;
        }
        ImGui::PopStyleColor();
    } else {
        if (is_active_recording) {
            ImGui::BeginDisabled();
            ImGui::Button("RUN SCIENTIFIC ANALYSIS", ImVec2(-1, 40));
            ImGui::EndDisabled();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.52f, 0.88f, 1.0f));
            if (ImGui::Button("RUN SCIENTIFIC ANALYSIS", ImVec2(-1, 40))) {
                start_analysis();
            }
            ImGui::PopStyleColor();
        }
    }

    if (has_results_.load()) {
        ImGui::Spacing();
        if (is_exporting_.load()) {
            ImGui::BeginDisabled();
            ImGui::Button("⏳ Exporting Artifacts...", ImVec2(-1, 34));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Export Plots (PNG) & CSV", ImVec2(-1, 34))) {
                if (export_thread_ && export_thread_->joinable()) {
                    export_thread_->join();
                }
                is_exporting_ = true;
                export_status_msg_ = "Exporting publication plots...";

                std::string path = cur_sel_file;
                if (path.empty()) path = "observation.cs16";

                FileMetadata meta;
                StreamingResults stream_snap;
                std::vector<double> rfi_snap, base_snap, cal_k_snap, cal_db_snap;
                AstrophysicalMetrics met_snap;
                AstroConfig cfg_snap = config_;

                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    meta.filepath = path;
                    meta.filesize = stream_res_.file_size_bytes;
                    meta.total_samples = stream_res_.total_samples;
                    meta.duration_s = stream_res_.total_duration_s;
                    meta.processing_time_s = stream_res_.elapsed_processing_time_s;
                    meta.throughput_mb_s = stream_res_.throughput_mb_s;
                    stream_snap = stream_res_;
                    rfi_snap = rfi_clean_power_;
                    base_snap = baseline_power_;
                    cal_k_snap = calibrated_k_;
                    cal_db_snap = calibrated_db_;
                    met_snap = metrics_;
                }

                export_thread_ = std::make_unique<std::thread>([this, path, cfg_snap, meta, stream_snap, rfi_snap, base_snap, cal_k_snap, cal_db_snap, met_snap]() {
                    try {
                        Exporter::export_all_artifacts(
                            cfg_snap.output_dir, path, cfg_snap, meta, stream_snap,
                            rfi_snap, base_snap, cal_k_snap, cal_db_snap, met_snap
                        );
                        export_status_msg_ = "Saved all 5 PNGs & CSV to: " + cfg_snap.output_dir;
                    } catch (const std::exception& e) {
                        export_status_msg_ = std::string("Export error: ") + e.what();
                    }
                    is_exporting_ = false;
                });
            }
        }

        if (!export_status_msg_.empty()) {
            ImGui::TextColored(ImVec4(0.11f, 0.82f, 0.63f, 1.0f), "%s", export_status_msg_.c_str());
        }
    }

    ImGui::EndChild();
}

void UiAnalyzePanel::render_tab_calibrated_spectrum() {
    if (!has_results_) {
        ImGui::TextDisabled("Run scientific analysis to view calibrated spectrum.");
        return;
    }

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 100 || canvas_size.y < 100) return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(22, 27, 34, 255));
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(48, 54, 61, 255));

    float margin_left = 60.0f;
    float margin_right = 20.0f;
    float margin_top = 40.0f;
    float margin_bottom = 40.0f;

    float plot_w = canvas_size.x - margin_left - margin_right;
    float plot_h = canvas_size.y - margin_top - margin_bottom;

    double v_min = -200.0, v_max = 200.0;
    double k_max = 0.5, k_min = -0.5;
    for (size_t i = 0; i < vel_kms_.size(); ++i) {
        if (vel_kms_[i] >= v_min && vel_kms_[i] <= v_max) {
            k_max = std::max(k_max, calibrated_k_[i]);
            k_min = std::min(k_min, calibrated_k_[i]);
        }
    }
    k_min = std::max(-1.0, k_min - 0.5);
    k_max = k_max * 1.35 + 1.0;

    auto map_pt = [&](double v, double k) {
        float x = canvas_pos.x + margin_left + static_cast<float>((v - v_min) / (v_max - v_min) * plot_w);
        float y = canvas_pos.y + margin_top + plot_h - static_cast<float>((k - k_min) / (k_max - k_min) * plot_h);
        return ImVec2(x, y);
    };

    // Velocity grid lines
    for (double v = -180; v <= 180; v += 60) {
        ImVec2 p_top = map_pt(v, k_max);
        ImVec2 p_bot = map_pt(v, k_min);
        draw_list->AddLine(p_top, p_bot, IM_COL32(48, 54, 61, 180));
        char v_str[32];
        snprintf(v_str, sizeof(v_str), "%+d", static_cast<int>(v));
        draw_list->AddText(ImVec2(p_bot.x - 12, p_bot.y + 6), IM_COL32(139, 148, 158, 255), v_str);
    }

    // Zero Kelvin line
    ImVec2 y0_l = map_pt(v_min, 0.0);
    ImVec2 y0_r = map_pt(v_max, 0.0);
    draw_list->AddLine(y0_l, y0_r, IM_COL32(139, 148, 158, 200), 1.0f);

    // Rest frame line v=0
    ImVec2 x0_t = map_pt(0.0, k_max);
    ImVec2 x0_b = map_pt(0.0, k_min);
    draw_list->AddLine(x0_t, x0_b, IM_COL32(116, 185, 255, 220), 1.5f);

    // Spectrum curve (Cyan #00f2fe)
    for (size_t i = 1; i < vel_kms_.size(); ++i) {
        if (vel_kms_[i] < v_min || vel_kms_[i - 1] > v_max) continue;
        ImVec2 p1 = map_pt(vel_kms_[i - 1], calibrated_k_[i - 1]);
        ImVec2 p2 = map_pt(vel_kms_[i], calibrated_k_[i]);
        draw_list->AddLine(p1, p2, IM_COL32(0, 242, 254, 255), 2.0f);
    }

    // Gaussian fit overlay (Pink #ff7675)
    if (metrics_.gauss_fit.success && !metrics_.gauss_fit.fitted_curve.empty()) {
        for (size_t i = 1; i < vel_kms_.size(); ++i) {
            if (vel_kms_[i] < v_min || vel_kms_[i - 1] > v_max) continue;
            ImVec2 p1 = map_pt(vel_kms_[i - 1], metrics_.gauss_fit.fitted_curve[i - 1]);
            ImVec2 p2 = map_pt(vel_kms_[i], metrics_.gauss_fit.fitted_curve[i]);
            draw_list->AddLine(p1, p2, IM_COL32(255, 118, 117, 230), 2.0f);
        }
    }

    // Galactic peak centroid
    ImVec2 peak_pt = map_pt(metrics_.peak_vel_kms, metrics_.peak_temp_k);
    draw_list->AddCircleFilled(peak_pt, 6.0f, IM_COL32(255, 71, 87, 255));
    draw_list->AddCircle(peak_pt, 7.0f, IM_COL32(255, 255, 255, 255), 0, 1.5f);

    // Interactive 2D Crosshair Cursor
    ImVec2 mouse_pos = ImGui::GetMousePos();
    if (mouse_pos.x >= canvas_pos.x + margin_left && mouse_pos.x <= canvas_pos.x + canvas_size.x - margin_right &&
        mouse_pos.y >= canvas_pos.y + margin_top && mouse_pos.y <= canvas_pos.y + canvas_size.y - margin_bottom) {
        
        draw_list->AddLine(ImVec2(mouse_pos.x, canvas_pos.y + margin_top), ImVec2(mouse_pos.x, canvas_pos.y + margin_top + plot_h), IM_COL32(102, 230, 255, 180), 1.0f);
        draw_list->AddLine(ImVec2(canvas_pos.x + margin_left, mouse_pos.y), ImVec2(canvas_pos.x + margin_left + plot_w, mouse_pos.y), IM_COL32(102, 230, 255, 180), 1.0f);

        float frac_x = (mouse_pos.x - (canvas_pos.x + margin_left)) / plot_w;
        float frac_y = (canvas_pos.y + margin_top + plot_h - mouse_pos.y) / plot_h;
        double cur_v = v_min + frac_x * (v_max - v_min);
        double cur_k = k_min + frac_y * (k_max - k_min);
        double cur_f = HYDROGEN_REST_FREQ_MHZ * (1.0 - cur_v / SPEED_OF_LIGHT_KMS);

        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "Doppler Velocity: %+.2f km/s", cur_v);
        ImGui::Text("Sky Frequency:    %.4f MHz", cur_f);
        ImGui::Text("Brightness Temp:  %.2f K", cur_k);
        ImGui::EndTooltip();
    }

    // HUD Metrics Overlay
    ImVec2 hud_pos = ImVec2(canvas_pos.x + margin_left + 15, canvas_pos.y + margin_top + 15);
    draw_list->AddRectFilled(hud_pos, ImVec2(hud_pos.x + 310, hud_pos.y + 140), IM_COL32(30, 39, 46, 230), 4.0f);
    draw_list->AddRect(hud_pos, ImVec2(hud_pos.x + 310, hud_pos.y + 140), IM_COL32(0, 210, 211, 255), 4.0f, 0, 1.2f);

    char hud_text[512];
    snprintf(hud_text, sizeof(hud_text),
             "Peak Frequency:  %.4f MHz\n"
             "Recession Vel:   %+.2f km/s\n"
             "Peak Brightness: %.2f K (%+.3f dB)\n"
             "Noise Floor RMS: %.3f K\n"
             "Signal SNR:      %.1f sigma\n"
             "Line FWHM Width: %.1f km/s\n"
             "Integrated Area: %.1f K*km/s\n"
             "N_HI Column:     %.2e cm^-2",
             metrics_.peak_freq_mhz, metrics_.peak_vel_kms,
             metrics_.peak_temp_k, metrics_.peak_power_db,
             metrics_.noise_std_k, metrics_.snr,
             metrics_.fwhm_kms, metrics_.integrated_flux_k_kms,
             metrics_.column_density_cm2);

    draw_list->AddText(ImVec2(hud_pos.x + 10, hud_pos.y + 8), IM_COL32(241, 242, 246, 255), hud_text);
}

void UiAnalyzePanel::render_tab_raw_and_baseline() {
    if (!has_results_) {
        ImGui::TextDisabled("Run scientific analysis to view passband baseline modeling.");
        return;
    }

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 100 || canvas_size.y < 100) return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float top_h = canvas_size.y * 0.62f;
    float bot_h = canvas_size.y * 0.35f;

    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + top_h), IM_COL32(22, 27, 34, 255));
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + top_h), IM_COL32(48, 54, 61, 255));

    ImVec2 bot_pos = ImVec2(canvas_pos.x, canvas_pos.y + top_h + 10);
    draw_list->AddRectFilled(bot_pos, ImVec2(bot_pos.x + canvas_size.x, bot_pos.y + bot_h), IM_COL32(22, 27, 34, 255));
    draw_list->AddRect(bot_pos, ImVec2(bot_pos.x + canvas_size.x, bot_pos.y + bot_h), IM_COL32(48, 54, 61, 255));

    size_t n = stream_res_.freq_mhz.size();
    if (n < 2) return;

    double f_min = stream_res_.freq_mhz.front();
    double f_max = stream_res_.freq_mhz.back();

    auto map_fx = [&](double f) {
        return canvas_pos.x + 60.0f + static_cast<float>((f - f_min) / (f_max - f_min) * (canvas_size.x - 80.0f));
    };

    // Shaded outer filter skirts
    int edge_bins = static_cast<int>(n * config_.passband_edge_fraction);
    float x_pb_l = map_fx(stream_res_.freq_mhz[edge_bins]);
    float x_pb_r = map_fx(stream_res_.freq_mhz[n - 1 - edge_bins]);

    draw_list->AddRectFilled(ImVec2(canvas_pos.x + 60, canvas_pos.y + 20), ImVec2(x_pb_l, canvas_pos.y + top_h - 20), IM_COL32(87, 96, 111, 70));
    draw_list->AddRectFilled(ImVec2(x_pb_r, canvas_pos.y + 20), ImVec2(canvas_pos.x + canvas_size.x - 20, canvas_pos.y + top_h - 20), IM_COL32(87, 96, 111, 70));

    // Protected HI window shaded
    double hi_half_mhz = config_.hi_protection_half_width_khz / 1e3;
    float x_hi_l = map_fx(HYDROGEN_REST_FREQ_MHZ - hi_half_mhz);
    float x_hi_r = map_fx(HYDROGEN_REST_FREQ_MHZ + hi_half_mhz);
    draw_list->AddRectFilled(ImVec2(x_hi_l, canvas_pos.y + 20), ImVec2(x_hi_r, canvas_pos.y + top_h - 20), IM_COL32(16, 172, 132, 50));

    // DC Notch vertical marker
    float x_dc = map_fx(config_.center_freq_hz / 1e6);
    draw_list->AddLine(ImVec2(x_dc, canvas_pos.y + 20), ImVec2(x_dc, canvas_pos.y + top_h - 20), IM_COL32(255, 71, 87, 255), 1.5f);

    // Draw raw vs baseline on top
    double p_min = 1e9, p_max = -1e9;
    std::vector<double> raw_db(n);
    for (size_t i = 0; i < n; ++i) {
        raw_db[i] = 10.0 * std::log10(std::max(stream_res_.averaged_power[i], 1e-12));
        p_min = std::min(p_min, raw_db[i]);
        p_max = std::max(p_max, raw_db[i]);
    }
    p_min -= 1.0; p_max += 1.0;

    auto map_py = [&](double p) {
        return canvas_pos.y + top_h - 30.0f - static_cast<float>((p - p_min) / (p_max - p_min) * (top_h - 60.0f));
    };

    for (size_t i = 1; i < n; ++i) {
        ImVec2 r1(map_fx(stream_res_.freq_mhz[i - 1]), map_py(raw_db[i - 1]));
        ImVec2 r2(map_fx(stream_res_.freq_mhz[i]), map_py(raw_db[i]));
        draw_list->AddLine(r1, r2, IM_COL32(84, 160, 255, 180), 1.0f);

        ImVec2 b1(map_fx(stream_res_.freq_mhz[i - 1]), map_py(baseline_db_[i - 1]));
        ImVec2 b2(map_fx(stream_res_.freq_mhz[i]), map_py(baseline_db_[i]));
        draw_list->AddLine(b1, b2, IM_COL32(255, 159, 67, 255), 2.0f);
    }

    // Residuals on bottom plot
    double res_min = -1.0, res_max = 1.0;
    for (size_t i = 0; i < n; ++i) {
        double r = calibrated_db_[i];
        res_min = std::min(res_min, r);
        res_max = std::max(res_max, r);
    }
    res_min -= 0.2; res_max += 0.2;

    auto map_ry = [&](double r) {
        return bot_pos.y + bot_h - 20.0f - static_cast<float>((r - res_min) / (res_max - res_min) * (bot_h - 40.0f));
    };

    // Zero residual line
    float zero_y = map_ry(0.0);
    draw_list->AddLine(ImVec2(canvas_pos.x + 60, zero_y), ImVec2(canvas_pos.x + canvas_size.x - 20, zero_y), IM_COL32(139, 148, 158, 180), 1.0f);

    for (size_t i = 1; i < n; ++i) {
        ImVec2 p1(map_fx(stream_res_.freq_mhz[i - 1]), map_ry(calibrated_db_[i - 1]));
        ImVec2 p2(map_fx(stream_res_.freq_mhz[i]), map_ry(calibrated_db_[i]));
        draw_list->AddLine(p1, p2, IM_COL32(29, 209, 161, 230), 1.5f);
    }

    // Labels
    draw_list->AddText(ImVec2(canvas_pos.x + 70, canvas_pos.y + 25), IM_COL32(255, 255, 255, 255), "Raw Averaged Spectrum (Blue) vs Standing-Wave Baseline Model (Orange)");
    draw_list->AddText(ImVec2(bot_pos.x + 70, bot_pos.y + 15), IM_COL32(29, 209, 161, 255), "Flattened Passband Residuals P_raw - P_base (dB)");
}

void UiAnalyzePanel::render_tab_waterfall() {
    if (!has_results_ || !waterfall_srv_) {
        ImGui::TextDisabled("Run scientific analysis to view time-frequency waterfall spectrogram.");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 100 || avail.y < 100) return;

    ImGui::Image(reinterpret_cast<ImTextureID>(waterfall_srv_), avail);
}

void UiAnalyzePanel::render_tab_master_dashboard() {
    if (!has_results_) {
        ImGui::TextDisabled("Run scientific analysis to view executive master dashboard.");
        return;
    }

    ImGui::TextColored(ImVec4(0.40f, 0.90f, 1.0f, 1.0f), "Executive 4-Panel Master Summary");
    ImGui::Separator();

    float half_w = (ImGui::GetContentRegionAvail().x - 10) * 0.5f;
    float half_h = (ImGui::GetContentRegionAvail().y - 10) * 0.5f;

    ImGui::BeginChild("Dash_Q1", ImVec2(half_w, half_h), true);
    render_tab_raw_and_baseline();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Dash_Q2", ImVec2(half_w, half_h), true);
    render_tab_calibrated_spectrum();
    ImGui::EndChild();

    ImGui::BeginChild("Dash_Q3", ImVec2(half_w, half_h), true);
    render_tab_calibrated_spectrum();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Dash_Q4", ImVec2(half_w, half_h), true);
    render_tab_waterfall();
    ImGui::EndChild();
}

void UiAnalyzePanel::render_panel(ID3D11DeviceContext* ctx, float width, float height) {
    // Check if auto-analyze was requested by Record -> Analyze bridge
    std::string target;
    if (app_state_.check_and_clear_auto_analyze(target)) {
        if (!target.empty()) {
            select_and_run_file(target, true);
        }
    }

    std::unique_lock<std::mutex> lock(data_mutex_, std::try_to_lock);
    bool data_available = has_results_.load() && lock.owns_lock();

    if (data_available) {
        update_waterfall_texture(ctx);
    }

    ImGui::BeginChild("AnalyzePanelCanvas", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);

    render_top_bar();

    ImGui::Spacing();
    render_sidebar_controls();

    ImGui::SameLine();
    ImGui::BeginChild("AnalyzeMainPlotArea", ImVec2(0, 0), true);

    if (ImGui::BeginTabBar("AnalysisTabs")) {
        if (ImGui::BeginTabItem("Calibrated HI Spectrum")) {
            if (data_available) {
                render_tab_calibrated_spectrum();
            } else {
                ImGui::TextDisabled("Run scientific analysis to view calibrated spectrum.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Raw Bandpass vs Baseline")) {
            if (data_available) {
                render_tab_raw_and_baseline();
            } else {
                ImGui::TextDisabled("Run scientific analysis to view passband baseline modeling.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Waterfall Spectrogram")) {
            if (data_available) {
                render_tab_waterfall();
            } else {
                ImGui::TextDisabled("Run scientific analysis to view time-frequency waterfall spectrogram.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Master Dashboard")) {
            if (data_available) {
                render_tab_master_dashboard();
            } else {
                ImGui::TextDisabled("Run scientific analysis to view executive master dashboard.");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace astro

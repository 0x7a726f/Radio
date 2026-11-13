#include "ui_analyze_panel.h"
#include "baseline_fitter.h"
#include "exporter.h"
#include "rfi_filter.h"
#include "simd_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace astro {

namespace {

static std::string get_exe_parent_output_dir() {
    fs::path dir = ui_obs::win_fs::get_exe_dir() / "output";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

static void build_normalized_waterfall(const StreamingResults& stream,
                                       int& out_w, int& out_h,
                                       std::vector<uint8_t>& out_u8,
                                       double& out_vmin_db, double& out_vmax_db) {
    out_w = 0;
    out_h = 0;
    out_u8.clear();
    out_vmin_db = -10.0;
    out_vmax_db = 10.0;

    const int src_r = stream.waterfall_rows;
    const int src_c = stream.waterfall_cols;
    if (src_r <= 0 || src_c <= 0 || stream.waterfall_data.size() < static_cast<size_t>(src_r) * src_c) {
        return;
    }

    // Estimate median passband reference power for relative dB scale
    std::vector<double> valid_p;
    valid_p.reserve(stream.averaged_power.size());
    for (double p : stream.averaged_power) {
        if (std::isfinite(p) && p > 1e-18) valid_p.push_back(p);
    }
    double ref_power = 1.0;
    if (!valid_p.empty()) {
        size_t mid = valid_p.size() / 2;
        std::nth_element(valid_p.begin(), valid_p.begin() + mid, valid_p.end());
        ref_power = std::max(valid_p[mid], 1e-18);
    }
    const double inv_ref = 1.0 / ref_power;

    // Downsample cleanly within DX11 safe texture caps (<= 2048 x 1024)
    out_w = std::min(src_c, 2048);
    out_h = std::min(src_r, 1024);

    std::vector<double> rel_db(static_cast<size_t>(out_w) * static_cast<size_t>(out_h), 0.0);
    for (int r = 0; r < out_h; ++r) {
        int r0 = static_cast<int>((static_cast<double>(r) / out_h) * src_r);
        int r1 = std::max(r0 + 1, static_cast<int>((static_cast<double>(r + 1) / out_h) * src_r));
        r1 = std::min(r1, src_r);

        for (int c = 0; c < out_w; ++c) {
            int c0 = static_cast<int>((static_cast<double>(c) / out_w) * src_c);
            int c1 = std::max(c0 + 1, static_cast<int>((static_cast<double>(c + 1) / out_w) * src_c));
            c1 = std::min(c1, src_c);

            // Max-pool over sub-bins so narrow H I/RFI peaks remain crisp when downsampled
            double peak_val = 0.0;
            for (int sr = r0; sr < r1; ++sr) {
                const float* row_ptr = stream.waterfall_data.data() + static_cast<size_t>(sr) * src_c;
                for (int sc = c0; sc < c1; ++sc) {
                    double v = static_cast<double>(row_ptr[sc]);
                    if (v > peak_val) peak_val = v;
                }
            }
            rel_db[static_cast<size_t>(r) * out_w + c] = 10.0 * std::log10(std::max(peak_val * inv_ref, 1e-12));
        }
    }

    // Compute 5th .. 99.5th percentile contrast window in O(N)
    std::vector<double> sorted_vals = rel_db;
    const size_t n = sorted_vals.size();
    const size_t idx_low = static_cast<size_t>(0.05 * static_cast<double>(n - 1));
    const size_t idx_high = static_cast<size_t>(0.995 * static_cast<double>(n - 1));
    std::nth_element(sorted_vals.begin(), sorted_vals.begin() + idx_low, sorted_vals.end());
    out_vmin_db = sorted_vals[idx_low];
    std::nth_element(sorted_vals.begin() + idx_low, sorted_vals.begin() + idx_high, sorted_vals.end());
    out_vmax_db = std::max(out_vmin_db + 1.0, sorted_vals[idx_high]);

    const double inv_span = 255.0 / (out_vmax_db - out_vmin_db);
    out_u8.resize(n);
    for (size_t i = 0; i < n; ++i) {
        double scaled = (rel_db[i] - out_vmin_db) * inv_span;
        out_u8[i] = static_cast<uint8_t>(std::clamp(scaled, 0.0, 255.0));
    }
}

} // namespace

UiAnalyzePanel::UiAnalyzePanel(AppState& app_state)
    : app_state_(app_state)
{
    active_lut256_ = ui_obs::build_colormap_lut256(ui_colormap_idx_);
    scan_files();
    if (!discovered_files_.empty()) {
        on_file_selected(discovered_files_[0]);
        selected_file_idx_ = 0;
    }
}

UiAnalyzePanel::~UiAnalyzePanel() {
    cancel_requested_ = true;
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    if (export_thread_ && export_thread_->joinable()) {
        export_thread_->join();
    }
    release_d3d_resources();
}

void UiAnalyzePanel::init_d3d_resources(ID3D11Device* device) {
    d3d_device_ = device;
}

void UiAnalyzePanel::release_d3d_resources() {
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
    last_uploaded_snap_ = nullptr;
}

std::shared_ptr<const AnalysisSnapshot> UiAnalyzePanel::get_snapshot() {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return active_snapshot_;
}

std::string UiAnalyzePanel::get_status_message() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_message_;
}

void UiAnalyzePanel::set_status_message(const std::string& msg) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_message_ = msg;
}

std::string UiAnalyzePanel::get_export_message() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return export_status_msg_;
}

void UiAnalyzePanel::set_export_message(const std::string& msg) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    export_status_msg_ = msg;
}

void UiAnalyzePanel::scan_files() {
    std::vector<std::string> search_dirs = {
        ui_obs::win_fs::get_default_recordings_dir(),
        "recordings",
        "bin/recordings",
        "../recordings",
        "."
    };

    if (custom_file_path_[0] != '\0') {
        std::error_code ec;
        fs::path p(custom_file_path_);
        if (p.has_parent_path() && fs::exists(p.parent_path(), ec)) {
            search_dirs.push_back(p.parent_path().string());
        }
    }

    discovered_files_ = SidecarReader::scan_cs16_files(search_dirs);

    // Resync selected_file_idx_ cleanly without unsafe equivalent() exceptions
    selected_file_idx_ = -1;
    if (custom_file_path_[0] != '\0') {
        std::error_code ec;
        fs::path cur_can = fs::weakly_canonical(fs::path(custom_file_path_), ec);
        for (size_t i = 0; i < discovered_files_.size(); ++i) {
            std::error_code ec2;
            fs::path cand = fs::weakly_canonical(fs::path(discovered_files_[i]), ec2);
            if ((!ec && !ec2 && cur_can == cand) || discovered_files_[i] == custom_file_path_) {
                selected_file_idx_ = static_cast<int>(i);
                break;
            }
        }
    }
}

void UiAnalyzePanel::select_and_run_file(const std::string& path, bool auto_run) {
    scan_files();
    on_file_selected(path);

    selected_file_idx_ = -1;
    std::error_code ec;
    fs::path target_can = fs::weakly_canonical(fs::path(path), ec);
    for (size_t i = 0; i < discovered_files_.size(); ++i) {
        std::error_code ec2;
        fs::path cand = fs::weakly_canonical(fs::path(discovered_files_[i]), ec2);
        if ((!ec && !ec2 && target_can == cand) || discovered_files_[i] == path) {
            selected_file_idx_ = static_cast<int>(i);
            break;
        }
    }

    if (auto_run) {
        start_analysis();
    }
}

void UiAnalyzePanel::on_file_selected(const std::string& path) {
    std::snprintf(custom_file_path_, sizeof(custom_file_path_), "%s", path.c_str());

    current_sidecar_ = SidecarReader::parse(path);
    if (current_sidecar_.found_sidecar || current_sidecar_.center_freq_hz > 0.0) {
        if (current_sidecar_.center_freq_hz > 0.0) {
            ui_center_freq_mhz_ = current_sidecar_.center_freq_hz / 1e6;
        }
        if (current_sidecar_.sample_rate_hz > 0.0) {
            ui_sample_rate_msps_ = current_sidecar_.sample_rate_hz / 1e6;
        }
        if (current_sidecar_.found_sidecar) {
            set_status_message("Loaded sidecar metadata (" + current_sidecar_.sidecar_source + "). Ready.");
        } else {
            set_status_message("Parsed metadata from filename. Ready.");
        }
    } else {
        set_status_message("Selected file. Verify LO / Sample Rate and run H I pipeline.");
    }
}

AnalyzeParamsSig UiAnalyzePanel::current_signature() const {
    AnalyzeParamsSig sig;
    sig.filepath = custom_file_path_;
    sig.center_freq_mhz = ui_center_freq_mhz_;
    sig.sample_rate_msps = ui_sample_rate_msps_;
    sig.fft_size_idx = ui_fft_size_idx_;
    sig.baseline_idx = ui_baseline_idx_;
    sig.tsys_k = ui_tsys_k_;
    sig.duration_s = ui_duration_s_;
    sig.dc_clear_khz = ui_dc_clear_khz_;
    sig.als_lambda = static_cast<double>(ui_als_lambda_);
    sig.als_p = static_cast<double>(ui_als_p_);
    sig.poly_degree = ui_poly_deg_;
    return sig;
}

bool UiAnalyzePanel::validate_inputs(std::string& out_error) const {
    std::string target = custom_file_path_;
    if (target.empty()) {
        out_error = "Select a .cs16 file first.";
        return false;
    }
    if (app_state_.is_file_actively_recording(target)) {
        out_error = "File is currently being written by Live Recorder.";
        return false;
    }
    std::error_code ec;
    if (!fs::exists(target, ec) || fs::is_directory(target, ec)) {
        out_error = "Input .cs16 file does not exist on disk.";
        return false;
    }
    if (ui_center_freq_mhz_ < 1.0 || ui_center_freq_mhz_ > 6000.0) {
        out_error = "Center frequency must be between 1 and 6000 MHz.";
        return false;
    }
    if (ui_sample_rate_msps_ < 0.1 || ui_sample_rate_msps_ > 20.0) {
        out_error = "Sample rate must be between 0.1 and 20.0 MSPS.";
        return false;
    }
    if (ui_tsys_k_ < 1.0f) {
        out_error = "System Temperature T_sys must be >= 1.0 K.";
        return false;
    }
    return true;
}

std::string UiAnalyzePanel::get_output_dir_for_source(const std::string& src_path) const {
    std::string stem = fs::path(src_path).stem().string();
    if (stem.empty()) stem = "analysis";
    fs::path out_dir = fs::path(get_exe_parent_output_dir()) / stem;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    return out_dir.string();
}

void UiAnalyzePanel::start_analysis() {
    if (is_analyzing_.load()) return;

    std::string err;
    if (!validate_inputs(err)) {
        set_status_message("Blocked: " + err);
        return;
    }

    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }

    static constexpr int fft_choices[] = {2048, 4096, 8192, 16384, 32768};
    AstroConfig run_cfg = config_;
    run_cfg.center_freq_hz = ui_center_freq_mhz_ * 1e6;
    run_cfg.sample_rate_hz = ui_sample_rate_msps_ * 1e6;
    run_cfg.fft_size = fft_choices[std::clamp(ui_fft_size_idx_, 0, 4)];
    run_cfg.system_temp_k = static_cast<double>(ui_tsys_k_);
    run_cfg.max_duration_s = static_cast<double>(ui_duration_s_);
    run_cfg.dc_clear_hz = static_cast<double>(ui_dc_clear_khz_) * 1000.0;
    run_cfg.als_lambda = static_cast<double>(ui_als_lambda_);
    run_cfg.als_p = static_cast<double>(ui_als_p_);
    run_cfg.poly_degree = std::clamp(ui_poly_deg_, 2, 20);
    run_cfg.verbose = false;
    run_cfg.baseline_method = (ui_baseline_idx_ == 1) ? BaselineMethod::ALS
                            : (ui_baseline_idx_ == 2) ? BaselineMethod::Poly
                                                      : BaselineMethod::RippleFilter;

    const std::string target_file = custom_file_path_;
    const AnalyzeParamsSig run_sig = current_signature();
    run_cfg.output_dir = get_output_dir_for_source(target_file);

    cancel_requested_ = false;
    is_analyzing_ = true;
    progress_pct_ = 0.0;
    progress_speed_ = 0.0;
    progress_eta_ = 0.0;
    set_status_message("Streaming FFT Accumulation (AVX2 + FMA)...");
    set_export_message("");

    worker_thread_ = std::make_unique<std::thread>([this, target_file, run_cfg, run_sig]() {
        try {
            StreamingProcessor processor(target_file, run_cfg);
            processor.set_cancel_token(&cancel_requested_);

            auto snap = std::make_shared<AnalysisSnapshot>();
            snap->source_filepath = target_file;
            snap->config_used = run_cfg;
            snap->stream = processor.process([this](double pct, double mbps, double eta) {
                progress_pct_.store(pct);
                progress_speed_.store(mbps);
                progress_eta_.store(eta);
            });

            if (cancel_requested_.load() || snap->stream.averaged_power.empty()) {
                set_status_message(cancel_requested_.load() ? "Analysis cancelled." : "Analysis failed: empty stream.");
                is_analyzing_ = false;
                return;
            }

            set_status_message("DC Excision, RFI Mitigation & Baseline Fit...");
            DCExcisionInfo dc_info;
            auto dc_clean = DCExcision::excise(snap->stream.averaged_power, run_cfg, dc_info);

            int num_rfi = 0;
            snap->rfi_clean_power = RFIFilter::mitigate(dc_clean, snap->stream.freq_mhz, run_cfg, num_rfi);

            // Precompute log10 dB passband once on worker thread for zero-cost GUI plotting
            snap->raw_db.resize(snap->rfi_clean_power.size());
            SimdDsp::power_to_db_f64(snap->rfi_clean_power.data(), snap->raw_db.data(), snap->rfi_clean_power.size());

            auto base_res = BaselineFitter::fit(snap->rfi_clean_power, snap->stream.freq_mhz, run_cfg);
            snap->baseline_power = std::move(base_res.baseline_power);
            snap->baseline_db = std::move(base_res.baseline_db);

            const size_t n = snap->rfi_clean_power.size();
            snap->calibrated_k.resize(n);
            snap->calibrated_db.resize(n);
            for (size_t i = 0; i < n; ++i) {
                double bp = std::max(snap->baseline_power[i], 1e-18);
                double ratio = std::max(snap->rfi_clean_power[i] / bp, 1e-12);
                snap->calibrated_k[i] = run_cfg.system_temp_k * (ratio - 1.0);
                snap->calibrated_db[i] = 10.0 * std::log10(ratio);
            }

            snap->vel_kms = Astrophysics::compute_doppler_velocity(snap->stream.freq_hz);

            double local_slope = 0.0, local_intercept = 0.0;
            BaselineFitter::refine_local(snap->vel_kms, snap->calibrated_k, snap->calibrated_db,
                                         run_cfg, local_slope, local_intercept);

            snap->metrics = Astrophysics::calculate_metrics(
                snap->stream.freq_mhz, snap->vel_kms, snap->calibrated_k, snap->calibrated_db, run_cfg);

            set_status_message("Building Normalized Waterfall Texture...");
            build_normalized_waterfall(snap->stream, snap->wf_tex_w, snap->wf_tex_h,
                                       snap->wf_norm_u8, snap->wf_vmin_db, snap->wf_vmax_db);
            snap->wf_duration_s = snap->stream.total_duration_s;

            {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                active_snapshot_ = snap;
                last_run_sig_ = run_sig;
            }

            char done_msg[256];
            std::snprintf(done_msg, sizeof(done_msg),
                          "Done in %.2fs (%.0f MB/s) | Peak: %.4f MHz (%.2f K, %.2f sigma)",
                          snap->stream.elapsed_processing_time_s,
                          snap->stream.throughput_mb_s,
                          snap->metrics.peak_freq_mhz,
                          snap->metrics.peak_temp_k,
                          snap->metrics.snr);
            set_status_message(done_msg);
        } catch (const std::exception& ex) {
            set_status_message(std::string("Pipeline error: ") + ex.what());
        }
        is_analyzing_ = false;
    });
}

void UiAnalyzePanel::cancel_analysis() {
    if (is_analyzing_.load()) {
        cancel_requested_ = true;
        set_status_message("Cancelling streaming worker...");
    }
}

void UiAnalyzePanel::start_export(const std::shared_ptr<const AnalysisSnapshot>& snap) {
    if (!snap || is_exporting_.load() || is_analyzing_.load()) return;

    if (export_thread_ && export_thread_->joinable()) {
        export_thread_->join();
    }

    is_exporting_ = true;
    set_export_message("Exporting 8 scientific artifacts...");

    export_thread_ = std::make_unique<std::thread>([this, snap]() {
        FileMetadata meta;
        meta.filepath = snap->source_filepath;
        meta.filesize = snap->stream.file_size_bytes;
        meta.total_samples = snap->stream.total_samples;
        meta.duration_s = snap->stream.total_duration_s;
        meta.processing_time_s = snap->stream.elapsed_processing_time_s;
        meta.throughput_mb_s = snap->stream.throughput_mb_s;

        std::string out_dir = get_output_dir_for_source(snap->source_filepath);
        bool ok = Exporter::export_all_artifacts(
            out_dir, snap->source_filepath, snap->config_used, meta,
            snap->stream, snap->rfi_clean_power, snap->baseline_power,
            snap->calibrated_k, snap->calibrated_db, snap->metrics);

        set_export_message(ok ? ("Exported 8 artifacts to " + out_dir)
                              : "Export failed (check write permissions).");
        is_exporting_ = false;
    });
}

void UiAnalyzePanel::render_panel(ID3D11DeviceContext* ctx, float width, float height) {
    auto snap = get_snapshot();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("##AnalyzeRoot", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float inner_w = width - 20.0f;
    const float top_h = 50.0f;
    render_top_bar(inner_w, top_h, snap);

    const float rem_h = std::max(120.0f, height - top_h - 24.0f);
    const float sidebar_w = std::min(310.0f, std::max(260.0f, inner_w * 0.24f));
    const float main_w = std::max(260.0f, inner_w - sidebar_w - 8.0f);

    render_sidebar_controls(sidebar_w, rem_h, snap);
    ImGui::SameLine();

    ImGui::BeginChild("##AnalyzeMainCol", ImVec2(main_w, rem_h), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Non-occluding Pinned KPI Strip + Stale-Parameter Banner
    if (snap) {
        // Stale parameter detection
        bool params_dirty = false;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            params_dirty = (current_signature() != last_run_sig_);
        }
        if (params_dirty && !is_analyzing_.load()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ui_obs::palette::Amber.x, ui_obs::palette::Amber.y, ui_obs::palette::Amber.z, 0.14f));
            ImGui::PushStyleColor(ImGuiCol_Border, ui_obs::palette::Amber);
            ImGui::BeginChild("##StaleBanner", ImVec2(main_w, 28.0f), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosY(5.0f);
            ImGui::TextColored(ui_obs::palette::Amber,
                               "Configuration changed since last run - Click 'Run H I Pipeline' to recompute.");
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        render_kpi_strip(main_w, snap->metrics, snap->config_used);
    }

    // Tabs Viewport
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##AnalyzePlotArea", ImVec2(main_w, std::max(100.0f, ImGui::GetContentRegionAvail().y)), true);

    if (!snap) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* title = is_analyzing_.load() ? "Processing I/Q Recording..." : "No H I Analysis Results Yet";
        ImVec2 ts = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2((avail.x - ts.x) * 0.5f, avail.y * 0.44f));
        ui_obs::PushFontSafe(fonts_.semibold);
        ImGui::TextColored(ui_obs::palette::Cyan, "%s", title);
        ImGui::PopFont();

        std::string msg = get_status_message();
        ImVec2 ms = ImGui::CalcTextSize(msg.c_str());
        ImGui::SetCursorPos(ImVec2(std::max(10.0f, (avail.x - ms.x) * 0.5f), avail.y * 0.44f + 26.0f));
        ImGui::TextColored(ui_obs::palette::TextMuted, "%s", msg.c_str());
    } else if (ImGui::BeginTabBar("##AnalyzeTabs")) {
        if (ImGui::BeginTabItem("1. Calibrated H I Spectrum")) {
            render_tab_calibrated_spectrum(snap);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("2. Raw Passband & Baseline")) {
            render_tab_raw_and_baseline(snap);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("3. Time-Frequency Waterfall")) {
            render_tab_waterfall(ctx, snap);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("4. Master Dashboard (2x2)")) {
            render_tab_master_dashboard(ctx, snap);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::EndChild(); // ##AnalyzeMainCol

    ImGui::EndChild(); // ##AnalyzeRoot
    ImGui::PopStyleVar(2);
}

void UiAnalyzePanel::render_top_bar(float width, float height, const std::shared_ptr<const AnalysisSnapshot>& snap) {
    (void)snap;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 6.0f));
    ImGui::BeginChild("##AnalyzeTopBar", ImVec2(width, height), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const bool running = is_analyzing_.load();
    const bool locked_rec = app_state_.is_file_actively_recording(custom_file_path_);

    // 1. Recording Combo + Browse + Refresh
    ImGui::BeginDisabled(running);
    const float right_controls_w = running ? 380.0f : 350.0f;
    const float combo_w = std::max(180.0f, width - right_controls_w - 190.0f);

    std::string disp_name = custom_file_path_[0]
        ? fs::path(custom_file_path_).filename().string()
        : "-- Select .cs16 File --";
    ImGui::SetNextItemWidth(combo_w);
    if (ImGui::BeginCombo("##Cs16Selector", disp_name.c_str())) {
        for (int i = 0; i < static_cast<int>(discovered_files_.size()); ++i) {
            std::string fn = fs::path(discovered_files_[static_cast<size_t>(i)]).filename().string();
            bool active_rec = app_state_.is_file_actively_recording(discovered_files_[static_cast<size_t>(i)]);
            std::string label = active_rec ? (fn + " [RECORDING]") : fn;
            bool is_sel = (selected_file_idx_ == i);
            if (ImGui::Selectable(label.c_str(), is_sel)) {
                selected_file_idx_ = i;
                on_file_selected(discovered_files_[static_cast<size_t>(i)]);
            }
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Browse...", ImVec2(80.0f, height - 14.0f))) {
        std::string picked;
        if (ui_obs::win_fs::browse_for_cs16_file(owner_hwnd_, ui_obs::win_fs::get_default_recordings_dir(), picked)) {
            select_and_run_file(picked, false);
        }
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Refresh", ImVec2(72.0f, height - 14.0f))) {
        scan_files();
    }
    ImGui::EndDisabled();

    // 2. Run / Cancel CTA + Live Progress Bar
    ImGui::SameLine(0.0f, 12.0f);
    if (!running) {
        std::string val_err;
        bool valid = validate_inputs(val_err);
        ImGui::BeginDisabled(!valid || locked_rec);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.184f, 0.506f, 0.969f, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.184f, 0.506f, 0.969f, 0.48f));
        ImGui::PushStyleColor(ImGuiCol_Border, ui_obs::palette::AccentBlue);
        ui_obs::PushFontSafe(fonts_.semibold);
        if (ImGui::Button("Run H I Pipeline", ImVec2(148.0f, height - 14.0f))) {
            start_analysis();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 12.0f);
        ImGui::TextColored(valid ? ui_obs::palette::TextMuted : ui_obs::palette::Amber,
                           "%s", (valid ? get_status_message() : val_err).c_str());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.937f, 0.267f, 0.267f, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.937f, 0.267f, 0.267f, 0.48f));
        ImGui::PushStyleColor(ImGuiCol_Border, ui_obs::palette::Red);
        if (ImGui::Button("Cancel", ImVec2(80.0f, height - 14.0f))) {
            cancel_analysis();
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 10.0f);
        float pct = static_cast<float>(progress_pct_.load() / 100.0);
        char prog_lbl[96];
        std::snprintf(prog_lbl, sizeof(prog_lbl), "%.1f%%  (%.0f MB/s, ETA %.1fs)",
                      progress_pct_.load(), progress_speed_.load(), progress_eta_.load());
        ImGui::ProgressBar(std::clamp(pct, 0.0f, 1.0f), ImVec2(std::max(140.0f, ImGui::GetContentRegionAvail().x), height - 14.0f), prog_lbl);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void UiAnalyzePanel::render_sidebar_controls(float width, float height, const std::shared_ptr<const AnalysisSnapshot>& snap) {
    ImGui::BeginChild("##AnalyzeSidebar", ImVec2(width, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    const bool running = is_analyzing_.load();

    // Card 1: File & Sidecar Metadata
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardMeta", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "RECORDING METADATA");
    ImGui::PopFont();
    ImGui::Separator();

    if (current_sidecar_.found_sidecar) {
        ImGui::TextColored(ui_obs::palette::Emerald, "Sidecar: %s", current_sidecar_.sidecar_source.c_str());
        if (!current_sidecar_.device_name.empty()) {
            ImGui::TextColored(ui_obs::palette::TextMuted, "Device: %s", current_sidecar_.device_name.c_str());
        }
        if (current_sidecar_.has_gain) {
            ImGui::TextColored(ui_obs::palette::TextMuted, "Gain: %.1f dB  |  LO Offset: %+.1f kHz",
                               current_sidecar_.gain_db, current_sidecar_.lo_offset_khz);
        }
    } else {
        ImGui::TextColored(ui_obs::palette::Amber, "No sidecar JSON/TXT found");
        ImGui::TextColored(ui_obs::palette::TextMuted, "Verify RF parameters below.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Card 2: RF & Calibration Parameters
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardCalib", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "RF & CALIBRATION");
    ImGui::PopFont();
    ImGui::Separator();

    ImGui::BeginDisabled(running);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Center Frequency (MHz)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputDouble("##AnaFreq", &ui_center_freq_mhz_, 0.05, 0.5, "%.4f MHz");

    ImGui::TextColored(ui_obs::palette::TextMuted, "Sample Rate (MSPS)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputDouble("##AnaRate", &ui_sample_rate_msps_, 0.1, 0.5, "%.3f MSPS");

    ImGui::TextColored(ui_obs::palette::TextMuted, "System Noise Temp T_sys (K)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputFloat("##AnaTsys", &ui_tsys_k_, 5.0f, 25.0f, "%.1f K")) {
        if (ui_tsys_k_ < 1.0f) ui_tsys_k_ = 1.0f;
    }

    ImGui::TextColored(ui_obs::palette::TextMuted, "Max Time Window (s, 0 = Full)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputFloat("##AnaDur", &ui_duration_s_, 5.0f, 30.0f, "%.1f s")) {
        if (ui_duration_s_ < 0.0f) ui_duration_s_ = 0.0f;
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Card 3: Scientific Pipeline Configuration
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardPipe", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "DSP & BASELINE MODEL");
    ImGui::PopFont();
    ImGui::Separator();

    ImGui::BeginDisabled(running);
    static const char* fft_labels[] = {"2048", "4096", "8192", "16384", "32768"};
    ImGui::TextColored(ui_obs::palette::TextMuted, "Spectral Resolution (FFT)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##AnaFft", &ui_fft_size_idx_, fft_labels, 5);

    ImGui::TextColored(ui_obs::palette::TextMuted, "DC Spike Excision (kHz)");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##AnaDc", &ui_dc_clear_khz_, 0.0f, 30.0f, "%.1f kHz")) {
        if (ui_dc_clear_khz_ < 0.0f) ui_dc_clear_khz_ = 0.0f;
    }

    static const char* base_labels[] = {"Ripple Filter (Gaussian)", "Asymmetric Least Squares", "Sigma-Clipped Polynomial"};
    ImGui::TextColored(ui_obs::palette::TextMuted, "Baseline Removal Method");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##AnaBase", &ui_baseline_idx_, base_labels, 3);

    if (ui_baseline_idx_ == 1) {
        ImGui::TextColored(ui_obs::palette::TextMuted, "ALS Smoothness (Lambda)");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##AlsLam", &ui_als_lambda_, 100.0f, 50000.0f, "%.0f");
        ImGui::TextColored(ui_obs::palette::TextMuted, "ALS Asymmetry (p)");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##AlsP", &ui_als_p_, 0.001f, 0.1f, "%.3f");
    } else if (ui_baseline_idx_ == 2) {
        ImGui::TextColored(ui_obs::palette::TextMuted, "Polynomial Degree");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##PolyDeg", &ui_poly_deg_, 3, 18);
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Card 4: Waterfall Colormap & Artifact Export
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
    ImGui::BeginChild("##CardExport", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::Cyan, "COLORMAP & EXPORT");
    ImGui::PopFont();
    ImGui::Separator();

    static const char* cmaps[] = {"Viridis", "Inferno", "Plasma", "Turbo"};
    ImGui::TextColored(ui_obs::palette::TextMuted, "Waterfall Colormap");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##AnaCmap", &ui_colormap_idx_, cmaps, 4)) {
        active_lut256_ = ui_obs::build_colormap_lut256(ui_colormap_idx_);
    }

    const bool can_export = (snap != nullptr && !running && !is_exporting_.load());
    ImGui::BeginDisabled(!can_export);
    if (ImGui::Button("Export All 8 Artifacts", ImVec2(-1.0f, 32.0f))) {
        start_export(snap);
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Open Output Directory", ImVec2(-1.0f, 28.0f))) {
        std::string out_dir = snap ? get_output_dir_for_source(snap->source_filepath) : get_exe_parent_output_dir();
        ui_obs::win_fs::open_folder_in_explorer(out_dir);
    }

    std::string exp_msg = get_export_message();
    if (!exp_msg.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui_obs::palette::Emerald);
        ImGui::TextWrapped("%s", exp_msg.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::EndChild(); // ##AnalyzeSidebar
}

void UiAnalyzePanel::render_kpi_strip(float width, const AstrophysicalMetrics& m, const AstroConfig& cfg) {
    (void)width;
    if (!ImGui::BeginTable("##AnaKpiStrip", 6, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }

    auto kpi_card = [&](int col, const char* title, const char* val_str, ImVec4 accent) {
        ImGui::TableSetColumnIndex(col);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::Surface);
        ImGui::BeginChild(title, ImVec2(-1.0f, 54.0f), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(ui_obs::palette::TextMuted, "%s", title);
        ui_obs::PushFontSafe(fonts_.mono);
        ImGui::TextColored(accent, "%s", val_str);
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    };

    ImGui::TableNextRow();
    char buf[64];

    std::snprintf(buf, sizeof(buf), "%.4f MHz", m.peak_freq_mhz);
    kpi_card(0, "Peak Frequency", buf, ui_obs::palette::Cyan);

    std::snprintf(buf, sizeof(buf), "%+.2f km/s", m.peak_vel_kms);
    kpi_card(1, "Doppler V_LSR", buf, ui_obs::palette::TextPrimary);

    std::snprintf(buf, sizeof(buf), "%.2f K", m.peak_temp_k);
    kpi_card(2, "Brightness T_A", buf, ui_obs::palette::Emerald);

    ImVec4 snr_col = (m.snr >= 3.0) ? ui_obs::palette::Emerald
                   : (m.snr >= 2.0) ? ui_obs::palette::Amber
                                    : ui_obs::palette::Red;
    std::snprintf(buf, sizeof(buf), "%.2f sigma", m.snr);
    kpi_card(3, "Detection SNR", buf, snr_col);

    std::snprintf(buf, sizeof(buf), "%.2f km/s", m.fwhm_kms);
    kpi_card(4, "Line FWHM", buf, ui_obs::palette::Pink);

    std::snprintf(buf, sizeof(buf), "%.2e /cm2", m.column_density_cm2);
    kpi_card(5, "N_HI Column", buf, cfg.system_temp_k > 0.0 ? ui_obs::palette::TextPrimary : ui_obs::palette::TextMuted);

    ImGui::EndTable();
}

void UiAnalyzePanel::render_tab_calibrated_spectrum(const std::shared_ptr<const AnalysisSnapshot>& snap) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 100.0f || avail.y < 100.0f || snap->vel_kms.size() < 2) return;

    // Plot calibrated T_A (K) vs Doppler Radial Velocity (km/s), zoomed cleanly [-220, +220] km/s if span permits
    const double v_first = snap->vel_kms.front();
    const double v_last  = snap->vel_kms.back();
    const double v_lo_full = std::min(v_first, v_last);
    const double v_hi_full = std::max(v_first, v_last);
    const double x_min = (v_hi_full - v_lo_full > 440.0) ? std::max(v_lo_full, -220.0) : v_lo_full;
    const double x_max = (v_hi_full - v_lo_full > 440.0) ? std::min(v_hi_full,  220.0) : v_hi_full;

    double ymin = 1e9, ymax = -1e9;
    for (size_t i = 0; i < snap->calibrated_k.size(); ++i) {
        double v = snap->vel_kms[i];
        if (v < x_min || v > x_max) continue;
        ymin = std::min(ymin, snap->calibrated_k[i]);
        ymax = std::max(ymax, snap->calibrated_k[i]);
    }
    if (ymin >= ymax) { ymin = -5.0; ymax = 15.0; }
    double pad = std::max(1.0, (ymax - ymin) * 0.14);
    ymin -= pad;
    ymax += pad;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ui_obs::PlotMargin m;
    m.left = 64.0f; m.right = 20.0f; m.top = 34.0f; m.bottom = 34.0f;
    auto vp = ui_obs::PlotViewport2D::create(origin, avail, x_min, x_max, ymin, ymax, m);
    vp.draw_background_and_grid(dl, 7, 5, "%.0f", "%.1f", "Doppler Radial Velocity V_LSR (km/s)", "Antenna Temp T_A (K)", fonts_.mono);

    // Top axis: corresponding Sky Frequency (MHz): f = f0 * (1 - v / c)
    const double f_top_left  = HYDROGEN_REST_FREQ_MHZ * (1.0 - x_min / SPEED_OF_LIGHT_KMS);
    const double f_top_right = HYDROGEN_REST_FREQ_MHZ * (1.0 - x_max / SPEED_OF_LIGHT_KMS);
    vp.draw_top_axis(dl, f_top_left, f_top_right, 7, "%.3f", "Sky Freq (MHz)", fonts_.mono);

    // Zero temperature baseline & H I Rest Velocity (0 km/s)
    if (ymin < 0.0 && ymax > 0.0) {
        float zy = vp.map_y(0.0);
        dl->AddLine(ImVec2(vp.plot_min.x, zy), ImVec2(vp.plot_max.x, zy), ui_obs::palette::U32(ui_obs::palette::TextDim, 0.8f), 1.0f);
    }
    if (0.0 >= x_min && 0.0 <= x_max) {
        float zx = vp.map_x(0.0);
        dl->AddLine(ImVec2(zx, vp.plot_min.y), ImVec2(zx, vp.plot_max.y), ui_obs::palette::U32(ui_obs::palette::Amber, 0.7f), 1.3f);
    }

    vp.draw_decimated_xy(dl, snap->vel_kms.size(),
                         [&](size_t i) { return snap->vel_kms[i]; },
                         [&](size_t i) { return snap->calibrated_k[i]; },
                         ui_obs::palette::U32(ui_obs::palette::Cyan), 1.6f, trace_scratch_);

    if (snap->metrics.gauss_fit.success &&
        snap->metrics.gauss_fit.fitted_curve.size() == snap->vel_kms.size()) {
        vp.draw_decimated_xy(dl, snap->vel_kms.size(),
                             [&](size_t i) { return snap->vel_kms[i]; },
                             [&](size_t i) { return snap->metrics.gauss_fit.fitted_curve[i]; },
                             ui_obs::palette::U32(ui_obs::palette::Pink), 2.2f, trace_scratch_);
    }

    // Legend top-right
    ImVec2 leg(vp.plot_max.x - 190.0f, vp.plot_min.y + 8.0f);
    dl->AddRectFilled(leg, ImVec2(vp.plot_max.x - 8.0f, vp.plot_min.y + 52.0f), ui_obs::palette::U32(ui_obs::palette::Surface, 0.88f), 4.0f);
    dl->AddRect(leg, ImVec2(vp.plot_max.x - 8.0f, vp.plot_min.y + 52.0f), ui_obs::palette::PlotBorder32, 4.0f);
    dl->AddLine(ImVec2(leg.x + 8.0f, leg.y + 13.0f), ImVec2(leg.x + 26.0f, leg.y + 13.0f), ui_obs::palette::U32(ui_obs::palette::Cyan), 2.0f);
    dl->AddText(ImVec2(leg.x + 32.0f, leg.y + 6.0f), ui_obs::palette::U32(ui_obs::palette::TextPrimary), "Calibrated H I");
    dl->AddLine(ImVec2(leg.x + 8.0f, leg.y + 31.0f), ImVec2(leg.x + 26.0f, leg.y + 31.0f), ui_obs::palette::U32(ui_obs::palette::Pink), 2.0f);
    dl->AddText(ImVec2(leg.x + 32.0f, leg.y + 24.0f), ui_obs::palette::U32(ui_obs::palette::TextPrimary), "Gaussian Profile");

    ImGui::InvisibleButton("##CalSpecHit", avail);
    if (ImGui::IsItemHovered() && vp.contains(ImGui::GetIO().MousePos)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        dl->AddLine(ImVec2(mp.x, vp.plot_min.y), ImVec2(mp.x, vp.plot_max.y), ui_obs::palette::PlotCrosshair);
        dl->AddLine(ImVec2(vp.plot_min.x, mp.y), ImVec2(vp.plot_max.x, mp.y), ui_obs::palette::PlotCrosshair);
        double vh = vp.unmap_x(mp.x);
        double th = vp.unmap_y(mp.y);
        double fh = HYDROGEN_REST_FREQ_MHZ * (1.0 - vh / SPEED_OF_LIGHT_KMS);
        ImGui::SetTooltip("V_LSR: %+.2f km/s  |  Freq: %.4f MHz  |  T_A: %.2f K", vh, fh, th);
    }
}

void UiAnalyzePanel::render_tab_raw_and_baseline(const std::shared_ptr<const AnalysisSnapshot>& snap) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 100.0f || avail.y < 100.0f || snap->stream.freq_mhz.size() < 2) return;

    const double f_min = snap->stream.freq_mhz.front();
    const double f_max = snap->stream.freq_mhz.back();

    auto [min_it, max_it] = std::minmax_element(snap->raw_db.begin(), snap->raw_db.end());
    double ymin = *min_it - 1.5;
    double ymax = *max_it + 1.5;
    if (ymin >= ymax) { ymin = -80.0; ymax = 0.0; }

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ui_obs::PlotMargin m;
    m.left = 64.0f; m.right = 20.0f; m.top = 14.0f; m.bottom = 32.0f;
    auto vp = ui_obs::PlotViewport2D::create(origin, avail, f_min, f_max, ymin, ymax, m);
    vp.draw_background_and_grid(dl, 6, 5, "%.2f", "%.1f", "Frequency (MHz)", "Rel Power (dB)", fonts_.mono);

    // Highlight protected HI band window
    double hi_mhz = HYDROGEN_REST_FREQ_MHZ;
    double hi_hw  = snap->config_used.hi_protection_half_width_khz * 1e-3;
    if (hi_mhz + hi_hw >= f_min && hi_mhz - hi_hw <= f_max) {
        float x0 = std::clamp(vp.map_x(hi_mhz - hi_hw), vp.plot_min.x, vp.plot_max.x);
        float x1 = std::clamp(vp.map_x(hi_mhz + hi_hw), vp.plot_min.x, vp.plot_max.x);
        dl->AddRectFilled(ImVec2(x0, vp.plot_min.y), ImVec2(x1, vp.plot_max.y), ui_obs::palette::U32(ui_obs::palette::Amber, 0.08f));
        float hx = vp.map_x(hi_mhz);
        dl->AddLine(ImVec2(hx, vp.plot_min.y), ImVec2(hx, vp.plot_max.y), ui_obs::palette::U32(ui_obs::palette::Amber, 0.7f));
    }

    vp.draw_decimated_trace(dl, snap->raw_db.size(), f_min, f_max,
                            [&](size_t i) { return snap->raw_db[i]; },
                            ui_obs::palette::U32(ui_obs::palette::Cyan), 1.5f, trace_scratch_);

    if (snap->baseline_db.size() == snap->raw_db.size()) {
        vp.draw_decimated_trace(dl, snap->baseline_db.size(), f_min, f_max,
                                [&](size_t i) { return snap->baseline_db[i]; },
                                ui_obs::palette::U32(ui_obs::palette::Orange), 2.1f, trace_scratch_);
    }

    ImGui::InvisibleButton("##RawBaseHit", avail);
    if (ImGui::IsItemHovered() && vp.contains(ImGui::GetIO().MousePos)) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        dl->AddLine(ImVec2(mp.x, vp.plot_min.y), ImVec2(mp.x, vp.plot_max.y), ui_obs::palette::PlotCrosshair);
        dl->AddLine(ImVec2(vp.plot_min.x, mp.y), ImVec2(vp.plot_max.x, mp.y), ui_obs::palette::PlotCrosshair);
        ImGui::SetTooltip("%.4f MHz  |  %.2f dB", vp.unmap_x(mp.x), vp.unmap_y(mp.y));
    }
}

void UiAnalyzePanel::render_waterfall_view(ID3D11DeviceContext* ctx, ImVec2 avail,
                                           const std::shared_ptr<const AnalysisSnapshot>& snap,
                                           const char* img_id) {
    (void)img_id;
    update_waterfall_texture(ctx, snap);
    if (!waterfall_srv_ || avail.x < 120.0f || avail.y < 60.0f) {
        ImGui::TextColored(ui_obs::palette::TextMuted, "No waterfall integration data available.");
        return;
    }

    const float axis_left = 54.0f;
    const float colorbar_right = 76.0f;
    const float bottom_axis = 26.0f;

    float img_w = std::max(40.0f, avail.x - axis_left - colorbar_right);
    float img_h = std::max(40.0f, avail.y - bottom_axis);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 img_min(origin.x + axis_left, origin.y + 4.0f);
    ImVec2 img_max(img_min.x + img_w, img_min.y + img_h - 4.0f);

    ImGui::SetCursorScreenPos(img_min);
    ImGui::Image(reinterpret_cast<ImTextureID>(waterfall_srv_), ImVec2(img_w, img_max.y - img_min.y));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(img_min, img_max, ui_obs::palette::PlotBorder32);

    // Axis ticks around image
    ui_obs::PushFontSafe(fonts_.mono);
    double f0 = snap->stream.freq_mhz.empty() ? 0.0 : snap->stream.freq_mhz.front();
    double f1 = snap->stream.freq_mhz.empty() ? 1.0 : snap->stream.freq_mhz.back();
    char buf[32];
    for (int i = 0; i <= 4; ++i) {
        double frac = static_cast<double>(i) / 4.0;
        float px = img_min.x + static_cast<float>(frac) * img_w;
        std::snprintf(buf, sizeof(buf), "%.2f", f0 + frac * (f1 - f0));
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(std::clamp(px - ts.x * 0.5f, img_min.x, img_max.x - ts.x), img_max.y + 4.0f),
                    ui_obs::palette::U32(ui_obs::palette::TextMuted), buf);
    }
    for (int i = 0; i <= 3; ++i) {
        double frac = static_cast<double>(i) / 3.0;
        float py = img_min.y + static_cast<float>(frac) * (img_max.y - img_min.y);
        std::snprintf(buf, sizeof(buf), "%.0fs", frac * snap->wf_duration_s);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(img_min.x - ts.x - 6.0f, py - ts.y * 0.5f),
                    ui_obs::palette::U32(ui_obs::palette::TextMuted), buf);
    }
    ImGui::PopFont();

    ui_obs::draw_vertical_colorbar(
        dl,
        ImVec2(img_max.x + 10.0f, img_min.y),
        ImVec2(img_max.x + 24.0f, img_max.y),
        snap->wf_vmin_db, snap->wf_vmax_db, "%.1f", active_lut256_, fonts_.mono
    );

    if (ImGui::IsItemHovered()) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        double u = std::clamp((mp.x - img_min.x) / img_w, 0.0f, 1.0f);
        double v = std::clamp((mp.y - img_min.y) / (img_max.y - img_min.y), 0.0f, 1.0f);
        double freq = f0 + u * (f1 - f0);
        double t_s = v * snap->wf_duration_s;
        ImGui::SetTooltip("%.4f MHz  |  t = %.1f s  |  Contrast [%.1f, %.1f] dB",
                          freq, t_s, snap->wf_vmin_db, snap->wf_vmax_db);
    }
}

void UiAnalyzePanel::render_tab_waterfall(ID3D11DeviceContext* ctx, const std::shared_ptr<const AnalysisSnapshot>& snap) {
    render_waterfall_view(ctx, ImGui::GetContentRegionAvail(), snap, "##TabWfImg");
}

void UiAnalyzePanel::render_tab_master_dashboard(ID3D11DeviceContext* ctx, const std::shared_ptr<const AnalysisSnapshot>& snap) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 160.0f || avail.y < 140.0f) return;

    const float cell_w = (avail.x - 10.0f) * 0.5f;
    const float cell_h = (avail.y - 10.0f) * 0.5f;

    // Row 1: Q1 Raw & Baseline | Q2 Calibrated H I Freq Spectrum (dB)
    ImGui::BeginChild("##Q1", ImVec2(cell_w, cell_h), true);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Q1: Raw Passband & Baseline (dB)");
    render_tab_raw_and_baseline(snap);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##Q2", ImVec2(cell_w, cell_h), true);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Q2: Calibrated H I Frequency Spectrum (dB vs MHz)");
    {
        ImVec2 q2_av = ImGui::GetContentRegionAvail();
        if (q2_av.x > 60.0f && q2_av.y > 40.0f && snap->stream.freq_mhz.size() >= 2 &&
            snap->calibrated_db.size() == snap->stream.freq_mhz.size()) {
            double f0 = snap->stream.freq_mhz.front();
            double f1 = snap->stream.freq_mhz.back();
            auto [mn, mx] = std::minmax_element(snap->calibrated_db.begin(), snap->calibrated_db.end());
            double ymin = std::min(-0.2, *mn - 0.15);
            double ymax = std::max(0.4,  *mx + 0.15);

            ui_obs::PlotMargin m;
            m.left = 54.0f; m.right = 14.0f; m.top = 10.0f; m.bottom = 28.0f;
            auto vp = ui_obs::PlotViewport2D::create(ImGui::GetCursorScreenPos(), q2_av, f0, f1, ymin, ymax, m);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            vp.draw_background_and_grid(dl, 5, 4, "%.2f", "%.2f", "Frequency (MHz)", "dB", fonts_.mono);
            if (ymin < 0.0 && ymax > 0.0) {
                float zy = vp.map_y(0.0);
                dl->AddLine(ImVec2(vp.plot_min.x, zy), ImVec2(vp.plot_max.x, zy), ui_obs::palette::U32(ui_obs::palette::TextDim));
            }
            if (HYDROGEN_REST_FREQ_MHZ >= f0 && HYDROGEN_REST_FREQ_MHZ <= f1) {
                float hx = vp.map_x(HYDROGEN_REST_FREQ_MHZ);
                dl->AddLine(ImVec2(hx, vp.plot_min.y), ImVec2(hx, vp.plot_max.y), ui_obs::palette::U32(ui_obs::palette::Amber, 0.7f));
            }
            vp.draw_decimated_trace(dl, snap->calibrated_db.size(), f0, f1,
                                    [&](size_t i) { return snap->calibrated_db[i]; },
                                    ui_obs::palette::U32(ui_obs::palette::Cyan), 1.5f, trace_scratch_);
            ImGui::InvisibleButton("##Q2Hit", q2_av);
        }
    }
    ImGui::EndChild();

    // Row 2: Q3 Doppler Velocity Profile (T_A K + Fit) | Q4 Integrated Waterfall
    ImGui::BeginChild("##Q3", ImVec2(cell_w, cell_h), true);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Q3: Doppler Velocity T_A (K) & Gaussian Fit");
    render_tab_calibrated_spectrum(snap);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##Q4", ImVec2(cell_w, cell_h), true);
    ImGui::TextColored(ui_obs::palette::TextMuted, "Q4: Integrated Waterfall History");
    render_waterfall_view(ctx, ImGui::GetContentRegionAvail(), snap, "##Q4WfImg");
    ImGui::EndChild();
}

void UiAnalyzePanel::update_waterfall_texture(ID3D11DeviceContext* ctx, const std::shared_ptr<const AnalysisSnapshot>& snap) {
    if (!d3d_device_ || !ctx || !snap || snap->wf_tex_w <= 0 || snap->wf_tex_h <= 0 || snap->wf_norm_u8.empty()) {
        return;
    }

    const int w = snap->wf_tex_w;
    const int h = snap->wf_tex_h;

    // Fast early-out if GPU texture already matches snapshot and colormap!
    if (waterfall_texture_ && last_uploaded_snap_ == snap.get() && last_colormap_idx_ == ui_colormap_idx_ &&
        tex_width_ == w && tex_height_ == h) {
        return;
    }

    if (!waterfall_texture_ || tex_width_ != w || tex_height_ != h) {
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
        const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h);
        if (rgba_upload_scratch_.size() != total) {
            rgba_upload_scratch_.resize(total);
        }
        const uint32_t* lut = active_lut256_.data();
        const uint8_t* src = snap->wf_norm_u8.data();
        for (size_t i = 0; i < total; ++i) {
            rgba_upload_scratch_[i] = lut[src[i]];
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(waterfall_texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
            const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint32_t);
            for (int y = 0; y < h; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * mapped.RowPitch,
                            rgba_upload_scratch_.data() + static_cast<size_t>(y) * static_cast<size_t>(w),
                            row_bytes);
            }
            ctx->Unmap(waterfall_texture_, 0);
        }

        last_uploaded_snap_ = snap.get();
        last_colormap_idx_ = ui_colormap_idx_;
    }
}

} // namespace astro

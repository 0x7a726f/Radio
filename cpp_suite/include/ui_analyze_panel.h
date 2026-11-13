#pragma once

#include <atomic>
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "app_state.h"
#include "astrophysics.h"
#include "config.h"
#include "constants.h"
#include "dc_excision.h"
#include "sidecar_reader.h"
#include "streaming_processor.h"
#include "ui_observatory.h"

namespace astro {

struct AnalysisSnapshot {
    std::string source_filepath;
    AstroConfig config_used;
    StreamingResults stream;
    std::vector<double> raw_db;
    std::vector<double> rfi_clean_power;
    std::vector<double> baseline_power;
    std::vector<double> baseline_db;
    std::vector<double> calibrated_k;
    std::vector<double> calibrated_db;
    std::vector<double> vel_kms;
    AstrophysicalMetrics metrics;

    // Pre-normalized 8-bit waterfall (downsampled <= 2048x1024) + percentile bounds
    int wf_tex_w = 0;
    int wf_tex_h = 0;
    std::vector<uint8_t> wf_norm_u8;
    double wf_vmin_db = -10.0;
    double wf_vmax_db = 10.0;
    double wf_duration_s = 0.0;
};

struct AnalyzeParamsSig {
    std::string filepath;
    double center_freq_mhz = 1420.5;
    double sample_rate_msps = 2.4;
    int fft_size_idx = 2;
    int baseline_idx = 0;
    float tsys_k = 100.0f;
    float duration_s = 0.0f;
    float dc_clear_khz = 5.0f;
    double als_lambda = 2000.0;
    double als_p = 0.02;
    int poly_degree = 10;

    bool operator==(const AnalyzeParamsSig& o) const {
        return filepath == o.filepath &&
               std::abs(center_freq_mhz - o.center_freq_mhz) < 1e-6 &&
               std::abs(sample_rate_msps - o.sample_rate_msps) < 1e-6 &&
               fft_size_idx == o.fft_size_idx &&
               baseline_idx == o.baseline_idx &&
               std::abs(tsys_k - o.tsys_k) < 1e-3f &&
               std::abs(duration_s - o.duration_s) < 1e-3f &&
               std::abs(dc_clear_khz - o.dc_clear_khz) < 1e-3f &&
               std::abs(als_lambda - o.als_lambda) < 1e-3 &&
               std::abs(als_p - o.als_p) < 1e-5 &&
               poly_degree == o.poly_degree;
    }
    bool operator!=(const AnalyzeParamsSig& o) const { return !(*this == o); }
};

class UiAnalyzePanel {
public:
    explicit UiAnalyzePanel(AppState& app_state);
    ~UiAnalyzePanel();

    void set_fonts(const ui_obs::UiFonts& fonts) { fonts_ = fonts; }
    void set_owner_hwnd(HWND hwnd) { owner_hwnd_ = hwnd; }

    void init_d3d_resources(ID3D11Device* device);
    void release_d3d_resources();

    void render_panel(ID3D11DeviceContext* ctx, float width, float height);

    void select_and_run_file(const std::string& path, bool auto_run = true);
    void scan_files();

    bool is_analyzing() const { return is_analyzing_.load(); }

private:
    void on_file_selected(const std::string& path);
    void start_analysis();
    void cancel_analysis();
    void start_export(const std::shared_ptr<const AnalysisSnapshot>& snap);

    AnalyzeParamsSig current_signature() const;
    bool validate_inputs(std::string& out_error) const;
    std::string get_output_dir_for_source(const std::string& src_path) const;

    std::shared_ptr<const AnalysisSnapshot> get_snapshot();
    std::string get_status_message();
    void set_status_message(const std::string& msg);
    std::string get_export_message();
    void set_export_message(const std::string& msg);

    void render_top_bar(float width, float height, const std::shared_ptr<const AnalysisSnapshot>& snap);
    void render_sidebar_controls(float width, float height, const std::shared_ptr<const AnalysisSnapshot>& snap);
    void render_kpi_strip(float width, const AstrophysicalMetrics& m, const AstroConfig& cfg);
    void render_waterfall_view(ID3D11DeviceContext* ctx, ImVec2 avail,
                               const std::shared_ptr<const AnalysisSnapshot>& snap,
                               const char* img_id);

    void render_tab_calibrated_spectrum(const std::shared_ptr<const AnalysisSnapshot>& snap);
    void render_tab_raw_and_baseline(const std::shared_ptr<const AnalysisSnapshot>& snap);
    void render_tab_waterfall(ID3D11DeviceContext* ctx, const std::shared_ptr<const AnalysisSnapshot>& snap);
    void render_tab_master_dashboard(ID3D11DeviceContext* ctx, const std::shared_ptr<const AnalysisSnapshot>& snap);

    void update_waterfall_texture(ID3D11DeviceContext* ctx, const std::shared_ptr<const AnalysisSnapshot>& snap);

    AppState& app_state_;
    ui_obs::UiFonts fonts_;
    HWND owner_hwnd_ = nullptr;

    // Direct3D 11 Resources
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11Texture2D* waterfall_texture_ = nullptr;
    ID3D11ShaderResourceView* waterfall_srv_ = nullptr;
    int tex_width_ = 0;
    int tex_height_ = 0;
    const AnalysisSnapshot* last_uploaded_snap_ = nullptr;
    int last_colormap_idx_ = -1;

    // File Selection State
    std::vector<std::string> discovered_files_;
    int selected_file_idx_ = -1;
    char custom_file_path_[512] = "";
    ObservationMetadata current_sidecar_;

    // Configuration Controls
    AstroConfig config_;
    double ui_center_freq_mhz_ = 1420.5;
    double ui_sample_rate_msps_ = 2.4;
    int ui_fft_size_idx_ = 2; // 8192
    int ui_baseline_idx_ = 0; // RippleFilter
    float ui_tsys_k_ = 100.0f;
    float ui_duration_s_ = 0.0f;
    float ui_dc_clear_khz_ = 5.0f;
    float ui_als_lambda_ = 2000.0f;
    float ui_als_p_ = 0.02f;
    int ui_poly_deg_ = 10;
    int ui_colormap_idx_ = 0; // Viridis

    // Asynchronous Execution & Thread-Safe Status
    std::atomic<bool> is_analyzing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> is_exporting_{false};
    std::atomic<double> progress_pct_{0.0};
    std::atomic<double> progress_speed_{0.0};
    std::atomic<double> progress_eta_{0.0};

    std::mutex status_mutex_;
    std::string status_message_ = "Ready. Select a .cs16 recording to begin.";
    std::string export_status_msg_;

    std::unique_ptr<std::thread> worker_thread_;
    std::unique_ptr<std::thread> export_thread_;

    // Lock-free-to-render snapshot swap
    std::mutex snapshot_mutex_;
    std::shared_ptr<const AnalysisSnapshot> active_snapshot_;
    AnalyzeParamsSig last_run_sig_;

    // Reusable UI plot vertex & colormap scratch buffers
    std::vector<ImVec2> trace_scratch_;
    std::vector<uint32_t> active_lut256_;
    std::vector<uint32_t> rgba_upload_scratch_;
};

} // namespace astro

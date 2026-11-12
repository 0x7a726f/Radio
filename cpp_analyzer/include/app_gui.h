#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <d3d11.h>

#include "config.h"
#include "astrophysics.h"
#include "streaming_processor.h"
#include "sidecar_reader.h"
#include "plot_renderer.h"
#include "dc_excision.h"

namespace astro {

class AppGui {
public:
    AppGui();
    ~AppGui();

    void init_d3d_resources(ID3D11Device* device);
    void release_d3d_resources();
    void render_frame(ID3D11DeviceContext* ctx, float width, float height);

private:
    void setup_satdump_theme();
    void scan_files();
    void on_file_selected(const std::string& path);
    void start_analysis();

    // UI Panels
    void render_top_bar();
    void render_sidebar_controls();
    void render_tab_calibrated_spectrum();
    void render_tab_raw_and_baseline();
    void render_tab_waterfall();
    void render_tab_master_dashboard();
    void update_waterfall_texture(ID3D11DeviceContext* ctx);

    // Direct3D 11 Resources
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11Texture2D* waterfall_texture_ = nullptr;
    ID3D11ShaderResourceView* waterfall_srv_ = nullptr;
    int tex_width_ = 0;
    int tex_height_ = 0;

    // File Selection State
    std::vector<std::string> discovered_files_;
    int selected_file_idx_ = -1;
    char custom_file_path_[512] = "";
    ObservationMetadata current_sidecar_;

    // Configuration & Tuning Controls
    AstroConfig config_;
    double ui_center_freq_mhz_ = 1420.5;
    double ui_sample_rate_msps_ = 2.4;
    int ui_fft_size_idx_ = 2; // 0=2048, 1=4096, 2=8192, 3=16384, 4=32768
    int ui_baseline_idx_ = 0; // 0=Ripple Filter, 1=ALS, 2=Poly
    float ui_tsys_k_ = 100.0f;
    float ui_duration_s_ = 0.0f;
    float ui_dc_clear_khz_ = 5.0f;
    int ui_colormap_idx_ = 0; // 0=Magma, 1=Viridis, 2=Turbo, 3=Classic

    // Asynchronous Execution & Progress
    std::atomic<bool> is_analyzing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<double> progress_pct_{0.0};
    std::atomic<double> progress_speed_{0.0};
    std::atomic<double> progress_eta_{0.0};
    std::string status_message_ = "Ready. Select a CS16 file to begin.";
    std::unique_ptr<std::thread> worker_thread_;
    std::mutex data_mutex_;

    // Analysis Results & Metrics
    std::atomic<bool> has_results_{false};
    StreamingResults stream_res_;
    std::vector<double> rfi_clean_power_;
    std::vector<double> baseline_power_;
    std::vector<double> baseline_db_;
    std::vector<double> calibrated_k_;
    std::vector<double> calibrated_db_;
    std::vector<double> vel_kms_;
    AstrophysicalMetrics metrics_;
    int num_rfi_excised_ = 0;
    DCExcisionInfo dc_info_;
    std::string export_status_msg_;
};

} // namespace astro

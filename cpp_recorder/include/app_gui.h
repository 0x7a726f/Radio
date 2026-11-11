#pragma once

#include "sdr_controller.h"
#include "dsp_pipeline.h"
#include "cs16_writer.h"
#include "waterfall.h"

#include <d3d11.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

class AppGui {
public:
    AppGui(SdrController& controller, DspPipeline& dsp, Cs16Writer& writer, Waterfall& waterfall);
    ~AppGui();

    void init_d3d_resources(ID3D11Device* device);
    void release_d3d_resources();

    void render_frame(ID3D11DeviceContext* context, float display_w, float display_h);

    // Apply frequency change with complete thread safety and DSP flush
    void set_frequency(uint64_t freq_hz);
    void step_frequency(int64_t delta_hz);

private:
    void setup_imgui_theme();
    void render_top_ribbon();
    void render_sidebar();
    void render_device_panel();
    void render_fft_panel();
    void render_telemetry_panel();
    void render_recording_panel();
    void render_display_panel(ID3D11DeviceContext* context, float width, float height);
    void render_spectrum_plot(float width, float height);
    void render_waterfall_plot(ID3D11DeviceContext* context, float width, float height);
    void render_status_bar(float width, float height);

    void update_waterfall_texture(ID3D11DeviceContext* context);
    void refresh_devices();
    void launch_offline_analyzer();

    SdrController& sdr_;
    DspPipeline& dsp_;
    Cs16Writer& writer_;
    Waterfall& waterfall_;

    // DirectX 11 texture resources for waterfall
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11Texture2D* waterfall_texture_ = nullptr;
    ID3D11ShaderResourceView* waterfall_srv_ = nullptr;
    int tex_width_ = 0;
    int tex_height_ = 0;

    // UI state
    int selected_device_idx_ = 0;
    std::vector<std::string> device_list_;
    std::string device_status_text_;
    uint32_t device_status_color_ = 0xFF2ED573; // Green by default

    char tune_input_buf_[32] = "1420.500";
    int selected_preset_idx_ = 0;
    int selected_sample_rate_idx_ = 2; // 2.4 Msps
    int ppm_correction_ = 0;
    float lna_gain_db_ = 25.4f;
    bool agc_enabled_ = false;
    bool bias_tee_enabled_ = false;

    int selected_fft_size_idx_ = 1; // 4096
    float fft_max_db_ = -50.0f;
    float fft_min_db_ = -105.0f;
    float avg_factor_ = 0.65f;
    int selected_palette_idx_ = 0;
    bool show_waterfall_ = true;
    bool show_freq_scale_ = true;
    bool fill_spectrum_ = true; // SatDump electric gradient under curve
    float waterfall_rate_fps_ = 30.0f;

    int selected_format_idx_ = 0; // cs16
    RecordingStats last_stats_;
    std::chrono::high_resolution_clock::time_point last_stats_time_;
    std::chrono::high_resolution_clock::time_point last_waterfall_update_time_;

    // Cached spectrum for display
    std::vector<float> spectrum_data_;
    float peak_freq_hz_ = 0.0f;
    float peak_dbfs_ = -100.0f;
};

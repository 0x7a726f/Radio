#pragma once

#include "sdr_controller.h"
#include "dsp_pipeline.h"
#include "cs16_writer.h"
#include "waterfall.h"
#include "app_state.h"
#include "ui_observatory.h"

#include <d3d11.h>
#include <chrono>
#include <string>
#include <vector>

class UiRecordPanel {
public:
    UiRecordPanel(SdrController& controller, DspPipeline& dsp, Cs16Writer& writer, Waterfall& waterfall, AppState& app_state);
    ~UiRecordPanel();

    void set_fonts(const ui_obs::UiFonts& fonts) { fonts_ = fonts; }
    void init_d3d_resources(ID3D11Device* device);
    void release_d3d_resources();

    void render_panel(ID3D11DeviceContext* context, float width, float height);

    void set_frequency(uint64_t freq_hz);
    void step_frequency(int64_t delta_hz);
    void sync_device_selection();

private:
    struct DeviceEntry {
        std::string label;
        bool is_virtual = true;
        uint32_t hw_index = 0;
    };

    void refresh_devices();
    float snap_gain_to_valid(float requested_db) const;

    void render_top_ribbon(float width, float height);
    void render_sidebar(float width, float height);
    void render_device_card();
    void render_radio_card();
    void render_dsp_card();
    void render_recording_card();
    void render_bias_tee_modal();

    void render_display_area(ID3D11DeviceContext* context, float width, float height);
    void render_spectrum_card(float width, float height);
    void render_waterfall_card(ID3D11DeviceContext* context, float width, float height);
    void update_waterfall_texture(ID3D11DeviceContext* context);

    SdrController& sdr_;
    DspPipeline& dsp_;
    Cs16Writer& writer_;
    Waterfall& waterfall_;
    AppState& app_state_;
    ui_obs::UiFonts fonts_;

    // DirectX 11 waterfall GPU texture
    ID3D11Device* d3d_device_ = nullptr;
    ID3D11Texture2D* waterfall_texture_ = nullptr;
    ID3D11ShaderResourceView* waterfall_srv_ = nullptr;
    int tex_width_ = 0;
    int tex_height_ = 0;

    // Hardware & Simulator entries
    std::vector<DeviceEntry> device_entries_;
    int selected_device_idx_ = 0;
    std::vector<float> valid_gains_;
    std::string device_status_text_;
    bool device_status_ok_ = true;

    // Radio tuning & frontend parameters
    char tune_input_buf_[32] = "1420.500";
    int selected_preset_idx_ = 0;
    int selected_sample_rate_idx_ = 2; // 2.4 Msps
    int ppm_correction_ = 0;
    float lna_gain_db_ = SdrConstants::DEFAULT_GAIN_DB;
    bool agc_enabled_ = false;
    bool bias_tee_enabled_ = false;
    bool open_bias_modal_ = false;

    // DSP & Display controls
    int selected_fft_size_idx_ = 1; // 4096
    float fft_max_db_ = SdrConstants::DEFAULT_FFT_MAX_DB;
    float fft_min_db_ = SdrConstants::DEFAULT_FFT_MIN_DB;
    float avg_factor_ = SdrConstants::DEFAULT_AVG_FACTOR;
    int selected_palette_idx_ = 1; // Viridis
    bool show_waterfall_ = true;
    bool fill_spectrum_ = true;

    // Recording settings & live telemetry
    char record_prefix_buf_[64] = "hi_21cm";
    float record_duration_s_ = 0.0f;
    RecordingStats last_stats_;
    std::chrono::steady_clock::time_point last_stats_time_{};
    std::chrono::steady_clock::time_point last_wf_update_time_{};

    // Cached spectrum trace & reusable plot vertex scratch buffer
    std::vector<float> spectrum_data_;
    std::vector<ImVec2> trace_scratch_;
    std::vector<uint32_t> colorbar_lut_scratch_;
    float peak_freq_hz_ = 0.0f;
    float peak_dbfs_ = -110.0f;
};

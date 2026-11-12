#pragma once

#include "app_state.h"
#include "sdr_controller.h"
#include "dsp_pipeline.h"
#include "cs16_writer.h"
#include "waterfall.h"
#include "ui_record_panel.h"
#include "ui_analyze_panel.h"

#include <d3d11.h>
#include <memory>
#include <string>

class AppGui {
public:
    AppGui();
    ~AppGui();

    void init_d3d_resources(ID3D11Device* device);
    void release_d3d_resources();

    void render_frame(ID3D11DeviceContext* context, float display_w, float display_h);

    AppState& get_state() { return state_; }

private:
    void setup_satdump_theme();
    void render_top_navigation_bar(float width, float height);
    void render_status_bar(float width, float height);
    size_t get_process_memory_mb();

    AppState state_;

    // Core SDR & DSP Subsystems
    SdrController sdr_;
    DspPipeline dsp_;
    Cs16Writer writer_;
    Waterfall waterfall_;

    // Section Panels
    UiRecordPanel record_panel_;
    astro::UiAnalyzePanel analyze_panel_;

    ID3D11Device* d3d_device_ = nullptr;
};

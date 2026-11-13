#include "app_gui.h"
#include "constants.h"
#include "simd_dsp.h"

#include <psapi.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>

AppGui::AppGui()
    : writer_(ui_obs::win_fs::get_default_recordings_dir()),
      waterfall_(SdrConstants::DEFAULT_WATERFALL_WIDTH, SdrConstants::DEFAULT_WATERFALL_HEIGHT),
      record_panel_(sdr_, dsp_, writer_, waterfall_, state_),
      analyze_panel_(state_)
{
    cached_simd_name_ = SimdDsp::get_simd_level_name();

    // Route live SDR packets into real-time FFT + async CS16 recorder
    sdr_.add_sample_callback([this](const uint8_t* data, size_t len) {
        dsp_.feed_samples(data, len);
        writer_.feed_samples(data, len);
    });
    sdr_.set_freq_changed_callback([this](uint64_t) {
        dsp_.flush();
    });

    sdr_.initialize(false, 0);
    sdr_.start_streaming();
    record_panel_.sync_device_selection();

    setup_observatory_theme();
    load_fonts();
}

AppGui::~AppGui() {
    sdr_.stop_streaming();
    writer_.stop_recording();
    release_d3d_resources();
}

void AppGui::set_owner_hwnd(HWND hwnd) {
    owner_hwnd_ = hwnd;
    analyze_panel_.set_owner_hwnd(hwnd);
}

void AppGui::setup_observatory_theme() {
    ui_obs::apply_observatory_slate_style();
}

void AppGui::load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    wchar_t win_dir_w[MAX_PATH] = {};
    UINT len = GetWindowsDirectoryW(win_dir_w, MAX_PATH);
    std::filesystem::path fonts_dir = (len > 0 && len < MAX_PATH)
        ? (std::filesystem::path(win_dir_w) / "Fonts")
        : std::filesystem::path("C:/Windows/Fonts");

    auto try_load = [&](const char* filename, float size_px) -> ImFont* {
        std::filesystem::path p = fonts_dir / filename;
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            std::string utf8_p = ui_obs::win_fs::wide_to_utf8(p.c_str());
            if (!utf8_p.empty()) {
                return io.Fonts->AddFontFromFileTTF(utf8_p.c_str(), size_px);
            }
        }
        return nullptr;
    };

    fonts_.regular  = try_load("segoeui.ttf", 15.5f);
    fonts_.semibold = try_load("seguisb.ttf", 16.0f);
    fonts_.mono     = try_load("consola.ttf", 14.5f);

    if (!fonts_.regular) {
        fonts_.regular = io.Fonts->AddFontDefault();
    }
    if (!fonts_.semibold) fonts_.semibold = fonts_.regular;
    if (!fonts_.mono)     fonts_.mono     = fonts_.regular;

    io.FontDefault = fonts_.regular;

    record_panel_.set_fonts(fonts_);
    analyze_panel_.set_fonts(fonts_);
}

void AppGui::init_d3d_resources(ID3D11Device* device) {
    d3d_device_ = device;
    record_panel_.init_d3d_resources(device);
    analyze_panel_.init_d3d_resources(device);
}

void AppGui::release_d3d_resources() {
    record_panel_.release_d3d_resources();
    analyze_panel_.release_d3d_resources();
}

size_t AppGui::poll_process_memory_mb() {
    auto now = std::chrono::steady_clock::now();
    if (last_ram_poll_.time_since_epoch().count() != 0) {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ram_poll_).count();
        if (elapsed_ms < 500) {
            return cached_ram_mb_;
        }
    }
    last_ram_poll_ = now;
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        cached_ram_mb_ = static_cast<size_t>(pmc.WorkingSetSize / (1024 * 1024));
    }
    return cached_ram_mb_;
}

void AppGui::render_top_navigation_bar(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::HeaderBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 0.0f));
    ImGui::BeginChild("##ObsTopNav", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    dl->AddLine(
        ImVec2(win_pos.x, win_pos.y + height - 1.0f),
        ImVec2(win_pos.x + width, win_pos.y + height - 1.0f),
        ui_obs::palette::U32(ui_obs::palette::Border),
        1.0f
    );

    const float mid_y = win_pos.y + height * 0.5f;

    // 1. Left Brand & H I Badge
    ui_obs::icons::draw_dish(dl, ImVec2(win_pos.x + 24.0f, mid_y), 9.5f, ui_obs::palette::U32(ui_obs::palette::Cyan));
    ImGui::SetCursorPos(ImVec2(42.0f, (height - ImGui::GetTextLineHeight()) * 0.5f));
    ui_obs::PushFontSafe(fonts_.semibold);
    ImGui::TextColored(ui_obs::palette::TextPrimary, "OBSERVATORY SUITE");
    ImGui::PopFont();

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetCursorPosY((height - 22.0f) * 0.5f);
    ImVec2 tag_pos = ImGui::GetCursorScreenPos();
    const char* band_txt = "21cm H I";
    ImVec2 tag_sz = ImGui::CalcTextSize(band_txt);
    dl->AddRectFilled(tag_pos, ImVec2(tag_pos.x + tag_sz.x + 14.0f, tag_pos.y + 22.0f),
                      ui_obs::palette::U32(ui_obs::palette::Emerald, 0.14f), 4.0f);
    dl->AddRect(tag_pos, ImVec2(tag_pos.x + tag_sz.x + 14.0f, tag_pos.y + 22.0f),
                ui_obs::palette::U32(ui_obs::palette::Emerald, 0.45f), 4.0f);
    dl->AddText(ImVec2(tag_pos.x + 7.0f, tag_pos.y + (22.0f - tag_sz.y) * 0.5f),
                ui_obs::palette::U32(ui_obs::palette::Emerald), band_txt);
    ImGui::Dummy(ImVec2(tag_sz.x + 14.0f, 22.0f));

    // 2. Center Segmented Mode Switcher
    const AppSection active_sec = state_.get_section();
    const float btn_w_rec = 192.0f;
    const float btn_w_ana = 212.0f;
    const float total_tabs_w = btn_w_rec + btn_w_ana + 6.0f;
    const float tabs_x = std::max(270.0f, (width - total_tabs_w) * 0.5f);

    // Subtle pill container around the mode switcher
    ImVec2 pill_min(win_pos.x + tabs_x - 4.0f, win_pos.y + 6.0f);
    ImVec2 pill_max(win_pos.x + tabs_x + total_tabs_w + 4.0f, win_pos.y + height - 6.0f);
    dl->AddRectFilled(pill_min, pill_max, ui_obs::palette::U32(ui_obs::palette::BgDeep), 7.0f);
    dl->AddRect(pill_min, pill_max, ui_obs::palette::U32(ui_obs::palette::Border), 7.0f);

    auto draw_nav_tab = [&](const char* label, float x_off, float btn_w, bool selected, bool is_rec) -> bool {
        ImGui::SetCursorPos(ImVec2(x_off, 8.0f));
        ImVec4 bg = selected ? ui_obs::palette::AccentBlue : ImVec4(0, 0, 0, 0);
        ImVec4 hov = selected ? ImVec4(0.24f, 0.55f, 0.98f, 1.0f) : ui_obs::palette::SurfaceHover;
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui_obs::palette::AccentBlue);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? ui_obs::palette::TextPrimary : ui_obs::palette::TextMuted);
        ui_obs::PushFontSafe(fonts_.semibold);

        char hidden_id[64];
        std::snprintf(hidden_id, sizeof(hidden_id), "##Nav_%s", label);
        bool clicked = ImGui::Button(hidden_id, ImVec2(btn_w, height - 16.0f));
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();

        ImU32 fg = selected ? ui_obs::palette::U32(ui_obs::palette::TextPrimary)
                            : ui_obs::palette::U32(ui_obs::palette::TextMuted);
        ImVec2 icon_c(rmin.x + 18.0f, (rmin.y + rmax.y) * 0.5f);
        if (is_rec) {
            ui_obs::icons::draw_dish(dl, icon_c, 7.0f, fg);
        } else {
            ui_obs::icons::draw_spectrum_peak(dl, icon_c, 7.5f, fg);
        }
        ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(rmin.x + 32.0f, (rmin.y + rmax.y - ts.y) * 0.5f), fg, label);

        ImGui::PopFont();
        ImGui::PopStyleColor(5);
        return clicked;
    };

    if (draw_nav_tab("1. Live SDR & Record", tabs_x, btn_w_rec, active_sec == AppSection::Record, true)) {
        state_.switch_to_record();
    }
    if (draw_nav_tab("2. H I Scientific Analysis", tabs_x + btn_w_rec + 6.0f, btn_w_ana, active_sec == AppSection::Analyze, false)) {
        state_.set_section(AppSection::Analyze);
    }

    // 3. Right Global Status Indicator Pill
    const bool is_rec = state_.is_recording_active();
    const bool is_ana = analyze_panel_.is_analyzing();
    const bool is_live = sdr_.is_streaming();

    const char* pill_label = "IDLE";
    ImVec4 pill_col = ui_obs::palette::TextMuted;
    if (is_rec) {
        pill_label = "REC ACTIVE";
        pill_col = ui_obs::palette::Red;
    } else if (is_ana) {
        pill_label = "ANALYZING";
        pill_col = ui_obs::palette::Cyan;
    } else if (is_live) {
        pill_label = sdr_.is_simulated() ? "RX SIMULATED" : "RX HARDWARE";
        pill_col = sdr_.is_simulated() ? ui_obs::palette::Amber : ui_obs::palette::Emerald;
    }

    ImVec2 p_sz = ImGui::CalcTextSize(pill_label);
    const float badge_w = p_sz.x + 34.0f;
    if (width - badge_w - 14.0f > tabs_x + total_tabs_w + 16.0f) {
        ImVec2 b_pos(win_pos.x + width - badge_w - 14.0f, win_pos.y + 10.0f);
        ImVec2 b_max(b_pos.x + badge_w, win_pos.y + height - 10.0f);
        dl->AddRectFilled(b_pos, b_max, ui_obs::palette::U32(pill_col, 0.14f), 5.0f);
        dl->AddRect(b_pos, b_max, ui_obs::palette::U32(pill_col, 0.55f), 5.0f);
        ui_obs::icons::draw_record_dot(dl, ImVec2(b_pos.x + 13.0f, (b_pos.y + b_max.y) * 0.5f), 5.0f,
                                       ui_obs::palette::U32(pill_col), is_rec);
        dl->AddText(ImVec2(b_pos.x + 24.0f, (b_pos.y + b_max.y - p_sz.y) * 0.5f),
                    ui_obs::palette::U32(pill_col), pill_label);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AppGui::render_status_bar(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::HeaderBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 0.0f));
    ImGui::BeginChild("##ObsStatusBar", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    dl->AddLine(win_pos, ImVec2(win_pos.x + width, win_pos.y), ui_obs::palette::U32(ui_obs::palette::Border), 1.0f);

    const float text_y = (height - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPosY(text_y);
    ui_obs::PushFontSafe(fonts_.mono);

    const AppSection sec = state_.get_section();
    ImGui::TextColored(ui_obs::palette::TextMuted, sec == AppSection::Record ? "MODE: LIVE SDR" : "MODE: OFFLINE H I");
    ImGui::SameLine();
    ImGui::TextColored(ui_obs::palette::TextDim, "|");
    ImGui::SameLine();
    ImGui::TextColored(ui_obs::palette::TextMuted, "RAM: %zu MB", poll_process_memory_mb());
    ImGui::SameLine();
    ImGui::TextColored(ui_obs::palette::TextDim, "|");
    ImGui::SameLine();
    ImGui::TextColored(ui_obs::palette::Emerald, "SIMD: %s", cached_simd_name_.c_str());

    if (sec == AppSection::Record) {
        ImGui::SameLine();
        ImGui::TextColored(ui_obs::palette::TextDim, "|");
        ImGui::SameLine();
        ImGui::TextColored(ui_obs::palette::Cyan, "DSP: %.0f us (%.1f MSPS)",
                           dsp_.get_last_latency_us(), dsp_.get_dsp_throughput_msps());

        ImGui::SameLine();
        ImGui::TextColored(ui_obs::palette::TextDim, "|");
        ImGui::SameLine();
        float ring_pct = sdr_.get_ring_buffer_fill_pct();
        ImVec4 ring_col = (ring_pct > 70.0f) ? ui_obs::palette::Amber : ui_obs::palette::Emerald;
        ImGui::TextColored(ring_col, "RING: %.1f%%", ring_pct);

        ImGui::SameLine();
        ImGui::TextColored(ui_obs::palette::TextDim, "|");
        ImGui::SameLine();
        uint64_t drops = sdr_.get_dropped_samples_count();
        ImGui::TextColored(drops > 0 ? ui_obs::palette::Red : ui_obs::palette::TextMuted,
                           "DROPS: %llu", static_cast<unsigned long long>(drops));
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ui_obs::palette::TextDim, "|");
        ImGui::SameLine();
        ImGui::TextColored(ui_obs::palette::TextMuted, "Shortcuts: [F1] Live SDR  [F2] Analysis");
    }

    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AppGui::render_frame(ID3D11DeviceContext* context, float display_w, float display_h) {
    if (display_w < 100.0f || display_h < 100.0f) return;

    // Keyboard shortcuts when not editing a text box
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
            state_.switch_to_record();
        } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            state_.set_section(AppSection::Analyze);
        }
    }

    // Consume pending [Analyze This Capture] bridge trigger
    std::string target_path;
    if (state_.check_and_clear_auto_analyze(target_path)) {
        analyze_panel_.select_and_run_file(target_path, true);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(display_w, display_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::Begin("##ObsRootWindow", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    const float nav_h = 46.0f;
    const float status_h = 26.0f;
    const float body_h = std::max(80.0f, display_h - nav_h - status_h);

    render_top_navigation_bar(display_w, nav_h);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_obs::palette::BgDeep);
    ImGui::BeginChild("##ObsWorkspace", ImVec2(display_w, body_h), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (state_.get_section() == AppSection::Record) {
        record_panel_.render_panel(context, display_w, body_h);
    } else {
        analyze_panel_.render_panel(context, display_w, body_h);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    render_status_bar(display_w, status_h);

    ImGui::End();
}

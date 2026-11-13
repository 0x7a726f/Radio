#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include "imgui.h"
#include "constants.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ui_obs {

// ======================================================================
// Observatory Slate Palette & Font Registry
// ======================================================================
struct UiFonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
    ImFont* mono = nullptr;
};

inline void PushFontSafe(ImFont* font) {
    ImGui::PushFont(font ? font : ImGui::GetFont(), 0.0f);
}

namespace palette {
    constexpr ImVec4 BgDeep       = ImVec4(astro::theme::BG_R, astro::theme::BG_G, astro::theme::BG_B, 1.0f);         // #0D1117
    constexpr ImVec4 HeaderBg     = ImVec4(0.059f, 0.078f, 0.106f, 1.0f);                                              // #0F141B
    constexpr ImVec4 Surface      = ImVec4(astro::theme::FRAME_R, astro::theme::FRAME_G, astro::theme::FRAME_B, 1.0f); // #131820
    constexpr ImVec4 SurfaceAlt   = ImVec4(0.098f, 0.125f, 0.165f, 1.0f);                                              // #19202A
    constexpr ImVec4 SurfaceHover = ImVec4(0.133f, 0.173f, 0.227f, 1.0f);                                              // #222C3A
    constexpr ImVec4 Border       = ImVec4(astro::theme::BORDER_R, astro::theme::BORDER_G, astro::theme::BORDER_B, 1.0f); // #2D3644

    constexpr ImVec4 TextPrimary  = ImVec4(0.902f, 0.929f, 0.953f, 1.0f); // #E6EDF3
    constexpr ImVec4 TextMuted    = ImVec4(astro::theme::MUTED_R, astro::theme::MUTED_G, astro::theme::MUTED_B, 1.0f); // #8B949E
    constexpr ImVec4 TextDim      = ImVec4(0.365f, 0.412f, 0.478f, 1.0f); // #5D697A

    constexpr ImVec4 AccentBlue   = ImVec4(astro::theme::ACCENT_R, astro::theme::ACCENT_G, astro::theme::ACCENT_B, 1.0f); // #2F81F7
    constexpr ImVec4 Cyan         = ImVec4(astro::theme::CYAN_R, astro::theme::CYAN_G, astro::theme::CYAN_B, 1.0f);       // #38BDF8
    constexpr ImVec4 Emerald      = ImVec4(astro::theme::GREEN_R, astro::theme::GREEN_G, astro::theme::GREEN_B, 1.0f);    // #10B981
    constexpr ImVec4 Red          = ImVec4(astro::theme::RED_R, astro::theme::RED_G, astro::theme::RED_B, 1.0f);          // #EF4444
    constexpr ImVec4 Amber        = ImVec4(astro::theme::AMBER_R, astro::theme::AMBER_G, astro::theme::AMBER_B, 1.0f);    // #F59E0B
    constexpr ImVec4 Orange       = ImVec4(astro::theme::ORANGE_R, astro::theme::ORANGE_G, astro::theme::ORANGE_B, 1.0f); // #FB923C
    constexpr ImVec4 Pink         = ImVec4(astro::theme::PINK_R, astro::theme::PINK_G, astro::theme::PINK_B, 1.0f);       // #F472B6
    constexpr ImVec4 Purple       = ImVec4(0.655f, 0.545f, 0.980f, 1.0f);                                                 // #A78BFA

    inline constexpr ImU32 U32(const ImVec4& c, float alpha_override = -1.0f) {
        int r = static_cast<int>(c.x * 255.0f + 0.5f);
        int g = static_cast<int>(c.y * 255.0f + 0.5f);
        int b = static_cast<int>(c.z * 255.0f + 0.5f);
        int a = static_cast<int>((alpha_override >= 0.0f ? alpha_override : c.w) * 255.0f + 0.5f);
        return IM_COL32(r, g, b, a);
    }

    constexpr ImU32 PlotBg32       = IM_COL32(11, 15, 20, 255);
    constexpr ImU32 PlotGrid32     = IM_COL32(30, 39, 52, 255);
    constexpr ImU32 PlotBorder32   = IM_COL32(45, 54, 68, 255);
    constexpr ImU32 PlotCrosshair  = IM_COL32(139, 148, 158, 140);
} // namespace palette

inline void apply_observatory_slate_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding     = ImVec2(12.0f, 10.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.CellPadding       = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 11.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 6.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                 = palette::TextPrimary;
    colors[ImGuiCol_TextDisabled]         = palette::TextMuted;
    colors[ImGuiCol_WindowBg]             = palette::BgDeep;
    colors[ImGuiCol_ChildBg]              = palette::Surface;
    colors[ImGuiCol_PopupBg]              = palette::SurfaceAlt;
    colors[ImGuiCol_Border]               = palette::Border;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg]              = palette::BgDeep;
    colors[ImGuiCol_FrameBgHovered]       = palette::SurfaceAlt;
    colors[ImGuiCol_FrameBgActive]        = palette::SurfaceHover;

    colors[ImGuiCol_TitleBg]              = palette::HeaderBg;
    colors[ImGuiCol_TitleBgActive]        = palette::HeaderBg;
    colors[ImGuiCol_MenuBarBg]            = palette::HeaderBg;

    colors[ImGuiCol_ScrollbarBg]          = palette::BgDeep;
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.20f, 0.25f, 0.32f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.34f, 0.43f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]  = palette::AccentBlue;

    colors[ImGuiCol_CheckMark]            = palette::AccentBlue;
    colors[ImGuiCol_SliderGrab]           = palette::AccentBlue;
    colors[ImGuiCol_SliderGrabActive]     = palette::Cyan;

    colors[ImGuiCol_Button]               = palette::SurfaceAlt;
    colors[ImGuiCol_ButtonHovered]        = palette::SurfaceHover;
    colors[ImGuiCol_ButtonActive]         = palette::AccentBlue;

    colors[ImGuiCol_Header]               = palette::SurfaceAlt;
    colors[ImGuiCol_HeaderHovered]        = palette::SurfaceHover;
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.184f, 0.506f, 0.969f, 0.35f);

    colors[ImGuiCol_Separator]            = palette::Border;
    colors[ImGuiCol_SeparatorHovered]     = palette::AccentBlue;
    colors[ImGuiCol_SeparatorActive]      = palette::Cyan;

    colors[ImGuiCol_Tab]                  = palette::Surface;
    colors[ImGuiCol_TabHovered]           = palette::SurfaceHover;
    colors[ImGuiCol_TabSelected]          = palette::SurfaceAlt;
    colors[ImGuiCol_TabSelectedOverline]  = palette::AccentBlue;

    colors[ImGuiCol_PlotLines]            = palette::Cyan;
    colors[ImGuiCol_PlotHistogram]        = palette::AccentBlue;
    colors[ImGuiCol_TableHeaderBg]        = palette::SurfaceAlt;
    colors[ImGuiCol_TableBorderStrong]    = palette::Border;
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.176f, 0.212f, 0.267f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.02f, 0.03f, 0.05f, 0.72f);
}

// ======================================================================
// Vector Icon Primitives (zero Unicode/emoji glyph fallback artifacts)
// ======================================================================
namespace icons {

inline void draw_dish(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    // Parabolic dish arc + feed horn + mast
    dl->PathClear();
    for (int i = 0; i <= 10; ++i) {
        float a = 3.65f + (static_cast<float>(i) / 10.0f) * 1.9f;
        dl->PathLineTo(ImVec2(c.x + std::cos(a) * r * 0.95f, c.y + std::sin(a) * r * 0.95f));
    }
    dl->PathStroke(col, 1.8f, ImDrawFlags_None);
    dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.1f), ImVec2(c.x + r * 0.65f, c.y - r * 0.65f), col, 1.5f);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.65f, c.y - r * 0.65f), 2.2f, col);
    dl->AddLine(ImVec2(c.x - r * 0.15f, c.y + r * 0.25f), ImVec2(c.x - r * 0.5f, c.y + r * 0.85f), col, 1.5f);
    dl->AddLine(ImVec2(c.x - r * 0.15f, c.y + r * 0.25f), ImVec2(c.x + r * 0.25f, c.y + r * 0.85f), col, 1.5f);
}

inline void draw_spectrum_peak(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    ImVec2 pts[7] = {
        ImVec2(c.x - r,        c.y + r * 0.55f),
        ImVec2(c.x - r * 0.55f, c.y + r * 0.45f),
        ImVec2(c.x - r * 0.25f, c.y - r * 0.15f),
        ImVec2(c.x,            c.y - r * 0.80f),
        ImVec2(c.x + r * 0.25f, c.y - r * 0.10f),
        ImVec2(c.x + r * 0.58f, c.y + r * 0.45f),
        ImVec2(c.x + r,        c.y + r * 0.55f)
    };
    dl->AddPolyline(pts, 7, col, 1.8f, ImDrawFlags_None);
    dl->AddLine(ImVec2(c.x - r, c.y + r * 0.72f), ImVec2(c.x + r, c.y + r * 0.72f), palette::U32(palette::TextMuted), 1.0f);
}

inline void draw_record_dot(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool active) {
    if (active) {
        dl->AddCircle(c, r * 1.15f, (col & 0x00FFFFFFu) | 0x66000000u, 16, 1.5f);
    }
    dl->AddCircleFilled(c, r * 0.65f, col, 16);
}

inline void draw_stop_square(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddRectFilled(ImVec2(c.x - r * 0.58f, c.y - r * 0.58f), ImVec2(c.x + r * 0.58f, c.y + r * 0.58f), col, 1.5f);
}

inline void draw_play_triangle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddTriangleFilled(
        ImVec2(c.x - r * 0.45f, c.y - r * 0.65f),
        ImVec2(c.x + r * 0.68f, c.y),
        ImVec2(c.x - r * 0.45f, c.y + r * 0.65f),
        col
    );
}

inline void draw_bolt(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    ImVec2 pts[4] = {
        ImVec2(c.x + r * 0.15f, c.y - r * 0.85f),
        ImVec2(c.x - r * 0.45f, c.y + r * 0.05f),
        ImVec2(c.x + r * 0.10f, c.y + r * 0.05f),
        ImVec2(c.x - r * 0.25f, c.y + r * 0.85f)
    };
    dl->AddPolyline(pts, 4, col, 1.8f, ImDrawFlags_None);
}

} // namespace icons

// ======================================================================
// Win32 Path & Native File Dialog Helpers
// ======================================================================
namespace win_fs {

inline std::string wide_to_utf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], len, nullptr, nullptr);
    return out;
}

inline std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 1) return L"";
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &out[0], len);
    return out;
}

inline std::filesystem::path get_exe_dir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
}

inline std::string get_default_recordings_dir() {
    std::filesystem::path p = get_exe_dir() / "recordings";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

inline bool browse_for_cs16_file(HWND owner, const std::string& initial_dir, std::string& out_path) {
    wchar_t file_buf[1024] = {};
    std::wstring init_w = utf8_to_wide(initial_dir);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"IQ Recordings (*.cs16)\0*.cs16\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = sizeof(file_buf) / sizeof(wchar_t);
    ofn.lpstrInitialDir = init_w.empty() ? nullptr : init_w.c_str();
    ofn.lpstrTitle = L"Select .cs16 I/Q Recording";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        out_path = wide_to_utf8(file_buf);
        return !out_path.empty();
    }
    return false;
}

inline void open_folder_in_explorer(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::wstring wdir = utf8_to_wide(dir);
    ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace win_fs

// ======================================================================
// Shared Scientific 2D Plot Viewport, Axis Ticks & Min/Max Decimator
// ======================================================================
struct PlotMargin {
    float left   = 58.0f;
    float right  = 18.0f;
    float top    = 14.0f;
    float bottom = 30.0f;
};

struct PlotViewport2D {
    ImVec2 origin{0, 0};
    ImVec2 total_size{0, 0};
    ImVec2 plot_min{0, 0};
    ImVec2 plot_max{0, 0};
    float  plot_w = 1.0f;
    float  plot_h = 1.0f;
    double x_min = 0.0, x_max = 1.0;
    double y_min = 0.0, y_max = 1.0;
    double inv_x_span = 1.0;
    double inv_y_span = 1.0;

    static PlotViewport2D create(ImVec2 origin_px, ImVec2 size_px,
                                 double xmin, double xmax,
                                 double ymin, double ymax,
                                 const PlotMargin& m = PlotMargin{}) {
        PlotViewport2D vp;
        vp.origin = origin_px;
        vp.total_size = size_px;
        vp.plot_min = ImVec2(origin_px.x + m.left, origin_px.y + m.top);
        vp.plot_max = ImVec2(
            std::max(vp.plot_min.x + 24.0f, origin_px.x + size_px.x - m.right),
            std::max(vp.plot_min.y + 24.0f, origin_px.y + size_px.y - m.bottom)
        );
        vp.plot_w = vp.plot_max.x - vp.plot_min.x;
        vp.plot_h = vp.plot_max.y - vp.plot_min.y;
        vp.x_min = xmin;
        vp.x_max = (std::abs(xmax - xmin) < 1e-12) ? (xmin + 1.0) : xmax;
        vp.y_min = ymin;
        vp.y_max = (std::abs(ymax - ymin) < 1e-12) ? (ymin + 1.0) : ymax;
        vp.inv_x_span = 1.0 / (vp.x_max - vp.x_min);
        vp.inv_y_span = 1.0 / (vp.y_max - vp.y_min);
        return vp;
    }

    float map_x(double x) const {
        return plot_min.x + static_cast<float>((x - x_min) * inv_x_span) * plot_w;
    }

    float map_y(double y) const {
        return plot_max.y - static_cast<float>((y - y_min) * inv_y_span) * plot_h;
    }

    double unmap_x(float px) const {
        return x_min + (static_cast<double>(px - plot_min.x) / plot_w) * (x_max - x_min);
    }

    double unmap_y(float py) const {
        return y_max - (static_cast<double>(py - plot_min.y) / plot_h) * (y_max - y_min);
    }

    bool contains(ImVec2 p) const {
        return p.x >= plot_min.x && p.x <= plot_max.x && p.y >= plot_min.y && p.y <= plot_max.y;
    }

    void draw_background_and_grid(ImDrawList* dl,
                                  int x_ticks, int y_ticks,
                                  const char* x_fmt, const char* y_fmt,
                                  const char* x_label = nullptr,
                                  const char* y_label = nullptr,
                                  ImFont* mono_font = nullptr) const {
        dl->AddRectFilled(plot_min, plot_max, palette::PlotBg32, 3.0f);

        PushFontSafe(mono_font);
        const float font_h = ImGui::GetFontSize();
        const ImU32 text_col = palette::U32(palette::TextMuted);

        // Horizontal grid & Y labels
        if (y_ticks > 1) {
            for (int i = 0; i < y_ticks; ++i) {
                double frac = static_cast<double>(i) / (y_ticks - 1);
                double val = y_min + frac * (y_max - y_min);
                float py = plot_max.y - static_cast<float>(frac) * plot_h;
                dl->AddLine(ImVec2(plot_min.x, py), ImVec2(plot_max.x, py), palette::PlotGrid32, 1.0f);

                char buf[32];
                std::snprintf(buf, sizeof(buf), y_fmt, val);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(plot_min.x - ts.x - 6.0f, py - font_h * 0.5f), text_col, buf);
            }
        }

        // Vertical grid & X labels
        if (x_ticks > 1) {
            for (int i = 0; i < x_ticks; ++i) {
                double frac = static_cast<double>(i) / (x_ticks - 1);
                double val = x_min + frac * (x_max - x_min);
                float px = plot_min.x + static_cast<float>(frac) * plot_w;
                dl->AddLine(ImVec2(px, plot_min.y), ImVec2(px, plot_max.y), palette::PlotGrid32, 1.0f);

                char buf[32];
                std::snprintf(buf, sizeof(buf), x_fmt, val);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                float lx = std::clamp(px - ts.x * 0.5f, plot_min.x, plot_max.x - ts.x);
                dl->AddText(ImVec2(lx, plot_max.y + 4.0f), text_col, buf);
            }
        }
        ImGui::PopFont();

        if (x_label && *x_label) {
            ImVec2 ts = ImGui::CalcTextSize(x_label);
            dl->AddText(ImVec2(plot_min.x + (plot_w - ts.x) * 0.5f, plot_max.y + 16.0f), palette::U32(palette::TextDim), x_label);
        }
        if (y_label && *y_label) {
            dl->AddText(ImVec2(plot_min.x + 6.0f, plot_min.y + 4.0f), palette::U32(palette::TextMuted), y_label);
        }

        dl->AddRect(plot_min, plot_max, palette::PlotBorder32, 3.0f, 0, 1.0f);
    }

    void draw_top_axis(ImDrawList* dl, double top_min, double top_max,
                       int ticks, const char* fmt, const char* suffix = nullptr,
                       ImFont* mono_font = nullptr) const {
        if (ticks <= 1) return;
        PushFontSafe(mono_font);
        const ImU32 text_col = palette::U32(palette::Cyan, 0.85f);
        for (int i = 0; i < ticks; ++i) {
            double frac = static_cast<double>(i) / (ticks - 1);
            double val = top_min + frac * (top_max - top_min);
            float px = plot_min.x + static_cast<float>(frac) * plot_w;
            dl->AddLine(ImVec2(px, plot_min.y - 3.0f), ImVec2(px, plot_min.y), palette::U32(palette::Cyan, 0.55f), 1.0f);

            char buf[40];
            std::snprintf(buf, sizeof(buf), fmt, val);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            float lx = std::clamp(px - ts.x * 0.5f, plot_min.x, plot_max.x - ts.x);
            dl->AddText(ImVec2(lx, plot_min.y - ts.y - 4.0f), text_col, buf);
        }
        if (suffix && *suffix) {
            ImVec2 ts = ImGui::CalcTextSize(suffix);
            dl->AddText(ImVec2(plot_max.x - ts.x, plot_min.y - ts.y - 16.0f), palette::U32(palette::TextDim), suffix);
        }
        ImGui::PopFont();
    }

    template <typename YGetter>
    void draw_decimated_trace(ImDrawList* dl, size_t n_points,
                              double trace_x_min, double trace_x_max,
                              YGetter&& y_at,
                              ImU32 col, float thickness,
                              std::vector<ImVec2>& scratch) const {
        if (n_points < 2 || plot_w < 2.0f) return;

        scratch.clear();
        const int max_cols = std::max(2, static_cast<int>(std::ceil(plot_w)));
        const double span_x = (std::abs(trace_x_max - trace_x_min) < 1e-12) ? 1.0 : (trace_x_max - trace_x_min);

        dl->PushClipRect(plot_min, plot_max, true);

        if (n_points <= static_cast<size_t>(max_cols * 2)) {
            scratch.reserve(n_points);
            for (size_t i = 0; i < n_points; ++i) {
                double x = trace_x_min + (static_cast<double>(i) / static_cast<double>(n_points - 1)) * span_x;
                double y = static_cast<double>(y_at(i));
                if (std::isfinite(y)) {
                    scratch.emplace_back(map_x(x), map_y(y));
                }
            }
        } else {
            // Pixel-column min/max envelope preservation so narrow HI/RFI spikes are never hidden
            scratch.reserve(static_cast<size_t>(max_cols) * 2);
            const double inv_n = 1.0 / static_cast<double>(n_points - 1);
            int current_col = -1;
            float col_x_first = 0.0f;
            float col_y_min = 0.0f;
            float col_y_max = 0.0f;
            bool has_sample = false;

            auto flush_col = [&]() {
                if (!has_sample) return;
                if (std::abs(col_y_max - col_y_min) < 0.35f) {
                    scratch.emplace_back(col_x_first, col_y_min);
                } else {
                    scratch.emplace_back(col_x_first, col_y_min);
                    scratch.emplace_back(col_x_first, col_y_max);
                }
                has_sample = false;
            };

            for (size_t i = 0; i < n_points; ++i) {
                double y = static_cast<double>(y_at(i));
                if (!std::isfinite(y)) continue;
                double x = trace_x_min + (static_cast<double>(i) * inv_n) * span_x;
                float px = map_x(x);
                float py = map_y(y);
                int col_idx = static_cast<int>(std::floor(px - plot_min.x));
                if (col_idx != current_col) {
                    flush_col();
                    current_col = col_idx;
                    col_x_first = px;
                    col_y_min = py;
                    col_y_max = py;
                    has_sample = true;
                } else {
                    if (py < col_y_min) col_y_min = py;
                    if (py > col_y_max) col_y_max = py;
                }
            }
            flush_col();
        }

        if (scratch.size() >= 2) {
            dl->AddPolyline(scratch.data(), static_cast<int>(scratch.size()), col, thickness, ImDrawFlags_None);
        }
        dl->PopClipRect();
    }

    template <typename XGetter, typename YGetter>
    void draw_decimated_xy(ImDrawList* dl, size_t n_points,
                           XGetter&& x_at, YGetter&& y_at,
                           ImU32 col, float thickness,
                           std::vector<ImVec2>& scratch) const {
        if (n_points < 2 || plot_w < 2.0f) return;
        scratch.clear();
        const int max_cols = std::max(2, static_cast<int>(std::ceil(plot_w)));
        dl->PushClipRect(plot_min, plot_max, true);

        if (n_points <= static_cast<size_t>(max_cols * 2)) {
            scratch.reserve(n_points);
            for (size_t i = 0; i < n_points; ++i) {
                double x = static_cast<double>(x_at(i));
                double y = static_cast<double>(y_at(i));
                if (std::isfinite(x) && std::isfinite(y)) {
                    scratch.emplace_back(map_x(x), map_y(y));
                }
            }
        } else {
            scratch.reserve(static_cast<size_t>(max_cols) * 2);
            int current_col = -1;
            float col_x = 0.0f, col_ymin = 0.0f, col_ymax = 0.0f;
            bool has_sample = false;
            auto flush_col = [&]() {
                if (!has_sample) return;
                scratch.emplace_back(col_x, col_ymin);
                if (std::abs(col_ymax - col_ymin) >= 0.35f) {
                    scratch.emplace_back(col_x, col_ymax);
                }
                has_sample = false;
            };
            for (size_t i = 0; i < n_points; ++i) {
                double x = static_cast<double>(x_at(i));
                double y = static_cast<double>(y_at(i));
                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                float px = map_x(x);
                float py = map_y(y);
                int c = static_cast<int>(std::floor(px - plot_min.x));
                if (c != current_col) {
                    flush_col();
                    current_col = c;
                    col_x = px;
                    col_ymin = py;
                    col_ymax = py;
                    has_sample = true;
                } else {
                    if (py < col_ymin) col_ymin = py;
                    if (py > col_ymax) col_ymax = py;
                }
            }
            flush_col();
        }

        if (scratch.size() >= 2) {
            dl->AddPolyline(scratch.data(), static_cast<int>(scratch.size()), col, thickness, ImDrawFlags_None);
        }
        dl->PopClipRect();
    }
};

// Shared 256-entry RGBA colormap builders (for Waterfall DX11 upload & Colorbar)
inline std::vector<uint32_t> build_colormap_lut256(int mode) {
    // 0: Viridis, 1: Inferno, 2: Plasma, 3: Turbo/Jet-like
    std::vector<uint32_t> lut(256, 0xFF000000u);
    auto lerp_c = [](uint8_t a, uint8_t b, float f) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(a + (b - a) * f, 0.0f, 255.0f));
    };

    struct Stop { uint8_t r, g, b; };
    static constexpr Stop viridis[5] = {{68,1,84}, {59,82,139}, {33,145,140}, {94,201,98}, {253,231,37}};
    static constexpr Stop inferno[5] = {{0,0,4}, {87,16,110}, {188,55,84}, {249,142,9}, {252,255,164}};
    static constexpr Stop plasma[5]  = {{13,8,135}, {126,3,168}, {204,71,120}, {248,149,64}, {240,249,33}};
    static constexpr Stop turbo[5]   = {{48,18,59}, {40,188,235}, {164,252,60}, {251,126,33}, {122,4,3}};

    const Stop* stops = viridis;
    if (mode == 1) stops = inferno;
    else if (mode == 2) stops = plasma;
    else if (mode == 3) stops = turbo;

    for (int i = 0; i < 256; ++i) {
        float t = static_cast<float>(i) / 255.0f;
        float pos = t * 4.0f;
        int idx = std::clamp(static_cast<int>(pos), 0, 3);
        float frac = pos - static_cast<float>(idx);
        uint8_t r = lerp_c(stops[idx].r, stops[idx + 1].r, frac);
        uint8_t g = lerp_c(stops[idx].g, stops[idx + 1].g, frac);
        uint8_t b = lerp_c(stops[idx].b, stops[idx + 1].b, frac);
        lut[static_cast<size_t>(i)] = IM_COL32(r, g, b, 255);
    }
    return lut;
}

inline void draw_vertical_colorbar(ImDrawList* dl, ImVec2 bar_min, ImVec2 bar_max,
                                   double vmin, double vmax, const char* fmt,
                                   const std::vector<uint32_t>& lut256,
                                   ImFont* mono_font = nullptr) {
    if (lut256.size() < 256 || bar_max.y <= bar_min.y + 10.0f) return;
    constexpr int kSteps = 64;
    const float h = bar_max.y - bar_min.y;
    for (int i = 0; i < kSteps; ++i) {
        float y0 = bar_min.y + (static_cast<float>(i) / kSteps) * h;
        float y1 = bar_min.y + (static_cast<float>(i + 1) / kSteps) * h;
        int lut_idx = std::clamp(255 - static_cast<int>((static_cast<float>(i) + 0.5f) / kSteps * 255.0f), 0, 255);
        dl->AddRectFilled(ImVec2(bar_min.x, y0), ImVec2(bar_max.x, y1 + 0.5f), lut256[static_cast<size_t>(lut_idx)]);
    }
    dl->AddRect(bar_min, bar_max, palette::PlotBorder32);

    PushFontSafe(mono_font);
    const ImU32 text_col = palette::U32(palette::TextMuted);
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, vmax);
    dl->AddText(ImVec2(bar_max.x + 5.0f, bar_min.y - 2.0f), text_col, buf);
    std::snprintf(buf, sizeof(buf), fmt, 0.5 * (vmin + vmax));
    dl->AddText(ImVec2(bar_max.x + 5.0f, bar_min.y + h * 0.5f - ImGui::GetFontSize() * 0.5f), text_col, buf);
    std::snprintf(buf, sizeof(buf), fmt, vmin);
    dl->AddText(ImVec2(bar_max.x + 5.0f, bar_max.y - ImGui::GetFontSize() + 2.0f), text_col, buf);
    ImGui::PopFont();
}

} // namespace ui_obs

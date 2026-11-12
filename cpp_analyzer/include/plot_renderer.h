#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "config.h"
#include "astrophysics.h"
#include "streaming_processor.h"

namespace astro {

enum class ColormapType {
    Magma,
    Viridis,
    Turbo,
    Classic
};

struct PixelColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    constexpr PixelColor() = default;
    constexpr PixelColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}
};

class SoftwareCanvas {
public:
    SoftwareCanvas(int width, int height, PixelColor bg_color = PixelColor(13, 17, 23, 255));

    int width() const { return width_; }
    int height() const { return height_; }
    const uint8_t* data() const { return pixels_.data(); }

    void clear(PixelColor color);
    void set_pixel(int x, int y, PixelColor color);
    void blend_pixel(int x, int y, PixelColor color);

    void draw_line(int x0, int y0, int x1, int y1, PixelColor color, int thickness = 1);
    void draw_rect(int x, int y, int w, int h, PixelColor color, int thickness = 1);
    void fill_rect(int x, int y, int w, int h, PixelColor color);
    void fill_rect_alpha(int x, int y, int w, int h, PixelColor color);
    void draw_circle(int cx, int cy, int radius, PixelColor color, bool fill = false);

    // Text rendering using embedded crisp 8x12 vector/bitmap font
    void draw_text(int x, int y, const std::string& text, PixelColor color, int scale = 1);
    int measure_text_width(const std::string& text, int scale = 1) const;

    // Render time-frequency waterfall spectrogram slice
    void render_waterfall(
        const float* matrix,
        int rows,
        int cols,
        int dest_x,
        int dest_y,
        int dest_w,
        int dest_h,
        float vmin,
        float vmax,
        ColormapType cmap = ColormapType::Magma
    );

    // Save canvas to PNG using stb_image_write
    bool save_png(const std::string& filepath) const;

private:
    int width_;
    int height_;
    std::vector<uint8_t> pixels_; // RGBA 4 bytes per pixel
};

// Colormap lookup helper: maps normalized value [0.0, 1.0] to RGBA
PixelColor sample_colormap(ColormapType cmap, float t);

class PlotRenderer {
public:
    static bool render_calibrated_spectrum(
        const std::vector<double>& freqs_mhz,
        const std::vector<double>& vel_kms,
        const std::vector<double>& calibrated_k,
        const std::vector<double>& calibrated_db,
        const AstrophysicalMetrics& metrics,
        const AstroConfig& config,
        const std::string& output_path
    );

    static bool render_raw_and_baseline(
        const std::vector<double>& freqs_mhz,
        const std::vector<double>& raw_power,
        const std::vector<double>& baseline_power,
        const AstrophysicalMetrics& metrics,
        const AstroConfig& config,
        const std::string& output_path
    );

    static bool render_doppler_profile(
        const std::vector<double>& vel_kms,
        const std::vector<double>& calibrated_k,
        const AstrophysicalMetrics& metrics,
        const AstroConfig& config,
        const std::string& output_path
    );

    static bool render_waterfall_plot(
        const std::vector<float>& waterfall_data,
        int rows,
        int cols,
        const std::vector<double>& waterfall_times,
        const std::vector<double>& freqs_mhz,
        const std::vector<double>& baseline_power,
        const AstroConfig& config,
        const std::string& output_path
    );

    static bool render_master_dashboard(
        const std::vector<double>& freqs_mhz,
        const std::vector<double>& vel_kms,
        const std::vector<double>& raw_power,
        const std::vector<double>& baseline_power,
        const std::vector<double>& calibrated_k,
        const std::vector<double>& calibrated_db,
        const std::vector<float>& waterfall_data,
        int wf_rows,
        int wf_cols,
        const std::vector<double>& waterfall_times,
        const AstrophysicalMetrics& metrics,
        const AstroConfig& config,
        const std::string& filepath,
        const std::string& output_path
    );
};

} // namespace astro

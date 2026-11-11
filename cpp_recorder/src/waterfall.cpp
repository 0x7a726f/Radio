#include "waterfall.h"
#include "simd_dsp.h"
#include <algorithm>
#include <cstring>
#include <cmath>

struct ColorAnchor {
    float pos;
    uint8_t r, g, b;
};

static inline uint32_t make_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    // Little-endian layout: byte0=R, byte1=G, byte2=B, byte3=A
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

Waterfall::Waterfall(int width, int height)
    : width_(width), height_(height) {
    pixels_.assign(width_ * height_, make_rgba(10, 15, 30));
    rebuild_palette_lut();
}

void Waterfall::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        pixels_.assign(width_ * height_, make_rgba(10, 15, 30));
    }
}

void Waterfall::clear() {
    std::fill(pixels_.begin(), pixels_.end(), make_rgba(10, 15, 30));
}

void Waterfall::set_palette(WaterfallPalette palette) {
    current_palette_ = palette;
    rebuild_palette_lut();
}

const std::vector<std::string>& Waterfall::get_palette_names() {
    static const std::vector<std::string> names = {
        "Classic SatDump",
        "Viridis",
        "Turbo",
        "Inferno",
        "Magma"
    };
    return names;
}

static void interpolate_palette(const std::vector<ColorAnchor>& anchors, uint32_t* lut) {
    for (int i = 0; i < 256; ++i) {
        float t = (float)i / 255.0f;
        if (t <= anchors.front().pos) {
            lut[i] = make_rgba(anchors.front().r, anchors.front().g, anchors.front().b);
            continue;
        }
        if (t >= anchors.back().pos) {
            lut[i] = make_rgba(anchors.back().r, anchors.back().g, anchors.back().b);
            continue;
        }
        for (size_t a = 0; a < anchors.size() - 1; ++a) {
            if (t >= anchors[a].pos && t <= anchors[a + 1].pos) {
                float seg = (t - anchors[a].pos) / (anchors[a + 1].pos - anchors[a].pos);
                uint8_t r = (uint8_t)(anchors[a].r + seg * (anchors[a + 1].r - anchors[a].r));
                uint8_t g = (uint8_t)(anchors[a].g + seg * (anchors[a + 1].g - anchors[a].g));
                uint8_t b = (uint8_t)(anchors[a].b + seg * (anchors[a + 1].b - anchors[a].b));
                lut[i] = make_rgba(r, g, b);
                break;
            }
        }
    }
}

void Waterfall::rebuild_palette_lut() {
    switch (current_palette_) {
        case WaterfallPalette::ClassicYoussef: {
            static const std::vector<ColorAnchor> anchors = {
                {0.00f,  10,  15,  45},
                {0.20f,   0,  50, 160},
                {0.38f,   0, 160, 210},
                {0.55f,  30, 200,  70},
                {0.72f, 250, 220,   0},
                {0.88f, 240,  50,   0},
                {1.00f, 255, 255, 255}
            };
            interpolate_palette(anchors, palette_lut_);
            break;
        }
        case WaterfallPalette::Viridis: {
            static const std::vector<ColorAnchor> anchors = {
                {0.00f,  68,   1,  84},
                {0.25f,  59,  82, 139},
                {0.50f,  33, 145, 140},
                {0.75f,  94, 201,  98},
                {1.00f, 253, 231,  37}
            };
            interpolate_palette(anchors, palette_lut_);
            break;
        }
        case WaterfallPalette::Turbo: {
            static const std::vector<ColorAnchor> anchors = {
                {0.00f,  48,  18,  59},
                {0.20f,  70, 134, 251},
                {0.40f,  27, 229, 181},
                {0.60f, 164, 252,  60},
                {0.80f, 251, 126,  33},
                {1.00f, 122,   4,   3}
            };
            interpolate_palette(anchors, palette_lut_);
            break;
        }
        case WaterfallPalette::Inferno: {
            static const std::vector<ColorAnchor> anchors = {
                {0.00f,   0,   0,   4},
                {0.25f,  87,  16, 110},
                {0.50f, 187,  55,  84},
                {0.75f, 249, 142,   9},
                {1.00f, 252, 255, 164}
            };
            interpolate_palette(anchors, palette_lut_);
            break;
        }
        case WaterfallPalette::Magma: {
            static const std::vector<ColorAnchor> anchors = {
                {0.00f,   0,   0,   4},
                {0.25f,  81,  18, 124},
                {0.50f, 182,  54, 121},
                {0.75f, 251, 136,  97},
                {1.00f, 252, 253, 191}
            };
            interpolate_palette(anchors, palette_lut_);
            break;
        }
    }
}

void Waterfall::add_line(const std::vector<float>& power_db, float min_db, float max_db) {
    if (width_ <= 0 || height_ <= 0 || power_db.empty()) return;

    // Roll rows down in memory (row 0 becomes row 1, etc.)
    if (height_ > 1) {
        std::memmove(&pixels_[width_], &pixels_[0], (size_t)(height_ - 1) * width_ * sizeof(uint32_t));
    }

    // High-performance vectorized resample and LUT palette mapping
    SimdDsp::map_waterfall_line_fast(power_db.data(), power_db.size(), min_db, max_db, palette_lut_, pixels_.data(), width_);
}

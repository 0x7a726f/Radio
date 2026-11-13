#include "plot_renderer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace astro {

// Simple 8x12 readable bitmap font for ASCII 32..126
static const uint8_t FONT_8X12[95][12] = {
    // Space 32
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! 33
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    // " 34
    {0x00,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // # 35
    {0x00,0x66,0x66,0xFF,0x66,0x66,0xFF,0x66,0x66,0x00,0x00,0x00},
    // $ 36
    {0x18,0x7E,0x18,0x3C,0x66,0x3C,0x18,0x66,0x3C,0x18,0x7E,0x18},
    // % 37
    {0x00,0xC6,0xCC,0x18,0x30,0x60,0xC6,0x00,0x00,0x00,0x00,0x00},
    // & 38
    {0x00,0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // ' 39
    {0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ( 40
    {0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00},
    // ) 41
    {0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00},
    // * 42
    {0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},
    // + 43
    {0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    // , 44
    {0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00},
    // - 45
    {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // . 46
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
    // / 47
    {0x00,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
    // 0 48
    {0x00,0x3C,0x66,0xC3,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00},
    // 1 49
    {0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
    // 2 50
    {0x00,0x7C,0xC6,0x0C,0x18,0x30,0x60,0xFF,0x00,0x00,0x00,0x00},
    // 3 51
    {0x00,0x7E,0x0C,0x18,0x3C,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 4 52
    {0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x00,0x00,0x00,0x00},
    // 5 53
    {0x00,0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 6 54
    {0x00,0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 7 55
    {0x00,0xFE,0x06,0x0C,0x18,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
    // 8 56
    {0x00,0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // 9 57
    {0x00,0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
    // : 58
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    // ; 59
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00},
    // < 60
    {0x00,0x06,0x18,0x60,0xC0,0x60,0x18,0x06,0x00,0x00,0x00,0x00},
    // = 61
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // > 62
    {0x00,0x60,0x18,0x06,0x03,0x06,0x18,0x60,0x00,0x00,0x00,0x00},
    // ? 63
    {0x00,0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00,0x00,0x00,0x00},
    // @ 64
    {0x00,0x7C,0xC6,0xDE,0xD6,0xD6,0xC0,0x7C,0x00,0x00,0x00,0x00},
    // A 65
    {0x00,0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // B 66
    {0x00,0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
    // C 67
    {0x00,0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00,0x00,0x00,0x00},
    // D 68
    {0x00,0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
    // E 69
    {0x00,0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00,0x00,0x00,0x00},
    // F 70
    {0x00,0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00,0x00,0x00,0x00},
    // G 71
    {0x00,0x3C,0x66,0xC0,0xCE,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
    // H 72
    {0x00,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    // I 73
    {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
    // J 74
    {0x00,0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
    // K 75
    {0x00,0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
    // L 76
    {0x00,0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    // M 77
    {0x00,0xC3,0xE7,0xFF,0xDB,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00},
    // N 78
    {0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0x00,0x00,0x00,0x00},
    // O 79
    {0x00,0x3C,0x66,0xC3,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00},
    // P 80
    {0x00,0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    // Q 81
    {0x00,0x3C,0x66,0xC3,0xC3,0xDB,0x6E,0x3C,0x06,0x00,0x00,0x00},
    // R 82
    {0x00,0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
    // S 83
    {0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // T 84
    {0x00,0xFF,0xDB,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // U 85
    {0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    // V 86
    {0x00,0xC3,0xC3,0xC3,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},
    // W 87
    {0x00,0xC3,0xC3,0xC3,0xDB,0xFF,0x66,0x66,0x00,0x00,0x00,0x00},
    // X 88
    {0x00,0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00,0x00,0x00,0x00},
    // Y 89
    {0x00,0xC3,0x66,0x3C,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // Z 90
    {0x00,0xFE,0xC6,0x0C,0x18,0x30,0x63,0xFE,0x00,0x00,0x00,0x00},
    // [ 91
    {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
    // \ 92
    {0x00,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00},
    // ] 93
    {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
    // ^ 94
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // _ 95
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00},
    // ` 96
    {0x00,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // a 97
    {0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00,0x00,0x00,0x00},
    // b 98
    {0x00,0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00,0x00,0x00,0x00},
    // c 99
    {0x00,0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00,0x00,0x00},
    // d 100
    {0x00,0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // e 101
    {0x00,0x00,0x00,0x3C,0x66,0xFE,0x60,0x3C,0x00,0x00,0x00,0x00},
    // f 102
    {0x00,0x1C,0x36,0x30,0x7C,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
    // g 103
    {0x00,0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78,0x00,0x00,0x00},
    // h 104
    {0x00,0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    // i 105
    {0x00,0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // j 106
    {0x00,0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0xCC,0x78,0x00,0x00,0x00},
    // k 107
    {0x00,0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00,0x00,0x00,0x00},
    // l 108
    {0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    // m 109
    {0x00,0x00,0x00,0xFE,0xDB,0xDB,0xDB,0xDB,0x00,0x00,0x00,0x00},
    // n 110
    {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
    // o 111
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},
    // p 112
    {0x00,0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0,0x00,0x00,0x00},
    // q 113
    {0x00,0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E,0x00,0x00,0x00},
    // r 114
    {0x00,0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    // s 115
    {0x00,0x00,0x00,0x7C,0xC0,0x78,0x06,0x7C,0x00,0x00,0x00,0x00},
    // t 116
    {0x00,0x10,0x30,0x7C,0x30,0x30,0x34,0x18,0x00,0x00,0x00,0x00},
    // u 117
    {0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    // v 118
    {0x00,0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},
    // w 119
    {0x00,0x00,0x00,0xC3,0xC3,0xDB,0xDB,0x66,0x00,0x00,0x00,0x00},
    // x 120
    {0x00,0x00,0x00,0xC3,0x66,0x3C,0x66,0xC3,0x00,0x00,0x00,0x00},
    // y 121
    {0x00,0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0x7C,0x00,0x00,0x00},
    // z 122
    {0x00,0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00,0x00,0x00,0x00},
    // { 123
    {0x00,0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
    // | 124
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
    // } 125
    {0x00,0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
    // ~ 126
    {0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

PixelColor sample_colormap(ColormapType cmap, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (cmap == ColormapType::Viridis) {
        // Viridis approximation
        float r = std::clamp(0.28f + 2.0f * (t - 0.4f) * (t - 0.4f), 0.0f, 1.0f);
        if (t < 0.4f) r = 0.28f * (1.0f - t / 0.4f);
        else r = std::min(1.0f, 0.95f * (t - 0.4f) / 0.6f + 0.1f);
        float g = std::clamp(t < 0.5f ? (t / 0.5f * 0.7f) : (0.7f + (t - 0.5f) * 0.6f), 0.0f, 1.0f);
        float b = std::clamp(t < 0.3f ? (0.3f + t / 0.3f * 0.4f) : (0.7f * (1.0f - (t - 0.3f) / 0.7f)), 0.0f, 1.0f);
        return PixelColor(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255));
    } else if (cmap == ColormapType::Turbo) {
        // Turbo approximation
        float r = std::clamp(std::sin(t * 3.14159f - 0.5f) * 0.5f + 0.5f, 0.0f, 1.0f);
        if (t > 0.6f) r = std::clamp((t - 0.6f) / 0.35f, 0.0f, 1.0f);
        float g = std::clamp(std::sin(t * 3.14159f) * 1.2f - 0.1f, 0.0f, 1.0f);
        float b = std::clamp(std::cos(t * 1.57f) * 1.2f - 0.2f, 0.0f, 1.0f);
        return PixelColor(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255));
    } else if (cmap == ColormapType::Classic) {
        // Jet / Classic
        float r = std::clamp(1.5f - std::abs(4.0f * t - 3.0f), 0.0f, 1.0f);
        float g = std::clamp(1.5f - std::abs(4.0f * t - 2.0f), 0.0f, 1.0f);
        float b = std::clamp(1.5f - std::abs(4.0f * t - 1.0f), 0.0f, 1.0f);
        return PixelColor(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255));
    } else {
        // Magma
        float r = std::clamp(t < 0.33f ? (t / 0.33f * 0.6f) : (0.6f + (t - 0.33f) / 0.67f * 0.4f), 0.0f, 1.0f);
        float g = std::clamp(t < 0.5f ? 0.05f : ((t - 0.5f) / 0.5f * 0.95f), 0.0f, 1.0f);
        float b = std::clamp(t < 0.25f ? (0.2f + t * 2.0f) : (0.7f * (1.0f - (t - 0.25f) / 0.75f)), 0.0f, 1.0f);
        return PixelColor(static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255));
    }
}

SoftwareCanvas::SoftwareCanvas(int width, int height, PixelColor bg_color)
    : width_(width), height_(height), pixels_(width * height * 4) {
    clear(bg_color);
}

void SoftwareCanvas::clear(PixelColor color) {
    uint32_t packed = (static_cast<uint32_t>(color.a) << 24)
                    | (static_cast<uint32_t>(color.b) << 16)
                    | (static_cast<uint32_t>(color.g) << 8)
                    | static_cast<uint32_t>(color.r);
    uint32_t* dst = reinterpret_cast<uint32_t*>(pixels_.data());
    std::fill_n(dst, static_cast<size_t>(width_) * height_, packed);
}

void SoftwareCanvas::set_pixel(int x, int y, PixelColor color) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    int idx = (y * width_ + x) * 4;
    pixels_[idx + 0] = color.r;
    pixels_[idx + 1] = color.g;
    pixels_[idx + 2] = color.b;
    pixels_[idx + 3] = color.a;
}

void SoftwareCanvas::blend_pixel(int x, int y, PixelColor color) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    int idx = (y * width_ + x) * 4;
    const uint32_t a = color.a;
    const uint32_t inv_a = 255u - a;
    pixels_[idx + 0] = static_cast<uint8_t>((color.r * a + pixels_[idx + 0] * inv_a + 127u) / 255u);
    pixels_[idx + 1] = static_cast<uint8_t>((color.g * a + pixels_[idx + 1] * inv_a + 127u) / 255u);
    pixels_[idx + 2] = static_cast<uint8_t>((color.b * a + pixels_[idx + 2] * inv_a + 127u) / 255u);
}

static float fpart(float x) { return x - std::floor(x); }
static float rfpart(float x) { return 1.0f - fpart(x); }

void SoftwareCanvas::draw_line(int x0_i, int y0_i, int x1_i, int y1_i, PixelColor color, int thickness) {
    // Pure horizontal or vertical lines: draw crisp spans respecting alpha
    if (x0_i == x1_i || y0_i == y1_i) {
        int x_min = std::min(x0_i, x1_i);
        int x_max = std::max(x0_i, x1_i);
        int y_min = std::min(y0_i, y1_i);
        int y_max = std::max(y0_i, y1_i);
        int rad = (thickness - 1) / 2;
        const bool opaque = (color.a == 255);

        if (x0_i == x1_i) {
            y_min = std::max(0, y_min);
            y_max = std::min(height_ - 1, y_max);
            for (int y = y_min; y <= y_max; ++y) {
                for (int rx = -rad; rx <= rad; ++rx) {
                    if (opaque) set_pixel(x0_i + rx, y, color);
                    else blend_pixel(x0_i + rx, y, color);
                }
            }
        } else {
            x_min = std::max(0, x_min);
            x_max = std::min(width_ - 1, x_max);
            for (int ry = -rad; ry <= rad; ++ry) {
                int py = y0_i + ry;
                if (py < 0 || py >= height_) continue;
                for (int x = x_min; x <= x_max; ++x) {
                    if (opaque) set_pixel(x, py, color);
                    else blend_pixel(x, py, color);
                }
            }
        }
        return;
    }

    // High-quality Xiaolin Wu anti-aliased line algorithm with sub-pixel coverage
    auto draw_single_aa = [this](float x0, float y0, float x1, float y1, PixelColor col) {
        bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
        if (steep) {
            std::swap(x0, y0);
            std::swap(x1, y1);
        }
        if (x0 > x1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }

        float dx = x1 - x0;
        float dy = y1 - y0;
        float gradient = (std::abs(dx) < 1e-6f) ? 1.0f : (dy / dx);

        auto plot = [this, steep, &col](int x, int y, float brightness) {
            if (brightness <= 0.001f) return;
            uint8_t a = static_cast<uint8_t>(std::clamp(brightness * col.a, 0.0f, 255.0f));
            if (steep) {
                blend_pixel(y, x, PixelColor(col.r, col.g, col.b, a));
            } else {
                blend_pixel(x, y, PixelColor(col.r, col.g, col.b, a));
            }
        };

        float xend = std::round(x0);
        float yend = y0 + gradient * (xend - x0);
        float xgap = rfpart(x0 + 0.5f);
        int xpxl1 = static_cast<int>(xend);
        int ypxl1 = static_cast<int>(std::floor(yend));

        plot(xpxl1, ypxl1, rfpart(yend) * xgap);
        plot(xpxl1, ypxl1 + 1, fpart(yend) * xgap);
        float intery = yend + gradient;

        xend = std::round(x1);
        yend = y1 + gradient * (xend - x1);
        xgap = fpart(x1 + 0.5f);
        int xpxl2 = static_cast<int>(xend);
        int ypxl2 = static_cast<int>(std::floor(yend));

        plot(xpxl2, ypxl2, rfpart(yend) * xgap);
        plot(xpxl2, ypxl2 + 1, fpart(yend) * xgap);

        for (int x = xpxl1 + 1; x < xpxl2; ++x) {
            plot(x, static_cast<int>(std::floor(intery)), rfpart(intery));
            plot(x, static_cast<int>(std::floor(intery)) + 1, fpart(intery));
            intery += gradient;
        }
    };

    float x0 = static_cast<float>(x0_i);
    float y0 = static_cast<float>(y0_i);
    float x1 = static_cast<float>(x1_i);
    float y1 = static_cast<float>(y1_i);

    if (thickness <= 1) {
        draw_single_aa(x0, y0, x1, y1, color);
    } else {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) {
            set_pixel(x0_i, y0_i, color);
            return;
        }
        float nx = -dy / len;
        float ny = dx / len;

        int rad = (thickness - 1);
        for (int step = -rad; step <= rad; ++step) {
            float offset = step * 0.5f;
            draw_single_aa(x0 + nx * offset, y0 + ny * offset, x1 + nx * offset, y1 + ny * offset, color);
        }
    }
}

void SoftwareCanvas::draw_rect(int x, int y, int w, int h, PixelColor color, int thickness) {
    draw_line(x, y, x + w, y, color, thickness);
    draw_line(x + w, y, x + w, y + h, color, thickness);
    draw_line(x + w, y + h, x, y + h, color, thickness);
    draw_line(x, y + h, x, y, color, thickness);
}

void SoftwareCanvas::fill_rect(int x, int y, int w, int h, PixelColor color) {
    int x_start = std::max(0, x);
    int x_end = std::min(width_, x + w);
    int y_start = std::max(0, y);
    int y_end = std::min(height_, y + h);

    for (int cy = y_start; cy < y_end; ++cy) {
        for (int cx = x_start; cx < x_end; ++cx) {
            set_pixel(cx, cy, color);
        }
    }
}

void SoftwareCanvas::fill_rect_alpha(int x, int y, int w, int h, PixelColor color) {
    int x_start = std::max(0, x);
    int x_end = std::min(width_, x + w);
    int y_start = std::max(0, y);
    int y_end = std::min(height_, y + h);

    for (int cy = y_start; cy < y_end; ++cy) {
        for (int cx = x_start; cx < x_end; ++cx) {
            blend_pixel(cx, cy, color);
        }
    }
}

void SoftwareCanvas::draw_circle(int cx, int cy, int radius, PixelColor color, bool fill) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            int dist_sq = x * x + y * y;
            if (fill) {
                if (dist_sq <= radius * radius) {
                    set_pixel(cx + x, cy + y, color);
                }
            } else {
                if (dist_sq <= radius * radius && dist_sq >= (radius - 1) * (radius - 1)) {
                    set_pixel(cx + x, cy + y, color);
                }
            }
        }
    }
}

void SoftwareCanvas::draw_text(int x, int y, const std::string& text, PixelColor color, int scale) {
    int cur_x = x;
    int cur_y = y;

    for (char ch : text) {
        if (ch == '\n') {
            cur_x = x;
            cur_y += 14 * scale;
            continue;
        }
        if (ch < 32 || ch > 126) ch = '?';
        int char_idx = ch - 32;

        for (int row = 0; row < 12; ++row) {
            uint8_t row_bits = FONT_8X12[char_idx][row];
            for (int col = 0; col < 8; ++col) {
                if ((row_bits >> (7 - col)) & 1) {
                    if (scale == 1) {
                        set_pixel(cur_x + col, cur_y + row, color);
                    } else {
                        fill_rect(cur_x + col * scale, cur_y + row * scale, scale, scale, color);
                    }
                }
            }
        }
        cur_x += 8 * scale;
    }
}

int SoftwareCanvas::measure_text_width(const std::string& text, int scale) const {
    size_t max_len = 0;
    size_t cur_len = 0;
    for (char ch : text) {
        if (ch == '\n') {
            max_len = std::max(max_len, cur_len);
            cur_len = 0;
        } else {
            cur_len++;
        }
    }
    max_len = std::max(max_len, cur_len);
    return static_cast<int>(max_len) * 8 * scale;
}

void SoftwareCanvas::render_waterfall(
    const float* matrix,
    int rows,
    int cols,
    int dest_x,
    int dest_y,
    int dest_w,
    int dest_h,
    float vmin,
    float vmax,
    ColormapType cmap
) {
    if (rows <= 0 || cols <= 0 || dest_w <= 0 || dest_h <= 0) return;
    float range = vmax - vmin;
    if (range <= 1e-6f) range = 1.0f;
    const float inv_range_255 = 255.0f / range;

    // Precompute 256-entry packed RGBA LUT for cmap
    uint32_t lut[256];
    for (int i = 0; i < 256; ++i) {
        PixelColor c = sample_colormap(cmap, static_cast<float>(i) / 255.0f);
        lut[i] = (static_cast<uint32_t>(c.a) << 24)
               | (static_cast<uint32_t>(c.b) << 16)
               | (static_cast<uint32_t>(c.g) << 8)
               | static_cast<uint32_t>(c.r);
    }

    // Precompute column source indices once outside py loop
    std::vector<int> src_cols(dest_w);
    const float col_scale = static_cast<float>(cols) / static_cast<float>(dest_w);
    for (int px = 0; px < dest_w; ++px) {
        src_cols[px] = std::clamp(static_cast<int>(px * col_scale), 0, cols - 1);
    }

    const int px_start = std::max(0, -dest_x);
    const int px_end = std::min(dest_w, width_ - dest_x);
    const int py_start = std::max(0, -dest_y);
    const int py_end = std::min(dest_h, height_ - dest_y);
    if (px_start >= px_end || py_start >= py_end) return;

    uint32_t* canvas_px = reinterpret_cast<uint32_t*>(pixels_.data());
    const float row_scale = static_cast<float>(rows) / static_cast<float>(dest_h);

    for (int py = py_start; py < py_end; ++py) {
        int src_r = std::clamp(static_cast<int>((dest_h - 1 - py) * row_scale), 0, rows - 1);
        const float* row_ptr = &matrix[static_cast<size_t>(src_r) * cols];
        uint32_t* dst_row = &canvas_px[static_cast<size_t>(dest_y + py) * width_ + dest_x];

        for (int px = px_start; px < px_end; ++px) {
            float norm = (row_ptr[src_cols[px]] - vmin) * inv_range_255;
            int idx = static_cast<int>(norm);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;
            dst_row[px] = lut[idx];
        }
    }
}

static void compute_waterfall_percentiles(std::vector<float>& vals, float& vmin, float& vmax) {
    if (vals.empty()) {
        vmin = -1.0f;
        vmax = 1.0f;
        return;
    }
    const size_t n = vals.size();
    const size_t i_lo = (n * 5) / 100;
    const size_t i_hi = std::min(n - 1, (n * 995) / 1000);
    std::nth_element(vals.begin(), vals.begin() + i_lo, vals.end());
    vmin = vals[i_lo];
    std::nth_element(vals.begin() + i_lo + 1, vals.begin() + i_hi, vals.end());
    vmax = vals[i_hi] + 0.3f;
}

bool SoftwareCanvas::save_png(const std::string& filepath) const {
    std::filesystem::path p(filepath);
    if (!p.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    int res = stbi_write_png(filepath.c_str(), width_, height_, 4, pixels_.data(), width_ * 4);
    return (res != 0);
}

// ------------------------------------------------------------------------------------------------
// PlotRenderer Publication Figures
// ------------------------------------------------------------------------------------------------

bool PlotRenderer::render_calibrated_spectrum(
    const std::vector<double>& freqs_mhz,
    const std::vector<double>& vel_kms,
    const std::vector<double>& calibrated_k,
    const std::vector<double>& calibrated_db,
    const AstrophysicalMetrics& metrics,
    const AstroConfig& config,
    const std::string& output_path
) {
    if (freqs_mhz.empty() || vel_kms.empty() || calibrated_k.empty() || calibrated_db.empty()) return false;
    const int W = 1200;
    const int H = 640;
    SoftwareCanvas canvas(W, H, PixelColor(13, 17, 23)); // #0d1117

    // Plot area margins
    const int left = 90;
    const int right = W - 50;
    const int top = 80;
    const int bottom = H - 70;
    const int plot_w = right - left;
    const int plot_h = bottom - top;

    // Background of plot box
    canvas.fill_rect(left, top, plot_w, plot_h, PixelColor(22, 27, 34)); // #161b22
    canvas.draw_rect(left, top, plot_w, plot_h, PixelColor(48, 54, 61), 1);

    // Zoom limits: v in [-200, 200]
    const double v_min = -200.0;
    const double v_max = 200.0;

    double k_max = 0.5;
    double k_min = -0.5;
    for (size_t i = 0; i < vel_kms.size(); ++i) {
        if (vel_kms[i] >= v_min && vel_kms[i] <= v_max) {
            k_max = std::max(k_max, calibrated_k[i]);
            k_min = std::min(k_min, calibrated_k[i]);
        }
    }
    k_min = std::max(-1.0, k_min - 0.5);
    k_max = k_max * 1.35 + 1.0;

    auto map_x = [&](double v) {
        return left + static_cast<int>((v - v_min) / (v_max - v_min) * plot_w);
    };
    auto map_y = [&](double k) {
        return bottom - static_cast<int>((k - k_min) / (k_max - k_min) * plot_h);
    };

    // Grid lines
    for (double v = -180; v <= 180; v += 60) {
        int gx = map_x(v);
        canvas.draw_line(gx, top, gx, bottom, PixelColor(48, 54, 61), 1);
        std::stringstream ss;
        ss << std::showpos << static_cast<int>(v);
        canvas.draw_text(gx - 14, bottom + 12, ss.str(), PixelColor(139, 148, 158), 1);

        // Top axis sky frequency (MHz)
        double f_mhz = HYDROGEN_REST_FREQ_MHZ * (1.0 - v / SPEED_OF_LIGHT_KMS);
        std::stringstream ss_f;
        ss_f << std::fixed << std::setprecision(3) << f_mhz;
        canvas.draw_text(gx - 20, top - 24, ss_f.str(), PixelColor(164, 176, 190), 1);
    }

    // Zero Kelvin line
    int y0 = map_y(0.0);
    if (y0 >= top && y0 <= bottom) {
        canvas.draw_line(left, y0, right, y0, PixelColor(139, 148, 158), 1);
    }

    // Rest frame v = 0 line
    int x0 = map_x(0.0);
    if (x0 >= left && x0 <= right) {
        canvas.draw_line(x0, top, x0, bottom, PixelColor(116, 185, 255), 1);
    }

    // Shaded fill under positive emission curve
    for (size_t i = 1; i < vel_kms.size(); ++i) {
        double v_a = vel_kms[i - 1];
        double v_b = vel_kms[i];
        if (v_b < v_min || v_a > v_max) continue;
        int xa = map_x(v_a);
        int xb = map_x(v_b);
        int ya = map_y(calibrated_k[i - 1]);
        int yb = map_y(calibrated_k[i]);

        int x_step_start = std::max(left, std::min(xa, xb));
        int x_step_end = std::min(right, std::max(xa, xb));
        for (int sx = x_step_start; sx <= x_step_end; ++sx) {
            float frac = (xa != xb) ? static_cast<float>(sx - xa) / (xb - xa) : 0.0f;
            int cur_y = static_cast<int>(ya + frac * (yb - ya));
            if (cur_y < y0) {
                canvas.draw_line(sx, cur_y, sx, y0, PixelColor(0, 242, 254, 45), 1);
            }
        }
    }

    // Calibrated spectrum line (Cyan #00f2fe)
    for (size_t i = 1; i < vel_kms.size(); ++i) {
        double v_a = vel_kms[i - 1];
        double v_b = vel_kms[i];
        if (v_b < v_min || v_a > v_max) continue;
        int xa = map_x(v_a);
        int xb = map_x(v_b);
        int ya = map_y(calibrated_k[i - 1]);
        int yb = map_y(calibrated_k[i]);
        canvas.draw_line(xa, ya, xb, yb, PixelColor(0, 242, 254), 2);
    }

    // Gaussian fit overlay (Salmon Pink #ff7675)
    if (metrics.gauss_fit.success && !metrics.gauss_fit.fitted_curve.empty()) {
        for (size_t i = 1; i < vel_kms.size(); ++i) {
            double v_a = vel_kms[i - 1];
            double v_b = vel_kms[i];
            if (v_b < v_min || v_a > v_max) continue;
            int xa = map_x(v_a);
            int xb = map_x(v_b);
            int ya = map_y(metrics.gauss_fit.fitted_curve[i - 1]);
            int yb = map_y(metrics.gauss_fit.fitted_curve[i]);
            canvas.draw_line(xa, ya, xb, yb, PixelColor(255, 118, 117), 2);
        }
    }

    // Peak marker circle
    int px = map_x(metrics.peak_vel_kms);
    int py = map_y(metrics.peak_temp_k);
    canvas.draw_circle(px, py, 6, PixelColor(255, 71, 87), true);
    canvas.draw_circle(px, py, 7, PixelColor(255, 255, 255), false);

    // Title & Axis Labels
    canvas.draw_text(left, 25, "Calibrated Hydrogen Line (HI) Spectrum - Galactic 21cm Analysis", PixelColor(255, 255, 255), 2);
    canvas.draw_text(left + plot_w / 2 - 140, bottom + 42, "Doppler Radial Velocity [Radio Def] (km/s)", PixelColor(201, 209, 217), 1);
    canvas.draw_text(left + plot_w / 2 - 100, 52, "Topocentric Sky Frequency (MHz)", PixelColor(164, 176, 190), 1);
    canvas.draw_text(15, top + plot_h / 2, "TA (K)", PixelColor(0, 242, 254), 1);

    // HUD Metrics Card Box
    int hud_x = left + 18;
    int hud_y = top + 18;
    int hud_w = 340;
    int hud_h = 160;
    canvas.fill_rect_alpha(hud_x, hud_y, hud_w, hud_h, PixelColor(30, 39, 46, 230));
    canvas.draw_rect(hud_x, hud_y, hud_w, hud_h, PixelColor(0, 210, 211), 1);

    std::stringstream hud;
    hud << "Target Line: Neutral Hydrogen (HI 21cm)\n"
        << "Rest Freq:   " << std::fixed << std::setprecision(4) << HYDROGEN_REST_FREQ_MHZ << " MHz\n"
        << "Obs Peak:    " << std::fixed << std::setprecision(4) << metrics.peak_freq_mhz << " MHz\n"
        << "Recession:   " << std::showpos << std::fixed << std::setprecision(2) << metrics.peak_vel_kms << " km/s\n"
        << "Brightness:  " << std::noshowpos << std::fixed << std::setprecision(2) << metrics.peak_temp_k << " K ("
        << std::showpos << std::fixed << std::setprecision(3) << metrics.peak_power_db << " dB)\n"
        << "Noise Floor: " << std::noshowpos << std::fixed << std::setprecision(3) << metrics.noise_std_k << " K\n"
        << "SNR:         " << std::fixed << std::setprecision(1) << metrics.snr << " sigma\n"
        << "FWHM Width:  " << std::fixed << std::setprecision(1) << metrics.fwhm_kms << " km/s\n"
        << "Flux W:      " << std::fixed << std::setprecision(1) << metrics.integrated_flux_k_kms << " K*km/s\n"
        << "N_HI Column: " << std::scientific << std::setprecision(2) << metrics.column_density_cm2 << " cm^-2";

    canvas.draw_text(hud_x + 10, hud_y + 10, hud.str(), PixelColor(241, 242, 246), 1);

    return canvas.save_png(output_path);
}

bool PlotRenderer::render_raw_and_baseline(
    const std::vector<double>& freqs_mhz,
    const std::vector<double>& raw_power,
    const std::vector<double>& baseline_power,
    const AstrophysicalMetrics& metrics,
    const AstroConfig& config,
    const std::string& output_path
) {
    if (freqs_mhz.empty() || raw_power.empty() || baseline_power.empty()) return false;
    const int W = 1200;
    const int H = 800;
    SoftwareCanvas canvas(W, H, PixelColor(13, 17, 23));

    int left = 90, right = W - 50;
    int top1 = 70, bottom1 = 440;
    int top2 = 500, bottom2 = H - 60;
    int plot_w = right - left;

    canvas.fill_rect(left, top1, plot_w, bottom1 - top1, PixelColor(22, 27, 34));
    canvas.draw_rect(left, top1, plot_w, bottom1 - top1, PixelColor(48, 54, 61), 1);

    canvas.fill_rect(left, top2, plot_w, bottom2 - top2, PixelColor(22, 27, 34));
    canvas.draw_rect(left, top2, plot_w, bottom2 - top2, PixelColor(48, 54, 61), 1);

    size_t n = freqs_mhz.size();
    if (n < 2) return false;

    double f_min = freqs_mhz.front();
    double f_max = freqs_mhz.back();

    std::vector<double> raw_db(n), base_db(n), flat_db(n);
    double y1_min = 1e9, y1_max = -1e9;
    for (size_t i = 0; i < n; ++i) {
        raw_db[i] = 10.0 * std::log10(std::max(raw_power[i], 1e-12));
        base_db[i] = 10.0 * std::log10(std::max(baseline_power[i], 1e-12));
        flat_db[i] = raw_db[i] - base_db[i];
        y1_min = std::min(y1_min, raw_db[i]);
        y1_max = std::max(y1_max, raw_db[i]);
    }
    y1_min -= 1.0;
    y1_max += 1.0;

    auto map_x = [&](double f) {
        return left + static_cast<int>((f - f_min) / (f_max - f_min) * plot_w);
    };
    auto map_y1 = [&](double p) {
        return bottom1 - static_cast<int>((p - y1_min) / (y1_max - y1_min) * (bottom1 - top1));
    };

    double y2_min = -0.4, y2_max = 0.6;
    auto map_y2 = [&](double r) {
        return bottom2 - static_cast<int>((r - y2_min) / (y2_max - y2_min) * (bottom2 - top2));
    };

    // Shaded outer filter skirts (outer 6.5%)
    int skirt_bins = static_cast<int>(n * config.passband_edge_fraction);
    int x_pb_start = map_x(freqs_mhz[skirt_bins]);
    int x_pb_end = map_x(freqs_mhz[n - 1 - skirt_bins]);

    canvas.fill_rect_alpha(left, top1, x_pb_start - left, bottom1 - top1, PixelColor(87, 96, 111, 80));
    canvas.fill_rect_alpha(x_pb_end, top1, right - x_pb_end, bottom1 - top1, PixelColor(87, 96, 111, 80));

    canvas.fill_rect_alpha(left, top2, x_pb_start - left, bottom2 - top2, PixelColor(87, 96, 111, 80));
    canvas.fill_rect_alpha(x_pb_end, top2, right - x_pb_end, bottom2 - top2, PixelColor(87, 96, 111, 80));

    // Protected HI window shaded
    double hi_half_mhz = config.hi_protection_half_width_khz / 1e3;
    int x_hi_l = map_x(HYDROGEN_REST_FREQ_MHZ - hi_half_mhz);
    int x_hi_r = map_x(HYDROGEN_REST_FREQ_MHZ + hi_half_mhz);
    canvas.fill_rect_alpha(x_hi_l, top1, x_hi_r - x_hi_l, bottom1 - top1, PixelColor(16, 172, 132, 45));
    canvas.fill_rect_alpha(x_hi_l, top2, x_hi_r - x_hi_l, bottom2 - top2, PixelColor(16, 172, 132, 45));

    // DC notch vertical line (LO tuned center frequency)
    double fc_mhz = config.center_freq_hz / 1e6;
    int x_dc = map_x(fc_mhz);
    canvas.draw_line(x_dc, top1, x_dc, bottom1, PixelColor(255, 71, 87), 2);
    canvas.draw_line(x_dc, top2, x_dc, bottom2, PixelColor(255, 71, 87), 1);

    // HI line vertical marker
    int x_hi = map_x(HYDROGEN_REST_FREQ_MHZ);
    canvas.draw_line(x_hi, top1, x_hi, bottom1, PixelColor(0, 210, 211), 1);
    canvas.draw_line(x_hi, top2, x_hi, bottom2, PixelColor(0, 210, 211), 1);

    // Top panel: Raw Spectrum (Sky blue) & Baseline Model (Orange)
    for (size_t i = 1; i < n; ++i) {
        int xa = map_x(freqs_mhz[i - 1]);
        int xb = map_x(freqs_mhz[i]);
        canvas.draw_line(xa, map_y1(raw_db[i - 1]), xb, map_y1(raw_db[i]), PixelColor(84, 160, 255), 1);
    }
    for (size_t i = 1; i < n; ++i) {
        int xa = map_x(freqs_mhz[i - 1]);
        int xb = map_x(freqs_mhz[i]);
        canvas.draw_line(xa, map_y1(base_db[i - 1]), xb, map_y1(base_db[i]), PixelColor(255, 159, 67), 2);
    }

    // Bottom panel: Flattened Residuals (Emerald green)
    int y2_zero = map_y2(0.0);
    canvas.draw_line(left, y2_zero, right, y2_zero, PixelColor(139, 148, 158), 1);
    for (size_t i = 1; i < n; ++i) {
        int xa = map_x(freqs_mhz[i - 1]);
        int xb = map_x(freqs_mhz[i]);
        canvas.draw_line(xa, map_y2(flat_db[i - 1]), xb, map_y2(flat_db[i]), PixelColor(29, 209, 161), 1);
    }

    // Titles & Text
    canvas.draw_text(left, 25, "Full SDR Bandpass & Instrumental Baseline Model", PixelColor(255, 255, 255), 2);
    canvas.draw_text(left + 15, top1 + 15, "Raw Averaged Spectrum (Blue) vs Baseline Model (Orange)", PixelColor(200, 200, 200), 1);
    canvas.draw_text(left + 15, top2 + 15, "Flattened Residual Spectrum (P_raw - P_base dB)", PixelColor(29, 209, 161), 1);
    canvas.draw_text(left + plot_w / 2 - 40, bottom2 + 25, "Frequency (MHz)", PixelColor(201, 209, 217), 1);

    return canvas.save_png(output_path);
}

bool PlotRenderer::render_doppler_profile(
    const std::vector<double>& vel_kms,
    const std::vector<double>& calibrated_k,
    const AstrophysicalMetrics& metrics,
    const AstroConfig& config,
    const std::string& output_path
) {
    if (vel_kms.empty() || calibrated_k.empty()) return false;
    const int W = 1000;
    const int H = 550;
    SoftwareCanvas canvas(W, H, PixelColor(13, 17, 23));

    int left = 90, right = W - 50;
    int top = 70, bottom = H - 70;
    int plot_w = right - left;
    int plot_h = bottom - top;

    canvas.fill_rect(left, top, plot_w, plot_h, PixelColor(22, 27, 34));
    canvas.draw_rect(left, top, plot_w, plot_h, PixelColor(48, 54, 61), 1);

    const double v_min = -250.0;
    const double v_max = 250.0;
    double k_max = 0.5, k_min = -0.5;
    for (size_t i = 0; i < vel_kms.size(); ++i) {
        if (vel_kms[i] >= v_min && vel_kms[i] <= v_max) {
            k_max = std::max(k_max, calibrated_k[i]);
            k_min = std::min(k_min, calibrated_k[i]);
        }
    }
    k_min = std::max(-1.0, k_min - 0.5);
    k_max = k_max * 1.35 + 1.0;

    auto map_x = [&](double v) {
        return left + static_cast<int>((v - v_min) / (v_max - v_min) * plot_w);
    };
    auto map_y = [&](double k) {
        return bottom - static_cast<int>((k - k_min) / (k_max - k_min) * plot_h);
    };

    // Zero velocity & rest frame
    int x0 = map_x(0.0);
    canvas.draw_line(x0, top, x0, bottom, PixelColor(254, 202, 87), 1);
    int y0 = map_y(0.0);
    canvas.draw_line(left, y0, right, y0, PixelColor(139, 148, 158), 1);

    // Spectrum curve (Electric blue #48dbfb)
    for (size_t i = 1; i < vel_kms.size(); ++i) {
        double v_a = vel_kms[i - 1], v_b = vel_kms[i];
        if (v_b < v_min || v_a > v_max) continue;
        int xa = map_x(v_a), xb = map_x(v_b);
        int ya = map_y(calibrated_k[i - 1]), yb = map_y(calibrated_k[i]);
        canvas.draw_line(xa, ya, xb, yb, PixelColor(72, 219, 251), 2);
    }

    // Gaussian fit
    if (metrics.gauss_fit.success && !metrics.gauss_fit.fitted_curve.empty()) {
        for (size_t i = 1; i < vel_kms.size(); ++i) {
            double v_a = vel_kms[i - 1], v_b = vel_kms[i];
            if (v_b < v_min || v_a > v_max) continue;
            int xa = map_x(v_a), xb = map_x(v_b);
            int ya = map_y(metrics.gauss_fit.fitted_curve[i - 1]);
            int yb = map_y(metrics.gauss_fit.fitted_curve[i]);
            canvas.draw_line(xa, ya, xb, yb, PixelColor(255, 107, 107), 2);
        }
    }

    // Peak marker
    int px = map_x(metrics.peak_vel_kms);
    int py = map_y(metrics.peak_temp_k);
    canvas.draw_circle(px, py, 6, PixelColor(255, 71, 87), true);

    canvas.draw_text(left, 25, "Galactic Neutral Hydrogen Radial Velocity Profile", PixelColor(255, 255, 255), 2);
    canvas.draw_text(left + plot_w / 2 - 120, bottom + 35, "Topocentric Radial Velocity v_LSR (km/s)", PixelColor(201, 209, 217), 1);
    canvas.draw_text(15, top + plot_h / 2, "TA (K)", PixelColor(72, 219, 251), 1);

    return canvas.save_png(output_path);
}

bool PlotRenderer::render_waterfall_plot(
    const std::vector<float>& waterfall_data,
    int rows,
    int cols,
    const std::vector<double>& waterfall_times,
    const std::vector<double>& freqs_mhz,
    const std::vector<double>& baseline_power,
    const AstroConfig& config,
    const std::string& output_path
) {
    if (waterfall_data.empty() || rows <= 0 || cols <= 0 || freqs_mhz.empty() || baseline_power.empty()) return false;
    const int W = 1200;
    const int H = 600;
    SoftwareCanvas canvas(W, H, PixelColor(13, 17, 23));

    int left = 90, right = W - 80;
    int top = 70, bottom = H - 60;
    int plot_w = right - left;
    int plot_h = bottom - top;

    // Zoom frequency span: +/- 600 kHz around HI line
    double f_center = HYDROGEN_REST_FREQ_MHZ;
    double f_span = 0.6; // MHz
    double f_min = f_center - f_span;
    double f_max = f_center + f_span;

    int c_start = 0, c_end = cols - 1;
    for (int c = 0; c < cols; ++c) {
        if (freqs_mhz[c] >= f_min) { c_start = c; break; }
    }
    for (int c = cols - 1; c >= 0; --c) {
        if (freqs_mhz[c] <= f_max) { c_end = c; break; }
    }
    int sub_cols = std::max(1, c_end - c_start + 1);

    // Compute relative dB: 10 * log10(waterfall / baseline)
    std::vector<float> sub_matrix(static_cast<size_t>(rows) * sub_cols);
    std::vector<float> inv_base(sub_cols);
    for (int sc = 0; sc < sub_cols; ++sc) {
        inv_base[sc] = 1.0f / static_cast<float>(std::max(1e-12, baseline_power[c_start + sc]));
    }
    for (int r = 0; r < rows; ++r) {
        const float* wf_row = &waterfall_data[static_cast<size_t>(r) * cols + c_start];
        float* dst_row = &sub_matrix[static_cast<size_t>(r) * sub_cols];
        for (int sc = 0; sc < sub_cols; ++sc) {
            dst_row[sc] = 10.0f * std::log10(std::max(1e-6f, wf_row[sc] * inv_base[sc]));
        }
    }

    // O(N) percentile scaling via nth_element
    if (sub_matrix.empty()) return false;
    std::vector<float> scratch_vals = sub_matrix;
    float vmin = 0.0f, vmax = 1.0f;
    compute_waterfall_percentiles(scratch_vals, vmin, vmax);

    canvas.render_waterfall(sub_matrix.data(), rows, sub_cols, left, top, plot_w, plot_h, vmin, vmax, ColormapType::Magma);
    canvas.draw_rect(left, top, plot_w, plot_h, PixelColor(48, 54, 61), 1);

    // HI rest frequency vertical marker
    double f_sub_min = freqs_mhz[c_start];
    double f_sub_max = freqs_mhz[c_end];
    int x_hi = left + static_cast<int>((HYDROGEN_REST_FREQ_MHZ - f_sub_min) / (f_sub_max - f_sub_min) * plot_w);
    if (x_hi >= left && x_hi <= right) {
        canvas.draw_line(x_hi, top, x_hi, bottom, PixelColor(0, 242, 254), 1);
    }

    // Colorbar on right
    int cb_x = right + 18;
    int cb_w = 20;
    for (int y = 0; y < plot_h; ++y) {
        float t = 1.0f - static_cast<float>(y) / plot_h;
        PixelColor col = sample_colormap(ColormapType::Magma, t);
        canvas.draw_line(cb_x, top + y, cb_x + cb_w, top + y, col, 1);
    }
    canvas.draw_rect(cb_x, top, cb_w, plot_h, PixelColor(48, 54, 61), 1);

    std::stringstream ss_max, ss_min;
    ss_max << "+" << std::fixed << std::setprecision(1) << vmax;
    ss_min << std::fixed << std::setprecision(1) << vmin;
    canvas.draw_text(cb_x + cb_w + 6, top, ss_max.str(), PixelColor(200, 200, 200), 1);
    canvas.draw_text(cb_x + cb_w + 6, bottom - 10, ss_min.str(), PixelColor(200, 200, 200), 1);

    canvas.draw_text(left, 25, "Time-Frequency Waterfall (Calibrated Spectrogram)", PixelColor(255, 255, 255), 2);
    canvas.draw_text(left + plot_w / 2 - 40, bottom + 35, "Frequency (MHz)", PixelColor(201, 209, 217), 1);
    canvas.draw_text(15, top + plot_h / 2, "Time (s)", PixelColor(201, 209, 217), 1);

    return canvas.save_png(output_path);
}

bool PlotRenderer::render_master_dashboard(
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
) {
    if (freqs_mhz.empty() || vel_kms.empty() || raw_power.empty() || baseline_power.empty() ||
        calibrated_k.empty() || waterfall_data.empty() || wf_rows <= 0 || wf_cols <= 0) return false;
    const int W = 1600;
    const int H = 1000;
    SoftwareCanvas canvas(W, H, PixelColor(13, 17, 23));

    // Header banner
    std::filesystem::path fp(filepath);
    double dur_s = waterfall_times.empty() ? 0.0 : waterfall_times.back();
    std::stringstream header;
    header << "RADIOTELESCOPE HYDROGEN LINE ANALYSIS DASHBOARD\n"
           << "File: " << fp.filename().string() << "  |  Duration: " << std::fixed << std::setprecision(1) << dur_s
           << "s (" << std::fixed << std::setprecision(2) << dur_s / 60.0 << " min)  |  LO: "
           << std::fixed << std::setprecision(4) << config.center_freq_hz / 1e6 << " MHz  |  Rate: "
           << std::fixed << std::setprecision(2) << config.sample_rate_hz / 1e6 << " MSPS  |  SNR: "
           << std::fixed << std::setprecision(1) << metrics.snr << " sigma";

    canvas.draw_text(60, 25, header.str(), PixelColor(255, 255, 255), 2);

    // 4 Quadrants:
    // Q1 (top-left): (A) Full Bandpass & Baseline Model
    // Q2 (top-right): (B) Calibrated Frequency Spectrum (HI Region)
    // Q3 (bottom-left): (C) Doppler Velocity & Line Fit
    // Q4 (bottom-right): (D) Time-Frequency Waterfall Spectrogram
    int pad = 50;
    int pane_w = (W - 3 * pad) / 2;
    int pane_h = (H - 120 - 2 * pad) / 2;

    int q1_x = pad, q1_y = 100;
    int q2_x = pad + pane_w + pad, q2_y = 100;
    int q3_x = pad, q3_y = 100 + pane_h + pad;
    int q4_x = pad + pane_w + pad, q4_y = 100 + pane_h + pad;

    // Draw pane boxes
    auto draw_pane = [&](int x, int y, const std::string& title) {
        canvas.fill_rect(x, y, pane_w, pane_h, PixelColor(22, 27, 34));
        canvas.draw_rect(x, y, pane_w, pane_h, PixelColor(48, 54, 61), 1);
        canvas.draw_text(x + 15, y + 15, title, PixelColor(255, 255, 255), 1);
    };

    draw_pane(q1_x, q1_y, "(A) Full Bandpass & Instrumental Baseline");
    draw_pane(q2_x, q2_y, "(B) Calibrated Frequency Spectrum (HI Region)");
    draw_pane(q3_x, q3_y, "(C) Doppler Velocity & Gaussian Line Profile");
    draw_pane(q4_x, q4_y, "(D) Time-Frequency Waterfall Spectrogram");

    // Render Q1: Raw vs Baseline
    size_t n = freqs_mhz.size();
    if (n > 1) {
        double f_min = freqs_mhz.front(), f_max = freqs_mhz.back();
        double p_min = 1e9, p_max = -1e9;
        std::vector<double> r_db(n), b_db(n);
        for (size_t i = 0; i < n; ++i) {
            r_db[i] = 10.0 * std::log10(std::max(raw_power[i], 1e-12));
            b_db[i] = 10.0 * std::log10(std::max(baseline_power[i], 1e-12));
            p_min = std::min(p_min, r_db[i]);
            p_max = std::max(p_max, r_db[i]);
        }
        p_min -= 1.0; p_max += 1.0;

        auto q1_mx = [&](double f) { return q1_x + 60 + static_cast<int>((f - f_min) / (f_max - f_min) * (pane_w - 80)); };
        auto q1_my = [&](double p) { return q1_y + pane_h - 40 - static_cast<int>((p - p_min) / (p_max - p_min) * (pane_h - 80)); };

        for (size_t i = 1; i < n; ++i) {
            canvas.draw_line(q1_mx(freqs_mhz[i - 1]), q1_my(r_db[i - 1]), q1_mx(freqs_mhz[i]), q1_my(r_db[i]), PixelColor(84, 160, 255), 1);
            canvas.draw_line(q1_mx(freqs_mhz[i - 1]), q1_my(b_db[i - 1]), q1_mx(freqs_mhz[i]), q1_my(b_db[i]), PixelColor(255, 159, 67), 2);
        }
    }

    // Render Q2: Calibrated dB around HI
    double f_hi_min = HYDROGEN_REST_FREQ_MHZ - 0.6;
    double f_hi_max = HYDROGEN_REST_FREQ_MHZ + 0.6;
    auto q2_mx = [&](double f) { return q2_x + 60 + static_cast<int>((f - f_hi_min) / (f_hi_max - f_hi_min) * (pane_w - 80)); };
    auto q2_my = [&](double db) { return q2_y + pane_h - 40 - static_cast<int>((db - (-0.2)) / (0.8 - (-0.2)) * (pane_h - 80)); };

    int q2_y0 = q2_my(0.0);
    canvas.draw_line(q2_x + 60, q2_y0, q2_x + pane_w - 20, q2_y0, PixelColor(139, 148, 158), 1);
    for (size_t i = 1; i < n; ++i) {
        if (freqs_mhz[i] < f_hi_min || freqs_mhz[i - 1] > f_hi_max) continue;
        canvas.draw_line(q2_mx(freqs_mhz[i - 1]), q2_my(calibrated_db[i - 1]), q2_mx(freqs_mhz[i]), q2_my(calibrated_db[i]), PixelColor(29, 209, 161), 1);
    }

    // Render Q3: Doppler Velocity Profile with Gaussian Fit
    double v_min = -180.0, v_max = 180.0;
    auto q3_mx = [&](double v) { return q3_x + 60 + static_cast<int>((v - v_min) / (v_max - v_min) * (pane_w - 80)); };
    auto q3_my = [&](double k) { return q3_y + pane_h - 40 - static_cast<int>((k - (-0.5)) / (metrics.peak_temp_k * 1.5 + 1.0 - (-0.5)) * (pane_h - 80)); };

    int q3_y0 = q3_my(0.0);
    canvas.draw_line(q3_x + 60, q3_y0, q3_x + pane_w - 20, q3_y0, PixelColor(139, 148, 158), 1);
    for (size_t i = 1; i < vel_kms.size(); ++i) {
        if (vel_kms[i] < v_min || vel_kms[i - 1] > v_max) continue;
        canvas.draw_line(q3_mx(vel_kms[i - 1]), q3_my(calibrated_k[i - 1]), q3_mx(vel_kms[i]), q3_my(calibrated_k[i]), PixelColor(0, 242, 254), 2);
    }
    if (metrics.gauss_fit.success && !metrics.gauss_fit.fitted_curve.empty()) {
        for (size_t i = 1; i < vel_kms.size(); ++i) {
            if (vel_kms[i] < v_min || vel_kms[i - 1] > v_max) continue;
            canvas.draw_line(q3_mx(vel_kms[i - 1]), q3_my(metrics.gauss_fit.fitted_curve[i - 1]), q3_mx(vel_kms[i]), q3_my(metrics.gauss_fit.fitted_curve[i]), PixelColor(255, 118, 117), 1);
        }
    }

    // Render Q4: Waterfall slice
    int q4_plot_x = q4_x + 60;
    int q4_plot_y = q4_y + 45;
    int q4_plot_w = pane_w - 90;
    int q4_plot_h = pane_h - 85;

    int c_start = 0, c_end = wf_cols - 1;
    for (int c = 0; c < wf_cols; ++c) {
        if (freqs_mhz[c] >= HYDROGEN_REST_FREQ_MHZ - 0.5) { c_start = c; break; }
    }
    for (int c = wf_cols - 1; c >= 0; --c) {
        if (freqs_mhz[c] <= HYDROGEN_REST_FREQ_MHZ + 0.5) { c_end = c; break; }
    }
    int sub_cols = std::max(1, c_end - c_start + 1);

    std::vector<float> wf_sub(static_cast<size_t>(wf_rows) * sub_cols);
    std::vector<float> inv_base(sub_cols);
    for (int sc = 0; sc < sub_cols; ++sc) {
        inv_base[sc] = 1.0f / static_cast<float>(std::max(1e-12, baseline_power[c_start + sc]));
    }
    for (int r = 0; r < wf_rows; ++r) {
        const float* wf_row = &waterfall_data[static_cast<size_t>(r) * wf_cols + c_start];
        float* dst_row = &wf_sub[static_cast<size_t>(r) * sub_cols];
        for (int sc = 0; sc < sub_cols; ++sc) {
            dst_row[sc] = 10.0f * std::log10(std::max(1e-6f, wf_row[sc] * inv_base[sc]));
        }
    }
    if (wf_sub.empty()) return false;
    std::vector<float> scratch_vals = wf_sub;
    float vmin = 0.0f, vmax = 1.0f;
    compute_waterfall_percentiles(scratch_vals, vmin, vmax);

    canvas.render_waterfall(wf_sub.data(), wf_rows, sub_cols, q4_plot_x, q4_plot_y, q4_plot_w, q4_plot_h, vmin, vmax, ColormapType::Magma);
    canvas.draw_rect(q4_plot_x, q4_plot_y, q4_plot_w, q4_plot_h, PixelColor(48, 54, 61), 1);

    return canvas.save_png(output_path);
}

} // namespace astro

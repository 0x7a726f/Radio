#pragma once

#include <vector>
#include <string>
#include <cstdint>

enum class WaterfallPalette {
    ClassicYoussef,
    Viridis,
    Turbo,
    Inferno,
    Magma
};

class Waterfall {
public:
    Waterfall(int width = 800, int height = 260);
    ~Waterfall() = default;

    void resize(int width, int height);
    void resize_preserving_history(int width, int height);
    int get_width() const { return width_; }
    int get_height() const { return height_; }

    void set_palette(WaterfallPalette palette);
    WaterfallPalette get_palette() const { return current_palette_; }
    static const std::vector<std::string>& get_palette_names();

    // Push one spectrum slice into the top row, rolling existing rows down
    void add_line(const std::vector<float>& power_db, float min_db, float max_db);

    // Direct access to raw RGBA32 pixels (width * height * 4 bytes) and active 256-color LUT
    const uint32_t* get_pixel_data() const { return pixels_.data(); }
    const uint32_t* get_palette_lut() const { return palette_lut_; }
    size_t get_pixel_count() const { return pixels_.size(); }
    size_t get_byte_size() const { return pixels_.size() * sizeof(uint32_t); }

    void clear();

private:
    void rebuild_palette_lut();

    int width_ = 800;
    int height_ = 260;
    WaterfallPalette current_palette_ = WaterfallPalette::ClassicYoussef;

    std::vector<uint32_t> pixels_; // Row 0 is the newest slice
    uint32_t palette_lut_[256];
};

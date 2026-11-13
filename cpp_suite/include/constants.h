#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ======================================================================
// Real-time SDR & Receiver Constants
// ======================================================================
namespace SdrConstants {

// Astronomical Neutral Hydrogen Line (21 cm spin-flip transition)
constexpr double HI_REST_FREQ_HZ = 1420405751.768;
constexpr uint64_t HI_REST_FREQ_ROUNDED_HZ = 1420405752ULL;
constexpr double SPEED_OF_LIGHT_KM_S = 299792.458; // Speed of light in km/s

// Default SDR parameters
constexpr uint64_t DEFAULT_CENTER_FREQ_HZ = 1420500000ULL; // 1420.5 MHz (+94.248 kHz offset)
constexpr uint32_t DEFAULT_SAMPLE_RATE_HZ = 2400000U;     // 2.4 MSPS
constexpr float DEFAULT_GAIN_DB = 25.4f;
constexpr bool DEFAULT_BIAS_TEE = false;
constexpr int DEFAULT_PPM = 0;

// Default DSP & Display parameters
constexpr int DEFAULT_FFT_SIZE = 4096;
constexpr float DEFAULT_FFT_MIN_DB = -105.0f;
constexpr float DEFAULT_FFT_MAX_DB = -50.0f;
constexpr float DEFAULT_AVG_FACTOR = 0.65f;
constexpr int DEFAULT_WATERFALL_WIDTH = 800;
constexpr int DEFAULT_WATERFALL_HEIGHT = 260;

// Hardware buffer sizes
constexpr size_t SDR_CHUNK_BYTES = 262144; // 128k complex uint8 samples (256 KB)
constexpr size_t SDR_CHUNK_SAMPLES = SDR_CHUNK_BYTES / 2;
constexpr size_t RING_BUFFER_CAPACITY = 32 * SDR_CHUNK_BYTES; // ~8 MB ring buffer

// Presets
struct BandPreset {
    const char* name;
    uint64_t freq_hz;
};

inline const std::vector<BandPreset>& GetPresets() {
    static const std::vector<BandPreset> presets = {
        {"H I 1420.5M (LO +94k)", 1420500000ULL},
        {"H I 1420.406M (Rest Line)", 1420405752ULL},
        {"H I 1420.2M (SatDump)", 1420206000ULL},
        {"FM 89.2 MHz (Broadcast)", 89200000ULL},
        {"FM 100.0 MHz (Broadcast)", 100000000ULL},
        {"Airband 121.5 MHz (ATIS)", 121500000ULL},
        {"Ham 144.8 MHz (2m APRS)", 144800000ULL},
        {"ISM 433.92 MHz (Telemetry)", 433920000ULL},
        {"ADS-B 1090.0 MHz (Aviation)", 1090000000ULL}
    };
    return presets;
}

struct SampleRateOption {
    const char* label;
    uint32_t rate_hz;
};

inline const std::vector<SampleRateOption>& GetSampleRates() {
    static const std::vector<SampleRateOption> rates = {
        {"1.024 Msps", 1024000U},
        {"2.048 Msps", 2048000U},
        {"2.4 Msps",   2400000U},
        {"2.56 Msps",  2560000U},
        {"3.2 Msps",   3200000U}
    };
    return rates;
}

inline const std::vector<int>& GetFftSizes() {
    static const std::vector<int> sizes = {2048, 4096, 8192, 16384};
    return sizes;
}

// Doppler velocity: v = c * (f0 - f) / f0
inline double CalculateDopplerVelocityKmS(double observed_freq_hz) {
    return SPEED_OF_LIGHT_KM_S * (HI_REST_FREQ_HZ - observed_freq_hz) / HI_REST_FREQ_HZ;
}

} // namespace SdrConstants


// ======================================================================
// Scientific Astrophysics & Analysis Constants
// ======================================================================
namespace astro {

// Fundamental Physical and Astronomical Constants
constexpr double HYDROGEN_REST_FREQ_HZ = 1420405751.768; // Exact HI 21cm line rest frequency (Hz)
constexpr double HYDROGEN_REST_FREQ_MHZ = HYDROGEN_REST_FREQ_HZ / 1e6; // ~1420.405752 MHz
constexpr double SPEED_OF_LIGHT_KMS = 299792.458; // Speed of light in vacuum (km/s)
constexpr double SPEED_OF_LIGHT_MS = SPEED_OF_LIGHT_KMS * 1e3;

// Neutral Hydrogen Column Density Conversion Factor (optically thin approximation)
// N_HI = 1.8224e18 * integral(T_B * dv) in [atoms * cm^-2]
// where T_B is brightness temperature in K, dv is velocity in km/s
constexpr double HI_COLUMN_DENSITY_FACTOR = 1.8224e18;

// Gaussian FWHM constant: FWHM = 2 * sqrt(2 * ln(2)) * sigma
constexpr double GAUSSIAN_SIGMA_TO_FWHM = 2.3548200450309493;

// Observatory Slate Scientific UI Theme Palette (Hex RGBA & Floats)
namespace theme {
    // Primary deep background: #0D1117
    constexpr float BG_R = 0.051f, BG_G = 0.067f, BG_B = 0.090f, BG_A = 1.000f;
    // Card / Child surface: #131820
    constexpr float FRAME_R = 0.075f, FRAME_G = 0.094f, FRAME_B = 0.125f, FRAME_A = 1.000f;
    // Surface border: #2D3644
    constexpr float BORDER_R = 0.176f, BORDER_G = 0.212f, BORDER_B = 0.267f, BORDER_A = 1.000f;
    // Primary CTA Accent: #2F81F7
    constexpr float ACCENT_R = 0.184f, ACCENT_G = 0.506f, ACCENT_B = 0.969f, ACCENT_A = 1.000f;
    // Cyan Live/HI Spectrum Trace: #38BDF8
    constexpr float CYAN_R = 0.220f, CYAN_G = 0.741f, CYAN_B = 0.973f, CYAN_A = 1.000f;
    // Alert / Recording / Peak Marker: #EF4444
    constexpr float RED_R = 0.937f, RED_G = 0.267f, RED_B = 0.267f, RED_A = 1.000f;
    // Emerald / Live Active / Residuals: #10B981
    constexpr float GREEN_R = 0.063f, GREEN_G = 0.725f, GREEN_B = 0.506f, GREEN_A = 1.000f;
    // Amber Warning / Bias-Tee / HI Rest Marker: #F59E0B
    constexpr float AMBER_R = 0.961f, AMBER_G = 0.620f, AMBER_B = 0.043f, AMBER_A = 1.000f;
    // Orange Baseline Curve: #FB923C
    constexpr float ORANGE_R = 0.984f, ORANGE_G = 0.573f, ORANGE_B = 0.235f, ORANGE_A = 1.000f;
    // Pink Gaussian Fit Curve: #F472B6
    constexpr float PINK_R = 0.957f, PINK_G = 0.447f, PINK_B = 0.714f, PINK_A = 1.000f;
    // Muted text / Grid: #8B949E
    constexpr float MUTED_R = 0.545f, MUTED_G = 0.580f, MUTED_B = 0.620f, MUTED_A = 1.000f;
}

} // namespace astro

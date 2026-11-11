#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SdrConstants {

// Astronomical Neutral Hydrogen Line (21 cm spin-flip transition)
constexpr double HI_REST_FREQ_HZ = 1420405751.786;
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

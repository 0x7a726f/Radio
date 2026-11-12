#pragma once

#include <cstdint>

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

// SatDump-inspired UI Theme Palette (Hex RGBA & Floats)
namespace theme {
    // Primary deep background: #0F0F0FF0
    constexpr float BG_R = 0.059f, BG_G = 0.059f, BG_B = 0.059f, BG_A = 0.941f;
    // Frame & Child surface: #3336388A
    constexpr float FRAME_R = 0.200f, FRAME_G = 0.212f, FRAME_B = 0.220f, FRAME_A = 0.541f;
    // Primary Button / Header Accent: #3D85E0
    constexpr float ACCENT_R = 0.239f, ACCENT_G = 0.522f, ACCENT_B = 0.878f, ACCENT_A = 1.0f;
    // Cyan glow / Spectrum / Reticle: #66E6FFFF
    constexpr float CYAN_R = 0.400f, CYAN_G = 0.902f, CYAN_B = 1.0f, CYAN_A = 1.0f;
    // Danger / LO DC notch: #FF4757
    constexpr float RED_R = 1.0f, RED_G = 0.278f, RED_B = 0.341f, RED_A = 1.0f;
    // Emerald / Valid HI line: #10AC84 / #1DD1A1
    constexpr float GREEN_R = 0.114f, GREEN_G = 0.820f, GREEN_B = 0.631f, GREEN_A = 1.0f;
    // Warning / Baseline curve: #FF9F43
    constexpr float ORANGE_R = 1.0f, ORANGE_G = 0.624f, ORANGE_B = 0.263f, ORANGE_A = 1.0f;
    // Muted text / Grid: #8B949E
    constexpr float MUTED_R = 0.545f, MUTED_G = 0.580f, MUTED_B = 0.620f, MUTED_A = 1.0f;
}

} // namespace astro

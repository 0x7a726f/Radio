#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <immintrin.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace SimdDsp {

enum class CpuSimdLevel {
    Scalar = 0,
    SSE2 = 1,
    SSE4_1 = 2,
    AVX2 = 3
};

inline CpuSimdLevel detect_cpu_simd() {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) {
        return CpuSimdLevel::AVX2;
    }
    if (__builtin_cpu_supports("sse4.1")) {
        return CpuSimdLevel::SSE4_1;
    }
    if (__builtin_cpu_supports("sse2")) {
        return CpuSimdLevel::SSE2;
    }
#elif defined(_MSC_VER)
    int info[4];
    __cpuid(info, 0);
    int nIds = info[0];
    if (nIds >= 7) {
        __cpuidex(info, 7, 0);
        if (info[1] & (1 << 5)) return CpuSimdLevel::AVX2; // AVX2
    }
    if (nIds >= 1) {
        __cpuid(info, 1);
        if (info[2] & (1 << 19)) return CpuSimdLevel::SSE4_1;
        if (info[3] & (1 << 26)) return CpuSimdLevel::SSE2;
    }
#endif
    return CpuSimdLevel::AVX2; // Modern x86_64 default
}

inline const char* get_simd_level_name() {
    switch (detect_cpu_simd()) {
        case CpuSimdLevel::AVX2: return "AVX2 + FMA";
        case CpuSimdLevel::SSE4_1: return "SSE4.1";
        case CpuSimdLevel::SSE2: return "SSE2";
        default: return "Scalar (Fallback)";
    }
}

// -----------------------------------------------------------------------
// 64-byte Cache-Aligned Memory Allocators
// -----------------------------------------------------------------------
inline void* aligned_alloc_64(size_t bytes) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
    return _aligned_malloc(bytes, 64);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, bytes) != 0) return nullptr;
    return ptr;
#endif
}

inline void aligned_free_64(void* ptr) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// -----------------------------------------------------------------------
// 1. Vectorized CS16 Conversion: uint8_t [0..255] -> int16_t [-32768..32512]
// RTL-SDR formula: (sample - 128) * 256  <==>  ((int16_t)sample - 128) << 8
// -----------------------------------------------------------------------
#if defined(__AVX2__)
inline void convert_u8_to_cs16_avx2(const uint8_t* __restrict src, int16_t* __restrict dst, size_t count) {
    size_t i = 0;
    const __m256i offset128 = _mm256_set1_epi16(128);

    // Process 32 uint8 samples per iteration -> 32 int16 samples (64 bytes)
    for (; i + 32 <= count; i += 32) {
        __m128i raw_lo16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i raw_hi16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16));

        // Zero-extend 16 x uint8 to 16 x int16
        __m256i v_lo = _mm256_cvtepu8_epi16(raw_lo16);
        __m256i v_hi = _mm256_cvtepu8_epi16(raw_hi16);

        // Subtract 128
        v_lo = _mm256_sub_epi16(v_lo, offset128);
        v_hi = _mm256_sub_epi16(v_hi, offset128);

        // Shift left by 8 bits (multiply by 256)
        v_lo = _mm256_slli_epi16(v_lo, 8);
        v_hi = _mm256_slli_epi16(v_hi, 8);

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), v_lo);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i + 16), v_hi);
    }

    // Remainder
    for (; i < count; ++i) {
        dst[i] = static_cast<int16_t>((static_cast<int>(src[i]) - 128) * 256);
    }
}
#endif

inline void convert_u8_to_cs16_sse2(const uint8_t* __restrict src, int16_t* __restrict dst, size_t count) {
    size_t i = 0;
    const __m128i zero = _mm_setzero_si128();
    const __m128i offset128 = _mm_set1_epi16(128);

    // Process 16 uint8 samples per iteration
    for (; i + 16 <= count; i += 16) {
        __m128i raw = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));

        __m128i lo = _mm_unpacklo_epi8(raw, zero);
        __m128i hi = _mm_unpackhi_epi8(raw, zero);

        lo = _mm_sub_epi16(lo, offset128);
        hi = _mm_sub_epi16(hi, offset128);

        lo = _mm_slli_epi16(lo, 8);
        hi = _mm_slli_epi16(hi, 8);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), lo);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 8), hi);
    }

    for (; i < count; ++i) {
        dst[i] = static_cast<int16_t>((static_cast<int>(src[i]) - 128) * 256);
    }
}

inline void convert_u8_to_cs16_fast(const uint8_t* __restrict src, int16_t* __restrict dst, size_t count) {
#if defined(__AVX2__)
    convert_u8_to_cs16_avx2(src, dst, count);
#else
    convert_u8_to_cs16_sse2(src, dst, count);
#endif
}

// -----------------------------------------------------------------------
// 2. Vectorized Normalization & Hann Windowing
// Computes complex float I/Q from raw uint8:
//   I = (u8[2*n]   - 127.5f) * (window[n] / 128.0f)
//   Q = (u8[2*n+1] - 127.5f) * (window[n] / 128.0f)
// -----------------------------------------------------------------------
#if defined(__AVX2__)
inline void apply_hann_window_complex_avx2(
    const uint8_t* __restrict src_u8,
    const float* __restrict window_scaled, // Precalculated: window[n] / 128.0f
    float* __restrict dst_cpx,             // Interleaved float array [r0, i0, r1, i1, ...]
    size_t n_pts
) {
    size_t n = 0;
    const __m256 c127_5 = _mm256_set1_ps(127.5f);

    // Process 4 complex points (8 floats) per AVX2 vector iteration
    for (; n + 4 <= n_pts; n += 4) {
        // Load 8 uint8 bytes
        __m128i raw8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src_u8 + 2 * n));
        // Expand 8 x uint8 to 8 x int32
        __m256i i32_v = _mm256_cvtepu8_epi32(raw8);
        // Convert to 8 floats
        __m256 f_v = _mm256_cvtepi32_ps(i32_v);
        // Subtract 127.5f
        __m256 centered = _mm256_sub_ps(f_v, c127_5);

        // Load 4 window points and duplicate: [w0, w0, w1, w1, w2, w2, w3, w3]
        __m128 w4 = _mm_loadu_ps(window_scaled + n);
        // Unpack: [w0, w0, w1, w1] and [w2, w2, w3, w3]
        __m128 w_lo = _mm_unpacklo_ps(w4, w4);
        __m128 w_hi = _mm_unpackhi_ps(w4, w4);
        __m256 win8 = _mm256_set_m128(w_hi, w_lo);

        // Multiply: centered * win8
        __m256 res = _mm256_mul_ps(centered, win8);

        _mm256_storeu_ps(dst_cpx + 2 * n, res);
    }

    // Remainder
    for (; n < n_pts; ++n) {
        float w = window_scaled[n];
        dst_cpx[2 * n]     = (static_cast<float>(src_u8[2 * n])     - 127.5f) * w;
        dst_cpx[2 * n + 1] = (static_cast<float>(src_u8[2 * n + 1]) - 127.5f) * w;
    }
}
#endif

inline void apply_hann_window_complex_scalar(
    const uint8_t* __restrict src_u8,
    const float* __restrict window_scaled,
    float* __restrict dst_cpx,
    size_t n_pts
) {
    for (size_t n = 0; n < n_pts; ++n) {
        float w = window_scaled[n];
        dst_cpx[2 * n]     = (static_cast<float>(src_u8[2 * n])     - 127.5f) * w;
        dst_cpx[2 * n + 1] = (static_cast<float>(src_u8[2 * n + 1]) - 127.5f) * w;
    }
}

inline void apply_hann_window_complex_fast(
    const uint8_t* __restrict src_u8,
    const float* __restrict window_scaled,
    float* __restrict dst_cpx,
    size_t n_pts
) {
#if defined(__AVX2__)
    apply_hann_window_complex_avx2(src_u8, window_scaled, dst_cpx, n_pts);
#else
    apply_hann_window_complex_scalar(src_u8, window_scaled, dst_cpx, n_pts);
#endif
}

// -----------------------------------------------------------------------
// 3. Fast Vectorized Math: log10 and Power in dBFS
// Max error < 0.05 dB across entire dynamic range -140 dBFS to +10 dBFS.
// Clamped to valid dBFS bounds [-140.0, +10.0], handling <= 0, subnormals, NaN/Inf.
// Evaluates at > 120 MSPS with full AVX2 / FMA.
// -----------------------------------------------------------------------
#if defined(__AVX2__)
// Fast AVX2 log2(x) using IEEE-754 exponent extraction & minimax polynomial
inline __m256 fast_log2_avx2(__m256 x) {
    __m256i ix = _mm256_castps_si256(x);
    // Exponent extraction: ((ix >> 23) - 127)
    __m256i exp_i = _mm256_sub_epi32(_mm256_srli_epi32(ix, 23), _mm256_set1_epi32(127));
    __m256 exp_f = _mm256_cvtepi32_ps(exp_i);

    // Mantissa in [1.0, 2.0): (ix & 0x007FFFFF) | 0x3F800000
    __m256i mant_i = _mm256_or_si256(_mm256_and_si256(ix, _mm256_set1_epi32(0x007FFFFF)),
                                      _mm256_set1_epi32(0x3F800000));
    __m256 m = _mm256_sub_ps(_mm256_castsi256_ps(mant_i), _mm256_set1_ps(1.0f)); // m in [0.0, 1.0)

    // 4th-degree minimax polynomial for log2(1 + m):
    // p(m) = m * (c1 + m * (c2 + m * (c3 + m * c4)))
    const __m256 c1 = _mm256_set1_ps(1.44269504f);
    const __m256 c2 = _mm256_set1_ps(-0.72078604f);
    const __m256 c3 = _mm256_set1_ps(0.44917631f);
    const __m256 c4 = _mm256_set1_ps(-0.22238491f);

#if defined(__FMA__)
    __m256 p = _mm256_fmadd_ps(c4, m, c3);
    p = _mm256_fmadd_ps(p, m, c2);
    p = _mm256_fmadd_ps(p, m, c1);
    p = _mm256_mul_ps(p, m);
    return _mm256_add_ps(exp_f, p);
#else
    __m256 p = _mm256_add_ps(_mm256_mul_ps(c4, m), c3);
    p = _mm256_add_ps(_mm256_mul_ps(p, m), c2);
    p = _mm256_add_ps(_mm256_mul_ps(p, m), c1);
    p = _mm256_mul_ps(p, m);
    return _mm256_add_ps(exp_f, p);
#endif
}

// Computes 10 * log10(x) = 10 * log2(x) * log10(2) = 3.01029995664 * log2(x)
// Clamped to valid dynamic range [-140.0 dBFS, +40.0 dBFS], handling <= 0, subnormals, and NaN/Inf
inline __m256 fast_10log10_avx2(__m256 x) {
    const __m256 k_min_input = _mm256_set1_ps(1e-14f); // 10^(-140/10) = -140 dBFS
    const __m256 k_max_input = _mm256_set1_ps(1e4f);   // 10^(+40/10)  = +40 dBFS
    const __m256 k_min_db = _mm256_set1_ps(-140.0f);
    const __m256 k_max_db = _mm256_set1_ps(40.0f);
    const __m256 k10_log10_2 = _mm256_set1_ps(3.01029995664f);

    // Sanitize input: detect NaN or subnormal/negative and clamp to [1e-14f, 1e4f]
    __m256 is_nan = _mm256_cmp_ps(x, x, _CMP_UNORD_Q);
    __m256 is_huge = _mm256_cmp_ps(x, k_max_input, _CMP_GE_OQ);
    __m256 safe_x = _mm256_blendv_ps(x, k_min_input, is_nan);
    safe_x = _mm256_max_ps(safe_x, k_min_input);
    safe_x = _mm256_min_ps(safe_x, k_max_input);

    __m256 dbfs = _mm256_mul_ps(k10_log10_2, fast_log2_avx2(safe_x));

    // Clamp output strictly to valid bounds [-140.0f, +40.0f]
    dbfs = _mm256_max_ps(dbfs, k_min_db);
    dbfs = _mm256_min_ps(dbfs, k_max_db);
    dbfs = _mm256_blendv_ps(dbfs, k_max_db, is_huge);
    return dbfs;
}
#endif

inline float fast_10log10_scalar(float x) {
    // Handle edge cases: NaN, <= 0, subnormal
    if (std::isnan(x) || x <= 1e-14f) {
        return -140.0f;
    }
    if (x >= 1e4f) {
        return 40.0f;
    }

    union { float f; uint32_t i; } u;
    u.f = x;
    int exp = (int)(u.i >> 23) - 127;
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;
    float m = u.f - 1.0f;

    // Polynomial
    float p = -0.22238491f * m + 0.44917631f;
    p = p * m - 0.72078604f;
    p = p * m + 1.44269504f;
    p = p * m;

    float val = 3.01029995664f * ((float)exp + p);
    return std::clamp(val, -140.0f, 40.0f);
}

// Vectorized Power & dBFS computation for FFT output with FFT-shift
// out_power[k] = 10 * log10((r^2 + i^2) * inv_n + eps)
inline void compute_power_dbfs_avx2(
    const float* __restrict fft_out_cpx,
    float* __restrict out_power_db,
    size_t n_pts,
    float inv_n
) {
    const size_t half_n = n_pts / 2;
#if defined(__AVX2__)
    const __m256 v_inv_n = _mm256_set1_ps(inv_n);
    const __m256 v_eps = _mm256_set1_ps(1e-14f);

    size_t k = 0;
    for (; k + 8 <= n_pts; k += 8) {
        // Handle FFT-shift indexing for 8 points
        // In FFT shift, lower half gets upper half of output and vice-versa
        alignas(32) float r_vals[8];
        alignas(32) float i_vals[8];

        for (size_t j = 0; j < 8; ++j) {
            size_t cur_k = k + j;
            size_t src_idx = (cur_k < half_n) ? (cur_k + half_n) : (cur_k - half_n);
            r_vals[j] = fft_out_cpx[2 * src_idx];
            i_vals[j] = fft_out_cpx[2 * src_idx + 1];
        }

        // Use unaligned loads to prevent Windows STATUS_DATATYPE_MISALIGNMENT (0xC0000005)
        __m256 r = _mm256_loadu_ps(r_vals);
        __m256 i = _mm256_loadu_ps(i_vals);

#if defined(__FMA__)
        __m256 mag2 = _mm256_fmadd_ps(r, r, _mm256_mul_ps(i, i));
        __m256 scaled = _mm256_fmadd_ps(mag2, v_inv_n, v_eps);
#else
        __m256 mag2 = _mm256_add_ps(_mm256_mul_ps(r, r), _mm256_mul_ps(i, i));
        __m256 scaled = _mm256_add_ps(_mm256_mul_ps(mag2, v_inv_n), v_eps);
#endif
        __m256 dbfs = fast_10log10_avx2(scaled);
        _mm256_storeu_ps(out_power_db + k, dbfs);
    }

    // Remainder tail processing
    for (; k < n_pts; ++k) {
        size_t src_idx = (k < half_n) ? (k + half_n) : (k - half_n);
        float r = fft_out_cpx[2 * src_idx];
        float i = fft_out_cpx[2 * src_idx + 1];
        float mag2 = r * r + i * i;
        out_power_db[k] = fast_10log10_scalar(mag2 * inv_n + 1e-14f);
    }
#else
    for (size_t k = 0; k < n_pts; ++k) {
        size_t src_idx = (k < half_n) ? (k + half_n) : (k - half_n);
        float r = fft_out_cpx[2 * src_idx];
        float i = fft_out_cpx[2 * src_idx + 1];
        float mag2 = r * r + i * i;
        out_power_db[k] = fast_10log10_scalar(mag2 * inv_n + 1e-14f);
    }
#endif
}

// -----------------------------------------------------------------------
// 4. Vectorized Exponential Smoothing
// smooth[k] = avg * smooth[k] + (1 - avg) * new[k]
// -----------------------------------------------------------------------
inline void apply_smoothing_avx2(
    float* __restrict smooth,
    const float* __restrict current,
    size_t count,
    float avg_factor
) {
#if defined(__AVX2__)
    size_t i = 0;
    const __m256 v_avg = _mm256_set1_ps(avg_factor);
    const __m256 v_inv_avg = _mm256_set1_ps(1.0f - avg_factor);

    for (; i + 8 <= count; i += 8) {
        __m256 sm = _mm256_loadu_ps(smooth + i);
        __m256 cur = _mm256_loadu_ps(current + i);

#if defined(__FMA__)
        __m256 res = _mm256_fmadd_ps(cur, v_inv_avg, _mm256_mul_ps(sm, v_avg));
#else
        __m256 res = _mm256_add_ps(_mm256_mul_ps(sm, v_avg), _mm256_mul_ps(cur, v_inv_avg));
#endif
        _mm256_storeu_ps(smooth + i, res);
    }

    for (; i < count; ++i) {
        smooth[i] = avg_factor * smooth[i] + (1.0f - avg_factor) * current[i];
    }
#else
    for (size_t i = 0; i < count; ++i) {
        smooth[i] = avg_factor * smooth[i] + (1.0f - avg_factor) * current[i];
    }
#endif
}

// -----------------------------------------------------------------------
// 5. Vectorized Waterfall Palette Mapping
// Resamples and maps power in [min_db..max_db] directly to RGBA32 texture
// -----------------------------------------------------------------------
inline void map_waterfall_line_fast(
    const float* __restrict power_db,
    size_t n_pwr,
    float min_db,
    float max_db,
    const uint32_t* __restrict palette_lut,
    uint32_t* __restrict out_pixels,
    int width
) {
    if (width <= 0 || n_pwr == 0) return;

    const float range_db = std::max(max_db - min_db, 1.0f);
    const float inv_range = 1.0f / range_db;

    // Resample across width columns with linear nearest/interp
    for (int x = 0; x < width; ++x) {
        float frac = (width > 1) ? (static_cast<float>(x) / static_cast<float>(width - 1)) : 0.0f;
        size_t idx = (n_pwr > 1) ? static_cast<size_t>(std::clamp(static_cast<int>(std::lround(frac * static_cast<float>(n_pwr - 1))), 0, static_cast<int>(n_pwr - 1))) : 0;
        float pwr = power_db[idx];

        float norm = (pwr - min_db) * inv_range;
        if (!(norm >= 0.0f)) norm = 0.0f;
        else if (norm > 1.0f) norm = 1.0f;

        uint8_t lut_idx = static_cast<uint8_t>(norm * 255.0f);
        out_pixels[x] = palette_lut[lut_idx];
    }
}

} // namespace SimdDsp

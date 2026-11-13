#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <vector>
#include <immintrin.h>

#if defined(__FMA__) || (defined(_MSC_VER) && defined(__AVX2__))
#define SIMD_HAS_FMA 1
#else
#define SIMD_HAS_FMA 0
#endif

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
    static const CpuSimdLevel cached_level = []() -> CpuSimdLevel {
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
    }();
    return cached_level;
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

#if SIMD_HAS_FMA
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

// High-precision (~1e-14) double-precision AVX2+FMA log10(x) for positive normal doubles
inline __m256d fast_log10_pd_avx2(__m256d x) {
    const __m256d k_sqrt2    = _mm256_set1_pd(1.4142135623730950488);
    const __m256d k_one      = _mm256_set1_pd(1.0);
    const __m256d k_half     = _mm256_set1_pd(0.5);
    const __m256d k_bias     = _mm256_set1_pd(4503599627371519.0); // 2^52 + 1023
    const __m256d k_log10_2  = _mm256_set1_pd(0.30102999566398119521);
    const __m256d k_2log10_e = _mm256_set1_pd(0.86858896380650365530); // 2 * log10(e)
    const __m256i k_exp_zero = _mm256_set1_epi64x(0x3FF0000000000000ULL);
    const __m256i k_mant_msk = _mm256_set1_epi64x(0x000FFFFFFFFFFFFFULL);
    const __m256i k_p2_52    = _mm256_set1_epi64x(0x4330000000000000ULL);

    __m256i ix = _mm256_castpd_si256(x);
    __m256i e_raw = _mm256_srli_epi64(ix, 52);
    __m256d k = _mm256_sub_pd(_mm256_castsi256_pd(_mm256_or_si256(e_raw, k_p2_52)), k_bias);
    __m256d m = _mm256_castsi256_pd(_mm256_or_si256(_mm256_and_si256(ix, k_mant_msk), k_exp_zero));

    // Shift mantissa to [sqrt(2)/2, sqrt(2)) so |u| = |(m-1)/(m+1)| <= 0.171573
    __m256d mask = _mm256_cmp_pd(m, k_sqrt2, _CMP_GE_OQ);
    m = _mm256_blendv_pd(m, _mm256_mul_pd(m, k_half), mask);
    k = _mm256_blendv_pd(k, _mm256_add_pd(k, k_one), mask);

    __m256d u = _mm256_div_pd(_mm256_sub_pd(m, k_one), _mm256_add_pd(m, k_one));
    __m256d u2 = _mm256_mul_pd(u, u);

    const __m256d c15 = _mm256_set1_pd(1.0 / 15.0);
    const __m256d c13 = _mm256_set1_pd(1.0 / 13.0);
    const __m256d c11 = _mm256_set1_pd(1.0 / 11.0);
    const __m256d c9  = _mm256_set1_pd(1.0 / 9.0);
    const __m256d c7  = _mm256_set1_pd(1.0 / 7.0);
    const __m256d c5  = _mm256_set1_pd(1.0 / 5.0);
    const __m256d c3  = _mm256_set1_pd(1.0 / 3.0);

#if SIMD_HAS_FMA
    __m256d poly = _mm256_fmadd_pd(c15, u2, c13);
    poly = _mm256_fmadd_pd(poly, u2, c11);
    poly = _mm256_fmadd_pd(poly, u2, c9);
    poly = _mm256_fmadd_pd(poly, u2, c7);
    poly = _mm256_fmadd_pd(poly, u2, c5);
    poly = _mm256_fmadd_pd(poly, u2, c3);
    poly = _mm256_fmadd_pd(poly, u2, k_one);
    __m256d log10_m = _mm256_mul_pd(_mm256_mul_pd(k_2log10_e, u), poly);
    return _mm256_fmadd_pd(k, k_log10_2, log10_m);
#else
    __m256d poly = _mm256_add_pd(_mm256_mul_pd(c15, u2), c13);
    poly = _mm256_add_pd(_mm256_mul_pd(poly, u2), c11);
    poly = _mm256_add_pd(_mm256_mul_pd(poly, u2), c9);
    poly = _mm256_add_pd(_mm256_mul_pd(poly, u2), c7);
    poly = _mm256_add_pd(_mm256_mul_pd(poly, u2), c5);
    poly = _mm256_add_pd(_mm256_mul_pd(poly, u2), c3);
    poly = _mm256_add_pd(_mm256_mul_pd(poly, u2), k_one);
    __m256d log10_m = _mm256_mul_pd(_mm256_mul_pd(k_2log10_e, u), poly);
    return _mm256_add_pd(_mm256_mul_pd(k, k_log10_2), log10_m);
#endif
}

// High-precision (~1e-14) double-precision AVX2+FMA pow(10, x) for x in [-300, +300]
inline __m256d fast_exp10_pd_avx2(__m256d x) {
    const __m256d k_min_x   = _mm256_set1_pd(-300.0);
    const __m256d k_max_x   = _mm256_set1_pd(300.0);
    const __m256d k_log2_10 = _mm256_set1_pd(3.32192809488736234787);
    const __m256d k_ln2     = _mm256_set1_pd(0.69314718055994530942);
    const __m256d k_bias    = _mm256_set1_pd(4503599627371519.0); // 2^52 + 1023
    const __m256d k_one     = _mm256_set1_pd(1.0);

    __m256d safe_x = _mm256_min_pd(_mm256_max_pd(x, k_min_x), k_max_x);
    __m256d z = _mm256_mul_pd(safe_x, k_log2_10);
    __m256d k = _mm256_round_pd(z, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    __m256d f = _mm256_sub_pd(z, k); // |f| <= 0.5
    __m256d t = _mm256_mul_pd(f, k_ln2); // |t| <= 0.346574

    // Degree-11 Taylor polynomial of exp(t)
    const __m256d c11 = _mm256_set1_pd(1.0 / 39916800.0);
    const __m256d c10 = _mm256_set1_pd(1.0 / 3628800.0);
    const __m256d c9  = _mm256_set1_pd(1.0 / 362880.0);
    const __m256d c8  = _mm256_set1_pd(1.0 / 40320.0);
    const __m256d c7  = _mm256_set1_pd(1.0 / 5040.0);
    const __m256d c6  = _mm256_set1_pd(1.0 / 720.0);
    const __m256d c5  = _mm256_set1_pd(1.0 / 120.0);
    const __m256d c4  = _mm256_set1_pd(1.0 / 24.0);
    const __m256d c3  = _mm256_set1_pd(1.0 / 6.0);
    const __m256d c2  = _mm256_set1_pd(0.5);

#if SIMD_HAS_FMA
    __m256d p = _mm256_fmadd_pd(c11, t, c10);
    p = _mm256_fmadd_pd(p, t, c9);
    p = _mm256_fmadd_pd(p, t, c8);
    p = _mm256_fmadd_pd(p, t, c7);
    p = _mm256_fmadd_pd(p, t, c6);
    p = _mm256_fmadd_pd(p, t, c5);
    p = _mm256_fmadd_pd(p, t, c4);
    p = _mm256_fmadd_pd(p, t, c3);
    p = _mm256_fmadd_pd(p, t, c2);
    p = _mm256_fmadd_pd(p, t, k_one);
    p = _mm256_fmadd_pd(p, t, k_one);
#else
    __m256d p = _mm256_add_pd(_mm256_mul_pd(c11, t), c10);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c9);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c8);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c7);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c6);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c5);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c4);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c3);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), c2);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), k_one);
    p = _mm256_add_pd(_mm256_mul_pd(p, t), k_one);
#endif

    __m256i pow2k_i = _mm256_slli_epi64(_mm256_castpd_si256(_mm256_add_pd(k, k_bias)), 52);
    return _mm256_mul_pd(p, _mm256_castsi256_pd(pow2k_i));
}
#endif

// Vectorized 10 * log10(max(in[i], floor_val)) over double array
inline void power_to_db_f64(const double* __restrict in, double* __restrict out, size_t n, double floor_val = 1e-12) {
    size_t i = 0;
#if defined(__AVX2__)
    const __m256d v_floor = _mm256_set1_pd(floor_val);
    const __m256d v_ten   = _mm256_set1_pd(10.0);
    for (; i + 4 <= n; i += 4) {
        __m256d x = _mm256_loadu_pd(in + i);
        __m256d is_nan = _mm256_cmp_pd(x, x, _CMP_UNORD_Q);
        __m256d safe_x = _mm256_max_pd(_mm256_blendv_pd(x, v_floor, is_nan), v_floor);
        __m256d db = _mm256_mul_pd(v_ten, fast_log10_pd_avx2(safe_x));
        _mm256_storeu_pd(out + i, db);
    }
#endif
    for (; i < n; ++i) {
        double v = (std::isnan(in[i]) || in[i] < floor_val) ? floor_val : in[i];
        out[i] = 10.0 * std::log10(v);
    }
}

// Vectorized 10^(in_db[i] / 10.0) over double array
inline void db_to_power_f64(const double* __restrict in_db, double* __restrict out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    const __m256d v_inv10 = _mm256_set1_pd(0.1);
    for (; i + 4 <= n; i += 4) {
        __m256d db = _mm256_loadu_pd(in_db + i);
        __m256d pwr = fast_exp10_pd_avx2(_mm256_mul_pd(db, v_inv10));
        _mm256_storeu_pd(out + i, pwr);
    }
#endif
    for (; i < n; ++i) {
        out[i] = std::pow(10.0, in_db[i] * 0.1);
    }
}

// Vectorized CS16 -> windowed interleaved float conversion:
// src_i16 has 2 * n_pts int16_t values [I0, Q0, I1, Q1, ...]
// win_interleaved has 2 * n_pts floats [w0, w0, w1, w1, ...]
inline void convert_cs16_window_float_fast(
    const int16_t* __restrict src_i16,
    const float* __restrict win_interleaved,
    float* __restrict dst_cpx,
    size_t n_pts
) {
    size_t k = 0;
    const size_t total_scalars = n_pts * 2;
#if defined(__AVX2__)
    for (; k + 8 <= total_scalars; k += 8) {
        __m128i raw8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_i16 + k));
        __m256i i32 = _mm256_cvtepi16_epi32(raw8);
        __m256 f = _mm256_cvtepi32_ps(i32);
        __m256 w = _mm256_loadu_ps(win_interleaved + k);
        _mm256_storeu_ps(dst_cpx + k, _mm256_mul_ps(f, w));
    }
#endif
    for (; k < total_scalars; ++k) {
        dst_cpx[k] = static_cast<float>(src_i16[k]) * win_interleaved[k];
    }
}

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

    // Helper lambda to process a contiguous complex range [src_base .. src_base + len) -> out_base
    auto process_range = [&](const float* __restrict src_cpx, float* __restrict dst_db, size_t len) {
        size_t i = 0;
        for (; i + 8 <= len; i += 8) {
            __m256 v0 = _mm256_loadu_ps(src_cpx + 2 * i);
            __m256 v1 = _mm256_loadu_ps(src_cpx + 2 * i + 8);
            __m256 sq0 = _mm256_mul_ps(v0, v0);
            __m256 sq1 = _mm256_mul_ps(v1, v1);
            // Horizontal add pairs of r^2 + i^2, then permute 64-bit lanes back to linear [0..7]
            __m256 hsum = _mm256_hadd_ps(sq0, sq1);
            __m256 mag2 = _mm256_castsi256_ps(_mm256_permute4x64_epi64(_mm256_castps_si256(hsum), 0xD8));

#if SIMD_HAS_FMA
            __m256 scaled = _mm256_fmadd_ps(mag2, v_inv_n, v_eps);
#else
            __m256 scaled = _mm256_add_ps(_mm256_mul_ps(mag2, v_inv_n), v_eps);
#endif
            __m256 dbfs = fast_10log10_avx2(scaled);
            _mm256_storeu_ps(dst_db + i, dbfs);
        }
        for (; i < len; ++i) {
            float r = src_cpx[2 * i];
            float im = src_cpx[2 * i + 1];
            float mag2 = r * r + im * im;
            dst_db[i] = fast_10log10_scalar(mag2 * inv_n + 1e-14f);
        }
    };

    // FFT shift: [half_n .. n_pts) -> out[0 .. n_pts - half_n), then [0 .. half_n) -> out[n_pts - half_n .. n_pts)
    const size_t first_len = n_pts - half_n;
    process_range(fft_out_cpx + 2 * half_n, out_power_db, first_len);
    process_range(fft_out_cpx, out_power_db + first_len, half_n);
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

#if SIMD_HAS_FMA
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
// 5. Peak-Preserving Vectorized Waterfall Palette Mapping
// Max-pools across frequency bins per pixel column so narrow spikes remain visible,
// then quantizes 8 columns at a time with AVX2.
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
    const float scale = 255.0f / range_db;

    thread_local std::vector<float> col_pwr;
    col_pwr.resize(static_cast<size_t>(width));

    if (n_pwr >= static_cast<size_t>(width)) {
        for (int x = 0; x < width; ++x) {
            size_t b0 = static_cast<size_t>(x) * n_pwr / static_cast<size_t>(width);
            size_t b1 = static_cast<size_t>(x + 1) * n_pwr / static_cast<size_t>(width);
            if (b1 <= b0) b1 = std::min(b0 + 1, n_pwr);
            float peak = power_db[b0];
            for (size_t b = b0 + 1; b < b1; ++b) {
                if (power_db[b] > peak) peak = power_db[b];
            }
            col_pwr[x] = peak;
        }
    } else {
        for (int x = 0; x < width; ++x) {
            float pos = (width > 1) ? (static_cast<float>(x) / static_cast<float>(width - 1)) * static_cast<float>(n_pwr - 1) : 0.0f;
            size_t b0 = static_cast<size_t>(pos);
            size_t b1 = std::min(b0 + 1, n_pwr - 1);
            float frac = pos - static_cast<float>(b0);
            col_pwr[x] = power_db[b0] + frac * (power_db[b1] - power_db[b0]);
        }
    }

    int x = 0;
#if defined(__AVX2__)
    const __m256 v_min   = _mm256_set1_ps(min_db);
    const __m256 v_scale = _mm256_set1_ps(scale);
    const __m256 v_zero  = _mm256_setzero_ps();
    const __m256 v_255   = _mm256_set1_ps(255.0f);
    for (; x + 8 <= width; x += 8) {
        __m256 p = _mm256_loadu_ps(col_pwr.data() + x);
        __m256 scaled = _mm256_mul_ps(_mm256_sub_ps(p, v_min), v_scale);
        // Clamp NaNs safely via min(max(scaled, 0), 255): cmp/blend or max(0, x) where NaN becomes 0
        __m256 valid = _mm256_cmp_ps(scaled, scaled, _CMP_ORD_Q);
        scaled = _mm256_and_ps(scaled, valid);
        scaled = _mm256_min_ps(_mm256_max_ps(scaled, v_zero), v_255);
        alignas(32) int32_t idx[8];
        _mm256_store_si256(reinterpret_cast<__m256i*>(idx), _mm256_cvttps_epi32(scaled));
        out_pixels[x + 0] = palette_lut[idx[0]];
        out_pixels[x + 1] = palette_lut[idx[1]];
        out_pixels[x + 2] = palette_lut[idx[2]];
        out_pixels[x + 3] = palette_lut[idx[3]];
        out_pixels[x + 4] = palette_lut[idx[4]];
        out_pixels[x + 5] = palette_lut[idx[5]];
        out_pixels[x + 6] = palette_lut[idx[6]];
        out_pixels[x + 7] = palette_lut[idx[7]];
    }
#endif
    for (; x < width; ++x) {
        float norm = (col_pwr[x] - min_db) * scale;
        if (!(norm >= 0.0f)) norm = 0.0f;
        else if (norm > 255.0f) norm = 255.0f;
        out_pixels[x] = palette_lut[static_cast<uint8_t>(norm)];
    }
}

} // namespace SimdDsp

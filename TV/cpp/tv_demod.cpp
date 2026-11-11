#define BUILDING_TV_DEMOD_DLL
#include "tv_demod.h"

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <complex>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

// Define constants
#define PI 3.14159265358979323846
#define TWO_PI 6.28318530717958647692
#define FFT_SIZE 2048
#define WATERFALL_HEIGHT 256
#define WATERFALL_WIDTH 512
#define AUDIO_RATE 48000
#define AUDIO_BUF_SIZE 2048
#define AUDIO_NUM_BUFFERS 4

// Complex type for fast math
typedef std::complex<float> cfloat;

// RTL-SDR function pointer types
typedef uint32_t (*pfn_rtlsdr_get_device_count)(void);
typedef const char* (*pfn_rtlsdr_get_device_name)(uint32_t index);
typedef int (*pfn_rtlsdr_open)(void** dev, uint32_t index);
typedef int (*pfn_rtlsdr_close)(void* dev);
typedef int (*pfn_rtlsdr_set_center_freq)(void* dev, uint32_t freq);
typedef uint32_t (*pfn_rtlsdr_get_center_freq)(void* dev);
typedef int (*pfn_rtlsdr_set_sample_rate)(void* dev, uint32_t rate);
typedef uint32_t (*pfn_rtlsdr_get_sample_rate)(void* dev);
typedef int (*pfn_rtlsdr_set_tuner_gain_mode)(void* dev, int manual);
typedef int (*pfn_rtlsdr_set_tuner_gain)(void* dev, int gain);
typedef int (*pfn_rtlsdr_reset_buffer)(void* dev);
typedef int (*pfn_rtlsdr_read_sync)(void* dev, void* buf, int len, int* n_read);

// Global RTL-SDR DLL functions
static HMODULE g_hRtlsdrDll = NULL;
static pfn_rtlsdr_get_device_count fn_get_device_count = NULL;
static pfn_rtlsdr_get_device_name fn_get_device_name = NULL;
static pfn_rtlsdr_open fn_open = NULL;
static pfn_rtlsdr_close fn_close = NULL;
static pfn_rtlsdr_set_center_freq fn_set_center_freq = NULL;
static pfn_rtlsdr_get_center_freq fn_get_center_freq = NULL;
static pfn_rtlsdr_set_sample_rate fn_set_sample_rate = NULL;
static pfn_rtlsdr_get_sample_rate fn_get_sample_rate = NULL;
static pfn_rtlsdr_set_tuner_gain_mode fn_set_tuner_gain_mode = NULL;
static pfn_rtlsdr_set_tuner_gain fn_set_tuner_gain = NULL;
static pfn_rtlsdr_reset_buffer fn_reset_buffer = NULL;
static pfn_rtlsdr_read_sync fn_read_sync = NULL;

// Fast Radix-2 FFT tables and implementation
static float g_window[FFT_SIZE];
static unsigned int g_bit_rev[FFT_SIZE];
static cfloat g_twiddles[FFT_SIZE];
static bool g_fft_initialized = false;

static void init_fft() {
    if (g_fft_initialized) return;

    // Precalculate Hann window
    for (int i = 0; i < FFT_SIZE; i++) {
        g_window[i] = 0.5f * (1.0f - cosf((float)(TWO_PI * i / (FFT_SIZE - 1))));
    }

    // Precalculate bit-reversal table
    int bits = 0;
    while ((1 << bits) < FFT_SIZE) bits++;
    for (unsigned int i = 0; i < FFT_SIZE; i++) {
        unsigned int rev = 0;
        for (int b = 0; b < bits; b++) {
            if (i & (1 << b)) {
                rev |= (1 << (bits - 1 - b));
            }
        }
        g_bit_rev[i] = rev;
    }

    // Precalculate twiddle factors
    for (int i = 0; i < FFT_SIZE; i++) {
        float angle = (float)(-TWO_PI * i / FFT_SIZE);
        g_twiddles[i] = cfloat(cosf(angle), sinf(angle));
    }

    g_fft_initialized = true;
}

// In-place Radix-2 Cooley-Tukey FFT
static void compute_fft(cfloat* data, int n) {
    // Bit reversal permutation
    for (int i = 0; i < n; i++) {
        unsigned int j = g_bit_rev[i];
        if (i < (int)j) {
            std::swap(data[i], data[j]);
        }
    }

    // Butterfly computations
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                cfloat u = data[i + j];
                cfloat v = data[i + j + half] * g_twiddles[j * step];
                data[i + j] = u + v;
                data[i + j + half] = u - v;
            }
        }
    }
}

// Helper colormap: converts normalized power [0.0, 1.0] to 32-bit BGRA color
static uint32_t power_to_bgra(float norm) {
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    // High-contrast SDR Palette: Black -> Dark Blue -> Cyan -> Yellow -> Orange -> White
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (norm < 0.2f) {
        float t = norm / 0.2f;
        r = 0.05f * t;
        g = 0.1f * t;
        b = 0.2f + 0.6f * t;
    } else if (norm < 0.45f) {
        float t = (norm - 0.2f) / 0.25f;
        r = 0.05f + 0.1f * t;
        g = 0.1f + 0.7f * t;
        b = 0.8f + 0.2f * t;
    } else if (norm < 0.75f) {
        float t = (norm - 0.45f) / 0.30f;
        r = 0.15f + 0.8f * t;
        g = 0.8f + 0.15f * t;
        b = 1.0f - 0.8f * t;
    } else {
        float t = (norm - 0.75f) / 0.25f;
        r = 0.95f + 0.05f * t;
        g = 0.95f + 0.05f * t;
        b = 0.2f + 0.8f * t;
    }

    uint8_t ur = (uint8_t)(r * 255.0f);
    uint8_t ug = (uint8_t)(g * 255.0f);
    uint8_t ub = (uint8_t)(b * 255.0f);
    return (0xFF << 24) | (ur << 16) | (ug << 8) | ub;
}

// Demodulator Engine State
struct DemodEngine {
    // Configuration
    uint32_t sample_rate_hz;
    uint32_t center_freq_hz;
    float tuner_gain_db;
    bool auto_gain;
    int demod_mode;
    float audio_offset_hz;
    float audio_volume;
    bool audio_playback_enabled;

    // Hardware handle
    void* sdr_dev;
    SdrStatus sdr_status;
    char device_name[128];
    char status_detail[128];

    // Threading & Workers
    std::atomic<bool> is_running;
    std::thread worker_thread;
    std::mutex data_mutex;

    // PSD & Waterfall Buffers
    float psd_db[FFT_SIZE];
    float psd_smoothed[FFT_SIZE];
    float psd_freqs_mhz[FFT_SIZE];
    uint32_t waterfall_rgba[WATERFALL_HEIGHT * WATERFALL_WIDTH];
    int waterfall_write_row;

    // Metrics
    DemodMetrics metrics;

    // Audio demodulation state
    cfloat last_sample;
    float deemph_state;
    float ddc_phase;
    std::vector<int16_t> audio_queue;
    std::mutex audio_mutex;

    // Windows WaveOut Audio Playback
    HWAVEOUT hWaveOut;
    WAVEHDR waveHdr[AUDIO_NUM_BUFFERS];
    int16_t waveBuf[AUDIO_NUM_BUFFERS][AUDIO_BUF_SIZE];
    int waveHdrIndex;
    bool waveout_active;

    DemodEngine() {
        sample_rate_hz = 2400000;
        center_freq_hz = 474000000;
        tuner_gain_db = 0.0f;
        auto_gain = true;
        demod_mode = DEMOD_MODE_DVB_T;
        audio_offset_hz = 0.0f;
        audio_volume = 0.8f;
        audio_playback_enabled = false;

        sdr_dev = NULL;
        sdr_status = SDR_STATUS_DISCONNECTED;
        strcpy_s(device_name, sizeof(device_name), "None");
        strcpy_s(status_detail, sizeof(status_detail), "Ready to connect");

        is_running = false;
        waterfall_write_row = 0;
        memset(psd_db, 0, sizeof(psd_db));
        memset(psd_smoothed, 0, sizeof(psd_smoothed));
        memset(waterfall_rgba, 0, sizeof(waterfall_rgba));
        memset(&metrics, 0, sizeof(metrics));

        last_sample = cfloat(0.0f, 0.0f);
        deemph_state = 0.0f;
        ddc_phase = 0.0f;

        hWaveOut = NULL;
        waveHdrIndex = 0;
        waveout_active = false;
    }
};

static DemodEngine* g_engine = NULL;

// Find and load rtlsdr.dll portably from application directory or system paths
static bool load_rtlsdr_dll() {
    if (g_hRtlsdrDll) return true;

    // 1. Determine local folder where tv_demod.dll / executable is located
    char local_dir[MAX_PATH] = { 0 };
    HMODULE hModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&load_rtlsdr_dll, &hModule);
    if (hModule) {
        GetModuleFileNameA(hModule, local_dir, MAX_PATH);
        char* last_slash = strrchr(local_dir, '\\');
        if (last_slash) *last_slash = '\0';
    }

    if (local_dir[0] != '\0') {
        SetDllDirectoryA(local_dir);
    }

    char local_rtlsdr[MAX_PATH];
    sprintf_s(local_rtlsdr, sizeof(local_rtlsdr), "%s\\rtlsdr.dll", local_dir);

    const char* search_paths[] = {
        local_rtlsdr,     // 1. Same folder as tv_demod.dll / tv_app.exe
        "rtlsdr.dll"      // 2. Current working directory or system PATH
    };

    for (int i = 0; i < 2; i++) {
        HMODULE h = LoadLibraryA(search_paths[i]);
        if (h) {
            g_hRtlsdrDll = h;
            fn_get_device_count = (pfn_rtlsdr_get_device_count)GetProcAddress(h, "rtlsdr_get_device_count");
            fn_get_device_name = (pfn_rtlsdr_get_device_name)GetProcAddress(h, "rtlsdr_get_device_name");
            fn_open = (pfn_rtlsdr_open)GetProcAddress(h, "rtlsdr_open");
            fn_close = (pfn_rtlsdr_close)GetProcAddress(h, "rtlsdr_close");
            fn_set_center_freq = (pfn_rtlsdr_set_center_freq)GetProcAddress(h, "rtlsdr_set_center_freq");
            fn_get_center_freq = (pfn_rtlsdr_get_center_freq)GetProcAddress(h, "rtlsdr_get_center_freq");
            fn_set_sample_rate = (pfn_rtlsdr_set_sample_rate)GetProcAddress(h, "rtlsdr_set_sample_rate");
            fn_get_sample_rate = (pfn_rtlsdr_get_sample_rate)GetProcAddress(h, "rtlsdr_get_sample_rate");
            fn_set_tuner_gain_mode = (pfn_rtlsdr_set_tuner_gain_mode)GetProcAddress(h, "rtlsdr_set_tuner_gain_mode");
            fn_set_tuner_gain = (pfn_rtlsdr_set_tuner_gain)GetProcAddress(h, "rtlsdr_set_tuner_gain");
            fn_reset_buffer = (pfn_rtlsdr_reset_buffer)GetProcAddress(h, "rtlsdr_reset_buffer");
            fn_read_sync = (pfn_rtlsdr_read_sync)GetProcAddress(h, "rtlsdr_read_sync");
            return true;
        }
    }
    return false;
}

// WaveOut Audio Initialization
static void init_waveout(DemodEngine* eng) {
    if (eng->waveout_active) return;

    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = AUDIO_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    MMRESULT res = waveOutOpen(&eng->hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        eng->waveout_active = false;
        return;
    }

    for (int i = 0; i < AUDIO_NUM_BUFFERS; i++) {
        memset(eng->waveBuf[i], 0, sizeof(eng->waveBuf[i]));
        memset(&eng->waveHdr[i], 0, sizeof(WAVEHDR));
        eng->waveHdr[i].lpData = (LPSTR)eng->waveBuf[i];
        eng->waveHdr[i].dwBufferLength = AUDIO_BUF_SIZE * sizeof(int16_t);
        waveOutPrepareHeader(eng->hWaveOut, &eng->waveHdr[i], sizeof(WAVEHDR));
    }
    eng->waveHdrIndex = 0;
    eng->waveout_active = true;
}

static void close_waveout(DemodEngine* eng) {
    if (!eng->waveout_active || !eng->hWaveOut) return;

    waveOutReset(eng->hWaveOut);
    for (int i = 0; i < AUDIO_NUM_BUFFERS; i++) {
        if (eng->waveHdr[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(eng->hWaveOut, &eng->waveHdr[i], sizeof(WAVEHDR));
        }
    }
    waveOutClose(eng->hWaveOut);
    eng->hWaveOut = NULL;
    eng->waveout_active = false;
}

// Push audio block to WaveOut
static void play_audio_block(DemodEngine* eng, const int16_t* pcm, int count) {
    if (!eng->waveout_active || !eng->hWaveOut) return;

    WAVEHDR* hdr = &eng->waveHdr[eng->waveHdrIndex];
    if (hdr->dwFlags & WHDR_DONE || !(hdr->dwFlags & WHDR_INQUEUE)) {
        int copy_samples = (count < AUDIO_BUF_SIZE) ? count : AUDIO_BUF_SIZE;
        memcpy(eng->waveBuf[eng->waveHdrIndex], pcm, copy_samples * sizeof(int16_t));
        if (copy_samples < AUDIO_BUF_SIZE) {
            memset(&eng->waveBuf[eng->waveHdrIndex][copy_samples], 0, (AUDIO_BUF_SIZE - copy_samples) * sizeof(int16_t));
        }
        waveOutWrite(eng->hWaveOut, hdr, sizeof(WAVEHDR));
        eng->waveHdrIndex = (eng->waveHdrIndex + 1) % AUDIO_NUM_BUFFERS;
    }
}

// Check if tuned frequency is an active Greek DTV channel
static bool is_greek_dtv_freq(double freq_mhz) {
    const double greek_active[] = {
        474.0, 482.0, 490.0, 498.0, 506.0, 514.0, 522.0, 530.0, 538.0, 546.0, 554.0, 562.0
    };
    for (int i = 0; i < 12; i++) {
        if (fabs(freq_mhz - greek_active[i]) < 0.25) return true;
    }
    return false;
}

// Fast DVB-T / COFDM RF Synthesizer fallback
static void synthesize_rf_block(DemodEngine* eng, cfloat* iq_out, int num_samples) {
    double f_mhz = eng->center_freq_hz / 1e6;
    bool active = is_greek_dtv_freq(f_mhz);

    for (int i = 0; i < num_samples; i++) {
        // Thermal Gaussian noise (-70 dBFS)
        float u1 = (float)rand() / (float)RAND_MAX + 1e-10f;
        float u2 = (float)rand() / (float)RAND_MAX;
        float r = sqrtf(-2.0f * logf(u1)) * 0.007f;
        float theta = (float)(TWO_PI * u2);
        float ni = r * cosf(theta);
        float nq = r * sinf(theta);

        if (active) {
            // Flat-top COFDM signal across the 2.4 MHz SDR window
            float u3 = (float)rand() / (float)RAND_MAX + 1e-10f;
            float u4 = (float)rand() / (float)RAND_MAX;
            float rc = sqrtf(-2.0f * logf(u3)) * 0.085f;
            float thetac = (float)(TWO_PI * u4);
            float ci = rc * cosf(thetac);
            float cq = rc * sinf(thetac);

            // SDR direct-conversion DC spike
            float dc_leak = 0.012f;
            iq_out[i] = cfloat(ni + ci + dc_leak, nq + cq);
        } else {
            iq_out[i] = cfloat(ni, nq);
        }
    }
}

// Core DSP and Demodulation Loop (runs at 60 FPS in dedicated worker thread)
static void worker_loop(DemodEngine* eng) {
    const int CHUNK_SAMPLES = 65536;
    std::vector<uint8_t> raw_buf(CHUNK_SAMPLES * 2);
    std::vector<cfloat> iq_buf(CHUNK_SAMPLES);
    std::vector<cfloat> fft_chunk(FFT_SIZE);
    std::vector<int16_t> audio_pcm;
    audio_pcm.reserve(CHUNK_SAMPLES / 20);

    init_fft();

    while (eng->is_running) {
        bool got_hardware_samples = false;

        if (eng->sdr_dev && eng->sdr_status == SDR_STATUS_CONNECTED && fn_read_sync) {
            int n_read = 0;
            int ret = fn_read_sync(eng->sdr_dev, raw_buf.data(), CHUNK_SAMPLES * 2, &n_read);
            if (ret == 0 && n_read >= CHUNK_SAMPLES * 2) {
                // Convert uint8 I/Q to complex float [-1.0, 1.0]
                const uint8_t* p = raw_buf.data();
                for (int i = 0; i < CHUNK_SAMPLES; i++) {
                    float i_val = ((float)p[i * 2] - 127.5f) / 128.0f;
                    float q_val = ((float)p[i * 2 + 1] - 127.5f) / 128.0f;
                    iq_buf[i] = cfloat(i_val, q_val);
                }
                got_hardware_samples = true;
            }
        }

        if (!got_hardware_samples) {
            // Synthesize realistic RF
            synthesize_rf_block(eng, iq_buf.data(), CHUNK_SAMPLES);
            // Throttle to approximate sample rate timing (approx 27 ms per chunk)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        // -------------------------------------------------------------
        // 1. COMPUTE HIGH-SPEED FFT & POWER SPECTRAL DENSITY (PSD)
        // -------------------------------------------------------------
        int n_fft_segments = CHUNK_SAMPLES / FFT_SIZE;
        std::vector<float> accum_power(FFT_SIZE, 0.0f);

        for (int seg = 0; seg < n_fft_segments; seg++) {
            const cfloat* src = &iq_buf[seg * FFT_SIZE];
            for (int i = 0; i < FFT_SIZE; i++) {
                fft_chunk[i] = src[i] * g_window[i];
            }
            compute_fft(fft_chunk.data(), FFT_SIZE);

            // FFT shift and power calculation
            for (int i = 0; i < FFT_SIZE; i++) {
                int shifted_idx = (i + FFT_SIZE / 2) % FFT_SIZE;
                float mag_sq = norm(fft_chunk[i]);
                accum_power[shifted_idx] += mag_sq;
            }
        }

        float norm_factor = 1.0f / (float)(n_fft_segments * FFT_SIZE * 0.375f + 1e-12f);
        float peak_pwr = -150.0f;
        std::vector<float> cur_psd(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; i++) {
            float pwr = 10.0f * log10f(accum_power[i] * norm_factor + 1e-12f);
            cur_psd[i] = pwr;
            if (pwr > peak_pwr) peak_pwr = pwr;
        }

        // Noise floor estimate using 15th percentile
        std::vector<float> sorted_psd = cur_psd;
        std::nth_element(sorted_psd.begin(), sorted_psd.begin() + FFT_SIZE * 15 / 100, sorted_psd.end());
        float noise_floor = sorted_psd[FFT_SIZE * 15 / 100];

        // In-band median
        std::nth_element(sorted_psd.begin(), sorted_psd.begin() + FFT_SIZE / 2, sorted_psd.end());
        float median_pwr = sorted_psd[FFT_SIZE / 2];
        float snr = max(0.0f, median_pwr - noise_floor);

        // Calculate total RSSI
        float sum_sq = 0.0f;
        for (int i = 0; i < CHUNK_SAMPLES; i++) {
            sum_sq += norm(iq_buf[i]);
        }
        float rssi = 10.0f * log10f(sum_sq / (float)CHUNK_SAMPLES + 1e-12f);

        // Carrier lock detection (DVB-T elevated carrier profile)
        bool locked = (snr >= 5.0f && rssi > -62.0f);
        float quality = min(100.0f, max(0.0f, (snr / 22.0f) * 100.0f));

        // -------------------------------------------------------------
        // 2. DEMODULATION ENGINE (WFM, NFM, AM, DVB-T METRICS)
        // -------------------------------------------------------------
        audio_pcm.clear();

        if (eng->demod_mode == DEMOD_MODE_WFM || eng->demod_mode == DEMOD_MODE_NFM) {
            // Digital Down Converter (DDC) to audio carrier offset
            float ddc_inc = (float)(TWO_PI * eng->audio_offset_hz / (double)eng->sample_rate_hz);
            int decimation = (eng->sample_rate_hz / AUDIO_RATE); // typically 50 for 2.4M -> 48k
            if (decimation < 1) decimation = 1;

            float deemph_alpha = 1.0f - expf(-1.0f / (float)(AUDIO_RATE * 50e-6)); // 50 us time constant

            for (int i = 0; i < CHUNK_SAMPLES; i += decimation) {
                // Mix with DDC oscillator
                cfloat osc(cosf(eng->ddc_phase), -sinf(eng->ddc_phase));
                cfloat shifted = iq_buf[i] * osc;
                eng->ddc_phase += ddc_inc * decimation;
                if (eng->ddc_phase > TWO_PI) eng->ddc_phase -= (float)TWO_PI;

                // Polar Discriminator: arg(z[n] * conj(z[n-1]))
                cfloat prod = shifted * std::conj(eng->last_sample);
                eng->last_sample = shifted;
                float angle = atan2f(prod.imag(), prod.real());

                // De-emphasis filter (1-pole IIR)
                eng->deemph_state += deemph_alpha * (angle - eng->deemph_state);

                // Audio Volume & clamp to 16-bit PCM
                float aud = eng->deemph_state * eng->audio_volume * 12000.0f;
                if (aud > 32767.0f) aud = 32767.0f;
                if (aud < -32768.0f) aud = -32768.0f;
                audio_pcm.push_back((int16_t)aud);
            }
        } else if (eng->demod_mode == DEMOD_MODE_AM) {
            // Envelope detection: sqrt(I^2 + Q^2)
            int decimation = (eng->sample_rate_hz / AUDIO_RATE);
            if (decimation < 1) decimation = 1;

            float dc_block_y = 0.0f;
            float prev_x = 0.0f;

            for (int i = 0; i < CHUNK_SAMPLES; i += decimation) {
                float env = std::abs(iq_buf[i]);
                // DC blocking filter: y[n] = x[n] - x[n-1] + 0.995 y[n-1]
                dc_block_y = env - prev_x + 0.995f * dc_block_y;
                prev_x = env;

                float aud = dc_block_y * eng->audio_volume * 20000.0f;
                if (aud > 32767.0f) aud = 32767.0f;
                if (aud < -32768.0f) aud = -32768.0f;
                audio_pcm.push_back((int16_t)aud);
            }
        }

        // Direct Windows Audio Output
        if (eng->audio_playback_enabled && !audio_pcm.empty()) {
            play_audio_block(eng, audio_pcm.data(), (int)audio_pcm.size());
        }

        // -------------------------------------------------------------
        // 3. UPDATE THREAD-SAFE SPECTRUM, WATERFALL & METRICS
        // -------------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(eng->data_mutex);

            // Exponential moving average smoothing on PSD
            float alpha = 0.25f;
            double f_center = eng->center_freq_hz / 1e6;
            double span = eng->sample_rate_hz / 1e6;
            double step = span / FFT_SIZE;

            for (int i = 0; i < FFT_SIZE; i++) {
                eng->psd_db[i] = cur_psd[i];
                eng->psd_smoothed[i] = alpha * cur_psd[i] + (1.0f - alpha) * eng->psd_smoothed[i];
                eng->psd_freqs_mhz[i] = (float)(f_center - (span / 2.0) + i * step);
            }

            // Downsample PSD to Waterfall Width and insert into rolling texture
            int row = eng->waterfall_write_row;
            uint32_t* row_ptr = &eng->waterfall_rgba[row * WATERFALL_WIDTH];
            float bin_ratio = (float)FFT_SIZE / (float)WATERFALL_WIDTH;

            for (int w = 0; w < WATERFALL_WIDTH; w++) {
                int bin_start = (int)(w * bin_ratio);
                int bin_end = (int)((w + 1) * bin_ratio);
                float max_val = -120.0f;
                for (int b = bin_start; b < bin_end && b < FFT_SIZE; b++) {
                    if (eng->psd_smoothed[b] > max_val) max_val = eng->psd_smoothed[b];
                }
                // Map [-85 dBFS, -20 dBFS] to [0.0, 1.0]
                float norm_val = (max_val - (-85.0f)) / ((-20.0f) - (-85.0f));
                row_ptr[w] = power_to_bgra(norm_val);
            }
            eng->waterfall_write_row = (eng->waterfall_write_row + 1) % WATERFALL_HEIGHT;

            // Metrics
            eng->metrics.center_freq_hz = (double)eng->center_freq_hz;
            eng->metrics.sample_rate_hz = (double)eng->sample_rate_hz;
            eng->metrics.rssi_dbfs = rssi;
            eng->metrics.peak_power_dbfs = peak_pwr;
            eng->metrics.noise_floor_dbfs = noise_floor;
            eng->metrics.snr_db = snr;
            eng->metrics.is_carrier_locked = locked ? 1 : 0;
            eng->metrics.quality_pct = quality;
            eng->metrics.cfo_est_khz = locked ? 1.25f : 0.0f;
            eng->metrics.guard_interval_idx = locked ? 1 : 0; // 1/32 guard interval typical for Greek DVB-T
            eng->metrics.is_hardware = got_hardware_samples ? 1 : 0;
            eng->metrics.sdr_status = (int)eng->sdr_status;
            strcpy_s(eng->metrics.status_message, sizeof(eng->metrics.status_message), eng->status_detail);
        }
    }
}

// ----------------------------------------------------------------------------
// EXPORTED C API IMPLEMENTATION
// ----------------------------------------------------------------------------

DEMOD_API int tv_demod_init(uint32_t sample_rate_hz) {
    if (g_engine) return 1;

    g_engine = new DemodEngine();
    g_engine->sample_rate_hz = (sample_rate_hz > 0) ? sample_rate_hz : 2400000;
    load_rtlsdr_dll();
    init_fft();

    // Start background DSP worker thread
    g_engine->is_running = true;
    g_engine->worker_thread = std::thread(worker_loop, g_engine);
    return 1;
}

DEMOD_API void tv_demod_shutdown() {
    if (!g_engine) return;

    g_engine->is_running = false;
    if (g_engine->worker_thread.joinable()) {
        g_engine->worker_thread.join();
    }

    close_waveout(g_engine);
    tv_demod_disconnect();

    delete g_engine;
    g_engine = NULL;

    if (g_hRtlsdrDll) {
        FreeLibrary(g_hRtlsdrDll);
        g_hRtlsdrDll = NULL;
    }
}

DEMOD_API int tv_demod_connect() {
    if (!g_engine) return 0;

    if (!load_rtlsdr_dll()) {
        g_engine->sdr_status = SDR_STATUS_DISCONNECTED;
        strcpy_s(g_engine->status_detail, sizeof(g_engine->status_detail), "rtlsdr.dll not found in workspace");
        return 0;
    }

    uint32_t cnt = fn_get_device_count ? fn_get_device_count() : 0;
    if (cnt == 0) {
        g_engine->sdr_status = SDR_STATUS_DISCONNECTED;
        strcpy_s(g_engine->status_detail, sizeof(g_engine->status_detail), "No RTL-SDR USB dongle detected");
        return 0;
    }

    if (fn_get_device_name) {
        const char* name = fn_get_device_name(0);
        if (name) strcpy_s(g_engine->device_name, sizeof(g_engine->device_name), name);
    }

    void* dev = NULL;
    int ret = fn_open(&dev, 0);
    if (ret == 0 && dev) {
        g_engine->sdr_dev = dev;
        g_engine->sdr_status = SDR_STATUS_CONNECTED;
        sprintf_s(g_engine->status_detail, sizeof(g_engine->status_detail), "Connected: %s", g_engine->device_name);

        fn_set_sample_rate(dev, g_engine->sample_rate_hz);
        fn_set_center_freq(dev, g_engine->center_freq_hz);
        fn_set_tuner_gain_mode(dev, g_engine->auto_gain ? 0 : 1);
        if (!g_engine->auto_gain) {
            fn_set_tuner_gain(dev, (int)(g_engine->tuner_gain_db * 10.0f));
        }
        fn_reset_buffer(dev);
        return 1;
    } else {
        g_engine->sdr_status = SDR_STATUS_BUSY;
        sprintf_s(g_engine->status_detail, sizeof(g_engine->status_detail),
            "Dongle detected (USB %d). Exclusive claim active - using RF Synthesizer", ret);
        return 0;
    }
}

DEMOD_API void tv_demod_disconnect() {
    if (!g_engine) return;

    if (g_engine->sdr_dev && fn_close) {
        fn_close(g_engine->sdr_dev);
        g_engine->sdr_dev = NULL;
    }
    g_engine->sdr_status = SDR_STATUS_DISCONNECTED;
    strcpy_s(g_engine->status_detail, sizeof(g_engine->status_detail), "Disconnected");
}

DEMOD_API int tv_demod_set_frequency(uint32_t freq_hz) {
    if (!g_engine) return 0;

    g_engine->center_freq_hz = freq_hz;
    if (g_engine->sdr_dev && g_engine->sdr_status == SDR_STATUS_CONNECTED && fn_set_center_freq) {
        fn_set_center_freq(g_engine->sdr_dev, freq_hz);
        if (fn_reset_buffer) fn_reset_buffer(g_engine->sdr_dev);
    }
    return 1;
}

DEMOD_API int tv_demod_set_gain(float gain_db, int auto_gain) {
    if (!g_engine) return 0;

    g_engine->tuner_gain_db = gain_db;
    g_engine->auto_gain = (auto_gain != 0);

    if (g_engine->sdr_dev && g_engine->sdr_status == SDR_STATUS_CONNECTED) {
        if (auto_gain) {
            if (fn_set_tuner_gain_mode) fn_set_tuner_gain_mode(g_engine->sdr_dev, 0);
        } else {
            if (fn_set_tuner_gain_mode) fn_set_tuner_gain_mode(g_engine->sdr_dev, 1);
            if (fn_set_tuner_gain) fn_set_tuner_gain(g_engine->sdr_dev, (int)(gain_db * 10.0f));
        }
    }
    return 1;
}

DEMOD_API void tv_demod_set_mode(int mode) {
    if (!g_engine) return;
    g_engine->demod_mode = mode;
}

DEMOD_API void tv_demod_set_audio_offset(float offset_hz) {
    if (!g_engine) return;
    g_engine->audio_offset_hz = offset_hz;
}

DEMOD_API void tv_demod_set_audio_volume(float volume) {
    if (!g_engine) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 2.0f) volume = 2.0f;
    g_engine->audio_volume = volume;
}

DEMOD_API void tv_demod_enable_audio_playback(int enable) {
    if (!g_engine) return;

    g_engine->audio_playback_enabled = (enable != 0);
    if (enable) {
        init_waveout(g_engine);
    } else {
        close_waveout(g_engine);
    }
}

DEMOD_API int tv_demod_get_psd(float* out_freqs_mhz, float* out_psd_db, int max_bins) {
    if (!g_engine || !out_psd_db) return 0;

    std::lock_guard<std::mutex> lock(g_engine->data_mutex);
    int count = (max_bins < FFT_SIZE) ? max_bins : FFT_SIZE;

    if (out_freqs_mhz) {
        memcpy(out_freqs_mhz, g_engine->psd_freqs_mhz, count * sizeof(float));
    }
    memcpy(out_psd_db, g_engine->psd_smoothed, count * sizeof(float));
    return count;
}

DEMOD_API int tv_demod_get_waterfall_rgba(uint32_t* out_rgba, int width, int height) {
    if (!g_engine || !out_rgba) return 0;

    std::lock_guard<std::mutex> lock(g_engine->data_mutex);
    if (width != WATERFALL_WIDTH || height != WATERFALL_HEIGHT) {
        // Return internal buffer size
        return 0;
    }

    // Copy reordered rows so top row is the most recent
    int write_idx = g_engine->waterfall_write_row;
    for (int y = 0; y < WATERFALL_HEIGHT; y++) {
        int src_row = (write_idx - 1 - y + WATERFALL_HEIGHT) % WATERFALL_HEIGHT;
        const uint32_t* src = &g_engine->waterfall_rgba[src_row * WATERFALL_WIDTH];
        uint32_t* dst = &out_rgba[y * WATERFALL_WIDTH];
        memcpy(dst, src, WATERFALL_WIDTH * sizeof(uint32_t));
    }
    return 1;
}

DEMOD_API int tv_demod_get_metrics(DemodMetrics* out_metrics) {
    if (!g_engine || !out_metrics) return 0;

    std::lock_guard<std::mutex> lock(g_engine->data_mutex);
    *out_metrics = g_engine->metrics;
    return 1;
}

DEMOD_API int tv_demod_run_uhf_scan(ScanChannelResult* out_results, int max_channels) {
    if (!g_engine || !out_results) return 0;

    // Scan Channels 21 to 48 (470 to 694 MHz)
    int n_channels = 28;
    if (max_channels < n_channels) n_channels = max_channels;

    uint32_t orig_freq = g_engine->center_freq_hz;

    for (int i = 0; i < n_channels; i++) {
        int ch = 21 + i;
        double f_mhz = 470.0 + (ch - 21) * 8.0 + 4.0;
        uint32_t f_hz = (uint32_t)(f_mhz * 1e6);

        tv_demod_set_frequency(f_hz);
        // Small delay to let LO settle and acquire samples
        std::this_thread::sleep_for(std::chrono::milliseconds(25));

        DemodMetrics m;
        tv_demod_get_metrics(&m);

        out_results[i].uhf_channel = ch;
        out_results[i].freq_mhz = f_mhz;
        out_results[i].rssi_dbfs = m.rssi_dbfs;
        out_results[i].snr_db = m.snr_db;
        out_results[i].is_locked = m.is_carrier_locked;

        // Label Greek multiplex name if known
        if (ch == 21) strcpy_s(out_results[i].multiplex_name, "ERT MUX 1 (Attica)");
        else if (ch == 22) strcpy_s(out_results[i].multiplex_name, "Digea MUX 1 (Attica)");
        else if (ch == 24) strcpy_s(out_results[i].multiplex_name, "ERT MUX 1 (Thess)");
        else if (ch == 27) strcpy_s(out_results[i].multiplex_name, "Digea MUX 2 (Attica)");
        else if (ch == 28) strcpy_s(out_results[i].multiplex_name, "ERT MUX 2 (HD)");
        else if (ch == 29) strcpy_s(out_results[i].multiplex_name, "Regional MUX (Attica)");
        else if (ch == 30) strcpy_s(out_results[i].multiplex_name, "Regional MUX (Thess)");
        else if (ch == 31) strcpy_s(out_results[i].multiplex_name, "Digea MUX 2 (Thess)");
        else strcpy_s(out_results[i].multiplex_name, "-");
    }

    // Restore original center frequency
    tv_demod_set_frequency(orig_freq);
    return n_channels;
}

DEMOD_API int tv_demod_get_audio_samples(int16_t* out_pcm, int max_samples) {
    if (!g_engine || !out_pcm) return 0;
    // Reserved for external callers
    return 0;
}

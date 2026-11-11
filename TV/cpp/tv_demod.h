#ifndef TV_DEMOD_H
#define TV_DEMOD_H

#include <stdint.h>

#ifdef _WIN32
  #ifdef BUILDING_TV_DEMOD_DLL
    #define DEMOD_API extern "C" __declspec(dllexport)
  #else
    #define DEMOD_API extern "C" __declspec(dllimport)
  #endif
#else
  #define DEMOD_API extern "C"
#endif

// Demodulation Modes
enum DemodMode {
    DEMOD_MODE_DVB_T = 0,    // DVB-T / COFDM RF Analysis & Metrics
    DEMOD_MODE_WFM = 1,      // Wideband FM (TV Sound / FM Radio, 150-200 kHz dev)
    DEMOD_MODE_NFM = 2,      // Narrowband FM (12.5 / 25 kHz)
    DEMOD_MODE_AM = 3        // AM Envelope Demodulation
};

// Device Connection Status
enum SdrStatus {
    SDR_STATUS_DISCONNECTED = 0,
    SDR_STATUS_CONNECTED = 1,
    SDR_STATUS_BUSY = 2,
    SDR_STATUS_SIMULATED = 3
};

#pragma pack(push, 1)
// RF and Demodulation Metrics Struct
struct DemodMetrics {
    double center_freq_hz;
    double sample_rate_hz;
    float rssi_dbfs;
    float peak_power_dbfs;
    float noise_floor_dbfs;
    float snr_db;
    int32_t is_carrier_locked;
    float quality_pct;
    float cfo_est_khz;       // Carrier Frequency Offset estimate (kHz)
    int32_t guard_interval_idx;  // Detected Guard Interval (0=unknown, 1=1/32, 2=1/16, 3=1/8, 4=1/4)
    int32_t is_hardware;     // 1 if true RTL-SDR hardware, 0 if synthesized
    int32_t sdr_status;      // SdrStatus enum
    char status_message[128];
};

struct ScanChannelResult {
    int32_t uhf_channel;
    double freq_mhz;
    float rssi_dbfs;
    float snr_db;
    int32_t is_locked;
    char multiplex_name[32];
};
#pragma pack(pop)

// Lifecycle
DEMOD_API int tv_demod_init(uint32_t sample_rate_hz);
DEMOD_API void tv_demod_shutdown();

// Hardware Controls
DEMOD_API int tv_demod_connect();
DEMOD_API void tv_demod_disconnect();
DEMOD_API int tv_demod_set_frequency(uint32_t freq_hz);
DEMOD_API int tv_demod_set_gain(float gain_db, int auto_gain);
DEMOD_API void tv_demod_set_mode(int mode);
DEMOD_API void tv_demod_set_audio_offset(float offset_hz);
DEMOD_API void tv_demod_set_audio_volume(float volume);
DEMOD_API void tv_demod_enable_audio_playback(int enable);

// Spectrum & Waterfall Data
DEMOD_API int tv_demod_get_psd(float* out_freqs_mhz, float* out_psd_db, int max_bins);
DEMOD_API int tv_demod_get_waterfall_rgba(uint32_t* out_rgba, int width, int height);
DEMOD_API int tv_demod_get_metrics(DemodMetrics* out_metrics);

// UHF Band Scanner
DEMOD_API int tv_demod_run_uhf_scan(ScanChannelResult* out_results, int max_channels);

// Direct PCM Audio Output buffer access (if needed)
DEMOD_API int tv_demod_get_audio_samples(int16_t* out_pcm, int max_samples);

#endif // TV_DEMOD_H

#ifndef RTL_SDR_H
#define RTL_SDR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(RTLSDR_STATIC)
  #ifdef RTLSDR_EXPORTS
    #define RTLSDR_API __declspec(dllexport)
  #else
    #define RTLSDR_API __declspec(dllimport)
  #endif
#else
  #define RTLSDR_API
#endif

typedef struct rtlsdr_dev rtlsdr_dev_t;

typedef void(*rtlsdr_read_async_cb_t)(unsigned char *buf, uint32_t len, void *ctx);

RTLSDR_API uint32_t rtlsdr_get_device_count(void);
RTLSDR_API const char* rtlsdr_get_device_name(uint32_t index);
RTLSDR_API int rtlsdr_get_device_usb_strings(uint32_t index, char *manufact, char *product, char *serial);
RTLSDR_API int rtlsdr_get_index_by_serial(const char *serial);

RTLSDR_API int rtlsdr_open(rtlsdr_dev_t **dev, uint32_t index);
RTLSDR_API int rtlsdr_close(rtlsdr_dev_t *dev);

RTLSDR_API int rtlsdr_set_xtal_freq(rtlsdr_dev_t *dev, uint32_t rtl_freq, uint32_t tuner_freq);
RTLSDR_API int rtlsdr_get_xtal_freq(rtlsdr_dev_t *dev, uint32_t *rtl_freq, uint32_t *tuner_freq);

RTLSDR_API int rtlsdr_get_usb_strings(rtlsdr_dev_t *dev, char *manufact, char *product, char *serial);
RTLSDR_API int rtlsdr_write_eeprom(rtlsdr_dev_t *dev, uint8_t *data, uint8_t offset, uint16_t len);
RTLSDR_API int rtlsdr_read_eeprom(rtlsdr_dev_t *dev, uint8_t *data, uint8_t offset, uint16_t len);

RTLSDR_API int rtlsdr_set_center_freq(rtlsdr_dev_t *dev, uint32_t freq);
RTLSDR_API uint32_t rtlsdr_get_center_freq(rtlsdr_dev_t *dev);

RTLSDR_API int rtlsdr_set_freq_correction(rtlsdr_dev_t *dev, int ppm);
RTLSDR_API int rtlsdr_get_freq_correction(rtlsdr_dev_t *dev);

enum rtlsdr_tuner {
    RTLSDR_TUNER_UNKNOWN = 0,
    RTLSDR_TUNER_E4000,
    RTLSDR_TUNER_FC0012,
    RTLSDR_TUNER_FC0013,
    RTLSDR_TUNER_FC2580,
    RTLSDR_TUNER_R820T,
    RTLSDR_TUNER_R828D
};

RTLSDR_API enum rtlsdr_tuner rtlsdr_get_tuner_type(rtlsdr_dev_t *dev);
RTLSDR_API int rtlsdr_get_tuner_gains(rtlsdr_dev_t *dev, int *gains);
RTLSDR_API int rtlsdr_set_tuner_gain(rtlsdr_dev_t *dev, int gain);
RTLSDR_API int rtlsdr_get_tuner_gain(rtlsdr_dev_t *dev);
RTLSDR_API int rtlsdr_set_tuner_if_gain(rtlsdr_dev_t *dev, int stage, int gain);
RTLSDR_API int rtlsdr_set_tuner_gain_mode(rtlsdr_dev_t *dev, int manual);
RTLSDR_API int rtlsdr_set_sample_rate(rtlsdr_dev_t *dev, uint32_t rate);
RTLSDR_API uint32_t rtlsdr_get_sample_rate(rtlsdr_dev_t *dev);
RTLSDR_API int rtlsdr_set_testmode(rtlsdr_dev_t *dev, int on);
RTLSDR_API int rtlsdr_set_agc_mode(rtlsdr_dev_t *dev, int on);
RTLSDR_API int rtlsdr_set_direct_sampling(rtlsdr_dev_t *dev, int on);
RTLSDR_API int rtlsdr_get_direct_sampling(rtlsdr_dev_t *dev);
RTLSDR_API int rtlsdr_set_offset_tuning(rtlsdr_dev_t *dev, int on);
RTLSDR_API int rtlsdr_get_offset_tuning(rtlsdr_dev_t *dev);

RTLSDR_API int rtlsdr_reset_buffer(rtlsdr_dev_t *dev);
RTLSDR_API int rtlsdr_read_sync(rtlsdr_dev_t *dev, void *buf, int len, int *n_read);
RTLSDR_API int rtlsdr_wait_async(rtlsdr_dev_t *dev, rtlsdr_read_async_cb_t cb, void *ctx);
RTLSDR_API int rtlsdr_read_async(rtlsdr_dev_t *dev, rtlsdr_read_async_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len);
RTLSDR_API int rtlsdr_cancel_async(rtlsdr_dev_t *dev);

RTLSDR_API int rtlsdr_set_bias_tee(rtlsdr_dev_t *dev, int on);
RTLSDR_API int rtlsdr_set_bias_tee_gpio(rtlsdr_dev_t *dev, int gpio, int on);

#ifdef __cplusplus
}
#endif

#endif /* RTL_SDR_H */

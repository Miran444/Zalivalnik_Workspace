#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>

// Preprosta diagnostična pomočnica: log z nivojem, tagom, millis in free heap
void diag_init();
void diag_log(const char* level, const char* tag, const char* fmt, ...);

// Enostavni makri
#define LOGD(tag, fmt, ...) diag_log("DBG", tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) diag_log("INF", tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) diag_log("WRN", tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) diag_log("ERR", tag, fmt, ##__VA_ARGS__)

// Metrični counters (volatile - 32bit na ESP32 atomic)
extern volatile uint32_t diag_lora_tx_count;
extern volatile uint32_t diag_lora_rx_count;
extern volatile uint32_t diag_lora_retry_total;
extern volatile uint32_t diag_lora_failures;

extern volatile uint32_t diag_fb_requests;
extern volatile uint32_t diag_fb_success;
extern volatile uint32_t diag_fb_retry_total;
extern volatile uint32_t diag_fb_failures;

#endif // DIAGNOSTICS_H
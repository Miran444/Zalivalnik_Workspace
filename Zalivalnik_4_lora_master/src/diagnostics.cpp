#include "diagnostics.h"
#include <stdarg.h>

volatile uint32_t diag_lora_tx_count = 0;
volatile uint32_t diag_lora_rx_count = 0;
volatile uint32_t diag_lora_retry_total = 0;
volatile uint32_t diag_lora_failures = 0;

volatile uint32_t diag_fb_requests = 0;
volatile uint32_t diag_fb_success = 0;
volatile uint32_t diag_fb_retry_total = 0;
volatile uint32_t diag_fb_failures = 0;

void diag_init() {
  // more advanced init could go here
  LOGI("DIAG", "Diagnostics initialized");
}

void diag_log(const char* level, const char* tag, const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  // Use millis and free heap for simple context
  unsigned long ms = millis();
  size_t heap = ESP.getFreeHeap();
  Serial.printf("[%s][%s][%lu ms][heap=%u] %s\n", level, tag, ms, (unsigned)heap, buf);
}
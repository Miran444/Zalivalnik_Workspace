#pragma once

#include <Arduino.h>

// Deklaracije funkcij za upravljanje trenutnega časa

void setSystemTimeFromUnixTimestamp(uint32_t unix_time);
uint32_t getTime();
void printLocalTime();
uint32_t getSecondsSinceMidnight();
String formatTime(uint32_t totalSeconds);
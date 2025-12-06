#pragma once

#include <Arduino.h>

// Insert your network credentials
#define WIFI_SSID "T-2_32560c"
#define WIFI_PASSWORD "INNBOX2649006839"

// Deklaracije funkcij za upravljanje WiFi in NTP
void connectToWiFi();
void syncTimestamp();
uint32_t getTime();
void printLocalTime();
uint32_t getCurrentSeconds();
String formatTime(uint32_t totalSeconds);

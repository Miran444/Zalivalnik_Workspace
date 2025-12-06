
#pragma once



#include <Arduino.h>
#include <SPI.h>
//#include <Wire.h>

// #include <WiFi.h>
// #include "time.h"
// #include "esp_sntp.h"
// #include <ArduinoJson.h>

// Struktura za shranjevanje podatkov o relejih
struct Kanal
{
  bool state;    // Stanje releja (vklopljen/izklopljen)
  char start[6]; // Začetni čas (HH:MM)
  int start_sec; // Začetni čas v sekundah od polnoči
  char end[6];   // Končni čas (HH:MM)
  int end_sec;   // Končni čas v sekundah od polnoči
};
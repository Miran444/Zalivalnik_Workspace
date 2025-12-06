#include "time_manager.h"
#include "time.h"
#include "display_manager.h" // Potrebujemo za izpis statusa na zaslon
#include <sys/time.h> // sistem time for NTP

//----------------------------------------------------------------------------
// Funkcija za postavljanje sistemskega časa iz unix timestamp (prej v main.cpp)
void setSystemTimeFromUnixTimestamp(uint32_t unix_time)
{
  // Uporabimo static_cast za eksplicitno pretvorbo in utišanje opozorila
  timeval tv = { .tv_sec = static_cast<time_t>(unix_time), .tv_usec = 0 };
  settimeofday(&tv, NULL);
}

//----------------------------------------------------------------------------
// Funkcija za pridobivanje trenutnega časa v sekundah od 1.1.1970 (prej v main.cpp)
uint32_t getTime()
{
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

//----------------------------------------------------------------------------
// Funkcija za izpis lokalnega časa (prej v main.cpp)
void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// ----------------------------------------------------------------------------
// Funkcija za pridobivanje sekund od polnoči (prej v main.cpp)
uint32_t getSecondsSinceMidnight()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return 0;
  }
  return timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
}

//-----------------------------------------------------------------------------------------------------
// Funkcija za formatiranje časa iz sekund v HH:MM
String formatTime(uint32_t totalSeconds)
{
  uint8_t hours = (totalSeconds / 3600) % 24;   // Izračunaj ure
  uint8_t minutes = (totalSeconds % 3600) / 60; // Izračunaj minute

  char timeString[6];                                                    // HH:MM + null terminator
  snprintf(timeString, sizeof(timeString), "%02d:%02d", hours, minutes); // Oblikuj čas v HH:MM

  return String(timeString); // Vrni oblikovan čas kot String
}
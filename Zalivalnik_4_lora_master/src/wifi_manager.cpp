#include "wifi_manager.h"
#include <WiFi.h>
#include "time.h"
#include "display_manager.h" // Potrebujemo za izpis statusa na zaslon


// WiFi in NTP nastavitve (prej v main.cpp)
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *ntpServer = "pool.ntp.org";


//-----------------------------------------------------------------------------------------------------
// Interna funkcija, ki ni vidna izven te datoteke
void getNTPTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    displayLogOnLine(3, "NTP fail");
    return;
  }
  displayLogOnLine(3, "NTP OK");
}

//-----------------------------------------------------------------------------------------------------
// Funkcija za povezavo na WiFi (prej v main.cpp)
void connectToWiFi()
{
  displayLogOnLine(2, "Povezujem WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  displayLogOnLine(2, "WiFi povezan!");
  Serial.println("\nWiFi povezan");
  Serial.print("IP naslov: ");
  Serial.println(WiFi.localIP());
}

//-----------------------------------------------------------------------------------------------------
// Funkcija za sinhronizacijo časa z NTP strežnikom 
void syncTimestamp()
{
  displayLogOnLine(3, "Sync cas...");
  
  // Definiramo TZ niz za Srednjeevropski čas
  const char* tz_info = "CET-1CEST,M3.5.0,M10.5.0/3";

  // Uporabimo configTzTime, ki hkrati nastavi časovni pas in zažene NTP klienta.
  // To je pravilna metoda za ESP32.
  configTzTime(tz_info, ntpServer);

  // Počakamo na dejansko sinhronizacijo časa.
  Serial.print("Cakam na NTP sinhronizacijo... ");
  struct tm timeinfo;
  int retry = 0;
  const int retry_max = 15; // Počakamo največ 15 sekund

  // Poskušamo dobiti čas. Če leto ni večje od 2022, čas še ni nastavljen.
  while (!getLocalTime(&timeinfo) || timeinfo.tm_year < (2022 - 1900)) {
    if(retry++ > retry_max) {
      Serial.println("\nNTP sinhronizacija neuspesna!");
      displayLogOnLine(3, "NTP fail");
      return;
    }
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nNTP sinhronizacija uspesna!");
  displayLogOnLine(3, "NTP OK");
  
  // Sedaj, ko je čas zagotovo nastavljen, ga lahko izpišemo.
  printLocalTime();
  uint32_t timestamp = getTime(); // Pridobi trenutni čas v UTC
  Serial.println("Trenutni čas: " + String(timestamp)); // izpiši trenutni čas
}

//---------------------------------------------------------------------------------------------------------------------
// Funkcija za pridobivanje trenutnega časa v sekundah od 1.1.1970 (prej v main.cpp)
uint32_t getTime()
{
  time_t now;
  time(&now); // Preprosto preberi sistemsko uro (ki je v UTC)
  
  // Preverimo, ali je ura sploh že bila nastavljena (čas po letu 2023)
  if (now < 1672531200) { 
    return 0;
  }
  return now;
}

//---------------------------------------------------------------------------------------------------------------------
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

//---------------------------------------------------------------------------------------------------------------------
// Funkcija za pridobivanje sekund od polnoči (prej v main.cpp)
uint32_t getCurrentSeconds()
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


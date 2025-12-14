#ifndef FIREBASE_Z_H
#define FIREBASE_Z_H

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
//#include <Firebase_ESP_Client.h> // Predpostavljamo, da uporabljaš to knjižnico
#include "Lora_master.h"
#include "unified_lora_handler.h"

// Deklaracije funkcij, ki bodo definirane v Firebase.cpp
void Init_Firebase();
void Firebase_Connect();
void Firebase_handleStreamUpdate(int kanalIndex, int start_sec, int end_sec);
void Firebase_readInterval();
void Firebase_readKanalUrnik(uint8_t kanalIndex);
void Firebase_Update_Relay_State(int kanal, bool state);
void Firebase_Update_Sensor_Data(unsigned long timestamp, float temp, float hum, float soil_moisture);
void Firebase_Update_INA3221_Data(const char* device_id, unsigned long timestamp, 
                                  uint16_t alert_flags, float shunt_voltage_sum_mV, float total_current_mA,
                                  float ch0_bus_V, float ch0_shunt_mV, float ch0_current_mA, float ch0_power_mW,
                                  float ch1_bus_V, float ch1_shunt_mV, float ch1_current_mA, float ch1_power_mW,
                                  float ch2_bus_V, float ch2_shunt_mV, float ch2_current_mA, float ch2_power_mW);
void Firebase_processResponse(AsyncResult &aResult);
void streamCallback(AsyncResult &aResult);


// --- DEKLARACIJA GLOBALNIH SPREMENLJIVK IZ main.cpp ---
// S tem povemo, da te spremenljivke obstajajo in so definirane drugje.
extern bool firebaseUpdatePending;
extern ChannelUpdateData pendingUpdateData;
// Globalna instanca strukture in zastavica
extern volatile bool newChannelDataAvailable;
extern ChannelUpdateData channelUpdate;
// extern uint8_t sensorReadIntervalMinutes = 5; // Interval branja senzorjev v minutah (2 - 20 minut)

// --- DEKLARACIJA FUNKCIJ IZ main.cpp ---
// S tem povemo, da te funkcije obstajajo in jih bo linker našel kasneje.
extern int ParseToInt(String payload);  // Funkcija za pretvorbo String v int
extern void PrikaziStanjeSenzorjevNaSerial(); // Funkcija za prikaz stanja senzorjev na Serial monitorju
extern void formatSecondsToTime(char* buffer, size_t bufferSize, int seconds); // Funkcija za pretvorbo sekund v "HH:MM" obliko
extern void set_Interval(uint8_t minutes); // Funkcija za nastavitev intervala v milisekundah

// --- DEKLARACIJA GLOBALNIH SPREMENLJIVK iz main.cpp ---
extern int get_intValue; // Spremenljivka za shranjevanje prebrane int vrednosti
extern Kanal firebase_kanal[8]; // Polje struktur za kanale
extern uint8_t currentChannelInProcess; // Trenutno obdelovan kanal
extern bool firebase_response_received; // Zastavica, ki označuje, da je bil odgovor prejet

// -----------------------------------------




// Extern deklaracije za Firebase objekte
extern FirebaseApp app;
extern WiFiClientSecure ssl_client;
extern WiFiClientSecure stream_ssl_client;

using AsyncClient = AsyncClientClass;
  
extern AsyncClient aClient;
extern AsyncClient streamClient;
extern RealtimeDatabase Database;
extern AsyncResult databaseResult;
extern AsyncResult streamResult;               // Za Firebase streaming


#endif // FIREBASE_Z_H

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
void Firebase_Update_Sensor_Data(unsigned long timestamp, SensorDataPayload const& sensors);
void Firebase_Update_INA_Data(unsigned long timestamp, const INA3221_DataPayload& data);
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
// extern bool firebase_sensor_update; // zastavica za posodobitev senzorjev v Firebase

// -----------------------------------------

// Deklaracija globalnega semaforja
// extern SemaphoreHandle_t firebaseSemaphore;


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

// --- NOVA STRUKTURA: Enostavna čakalna vrsta brez dinamične alokacije ---
#define FIREBASE_QUEUE_SIZE 10

// --- NOVO: Struktura in čakalna vrsta za Firebase naloge ---
enum class FirebaseTaskType {
    UPDATE_SENSORS,
    UPDATE_INA,
    UPDATE_RELAY_STATE
};

struct FirebaseOperation {
    FirebaseTaskType type;
    unsigned long timestamp;
    union {
        SensorDataPayload sensors;
        INA3221_DataPayload ina;
        struct {
            uint8_t kanal;
            bool state;
        } relay;
    } data;
    bool pending;  // Ali je operacija aktivna?
};

// Globalna čakalna vrsta (statična alokacija)
extern FirebaseOperation firebaseOpsQueue[FIREBASE_QUEUE_SIZE];
extern uint8_t firebase_queue_head;
extern uint8_t firebase_queue_tail;
extern uint8_t firebase_queue_count;

// Funkcije za upravljanje čakalne vrste
bool Firebase_QueueOperation(const FirebaseOperation& op);
bool Firebase_ProcessNextOperation();
void Firebase_ResetConnection();

#endif // FIREBASE_Z_H

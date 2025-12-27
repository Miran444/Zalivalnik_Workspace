#ifndef SENSOR_QUEUE_H
#define SENSOR_QUEUE_H

#include <Arduino.h>
#include "message_protocol.h"


// --- Tipi senzorskih operacij ---
enum class SensorTaskType : uint8_t {
    READ_SENSORS,
    READ_INA,
    READ_POWER,
    READ_WATER_CONSUMPTION
    // Dodaj po potrebi
};

// --- Stanja operacije ---
enum class SensorTaskState : uint8_t {
    IDLE,           // Čakalna vrsta prazna
    SENDING,        // Pošiljamo LoRa ukaz
    WAITING_LORA,   // Čakamo na LoRa odgovor
    WAITING_FIREBASE, // Čakamo na Firebase potrditev
    SUCCESS,        // Operacija uspešna
    RETRY,          // Ponovni poskus
    FAILED          // Končna napaka
};

// --- Struktura za eno senzorsko operacijo ---
struct SensorOperation {
    SensorTaskType type;
    SensorTaskState state;
    uint8_t retry_count;
    unsigned long last_attempt_time;
    CommandType lora_command;  // Kateri LoRa ukaz poslati
};

// --- Nastavitve ---
#define SENSOR_QUEUE_SIZE 5
#define SENSOR_RETRY_MAX 3
#define SENSOR_RETRY_DELAY_MS 5000  // 5 sekund med poskusi
#define SENSOR_LORA_TIMEOUT_MS 3000 // 3 sekunde za LoRa odgovor

// --- Globalne spremenljivke ---
extern SensorOperation sensorOpsQueue[SENSOR_QUEUE_SIZE];
extern uint8_t sensor_queue_head;
extern uint8_t sensor_queue_tail;
extern uint8_t sensor_queue_count;
extern SensorOperation* current_sensor_op;  // Trenutno aktivna operacija

// --- Funkcije ---
void Sensor_InitQueue();
bool Sensor_QueueOperation(SensorTaskType type);
void Sensor_ProcessQueue();  // Kliči v loop()
void Sensor_OnLoRaResponse(bool success);
void Sensor_OnFirebaseResponse(bool success);
uint8_t Sensor_GetActiveOperationCount();  // Vrne število aktivnih operacij (v vrsti + trenutna)

extern void Lora_prepare_and_send_packet(CommandType cmd, const void *payload_data, size_t payload_size);

#endif // SENSOR_QUEUE_H
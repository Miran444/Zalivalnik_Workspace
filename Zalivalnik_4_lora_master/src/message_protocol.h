#pragma once
#include <Arduino.h>

// --- Tipi ukazov ---
enum class CommandType : uint8_t {
    // Ukazi od Master -> Rele
    CMD_SET_TIME = 1,
    CMD_GET_STATUS = 2,
    CMD_GET_URNIK = 3,
    CMD_GET_ALL_URNIK = 9,
    CMD_UPDATE_URNIK = 4,
    CMD_UPDATE_ALL_URNIK = 11,
    CMD_GET_SENSORS = 5,
    CMD_TOGGLE_RELAY = 6,
    CMD_GET_POWER = 7,
    CMD_GET_WATER_CONSUMPTION = 8,
    CMD_TEST = 10,
    CMD_INIT_DONE = 12,
    CMD_GET_INA_DATA = 13,

    // Odgovori od Rele -> Master
    RESPONSE_ACK = 100,
    RESPONSE_STATUS = 101,
    RESPONSE_URNIK = 102,
    RESPONSE_UPDATE_URNIK = 109,
    RESPONSE_TIME_SET = 103,
    RESPONSE_SENSORS = 104,
    RESPONSE_POWER = 105,
    RESPONSE_TEST = 106,
    RESPONSE_INIT_DONE = 107,
    RESPONSE_WATER_CONSUMPTION = 108,
    RESPONSE_INA_DATA = 110,
    
    // Notifikacije od Rele -> Master (brez zahteve)
    NOTIFY_LOW_BATT = 200,
    NOTIFY_TEMP_HIGH = 201,
    NOTIFY_RELAY_STATE_CHANGED = 202,
    NOTIFY_WATER_LEAK = 203,
    NOTIFY_TIME_REQUEST = 204,
    NOTIFY_SENSOR_ERROR = 206,
    NOTIFY_RESET_OCCURED = 207,
    NOTIFY_INA_DATA = 208,
    NOTIFY_INA_ALERT = 209,

    // Potrditev notifikacije od Master -> Rele
    ACK_NOTIFICATION = 250,
    ACK_RESET_OCCURED = 251,
    ACK_TIME_REQUEST = 252
};

// --- Statusi odgovora ---
enum class AckStatus : uint8_t {
    ACK_OK = 0,
    ERR_NACK = 1,
    ERR_CRC = 2,
    ERR_INVALID_COMMAND = 3,
    ERR_INVALID_PAYLOAD = 4,
    ERR_INVALID_PARAMETER = 5,
    ERR_INVALID_INDEX = 6
};

// --- DODAJTE TE DVE VRSTICI ---
// Prisili prevajalnik, da člane strukture zapakira tesno, brez poravnave.
// To zagotavlja, da je razporeditev bajtov v pomnilniku enaka na vseh napravah.
#pragma pack(push, 1)
// -----------------------------
#define LORA_SYNC_WORD 0xABCD   // Sinhronizacijska beseda za LoRa komunikacijo

#define LORA_PACKET_PAYLOAD_SIZE 57 // Največja velikost payload-a v bajtih

// --- Strukture za Payload ---
struct TimePayload {
    uint32_t unix_time;
};

struct TimeResponsePayload {
    uint32_t seconds_since_midnight;
};

struct UrnikPayload {
    uint8_t single; // 1 = en kanal, 0 = vsi kanali
    uint8_t releIndex; // 0-7
    uint32_t startTimeSec;
    uint32_t endTimeSec;
    uint8_t status; // 0 = OK, >1 = ERROR
};

// struct UrnikUpdatePayload {
//     bool single; // true = en kanal, false = vsi kanali
//     uint8_t releIndex; // 0-7
//     uint8_t status;
// };

// Nova struktura samo za stanje relejev
struct RelayStatusPayload {
    uint8_t relayStates; // Bitmask: bit 0 = rele 1, ...
};

// Nova struktura samo za podatke senzorjev
struct SensorDataPayload {
    float temperature;
    float humidity;
    float soil_moisture;
    float battery_voltage;
};

struct NotificationPayload {
    uint8_t code; // Koda notifikacije
    float value;  // Pripadajoča vrednost (npr. napetost baterije)
};

struct RelayStateChangePayload {
  uint8_t releIndex; // Indeks releja (0-7)
  uint8_t newState;  // Novo stanje (0 za OFF, 1 za ON)
};

struct RelayStatePayload {
  uint8_t channel; // Indeks releja (0-7)
  bool state;  // Novo stanje (0 za OFF, 1 za ON)
};


// --- Glavna struktura paketa ---
struct LoRaPacket {
    uint16_t syncWord;
    uint16_t messageId;
    CommandType command;
    uint8_t payload[LORA_PACKET_PAYLOAD_SIZE];
    uint16_t crc;
};

struct INA3221_ChannelData {
    float bus_voltage;
    float current_mA;
    float power_mW;
    float shunt_voltage_mV;
};

struct INA3221_DataPayload {
    INA3221_ChannelData channels[3];
    uint16_t alert_flags;
    float shunt_voltage_sum_mV;
    // float total_current_mA;
};

struct INA3221_AlertPayload {
    uint16_t alert_number;
    uint32_t timestamp;

};

// --- DODAJTE TO VRSTICO ---
// Vrne nastavitev poravnave na prejšnje stanje
#pragma pack(pop)
// ---------------------------
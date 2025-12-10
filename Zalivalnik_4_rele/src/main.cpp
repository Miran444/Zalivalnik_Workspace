//
// main.cpp
// Zalivalnik_4_rele
// Created by User on 2025-04-14.
// Copyright © 2025 User. All rights reserved.
// ----------------------------------------------------------------------------

#include <Arduino.h>
#include "SPIFFS.h"
#include "Adafruit_SHT4x.h"
#include "INA3221.h"
#include <stdio.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include "message_protocol.h"

// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

const uint8_t DEBOUNCE_DELAY = 30; // in milliseconds
#define LED_PIN 16                 // zunanja LED dioda
#define LED_BUILTIN 23             // ugrajena LED dioda
#define BTN_PIN 17                 // zunanji gumb

#define FORMAT_SPIFFS_IF_FAILED true // format SPIFFS if mounting fails

#define LED 1      // notify LED
#define SENSOR 2   // notify sensor
#define BUTTON 3   // notify button
#define ALARM 4    // notify alarm
#define RELAY 5    // notify relay
#define NETWORKS 6 // notify networks
#define SCHEDULE 7 // notify schedule

// Define the I2C pins
#define SDA_PIN 21
#define SCL_PIN 22

// Določite GPIO pine za RX in TX
#define RELAY_RX_PIN 5  // Prilagodite glede na vaše potrebe
#define RELAY_TX_PIN 18 // Prilagodite glede na vaše potrebe

// Mask/Enable Register bit flags for INA3221
#define INA3221_CONV_READY (1UL << 0)     ///< Conversion Ready
#define INA3221_TIMECONT_ALERT (1UL << 1) ///< Timing Control Alert
#define INA3221_POWER_VALID (1UL << 2)    ///< Power Valid Alert
#define INA3221_WARN_CH3 (1UL << 3)       ///< Warning Alert for Channel 3
#define INA3221_WARN_CH2 (1UL << 4)       ///< Warning Alert for Channel 2
#define INA3221_WARN_CH1 (1UL << 5)       ///< Warning Alert for Channel 1
#define INA3221_SUMMATION (1UL << 6)      ///< Summation Alert
#define INA3221_CRITICAL_CH3 (1UL << 7)   ///< Critical Alert for Channel 3
#define INA3221_CRITICAL_CH2 (1UL << 8)   ///< Critical Alert for Channel 2
#define INA3221_CRITICAL_CH1 (1UL << 9)   ///< Critical Alert for Channel 1

// Constants for notification retry logic
const unsigned long NOTIFICATION_ACK_TIMEOUT = 5000; // 5 sekunde za čakanje na ACK
const uint8_t MAX_NOTIFICATION_RETRIES = 3;          // Največje število ponovitev

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

// Led    led = {LED_PIN, false}; // LED na protoboardu
// Led    onboard_led = { LED_BUILTIN, false };  //onboard LED
// Button button = { BTN_PIN, HIGH, 0, 0 };  // button

unsigned long milisekunda = 0; // časovna spremenljivka za branje gumba
unsigned long lastCheckTime = 0; // čas zadnje kontrole za senzorje
unsigned long lastSensorReadTime = 0; // čas zadnjega branja senzorev
unsigned long lastLoRaSendTime = 0; // čas zadnjega pošiljanja preko LoRa
unsigned long lastDisplayUpdateTime = 0; // čas zadnje posodobitve zaslona
unsigned long lastRelayCheckTime = 0; // čas zadnje kontrole relejev
unsigned long lastINA3221ReadTime = 0; // čas zadnjega branja INA3221 senzorja
int secondsFromMidnight = 0; // trenutni čas od polnoči v sekundah

// Create an instance of the SHT4x sensor
Adafruit_SHT4x sht4x = Adafruit_SHT4x();

// Create an INA3221 object
INA3221 INA(0x40);

// global variables for temperature and humidity
float Temperature; // temperature
float Humidity;    // humidity

uint8_t ACK_status = 0; // 0 = ACK, 1 = NACK, 2 = ERROR, 3 = INVALID, 4 = INVALID COMMAND, 5 = INVALID PARAMETER, 6 = ACK WITH DATA

// Create an instance of the Preferences class
Preferences preferences; // Create an instance of the Preferences class

// Create an instance of the HardwareSerial class for the relay
HardwareSerial relaySerial(1); // Use UART1 for the relay

// String za pošiljanje odgovora
String responseMessage = ""; // String for sending the response

bool error_flag = false; // zastavica za napako sistema

// Definition of relay configuration
// struct RelayConfig {
//   int pin;            // Pin number for the relay
//   bool state;         // State of the relay (on/off)
//   char start[6];      // Start time (HH:MM)
//   int start_sec;      // Start time in seconds since midnight
//   char end[6];        // End time (HH:MM)
//   int end_sec;        // End time in seconds since midnight
// };

struct Kanal
{
  int pin;       // Pin number for the relay
  bool state;    // Stanje releja (vklopljen/izklopljen)
  char start[6]; // Začetni čas (HH:MM)
  int start_sec; // Začetni čas v sekundah od polnoči
  char end[6];   // Končni čas (HH:MM)
  int end_sec;   // Končni čas v sekundah od polnoči
};

Kanal kanal[8] = {
    {32, false, "00:00", 0, "00:00", 0},
    {33, false, "00:00", 0, "00:00", 0},
    {25, false, "00:00", 0, "00:00", 0},
    {26, false, "00:00", 0, "00:00", 0},
    {27, false, "00:00", 0, "00:00", 0},
    {14, false, "00:00", 0, "00:00", 0},
    {12, false, "00:00", 0, "00:00", 0},
    {13, false, "00:00", 0, "00:00", 0}};

LoRaPacket receivedPacket; // Globalna spremenljivka za prejeti paket

// Spremenljivke za upravljanje s potrditvami obvestil
LoRaPacket unacknowledged_notification;
bool notification_awaiting_ack = false;
unsigned long last_notification_send_time = 0;
uint8_t notification_retry_count = 0;

// zastavica da je sistemski čas nastavljen
bool system_time_set = false;
bool init_done_flag = false; // zastavica za zaključek inicializacije
bool obdelava_paketa = false; // zastavica za obdelavo paketa v glavnem zanki
uint8_t relay_states_bitmask = 0; // Bitmaska za trenutno stanje relejev

// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct Led
{
  // state variables
  uint8_t pin; // pin number for the LED
  bool on;     // logical state of the LED

  // methods
  void update() // method for updating the physical state of the LED
  {
    digitalWrite(pin, on ? HIGH : LOW);
  }

  void toggle() // method for toggling the logical state of the LED
  {
    on = !on;
    update();
  }
  // method for blinking the LED for a given duration on and off
  void blink(unsigned long onInterval, unsigned long offInterval)
  {
    on = millis() % (onInterval + offInterval) < onInterval;
    update();
  }
};

Led led = {LED_PIN, false};             // LED na protoboardu
Led onboard_led = {LED_BUILTIN, false}; // onboard LED

// ----------------------------------------------------------------------------
// Definition of the Button component
// ----------------------------------------------------------------------------

struct Button
{
  // state variables
  uint8_t pin;               // pin number for the button
  bool lastReading;          // last reading of the button
  uint32_t lastDebounceTime; // last time the button was read
  uint16_t state;            // current state of the button

  // methods determining the logical state of the button
  bool pressed() { return state == 1; }                                              // short press
  bool released() { return state == 0xffff; }                                        // short release
  bool held_given(uint16_t count = 0) { return state == 1 + count; }                 // hold press for a given time
  bool held_over(uint16_t count = 0) { return state > 1 + count && state < 0xffff; } // hold press for over a given time

  // method for reading the physical state of the button
  void read()
  {
    // reads the voltage on the pin connected to the button
    bool reading = digitalRead(pin);

    // if the logic level has changed since the last reading,
    // we reset the timer which counts down the necessary time
    // beyond which we can consider that the bouncing effect
    // has passed.
    if (reading != lastReading)
    {
      lastDebounceTime = millis();
    }

    // from the moment we're out of the bouncing phase
    // the actual status of the button can be determined
    if (millis() - lastDebounceTime > DEBOUNCE_DELAY)
    {
      // don't forget that the read pin is pulled-up
      bool pressed = reading == LOW;
      if (pressed)
      {
        if (state < 0xfffe)
          state++;
        else if (state == 0xfffe)
          state = 2;
      }
      else if (state)
      {
        state = state == 0xffff ? 0 : 0xffff;
      }
    }
    // finally, each new reading is saved
    lastReading = reading;
  }
};

Button button = {BTN_PIN, HIGH, 0, 0}; // button

//===========================================================================
// --- POMOŽNE FUNKCIJE ---
//===========================================================================

// ----------------------------------------------------------------------------
// Function to initialize relays
void initRelays()
{
  for (int i = 0; i < 8; i++)
  {
    pinMode(kanal[i].pin, OUTPUT);   // Set relay pin as output
    digitalWrite(kanal[i].pin, LOW); // Relays off by default
    kanal[i].state = false;           // Initialize relay state to off
  }
}

// ----------------------------------------------------------------------------
// Function to set relay state
void setRelayState(int relayIndex, bool state)
{
  if (relayIndex >= 0 && relayIndex < 8)
  {
    kanal[relayIndex].state = state;                         // Update the state in the kanal structure
    digitalWrite(kanal[relayIndex].pin, state ? HIGH : LOW); // Set relay state
  }
}

// ----------------------------------------------------------------------------
// Function to load channel data from preferences
// ----------------------------------------------------------------------------
void loadKanalFromPreferences()
{
  preferences.begin("kanal-data", false); // "kanal-data" je ime prostora v NVS
  for (int i = 0; i < 8; i++)
  {
    String prefix = "K" + String(i); // Ustvari ključ za vsak kanal
    String start = preferences.getString((prefix + "_start").c_str(), "00:00");
    String end = preferences.getString((prefix + "_end").c_str(), "00:00");
    int start_sec = preferences.getInt((prefix + "_start_sec").c_str(), 0);
    int end_sec = preferences.getInt((prefix + "_end_sec").c_str(), 0);
    //bool state = preferences.getBool((prefix + "_state").c_str(), false);

    // Kopiraj prebrane vrednosti v strukturo kanal
    strncpy(kanal[i].start, start.c_str(), sizeof(kanal[i].start) - 1);
    kanal[i].start[sizeof(kanal[i].start) - 1] = '\0'; // Zagotovi null-terminacijo
    strncpy(kanal[i].end, end.c_str(), sizeof(kanal[i].end) - 1);
    kanal[i].end[sizeof(kanal[i].end) - 1] = '\0'; // Zagotovi null-terminacijo
    kanal[i].start_sec = start_sec;
    kanal[i].end_sec = end_sec;
    //kanal[i].state = state;
  }
  preferences.end(); // Zapri prostor v NVS
  Serial.println("Kanal data loaded from Preferences.");
}

// ----------------------------------------------------------------------------
// Function to save a single channel data to preferences
// ----------------------------------------------------------------------------
void saveKanalToPreferences(int kanalIndex)
{
  preferences.begin("kanal-data", false);
  String prefix = "K" + String(kanalIndex);
  preferences.putString((prefix + "_start").c_str(), kanal[kanalIndex].start);
  preferences.putString((prefix + "_end").c_str(), kanal[kanalIndex].end);
  preferences.putInt((prefix + "_start_sec").c_str(), kanal[kanalIndex].start_sec);
  preferences.putInt((prefix + "_end_sec").c_str(), kanal[kanalIndex].end_sec);
  preferences.end();
}

//---------------------------------------------------------------------------
// Function to save channel data to preferences
//---------------------------------------------------------------------------
void saveAllKanalsToPreferences()
{
  preferences.begin("kanal-data", false); // "kanal-data" je ime prostora v NVS
  for (int i = 0; i < 8; i++)
  {
    saveKanalToPreferences(i);
  }
  preferences.end(); // Zapri prostor v NVS
  Serial.println("Kanal data saved to Preferences.");
}


// ----------------------------------------------------------------------------
// Izračun CRC-16/MODBUS
// ----------------------------------------------------------------------------
uint16_t calculate_crc(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

// ----------------------------------------------------------------------------
// Pripravi in pošlji odgovor nazaj na Lora_rele (bridge)
// ----------------------------------------------------------------------------
void send_response_to_bridge(uint16_t request_id, CommandType cmd, const void *payload_data, size_t payload_size)
{
  LoRaPacket packet;
  packet.syncWord = LORA_SYNC_WORD;
  packet.messageId = request_id;
  packet.command = cmd;

  memset(packet.payload, 0, sizeof(packet.payload));
  if (payload_data != nullptr && payload_size > 0)
  {
    memcpy(packet.payload, payload_data, payload_size);
  }

  packet.crc = calculate_crc((const uint8_t *)&packet, offsetof(LoRaPacket, crc));
  Serial.printf("Sending response to bridge: Message ID: %u, Command: %u, Payload Size: %zu, CRC: 0x%04X\n",
                packet.messageId, static_cast<uint8_t>(packet.command), payload_size, packet.crc);

  // Check if the relaySerial is available for writing
  int avByte = relaySerial.availableForWrite();
  if (avByte >= sizeof(LoRaPacket))
  {
    relaySerial.write((const uint8_t *)&packet, sizeof(LoRaPacket));
  }
  else
  {
    Serial.println("Not enough space in relaySerial buffer.");
  }

  obdelava_paketa = false;  // smo končali z obdelavo paketa
}

// ----------------------------------------------------------------------------
// Pripravi in pošlji obvestilo (notification) na Lora_rele (bridge)
// To se uporablja za sporočila, ki jih rele pošilja samoiniciativno.
// ----------------------------------------------------------------------------
void send_notification_to_bridge(CommandType cmd, const void* payload_data, size_t payload_size) {
    // Ne pošlji novega obvestila, če že čakamo na potrditev prejšnjega
    if (notification_awaiting_ack) {
        Serial.println("Opozorilo: Prejšnje obvestilo še ni potrjeno. Novo obvestilo je bilo zavrženo.");
        return;
    }

    static uint16_t notification_id = 0; // Števec za ID obvestil
    LoRaPacket packet;
    packet.syncWord = LORA_SYNC_WORD;
    packet.messageId = notification_id++; // Uporabimo in povečamo statični števec
    packet.command = cmd;
    
    memset(packet.payload, 0, sizeof(packet.payload));
    if (payload_data != nullptr && payload_size > 0) {
        memcpy(packet.payload, payload_data, payload_size);
    }

    packet.crc = calculate_crc((const uint8_t*)&packet, offsetof(LoRaPacket, crc));
    
    // Shrani paket in nastavi stanje za čakanje na ACK
    memcpy(&unacknowledged_notification, &packet, sizeof(LoRaPacket));
    notification_awaiting_ack = true;
    last_notification_send_time = millis();
    notification_retry_count = 0;

    Serial.printf("Pošiljam obvestilo (poskus %d): ID: %u, Ukaz: %u\n", notification_retry_count + 1, packet.messageId, static_cast<uint8_t>(packet.command));

    if (relaySerial.availableForWrite() >= sizeof(LoRaPacket)) {
        relaySerial.write((const uint8_t*)&packet, sizeof(LoRaPacket));
    } else {
        Serial.println("Napaka: Ni dovolj prostora v bufferju za pošiljanje obvestila.");
        notification_awaiting_ack = false; // Prekliči pošiljanje
    }

    //Pričakujemo odgovor v handle_packet_from_bridge CommandType::ACK_NOTIFICATION
}


// ---------------------------------------------------------------------------
// Function to print the current local time
//---------------------------------------------------------------------------
// This function prints the current local time in the format "Mon 14.4.2025 20:33:28"
// It uses the localtime_r function to convert the time_t value to a tm structure
void print_local_time()
{
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  char buffer[64]; // Buffer to hold the formatted time string

  // Format the time as "Mon 14.4.2025 20:33:28"
  strftime(buffer, sizeof(buffer), "%a %d.%m.%Y %H:%M:%S", &timeinfo);

  Serial.printf("\nLocal time: %s\n", buffer);
}

// ----------------------------------------------------------------------------
// Funkcija za pretvorbo časa v obliki "HH:MM" v sekunde od začetka dneva
// ----------------------------------------------------------------------------
int parseTimeToSeconds(const char *timeStr)
{
  int hours, minutes;
  sscanf(timeStr, "%d:%d", &hours, &minutes); // Razčleni niz
  // Preveri, če so ure in minute v veljavnem obsegu
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
  {
    Serial.println("Invalid time format. Please use HH:MM format.");
    return -1; // Vrni -1, če je čas neveljaven
  }
 
  return hours * 3600 + minutes * 60;         // Pretvori v sekunde
}


// ----------------------------------------------------------------------------
// Funkcija za pretvorbo časa v int obliki sekunde od začetka dneva v obliko "HH:MM"
// ----------------------------------------------------------------------------
void formatSecondsToTime(char* buffer, size_t bufferSize, int seconds)
{
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  snprintf(buffer, bufferSize, "%02d:%02d", hours, minutes);
}

//-----------------------------------------------------------------------------------------------------
// Funkcija za polneje urnik_payload strukture z podatki kanala
//-----------------------------------------------------------------------------------------------------
void populateUrnikPayload(UrnikPayload& urnik_payload, int releIndex)
{
  urnik_payload.releIndex = releIndex;
  urnik_payload.startTimeSec = kanal[releIndex].start_sec;
  urnik_payload.endTimeSec = kanal[releIndex].end_sec;
  // Kličemo novo funkcijo in ji direktno podamo ciljni buffer.
  // formatSecondsToTime(urnik_payload.start, sizeof(urnik_payload.start), kanal[releIndex].start_sec);
  // formatSecondsToTime(urnik_payload.end, sizeof(urnik_payload.end), kanal[releIndex].end_sec);
}


//-----------------------------------------------------------------------------------------------------
// Funkcija za branje urnik_payload strukture in kopiranje v kanal strukturo
//-----------------------------------------------------------------------------------------------------
void readUrnikPayload(const UrnikPayload& urnik_payload, int releIndex)
{
  kanal[releIndex].start_sec = urnik_payload.startTimeSec;
  kanal[releIndex].end_sec = urnik_payload.endTimeSec;
  // --- SPREMEMBA TUKAJ ---
  // Kličemo novo funkcijo in ji direktno podamo ciljni buffer.
  formatSecondsToTime(kanal[releIndex].start, sizeof(kanal[releIndex].start), kanal[releIndex].start_sec);
  formatSecondsToTime(kanal[releIndex].end, sizeof(kanal[releIndex].end), kanal[releIndex].end_sec);
  // -----------------------
}

//---------------------------------------------------------------------------------------------------------------------
// Funkcija za pridobivanje sekund od polnoči (prej v main.cpp)
//---------------------------------------------------------------------------------------------------------------------
uint32_t getSecondsFromMidnight()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return 0;
  }
  return timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
}

// ----------------------------------------------------------------------------
// SHT4x sensor initialization
// ----------------------------------------------------------------------------
void initSHT4x()
{
  if (!sht4x.begin())
  {
    Serial.println("Could not find a valid SHT4x sensor, check wiring!");
    while (1)
    {
      onboard_led.blink(50, 150);
    }
  }

  Serial.printf("Found SHT4x I2C address : 0x%04X\n", SHT4x_DEFAULT_ADDR);
  Serial.printf("Serial number: 0x%04X\n", sht4x.readSerial());

  // You can have 3 different precisions, higher precision takes longer
  sht4x.setPrecision(SHT4X_HIGH_PRECISION);
  switch (sht4x.getPrecision())
  {
  case SHT4X_HIGH_PRECISION:
    Serial.println("High precision");
    break;
  case SHT4X_MED_PRECISION:
    Serial.println("Med precision");
    break;
  case SHT4X_LOW_PRECISION:
    Serial.println("Low precision");
    break;
  }

  // You can have 6 different heater settings
  // higher heat and longer times uses more power
  // and reads will take longer too!
  sht4x.setHeater(SHT4X_NO_HEATER);
  switch (sht4x.getHeater())
  {
  case SHT4X_NO_HEATER:
    Serial.println("No heater");
    break;
  case SHT4X_HIGH_HEATER_1S:
    Serial.println("High heat for 1 second");
    break;
  case SHT4X_HIGH_HEATER_100MS:
    Serial.println("High heat for 0.1 second");
    break;
  case SHT4X_MED_HEATER_1S:
    Serial.println("Medium heat for 1 second");
    break;
  case SHT4X_MED_HEATER_100MS:
    Serial.println("Medium heat for 0.1 second");
    break;
  case SHT4X_LOW_HEATER_1S:
    Serial.println("Low heat for 1 second");
    break;
  case SHT4X_LOW_HEATER_100MS:
    Serial.println("Low heat for 0.1 second");
    break;
  }
}

// ----------------------------------------------------------------------------
// Function for reading the SHT4x sensor
// ----------------------------------------------------------------------------
void Read_SHT4X(uint8_t sensor_nr)
{
  sensors_event_t humidity, temp;
  bool Read_success = false;

  // sht4x.getEvent(&humidity, &temp); // read the sensor
  Read_success = sht4x.getEvent(&humidity, &temp); // read the sensor

  // Store the temperature and humidity values with two decimal places
  Temperature = round(temp.temperature * 100) / 100.0;
  Humidity = round(humidity.relative_humidity * 100) / 100.0;
  // Temperature = temp.temperature;
  // Humidity = humidity.relative_humidity;

  if (Read_success)
  {
    Serial.print("Sensor ");
    Serial.print(sensor_nr);
    Serial.print(": Temperature: ");
    Serial.print(Temperature);
    Serial.print(" °C, Humidity: ");
    Serial.print(Humidity);
    Serial.println(" % rH");
  }
  else
  {
    Serial.println("Error reading sensor!");
  }
}

// ----------------------------------------------------------------------------
// Funkcija za inicializacijo INA3221 senzorja
// ----------------------------------------------------------------------------
void initINA3221()
{

  if (!INA.begin() )
  {
    Serial.println("could not connect. Fix and Reboot");
  }
  else
  {
    Serial.printf("INA3221 Found at I2C address: \t0x%04X\n", INA.getAddress());
  }

  Serial.printf("DieID: \t0x%04X\n", INA.getDieID());
  Serial.printf("ManID: \t0x%04X\n", INA.getManufacturerID());
  Serial.printf(" Conf: \t0x%04X\n", INA.getConfiguration());

  // Settings
  Serial.println("Setting up INA3221...");
  INA.setMode(7); // Set to continuous shunt and bus measurement mode
  Serial.printf("Mode set to: %d\n", INA.getMode());

  // Set average of samples
  INA.setAverage(2); // 0=1,1=4,2=16,3=64,4=128,5=256,6=512,7=1024
  Serial.printf("Averaging set to: %d\n", INA.getAverage());

  INA.setWarningAlert(0, 38 * 1000);  // Channel 1 Warning at 38mV

  //  overwrite default shunts.
  INA.setShuntR(0, 0.10213);
  Serial.printf("Shunt R0 set to: %.5f Ohms\n", INA.getShuntR(0));
  INA.setShuntR(1, 0.10063);
  Serial.printf("Shunt R1 set to: %.5f Ohms\n", INA.getShuntR(1));
  INA.setShuntR(2, 0.10189);
  Serial.printf("Shunt R2 set to: %.5f Ohms\n", INA.getShuntR(2));

  Serial.println("\nCHAN\tCRITIC\tWARNING");
  Serial.println("\n    \t mV  \t mV");
  for (int ch = 0; ch < 3; ch++)
  {
    Serial.print(ch);
    Serial.print("\t");
    Serial.print(INA.getCriticalAlert(ch) / 1000.0);
    Serial.print("\t");
    Serial.print(INA.getWarningAlert(ch) / 1000.0);
    Serial.println();
  }

  Serial.printf("SV_SUM: %.3f\nShunt Voltage (mV)", INA.getShuntVoltageSum() / 1000.0);
  Serial.printf("SV_LIMIT: \t%.3f\n", INA.getShuntVoltageSumLimit() / 1000.0);

  Serial.printf("Mask/ Enable: %04X\n", INA.getMaskEnable());
  Serial.printf("Power Limit (mV): UPPER: %.3f, LOWER: %.3f\n", INA.getPowerUpperLimit() / 1000.0, INA.getPowerLowerLimit() / 1000.0);
  Serial.println("INA3221 setup done.");  
  Serial.println("-----------------------------------");
}

// ----------------------------------------------------------------------------
// Funkcija za branje INA3221 senzorja
// ----------------------------------------------------------------------------
void Read_INA3221()
{
  float sumCurrent = 0.0;

  // Display voltage and current (in mA) for all three channels
  Serial.println("\nCHAN\tBUS\tSHUNT\tCURRENT\tPOWER");
  Serial.println("\t   V \t   mV \t   mA \t   W");
  for (int ch = 0; ch < 3; ch++)
  {
    Serial.print(ch);
    Serial.print("\t");
    Serial.print(INA.getBusVoltage(ch), 3);
    Serial.print("\t");
    Serial.print(INA.getShuntVoltage_mV(ch), 3);
    Serial.print("\t");
    sumCurrent += INA.getCurrent_mA(ch);
    Serial.print(INA.getCurrent_mA(ch), 3);
    Serial.print("\t");
    Serial.print(INA.getPower(ch), 3);
    Serial.println();
  }
  Serial.printf("Total Current: %.3f\n", sumCurrent);
  Serial.printf("Total Shunt Voltage (mV): %.3f\n", INA.getShuntVoltageSum() / 1000.0);

  uint16_t flags = INA.getMaskEnable();

  if (flags & INA3221_CRITICAL_CH1) {
    Serial.println("   !!! Channel 1 Critical Current !!!");
  }
  if (flags & INA3221_CRITICAL_CH2) {
    Serial.println("   !!! Channel 2 Critical Current !!!");
  }
  if (flags & INA3221_CRITICAL_CH3) {
    Serial.println("   !!! Channel 3 Critical Current !!!");
  }
    if (flags & INA3221_WARN_CH1) {
    Serial.println("   ... Channel 1 Warning Current ...");
  }
  if (flags & INA3221_WARN_CH2) {
    Serial.println("   ... Channel 2 Warning Current ...");
  }
  if (flags & INA3221_WARN_CH3) {
    Serial.println("   ... Channel 3 Warning Current ...");
  }
}

// ----------------------------------------------------------------------------
// Funkcija za izpis urnika na serijski monitor
// ----------------------------------------------------------------------------
void PrintUrnik()
{
  Serial.println("[UR] Izpis urnika:");
  for (int i = 0; i < 8; i++)
  {
    Serial.printf("Kanal %d: Start: %s, End: %s, State: %d\n", i + 1, kanal[i].start, kanal[i].end, kanal[i].state);
  }
}

// ----------------------------------------------------------------------------
// Funkcija za preverjanje gumba
// ----------------------------------------------------------------------------
void handleButton() {

  // ensure the button state is read every 1 milisecond
  if (millis() != milisekunda)
  {
    button.read();

    //---------------------------------------------------------------------------
    // če je gumb pritisnjen
    if (button.pressed())
    {
      led.toggle(); // toggle the LED

    }
    //---------------------------------------------------------------------------
    // če je gumb pritisnjen za določen čas (mS) 
    else if (button.held_given(1000))
    { 

      print_local_time(); // print local time
      // notifyClients(NETWORKS); // send the WiFi networks to the client
      Read_SHT4X(1); // read the SHT4x sensor
      // notifyClients(SENSOR);   // send the sensor data to all clients
    }

    //---------------------------------------------------------------------------
    // če je gumb pritisnjen več kot določen čas (mS) 
    else if (button.held_over(1000))
    {
      onboard_led.blink(100, 100); // blink the onboard LED
    }

    //---------------------------------------------------------------------------
    // če je gumb sproščen
    else if (button.released())
    {
      
    }
  } 
}

// ----------------------------------------------------------------------------
// preveri stanje relejev glede na urnik
// ----------------------------------------------------------------------------
// This function checks the relay states based on the schedule and updates them accordingly
// Function is called periodically in the main loop every second
bool checkRelayStates()
{

  bool state_changed = false; // Zastavica za sledenje spremembam
  relay_states_bitmask = 0; // Bitmaska za trenutno stanje relejev
  char buffer[6]; // Buffer za formatiran čas "HH:MM"

  secondsFromMidnight = getSecondsFromMidnight(); // Posodobi secondsFromMidnight

  //  Preveri vsak kanal
  for (int i = 0; i < 8; i++)
  {
    if (kanal[i].state) // Če je kanal trenutno vklopljen 
    {
      bitSet(relay_states_bitmask, i); // Posodobi bitmasko
    }

    // Pretvori čas vklopa in izklopa v sekunde od začetka dneva
    int startSeconds = kanal[i].start_sec;
    int endSeconds = kanal[i].end_sec;

    // Če sta oba start_sec in end_sec enaka 0, pomeni, da je kanal izklopljen
    if (startSeconds == 0 && endSeconds == 0)
    {
      if (kanal[i].state) // Če je kanal trenutno vklopljen 
      {
        kanal[i].state = false;          // Izklopi kanal
        digitalWrite(kanal[i].pin, LOW); // Izklopi rele
        bitClear(relay_states_bitmask, i); // Posodobi bitmasko
        state_changed = true;  // Označi, da je prišlo do spremembe stanja          
      } 
      continue;
    }

    // Preveri, ali je treba vklopiti rele
    if (!kanal[i].state && (secondsFromMidnight >= startSeconds && secondsFromMidnight < endSeconds))
    {
      kanal[i].state = true;            // Vklopi kanal
      digitalWrite(kanal[i].pin, HIGH); // Vklopi rele
      bitSet(relay_states_bitmask, i);  // Posodobi bitmasko

      formatSecondsToTime(buffer, sizeof(buffer), secondsFromMidnight);
      Serial.printf("Kanal %d vklopljen ob %s\n", i + 1, buffer);
      state_changed = true;

    }
    // Preveri, ali je treba izklopiti rele
    else if (kanal[i].state && (secondsFromMidnight >= endSeconds || secondsFromMidnight < startSeconds))
    {
      kanal[i].state = false;          // Izklopi kanal
      digitalWrite(kanal[i].pin, LOW); // Izklopi rele
      bitClear(relay_states_bitmask, i);  // Posodobi bitmasko

      formatSecondsToTime(buffer, sizeof(buffer), secondsFromMidnight);
      Serial.printf("Kanal %d izklopljen ob %s\n", i + 1, buffer);
      state_changed = true;
    }
  }

  return state_changed;

}

// ---------------------------------------------------------------------------------------
// --- FUNKCIJA ZA OBDELAVO BINARNIH PAKETOV IZ LORA_RELE BOARDA---
// ---------------------------------------------------------------------------------------
void handle_packet_from_bridge(const LoRaPacket &packet)
{
  // 1. Preverimo CRC
  uint16_t received_crc = packet.crc;
  uint16_t calculated_crc = calculate_crc((const uint8_t *)&packet, offsetof(LoRaPacket, crc));
  if (received_crc != calculated_crc)
  {
    Serial.println("NAPAKA: CRC prejetega paketa se ne ujema!");
    // Ne pošljemo odgovora, ker je paket morda popolnoma napačen
    return;
  }

  Serial.printf("Prejet veljaven paket ID: %d, Ukaz: %d\n", packet.messageId, (int)packet.command);
  obdelava_paketa = true; // Nastavi zastavico da obdelujemo paket

  // 2. Obdelava glede na tip ukaza
  switch (packet.command)
  {
    //=======================================================================
  case CommandType::ACK_NOTIFICATION:
    // Obdelava ACK obvestila
    if (notification_awaiting_ack && packet.messageId == unacknowledged_notification.messageId)
    {
      Serial.printf("Prejeto ACK za obvestilo ID: %d. Uspešno.\n", packet.messageId);
      notification_awaiting_ack = false; // Uspešno potrjeno, ponastavi stanje
      obdelava_paketa = false; // Končaj obdelavo paketa
    }
    else
    {
      Serial.printf("Prejeto nepričakovano ACK obvestilo za ID: %d\n", packet.messageId);
    }
    break;

    //=======================================================================
  case CommandType::CMD_SET_TIME:
    // Nastavi čas na podlagi prejete vrednosti UNIX časa
    {
      system_time_set = false;  // to je začetek inicializacije rele modula
      init_done_flag = false;

       // Tukaj izklopimo vse releje ob nastavitvi časa.
      // Ponastavili se bodo po koncu inicializacije.
      for (int i = 0; i < 8; i++)
      {
        kanal[i].state = false;            // Nastavi stanje v strukturi
        digitalWrite(kanal[i].pin, LOW); // Izklopi rele
      }     

      TimePayload time_payload;
      memcpy(&time_payload, packet.payload, sizeof(TimePayload));

      // Uporabimo static_cast za eksplicitno pretvorbo in utišanje opozorila
      // Ta del je PRAVILEN. Nastavi sistemsko uro na prejeti UTC timestamp.
      timeval tv = {.tv_sec = static_cast<time_t>(time_payload.unix_time), .tv_usec = 0};
      settimeofday(&tv, NULL);

      // Ponovno uveljavimo nastavitve časovnega pasu.
      // To prisili sistem, da uporabi TZ spremenljivko na novo nastavljeni uri.
      tzset();

      print_local_time();                             // Prikaži nastavljeni čas
      secondsFromMidnight = getSecondsFromMidnight(); // Posodobi secondsFromMidnight

      Serial.printf("Sekunde od polnoči: %d\n", secondsFromMidnight);

      TimeResponsePayload response_payload;
      response_payload.seconds_since_midnight = secondsFromMidnight;
      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_TIME_SET, &response_payload, sizeof(response_payload));
      system_time_set = true; // Nastavi zastavico, da je sistemski čas nastavljen
      notification_awaiting_ack = false; // Prekliči pošiljanje obvestila, če je bilo v teku (če pošljmo zahtevo po nastavitvi časa)
      break;
    }

  //=======================================================================
  case CommandType::CMD_GET_STATUS:
    // Pošlji stanje vseh relejev kot bitmask
    {
      // Najprej preberemo stanje vseh relejev z funkcijo checkRelayStates
      checkRelayStates(); // Pridobimo trenutno stanje relejev v relay_states_bitmask
      PrintUrnik(); // Izpišemo urnik za debug

      // Pripravi payload s trenutnim stanjem vseh relejev
      RelayStatusPayload status_payload;
      status_payload.relayStates = relay_states_bitmask;

      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_STATUS, &status_payload, sizeof(status_payload));
      break;
    }

  //=======================================================================
  case CommandType::CMD_GET_SENSORS:
    // Pošlji podatke senzorjev
    {
      Read_SHT4X(1); // Osvežimo vrednosti senzorjev
      SensorDataPayload sensor_payload;
      sensor_payload.temperature = Temperature;
      sensor_payload.humidity = Humidity;
      // Dodajte še ostale senzorje, če jih imate
      sensor_payload.soil_moisture = 0;
      sensor_payload.battery_voltage = 0;
      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_SENSORS, &sensor_payload, sizeof(sensor_payload));
      break;
    }

  //=======================================================================
  case CommandType::CMD_UPDATE_URNIK:
    // Nastavi urnik za določen rele
    // ne pošljemo celotne strukture, ampak samo indeks in čas v sekundah
    // ure in minute se izračunajo na napravi
    {
      UrnikPayload received_urnik; // Pripravimo strukturo za prejem
      memcpy(&received_urnik, packet.payload, sizeof(UrnikPayload));
      uint8_t index = received_urnik.releIndex;

      UrnikPayload update_payload; // Pripravimo strukturo za odgovor
      update_payload.releIndex = index;

      if (index < 8)
      {

        // Posodobimo strukturo kanal z novim urnikom
        readUrnikPayload(received_urnik, index);
        // Prikaz nastavljenega urnika za debug
        Serial.printf("Nastavljen urnik za rele %d: Start %s (%d sec), End %s (%d sec)\n",
                      index + 1,
                      kanal[index].start,
                      kanal[index].start_sec,
                      kanal[index].end,
                      kanal[index].end_sec);

        // Shranjevanje v Preferences
        saveKanalToPreferences(index);
        Serial.printf("Urnik za rele %d shranjen v Preferences.\n", index + 1);
        update_payload.status = (uint8_t)AckStatus::ACK_OK;
      }
      else
      {
        update_payload.status = (uint8_t)AckStatus::ERR_INVALID_INDEX;
        Serial.println("Napaka: Neveljaven indeks releja za posodobitev urnika!");
      }

      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_UPDATE_URNIK, &update_payload, sizeof(update_payload));
      break;
    }

  //=======================================================================
  case CommandType::CMD_GET_URNIK:
    // Pošlji urnik enega releja
    {
      UrnikPayload urnik_payload;                                   // Pripravimo strukturo za odgovor
      memcpy(&urnik_payload, packet.payload, sizeof(UrnikPayload)); // kopiramo prejeto strukturo
      int releIndex = urnik_payload.releIndex;                      // Preberi indeks releja iz prejete strukture

      if (releIndex >= 8)
      {
        // uint8_t error_status = (uint8_t)AckStatus::ERR_INVALID_PARAMETER;
        urnik_payload.status = (uint8_t)AckStatus::ERR_INVALID_INDEX; // napaka - neveljaven indeks
      }
      else
      {
        // populateUrnikPayload(urnik_payload, releIndex); // Napolni urnik_payload z ustreznimi podatki
        urnik_payload.releIndex = releIndex;
        urnik_payload.startTimeSec = kanal[releIndex].start_sec;
        urnik_payload.endTimeSec = kanal[releIndex].end_sec;
        urnik_payload.status = (uint8_t)AckStatus::ACK_OK; // status OK
      }

      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_URNIK, &urnik_payload, sizeof(urnik_payload));
      break;
    }

  //=======================================================================
  case CommandType::CMD_TEST:
    // Test komunikacije
    {
      Serial.println("Prejet CMD_TEST ukaz.");
      uint8_t test_response = 0x42; // Primer testnega odgovora
      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_TEST, &test_response, sizeof(test_response));
      break;
    }

    //=======================================================================
    case CommandType::CMD_INIT_DONE:
    {
      Serial.println("Prejet CMD_INIT_DONE ukaz. Inicializacija zaključena.");
      uint8_t init_response = 0x01; // Primer odgovora
      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_INIT_DONE, &init_response, sizeof(init_response));
      init_done_flag = true; // Nastavi zastavico, da je inicializacija zaključena
      break;
    }

    //=======================================================================
    // Dodajte tukaj obdelavo drugih ukazov, kot so

    // TODO: Dodajte še ostale ukaze (CMD_GET_URNIK, itd.)

    default:
    {
      Serial.println("Neznan ukaz v prejetem paketu.");
      uint8_t error_status = (uint8_t)AckStatus::ERR_INVALID_COMMAND;
      send_response_to_bridge(packet.messageId, CommandType::RESPONSE_ACK, &error_status, sizeof(error_status));
      break;
    }
  }
}

// ----------------------------------------------------------------------------
// --- POPRAVLJENO ROBUSTNO BRANJE SERIJSKEGA PORTA ---
// ----------------------------------------------------------------------------
void ReadFromRelaySerial()
{

  while (relaySerial.available() > 0)
  {
    // Preberemo prvi bajt iz bufferja
    uint8_t firstByte = relaySerial.read();

    // Preverimo, ali se ujema s prvim bajtom našega Sync Worda
    if (firstByte == (LORA_SYNC_WORD & 0xFF))
    {
      // Prvi bajt se ujema. Počakamo na drugega.
      // Uporabimo kratek timeout, da se ne zataknemo za vedno.
      unsigned long startTime = millis();
      while (relaySerial.available() == 0 && millis() - startTime < 10) {
        // Počakamo na drugi bajt
        delay(1);
      }

      // Če drugi bajt ni prišel, nadaljujemo z zunanjim while ciklom
      if (relaySerial.available() == 0) {
        continue;
      }

      // Preberemo drugi bajt
      uint8_t secondByte = relaySerial.read();

      // Preverimo, ali se drugi bajt ujema z drugim bajtom našega Sync Worda
      if (secondByte == ((LORA_SYNC_WORD >> 8) & 0xFF))
      {
        // USPEH! Našli smo celoten Sync Word (0xCD, 0xAB).
        // Sedaj moramo prebrati preostanek paketa.
        const int remainingBytes = sizeof(LoRaPacket) - 2; // -2 za sync word
        uint8_t packetBuffer[sizeof(LoRaPacket)];

        // Shranimo sync word, ki smo ga že prebrali
        packetBuffer[0] = firstByte;
        packetBuffer[1] = secondByte;

        // Počakamo, da prispe preostanek paketa
        startTime = millis();
        while (relaySerial.available() < remainingBytes && millis() - startTime < 100) {
            delay(1);
        }

        if (relaySerial.available() < remainingBytes) {
            // Timeout! Nismo prejeli celotnega paketa. Zavržemo, kar smo našli.
            Serial.println("Napaka: Timeout po najdbi Sync Worda. Paket ni popoln.");
            continue;
        }

        // Preberemo preostanek paketa v naš buffer
        relaySerial.readBytes(packetBuffer + 2, remainingBytes);

        // Sedaj imamo celoten paket v `packetBuffer`.
        // Prepišemo ga v LoRaPacket strukturo in obdelamo.
        // LoRaPacket receivedPacket;
        memcpy(&receivedPacket, packetBuffer, sizeof(LoRaPacket));
        
        handle_packet_from_bridge(receivedPacket);

        // Nadaljujemo z zanko, da takoj preverimo za naslednji paket
        continue;
      }
    }
    
    // Če smo prišli do sem, prvi bajt ni bil del Sync Worda (ali pa drugi ni bil pravilen).
    // To so "smeti", ki jih moramo zavreči.
    Serial.print("Zavracam smeti: 0x");
    Serial.println(firstByte, HEX);
  }

}

// ----------------------------------------------------------------------------
// Funkcija za upravljanje ponovnega pošiljanja obvestil
// ----------------------------------------------------------------------------
void manage_notification_retries() {
    if (!notification_awaiting_ack) {
        return; // Ničesar za narediti
    }

    // Preveri, ali je potekel čas za ACK
    if (millis() - last_notification_send_time > NOTIFICATION_ACK_TIMEOUT) {
        if (notification_retry_count < MAX_NOTIFICATION_RETRIES) {
            // Čas je potekel, poskusi ponovno
            notification_retry_count++;
            last_notification_send_time = millis();

            Serial.printf("ACK ni bil prejet. Ponovno pošiljam obvestilo (poskus %d): ID: %u\n", notification_retry_count + 1, unacknowledged_notification.messageId);

            if (relaySerial.availableForWrite() >= sizeof(LoRaPacket)) {
                relaySerial.write((const uint8_t*)&unacknowledged_notification, sizeof(LoRaPacket));
                error_flag = false; // Ponovno pošiljanje uspešno
            } else {
                Serial.println("Napaka: Ni dovolj prostora v bufferju za ponovno pošiljanje.");
                error_flag = true; // Nastavi napako
            }
        } else {
            // Doseženo največje število poskusov
            Serial.printf("Napaka: Obvestilo ID %u ni bilo potrjeno po %d poskusih. Obupujem.\n", unacknowledged_notification.messageId, MAX_NOTIFICATION_RETRIES + 1);
            notification_awaiting_ack = false; // Prenehaj s poskusi
            error_flag = true; // Nastavi napako
        }
    }
}

// ----------------------------------------------------------------------------
// Main setup and loop functions
// ----------------------------------------------------------------------------
void setup()
{
  // Initialize the LED, onboard LED, and button
  pinMode(led.pin, OUTPUT);         // LED is output
  pinMode(onboard_led.pin, OUTPUT); // onboard LED is output
  pinMode(button.pin, INPUT);       // button is input and is pulled-up with a external resistor

  Serial.begin(115200);
  while (!Serial)
    delay(10); // Wait for Serial port to connect

  // Inicializacija I2C vodila - DODAJTE TO
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("I2C vodilo inicializirano.");

  // Nastavi okoljsko spremenljivko TZ za Srednjeevropski čas (CET/CEST)
  // CET-1CEST,M3.5.0,M10.5.0/3
  // CET: Ime standardnega časa
  // -1:  Offset od UTC. Za UTC+1 je vrednost -1.
  // CEST: Ime poletnega časa
  // M3.5.0: Pravilo za začetek poletnega časa (zadnja nedelja v marcu)
  // M10.5.0/3: Pravilo za konec poletnega časa (zadnja nedelja v oktobru ob 3:00)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset(); // Uveljavi nastavitve časovnega pasu
  Serial.println("Casovni pas nastavljen na CET/CEST.");
  // -------------------------------------------------

  // Naloži podatke iz Preferences
  loadKanalFromPreferences();

  // Initialize relays
  initRelays();

  // UART1 za komunikacijo z ESP32-PICO-D4
  relaySerial.begin(9600, SERIAL_8N1, RELAY_RX_PIN, RELAY_TX_PIN); // UART1 za komunikacijo z ESP32-PICO-D4
  // Počakaj trenutek, da se serijska povezava stabilizira
  delay(100);

  // Počisti prejemni buffer
  while (relaySerial.available() > 0)
  {
    relaySerial.read(); // Preberi in ignoriraj vse podatke v bufferju
  }
  Serial.println("Serijski buffer za relaySerial je bil očiščen.");
  Serial.println("ESP32-WROOM-32E Sprejemnik pripravljen.");

  // Print the local time
  print_local_time();

  // Initialize the SHT4x sensor
  initSHT4x();  // Initialize the SHT4x sensor
  initINA3221();  // Initialize the INA3221 sensor
  Read_SHT4X(1); // read the SHT4x sensor
  Read_INA3221(); // read the INA3221 sensor

  // Print the schedule to the serial monitor
  // PrintUrnik();

  milisekunda = millis(); // initialize the milisekunda variable
  lastCheckTime = millis(); // Inicializacija časovnika za checkRelayStates

  // Ker sistemski čas še ni nastavljen, pošljemo zahtevo za nastavitev časa
  //send_notification_to_bridge(CommandType::NOTIFY_TIME_REQUEST, NULL, 0);

  // Ker se je board ponovno zagnal, pošljemo zahtevo za inicijalizacijo
  send_notification_to_bridge(CommandType::NOTIFY_RESET_OCCURED, NULL, 0);
  // Pričakujemo, da se vrne ACK za to obvestilo
}

// ----------------------------------------------------------------------------
// Main loop function
// ----------------------------------------------------------------------------
void loop()
{
  // vsako sekundo preveri stanje relejev glede na urnik
  if (millis() - lastCheckTime >= 1000)
  {
    lastCheckTime = millis(); // Ponastavi časovnik

    // Tukaj obravnavaj stanje relejev samo če je inicicializacija zaključena, sistemski čas nastavljen in ni v teku obdelava paketa
    if (init_done_flag && system_time_set && !obdelava_paketa)
      if (checkRelayStates()) // Če je prišlo do spremembe stanja
      {
        // Pripravi payload s trenutnim stanjem vseh relejev
        RelayStatusPayload status_payload;
        status_payload.relayStates = relay_states_bitmask;

        // funkcija že vsebuje logiko za preverjanje čakalne vrste
        obdelava_paketa = true; // Nastavi zastavico da obdelujemo paket
        send_notification_to_bridge(CommandType::NOTIFY_RELAY_STATE_CHANGED, &status_payload, sizeof(status_payload)); // Pošlji obvestilo z novim stanjem vseh relejev  
      }

     
  }

  // Handle button state
  handleButton(); // preveri stanje gumba

  // Upravljaj s ponovnim pošiljanjem obvestil, ki čakajo na ACK
  manage_notification_retries();

  // Preberi podatke iz relaySerial in obdelaj pakete
  ReadFromRelaySerial();

  //vsakih 10sec preberi INA3221 senzor
  if (millis() - lastINA3221ReadTime >= 10000) {
    lastINA3221ReadTime = millis();
    Read_INA3221();
  }

  if (error_flag)
    onboard_led.blink(200, 200); // LED blinka z napako
  else
    onboard_led.blink(500, 500); // LED blinka vsakih 500 ms

}
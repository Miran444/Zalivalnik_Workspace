/*

  This example is intended to run on two SX126x radios,
  and send packets between the two.

   This example transmits packets using SX1276 LoRa radio module.
   Each packet contains up to 256 bytes of data, in the form of:
    - Arduino String
    - null-terminated char array (C-string)
    - arbitrary binary data (byte array)

   For full API reference, see the GitHub Pages
   https://jgromes.github.io/RadioLib/
*/
#include "Lora_master.h"
#include "Firebase_z.h"      // Naša lastna knjižnica za Firebase funkcije
#include "display_manager.h" // Knjižnica za upravljanje z zaslonom
#include "wifi_manager.h"    // Knjižnica za upravljanje z WiFi in NTP
#include "unified_lora_handler.h"
#include "message_protocol.h"
#include "sensor_queue.h"

#define IS_MASTER
#define DEFAULT_SENSOR_READ_INTERVAL_MINUTES 5 // Privzeti interval branja senzorjev v minutah
// // --- NOVO: Struktura in čakalna vrsta za Firebase naloge ---
// enum class FirebaseTaskType {
//     UPDATE_SENSORS,
//     UPDATE_INA,
//     UPDATE_RELAY_STATE
// };

// struct FirebaseQueueItem {
//     FirebaseTaskType taskType;
//     union {
//         SensorDataPayload sensorData;
//         INA3221_DataPayload inaData;
//         RelayStatePayload relayState;
//     } data;
//     unsigned long timestamp;
// };
// ---------------------------------------------------------

// --- GLOBALNE SPREMENLJIVKE ---

// Čakalna vrsta za ukaze za rele modul
bool firebaseUpdatePending = false;  // Ali imamo čakajočo posodobitev iz Firebase?
ChannelUpdateData pendingUpdateData; // Podatki, ki čakajo na pošiljanje
volatile bool newChannelDataAvailable;
ChannelUpdateData channelUpdate;

// // spremenljivke za LORA TIMEOUT (ko pošljemo zahtevo in čakamo na odgovor)
// unsigned long lora_response_timeout_start = 0;
// const unsigned long LORA_RESPONSE_TIMEOUT_MS = 3000; // 3 sekunde za odgovor

// // --- NOVO: NABIRALNIK ZA ODHODNE PAKETE IN PONOVNE POSKUSE ---
// LoRaPacket last_sent_packet;                   // Hranimo zadnji paket, ki čaka na odgovor
// bool is_waiting_for_critical_response = false; // Zastavica, ki pove, da paket v nabiralniku čaka na odgovor
// uint8_t lora_retry_count = 0;                  // Števec ponovnih poskusov za splošne ukaze
// const uint8_t MAX_LORA_RETRIES = 3;            // Največje število ponovnih poskusov

// --- SPREMENLJIVKE ZA ČASOVNIK IN URNIK ---
uint32_t timestamp;           // Trenutni timestamp (vsaj enkrat na sekundo posodobljen)
uint32_t secFromMidnight = 0; // Čas od polnoči v sekundah
uint32_t Interval_mS = 0;     // Interval v milisekundah
unsigned long lastSensorRead = 0; // Čas zadnjega branja senzorjev

// --- SPREMENLJIVKE ZA SEKVENČNI AVTOMAT (STATE MACHINE) ---

// Definicija stanj za inicializacijo Rele modula
enum class InitState
{
  IDLE,                       // Ne dela ničesar
  READ_FIREBASE_INTERVAL,     // Pošlji zahtevo za branje intervala iz Firebase
  WAIT_FOR_INTERVAL_RESPONSE, // Čakaj na odgovor za interval
  START,                      // Začetek inicializacije
  SEND_TIME,                  // Pošlji ukaz za čas
  WAIT_FOR_TIME_RESPONSE,     // Čakaj na odgovor za čas
  SEND_STATUS_REQUEST,        // Pošlji zahtevo za status
  WAIT_FOR_STATUS_RESPONSE,   // Čakaj na odgovor za status
  SEND_SENSORS_REQUEST,       // Pošlji zahtevo za senzorje
  WAIT_FOR_SENSORS_RESPONSE,  // Čakaj na odgovor za senzorje
  SEND_URNIK_REQUEST,         // Pošlji zahtevo za urnik
  WAIT_FOR_URNIK_RESPONSE,    // Čakaj na odgovor za urnik
  READ_FROM_FIREBASE,         // Pošlji zahtevo na Firebase za en kanal
  WAIT_FOR_FIREBASE_RESPONSE, // Čakaj na odgovor od Firebase
  COMPARE_AND_SEND_LORA,      // Primerjaj urnike in po potrebi pošlji LoRa ukaz
  WAIT_FOR_UPDATE_URNIK,      // Čakaj na potrditev RESPONSE_UPDATE_URNIK od Rele modula
  FIREBASE_UPDATE_STATUS,     // Pošlji zahtevo za posodobitev stanja kanala v Firebase
  WAIT_FOR_FIREBASE_UPDATE_STATUS, // Čakaj na odgovor od Firebase za posodobitev stanja kanala
  INIT_DONE,                  // Inicializacija končana
  WAIT_FOR_INIT_COMPLETE,     // Čakaj na dokončanje inicializacije
  DONE,                       // Inicializacija končana
  STOP,                       // Zaustavi inicializacijo
  ERROR                       // Napaka med inicializacijo
};

InitState currentInitState = InitState::IDLE; // Trenutno stanje avtomata
uint8_t currentChannelInProcess = 0;          // Kateri kanal (0-7) trenutno obdelujemo

// --- SPREMENLJIVKE ZA TIMEOUT PRI INICIALIZACIJI ---
unsigned long initState_timeout_start = 0;           // Časovnik za timeout pri čakanju na odgovor
const unsigned long INIT_RESPONSE_TIMEOUT_MS = 5000; // 5 sekund za odgovor
const unsigned long FIREBASE_RESPONSE_TIMEOUT_MS = 15000; // 15 sekund za odgovor iz Firebase

// --- SPREMENLJIVKE ZA PONOVNE POSKUSE ---
const uint8_t MAX_INIT_RETRIES = 3; // Največje število ponovnih poskusov
uint8_t init_retry_count = 0;       // Trenutni števec ponovnih poskusov

// zastavice za spremljanje prejetih odgovorov
bool lora_response_received = false;     // Ali je bil prejet Lora odgovor
bool firebase_response_received = false; // Ali smo prejeli odgovor iz Firebase?
// LoRaPacket last_received_packet;         // Shranimo zadnji prejeti paket za obdelavo v avtomatu

// bool firebase_sensor_update = false; // zastavica za posodobitev senzorjev v Firebase
bool reset_occured = false; // zastavica, da je prišlo do ponovnega zagona na Rele modulu
// bool system_time_set = false; // zastavica, da je bil sistemski čas nastavljen
// bool notification_awaiting_ack = false; // zastavica, da čakamo na potrditev obvestila

// -- SPREMENLJIVKE IZ INA3221 SENZORJA --
// float ina3221_bus_voltage = 0.0;   // Napetost na I2C vodilu
// float ina3221_shunt_voltage = 0.0; // Napetost na shunt uporu
// float ina3221_current = 0.0;       // Tok, izmerjen z INA3221
// float ina3221_power = 0.0;         // Moč, izmerjena z INA3221
// float ina3221_total_current = 0.0;    // Tok na obremenitvi iz INA3221
// float ina3221_shunt_voltage_sum = 0.0; // Skupna napetost na shunt uporih

// Globalna spremenljivka za shranjevanje podatkov iz INA3221
INA3221_DataPayload ina_data;

// -------------------------------------------------------------
// Definition of the Kanal (Relay) component
// // Struktura za shranjevanje podatkov o relejih
// struct Kanal
// {
//   bool state;    // Stanje releja (vklopljen/izklopljen)
//   char start[6]; // Začetni čas (HH:MM)
//   int start_sec; // Začetni čas v sekundah od polnoči
//   char end[6];   // Končni čas (HH:MM)
//   int end_sec;   // Končni čas v sekundah od polnoči
// };

Kanal kanal[8] = {
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0}};

Kanal firebase_kanal[8] = {
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0},
    {false, "00:00", 0, "00:00", 0}}; // Končamo z definicijo Firebase kanalov

// globalne spremenljivke za podatke senzorjev
float Temperature = 0.0;        // trenutna temperatura
float Humidity = 0.0;           // trenutna vlažnost
float SoilMoisture = 0.0;       // trenutna vlažnost tal
float BatteryVoltage = 0.0;     // trenutna napetost baterije
float CurrentConsumption = 0.0; // trenutna poraba toka
float WaterConsumption = 0.0;   // skupna poraba vode

// spremenljivke za Firebase
FirebaseApp app;
WiFiClientSecure ssl_client;
WiFiClientSecure stream_ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient streamClient(stream_ssl_client);
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;
AsyncResult databaseResult;
AsyncResult streamResult; // Za Firebase streaming

bool firebase_init_flag = false; // Ali je Firebase inicializiran?
bool taskComplete = false;
unsigned long lastSync = 0; // Čas zadnje sinhronizacije s Firebase

int get_intValue = 0;        // Variables get from the database
uint16_t messageCounter = 0; // Števec poslanih sporočil

bool init_done_flag = false; // zastavica za zaključek inicializacije

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

// Led    led = {LED_PIN, false}; // LED na protoboardu
Led onboard_led = {BOARD_LED, false}; // onboard LED

// ----------------------------------------------------------------------------
// --- Pomožne funkcije ---
uint16_t calculate_crc(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t j = 0; j < 8; j++)
    {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}
// ----------------------------------------------------------------------------
// Izpis v binarnem načinu
void printBinary(uint16_t value)
{
  for (int i = 15; i >= 0; i--)
  {
    Serial.print(bitRead(value, i));
  }
  Serial.println();
}

//------------------------------------------------------------------------------------------------------------------------
// Ta funkcija prejme String, ki lahko vsebuje narekovaje (npr. ""57960""),
// odstrani narekovaje in pretvori preostanek v integer.
int ParseToInt(String payload)
{
  // Najprej odstranimo morebitne presledke na začetku in koncu
  payload.trim();

  // Preverimo, ali je niz obdan z narekovaji
  if (payload.startsWith("\"") && payload.endsWith("\""))
  {
    // Odstranimo prvi in zadnji znak (narekovaja)
    String numberString = payload.substring(1, payload.length() - 1);
    return numberString.toInt();
  }
  else
  {
    // Če niz ni obdan z narekovaji, poskusimo s standardno pretvorbo
    return payload.toInt();
  }
}

// ----------------------------------------------------------------------------
// Funkcija za pretvorbo časa v int obliki sekunde od začetka dneva v obliko "HH:MM"
// --- SPREMENJENA, BOLJ UČINKOVITA VERZIJA ---
void formatSecondsToTime(char *buffer, size_t bufferSize, int seconds)
{
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  snprintf(buffer, bufferSize, "%02d:%02d", hours, minutes);
}

// ----------------------------------------------------------------------------
// Funkcija za pretvorbo minut intervala v milisekunde
void set_Interval(uint8_t minutes)
{
  Interval_mS = minutes * 60UL * 1000UL; // Pretvorba minut v milisekunde
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za pošiljanje stanja senzorjev na serijski monitor
void PrikaziStanjeSenzorjevNaSerial()
{
  char buffer[32];

  Serial.print("--- Stanje Senzorjev ob ");
  printLocalTime();
  Serial.print("Temperatura: "); Serial.print(Temperature); Serial.print("°C");
  Serial.print(", Vlažnost: "); Serial.print(Humidity); Serial.print("%");
  Serial.print(", Tla: "); Serial.print(SoilMoisture); Serial.print("%");
  Serial.print(", Baterija: "); Serial.print(BatteryVoltage); Serial.print("V");
  Serial.print(", Tok: "); Serial.print(CurrentConsumption); Serial.print("A");
  Serial.print(", Poraba vode: "); Serial.print(WaterConsumption); Serial.println("L");
  Serial.println("----------------------");
  
  snprintf(buffer, sizeof(buffer), "T:%.1fC H:%.1f%%", Temperature, Humidity);
  displayLogOnLine(LINE_ERROR_WARNING, buffer);
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za pošiljanje stanja kanalov na serijski monitor
void PrikaziStanjeRelejevNaSerial()
{
  // Prikažemo stanje kanalov na serialnem monitorju
  Serial.println("--- Stanje Relejev ---");
  for (int i = 0; i < 8; i++)
  {
    Serial.print("Kanal "); Serial.print(i + 1); Serial.print(": ");
    Serial.print("Stanje: "); Serial.print(kanal[i].state ? "ON" : "OFF");
    Serial.print(", Urnik: "); Serial.print(kanal[i].start); Serial.print(" - "); Serial.print(kanal[i].end);
    Serial.print(", Start sec: "); Serial.print(kanal[i].start_sec); Serial.print(", End sec: "); Serial.println(kanal[i].end_sec);
  }

  Serial.println("----------------------");
}

//---------------------------------------------------------------------------------------------------------------------
// Funkcija za pripravo in pošiljanje LoRa paketa
void Lora_prepare_and_send_packet(CommandType cmd, const void *payload_data, size_t payload_size)
{
  LoRaPacket packet;
  packet.syncWord = LORA_SYNC_WORD;
  packet.messageId = messageCounter++;
  packet.command = cmd;
  memset(packet.payload, 0, sizeof(packet.payload));
  if (payload_data != nullptr && payload_size > 0)
  {
    memcpy(packet.payload, payload_data, payload_size);
  }
    // Preveri porabo sklada
    UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("[Lora] Preostala velikost sklada: %d bajtov\n", stackLeft); 

  char logBuffer[32];
  snprintf(logBuffer, sizeof(logBuffer), "OUT ID: %d, CMD: %d", packet.messageId, (uint8_t)packet.command);
  Serial.println(logBuffer);
  // displayLogOnLine(4, logBuffer);
  packet.crc = calculate_crc((const uint8_t *)&packet, offsetof(LoRaPacket, crc));

  // Če smo v načinu inicializacije ali pošiljamo iz SENSOR_QUEUE preskočimo nastavitev konteksta za ponovni poskus
  if (lora_get_context() != LoRaContext::INITIALIZATION && lora_get_context() != LoRaContext::SENSOR_QUEUE)
  {
    // Nastavi kontekst na čakanje na odgovor
    lora_set_context(LoRaContext::WAITING_FOR_RESPONSE);
  }

  // ENOSTAVNO: Samo pošlji - vsa logika je v lora_send_packet!
  if (!lora_send_packet(packet))
  {
    Serial.println("[MAIN] Napaka pri pošiljanju paketa!");
    lora_set_context(LoRaContext::IDLE); // Reset konteksta
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Funkcija za pripravo in pošiljanje ODGOVORA na prejet paket
void Lora_prepare_and_send_response(uint16_t request_id, CommandType cmd, const void *payload_data, size_t payload_size)
{
  LoRaPacket packet;
  packet.syncWord = LORA_SYNC_WORD;
  packet.messageId = request_id; // Uporabi ID iz zahteve!
  packet.command = cmd;

  memset(packet.payload, 0, sizeof(packet.payload));
  if (payload_data != nullptr && payload_size > 0)
  {
    memcpy(packet.payload, payload_data, payload_size);
  }

  packet.crc = calculate_crc((const uint8_t *)&packet, offsetof(LoRaPacket, crc));

  // Odgovor ni kritičen, ne čakamo na potrditev
  // lora_set_waiting_for_response(false);
  lora_set_context(LoRaContext::JUST_ACK);

    if (!lora_send_packet(packet))
  {
    Serial.println("[MAIN] Napaka pri pošiljanju paketa!");
    lora_set_context(LoRaContext::IDLE); // Reset konteksta
  }
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za pošiljanje UTC timestamp za sinhronizacijo ure na LoRa rele modul
// Pošlje paket z ukazom CMD_SET_TIME in trenutnim Unix časovnim žigom v payloadu.

void Rele_sendUTC()
{
  Serial.println("Pripravljam paket za sinhronizacijo casa (CMD_SET_TIME)...");

  // 1. Pripravimo payload s trenutnim časom.
  TimePayload time_payload;
  // shranimo tudi čas od polnoči za poznejše preverjanje
  secFromMidnight = getCurrentSeconds(); // Pridobimo trenutni čas v sekundah od polnoči
  Serial.printf("  Čas od polnoči: %u sekund\n", secFromMidnight);
  time_payload.unix_time = getTime(); // Pridobimo trenutni Unix časovni žig.

  Serial.printf("  Posiljam timestamp: %u\n", time_payload.unix_time);

  // 2. Pošljemo paket z uporabo obstoječe funkcije.
  Lora_prepare_and_send_packet(CommandType::CMD_SET_TIME, &time_payload, sizeof(time_payload));
  // Odgovor v Lora_handle_received_packet, - RESPONSE_TIME.
}

//--------------------------------------------------------------------------------------
// Funkcija za pošiljanje ukaza za branje senzorjev
void Rele_readSensors()
{
  Serial.println("Pripravljam paket za branje senzorjev (CMD_GET_SENSORS)...");

  // Pošljemo paket z ukazom CMD_GET_SENSORS.
  // Ta ukaz ne potrebuje nobenih podatkov v payloadu.
  Lora_prepare_and_send_packet(CommandType::CMD_GET_SENSORS, nullptr, 0);
  // Odgovor v Lora_handle_received_packet, - RESPONSE_SENSORS.
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za pošiljanje ukaza za branje INA podatkov
void Read_INA()
{
  Serial.println("Pripravljam paket za branje INA podatkov (CMD_GET_INA_DATA)...");

  // Pošljemo paket z ukazom CMD_GET_INA_DATA.
  // Ta ukaz ne potrebuje nobenih podatkov v payloadu.
  Lora_prepare_and_send_packet(CommandType::CMD_GET_INA_DATA, nullptr, 0);
  // Odgovor v Lora_handle_received_packet, - RESPONSE_INA_DATA.
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za preverjanje stanja relejev
void Rele_readRelaysStatus()
{
  Serial.println("Preverjanje stanja relejev (CMD_GET_STATUS)...");
  // Pošljemo paket z ukazom CMD_GET_STATUS.
  // Ta ukaz ne potrebuje nobenih podatkov v payloadu.
  Lora_prepare_and_send_packet(CommandType::CMD_GET_STATUS, nullptr, 0);
  // Odgovor v Lora_handle_received_packet, - RESPONSE_STATUS.
}

//---------------------------------------------------------------------------------------------------------------------
// Funkcija za branje urnikov relejev iz Rele modula
void Rele_readRelayUrnik(uint8_t kanalIndex)
{
  Serial.printf("Branje urnika za kanal %d (CMD_GET_URNIK)...\n", kanalIndex + 1);

  // Pripravimo payload z indeksom kanala
  UrnikPayload urnik_request;
  urnik_request.releIndex = kanalIndex; // Kanali so indeksirani od 0 do 7

  // Pošljemo paket z uporabo obstoječe funkcije.
  Lora_prepare_and_send_packet(CommandType::CMD_GET_URNIK, &urnik_request, sizeof(urnik_request));
  // Odgovor v Lora_handle_received_packet, - RESPONSE_URNIK.
}

//-----------------------------------------------------
// Funkcija za update urnikov relejev v Rele modulu
void Rele_updateRelayUrnik(uint8_t index, uint32_t startSec, uint32_t endSec)
{
  Serial.printf("Posiljanje posodobitve urnika za kanal %d (CMD_UPDATE_URNIK)...\n", index + 1);
  currentChannelInProcess = index; // shranimo kateri kanal posodabljamo
  // Pripravimo payload z novim urnikom za izbrani kanal
  UrnikPayload urnik_payload;
  urnik_payload.releIndex = index; // Kanali so indeksirani od 0 do 7
  urnik_payload.startTimeSec = startSec;
  urnik_payload.endTimeSec = endSec;

  Serial.printf("  Nov urnik - Start sec: %d, End sec: %d\n",
                urnik_payload.startTimeSec,
                urnik_payload.endTimeSec);

  // Pošljemo paket z uporabo obstoječe funkcije.
  Lora_prepare_and_send_packet(CommandType::CMD_UPDATE_URNIK, &urnik_payload, sizeof(urnik_payload));
  // Odgovor v Lora_handle_received_packet, - RESPONSE_UPDATE_URNIK.
}

//---------------------------------------------------------------------------------------------------------------------
// --- NOVA POMOŽNA FUNKCIJA ZA PREVERJANJE STANJA ČAKANJA V AVTOMATU ---
// Ta funkcija preveri, ali je bil prejet odgovor ali je potekel čas.
// Vrne `true`, če je avtomat prešel v novo stanje (uspeh, napaka ali ponovni poskus).
// Vrne `false`, če še vedno čakamo in ni prišlo do spremembe stanja.
bool check_init_wait_state(bool response_flag, InitState success_state, InitState retry_state, const char *wait_description)
{
  // 1. PRIMER: USPEH - Prejeli smo odgovor
  if (response_flag)
  {
    init_retry_count = 0; // Uspeh! Ponastavimo števec poskusov.
    Serial.printf("[INIT] Uspeh pri čakanju na '%s'.\n", wait_description);
    currentInitState = success_state; // Premaknemo se na naslednje stanje
    return true;                      // Stanje se je spremenilo
  }

  // 2. PRIMER: TIMEOUT - Odgovora nismo prejeli v določenem času
  if (millis() - initState_timeout_start > INIT_RESPONSE_TIMEOUT_MS)
  {
    init_retry_count++; // Povečamo števec poskusov
    Serial.printf("[INIT] Timeout pri čakanju na '%s'. Poskus %d/%d...\n", wait_description, init_retry_count, MAX_INIT_RETRIES);

    // Preverimo, ali smo presegli število poskusov
    if (init_retry_count >= MAX_INIT_RETRIES)
    {
      Serial.println("[INIT] NAPAKA: Preseženo število poskusov. Prekinjam inicializacijo.");
      displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Fatal Timeout!");
      currentInitState = InitState::ERROR;
      lora_set_context(LoRaContext::IDLE);   // VRNI na normalno delovanje
      initState_timeout_start = millis(); // Ponastavimo časovnik za čakanje v ERROR stanju
    }
    else
    {
      // Nismo presegli poskusov, vrnemo se na prejšnje stanje za ponovni poskus.
      currentInitState = retry_state;
    }
    return true; // Stanje se je spremenilo
  }

  // 3. PRIMER: ČAKANJE - Ni ne odgovora ne timeouta.
  return false; // Še vedno čakamo, stanje se ni spremenilo.
}

//---------------------------------------------------------------------------------------------------------------------
// --- NOVA FUNKCIJA ZA UPRAVLJANJE SEKVENČNEGA AVTOMATA ---
void manageReleInitialization()
{

  // Avtomat se ne izvaja, če je končan, v mirovanju ali v stanju napake.
  if (currentInitState == InitState::IDLE || currentInitState == InitState::STOP)
  {
    return;
  }

  // Glavna logika avtomata
  switch (currentInitState)
  {
  // --- NOVO STANJE: Pošlji zahtevo za branje intervala ---
  //============================================================================
  case InitState::READ_FIREBASE_INTERVAL:
  //============================================================================
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Reading Interval...");
    
     // SPREMEMBA: Kličemo funkcijo direktno, ne uporabljamo več čakalne vrste.
    Firebase_readInterval();

    firebase_response_received = false; // Ponastavimo zastavico za odgovor
    initState_timeout_start = millis();      // Ponastavimo timer za timeout
    currentInitState = InitState::WAIT_FOR_INTERVAL_RESPONSE;
    break;


  // --- NOVO STANJE: Čakaj na odgovor za interval ---
  //============================================================================
  case InitState::WAIT_FOR_INTERVAL_RESPONSE:
  //============================================================================  
    if (firebase_response_received)
    {
      Serial.println("[INIT] Uspeh pri branju intervala.");
      currentInitState = InitState::START; // Nadaljujemo z naslednjim korakom (START)
      initState_timeout_start = millis(); // Ponastavimo timer za naslednji korak
    }
    else if (millis() - initState_timeout_start > FIREBASE_RESPONSE_TIMEOUT_MS)
    {
      Serial.println("[INIT] Timeout pri čakanju na 'odgovor za interval'. Uporabljam privzeto vrednost.");
      set_Interval(DEFAULT_SENSOR_READ_INTERVAL_MINUTES); // Nastavimo privzeto vrednost
      currentInitState = InitState::START; // Vseeno nadaljujemo z inicializacijo
    }
    break;

  //============================================================================
  case InitState::START:
    //============================================================================
    Serial.println("--- ZACENJAM INICIALIZACIJO RELE MODULA ---");
    lora_set_context(LoRaContext::INITIALIZATION); // NASTAVI kontekst inicializacije
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Starting...");
    currentInitState = InitState::SEND_TIME;
    break;

  //============================================================================
  case InitState::SEND_TIME:
    //============================================================================
    Serial.println("[INIT] Korak 1: Posiljam cas...");
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] UTC time");
    Rele_sendUTC();                     // Pošljemo ukaz za sinhronizacijo časa
    initState_timeout_start = millis(); // Zaženemo timer za timeout
    currentInitState = InitState::WAIT_FOR_TIME_RESPONSE;
    break;

  //============================================================================
  case InitState::WAIT_FOR_TIME_RESPONSE:
    //============================================================================
    if (check_init_wait_state(lora_response_received, InitState::SEND_SENSORS_REQUEST, InitState::SEND_TIME, "odgovor za čas"))
    {
      lora_response_received = false; // Vedno počistimo zastavico po obdelavi
      // if (init_retry_count == 2) // Če je bil to zadnji poskus
      // {
      //   init_retry_count = 0; // Ponastavimo števec poskusov zato da pošiljamo neprekinjeno

      // }
    }
    break;

  //============================================================================
  case InitState::SEND_SENSORS_REQUEST:
    //============================================================================
    Serial.println("[INIT] Korak 3: Posiljam zahtevo za senzorje...");
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Sensors");
    Rele_readSensors(); // Pošljemo zahtevo za branje senzorjev
    initState_timeout_start = millis();
    currentInitState = InitState::WAIT_FOR_SENSORS_RESPONSE;
    lora_response_received = false; // Resetiramo zastavico
    break;

  //============================================================================
  case InitState::WAIT_FOR_SENSORS_RESPONSE:
    //============================================================================
    if (check_init_wait_state(lora_response_received, InitState::SEND_URNIK_REQUEST, InitState::SEND_SENSORS_REQUEST, "odgovor za senzorje"))
    {
      lora_response_received = false;
      if (currentInitState == InitState::SEND_URNIK_REQUEST)
      {
        currentChannelInProcess = 0; // Resetiramo indeks samo ob uspehu
      }
    }
    break;

  //============================================================================
  case InitState::SEND_URNIK_REQUEST:
    //============================================================================
    Serial.printf("[INIT] Posiljam zahtevo za urnik kanala %d...\n", currentChannelInProcess + 1);

    char logBuffer[20];
    snprintf(logBuffer, sizeof(logBuffer), "[INIT] Urnik K%d", currentChannelInProcess + 1);
    displayLogOnLine(LINE_ERROR_WARNING, logBuffer);

    // Pošljemo zahtevo za EN sam urnik
    Rele_readRelayUrnik(currentChannelInProcess);

    initState_timeout_start = millis(); // Ponastavimo timeout za čakanje na odgovor
    currentInitState = InitState::WAIT_FOR_URNIK_RESPONSE;
    lora_response_received = false; // Resetiramo zastavico
    break;

  //============================================================================
  case InitState::WAIT_FOR_URNIK_RESPONSE:
    //============================================================================
    if (check_init_wait_state(lora_response_received, InitState::SEND_URNIK_REQUEST, InitState::SEND_URNIK_REQUEST, "odgovor za urnik"))
    {
      lora_response_received = false;
      // Posebna logika za to stanje
      if (currentInitState == InitState::SEND_URNIK_REQUEST)
      { // Če je bil klic uspešen
        currentChannelInProcess++;
        if (currentChannelInProcess >= 8)
        {
          Serial.println("[INIT] Vsi urniki uspesno prebrani.");
          currentInitState = InitState::READ_FROM_FIREBASE;
          currentChannelInProcess = 0;
        }
      }
    }
    break;

  //============================================================================
  case InitState::READ_FROM_FIREBASE:
    //============================================================================
    Serial.printf("[INIT] Branje urnika iz Firebase za kanal %d...\n", currentChannelInProcess + 1);
    
    char logBufferFb[20];
    snprintf(logBufferFb, sizeof(logBufferFb), "[INIT] Branje K%d", currentChannelInProcess + 1);
    displayLogOnLine(LINE_ERROR_WARNING, logBufferFb);

    Firebase_readKanalUrnik(currentChannelInProcess); // Pošljemo zahtevo za branje urnika iz Firebase za trenutni kanal

    currentInitState = InitState::WAIT_FOR_FIREBASE_RESPONSE;
    initState_timeout_start = millis(); // za timeout
    firebase_response_received = false; // Resetiramo zastavico
    break;

  //============================================================================
  case InitState::WAIT_FOR_FIREBASE_RESPONSE:
    //============================================================================
    // Za Firebase uporabimo `firebase_response_received` zastavico
    if (check_init_wait_state(firebase_response_received, InitState::READ_FROM_FIREBASE, InitState::READ_FROM_FIREBASE, "odgovor iz Firebase"))
    {
      firebase_response_received = false;
      // Posebna logika za to stanje
      if (init_retry_count == 0)  // Če je bil klic uspešen
      { 
        if (currentInitState == InitState::READ_FROM_FIREBASE)  // nam pove da smo v inicializaciji
        {        
          currentChannelInProcess++;
          if (currentChannelInProcess >= 8)
          {
            Serial.println("[INIT] Vsi urniki iz Firebase uspesno prebrani.");
            currentInitState = InitState::COMPARE_AND_SEND_LORA;
            currentChannelInProcess = 0;
          }
        }
      }
      else
      { // Če je bil klic neuspešen (ponovni poskus)
        // currentChannelInProcess ostane enak, da ponovimo branje za isti kanal
      }
    }
    break;

  //============================================================================
  case InitState::COMPARE_AND_SEND_LORA:
    //============================================================================
    // Primerjamo podatke iz Firebase (firebase_kanal) z lokalnimi (kanal)

    if (currentChannelInProcess >= 8)
    {
      // Končali smo z vsemi kanali, obvestimo Rele, da je inicializacija končana
      currentInitState = InitState::SEND_STATUS_REQUEST;
      currentChannelInProcess = 0; // Resetiramo za morebitno kasnejšo uporabo
    }
    else
    {
      if (firebase_kanal[currentChannelInProcess].start_sec != kanal[currentChannelInProcess].start_sec ||
          firebase_kanal[currentChannelInProcess].end_sec != kanal[currentChannelInProcess].end_sec)
      {
        Serial.printf("[INIT] Razlika za kanal %d. Posiljam posodobitev na Rele...\n", currentChannelInProcess + 1);

        Rele_updateRelayUrnik(
            currentChannelInProcess,
            firebase_kanal[currentChannelInProcess].start_sec,
            firebase_kanal[currentChannelInProcess].end_sec);

        currentInitState = InitState::WAIT_FOR_UPDATE_URNIK;
        initState_timeout_start = millis();
        lora_response_received = false; // Resetiramo zastavico
        // Odgovor v Lora_handle_received_packet - RESPONSE_UPDATE_URNIK.
      }
      else // Ni razlike, gremo direktno na naslednji kanal
      {
        Serial.printf("[INIT] Urnik za kanal %d je ze usklajen.\n", currentChannelInProcess + 1);
        currentChannelInProcess++;
        currentInitState = InitState::COMPARE_AND_SEND_LORA; // Naslednji krog
      }
    }

    break;
  //============================================================================
  case InitState::WAIT_FOR_UPDATE_URNIK:
    //============================================================================
    // Čakamo na odgovor iz `Lora_handle_received_packet` - RESPONSE_UPDATE_URNIK.
    if (lora_response_received)
    {
      lora_response_received = false;
      currentChannelInProcess++;                           // Povečamo indeks za naslednji kanal
      currentInitState = InitState::COMPARE_AND_SEND_LORA; // Naslednji krog
    }

    break;


  //============================================================================
  case InitState::SEND_STATUS_REQUEST:
    //============================================================================
    Serial.println("[INIT] Korak 2: Posiljam zahtevo za status relejev...");
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Status");
    Rele_readRelaysStatus();
    initState_timeout_start = millis();
    currentInitState = InitState::WAIT_FOR_STATUS_RESPONSE;
    break;

  //============================================================================
  case InitState::WAIT_FOR_STATUS_RESPONSE:
    //============================================================================
    if (check_init_wait_state(lora_response_received, InitState::FIREBASE_UPDATE_STATUS, InitState::SEND_STATUS_REQUEST, "odgovor za status"))
    { 
      lora_response_received = false;
    }
    // Odgovor v Lora_handle_received_packet, - RESPONSE_STATUS.

    break;

  //============================================================================
  case InitState::FIREBASE_UPDATE_STATUS:
    //============================================================================
    Serial.printf("[INIT] Posodabljanje stanja v Firebase za kanal %d...\n", currentChannelInProcess + 1);
    
    char logBufferUpdate[24];
    snprintf(logBufferUpdate, sizeof(logBufferUpdate), "[INIT] Posodabljanje K%d", currentChannelInProcess + 1);
    displayLogOnLine(LINE_ERROR_WARNING, logBufferUpdate);

    Firebase_Update_Relay_State(currentChannelInProcess + 1, kanal[currentChannelInProcess].state); // Pošljemo zahtevo za posodobitev stanja v Firebase za trenutni kanal

    currentInitState = InitState::WAIT_FOR_FIREBASE_UPDATE_STATUS;
    initState_timeout_start = millis(); // za timeout
    firebase_response_received = false; // Resetiramo zastavico
    break;

  //============================================================================
  case InitState::WAIT_FOR_FIREBASE_UPDATE_STATUS:
    //============================================================================
    // Za Firebase uporabimo `firebase_response_received` zastavico
    if (check_init_wait_state(firebase_response_received, InitState::FIREBASE_UPDATE_STATUS, InitState::FIREBASE_UPDATE_STATUS, "odgovor iz Firebase"))
    {
      firebase_response_received = false;
      // Posebna logika za to stanje
      if (currentInitState == InitState::FIREBASE_UPDATE_STATUS)
      { // Če je bil klic uspešen
        currentChannelInProcess++;
        if (currentChannelInProcess >= 8)
        {
          Serial.println("[INIT] Vsi statusi kanalov iz Firebase uspesno posodobljeni.");
          currentInitState = InitState::INIT_DONE;
          currentChannelInProcess = 0;
        }
      }
    }
    break;

  //============================================================================
  case InitState::INIT_DONE:
    //============================================================================
    // Pošljemo signal Rele modulu, da je inicializacija končana

    Serial.println("[INIT] Obvestilo Rele modulu, da je inicializacija končana...");
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Finalizing...");
    // Pošljemo paket z ukazom CMD_INIT_DONE.
    Lora_prepare_and_send_packet(CommandType::CMD_INIT_DONE, nullptr, 0);
    currentInitState = InitState::WAIT_FOR_INIT_COMPLETE;
    initState_timeout_start = millis();
    lora_response_received = false; // Resetiramo zastavico

    break;

  //============================================================================
  case InitState::WAIT_FOR_INIT_COMPLETE:
    //============================================================================
    // Čakamo na potrditev, da je inicializacija končana
    if (check_init_wait_state(lora_response_received, InitState::DONE, InitState::INIT_DONE, "odgovor za init done"))
    {
      lora_response_received = false;
      // currentInitState = InitState::DONE;
    }

    break;

  //============================================================================
  case InitState::DONE:
    //============================================================================
    Serial.println("[INIT] Inicializacija uspesno zakljucena.");
    displayLogOnLine(LINE_ERROR_WARNING, "[INIT] Done");
    lora_set_context(LoRaContext::IDLE); // Vrni na IDLE stanje
    init_done_flag = true;              // Nastavi zastavico, da je inicializacija zaključena
    currentInitState = InitState::STOP; // Postavi avtomat v stanje mirovanja
    lastSync = millis();                // Zapomni si čas zadnje sinhronizacije
    lastSensorRead = millis();          // Postavi čas zadnjega branja senzorjev
    break;

  //============================================================================
  case InitState::ERROR:
    //============================================================================
    // Tukaj moramo počakati 5 minut in znova poskusiti inicializacijo
    if (millis() - initState_timeout_start > 300000)
    { // 5 minut (300000 ms)
      Serial.println("[INIT] Ponovni poskus inicializacije po napaki...");

      currentInitState = InitState::START;
      init_retry_count = 0; // Ponastavimo števec poskusov
    }

    break;

    // ... ostala stanja (STOP) ...
  }

  // ... preostanek loop() funkcije ...
}

//================================================================================================================
// --- Glavna funkcija za obdelavo prejetih paketov (Callback) iz lora_rx_task ---
void Lora_handle_received_packet(const LoRaPacket &packet)
//================================================================================================================
{
  // Prikažemo ID in ukaz prejetega paketa na zaslonu
  char logBuffer[32];
  snprintf(logBuffer, sizeof(logBuffer), "In ID: %d, CMD: %d", packet.messageId, (uint8_t)packet.command);
  Serial.println(logBuffer);
  // displayLogOnLine(4, logBuffer);

  // Preverimo CRC
  uint16_t received_crc = packet.crc;
  uint16_t calculated_crc = calculate_crc((const uint8_t *)&packet, offsetof(LoRaPacket, crc));
  if (received_crc != calculated_crc)
  {
    Serial.println("NAPAKA: CRC se ne ujema!");
    return;
  }
  Serial.println("CRC OK.");

  // Logika za obdelavo glede na tip ukaza
  switch (packet.command)
  {
  //=================================================================================================
  case CommandType::RESPONSE_ACK:
    //=================================================================================================
    {
      AckStatus status = (AckStatus)packet.payload[0];
      Serial.print("Prejet ACK za sporocilo ID ");
      Serial.print(packet.messageId);
      Serial.print(" s statusom: ");
      Serial.println((int)status);

      if (status == AckStatus::ACK_OK)
      {
      }

      lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat
      break;
    }

  //=================================================================================================
  case CommandType::RESPONSE_TIME_SET:
    //=================================================================================================
    {
      // Preberemo odgovor na CMD_SET_TIME
      TimeResponsePayload sec_from_midnight_payload;
      memcpy(&sec_from_midnight_payload, packet.payload, sizeof(TimeResponsePayload));

      Serial.print("Prejet odgovor: ");
      Serial.println(sec_from_midnight_payload.seconds_since_midnight);

      // Preverimo ali se je čas pravilno nastavil
      if (sec_from_midnight_payload.seconds_since_midnight == secFromMidnight)
      {
        // Serial.println("Čas na Rele enoti je bil uspešno nastavljen.");
        lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat
      }
      else
      {
        Serial.println("NAPAKA: Čas na Rele enoti ni bil pravilno nastavljen.");
        // Tukaj lahko dodate dodatno obdelavo napake, npr. ponovni poskus nastavitve časa
        // Rele_sendUTC(); // Ponovno pošljemo čas
      }
      break;
    }

  //=================================================================================================
  case CommandType::RESPONSE_STATUS:
    //=================================================================================================
    { // Preberemo stanje relejev na Rele modulu
      RelayStatusPayload status;
      memcpy(&status, packet.payload, sizeof(RelayStatusPayload));
      Serial.println("Prejeto stanje relejev:");
      // Izpiši stanje relejev iz bitmaske
      for (int i = 0; i < 8; i++)
      {
        bool is_on = bitRead(status.relayStates, i);
        Serial.printf("  Rele %d: %s\n", i + 1, is_on ? "ON" : "OFF");
#ifdef IS_MASTER
        // Shranimo v strukturo kanalov
        kanal[i].state = is_on;
        //Firebase_Update_Relay_State(i, kanal[i].state); // Posodobimo stanje v Firebase
#endif
      }
      lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat
      // Po prejemu statusa lahko prikažemo posodobljeno stanje
      // PrikaziStanjeRelejevNaSerial();

      break;
    }
  //=================================================================================================
  case CommandType::RESPONSE_SENSORS:
    //=================================================================================================
    { // Preberemo podatke senzorjev iz Rele modula
      SensorDataPayload sensors;
      memcpy(&sensors, packet.payload, sizeof(SensorDataPayload));
      Serial.println("Prejeti podatki senzorjev:");
      Temperature = sensors.temperature;
      Humidity = sensors.humidity;
      SoilMoisture = sensors.soil_moisture;
      BatteryVoltage = sensors.battery_voltage;
      // CurrentConsumption = sensors.current_consumption;
      // WaterConsumption = sensors.water_consumption;

      // DODAJ: Signaliziraj čakalni vrsti, da je LoRa odgovor uspešen
      Sensor_OnLoRaResponse(true);

      // Tukaj obdelajte podatke senzorjev, npr. posodobite Firebase
      timestamp = getTime(); // Pridobimo trenutni Unix časovni žig.
      // FirebaseOperation op;
      // op.type = FirebaseTaskType::UPDATE_SENSORS;
      // op.timestamp = timestamp;
      // op.data.sensors = sensors;
      // op.pending = true;

      // if (!Firebase_QueueOperation(op)) { // Dodamo operacijo v čakalno vrsto
      //     Serial.println("[LORA] OPOZORILO: Firebase queue full, sensor data dropped!");
      // }

      // firebase_response_received = false;
      Firebase_Update_Sensor_Data(timestamp, sensors);
      // povratna informacija za update v: Firebase_processResponse
      // PrikaziStanjeSenzorjevNaSerial();
      lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat

      break;
    }

  // =================================================================================================
  case CommandType::RESPONSE_URNIK:
    // =================================================================================================
    {
      // Rele modul je poslal urnik za en kanal
      UrnikPayload received_urnik;
      memcpy(&received_urnik, packet.payload, sizeof(UrnikPayload));
      uint8_t status = received_urnik.status; // vrne se status - 0 OK, >1 error
      uint8_t index = received_urnik.releIndex;

      if (status == (uint8_t)AckStatus::ACK_OK) // status OK
      {

        if (index < 8 && index == currentChannelInProcess)
        {
          // Posodobi lokalno stanje za prejeti kanal
          kanal[index].start_sec = received_urnik.startTimeSec;
          kanal[index].end_sec = received_urnik.endTimeSec;
          formatSecondsToTime(kanal[index].start, sizeof(kanal[index].start), kanal[index].start_sec);
          formatSecondsToTime(kanal[index].end, sizeof(kanal[index].end), kanal[index].end_sec);

          Serial.printf("[INIT] Prejet urnik za kanal %d: %s - %s (Start sec=%d, End sec=%d)\n",
                        index + 1,
                        kanal[index].start,
                        kanal[index].end,
                        kanal[index].start_sec,
                        kanal[index].end_sec);
        }
        else
        {
          Serial.printf("NAPAKA: Neujemanje indeksa pri prejetem urniku! Pričakovan: %d, Prejet: %d\n", currentChannelInProcess, index);
        }
      }
      else
      {
        Serial.printf("NAPAKA: Rele modul vrnil napako pri branju urnika kanala %d, status: %d\n", index + 1, status);
      }

      lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat

      break;
    }
  //=================================================================================================
  case CommandType::RESPONSE_UPDATE_URNIK:
    //=================================================================================================
    {
      // Tukaj obdelajte odgovor po posodobitvi urnika v Rele modulu
      // vrne se status - 0 OK, >1 error in indeks posodobljenega kanala
      UrnikPayload update_response;
      memcpy(&update_response, packet.payload, sizeof(UrnikPayload));

      uint8_t status = update_response.status;   // AckStatus poslan z Rele modula
      uint8_t index = update_response.releIndex; // Indeks kanala (1-8) ki smo ga posodobili

      if (status == (uint8_t)AckStatus::ACK_OK)
      {

        if (index == currentChannelInProcess)
        {
          Serial.printf("ACK OK za posodobitev urnika kanala %d. Posodabljam lokalno stanje.\n", index + 1);

          // Posodobimo našo lokalno kopijo stanja, da se ujema s tem, kar smo poslali
          kanal[index].start_sec = firebase_kanal[index].start_sec;
          kanal[index].end_sec = firebase_kanal[index].end_sec;

          // Pridobimo čas v obliki "HH:MM"
          formatSecondsToTime(kanal[index].start, sizeof(kanal[index].start), kanal[index].start_sec);
          formatSecondsToTime(kanal[index].end, sizeof(kanal[index].end), kanal[index].end_sec);

          // Izpišemo nov urnik za debug
          Serial.printf("  Nov urnik kanala %d: %s - %s\n",
                        index + 1,
                        kanal[index].start,
                        kanal[index].end);
        }
        else
        {
          Serial.printf("NAPAKA: Neujemanje indeksa pri posodobitvi urnika! Pričakovan: %d, Prejet: %d\n", currentChannelInProcess, index);
        }
      }
      else
      {
        Serial.printf("NAPAKA: Rele modul vrnil napako pri posodobitvi urnika kanala %d, status: %d\n", index + 1, status);
      }

      lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat

      break;
    }

  //=================================================================================================
  case CommandType::RESPONSE_INIT_DONE:
    //=================================================================================================
    {
      Serial.println("Rele modul je potrdil, da je inicializacija končana.");
      lora_response_received = true; // nastavimo zastavico za inicializacijski avtomat
      break;
    }

  //=================================================================================================
  case CommandType::NOTIFY_LOW_BATT:
    //=================================================================================================
    {
      Serial.println("!!! OPOZORILO: Nizka napetost baterije na Rele enoti !!!");
      // Pošljemo potrditev (ACK) nazaj na Rele
      // pridobimo ID sporočila iz prejete notifikacije
      uint8_t message_id = packet.messageId;
      Lora_prepare_and_send_response(message_id, CommandType::ACK_NOTIFICATION, &message_id, sizeof(message_id));
      break;
    }

  //=================================================================================================
  case CommandType::NOTIFY_RELAY_STATE_CHANGED:
    //=================================================================================================
    {
      Serial.println("!!! OPOZORILO: Sprememba stanja releja na Rele enoti !!!");
      // Pridobimo kateri rele se je spremenil in njegovo novo stanje
      RelayStatusPayload status;
      memcpy(&status, packet.payload, sizeof(RelayStatusPayload));

      for (int i = 0; i < 8; i++)
      {
        bool is_on = bitRead(status.relayStates, i);
        
        // Preverimo če je prišlo do spremembe stanja
        if (kanal[i].state != is_on)
        {
          Serial.printf("  Rele %d novo stanje: %s\n", i + 1, is_on ? "ON" : "OFF");

          // POSODOBI LOKALNO STANJE PRED POŠILJANJEM V FIREBASE!
          kanal[i].state = is_on;          

          // Pošlji v Firebase
          Firebase_Update_Relay_State(i + 1, is_on);

        }
      }

      // Pošljemo potrditev (ACK) nazaj na Rele
      // pridobimo ID sporočila iz prejete notifikacije
      uint8_t message_id = packet.messageId;
      Lora_prepare_and_send_response(message_id, CommandType::ACK_NOTIFICATION, &message_id, sizeof(message_id));

      break;
    }

  // ... obdelava ostalih tipov odgovorov in notifikacij ...

  //=================================================================================================
  // NOVO: Obdelava podatkov iz INA3221 senzorja
  //=================================================================================================
  case CommandType::RESPONSE_INA_DATA:
  {
      Serial.println("Prejeti podatki iz INA3221 senzorja.");

      // 1. Preberi podatke iz payloada
      memcpy(&ina_data, packet.payload, sizeof(INA3221_DataPayload));

      // 2. Izpiši prejete podatke za preverjanje
      // const char* kanal_str[3] = {"Battery", "Solar", "Load"};
      // for (int i = 0; i < 3; i++) {
      //     Serial.printf("  %s: Napetost: %.2f V, Tok: %.2f mA, Shunt napetost: %.2f mV, Moč: %.2f mW\n",
      //                   kanal_str[i],
      //                   ina_data.channels[i].bus_voltage,
      //                   ina_data.channels[i].current_mA,
      //                   ina_data.channels[i].shunt_voltage_mV,
      //                   ina_data.channels[i].power_mW);
      // }
      // Serial.printf("  Alert zastavice: 0x%04X\n", ina_data.alert_flags);
      // Serial.printf("  Shunt voltage Sum: %.3f \n", ina_data.shunt_voltage_sum_mV);

      // Signaliziraj čakalni vrsti, da so podatki uspešno prejeti
      Sensor_OnLoRaResponse(true);

      // 3. Posodobi podatke v Firebase
      timestamp = getTime(); // Pridobimo trenutni Unix časovni žig.

      // Pošlji v Firebase
      Firebase_Update_INA_Data(timestamp, ina_data);
      break;
  }


    //=================================================================================================
  case CommandType::NOTIFY_TIME_REQUEST:
    //=================================================================================================
    {
      Serial.println("!!! OPOZORILO: Zahteva za čas na Rele enoti !!!");
      // Pripravimo in pošljemo paket nazaj na Rele
      Rele_sendUTC();                     // Pošljemo ukaz za sinhronizacijo časa
      initState_timeout_start = millis(); // Zaženemo timer za timeout
      break;
    }

    //=================================================================================================
  case CommandType::NOTIFY_RESET_OCCURED:
    //=================================================================================================
    {
      Serial.println("!!! OPOZORILO: Ponastavitev se je zgodila na Rele enoti !!!");
      // Začnemo inicializacijski avtomat znova
      // Najprej pošljemo nazaj ACK
      uint8_t message_id = packet.messageId;
      Lora_prepare_and_send_response(message_id, CommandType::ACK_NOTIFICATION, &message_id, sizeof(message_id));
      reset_occured = true; // nastavimo zastavico za inicializacijski avtomat
      // Ko bo sporočilo poslano , se bo inicializacijski avtomat zagnal znova v loop funkciji
      break;
    }

  default:
    Serial.println("Neznan ukaz v prejetem paketu.");
    break;
  }
  Serial.println("--------------------");
}

//---------------------------------------------------------------------------------------------------------------------
void setup()
{
  pinMode(onboard_led.pin, OUTPUT); // onboard LED is output
  onboard_led.on = false;           // turn off the LED
  onboard_led.update();             // update the LED state

  Serial.begin(115200); // Inicializiraj serijsko komunikacijo
  init_display(); // Inicializiraj OLED zaslon

  connectToWiFi(); // Povezava z WiFi
  syncTimestamp(); // Sinhroniziraj čas z NTP strežnikom

  displayLogOnLine(LINE_TITLE, "Master LoRa");
  displayLogOnLine(LINE_LORA_STATUS, "Setup board...");
  delay(500);
  displayLogOnLine(LINE_RSSI_SNR, "WiFi: " + String(WiFi.localIP()));
  delay(400);
  displayLogOnLine(LINE_RSSI_SNR, "Init Lora...");
    // Registriraj callback funkcijo za obdelavo LoRa paketov
  // Ta funkcija bo poklicana iz lora_process_received_packets() v loop()
  lora_initialize(Lora_handle_received_packet);
  delay(400);
  displayLogOnLine(LINE_RSSI_SNR, "Init Firebase...");
  Init_Firebase(); // Inicializiraj Firebase
  displayLogOnLine(LINE_RSSI_SNR, "Init Firebase...OK");
  delay(400);

  Sensor_InitQueue(); // DODAJ: Inicializacija senzorske čakalne vrste

  // Ustvari FreeRTOS naloge
  // xTaskCreate(LoRaTask, "LoRaTask", 8192, NULL, 3, NULL);  // Naloga za LoRa komunikacijo (višja prioriteta)

  displayLogOnLine(LINE_RSSI_SNR, "Init done.");
  displayLogOnLine(LINE_LORA_STATUS, "Setup board...OK");
  displayLogOnLine(LINE_PACKET_INFO, "WAITING FOR DATA");

  Serial.println("Setup končan.");
  printLocalTime();                                     // Izpiši lokalni čas
  timestamp = getTime();                                // Get current timestamp
  Serial.println("Trenutni čas: " + String(timestamp)); // izpiši trenutni čas
  secFromMidnight = getCurrentSeconds();                // Pridobimo trenutni čas v sekundah od polnoči
}


//---------------------------------------------------------------------------------------------------------------------
void loop()
{
  // Ali imamo čakajočo posodobitev IN ali je LoRa sedaj prosta?
  if (firebaseUpdatePending && !lora_is_busy())
  {
    Serial.println("[MAIN] Pošiljam čakajočo posodobitev iz Firebase...");

    // Pošljemo podatke, ki so bili shranjeni v nabiralniku
    Rele_updateRelayUrnik(
        pendingUpdateData.kanalIndex,
        pendingUpdateData.start_sec,
        pendingUpdateData.end_sec);

    // Počistimo zastavico, da ne pošljemo ukaza večkrat
    firebaseUpdatePending = false;
  }

  //-------------------------------------------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) // Preverimo WiFi povezavo
  {
    Serial.println("WiFi povezava izgubljena! Poskušam ponovno...");
    displayLogOnLine(LINE_RSSI_SNR, "WiFi lost! "); // display ima samo 16 znakov!
    connectToWiFi();
  }

  //-------------------------------------------------------------------------------------
  // Firebase obdelava
  app.loop(); // To maintain the authentication and async tasks

  if (app.ready())
  {
    if (!taskComplete) // samo na začetku
    {
      displayLogOnLine(LINE_RSSI_SNR, "Init Firebase...OK");
      taskComplete = true;
      displayLogOnLine(LINE_LORA_STATUS, "Link Firebase...");
      Firebase_Connect();
      displayLogOnLine(LINE_LORA_STATUS, "Link Firebase...OK");

      // --- Zaženemo avtomatiko za inicializacijo ---
      initState_timeout_start = millis();                   // Zaženemo timer za timeout
      currentInitState = InitState::READ_FIREBASE_INTERVAL; // ZAČNEMO Z BRANJEM INTERVALA

      firebase_init_flag = true; // Nastavimo flag, da je Firebase inicializiran
    }
    else
    {
      //---- Periodične naloge----
      // Tukaj lahko izvajamo periodične naloge ko smo končali z inicializacijo

      // Procesiranje senzorske čakalne vrste
      static unsigned long lastSensorCheck = 0;
      if (millis() - lastSensorCheck > 100) { // 10x na sekundo
          lastSensorCheck = millis();
          Sensor_ProcessQueue();
      }


      // NOVO: Sinhronizacija na cele minute
      static bool firstReadDone = false;
      
      if (!firstReadDone) {
          // PRVI KLIC: Počakaj na prehod minute (sekunda == 0)
          time_t now;
          struct tm timeinfo;
          time(&now);
          localtime_r(&now, &timeinfo);
          
          if (timeinfo.tm_sec == 0 && init_done_flag && Interval_mS > 0) {
              // Točno ob prehodu minute (XX:XX:00)
              Serial.println("[SENSORS] Prvi sinhroniziran klic ob prehodu minute.");
              
              lastSensorRead = millis();
              firstReadDone = true;
              
              // Dodaj operacije v čakalno vrsto
              Sensor_QueueOperation(SensorTaskType::READ_SENSORS);
              Sensor_QueueOperation(SensorTaskType::READ_INA);
          }
      } else {
          // NASLEDNJI KLICI: Normalen interval
          if (init_done_flag && Interval_mS > 0 && (millis() - lastSensorRead >= Interval_mS)) {
              lastSensorRead = millis();
              
              Serial.println("[SENSORS] Periodično branje senzorjev.");
              
              // Dodaj operacije v čakalno vrsto
              Sensor_QueueOperation(SensorTaskType::READ_SENSORS);
              Sensor_QueueOperation(SensorTaskType::READ_INA);
          }
      }

      // NOVO: Procesiranje Firebase čakalne vrste v glavnem loop-u
      // To pokličite periodično (na primer vsakih 100-500ms)
      // NOVO: Preverjanje Firebase retry
      static unsigned long lastFirebaseCheck = 0;
      if (millis() - lastFirebaseCheck > 500) // 2x na sekundo
      {
        lastFirebaseCheck = millis();
        Firebase_CheckAndRetry();
      }
      // --- konec periodičnih nalog ---

      //--------------------------------------------------------------------------------------------------
      // Preveri, ali so na voljo novi podatki iz Firebase streama
      if (newChannelDataAvailable)
      {
        // Tukaj pokličite funkcijo za pošiljanje podatkov Rele modulu
        Firebase_handleStreamUpdate(channelUpdate.kanalIndex, channelUpdate.start_sec, channelUpdate.end_sec);

        // Počisti zastavico, da ne obdelamo istih podatkov večkrat
        newChannelDataAvailable = false;
      }
      //--------------------------------------------------------------------------------------------------
      // Preveri, ali je prišlo do ponovnega zagona Rele modula
      if (reset_occured && !lora_is_busy())
      {
        // Zaženemo inicializacijski avtomat znova
        init_done_flag = false; // Ponastavi zastavico, da inicializacija ni končana
        Serial.println("[INIT] Ponovna inicializacija zaradi ponastavitve Rele modula...");
        initState_timeout_start = millis(); // Zaženemo timer za timeout
        currentInitState = InitState::START;
        reset_occured = false; // Ponastavi zastavico
      }
    }
  }

  // --- KLIČEMO NAŠ SEKVENČNI AVTOMAT ZA INICIJALIZACIJO ---
  manageReleInitialization();

  // ZAMENJAJ LoRa obdelavo z enim klicem:
  lora_process_received_packets(); // Namesto vsega v LoRaTask

  // --- ONBOARD LED BLINK ---
  if (currentInitState == InitState::ERROR)
  {
    onboard_led.blink(100, 100); // LED blinka vsakih 100 ms
  }
  else if (currentInitState == InitState::START || currentInitState == InitState::WAIT_FOR_INIT_COMPLETE)
  {
    onboard_led.blink(100, 900); // LED blinka vsakih 1000 ms
  }
  else
  {
    onboard_led.blink(500, 500); // LED blinka vsakih 500 ms
  }
}

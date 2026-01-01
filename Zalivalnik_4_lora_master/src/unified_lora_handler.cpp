#include "unified_lora_handler.h"
#include "crypto_manager.h"
#include <RadioLib.h>
#include "diagnostics.h"
#include <stdlib.h> // for rand

// --- Definicije pinov za LORA (kot prej) ---
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23 // Ali 14
#define LORA_DIO0 26
#define LORA_DIO1 33

// Določite GPIO pine RX in TX za LoRaSerial
#define LORA_RX_PIN 13 // Prilagodite glede na vaše potrebe
#define LORA_TX_PIN 12 // Prilagodite glede na vaše potrebe

// --- LoRa konfiguracija (kot prej) ---
const float RF_FREQUENCY = 868.0;
const float BANDWIDTH = 125.0;
const uint8_t SPREADING_FACTOR = 7;
const uint8_t CODING_RATE = 5;
const uint8_t SYNC_WORD = 0x12;
const int8_t TX_POWER = 14;
const uint16_t PREAMBLE_LEN = 8;

// --- LoRa modul in pini ---
// SX1276 radio = new Module(18, 26, 14, 33); // NSS, DIO0, RESET, DIO1
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1); // NSS, DIO0, RESET, DIO1

// --- Interne spremenljivke ---
volatile bool operationDone = false;            // Zastavica za signalizacijo konca operacije (TX ali RX) 
volatile bool loraIsTransmitting = false;       // Ali je LoRa v načinu oddajanja
volatile bool loraIsWaitingResponse = false;    // Ali LoRa čaka na odgovor
PacketHandlerCallback packetCallback = nullptr;

// --- FreeRTOS objekti ---
SemaphoreHandle_t loraInterruptSemaphore = NULL;
TaskHandle_t loraRxTaskHandle = NULL;
TaskHandle_t loraTxTaskHandle = NULL;
TaskHandle_t loraDispatchTaskHandle = NULL;

// --- Prototipi taskov ---
void lora_dispatch_task(void *pvParameters);
void lora_rx_task(void *pvParameters);
void lora_tx_task(void *pvParameters);

// Definicije za nabiralnik prejetih paketov ---
volatile bool lora_new_packet_available = false;
LoRaPacket lora_received_packet;

// Lora timeout / retry
unsigned long lora_response_timeout_start = 0;
unsigned long lora_next_retry_time = 0; // NEW: next allowed retry time
const unsigned long LORA_RESPONSE_TIMEOUT_MS = 3000; // baseline timeout

// --- NABIRALNIK ZA ODHODNE PAKETE IN PONOVNE POSKUSE ---
LoRaPacket last_sent_packet;                   // Hranimo zadnji paket, ki čaka na odgovor
uint8_t lora_retry_count = 0;                  // Števec ponovnih poskusov za splošne ukaze
const uint8_t MAX_LORA_RETRIES = 3;            // Največje število ponovnih poskusov


// --- ISR ---
#if defined(ESP32)
void IRAM_ATTR setFlag_unified() {
    // Znotraj ISR-a naredimo samo najnujnejše: oddamo semafor.
    // Ne kličemo nobenih drugih funkcij.
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(loraInterruptSemaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}
#else
void setFlag_unified() { operationDone = true; }
#endif






//-----------------------------------------------------------------------------------------
// --- Implementacija javnih funkcij za stanje ---

// Preveri, ali je LoRa zasedena (pošilja ali čaka na odgovor)
bool lora_is_busy() {
  return loraIsTransmitting || loraIsWaitingResponse;
}

//-----------------------------------------------------------------------------------------
static LoRaContext currentContext = LoRaContext::IDLE;

// DODAJ: Funkcija za nastavitev konteksta
void lora_set_context(LoRaContext context) {
  currentContext = context;
}

// Funkcija za pridobitev trenutnega konteksta
LoRaContext lora_get_context() {
  return currentContext;
}

//-----------------------------------------------------------------------------------------
// --- Javne funkcije ---
void lora_initialize(PacketHandlerCallback callback)
{
  crypto_init();
  packetCallback = callback;

  // --- NOVO: Inicializacija FreeRTOS objektov ---
  loraInterruptSemaphore = xSemaphoreCreateBinary();
  if (loraInterruptSemaphore == NULL) {
      Serial.println("[LORA] Napaka pri kreiranju loraInterruptSemaphore!");
      // Obravnava napake
  }

  // Kreiranje taskov za obdelavo LoRa dogodkov
  xTaskCreate(lora_dispatch_task, "LoRaDispatch", 16384, NULL, 2, &loraDispatchTaskHandle); // Visoka prioriteta
  xTaskCreate(lora_rx_task, "LoRaRX", 12288, NULL, 1, &loraRxTaskHandle);
  xTaskCreate(lora_tx_task, "LoRaTX", 10240, NULL, 1, &loraTxTaskHandle);
  // --- KONEC NOVO ---

  // int state = radio.begin();
  int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, TX_POWER, PREAMBLE_LEN);
  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.print(F("[LORA] Init napaka, koda: "));
    displayLogOnLine(LINE_LORA_STATUS,"[LORA] Init err!");
    Serial.println(state);
    while (true)
      ;
  }

  radio.setDio0Action(setFlag_unified, RISING);
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.print(F("[LORA] Napaka pri inicializaciji sprejema, koda: "));
    Serial.println(state);
    displayLogOnLine(LINE_LORA_STATUS,"[LORA] Init error!");
  }else{
    displayLogOnLine(LINE_LORA_STATUS,"[LORA] Ready.");
  }
  Serial.println(F("[LORA] Init uspesen. Cakam na sporocila..."));
}

// -----------------------------------------------------------------------------------------
// Pošlji paket prek LoRa
bool lora_send_packet(const LoRaPacket &packet)
{
  // Preverimo, ali je LoRa modul pripravljen za pošiljanje nove zahteve
  if (loraIsTransmitting || loraIsWaitingResponse)
  {
    Serial.println("[LORA] Busy, ne morem poslati!");
    return false; // Vrnemo neuspeh
  }

  // --- NOVO: Aktiviramo mehanizem za ponovne poskuse SAMO če pri čakanju na odgovor ---
  if (currentContext == LoRaContext::WAITING_FOR_RESPONSE)
  {
    memcpy(&last_sent_packet, &packet, sizeof(LoRaPacket));
    lora_retry_count = 0;
    // is_waiting_for_critical_response = true;
    lora_response_timeout_start = millis();
    Serial.println("[LORA] Aktiviran mehanizem za ponovne poskuse.");
  }
  else if (currentContext == LoRaContext::INITIALIZATION)
  {
    // Sredi inicializacije - ne aktiviramo splošnega mehanizma
    // is_waiting_for_critical_response = false;
    Serial.println("[LORA] Način inicializacije - brez splošnih poskusov.");
  }
  else
  {
    // Samo ACK - ne aktiviramo splošnega mehanizma
    // is_waiting_for_critical_response = false;
    Serial.println("[LORA] Način samo ACK - brez poskusov.");
  }

  // Priprava šifriranega paketa
  uint8_t encrypted[sizeof(LoRaPacket) + 16];
  size_t enc_len = sizeof(encrypted);

  if (!encrypt_packet(packet, encrypted, enc_len))
  {
    Serial.println("[LORA] Napaka pri šifriranju!");
    // is_waiting_for_critical_response = false; // Sprostimo
    currentContext = LoRaContext::IDLE; // Reset konteksta
    return false;
  }

  // Pošiljanje
  loraIsTransmitting = true;
  int state = radio.startTransmit(encrypted, enc_len);

  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.printf("[LORA] Napaka pri pošiljanju: %d\n", state);
    loraIsTransmitting = false;
    // is_waiting_for_critical_response = false; // Sprostimo
    currentContext = LoRaContext::IDLE; // Reset konteksta
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------------------------
// --- NOVO: Implementacija FreeRTOS taskov ---

// Task, ki ga prebudi ISR in ugotovi, ali gre za RX ali TX dogodek
void lora_dispatch_task(void *pvParameters) {
    for (;;) {
        // Čakaj na prekinitev od LoRa modula (sproži jo ISR)
        if (xSemaphoreTake(loraInterruptSemaphore, portMAX_DELAY) == pdTRUE) {
            // Preverimo, ali je bila prekinitev od pošiljanja ali prejema
            uint16_t irqFlags = radio.getIRQFlags();

            if (irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE) {
                // Obvesti TX task, da je oddajanje končano
                xTaskNotifyGive(loraTxTaskHandle);
            } else if (irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE) {
                // Obvesti RX task, da je prispel nov paket
                xTaskNotifyGive(loraRxTaskHandle);
            }
        }
    }
}

// Task za obdelavo končanega oddajanja
void lora_tx_task(void *pvParameters) {
    for (;;) {
        // Čakaj na obvestilo iz dispatch taska
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // DODAJ DIAGNOSTIKO
        UBaseType_t txStackLeft = uxTaskGetStackHighWaterMark(loraTxTaskHandle);
        Serial.printf("[TX TASK] Stack left: %d\n", txStackLeft);

        if (txStackLeft < 512) { // OPOZORILO!
            Serial.println("[TX TASK] ⚠️ STACK CRITICAL!");
        }

        radio.finishTransmit(); // Zaključimo oddajanje
        loraIsTransmitting = false; // Ni več v načinu oddajanja
        Serial.println("[LORA] Paket uspesno poslan.");
        // displayLogOnLine(LINE_LORA_STATUS,"[LORA] TX done.");

        // Po končanem oddajanju takoj preklopimo nazaj v način sprejemanja
        int state = radio.startReceive();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.print(F("[LORA] Napaka pri preklopu v RX po TX, koda: "));
            Serial.println(state);
            // displayLogOnLine(LINE_LORA_STATUS, "[LORA] RX ERR");
        } else {
          Serial.println("[LORA] Preklop v RX po TX uspesen.");
            // displayLogOnLine(LINE_LORA_STATUS, "[LORA] RX READY");
        }
        Serial.println("-------------------------");
        Serial.println("");
    }
}


// Task za obdelavo prejetega paketa - POENOSTAVLJEN
void lora_rx_task(void *pvParameters)
{
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Čakaj na obvestilo

    // DODAJ DIAGNOSTIKO
    UBaseType_t rxStackLeft = uxTaskGetStackHighWaterMark(loraRxTaskHandle);
    Serial.printf("[RX TASK] Stack left: %d\n", rxStackLeft);

    if (rxStackLeft < 512) { // OPOZORILO!
        Serial.println("[RX TASK] ⚠️ STACK CRITICAL!");
    }

    uint8_t rxBuffer[sizeof(LoRaPacket) + 16];
    int len = radio.getPacketLength();

    loraIsWaitingResponse = false;

    if (len == sizeof(rxBuffer))
    {
      radio.readData(rxBuffer, len);

      LoRaPacket tempPacket;
      if (decrypt_packet(rxBuffer, len, tempPacket))
      {
        Serial.printf("[LORA RX] ID:%d CMD:%d RSSI:%d SNR:%.1f\n",
                      tempPacket.messageId, (uint8_t)tempPacket.command,
                      radio.getRSSI(), radio.getSNR());

        // --- POSODOBLJENO: Ob prejemu odgovora, ustavimo mehanizem ---
        if (currentContext == LoRaContext::WAITING_FOR_RESPONSE)  // Če čakamo na odgovor
        {
          Serial.println("[LORA RETRY] Prejet odgovor, ustavljam ponovne poskuse.");
          // is_waiting_for_critical_response = false;
          currentContext = LoRaContext::IDLE; // Reset konteksta
          lora_retry_count = 0;
        }
        memcpy(&lora_received_packet, &tempPacket, sizeof(LoRaPacket));
        lora_new_packet_available = true;
      }
    }

    radio.startReceive();
  }
}


// -----------------------------------------------------------------------------------------
// PREMAKNI manage_lora_retries() iz main.cpp sem
void manage_lora_retries()
{
  if (currentContext == LoRaContext::SENSOR_QUEUE) return;
  if (currentContext != LoRaContext::WAITING_FOR_RESPONSE) return;
  
  if (millis() - lora_response_timeout_start > LORA_RESPONSE_TIMEOUT_MS)
  {
    lora_retry_count++;
    
    if (lora_retry_count >= MAX_LORA_RETRIES)
    {
      Serial.printf("[LORA RETRY] NAPAKA: Preseženo število poskusov za ukaz %d.\n", (int)last_sent_packet.command);
      currentContext = LoRaContext::IDLE; // Reset konteksta
      // lora_set_waiting_for_response(false);
    }
    else
    {
      Serial.printf("[LORA RETRY] Timeout! Poskus %d/%d\n", lora_retry_count, MAX_LORA_RETRIES);
      if (lora_send_packet(last_sent_packet))
      {
        lora_response_timeout_start = millis();
      }
      else
      {
        currentContext = LoRaContext::IDLE; // Reset če pošiljanje ne uspe
      }      
    }
  }
}

//------------------------------------------------------------------------------------------
// NOVA funkcija, ki nadomesti LoRaTask iz main.cpp
void lora_process_received_packets()
{
  // 1. Upravljaj ponovne poskuse
  manage_lora_retries();
  
  // 2. Obdelaj prejete pakete
  if (lora_new_packet_available)
  {
    // Kliči callback, ki je registriran pri inicializaciji (Lora_handle_received_packet)
    if (packetCallback != nullptr)
    {
      packetCallback(lora_received_packet);
    }
    lora_new_packet_available = false;
  }
}


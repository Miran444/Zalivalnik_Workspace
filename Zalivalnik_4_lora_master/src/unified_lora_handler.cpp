#include "unified_lora_handler.h"
#include "crypto_manager.h"
#include <RadioLib.h>

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

// --- NOVO: FreeRTOS objekti ---
SemaphoreHandle_t loraInterruptSemaphore = NULL;
TaskHandle_t loraRxTaskHandle = NULL;
TaskHandle_t loraTxTaskHandle = NULL;
TaskHandle_t loraDispatchTaskHandle = NULL;

// --- Prototipi taskov ---
void lora_dispatch_task(void *pvParameters);
void lora_rx_task(void *pvParameters);
void lora_tx_task(void *pvParameters);

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


// Definicije za nabiralnik prejetih paketov ---
volatile bool lora_new_packet_available = false;
LoRaPacket lora_received_packet;


//-----------------------------------------------------------------------------------------
// --- Implementacija javnih funkcij za stanje ---

void lora_set_waiting_for_response(bool waiting) {
  loraIsWaitingResponse = waiting;
}

// Preveri, ali je LoRa zasedena (pošilja ali čaka na odgovor)
bool lora_is_busy() {
  return loraIsTransmitting || loraIsWaitingResponse;
}

bool lora_is_waiting_for_response() {
  return loraIsWaitingResponse;
}

// Pomožna funkcija za preverjanje, ali je ukaz odgovor
bool is_response_command(CommandType cmd) {
    // Predpostavimo, da se vsi odgovori začnejo z RESPONSE_
    // To je odvisno od vaše definicije v message_protocol.h
    return (uint8_t)cmd >= (uint8_t)CommandType::RESPONSE_ACK;
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
  xTaskCreate(lora_dispatch_task, "LoRaDispatch", 2048, NULL, 10, &loraDispatchTaskHandle); // Visoka prioriteta
  xTaskCreate(lora_rx_task, "LoRaRX", 4096, NULL, 5, &loraRxTaskHandle);
  xTaskCreate(lora_tx_task, "LoRaTX", 2048, NULL, 5, &loraTxTaskHandle);
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
  if (lora_is_busy()) {
    Serial.println("[LORA] LoRa je zasedena zaradi: " + String(loraIsTransmitting ? "TX" : "RX") + ". Ne morem poslati paketa.");
    return false; // Vrnemo neuspeh
  }

  // Šifriramo in pošljemo paket
  uint8_t txBuffer[sizeof(LoRaPacket) + 16]; // Prostor za paket + GCM tag
  if (!encrypt_packet(packet, txBuffer, sizeof(txBuffer)))
  {
    Serial.println("[LORA] Napaka pri sifriranju!");
    return false;
  }
  Serial.println("[LORA] Paket uspesno sifriran. Zacenjam posiljanje...");
  operationDone = false; // Počistimo zastavico pred pošiljanjem
  int state = radio.startTransmit(txBuffer, sizeof(txBuffer));

  if (state == RADIOLIB_ERR_NONE)
  {
    displayLogOnLine(LINE_LORA_STATUS, "[LORA] TX Start...");
    // Pošiljanje se je uspešno začelo. Funkcija se lahko vrne.
    loraIsTransmitting = true;  // Nastavimo zastavico oddajanja
    // Glavna zanka lora_loop() bo poskrbela za zaključek.
    return true;
  }
  else
  {
    Serial.print(F("[LORA] Napaka pri zacetku posiljanja, koda: "));
    Serial.println(state);
    displayLogOnLine(LINE_LORA_STATUS, "[LORA] TX napaka!");
    return false;
  }
}

// -----------------------------------------------------------------------------------------
// Glavna zanka za obdelavo LoRa dogodkov
// void lora_loop()
// {
//   if (!operationDone) // Ni bilo dogodka, Lora ni oddala ali prejela ničesar
//   {
//     return;
//   }

//   // Zgodil se je dogodek, počistimo zastavico
//   operationDone = false;

//   // Preverimo, ali je bila prekinitev od pošiljanja ali prejema
//   uint16_t irqFlags = radio.getIRQFlags();

//   if (irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE)
//   {
//     radio.finishTransmit(); // Zaključimo oddajanje
//     loraIsTransmitting = false; // Ni več v načinu oddajanja
//     Serial.println("[LORA] Paket uspesno poslan.");
//     displayLogOnLine(LINE_LORA_STATUS,"[LORA] TX done.");
//   }
//   else if (irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE)
//   {
//     uint8_t rxBuffer[sizeof(LoRaPacket) + 16];
//     int len = radio.getPacketLength();

//     loraIsWaitingResponse = false; // Prejeli smo nekaj, ne čakamo več na odgovor

//     // Preberemo podatke
//     if (len != sizeof(rxBuffer))
//     {
//       Serial.println("[LORA] Napaka: Prejet paket napacne dolzine!");
//     }
//     else
//     {
//       radio.readData(rxBuffer, len);

//       // Prikaz RSSI in SNR na zaslonu in serijskem monitorju
//       String debugLine = "RSSI " + String(radio.getRSSI()) + " SNR " + String(radio.getSNR());
//       displayLogOnLine(LINE_RSSI_SNR, debugLine);

//       LoRaPacket tempPacket;
//       if (decrypt_packet(rxBuffer, len, tempPacket))
//       {
//         Serial.println("[LORA] Prejet paket uspesno desifriran. Postavljam v čakalno vrsto.");
//         // Prikažemo prejeti paket
//         String logMsg = "Out ID: " + String(tempPacket.messageId) + ", CMD: " + String((uint8_t)tempPacket.command);
//         displayLogOnLine(LINE_PACKET_INFO, logMsg.c_str());
//         // Ne kličemo več dolgotrajne funkcije.
//         // Samo skopiramo podatke v nabiralnik in postavimo zastavico.
//         memcpy(&lora_received_packet, &tempPacket, sizeof(LoRaPacket));
//         lora_new_packet_available = true;
//       }
//       else
//       {
//         Serial.println("[LORA] Napaka: Desifriranje neuspesno (napacen kljuc ali poskodovani podatki).");
//         // Prikažemo poslan paket
//         // Serial.println("[LORA] Prejeti podatki (heks):");
//         // for (int i = 0; i < len; i++)
//         // {
//         //   if (rxBuffer[i] < 0x10)
//         //     Serial.print("0");
//         //   Serial.print(rxBuffer[i], HEX);
//         //   Serial.print(" ");
//         // }
//       }
//     }
//   }
//   // Ponovno zaženemo sprejem
//   int state = radio.startReceive();
//   if (state != RADIOLIB_ERR_NONE)
//   {
//     Serial.print(F("[LORA] Napaka pri inicializaciji sprejema, koda: "));
//     Serial.println(state);
//     displayLogOnLine(LINE_LORA_STATUS, "[LORA] RX ERR");
//   }
//   displayLogOnLine(LINE_LORA_STATUS, "[LORA] RX READY");
//   Serial.println("-------------------------");
//   Serial.println("");
// }

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

// Task za obdelavo prejetega paketa
void lora_rx_task(void *pvParameters) {
    for (;;) {
        // Čakaj na obvestilo iz dispatch taska
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t rxBuffer[sizeof(LoRaPacket) + 16];
        int len = radio.getPacketLength();

        loraIsWaitingResponse = false; // Prejeli smo nekaj, ne čakamo več na odgovor

        // Preberemo podatke
        if (len != sizeof(rxBuffer)) {
            Serial.println("[LORA] Napaka: Prejet paket napacne dolzine!");
        } else {
            radio.readData(rxBuffer, len);

            // Prikaz RSSI in SNR na zaslonu in serijskem monitorju
            String debugLine = "RSSI " + String(radio.getRSSI()) + " SNR " + String(radio.getSNR());
            // displayLogOnLine(LINE_RSSI_SNR, debugLine);

            LoRaPacket tempPacket;
            if (decrypt_packet(rxBuffer, len, tempPacket)) {
                Serial.println("[LORA] Prejet paket uspesno desifriran. Postavljam v čakalno vrsto.");
                // Prikažemo prejeti paket
                String logMsg = "Out ID: " + String(tempPacket.messageId) + ", CMD: " + String((uint8_t)tempPacket.command);
                // displayLogOnLine(LINE_PACKET_INFO, logMsg.c_str());
                // Ne kličemo več dolgotrajne funkcije.
                // Samo skopiramo podatke v nabiralnik in postavimo zastavico.
                memcpy(&lora_received_packet, &tempPacket, sizeof(LoRaPacket));
                lora_new_packet_available = true;
            } else {
                Serial.println("[LORA] Napaka: Desifriranje neuspesno (napacen kljuc ali poskodovani podatki).");
            }
        }
        // Ponovno zaženemo sprejem
        int state = radio.startReceive();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.print(F("[LORA] Napaka pri inicializaciji sprejema, koda: "));
            Serial.println(state);
            // displayLogOnLine(LINE_LORA_STATUS, "[LORA] RX ERR");
        }
        Serial.println("[LORA] Preklop v RX.");
        // displayLogOnLine(LINE_LORA_STATUS, "[LORA] RX READY");
        Serial.println("-------------------------");
        Serial.println("");
    }
}        
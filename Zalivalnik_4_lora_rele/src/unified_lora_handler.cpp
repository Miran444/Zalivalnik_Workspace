#include "unified_lora_handler.h"
#include "crypto_manager.h"
#include <RadioLib.h>
#include "display_manager.h" // Knjižnica za upravljanje z zaslonom

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
volatile bool operationDone = false;
PacketHandlerCallback packetCallback = nullptr;

// --- ISR ---
#if defined(ESP32)
void IRAM_ATTR setFlag_unified() { operationDone = true; }
#else
void setFlag_unified() { operationDone = true; }
#endif

// --- Javne funkcije ---
//---------------------------------------------------------------------------
// Inicializacija LoRa modula
void lora_initialize(PacketHandlerCallback callback)
{
  crypto_init();
  packetCallback = callback;

  // int state = radio.begin();
  int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, TX_POWER, PREAMBLE_LEN);
  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.print(F("LoRa init napaka, koda: "));
    Serial.println(state);
    displayLogOnLine(3, "LoRa INIT ERR");
    while (true)
      ;
  }

  radio.setDio0Action(setFlag_unified, RISING); // Nastavimo prekinitev na DIO0 pin

  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.print(F("Napaka pri zacetku sprejema, koda: "));
    Serial.println(state);
    displayLogOnLine(3, "LoRa RX ERR");
  }
  displayLogOnLine(3, "LoRa RX READY");
}

//---------------------------------------------------------------------------
// Pošlji LoRa paket
bool lora_send_packet(const LoRaPacket &packet)
{
  uint8_t txBuffer[sizeof(LoRaPacket) + 16]; // Prostor za paket + GCM tag
  if (!encrypt_packet(packet, txBuffer, sizeof(txBuffer)))
  {
    //Serial.println("Napaka pri sifriranju!");
    displayLogOnLine(3, "LoRa ENCRYPT ERR");
    return false;
  }

  int state = radio.startTransmit(txBuffer, sizeof(txBuffer));
  if (state == RADIOLIB_ERR_NONE)
  {
    displayLogOnLine(3, "LoRa  OUT");
    return true;
  }
  else
  {
    Serial.print(F("Napaka pri zacetku posiljanja, koda: "));
    Serial.println(state);
    displayLogOnLine(3, "LoRa TX ERR");
    return false;
  }
}

//---------------------------------------------------------------------------
// Ta funkcija mora biti klicana v glavni zanki (loop)
void lora_loop()
{
  if (!operationDone)
  {
    return;
  }
  operationDone = false;

  // Preverimo, ali je bila prekinitev od pošiljanja ali prejema
  uint16_t irqFlags = radio.getIRQFlags();

  if (irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE)
  {
    radio.finishTransmit();
    //Serial.println(F("Posiljanje koncano."));
    // displayLogOnLine(3, "LoRa  TX OK");
  }
  else if (irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE)
  {
    uint8_t rxBuffer[sizeof(LoRaPacket) + 16];
    int len = radio.getPacketLength();

    if (len != sizeof(rxBuffer))
    {
      //Serial.println("Napaka: Prejet paket napacne dolzine!");
      displayLogOnLine(3, "LoRa RX LEN ERR");
    }
    else
    {
      radio.readData(rxBuffer, len);
      //Serial.println("Paket prejet, poskusam desifrirati...");
      displayLogOnLine(3, "LoRa  RX");
      // Prikaz RSSI in SNR na zaslonu in serijskem monitorju
      String debugLine = "RSSI " + String(radio.getRSSI()) + " SNR " + String(radio.getSNR());
      displayLogOnLine(5, debugLine);
      // ---------------------------

      LoRaPacket receivedPacket;
      if (decrypt_packet(rxBuffer, len, receivedPacket))
      {
        //Serial.println("Paket uspesno desifriran.");
        displayLogOnLine(3, "LoRa  DECRYPT OK");
        if (packetCallback != nullptr)
        {
          packetCallback(receivedPacket); // Kličemo glavno logiko aplikacije
        }
      }
      else
      {
        //Serial.println("Napaka: Desifriranje neuspesno (napacen kljuc ali poskodovani podatki).");
        displayLogOnLine(3, "LoRa DECRYPT ERR");
      }
    }
  }

  // Po vsaki operaciji se vrnemo v način sprejema
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE)
  {
    Serial.print(F("Napaka pri zacetku sprejema, koda: "));
    Serial.println(state);
    displayLogOnLine(3, "LoRa RX ERR");
  }
   displayLogOnLine(3, "LoRa RX READY");
}
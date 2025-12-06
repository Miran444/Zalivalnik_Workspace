/*
   RadioLib Transmit with Interrupts Example

   This example transmits packets using SX1276/SX1278/SX1262/SX1268/SX1280/LR1121 LoRa radio module.
   Each packet contains up to 256 bytes of data, in the form of:
    - Arduino String
    - null-terminated char array (C-string)
    - arbitrary binary data (byte array)

   For full API reference, see the GitHub Pages
   https://jgromes.github.io/RadioLib/
*/
#include "Lora_rele.h"
#include "display_manager.h" // Knjižnica za upravljanje z zaslonom
#include "unified_lora_handler.h"
#include "message_protocol.h"
#include "time_manager.h"
#include <time.h>
#include <sys/time.h>


// IZBERITE ENO OD TEH DVEH:
// #define IS_MASTER
#define IS_SLAVE

// Določite GPIO pine RX in TX za LoRaSerial
#define LORA_RX_PIN 13 // Prilagodite glede na vaše potrebe
#define LORA_TX_PIN 12 // Prilagodite glede na vaše potrebe

String payload = "";          // komanda za rele
uint16_t msgId = 0;     // ID sporočila

// Uporabite UART1 (lahko tudi UART2, če je na voljo)
HardwareSerial LoRaSerial(1);


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

// //---------------------------------------------------------------------------------------------------------------------
// // Funkcija za nastavitev časa iz timestamp vrednosti
// void set_time_from_timestamp(uint32_t timestamp) {
//   // Pretvorimo timestamp v čas
//   time_t t = (time_t)timestamp;
//   struct tm *timeinfo = localtime(&t);
//   if (timeinfo == NULL) {
//     // Napaka pri pretvorbi časa
//     displayLogOnLine(4, "Time PARSE ERR");
//     return;
//   }

//   // Nastavimo sistemski čas (POSIX) na modulu
//   struct timeval tv;
//   tv.tv_sec = t;
//   tv.tv_usec = 0;
//   if (settimeofday(&tv, NULL) != 0) {
//     // Če nastavljanje časa ni uspelo, zabeležimo napako na zaslon
//     displayLogOnLine(4, "Time SET ERR");
//     return;
//   }

//   // Uspešno nastavljeno
//   displayLogOnLine(4, "Time SET");
// }


//---------------------------------------------------------------------------------------------------------------------
// Ta funkcija se sproži, ko iz LoRa prejmemo veljaven paket od Masterja.
// Njena naloga je samo, da paket posreduje na Rele modul.
void handle_received_packet(const LoRaPacket& packet) {

  // LoRaPacket packetCopy;
  // memcpy(&packetCopy, &packet, sizeof(LoRaPacket)); // Naredimo kopijo paketa za nadaljnjo uporabo

  // Paket, prejet iz LoRa, samo pošljemo naprej preko serijske povezave na Rele modul.
  // Check if the relaySerial is available for writing
  int avByte = LoRaSerial.availableForWrite();
  if (avByte >= sizeof(LoRaPacket)) {
      LoRaSerial.write((const uint8_t*)&packet, sizeof(LoRaPacket));
      //displayLogOnLine(2, "Serial OUT");
  } else {
      //Serial.println("Not enough space in LoRaSerial buffer.");
      displayLogOnLine(2, "Serial TX ERR");
  }

  // String in = "In: ID: " + String(packet.messageId) + " CMD: " + String((int)packet.command);
  // displayLogOnLine(3, "LoRa IN");
  // displayLogOnLine(4, in);


  // tukaj dodamo še pregled komande od Masterja, če je komanda CommandType::CMD_SET_TIME
  // if (packet.command == CommandType::CMD_SET_TIME) {
  //     // nastavimo tudi čas na modulu
  //     uint32_t timestamp;
  //     memcpy(&timestamp, packet.payload, sizeof(uint32_t));
  //     set_time_from_timestamp(timestamp);
  //     //Serial.println("Čas nastavljen iz paketa CMD_SET_TIME.");
  //     displayLogOnLine(4, "Time SET");
  //     // ...
  // }
}

//---------------------------------------------------------------------------------------------------------------------
// Ta funkcija se sproži, ko iz Rele modula prejmemo veljaven paket preko LoRaSerial.
// Njena naloga je, da ta paket posreduje naprej na Master preko LoRa.
void forward_packet_to_master(const LoRaPacket& packet_from_rele)
{
  LoRaPacket packetCopy;
  memcpy(&packetCopy, &packet_from_rele, sizeof(LoRaPacket)); // Naredimo kopijo paketa za nadaljnjo uporabo

  // Paket, prejet iz serijske povezave, samo pošljemo naprej preko LoRa.
  // Funkcija lora_send_packet bo poskrbela za šifriranje.
  lora_send_packet(packet_from_rele);

  //Serial.printf("Posredujem paket (ID: %d, Odgovor: %d) na Masterja...\n", packet_from_rele.messageId, (int)packet_from_rele.command);
  String out = "Out: ID: " + String(packetCopy.messageId) + " CMD: " + String((int)packetCopy.command);
  displayLogOnLine(4, out);


}

//-----------------------------------------------------------------------------------------------------
// Periodično pošilja testni paket na Rele modul, dokler povezava ni potrjena.
bool testReleConnection()
{
  // Uporabimo statične spremenljivke, da ohranimo stanje med klici funkcije loop()
  static unsigned long lastTestSend = 0;
  static bool connectionToReleConfirmed = false;
  static bool firstRun = true; // Dodamo zastavico za prvi zagon

  // Če je povezava že potrjena, ne delamo ničesar več.
  if (connectionToReleConfirmed) {
    return true;
  }

  // Preverimo, ali je čas za pošiljanje novega testnega paketa.
  if (firstRun || millis() - lastTestSend > 10000) {
      lastTestSend = millis();
      firstRun = false; // Po prvem pošiljanju nastavimo zastavico na false
      //Serial.println("--> TEST: Posiljam testni paket (CMD_TEST) na Rele modul...");
      displayLogOnLine(2, "Serial TEST");
      
      // Sestavimo testni paket
      LoRaPacket testPacket;
      testPacket.syncWord = LORA_SYNC_WORD;
      testPacket.messageId = 9999; // Uporabimo poseben ID za test
      testPacket.command = CommandType::CMD_TEST;
      memset(testPacket.payload, 0, sizeof(testPacket.payload));

      // Izračunamo CRC
      testPacket.crc = calculate_crc((const uint8_t*)&testPacket, offsetof(LoRaPacket, crc));

      // Pošljemo paket na Rele modul
      LoRaSerial.write((const uint8_t*)&testPacket, sizeof(LoRaPacket));
  }

  // Preverimo, ali je medtem prispel kakršenkoli odgovor od Rele modula.
  if (LoRaSerial.available() > 0) {
      connectionToReleConfirmed = true;
      //Serial.println("--> TEST: Odgovor od Rele modula zaznan! Povezava deluje. Ustavljam testno posiljanje.");
      displayLogOnLine(2, "Serial OK");
      displayLogOnLine(4, "");

      // Izbrišemo morebitne preostale podatke v bufferju
      while (LoRaSerial.available() > 0) {
          LoRaSerial.read();
      }
  }
  return connectionToReleConfirmed;
}


//---------------------------------------------------------------------------------------------------------------------
void setup()
{
  pinMode(onboard_led.pin, OUTPUT); // onboard LED is output
  onboard_led.on = false;           // turn off the LED
  onboard_led.update();             // update the LED state

  Serial.begin(115200); // Inicializiraj serijsko komunikacijo za debug

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN); // Inicializiraj LoRaSerial UART1
  // Počakaj trenutek, da se serijska povezava stabilizira
  delay(100);

  // Počisti prejemni buffer
  while (LoRaSerial.available() > 0)
  {
    LoRaSerial.read(); // Preberi in ignoriraj vse podatke v bufferju
  }

  Serial.println("Serijski buffer za LoRaSerial je bil očiščen.");

  init_display(); // Inicializiraj OLED zaslon

  displayLogOnLine(1, "Slave LoRa");
  displayLogOnLine(2, "Inicializacija...");
  displayLogOnLine(3, "OLED OK");
  delay(500);

  displayLogOnLine(3, "Lora INIT");
  // Inicializacija LoRa z našo callback funkcijo
  lora_initialize(handle_received_packet);

  displayLogOnLine(2, "Setup board...OK");
  delay(500);
}


//----------------------------------------------------------------------------------------------------------------------
void loop()
{
  // 1. Obdelaj dogodke iz LoRa (prejem/pošiljanje)
  lora_loop();

  // 2. Periodično preverjaj povezavo z Rele modulom, dokler ni potrjena.
  if (testReleConnection())
  {
    // Povezava je potrjena, nadaljujemo z branjem paketov iz LoRaSerial
    // 3. Robustno branje serijskega porta za normalno delovanje
    while (LoRaSerial.available() > 0)
    {
      // Preberemo prvi bajt iz bufferja
      uint8_t firstByte = LoRaSerial.read();

      // Preverimo, ali se ujema s prvim bajtom našega Sync Worda
      if (firstByte == (LORA_SYNC_WORD & 0xFF))
      {
        // Prvi bajt se ujema. Počakamo na drugega.
        // Uporabimo kratek timeout, da se ne zataknemo za vedno.
        unsigned long startTime = millis();
        while (LoRaSerial.available() == 0 && millis() - startTime < 10)
        {
          // Počakamo na drugi bajt
          delay(1);
        }

        // Če drugi bajt ni prišel, nadaljujemo z zunanjim while ciklom
        if (LoRaSerial.available() == 0)
        {
          continue;
        }

        // Preberemo drugi bajt
        uint8_t secondByte = LoRaSerial.read();

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
          while (LoRaSerial.available() < remainingBytes && millis() - startTime < 100)
          {
            delay(1);
          }

          if (LoRaSerial.available() < remainingBytes)
          {
            // Timeout! Nismo prejeli celotnega paketa. Zavržemo, kar smo našli.
            //Serial.println("Napaka: Timeout po najdbi Sync Worda. Paket ni popoln.");
            displayLogOnLine(2, "Serial RX TIMEOUT");
            continue;
          }

          // Preberemo preostanek paketa v naš buffer
          LoRaSerial.readBytes(packetBuffer + 2, remainingBytes);

          // Sedaj imamo celoten paket v `packetBuffer`.
          // Prepišemo ga v LoRaPacket strukturo in obdelamo.
          LoRaPacket receivedPacket;
          memcpy(&receivedPacket, packetBuffer, sizeof(LoRaPacket));
          displayLogOnLine(2, "Serial IN");
          forward_packet_to_master(receivedPacket);

          // Nadaljujemo z zanko, da takoj preverimo za naslednji paket
          continue;
        }
      }

      // Če smo prišli do sem, prvi bajt ni bil del Sync Worda (ali pa drugi ni bil pravilen).
      // To so "smeti", ki jih moramo zavreči.
      // Serial.print("Zavracam smeti: 0x");
      // Serial.println(firstByte, HEX);
    }
  }
  onboard_led.blink(500, 500); // LED blinka vsakih 500ms
}
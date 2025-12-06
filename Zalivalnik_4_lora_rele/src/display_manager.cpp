#include "display_manager.h"
#include "Lora_rele.h" // Potrebujemo za dostop do 'kanal' strukture v PrikaziStanjeRelejev

// Definicija in inicializacija U8g2 objekta. To se zgodi samo tukaj.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// Zunanja spremenljivka iz main.cpp, ki jo potrebujemo tukaj
extern Kanal kanal[8];

// Globalni buffer za trenutno vsebino vrstic na OLED
String currentLines[5] = {"", "", "", "", ""};

// Funkcija za inicializacijo zaslona (prej v setup())
void init_display()
{
  u8g2.begin();
  u8g2.clearBuffer();
  // u8g2.setFont(u8g2_font_6x10_tf);
  // u8g2.setCursor(0, 10);
  // u8g2.print("Zalivalnik 4 Lora Master");
  // u8g2.setCursor(0, 22);
  // u8g2.print("Verzija 1.0");
  // u8g2.sendBuffer();
  // delay(2000);
  // u8g2.clearBuffer();
  // u8g2.sendBuffer();
}

  //---------------------------------------------------------------------------------------------------------------------
// Funkcija za izpis nove vsebine v določeno vrstico
void displayLogOnLine(uint8_t line, const String &newLineString)
{
  if (line < 1 || line > 5)
  {
    Serial.println("Napaka: Vrstica mora biti med 1 in 5!");
    return;
  }

  uint8_t lineIndex = line - 1; // Pretvori vrstico (1-5) v indeks (0-4)

  if (currentLines[lineIndex] != newLineString)
  {                                          // Posodobi samo, če je vsebina drugačna
    currentLines[lineIndex] = newLineString; // Posodobi globalni buffer

    // Izračunaj Y koordinato za dano vrstico
    uint8_t yPosition = 12 * (lineIndex + 1) - 2;

    // Počisti samo določeno vrstico
    u8g2.setDrawColor(0);                     // Nastavi barvo na "črno" za brisanje
    u8g2.drawBox(0, 12 * lineIndex, 128, 12); // Počisti vrstico
    u8g2.setDrawColor(1);                     // Nastavi barvo nazaj na "belo" za risanje

    // Nariši novo vsebino vrstice
    if (!newLineString.isEmpty())
    {
      u8g2.setFont(u8g2_font_ncenB08_tr); // Izberite font
      u8g2.drawStr(0, yPosition, newLineString.c_str());
    }

    // Pošlji posodobljen buffer na zaslon
    u8g2.sendBuffer();

    // Izpiši na Serial za debug
    Serial.println(String("Vrstica ") + String(line) + ": " + newLineString);
  }
}



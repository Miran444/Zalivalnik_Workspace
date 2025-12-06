#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

// Extern deklaracija pove prevajalniku, da ta objekt obstaja nekje drugje (v display_manager.cpp).
// Tako lahko do njega dostopajo tudi druge datoteke, če bi bilo potrebno.
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// I2C pini za OLED
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST U8X8_PIN_NONE // Ali npr. 16, če ga vaša plošča ima in ga želite uporabiti

// Deklaracije funkcij za upravljanje zaslona
void init_display();
void displayLogOnLine(uint8_t line, const String &newLineString);
// void drawMain();
// void PrikaziStanjeRelejev();
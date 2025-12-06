#pragma once

#include <Arduino.h>

// Deklaracija funkcije za razčlenjevanje sporočil iz LoRa rele modula.
// Vrne 'true', če je bilo sporočilo uspešno prepoznano in obdelano.
bool parseLoraMessage(const String &message);
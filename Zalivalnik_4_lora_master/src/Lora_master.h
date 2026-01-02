
#pragma once


#include <RadioLib.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Struktura za shranjevanje podatkov o relejih
struct Kanal
{
  bool state;    // Stanje releja (vklopljen/izklopljen)
  char start[6]; // Začetni čas (HH:MM)
  int start_sec; // Začetni čas v sekundah od polnoči
  char end[6];   // Končni čas (HH:MM)
  int end_sec;   // Končni čas v sekundah od polnoči
};

// Struktura za prenos posodobljenih podatkov o kanalu iz Firebase streaminga
struct ChannelUpdateData {
  int kanalIndex = -1;
  int start_sec = -1;
  int end_sec = -1;
};

// Function to print a value in binary format
void printBinary(uint16_t value);

// Function to display the state of the relays
void PrikaziStanjeRelejevNaSerial(); 

// Funkcija za update urnikov relejev v Rele modulu
void Rele_updateRelayUrnik(uint8_t index, uint32_t startSec, uint32_t endSec);

extern FirebaseApp app;


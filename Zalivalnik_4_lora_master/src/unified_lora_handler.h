#pragma once
#include "message_protocol.h"
#include "display_manager.h" // Knjižnica za upravljanje z zaslonom

// Tip funkcije (callback), ki jo bo handler klical ob prejemu veljavnega paketa
using PacketHandlerCallback = void (*)(const LoRaPacket& packet);

void lora_initialize(PacketHandlerCallback callback);
//void lora_loop();
bool lora_send_packet(const LoRaPacket& packet);
// --- NOVO: Funkciji za upravljanje stanja čakanja ---
void lora_set_waiting_for_response(bool waiting); // Nastavi stanje čakanja na odgovor
bool lora_is_busy();  // Preveri, ali je LoRa zasedena (pošilja ali čaka na odgovor)
bool lora_is_waiting_for_response(); // Preveri, ali čakamo na odgovor
// Deklaracije za nabiralnik prejetih paketov ---
extern volatile bool lora_new_packet_available;
extern LoRaPacket lora_received_packet;
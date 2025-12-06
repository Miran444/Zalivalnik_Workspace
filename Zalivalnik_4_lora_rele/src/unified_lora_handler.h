#pragma once
#include "message_protocol.h"

// Tip funkcije (callback), ki jo bo handler klical ob prejemu veljavnega paketa
using PacketHandlerCallback = void (*)(const LoRaPacket& packet);

void lora_initialize(PacketHandlerCallback callback);
void lora_loop();
bool lora_send_packet(const LoRaPacket& packet);
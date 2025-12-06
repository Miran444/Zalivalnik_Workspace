#pragma once
#include "message_protocol.h"

void crypto_init();
bool encrypt_packet(const LoRaPacket& packet, uint8_t* buffer, size_t buffer_len);
bool decrypt_packet(const uint8_t* buffer, size_t buffer_len, LoRaPacket& packet);
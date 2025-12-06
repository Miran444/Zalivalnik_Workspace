#include "crypto_manager.h"
#include <Crypto.h>
#include <AES.h>
#include <GCM.h>

// !!! POMEMBNO: Ta ključ mora biti enak na obeh napravah! !!!
// Generirajte si svojega naključnega 16-bajtnega (128-bit) ključa.
static const uint8_t AES_KEY[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

// Inicializacijski vektor (IV) - 12 bajtov za GCM.
// Za večjo varnost bi se moral pošiljati z vsakim sporočilom, a za test je lahko statičen.
static const uint8_t AES_IV[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
};

GCM<AES128> gcm;

void crypto_init() {
    gcm.setKey(AES_KEY, sizeof(AES_KEY));
    gcm.setIV(AES_IV, sizeof(AES_IV));
}

bool encrypt_packet(const LoRaPacket& packet, uint8_t* buffer, size_t buffer_len) {
    if (buffer_len < sizeof(LoRaPacket) + 16) { // 16 bajtov za GCM avtentikacijsko oznako
        return false;
    }
            // --- KLJUČNA SPREMEMBA: Ponastavi stanje pred vsakim šifriranjem ---
    gcm.setKey(AES_KEY, sizeof(AES_KEY));
    gcm.setIV(AES_IV, sizeof(AES_IV));
    // -----------------------------------------------------------------
    gcm.encrypt(buffer, (const uint8_t*)&packet, sizeof(LoRaPacket));
    gcm.computeTag(buffer + sizeof(LoRaPacket), 16);
    return true;
}

bool decrypt_packet(const uint8_t* buffer, size_t buffer_len, LoRaPacket& packet) {
    if (buffer_len < sizeof(LoRaPacket) + 16) {
        return false;        
    }
        // --- KLJUČNA SPREMEMBA: Ponastavi stanje pred vsakim dešifriranjem ---
    gcm.setKey(AES_KEY, sizeof(AES_KEY));
    gcm.setIV(AES_IV, sizeof(AES_IV));
    // -----------------------------------------------------------------
    gcm.decrypt((uint8_t*)&packet, buffer, sizeof(LoRaPacket));
    return gcm.checkTag(buffer + sizeof(LoRaPacket), 16);
}
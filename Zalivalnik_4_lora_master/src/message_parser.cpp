#include "message_parser.h"
#include "Lora_master.h" // Potrebujemo za 'Kanal' strukturo
// #include "main.h"        // Potrebujemo za dostop do zunanjih funkcij in spremenljivk

// --- Zunanje spremenljivke iz main.cpp, ki jih ta modul potrebuje ---
extern Kanal kanal[8];
extern float Temperature;
extern float Humidity;
extern float Soil_Moisture;
extern bool rele_senzor_read;
extern int test_step;

// --- Zunanje funkcije iz main.cpp ---
extern void PrikaziStanjeRelejevNaSerial();

// Glavna funkcija za razčlenjevanje sporočil
bool parseLoraMessage(const String &message)
{
    // ACK:6-9;K1: OFF,K2: OFF,K3: OFF,K4: OFF,K5: OFF,K6: OFF,K7: OFF,K8: OFF,
    // --- Sporočilo o stanju VSEH relejev (odgovor na ukaz "status") ---
    // Format: "S:10101010"
    if (message.startsWith("S:"))
    {
        String states = message.substring(2);
        if (states.length() == 8)
        {
            for (int i = 0; i < 8; i++)
            {
                kanal[i].state = (states.charAt(i) == '1');
            }
            PrikaziStanjeRelejevNaSerial(); // Izpišemo novo stanje na serijski monitor
            return true;
        }
    }

    // --- Sporočilo o urniku POSAMEZNEGA kanala (odgovor na ukaz "urnik X") ---
    // Format: "K1,S-08:00,E-09:00"
    else if (message.startsWith("K") && message.indexOf(',') > 0)
    {
        int kanalId = message.substring(1, message.indexOf(',')).toInt();
        if (kanalId >= 1 && kanalId <= 8)
        {
            int channelIndex = kanalId - 1;
            // Uporaba sscanf za bolj robustno parsiranje urnika
            int startH, startM, endH, endM;
            if (sscanf(message.c_str(), "K%*d,S-%d:%d,E-%d:%d", &startH, &startM, &endH, &endM) == 4)
            {
                snprintf(kanal[channelIndex].start, 6, "%02d:%02d", startH, startM);
                snprintf(kanal[channelIndex].end, 6, "%02d:%02d", endH, endM);
                kanal[channelIndex].start_sec = startH * 3600 + startM * 60;
                kanal[channelIndex].end_sec = endH * 3600 + endM * 60;
                return true;
            }
        }
    }

    // --- Sporočilo s podatki senzorjev (odgovor na ukaz "senzor") ---
    // Format: "T: 23.40, H: 55.10, V: 45.50"
    else if (message.startsWith("T:"))
    {
        // Uporaba sscanf za robustno parsiranje float vrednosti
        float temp, hum, soil;
        if (sscanf(message.c_str(), "T: %f, H: %f, V: %f", &temp, &hum, &soil) == 3)
        {
            Temperature = temp;
            Humidity = hum;
            Soil_Moisture = soil;
            rele_senzor_read = true;
            return true;
        }
    }

    // --- ACK sporočila ---
    else if (message.startsWith("ACK"))
    {
        // Primer: "ACK: update OK"
        if (message.indexOf("update OK") > 0)
        {
            // To je bil odgovor na testni ukaz
            if (test_step == 2)
            {
                test_step = 3; // Premakni testno sekvenco naprej
            }
            return true;
        }
        // Dodajte obdelavo za druge ACK odgovore, če je potrebno
        return true; // Prepoznali smo ACK, tudi če ne naredimo nič
    }

    // Če sporočilo ne ustreza nobenemu znanemu formatu
    Serial.println("Neznano sporočilo: " + message);
    return false;
}
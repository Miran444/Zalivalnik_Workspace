# Firebase INA3221 Sensor Data Integration

## Pregled (Overview)

Ta projekt vsebuje funkcijo `Firebase_Update_INA3221_Data()`, ki omogoča vpis podatkov iz INA3221 senzorja v Firebase Realtime Database v gnezdeni JSON strukturi.

## Struktura podatkov v Firebase

Podatki se shranjujejo v naslednji strukturi:

```
/UserData/{uid}/sensors/
  ├── ina3221_device_1/
  │   └── readings/
  │       ├── 1678886400/
  │       │   ├── alert_flags: 0
  │       │   ├── shunt_voltage_sum_mV: 150.0
  │       │   ├── total_current_mA: 200.0
  │       │   ├── ch0/
  │       │   │   ├── bus_voltage_V: 5.1
  │       │   │   ├── shunt_voltage_mV: 10.0
  │       │   │   ├── current_mA: 50.0
  │       │   │   └── power_mW: 250.0
  │       │   ├── ch1/
  │       │   │   ├── bus_voltage_V: 5.0
  │       │   │   ├── shunt_voltage_mV: 12.0
  │       │   │   ├── current_mA: 60.0
  │       │   │   └── power_mW: 300.0
  │       │   └── ch2/
  │       │       ├── bus_voltage_V: 4.9
  │       │       ├── shunt_voltage_mV: 8.0
  │       │       ├── current_mA: 40.0
  │       │       └── power_mW: 196.0
  │       └── 1678886460/
  │           └── ... (naslednje meritve)
  └── ina3221_device_2/
      └── ... (podatki druge naprave)
```

## JSON format

Podatki se v Firebase shranijo kot:

```json
{
  "sensors": {
    "ina3221_device_1": {
      "readings": {
        "1678886400": {
          "alert_flags": 0,
          "shunt_voltage_sum_mV": 150.0,
          "total_current_mA": 200.0,
          "ch0": {
            "bus_voltage_V": 5.1,
            "shunt_voltage_mV": 10.0,
            "current_mA": 50.0,
            "power_mW": 250.0
          },
          "ch1": {
            "bus_voltage_V": 5.0,
            "shunt_voltage_mV": 12.0,
            "current_mA": 60.0,
            "power_mW": 300.0
          },
          "ch2": {
            "bus_voltage_V": 4.9,
            "shunt_voltage_mV": 8.0,
            "current_mA": 40.0,
            "power_mW": 196.0
          }
        }
      }
    }
  }
}
```

## Uporaba

### Include datoteke

```cpp
#include "Firebase_z.h"
#include "INA3221.h"
```

### Inicializacija

```cpp
// Ustvari INA3221 objekt (globalna spremenljivka)
INA3221 INA(0x40);

void setup() {
  // Inicializiraj Firebase
  Init_Firebase();
  Firebase_Connect();
  
  // Inicializiraj INA3221
  INA.begin();
  INA.setMode(7); // Continuous measurement
}
```

### Primer uporabe funkcije

```cpp
void loop() {
  static unsigned long lastReadTime = 0;
  
  // Beri senzor vsakih 60 sekund
  if (millis() - lastReadTime >= 60000) {
    lastReadTime = millis();
    
    // Pridobi trenutni čas (Unix timestamp)
    unsigned long timestamp = time(nullptr);
    
    // Preberi alert flags
    uint16_t alert_flags = INA.getMaskEnable();
    
    // Izračunaj skupne vrednosti
    float shunt_sum = INA.getShuntVoltageSum() / 1000.0; // μV -> mV
    float total_current = INA.getCurrent_mA(0) + 
                          INA.getCurrent_mA(1) + 
                          INA.getCurrent_mA(2);
    
    // Preberi podatke za vse tri kanale
    float ch0_bus = INA.getBusVoltage(0);
    float ch0_shunt = INA.getShuntVoltage_mV(0);
    float ch0_current = INA.getCurrent_mA(0);
    float ch0_power = INA.getPower(0) * 1000.0; // W -> mW
    
    float ch1_bus = INA.getBusVoltage(1);
    float ch1_shunt = INA.getShuntVoltage_mV(1);
    float ch1_current = INA.getCurrent_mA(1);
    float ch1_power = INA.getPower(1) * 1000.0;
    
    float ch2_bus = INA.getBusVoltage(2);
    float ch2_shunt = INA.getShuntVoltage_mV(2);
    float ch2_current = INA.getCurrent_mA(2);
    float ch2_power = INA.getPower(2) * 1000.0;
    
    // Pošlji v Firebase
    Firebase_Update_INA3221_Data(
      "ina3221_device_1",  // Device ID
      timestamp,           // Unix timestamp
      alert_flags,         // Alert flags
      shunt_sum,          // Total shunt voltage (mV)
      total_current,      // Total current (mA)
      ch0_bus, ch0_shunt, ch0_current, ch0_power,  // Channel 0
      ch1_bus, ch1_shunt, ch1_current, ch1_power,  // Channel 1
      ch2_bus, ch2_shunt, ch2_current, ch2_power   // Channel 2
    );
    
    Serial.println("INA3221 data sent to Firebase!");
  }
}
```

## Parametri funkcije

| Parameter | Tip | Opis |
|-----------|-----|------|
| `device_id` | `const char*` | Identifikator naprave (npr. "ina3221_device_1") |
| `timestamp` | `unsigned long` | Unix timestamp (sekunde od 1.1.1970) |
| `alert_flags` | `uint16_t` | Alert flags iz INA3221 mask/enable registra |
| `shunt_voltage_sum_mV` | `float` | Skupna shunt napetost v miliVoltih |
| `total_current_mA` | `float` | Skupni tok vseh kanalov v miliAmperih |
| `ch0_bus_V` | `float` | Bus napetost kanala 0 v Voltih |
| `ch0_shunt_mV` | `float` | Shunt napetost kanala 0 v miliVoltih |
| `ch0_current_mA` | `float` | Tok kanala 0 v miliAmperih |
| `ch0_power_mW` | `float` | Moč kanala 0 v miliWattih |
| `ch1_bus_V` | `float` | Bus napetost kanala 1 v Voltih |
| `ch1_shunt_mV` | `float` | Shunt napetost kanala 1 v miliVoltih |
| `ch1_current_mA` | `float` | Tok kanala 1 v miliAmperih |
| `ch1_power_mW` | `float` | Moč kanala 1 v miliWattih |
| `ch2_bus_V` | `float` | Bus napetost kanala 2 v Voltih |
| `ch2_shunt_mV` | `float` | Shunt napetost kanala 2 v miliVoltih |
| `ch2_current_mA` | `float` | Tok kanala 2 v miliAmperih |
| `ch2_power_mW` | `float` | Moč kanala 2 v miliWattih |

## Opombe

1. **Asinhrona komunikacija**: Funkcija deluje asinhrono. Odgovor bo prejet v callback funkciji `Firebase_processResponse()` s task ID "updateINA3221Task".

2. **Natančnost**: Vse vrednosti se shranjujejo s 3 decimalnimi mesti natančnosti.

3. **Timestamp kot ključ**: Unix timestamp se uporablja kot ključ v Firebase strukturi, kar omogoča enostavno časovno urejanje podatkov.

4. **Več naprav**: Z različnimi device_id lahko shranjujete podatke iz več INA3221 senzorjev v isti Firebase bazi.

5. **Firebase pot**: Podatki se shranjujejo na poti:  
   `/UserData/{uid}/sensors/{device_id}/readings/{timestamp}`

## Preverjanje uspešnosti

Odgovor lahko spremljate v Serial monitorju:

```
[FIREBASE] task: updateINA3221Task, payload: ...
[FIREBASE] INA3221 sensor data uploaded
```

## Povezane datoteke

- `Firebase_z.h` - Deklaracija funkcije
- `Firebase_z.cpp` - Implementacija funkcije
- `INA3221_Firebase_Example.txt` - Dodatni primeri uporabe

## Licence

Ta koda je del Zalivalnik projekta in uporablja:
- FirebaseClient knjižnico (Mobizt)
- INA3221 knjižnico (Rob Tillaart)

## Avtorji

- Implementacija: GitHub Copilot
- Projekt: Miran444

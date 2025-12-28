#include "Firebase_z.h"
#include "wifi_manager.h"
#include "sensor_queue.h"
//------------------------------------------------------------------------------------------------------------------------

// Firebase Zalivalnik2 credentials
#define FIREBASE_API_KEY "AIzaSyDzTD6IjH7gSuAiHyRQoZtc-A-VTIT-4Iw"
#define FIREBASE_DATABASE_URL "https://zalivalnik2-default-rtdb.europe-west1.firebasedatabase.app/"
#define FIREBASE_USER_EMAIL "miranbudal@gmail.com"
#define FIREBASE_USER_PASSWORD "vonja444"
#define FIREBASE_USER_UID "68TA11sAdOaFnIdmqzDtkx3QIBC2"
//--------------------------------------------------------------------------------------------------------

// Authentication
UserAuth user_auth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD);

// --- SPREMENJENO: Uporaba char nizov namesto String objektov ---
// Database main path (to be updated in setup with the user UID)
char databasePath[48];
char sensorPath[64];
char inaPath[64];
char kanaliPath[64];
char chartIntervalPath[72];
char uid[32];

bool relayState[8]; // Stanja relejev (za 8 relejev) false = OFF, true = ON
bool status;        // Spremenljivka za shranjevanje statusa operacij
bool ssl_avtentikacija = false; // Spremenljivka za shranjevanje stanja SSL avtentikacije

// DODAJ retry mehanizem:
static unsigned long lastFirebaseOperationTime = 0;
static uint8_t firebaseRetryCount = 0;
const uint8_t MAX_FIREBASE_RETRIES = 3;
const unsigned long FIREBASE_RESPONSE_TIMEOUT = 10000; // 10 sekund

// Struktura za shranjevanje zadnje operacije (za retry)
struct LastFirebaseOperation {
    enum class Type { NONE, UPDATE_SENSOR, UPDATE_INA, UPDATE_RELAY, GET_URNIK, GET_INTERVAL } type;
    union {
        struct {
            unsigned long timestamp;
            SensorDataPayload data;
        } sensor;
        struct {
            unsigned long timestamp;
            INA3221_DataPayload data;
        } ina;
        struct {
            int kanal;
            bool state;
        } relay;
        struct {
            uint8_t kanalIndex;
        } urnik;
    } data;
    bool waiting_for_response;
} lastOperation = {LastFirebaseOperation::Type::NONE, {}, false};


//------------------------------------------------------------------------------------------------------------------------
// Funkcija za inicializacijo Firebase
void Init_Firebase()
{
  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setTimeout(10000);
  ssl_client.setHandshakeTimeout(15);

  // Configure stream SSL client
  stream_ssl_client.setInsecure();
  stream_ssl_client.setTimeout(5000);
  stream_ssl_client.setHandshakeTimeout(10);

  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), Firebase_processResponse, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(FIREBASE_DATABASE_URL);

}

//------------------------------------------------------------------------------------------------------------------------
// Function for connecting to Firebase for getting UID and setting database paths
void Firebase_Connect()
{
  strncpy(uid, app.getUid().c_str(), sizeof(uid) - 1);
  uid[sizeof(uid) - 1] = '\0'; // Zagotovimo null-terminacijo

  snprintf(databasePath, sizeof(databasePath), "/UserData/%s", uid);
  snprintf(sensorPath, sizeof(sensorPath), "%s/Sensors", databasePath);
  snprintf(inaPath, sizeof(inaPath), "%s/INA3221", databasePath);
  snprintf(kanaliPath, sizeof(kanaliPath), "%s/Kanali/kanal", databasePath);
  snprintf(chartIntervalPath, sizeof(chartIntervalPath), "%s/charts/Interval", databasePath);

  Firebase.printf("[F_CONNECT] Free Heap: %d\n", ESP.getFreeHeap());

  streamClient.setSSEFilters("put,patch,cancel");
  Database.get(streamClient, databasePath, streamCallback, true /* SSE mode (HTTP Streaming) */, "mainStreamTask");

}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za preverjanje če je Firebase pripravljen
bool Firebase_IsReady()
{
  // DODAJ PREVERJANJE PROSTEGA HEAP-a PRED OPERACIJO
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 30000) { // Potrebuješ vsaj ~30KB za SSL
    Firebase.printf("[F_READY] OPOZORILO: Premalo prostega heap-a: %d bajtov. Preskakujem posodobitev.\n", freeHeap);
    return false;
  }

  if (!app.ready())
  {
    Firebase.printf("[F_READY] Firebase ni pripravljen za pošiljanje INA podatkov.\n");
    return false;
  }

  // NOVO: Počakaj na avtentikacijo
  if (!ssl_avtentikacija) {
    Firebase.printf("[F_READY] Avtentikacija v teku, čakam...\n");
    return false;
  }

  return true;
}

//------------------------------------------------------------------------------------------------------------------------
// Callback funkcija za obdelavo sprememb iz Firebase stream-a
void streamCallback(AsyncResult &aResult)
{
  // Exits when no result is available when calling from the loop.
  if (!aResult.isResult()) return;

  if (aResult.isEvent()) Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  if (aResult.isDebug()) Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  if (aResult.isError()) Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

  if (aResult.available())
  {
    RealtimeDatabaseResult &stream = aResult.to<RealtimeDatabaseResult>();
    if (stream.isStream())
    {
      Serial.println("----------------------------");
      Firebase.printf("[STREAM] event: %s\n", stream.event().c_str());

      // PRAVILNO: Najprej shrani v String objekt
      String path_str = stream.dataPath();
      const char* path = path_str.c_str();  // Zdaj je kazalec veljaven
      Firebase.printf("[STREAM] String path: %s\n", path);

      //-----------------------------------------------------------------------------------------
      // Ali je sprememba znotraj /Kanali?
      if (strncmp(path, "/Kanali", 7) == 0)
      {
        // PRAV TAKO shranimo payload v String
        String payload_str = stream.data();
        const char* payload = payload_str.c_str();  // Kazalec na veljavno pomnilniško lokacijo
        Firebase.printf("[STREAM] Payload: %s\n", payload);

        int kanalIndex = -1;
        const char* kanalStr = strstr(payload, "kanal");
        if (kanalStr != NULL) {
            // Preskočimo "kanal" in pretvorimo številko v int
            kanalIndex = atoi(kanalStr + 5);
        }

        if (kanalIndex != -1)
        {
          // Preveri, ali je sprememba v 'state' polju
          const char* stateKey = "\"state\":";
          const char* statePtr = strstr(payload, stateKey);
          if (statePtr != NULL) {
            Serial.printf("[STREAM] Zaznana sprememba stanja, ne urnika. Ignoriram.\n");
            return; // NE pošiljamo nazaj na Rele!
          }

          // Samo urnik (start_sec/end_sec) procesiramo naprej
          int startSec = firebase_kanal[kanalIndex - 1].start_sec;
          int endSec = firebase_kanal[kanalIndex - 1].end_sec;
          bool dataChanged = false; // Zastavica, ki pove, ali je prišlo do spremembe

          // Preverimo, ali se je spremenil 'start_sec'
          const char* startSecKey = "start_sec\":";
          const char* startSecPtr = strstr(payload, startSecKey);
          if (startSecPtr != NULL)
          {
            // Premaknemo kazalec za dolžino ključa, da pridemo do vrednosti
            startSec = atoi(startSecPtr + strlen(startSecKey));
            Firebase.printf("[STREAM] Kanal %d je spremenjen, nov začetni čas: %d\n", kanalIndex, startSec);
            firebase_kanal[kanalIndex - 1].start_sec = startSec;
            dataChanged = true;
          }

          // Preverimo, ali se je spremenil 'end_sec'
          const char* endSecKey = "end_sec\":";
          const char* endSecPtr = strstr(payload, endSecKey);
          if (endSecPtr != NULL)
          {
            // Premaknemo kazalec za dolžino ključa, da pridemo do vrednosti
            endSec = atoi(endSecPtr + strlen(endSecKey));
            Firebase.printf("[STREAM] Kanal %d je spremenjen, nov končni čas: %d\n", kanalIndex, endSec);
            firebase_kanal[kanalIndex - 1].end_sec = endSec;
            dataChanged = true;
          }
          // Pošljemo posodobitev samo, če je dejansko prišlo do spremembe
          if (dataChanged)
          {
            // Nastavimo zastavico, da je na voljo nova posodobitev za pošiljanje na rele
            channelUpdate.kanalIndex = kanalIndex;
            channelUpdate.start_sec = startSec;
            channelUpdate.end_sec = endSec;
            newChannelDataAvailable = true;
          }
        }
      }
      //-----------------------------------------------------------------------------------------
      // Ali je sprememba poti /charts/Interval?
      else if (strcmp(path, "/charts/Interval") == 0)
      {
        uint8_t chartInterval = stream.to<uint8_t>();
        set_Interval(chartInterval);
        Firebase.printf("[STREAM] Nastavljen interval branja senzorjev: %d minut\n", chartInterval);
      }
    }
    else
    {
      Serial.println("----------------------------");
      Firebase.printf("[STREAM] task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
    }
  }
}

//-----------------------------------------------------------------------------------------------------------
// Funkcija za obdelavo posodobljenih podatkov iz Streaminga Firebase
void Firebase_handleStreamUpdate(int kanalIndex, int start_sec, int end_sec)
{
  int index = kanalIndex - 1;
  if (index >= 0 && index < 8)
  {
    if (start_sec != -1)
    {
      firebase_kanal[index].start_sec = start_sec;
      formatSecondsToTime(firebase_kanal[index].start, sizeof(firebase_kanal[index].start), start_sec);
    }
    if (end_sec != -1)
    {
      firebase_kanal[index].end_sec = end_sec;
      formatSecondsToTime(firebase_kanal[index].end, sizeof(firebase_kanal[index].end), end_sec);
    }

    if (lora_is_busy())
    {
      Firebase.printf("[STREAM] LoRa zasedena. Shranjujem posodobitev za kasneje.\n");
      pendingUpdateData.kanalIndex = index;
      pendingUpdateData.start_sec = firebase_kanal[index].start_sec;
      pendingUpdateData.end_sec = firebase_kanal[index].end_sec;
      firebaseUpdatePending = true;
    }
    else
    {
      Firebase.printf("[STREAM] LoRa prosta. Pošiljam posodobitev takoj.\n");
      Rele_updateRelayUrnik(index, firebase_kanal[index].start_sec, firebase_kanal[index].end_sec);
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov iz Firebase (chart interval)
void Firebase_readInterval()
{
  if (!ssl_avtentikacija || !app.ready())
  {
    Firebase.printf("[F_GET_INTERVAL] Firebase ni pripravljen.\n");
    return;
  }

  Firebase.printf("[F_GET_INTERVAL] Reading interval (poskus %d/%d)...\n",
                  firebaseRetryCount + 1, MAX_FIREBASE_RETRIES);

  lastOperation.type = LastFirebaseOperation::Type::GET_INTERVAL;
  lastOperation.waiting_for_response = true;
  lastFirebaseOperationTime = millis();
  firebase_response_received = false;

  Database.get(aClient, chartIntervalPath, Firebase_processResponse, false, "getChartIntervalTask");
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija ki prebere urnik iz Firebase in ga shrani v globalno spremenljivko firebase_kanal
void Firebase_readKanalUrnik(uint8_t kanalIndex)
{
  if (!ssl_avtentikacija || !app.ready())
  {
    Firebase.printf("[F_GET_URNIK] Firebase ni pripravljen.\n");
    return;
  }

  char path_buffer[96];
  snprintf(path_buffer, sizeof(path_buffer), "%s%d", kanaliPath, kanalIndex + 1);

  Firebase.printf("[F_GET_URNIK] Reading schedule (poskus %d/%d)...\n",
                  firebaseRetryCount + 1, MAX_FIREBASE_RETRIES);

  // NOVO: Shrani operacijo
  lastOperation.type = LastFirebaseOperation::Type::GET_URNIK;
  lastOperation.data.urnik.kanalIndex = kanalIndex;
  lastOperation.waiting_for_response = true;
  lastFirebaseOperationTime = millis();
  firebase_response_received = false;

  Database.get(aClient, path_buffer, Firebase_processResponse, false, "getUrnikTask");
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov v Firebase (eno polje kanala)
void Firebase_Update_Relay_State(int kanal, bool state)
{
  if (!ssl_avtentikacija || !app.ready())
  {
    Firebase.printf("[F_UPDATE_RELAY] Firebase ni pripravljen.\n");
    return;
  }

  char path_buffer[100];
  snprintf(path_buffer, sizeof(path_buffer), "%s%d/state", kanaliPath, kanal);
  const char *state_payload = state ? "ON" : "OFF";

  Firebase.printf("[F_UPDATE_RELAY] Sending relay state (poskus %d/%d)...\n",
                  firebaseRetryCount + 1, MAX_FIREBASE_RETRIES);

  // NOVO: Shrani operacijo
  lastOperation.type = LastFirebaseOperation::Type::UPDATE_RELAY;
  lastOperation.data.relay.kanal = kanal;
  lastOperation.data.relay.state = state;
  lastOperation.waiting_for_response = true;
  lastFirebaseOperationTime = millis();
  firebase_response_received = false;

  Database.set(aClient, path_buffer, state_payload, Firebase_processResponse, "updateStateTask");
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov senzorjev v Firebase
void Firebase_Update_Sensor_Data(unsigned long timestamp, const SensorDataPayload &sensors)
{
  // Pripravimo bufferje za pretvorbo float vrednosti v nize
  char temp_str[8];
  char hum_str[8];
  char soil_str[8];
  char time_str[12];

  // Varno pretvorimo vrednosti v nize
  dtostrf(sensors.temperature, 4, 2, temp_str);
  dtostrf(sensors.humidity, 4, 2, hum_str);
  dtostrf(sensors.soil_moisture, 4, 2, soil_str);
  snprintf(time_str, sizeof(time_str), "%lu", timestamp);

  // Uporabimo vgrajeni JsonWriter knjižnice Firebase
  object_t json, obj1, obj2, obj3, obj4;
  JsonWriter writer;

  writer.create(obj1, "temperature", string_t(temp_str));
  writer.create(obj2, "humidity", string_t(hum_str));
  writer.create(obj3, "soil_moisture", string_t(soil_str));
  writer.create(obj4, "timestamp", string_t(time_str));
  writer.join(json, 4, obj1, obj2, obj3, obj4);

  // Sestavimo pot brez uporabe String objekta
  char path_buffer[96];
  snprintf(path_buffer, sizeof(path_buffer), "%s/%lu", sensorPath, timestamp);

  Firebase.printf("[F_UPDATE_SENSOR] Sending sensor data (poskus %d/%d)...\n",
                  firebaseRetryCount + 1, MAX_FIREBASE_RETRIES);

  // NOVO: Shrani operacijo za morebitni retry
  lastOperation.type = LastFirebaseOperation::Type::UPDATE_SENSOR;
  lastOperation.data.sensor.timestamp = timestamp;
  lastOperation.data.sensor.data = sensors;
  lastOperation.waiting_for_response = true;
  lastFirebaseOperationTime = millis();
  firebase_response_received = false;

  if (!Firebase_IsReady())
  {
    Firebase.printf("[F_UPDATE_SENSOR] Firebase ni pripravljen za posodobitev podatkov senzorjev.\n");
    Sensor_OnFirebaseResponse(false);
    return;
  }
  // Pošljemo podatke
  Database.set<object_t>(aClient, path_buffer, json, Firebase_processResponse, "updateSensorTask");
  
}

// Funkcija za pošiljanje INA podatkov v Firebase (PREDELANO z ArduinoJson)
//----------------------------------------------------------------------------------------------------------------------
void Firebase_Update_INA_Data(unsigned long timestamp, const INA3221_DataPayload &data)
{
  JsonWriter writer;
  object_t final_json;

  // Pripravimo bufferje za pretvorbo float vrednosti v nize
  char value_str[12];

  // Ustvarimo objekte za vsak kanal posebej
  object_t ch0_obj, ch1_obj, ch2_obj;
  object_t ch0_items[4], ch1_items[4], ch2_items[4];

  // Kanal 0
  dtostrf(data.channels[0].bus_voltage, 4, 2, value_str);
  writer.create(ch0_items[0], "bus_voltage_V", string_t(value_str));
  dtostrf(data.channels[0].shunt_voltage_mV, 4, 2, value_str);
  writer.create(ch0_items[1], "shunt_voltage_mV", string_t(value_str));
  dtostrf(data.channels[0].current_mA, 4, 2, value_str);
  writer.create(ch0_items[2], "current_mA", string_t(value_str));
  dtostrf(data.channels[0].power_mW, 4, 2, value_str);
  writer.create(ch0_items[3], "power_mW", string_t(value_str));
  writer.join(ch0_obj, 4, ch0_items[0], ch0_items[1], ch0_items[2], ch0_items[3]);

  // Kanal 1
  dtostrf(data.channels[1].bus_voltage, 4, 2, value_str);
  writer.create(ch1_items[0], "bus_voltage_V", string_t(value_str));
  dtostrf(data.channels[1].shunt_voltage_mV, 4, 2, value_str);
  writer.create(ch1_items[1], "shunt_voltage_mV", string_t(value_str));
  dtostrf(data.channels[1].current_mA, 4, 2, value_str);
  writer.create(ch1_items[2], "current_mA", string_t(value_str));
  dtostrf(data.channels[1].power_mW, 4, 2, value_str);
  writer.create(ch1_items[3], "power_mW", string_t(value_str));
  writer.join(ch1_obj, 4, ch1_items[0], ch1_items[1], ch1_items[2], ch1_items[3]);

  // Kanal 2
  dtostrf(data.channels[2].bus_voltage, 4, 2, value_str);
  writer.create(ch2_items[0], "bus_voltage_V", string_t(value_str));
  dtostrf(data.channels[2].shunt_voltage_mV, 4, 2, value_str);
  writer.create(ch2_items[1], "shunt_voltage_mV", string_t(value_str));
  dtostrf(data.channels[2].current_mA, 4, 2, value_str);
  writer.create(ch2_items[2], "current_mA", string_t(value_str));
  dtostrf(data.channels[2].power_mW, 4, 2, value_str);
  writer.create(ch2_items[3], "power_mW", string_t(value_str));
  writer.join(ch2_obj, 4, ch2_items[0], ch2_items[1], ch2_items[2], ch2_items[3]);

  // Pripravimo še ostale objekte na najvišjem nivoju
  object_t top_level_items[6];
  writer.create(top_level_items[0], "ch0", ch0_obj);
  writer.create(top_level_items[1], "ch1", ch1_obj);
  writer.create(top_level_items[2], "ch2", ch2_obj);
  
  snprintf(value_str, sizeof(value_str), "%u", data.alert_flags);
  writer.create(top_level_items[3], "alert_flags", string_t(value_str));
  
  dtostrf(data.shunt_voltage_sum_mV, 4, 2, value_str);
  writer.create(top_level_items[4], "shunt_voltage_sum_mV", string_t(value_str));
  
  snprintf(value_str, sizeof(value_str), "%lu", timestamp);
  writer.create(top_level_items[5], "timestamp", string_t(value_str));

  // Združimo vse v končni JSON
  writer.join(final_json, 6, top_level_items[0], top_level_items[1], top_level_items[2], top_level_items[3], top_level_items[4], top_level_items[5]);

  // Sestavimo pot
  char path_buffer[96];
  snprintf(path_buffer, sizeof(path_buffer), "%s/%lu", inaPath, timestamp);

  Firebase.printf("[F_UPDATE_INA] Sending INA data (poskus %d/%d)...\n",
                  firebaseRetryCount + 1, MAX_FIREBASE_RETRIES);

  // NOVO: Shrani operacijo za retry
  lastOperation.type = LastFirebaseOperation::Type::UPDATE_INA;
  lastOperation.data.ina.timestamp = timestamp;
  lastOperation.data.ina.data = data;
  lastOperation.waiting_for_response = true;
  lastFirebaseOperationTime = millis();
  firebase_response_received = false;

  if (!Firebase_IsReady())
  {
    Firebase.printf("[F_UPDATE_INA] Firebase ni pripravljen za posodobitev INA podatkov.\n");
    Sensor_OnFirebaseResponse(false);
    return;
  }

  // Pošljemo podatke
  Database.set<object_t>(aClient, path_buffer, final_json, Firebase_processResponse, "updateINA3221Task");
  

}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za obdelavo odgovora iz Firebase
void Firebase_processResponse(AsyncResult &aResult)
{
  if (!aResult.isResult())
    return;

  // DODAJ: Če je timeout ali connection error, signaliziraj neuspeh
  if (aResult.isError())
  {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n",
                    aResult.uid().c_str(),
                    aResult.error().message().c_str(),
                    aResult.error().code());

    // // Signaliziraj neuspeh za senzorske operacije
    // if (strcmp(aResult.uid().c_str(), "updateSensorTask") == 0 ||
    //     strcmp(aResult.uid().c_str(), "updateINA3221Task") == 0)
    // {
    //   Sensor_OnFirebaseResponse(false);
    // }
      // NE signaliziraj neuspeha takoj - naj retry mehanizem poskrbi

    firebase_response_received = false;
    return;
  }

  if (aResult.isEvent())
  {
    Firebase.printf("Event task: %s, msg: %s, code: %d\n",
                    aResult.uid().c_str(),
                    aResult.appEvent().message().c_str(),
                    aResult.appEvent().code());

    // Preverimo za avtentikacijo
    if (strcmp(aResult.uid().c_str(), "🔐 authTask") == 0)
    {
      if (aResult.appEvent().code() == 7) // začetek avtentikacije
      {
        ssl_avtentikacija = false; // Med avtentikacijo onemogočimo operacije
      }
      else if (aResult.appEvent().code() == 10) // konec avtentikacije
      {
        ssl_avtentikacija = true; // Ko se zaključi avtentikacija, omogočimo operacije
        Firebase.printf("[F_AUTH] Avtentikacija uspešna ob:\n");
        printLocalTime();
      }
    }
  }

  if (aResult.isDebug())
  {
    // pripišemo trenutni čas
    printLocalTime();
    Firebase.printf("Debug task: %s, msg: %s\n",
                    aResult.uid().c_str(),
                    aResult.debug().c_str());
  }

  if (aResult.available())
  {
    // Firebase.printf("[F_RESPONSE] task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());

    const char *path = aResult.path().c_str();
    Firebase.printf("[F_RESPONSE] String path: %s\n", path);

    // Obdelava različnih nalog glede na UID
    // --- Branje iz Firebase ---
    //-----------------------------------------------------------------------------------------
    // Preberi urnik kanala
    if (strcmp(aResult.uid().c_str(), "getUrnikTask") == 0)
    {
      // Pridobimo indeks kanala iz poti
      int kanalIndex = -1;
      const char *kanalStr = strstr(path, "kanal");
      if (kanalStr != NULL)
      {
        // Preskočimo "kanal" in pretvorimo številko v int
        kanalIndex = atoi(kanalStr + 5) - 1; // -1 za 0-based indeks
      }

      Firebase.printf("[F_RESPONSE] Prejet urnik. Pričakovan kanal: %d, Prejet kanal: %d\n", currentChannelInProcess, kanalIndex);

      if (kanalIndex != -1 && kanalIndex == currentChannelInProcess)
      {
        String payload = aResult.c_str();
        int start_sec = -1;
        int end_sec = -1;

        // --- Ročno razčlenjevanje za "start_sec" ---
        // String startKey = "start_sec\":";
        int startIndex = payload.indexOf("start_sec\":");
        if (startIndex != -1)
        {
          int valueStartIndex = startIndex + 11; // dolžina "start_sec\":"
          int valueEndIndex = payload.indexOf(',', valueStartIndex);
          if (valueEndIndex == -1)
          { // Če je zadnji element, iščemo '}'
            valueEndIndex = payload.indexOf('}', valueStartIndex);
          }
          if (valueEndIndex != -1)
          {
            start_sec = payload.substring(valueStartIndex, valueEndIndex).toInt();
          }
        }

        // --- Ročno razčlenjevanje za "end_sec" ---
        startIndex = payload.indexOf("end_sec\":");
        if (startIndex != -1)
        {
          int valueStartIndex = startIndex + 9; // dolžina "end_sec\":"
          int valueEndIndex = payload.indexOf(',', valueStartIndex);
          if (valueEndIndex == -1)
          { // Če je zadnji element, iščemo '}'
            valueEndIndex = payload.indexOf('}', valueStartIndex);
          }
          if (valueEndIndex != -1)
          {
            end_sec = payload.substring(valueStartIndex, valueEndIndex).toInt();
          }
        }

        // Preverimo, ali smo uspešno prebrali obe vrednosti
        if (start_sec != -1 && end_sec != -1)
        {
          firebase_kanal[kanalIndex].start_sec = start_sec;
          firebase_kanal[kanalIndex].end_sec = end_sec;
          formatSecondsToTime(firebase_kanal[kanalIndex].start, sizeof(firebase_kanal[kanalIndex].start), start_sec);
          formatSecondsToTime(firebase_kanal[kanalIndex].end, sizeof(firebase_kanal[kanalIndex].end), end_sec);

          Firebase.printf("[F_RESPONSE] Prejeta oba dela za kanal %d. Signaliziram uspeh ✅\n", kanalIndex);
          firebase_response_received = true; // Signaliziramo uspeh
          firebaseRetryCount = 0;
          lastOperation.waiting_for_response = false;          
        }
        else
        {
          Firebase.printf("[F_RESPONSE] JSON za urnik nima pričakovanih polj (start_sec, end_sec) ali pa je napaka pri razčlenjevanju.\n");
        }
      }
    }

    //-----------------------------------------------------------------------------------------
    // Preberi interval za grafe
    if (strcmp(aResult.uid().c_str(), "getChartIntervalTask") == 0)
    {
      uint8_t sensorReadIntervalMinutes = aResult.payload().toInt();
      set_Interval(sensorReadIntervalMinutes);
      Firebase.printf("[F_RESPONSE] Sensor read interval set to: %d minutes ✅\n", sensorReadIntervalMinutes);
      firebase_response_received = true;
      firebaseRetryCount = 0;
      lastOperation.waiting_for_response = false;
    }

    // --- Odgovor na pisanje v Firebase ---
    //-----------------------------------------------------------------------------------------
    // Posodobljeni podatki senzorjev
    if (strcmp(aResult.uid().c_str(), "updateSensorTask") == 0)
    {
      Firebase.printf("[F_RESPONSE] Sensor data uploaded ✅\n");
      // PrikaziStanjeSenzorjevNaSerial();
      firebase_response_received = true;
      firebaseRetryCount = 0;
      lastOperation.waiting_for_response = false;      
      Sensor_OnFirebaseResponse(true);  // Signaliziraj senzorski čakalni vrsti
      
    }

    //-----------------------------------------------------------------------------------------
    // Posodobljeni INA3221 podatki
    if (strcmp(aResult.uid().c_str(), "updateINA3221Task") == 0)
    {
      Firebase.printf("[F_RESPONSE] INA3221 data uploaded ✅\n");
      firebase_response_received = true;
      firebaseRetryCount = 0;
      lastOperation.waiting_for_response = false;      
      Sensor_OnFirebaseResponse(true);  // Signaliziraj senzorski čakalni vrsti

    }
    //-----------------------------------------------------------------------------------------
    // Posodobljeno stanje releja
    if (strcmp(aResult.uid().c_str(), "updateStateTask") == 0)
    {
      Firebase.printf("[F_RESPONSE] State data uploaded ✅\n");
      firebase_response_received = true;
      firebaseRetryCount = 0;
      lastOperation.waiting_for_response = false;

    }

  }
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za preverjanje timeoutov in retry Firebase
void Firebase_CheckAndRetry()
{
  // Če ni aktivne operacije, nič ne počnemo
  if (!lastOperation.waiting_for_response)
  {
    return;
  }

  // Če smo prejeli odgovor, resetiramo
  if (firebase_response_received)
  {
    lastOperation.waiting_for_response = false;
    firebaseRetryCount = 0;
    Firebase.printf("[FB_RETRY] Odgovor prejet, reset retry števca.\n");
    return;
  }

  // Preveri timeout
  if (millis() - lastFirebaseOperationTime < FIREBASE_RESPONSE_TIMEOUT)
  {
    return; // Še čakamo
  }

  // Timeout!
  firebaseRetryCount++;
  Firebase.printf("[FB_RETRY] TIMEOUT! Poskus %d/%d\n",
                  firebaseRetryCount, MAX_FIREBASE_RETRIES);

  // Če smo dosegli max retry, signaliziraj neuspeh
  if (firebaseRetryCount >= MAX_FIREBASE_RETRIES)
  {
    Firebase.printf("[FB_RETRY] Maksimalno število poskusov doseženo. NEUSPEH!\n");

    // Signaliziraj neuspeh glede na tip operacije
    if (lastOperation.type == LastFirebaseOperation::Type::UPDATE_SENSOR ||
        lastOperation.type == LastFirebaseOperation::Type::UPDATE_INA)
    {
      Sensor_OnFirebaseResponse(false);
    }

    // Reset
    lastOperation.waiting_for_response = false;
    firebaseRetryCount = 0;
    return;
  }

  // Ponovni poskus - pokliči ustrezno funkcijo
  Firebase.printf("[FB_RETRY] Ponovni poskus...\n");

  switch (lastOperation.type)
  {
  case LastFirebaseOperation::Type::UPDATE_SENSOR:
    Firebase_Update_Sensor_Data(lastOperation.data.sensor.timestamp,
                                lastOperation.data.sensor.data);
    break;

  case LastFirebaseOperation::Type::UPDATE_INA:
    Firebase_Update_INA_Data(lastOperation.data.ina.timestamp,
                             lastOperation.data.ina.data);
    break;

  case LastFirebaseOperation::Type::UPDATE_RELAY:
    Firebase_Update_Relay_State(lastOperation.data.relay.kanal,
                                lastOperation.data.relay.state);
    break;

  case LastFirebaseOperation::Type::GET_URNIK:
    Firebase_readKanalUrnik(lastOperation.data.urnik.kanalIndex);
    break;

  case LastFirebaseOperation::Type::GET_INTERVAL:
    Firebase_readInterval();
    break;

  default:
    break;
  }
}

    // Preveri, ali so še aktivne naloge
    // if (aClient.taskCount() == 0) {
    //   Serial.println("Vse naloge so končane.");
    // }
    // else
    // {
    //   Serial.printf("Še %d nalog aktivnih.\n", aClient.taskCount());
    // }

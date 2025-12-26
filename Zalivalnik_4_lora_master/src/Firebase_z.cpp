#include "Firebase_z.h"
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

bool relayState[8]; // Stanja relejev (za 8 relejev)
                    // false = OFF, true = ON
bool status;        // Spremenljivka za shranjevanje statusa operacij

// --- STATIČNA ČAKALNA VRSTA (brez malloc!) ---
FirebaseOperation firebaseOpsQueue[FIREBASE_QUEUE_SIZE];
uint8_t firebase_queue_head = 0;
uint8_t firebase_queue_tail = 0;
uint8_t firebase_queue_count = 0;

static unsigned long lastConnectionAttempt = 0;
static uint8_t connectionRetries = 0;
const uint8_t MAX_CONNECTION_RETRIES = 3;

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za inicializacijo Firebase
void Init_Firebase()
{
  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setTimeout(5000);
  ssl_client.setHandshakeTimeout(10);
 
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
          // DODAJ: Preveri, ali je to naša lastna posodobitev
          // if (ignoreNextStreamUpdate[kanalIndex - 1]) {
          //   Serial.printf("[STREAM] Ignoriram lastno posodobitev za kanal %d\n", kanalIndex);
          //   ignoreNextStreamUpdate[kanalIndex - 1] = false;
          //   return; // Ne procesiramo naprej!
          // }

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
  Firebase.printf("[FIREBASE] Branje intervala iz Firebase...\n");
  Database.get(aClient, chartIntervalPath, Firebase_processResponse, false, "getChartIntervalTask");
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija ki prebere urnik iz Firebase in ga shrani v globalno spremenljivko firebase_kanal
void Firebase_readKanalUrnik(uint8_t kanalIndex)
{
  char path_buffer[96];
  snprintf(path_buffer, sizeof(path_buffer), "%s%d", kanaliPath, kanalIndex + 1);
  Database.get(aClient, path_buffer, Firebase_processResponse, false, "getUrnikTask");
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov v Firebase (eno polje kanala)
void Firebase_Update_Relay_State(int kanal, bool state)
{
    // DODAJ PREVERJANJE PROSTEGA HEAP-a PRED OPERACIJO
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 30000) { // Potrebuješ vsaj ~30KB za SSL
    Firebase.printf("[F_UPDATE_RELAY] OPOZORILO: Premalo prostega heap-a: %d bajtov. Preskakujem posodobitev.\n", freeHeap);
    firebase_response_received = false; // Obravnavaj kot neuspešno
    return;
  }

  if (!app.ready())
  {
    Firebase.printf("[F_UPDATE_RELAY] Firebase ni pripravljen za pošiljanje stanja releja.\n");
    return;
  }

  // Sestavimo pot direktno do polja 'state'
  char path_buffer[100];
  snprintf(path_buffer, sizeof(path_buffer), "%s%d/state", kanaliPath, kanal);

  // Pripravimo vrednost, ki jo želimo nastaviti (ne več kot JSON objekt)
  const char* state_payload = state ? "ON" : "OFF";

  // DODAJ PRED POŠILJANJEM: Označi, da ignoriramo naslednji stream update za ta kanal
  // ignoreNextStreamUpdate[kanal - 1] = true;

  Firebase.printf("[F_UPDATE_RELAY] Sending set to: %s, payload: %s\n", path_buffer, state_payload);

  // Uporabimo metodo 'set' za nastavitev vrednosti specifičnega polja.
  // To je bolj robustno kot 'update' za posamezna polja.
  Database.set(aClient, path_buffer, state_payload, Firebase_processResponse, "updateStateTask");
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov senzorjev v Firebase (PREDELANO z ArduinoJson)
void Firebase_Update_Sensor_Data(unsigned long timestamp, const SensorDataPayload &sensors)
{
    // DODAJ PREVERJANJE PROSTEGA HEAP-a PRED OPERACIJO
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 30000) { // Potrebuješ vsaj ~30KB za SSL
    Firebase.printf("[F_UPDATE_SENSOR] OPOZORILO: Premalo prostega heap-a: %d bajtov. Preskakujem posodobitev.\n", freeHeap);
    firebase_response_received = false; // Obravnavaj kot neuspešno
    return;
  }
  if (!app.ready())
  {
    Firebase.printf("[F_UPDATE_SENSOR] Firebase ni pripravljen za pošiljanje senzornih podatkov.\n");
    return;
  }

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

  Firebase.printf("[F_UPDATE_SENSOR] Sending sensor data to: %s\n", path_buffer);

  // Pošljemo podatke z asinhronim klicem
  Database.set<object_t>(aClient, path_buffer, json, Firebase_processResponse, "updateSensorTask");
}

// NOVO: Funkcija za pošiljanje INA podatkov v Firebase (PREDELANO z ArduinoJson)
//----------------------------------------------------------------------------------------------------------------------
void Firebase_Update_INA_Data(unsigned long timestamp, const INA3221_DataPayload &data)
{
    // DODAJ PREVERJANJE PROSTEGA HEAP-a PRED OPERACIJO
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 30000) { // Potrebuješ vsaj ~30KB za SSL
    Firebase.printf("[F_UPDATE_INA] OPOZORILO: Premalo prostega heap-a: %d bajtov. Preskakujem posodobitev.\n", freeHeap);
    firebase_response_received = false; // Obravnavaj kot neuspešno
    return;
  }

  if (!app.ready())
  {
    Firebase.printf("[F_UPDATE_INA] Firebase ni pripravljen za pošiljanje INA podatkov.\n");
    return;
  }

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

  Firebase.printf("[F_UPDATE_INA] Sending INA data to: %s\n", path_buffer);

  // Pošljemo podatke
  Database.set<object_t>(aClient, path_buffer, final_json, Firebase_processResponse, "updateINA3221Task");

}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za obdelavo odgovora iz Firebase
void Firebase_processResponse(AsyncResult &aResult)
{
  if (!aResult.isResult()) return;

  // DODAJ: Če je timeout ali connection error, signaliziraj neuspeh
  if (aResult.isError()) {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", 
                   aResult.uid().c_str(), 
                   aResult.error().message().c_str(), 
                   aResult.error().code());
    
    // Signaliziraj neuspeh za senzorske operacije
    if (strcmp(aResult.uid().c_str(), "updateSensorTask") == 0 ||
        strcmp(aResult.uid().c_str(), "updateINA3221Task") == 0) {
      Sensor_OnFirebaseResponse(false);
    }
    
    firebase_response_received = false;
    return;
  }

  if (aResult.isEvent()) Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  if (aResult.isDebug()) Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

  if (aResult.available())
  {
    Firebase.printf("[F_RESPONSE] task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
    // String path = aResult.path().c_str();
    // Firebase.printf("[DIAGNOZA] Firebase path: %s\n", path.c_str());

    const char* path = aResult.path().c_str();
    Firebase.printf("[F_RESPONSE] String path: %s\n", path);

    // Obdelava različnih nalog glede na UID

    //-----------------------------------------------------------------------------------------
    // Preberi urnik kanala
    if (strcmp(aResult.uid().c_str(), "getUrnikTask") == 0)
    {
      // Pridobimo indeks kanala iz poti
      int kanalIndex = -1;
      const char* kanalStr = strstr(path, "kanal");
      if (kanalStr != NULL) {
          // Preskočimo "kanal" in pretvorimo številko v int
          kanalIndex = atoi(kanalStr + 5) - 1; // -1 za 0-based indeks
      }

      // int kanaliPos = path.indexOf("/Kanali/kanal");
      // if (kanaliPos != -1)
      // {
      //   kanalIndex = path.substring(kanaliPos + 13).toInt() - 1; // +13 je dolžina "/Kanali/kanal", -1 za 0-based indeks
      // }

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

          Firebase.printf("[F_RESPONSE] Prejeta oba dela za kanal %d. Signaliziram uspeh.\n", kanalIndex);
          firebase_response_received = true; // Signaliziramo uspeh
        }
        else
        {
          Firebase.printf("[F_RESPONSE] JSON za urnik nima pričakovanih polj (start_sec, end_sec) ali pa je napaka pri razčlenjevanju.\n");
        }
      }
    }

    //-----------------------------------------------------------------------------------------
    // Posodobi podatke senzorjev
    if (strcmp(aResult.uid().c_str(), "updateSensorTask") == 0)
    {
      Firebase.printf("[F_RESPONSE] Sensor data uploaded\n");
      PrikaziStanjeSenzorjevNaSerial();
      firebase_response_received = true;
      // DODAJ: Signaliziraj senzorski čakalni vrsti
      Sensor_OnFirebaseResponse(true);
    }

    //-----------------------------------------------------------------------------------------
    // Posodobi stanje releja
    if (strcmp(aResult.uid().c_str(), "updateStateTask") == 0)
    {
      Firebase.printf("[F_RESPONSE] State data uploaded\n");
      firebase_response_received = true;
    }

    //-----------------------------------------------------------------------------------------
    // Preberi interval za grafe
    if (strcmp(aResult.uid().c_str(), "getChartIntervalTask") == 0)
    {
      uint8_t sensorReadIntervalMinutes = aResult.payload().toInt();
      set_Interval(sensorReadIntervalMinutes);
      Firebase.printf("[F_RESPONSE] Sensor read interval set to: %d minutes\n", sensorReadIntervalMinutes);
      firebase_response_received = true;
    }

    //-----------------------------------------------------------------------------------------
    // Posodobi INA3221 podatke
    if (strcmp(aResult.uid().c_str(), "updateINA3221Task") == 0)
    {
      Firebase.printf("[F_RESPONSE] INA3221 data uploaded\n");
      firebase_response_received = true;
      // DODAJ: Signaliziraj senzorski čakalni vrsti
      Sensor_OnFirebaseResponse(true);

    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Dodaj operacijo v čakalno vrsto
bool Firebase_QueueOperation(const FirebaseOperation& op) {
    if (firebase_queue_count >= FIREBASE_QUEUE_SIZE) {
        Firebase.printf("[FB_QUEUE] POLNA! Preskakujem operacijo.\n");
        return false;
    }
    
    firebaseOpsQueue[firebase_queue_tail] = op;
    firebase_queue_tail = (firebase_queue_tail + 1) % FIREBASE_QUEUE_SIZE;
    firebase_queue_count++;

    Firebase.printf("[FB_QUEUE] Dodana operacija. V vrsti: %d/%d\n", 
                  firebase_queue_count, FIREBASE_QUEUE_SIZE);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
// Procesira naslednjo operacijo iz čakalne vrste
bool Firebase_ProcessNextOperation() {
    if (firebase_queue_count == 0) {
        return false;  // Ni operacij
    }
    
    // Preveri heap pred operacijo
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 30000) {
        Firebase.printf("[FB_QUEUE] Premalo heap-a: %d B. Čakam...\n", freeHeap);
        return false;
    }
    
    // Preveri, ali Firebase ni zaseden
    if (!app.ready()) {
        // Če Firebase ni ready, počakaj malo in poskusi ponovno
        if (millis() - lastConnectionAttempt > 2000) {
            lastConnectionAttempt = millis();
            connectionRetries++;
            
            if (connectionRetries > MAX_CONNECTION_RETRIES) {
                Firebase.printf("[FB_QUEUE] NAPAKA: Firebase se ne more povezati po %d poskusih!\n", MAX_CONNECTION_RETRIES);
                
                // Odstrani operacijo, da se ne zatakne
                firebase_queue_head = (firebase_queue_head + 1) % FIREBASE_QUEUE_SIZE;
                firebase_queue_count--;
                connectionRetries = 0;
                
                // DODAJ: Signaliziraj neuspeh senzorski vrsti
                FirebaseOperation& op = firebaseOpsQueue[firebase_queue_head];
                if (op.type == FirebaseTaskType::UPDATE_SENSORS || 
                    op.type == FirebaseTaskType::UPDATE_INA) {
                    Sensor_OnFirebaseResponse(false);
                }
                
                return false;
            }
            Firebase.printf("[FB_QUEUE] Firebase ni ready. Poskus %d/%d...\n", 
                          connectionRetries, MAX_CONNECTION_RETRIES);
        }      
        return false;
    }

        // Reset števca ob uspešni povezavi
    connectionRetries = 0;

    FirebaseOperation& op = firebaseOpsQueue[firebase_queue_head];

    Firebase.printf("[FB_QUEUE] Procesiranje operacije tipa %d, timestamp: %lu\n", 
                  (int)op.type, op.timestamp);
    
    // Izvedi operacijo
    switch (op.type) {
        case FirebaseTaskType::UPDATE_SENSORS:
            Firebase_Update_Sensor_Data(op.timestamp, op.data.sensors);
            break;
            
        case FirebaseTaskType::UPDATE_INA:
            Firebase_Update_INA_Data(op.timestamp, op.data.ina);
            break;
            
        case FirebaseTaskType::UPDATE_RELAY_STATE:
            Firebase_Update_Relay_State(op.data.relay.kanal, op.data.relay.state);
            break;
    }
    
    // Odstrani iz čakalne vrste
    firebase_queue_head = (firebase_queue_head + 1) % FIREBASE_QUEUE_SIZE;
    firebase_queue_count--;
    
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
void Firebase_ResetConnection() {
    Firebase.printf("[FB_RESET] Resetiranje SSL povezave...\n");
    
    ssl_client.stop();
    delay(100);
    
    ssl_client.setInsecure();
    ssl_client.setTimeout(5000);
    ssl_client.setHandshakeTimeout(10);
    
    Firebase.printf("[FB_RESET] SSL povezava resetirana.\n");
}
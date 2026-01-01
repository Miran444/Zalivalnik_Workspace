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

// FreeRTOS objekti
// SemaphoreHandle_t firebaseSemaphore = NULL;
// TaskHandle_t firebaseTaskHandle = NULL;

// Prototipi funkcij
// void Firebase_Task(void *pvParameters);

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

// DODAJ globalne spremenljivke za stream monitoring
static unsigned long lastFirebaseActivityTime = 0;
const unsigned long FIREBASE_RECONNECT_INTERVAL = 180000; //  minute
static bool FirebaseNeedsReconnect = false;

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
  // Authentication
  UserAuth user_auth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD);

  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setTimeout(10000);
  ssl_client.setHandshakeTimeout(20);

  // Configure stream SSL client
  stream_ssl_client.setInsecure();
  stream_ssl_client.setTimeout(15000);
  stream_ssl_client.setHandshakeTimeout(20);

  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), Firebase_processResponse, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(FIREBASE_DATABASE_URL);

  // Ustvari Firebase task z velikim skladom
  // xTaskCreate(Firebase_Task, "Firebase", 24576, NULL, 1, &firebaseTaskHandle); // 24KB!


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

  streamClient.setSSEFilters("put,patch,cancel,auth_revoked,keep-alive");
  Database.get(streamClient, databasePath, streamCallback, true /* SSE mode (HTTP Streaming) */, "mainStreamTask");

}

//------------------------------------------------------------------------------------------------------------------------
// Firebase task - prenesene ponavljajoče se naloge iz main loop-a
// void Firebase_Task(void *pvParameters)
// {
//   for (;;)
//   {
//     // 1. Preverjanje heap-a PRED app.loop()
//     size_t freeHeap = ESP.getFreeHeap();
//     if (freeHeap < 40000) // Manj kot 40KB = KRITIČNO!
//     {
//       Serial.printf("[FB_TASK] ⚠️ HEAP KRITIČEN: %d bajtov!\n", freeHeap);
      
//       // FirebaseNeedsReconnect = true;
//       // POČISTI stare slote
//       // Database.resetApp();
//       // delay(1000); // Počakaj na cleanup
//     }

//     // 2. Procesiranje Firebase dogodkov (TO JE PRIORITETA!)
//     app.loop();

//     //--------------------------------------------------------------------------------------------------
//     // 3. Dodaj diagnostiko za task
//     static unsigned long lastStackCheck = 0;
//     if (millis() - lastStackCheck > 30000) { // Vsakih 30 sekund
//       lastStackCheck = millis();

//       UBaseType_t firebaseStackLeft = uxTaskGetStackHighWaterMark(firebaseTaskHandle);
//       Serial.printf("[FIREBASE TASK] Stack left: %d\n", firebaseStackLeft);

//       if (firebaseStackLeft < 512) { // OPOZORILO!
//           Serial.println("[FIREBASE TASK] ⚠️ STACK CRITICAL!");
//       }
//     }

//     //--------------------------------------------------------------------------------------------------
//     // 4. UPORABI Firebase_CheckAndRetry() - ne dupliraj logike!
//     Firebase_CheckAndRetry(); // ← TA FUNKCIJA ŽE VSE NAREDI!

//     //--------------------------------------------------------------------------------------------------
//     // 5. Stream monitoring
//     Firebase_CheckStreamHealth();

//     //--------------------------------------------------------------------------------------------------
//     // 6. Stream update logika
//     if (app.ready())
//     {
//       // Firebase_CheckAndRetry();
//       // Sensor_ProcessQueue();
//       static bool firebaseUpdatePending = false;
      
//       if (newChannelDataAvailable)  // Preveri, ali so na voljo novi podatki iz Firebase streama
//       {
//         // Tukaj pokličite funkcijo za pripravo podatkov za posodobitev
//         if (Firebase_handleStreamUpdate(channelUpdate.kanalIndex, channelUpdate.start_sec, channelUpdate.end_sec))
//         {
//           if (lora_is_busy())
//           {
//             Firebase.printf("[STREAM] LoRa zasedena. Shranjujem posodobitev za kasneje.\n");
//             firebaseUpdatePending = true; // čakamo na prosto LoRa
//           }        
//           else
//           {
//             Firebase.printf("[STREAM] LoRa prosta. Pošiljam posodobitev takoj.\n");
//             firebaseUpdatePending_OK = true; // pošljemo posodobitev takoj v main loop-u
//           }                  
//         }
//         else
//         {
//           Firebase.printf("[STREAM] Ni sprememb urnika, samo stanje. Ne pošiljam posodobitve.\n");
//         }
//         newChannelDataAvailable = false;  // Počisti zastavico, da ne obdelamo istih podatkov večkrat  
//       }

//       // Lora je prosta, pošlji posodobitev prek LoRa v main loop-u 
//       if (firebaseUpdatePending && !lora_is_busy())
//       {
//         Firebase.printf("[STREAM] LoRa prosta. Pošiljam posodobitev.\n");
//         firebaseUpdatePending = false;
//         firebaseUpdatePending_OK = true;
//       }
//     }

//     vTaskDelay(100 / portTICK_PERIOD_MS); // 10x na sekundo

//   }
// }

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

  if (aResult.isEvent()) Firebase.printf("[F_STREAM] Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  if (aResult.isDebug()) Firebase.printf("[F_STREAM] Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

  if (aResult.isError())
  {
    Firebase.printf("[STREAM]  ⚠️  Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

    // NOVO: TCP connection failed -> označimo za reconnect
    if (aResult.error().code() == -1 || aResult.error().code() == 401)
    {
      Firebase.printf("[STREAM] ⚠️ TCP povezava prekinjena! Načrtujem reconnect...\n");
      FirebaseNeedsReconnect = true;
    }
    return;
  }
  if (aResult.available())
  {
    RealtimeDatabaseResult &stream = aResult.to<RealtimeDatabaseResult>();
    if (stream.isStream())
    {
      //Serial.println("----------------------------");
      Firebase.printf("[STREAM] event: %s\n", stream.event().c_str());
   
      if (stream.event() == "keep-alive")   // če je event "keep-alive"
      {
        lastFirebaseActivityTime = millis();
      }

      // Najprej shrani v String objekt
      String path_str = stream.dataPath();
      const char* path = path_str.c_str();  // Zdaj je kazalec veljaven
      // Firebase.printf("[STREAM] String path: %s\n", path);

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
bool Firebase_handleStreamUpdate(int kanalIndex, int start_sec, int end_sec)
{
  int index = kanalIndex - 1;
  bool updated = false;

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

    // Pripravi podatke za posodobitev
    pendingUpdateData.kanalIndex = index;
    pendingUpdateData.start_sec = firebase_kanal[index].start_sec;
    pendingUpdateData.end_sec = firebase_kanal[index].end_sec;
    updated = true;

  }
  else
  {
    Firebase.printf("[STREAM] Ni sprememb urnika, samo stanje. Ne pošiljam posodobitve.\n");
  }

  return updated;
}

//------------------------------------------------------------------------------------------------------------------------
// NOVA funkcija: Preverjanje in reconnect streama
void Firebase_CheckStreamHealth()
{
  // Če smo v procesu Wi-Fi reconnecta, počakamo
  if (!WiFi.isConnected() || !ssl_avtentikacija)
  {
    return;
  }
  static bool connecting = false;
  static bool reconnectAttempted = false;

  // Preveri, ali je potreben reconnect
  unsigned long timeSinceLastActivity = millis() - lastFirebaseActivityTime;

  if (FirebaseNeedsReconnect || timeSinceLastActivity > get_Interval() + 1000)  // Interval + 1 sekunda
  {
    Firebase.printf("[F_FIREBASE] ⚠️ Firebase reconnect potreben (čas od zadnje aktivnosti: %lu ms)\n", 
                    timeSinceLastActivity);

    lastFirebaseActivityTime = millis();
    FirebaseNeedsReconnect = false;
    Database.resetApp();  // NOVO: Ponastavi aplikacijo

    reconnectAttempted = true;
  }

  if (!connecting && reconnectAttempted)
  {    
    Firebase.printf("[STREAM] Firebase init...\n");
    connecting = true;
    reconnectAttempted = false;
    Init_Firebase();
    return; // Če je že v teku, ne začni znova
  }

  // Ponovno vzpostavi aplikacijo
  if (app.ready() && connecting)
  {
    Firebase.printf("[STREAM] Firebase connect...\n");
    Firebase_Connect();    
    connecting = false;
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
  // SAMO EN KLIC available() NA ZAČETKU!
  bool hasResult = aResult.isResult();
  bool hasError = aResult.isError();
  bool isEvent = aResult.isEvent();
  bool isDebug = aResult.isDebug();
  bool hasData = aResult.available();

  static uint32_t call_count = 0;
  Serial.printf("[F_RESPONSE] KLIC #%u, isResult=%d, isError=%d, isEvent=%d, isDebug=%d, available=%d\n",
                ++call_count, hasResult, hasError, isEvent, isDebug, hasData);

  if (!hasResult) {
    Serial.println("[F_RESPONSE] Ni rezultata, izhajam.");
    return;
  }

  // ----------------------------------------------------------------------------------------
  // 1. NAJPREJ napake
  if (hasError)
  {
    Firebase.printf("[F_RESPONSE] Error task: %s, msg: %s, code: %d\n",
                    aResult.uid().c_str(),
                    aResult.error().message().c_str(),
                    aResult.error().code());

    // NOVO: TCP connection failed -> označimo za reconnect
    if (aResult.error().code() == -1 || aResult.error().code() == 401)
    {
      Firebase.printf("[F_RESPONSE] ⚠️ TCP povezava prekinjena! Načrtujem reconnect...\n");
      FirebaseNeedsReconnect = true;
    }

    firebase_response_received = false;
    return; // error sporočila nimajo payload-a
  }

  // ----------------------------------------------------------------------------------------
  // 2. DOGODKI
  if (isEvent)
  {
    Firebase.printf("[F_RESPONSE]Event task: %s, msg: %s, code: %d\n",
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
        // Firebase.printf("[F_AUTH] Avtentikacija uspešna ob:\n");
        // printLocalTime();
      }
    }
    //return;
  }

  // ----------------------------------------------------------------------------------------
  // 3. DEBUG
  if (isDebug)
  {
    // pripišemo trenutni čas
    // printLocalTime();
    Firebase.printf("[F_RESPONSE] Debug task: %s, msg: %s\n",
                    aResult.uid().c_str(),
                    aResult.debug().c_str());
    //return; // Debug sporočila imajo tudi payload-a
  }

  // ----------------------------------------------------------------------------------------
  // 4. CHECK
  if (!hasData)
  {
    Serial.println("[F_RESPONSE] ⚠️ No available result!");
    return;
  }

  // 5. KOPIRAJ path TAKOJ (ne shrani kazalca!)
  String pathStr = aResult.path();     // ← KOPIRAJ v String!
  String payloadStr = aResult.c_str(); // ← KOPIRAJ payload!
  String uidStr = aResult.uid();       // ← KOPIRAJ UID!

  // 6. ZDAJ je varno - delamo s kopijami!
  const char *path = pathStr.c_str();
  const char *payload = payloadStr.c_str();
  const char *uid = uidStr.c_str();

  if (!path || strlen(path) == 0) {
    Serial.println("[F_RESPONSE] ⚠️ Path is NULL!");
    return;
  }

  Firebase.printf("[F_RESPONSE] Path: %s\n", path);
  lastFirebaseActivityTime = millis();

  // Obdelava različnih nalog glede na UID
  // --- Branje iz Firebase ---
  //-----------------------------------------------------------------------------------------
  // Preberi urnik kanala
  if (strcmp(uid, "getUrnikTask") == 0)
  {
    // Pridobimo indeks kanala iz poti path
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
      // String payload = aResult.c_str();
      int start_sec = -1;
      int end_sec = -1;

      // --- Ročno razčlenjevanje za "start_sec" ---
      // String startKey = "start_sec\":";
      int startIndex = payloadStr.indexOf("start_sec\":");
      if (startIndex != -1)
      {
        int valueStartIndex = startIndex + 11; // dolžina "start_sec\":"
        int valueEndIndex = payloadStr.indexOf(',', valueStartIndex);
        if (valueEndIndex == -1)
        { // Če je zadnji element, iščemo '}'
          valueEndIndex = payloadStr.indexOf('}', valueStartIndex);
        }
        if (valueEndIndex != -1)
        {
          start_sec = payloadStr.substring(valueStartIndex, valueEndIndex).toInt();
        }
      }

      // --- Ročno razčlenjevanje za "end_sec" ---
      startIndex = payloadStr.indexOf("end_sec\":");
      if (startIndex != -1)
      {
        int valueStartIndex = startIndex + 9; // dolžina "end_sec\":"
        int valueEndIndex = payloadStr.indexOf(',', valueStartIndex);
        if (valueEndIndex == -1)
        { // Če je zadnji element, iščemo '}'
          valueEndIndex = payloadStr.indexOf('}', valueStartIndex);
        }
        if (valueEndIndex != -1)
        {
          end_sec = payloadStr.substring(valueStartIndex, valueEndIndex).toInt();
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
  if (strcmp(uid, "getChartIntervalTask") == 0)
  {
    uint8_t interval = payloadStr.toInt();
    if (interval > 0) { // Preveri veljavnost
      set_Interval(interval);
      Firebase.printf("[F_RESPONSE] Interval nastavljen: %d minut ✅\n", interval);
      firebase_response_received = true;
      firebaseRetryCount = 0;
      lastOperation.waiting_for_response = false;
    } else {
      Firebase.printf("[F_RESPONSE] ⚠️ Neveljaven interval: %s\n", payload);
    }
  }

  // --- Odgovor na pisanje v Firebase ---
  //-----------------------------------------------------------------------------------------
  // Posodobljeni podatki senzorjev
  if (strcmp(uid, "updateSensorTask") == 0)
  {
    Firebase.printf("[F_RESPONSE] Sensor data uploaded ✅\n");
    // PrikaziStanjeSenzorjevNaSerial();
    firebase_response_received = true;
    firebaseRetryCount = 0;
    lastOperation.waiting_for_response = false;
    Sensor_OnFirebaseResponse(true); // Signaliziraj senzorski čakalni vrsti

    // // SAMO TUKAJ preveri heap
    // static size_t lastHeap = ESP.getFreeHeap();
    // size_t currentHeap = ESP.getFreeHeap();
    // int diff = lastHeap - currentHeap;
    // if (abs(diff) > 1000) {
    //   Serial.printf("[F_RESPONSE] HEAP sprememba: %d\n", diff);
    // }
    // lastHeap = currentHeap;

  }

  //-----------------------------------------------------------------------------------------
  // Posodobljeni INA3221 podatki
  if (strcmp(uid, "updateINA3221Task") == 0)
  {
    Firebase.printf("[F_RESPONSE] INA3221 data uploaded ✅\n");
    firebase_response_received = true;
    firebaseRetryCount = 0;
    lastOperation.waiting_for_response = false;
    Sensor_OnFirebaseResponse(true); // Signaliziraj senzorski čakalni vrsti

    // SAMO TUKAJ preveri heap
    static size_t lastHeap = ESP.getFreeHeap();
    size_t currentHeap = ESP.getFreeHeap();
    int diff = lastHeap - currentHeap;
    if (abs(diff) > 1000) {
      Serial.printf("[F_RESPONSE] HEAP sprememba: %d\n", diff);
    }
    lastHeap = currentHeap; 

  }
  //-----------------------------------------------------------------------------------------
  // Posodobljeno stanje releja
  if (strcmp(uid, "updateStateTask") == 0)
  {
    Firebase.printf("[F_RESPONSE] State data uploaded ✅\n");
    firebase_response_received = true;
    firebaseRetryCount = 0;
    lastOperation.waiting_for_response = false;
  }

}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za preverjanje timeoutov in retry Firebase
void Firebase_CheckAndRetry()
{
  // 1. Če ni aktivne operacije, nič ne počnemo
  if (!lastOperation.waiting_for_response)
  {
    return;
  }

  // 2. Če smo prejeli odgovor, resetiramo
  if (firebase_response_received)
  {
    lastOperation.waiting_for_response = false;
    firebaseRetryCount = 0;
    Firebase.printf("[FB_RETRY] ✅ Odgovor prejet, reset retry števca.\n");
    return;
  }

  // 3. Preveri timeout
  if (millis() - lastFirebaseOperationTime < FIREBASE_RESPONSE_TIMEOUT)
  {
    return; // Še čakamo
  }

  // 4. Timeout!
  firebaseRetryCount++;

  Firebase.printf("[FB_RETRY] ⚠️ TIMEOUT! Poskus %d/%d\n",
                  firebaseRetryCount, MAX_FIREBASE_RETRIES);

  // 5. Preveri, ali je Firebase še vedno pripravljen
  if (!app.ready() || !ssl_avtentikacija)
  {
    Firebase.printf("[FB_RETRY] Firebase ni pripravljen. Preskakujem retry.\n");
    
    // Signaliziraj neuspeh
    if (lastOperation.type == LastFirebaseOperation::Type::UPDATE_SENSOR ||
        lastOperation.type == LastFirebaseOperation::Type::UPDATE_INA)
    {
      Sensor_OnFirebaseResponse(false);
    }
    
    lastOperation.waiting_for_response = false;
    firebaseRetryCount = 0;
    return;
  }                

  // 6.Če smo dosegli max retry, signaliziraj neuspeh
  if (firebaseRetryCount >= MAX_FIREBASE_RETRIES)
  {
    Firebase.printf("[FB_RETRY] ❌ Maksimalno število poskusov doseženo. NEUSPEH!\n");

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

  // 7. Ponovni poskus - pokliči ustrezno funkcijo
  Firebase.printf("[FB_RETRY] 🔄 Ponovni poskus...\n");

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
    Firebase.printf("[FB_RETRY] ⚠️ Neznani tip operacije!\n");
    lastOperation.waiting_for_response = false;
    firebaseRetryCount = 0;
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

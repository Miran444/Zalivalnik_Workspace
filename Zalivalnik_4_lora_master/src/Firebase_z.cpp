#include "Firebase_z.h"

// Firebase Zalivalnik2 credentials
// #define FIREBASE_API_KEY "AIzaSyAR1ZPKJkh_Rt15nN8jrUr0kgrzlbdihwU"
#define FIREBASE_API_KEY "AIzaSyDzTD6IjH7gSuAiHyRQoZtc-A-VTIT-4Iw"
// #define FIREBASE_DATABASE_URL "https://zalivalnik-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_DATABASE_URL "https://zalivalnik2-default-rtdb.europe-west1.firebasedatabase.app/"
#define FIREBASE_USER_EMAIL "miranbudal@gmail.com"
#define FIREBASE_USER_PASSWORD "vonja444"
#define FIREBASE_USER_UID "68TA11sAdOaFnIdmqzDtkx3QIBC2"
//--------------------------------------------------------------------------------------------------------

// Authentication
UserAuth user_auth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD);

// Database main path (to be updated in setup with the user UID)
String databasePath;
String SensorPath = "/Sensors";
String KanaliPath = "/Kanali";
String ChartIntervalPath = "/charts/Interval";

// Database child nodes
String tempPath = "/temperature";
String humPath = "/humidity";
String soilMoisturePath = "/soil_moisture";
String timePath = "/timestamp";
String statePath = "/state";
String startPath = "/start";
String startSecPath = "/start_sec";
String endPath = "/end";
String endSecPath = "/end_sec";

String get_stringValue;

// Parent Node (to be updated in every loop)
String parentPath;

// Variable to save USER UID
String uid;
// String uid = FIREBASE_USER_UID;

bool relayState[8]; // Stanja relejev (za 8 relejev)
                    // false = OFF, true = ON
bool status;        // Spremenljivka za shranjevanje statusa operacij

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za inicializacijo Firebase
void Init_Firebase()
{
  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setTimeout(1000);
  ssl_client.setHandshakeTimeout(5);

  stream_ssl_client.setInsecure();
  stream_ssl_client.setTimeout(1000);
  stream_ssl_client.setHandshakeTimeout(5);

  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), Firebase_processResponse, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(FIREBASE_DATABASE_URL);
}

//------------------------------------------------------------------------------------------------------------------------
// Function for connecting to Firebase for getting UID and setting database paths
void Firebase_Connect()
{
  uid = app.getUid().c_str();
  // uid = FIREBASE_USER_UID;
  databasePath = "/UserData/" + uid;
  SensorPath = databasePath + "/Sensors";
  KanaliPath = databasePath + "/Kanali/kanal";
  ChartIntervalPath = databasePath + "/charts/Interval";

  Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());

  // To clear all prevousely set filter to allow all Stream events, use AsyncClientClass::setSSEFilters().
  // streamClient.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
  streamClient.setSSEFilters("put,patch,cancel");
  // Start listening to changes on "Kanali" path
  // The "unauthenticate" error can be occurred in this case because we don't wait
  // the app to be authenticated before connecting the stream.
  // This is ok as stream task will be reconnected automatically when the app is authenticated.
  // The streamClient must be used for Stream only.
  Database.get(streamClient, databasePath , streamCallback, true /* SSE mode (HTTP Streaming) */, "mainStreamTask");
  // Database.get(streamClient, ChartIntervalPath, streamCallback, true /* SSE mode (HTTP Streaming) */, "streamChartIntervalTask");
  // Serial.println("UID: " + uid);
  // Serial.println("Database Path: " + databasePath);
  // Serial.println("Sensor Path: " + SensorPath);
  // Serial.println("Kanali Path: " + KanaliPath);
  // Serial.println("Chart Interval Path: " + ChartIntervalPath);

  // Zaženi poslušanje sprememb na kanalih
  // Firebase_Start_Kanali_Stream();
}

//------------------------------------------------------------------------------------------------------------------------
// Callback funkcija za obdelavo sprememb iz Firebase stream-a
void streamCallback(AsyncResult &aResult)
{
  // Exits when no result is available when calling from the loop.
  if (!aResult.isResult())
    return;

  if (aResult.isEvent())
  {
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  }

  if (aResult.isDebug())
  {
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  }

  if (aResult.isError())
  {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  }

  if (aResult.available())
  {
    RealtimeDatabaseResult &stream = aResult.to<RealtimeDatabaseResult>();
    if (stream.isStream())
    {
      Serial.println("----------------------------");
      Firebase.printf("event: %s\n", stream.event().c_str());


      // --- SPREMEMBA 3: Preverite pot (path), da ugotovite, kaj se je spremenilo ---
      String path = stream.dataPath().c_str();
      Firebase.printf("String path: %s\n", path.c_str());

      // Ali je sprememba znotraj /Kanali?
      if (path.startsWith("/Kanali"))
      {

        String v5 = stream.to<String>();
        Firebase.printf("String data: %s\n", v5.c_str());

        // You can add your code to process the stream data here
        // v5 = "{"kanal4/start":"14:55","kanal4/start_sec":53700}"
        // Iz stringa v5 lahko razberemo, kateri kanal je bil spremenjen in kakšno je novo stanje
        // Najdemo kateri kanal je bil spremenjen
        int kanalIndex = -1;
        int startSecIndex = v5.indexOf("start_sec");
        int endSecIndex = v5.indexOf("end_sec");

        if (v5.indexOf("kanal1/") != -1)
          kanalIndex = 1;
        else if (v5.indexOf("kanal2/") != -1)
          kanalIndex = 2;
        else if (v5.indexOf("kanal3/") != -1)
          kanalIndex = 3;
        else if (v5.indexOf("kanal4/") != -1)
          kanalIndex = 4;
        else if (v5.indexOf("kanal5/") != -1)
          kanalIndex = 5;
        else if (v5.indexOf("kanal6/") != -1)
          kanalIndex = 6;
        else if (v5.indexOf("kanal7/") != -1)
          kanalIndex = 7;
        else if (v5.indexOf("kanal8/") != -1)
          kanalIndex = 8;

        if (kanalIndex != -1)
        {
          int startSec = -1;
          int endSec = -1;

          // Kanal je bil najden, sedaj lahko obdelamo novo stanje
          if (startSecIndex != -1)
          {
            // Najdemo začetek vrednosti (za ključem "start_sec":)
            int valueStartIndex = startSecIndex + strlen("start_sec\":");
            // Najdemo konec vrednosti (prva naslednja vejica ali zaklepaj)
            int valueEndIndex = v5.indexOf(',', valueStartIndex);
            if (valueEndIndex == -1)
            {
              valueEndIndex = v5.indexOf('}', valueStartIndex);
            }
            // Izvlečemo samo vrednost in jo pretvorimo v int
            String startSecStr = v5.substring(valueStartIndex, valueEndIndex);
            int startSec = startSecStr.toInt();
            Firebase.printf("Kanal %d je spremenjen, nov začetni čas: %d\n", kanalIndex, startSec);
            // Vrnemo nove vrednosti v glavno zanko
            // Popravimo vrednost v firebase_kanali_data strukturi
            firebase_kanal[kanalIndex - 1].start_sec = startSec;
          }

          if (endSecIndex != -1)
          {
            // Najdemo začetek vrednosti (za ključem "end_sec":)
            int valueStartIndex = endSecIndex + strlen("end_sec\":");
            // Najdemo konec vrednosti (prva naslednja vejica ali zaklepaj)
            int valueEndIndex = v5.indexOf(',', valueStartIndex);
            if (valueEndIndex == -1)
            {
              valueEndIndex = v5.indexOf('}', valueStartIndex);
            }
            // Izvlečemo samo vrednost in jo pretvorimo v int
            String endSecStr = v5.substring(valueStartIndex, valueEndIndex);
            int endSec = endSecStr.toInt();
            Firebase.printf("Kanal %d je spremenjen, nov končni čas: %d\n", kanalIndex, endSec);
            // Popravimo vrednost v firebase_kanali_data strukturi
            firebase_kanal[kanalIndex - 1].end_sec = endSec;
          }
          // Nastavimo zastavico, da je na voljo nova posodobitev
          channelUpdate.kanalIndex = kanalIndex;
          channelUpdate.start_sec = startSec;
          channelUpdate.end_sec = endSec;
          newChannelDataAvailable = true;
        }
      }

      // Ali je sprememba na poti /charts/Interval?
      else if (path.startsWith("/charts/Interval"))
      {
        String chartData = stream.to<String>();
        uint8_t chartInterval = chartData.toInt();
        Firebase.printf("Chart Data: %s\n", chartData.c_str());
        // Nastavimo nov interval branja senzorjev
        uint8_t sensorReadIntervalMinutes = chartInterval;
        set_Interval(sensorReadIntervalMinutes); // Nastavimo interval branja senzorjev
        Firebase.printf("Nastavljen interval branja senzorjev: %d minut\n", sensorReadIntervalMinutes);
      }
    }
    else
    {
      Serial.println("----------------------------");
      Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
    }

    // Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
  }
}

//-----------------------------------------------------------------------------------------------------------
// Funkcija za obdelavo posodobljenih podatkov iz Streaminga Firebase
void Firebase_handleStreamUpdate(int kanalIndex, int start_sec, int end_sec)
{

  // Posodobimo lokalno strukturo kanal
  int index = kanalIndex - 1; // pretvorba na indeks od 0 do 7
  if (index >= 0 && index < 8)
  {
    // start_sec ali end_sec ima vrednost -1, kar pomeni, da ni veljavna posodobitev
    if (start_sec != -1)
    {
      // Posodobimo samo firebase_kanal
      firebase_kanal[index].start_sec = start_sec;
      formatSecondsToTime(firebase_kanal[index].start, sizeof(firebase_kanal[index].start), start_sec);
    }
    if (end_sec != -1)
    {
      firebase_kanal[index].end_sec = end_sec;
      formatSecondsToTime(firebase_kanal[index].end, sizeof(firebase_kanal[index].end), end_sec);
    }

    // Sedaj poskusimo poslati podatke
    if (lora_is_busy())
    {
      // LoRa je zasedena. Shranimo podatke v čakalno vrsto (mailbox).
      Serial.println("[STREAM] LoRa zasedena. Shranjujem posodobitev za kasneje.");
      pendingUpdateData.kanalIndex = index;
      pendingUpdateData.start_sec = firebase_kanal[index].start_sec;
      pendingUpdateData.end_sec = firebase_kanal[index].end_sec;
      firebaseUpdatePending = true; // Označimo, da imamo čakajoč ukaz
    }
    else
    {
      // LoRa je prosta. Takoj pošljemo ukaz.
      Serial.println("[STREAM] LoRa prosta. Pošiljam posodobitev takoj.");
      Rele_updateRelayUrnik(index, firebase_kanal[index].start_sec, firebase_kanal[index].end_sec);
      // Odgovor v Lora_handle_received_packet, - RESPONSE_UPDATE_URNIK.
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov iz Firebase (chart interval)

void Firebase_readInterval()
{
  Serial.printf("[FIREBASE] Branje intervala iz Firebase...\n");
  // Async call with callback function.
  Database.get(aClient, ChartIntervalPath, Firebase_processResponse, false /* only for Stream */, "getChartIntervalTask");
  // Odgovor bo obdelan v funkciji Firebase_processResponse
}
//------------------------------------------------------------------------------------------------------------------------
// Funkcija za branje podatkov iz Firebase (start_sec kanala)

void Firebase_get_Channel_Start_Seconds(int kanal)
{
  if (kanal >= 9)
    return;
  // Parent path for each kanal data entry
  String parentPath = KanaliPath + String(kanal) + startSecPath;
  // Async call with callback function.
  Database.get(aClient, parentPath, Firebase_processResponse, false /* only for Stream */, "getStartSecTask");
  // Odgovor bo obdelan v funkciji Firebase_processResponse
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za branje podatkov iz Firebase (end_sec kanala)

void Firebase_get_Channel_End_Seconds(int kanal)
{
  if (kanal >= 9)
    return;
  // Parent path for each kanal data entry
  String parentPath = KanaliPath + String(kanal) + endSecPath;
  // Async call with callback function.
  Database.get(aClient, parentPath, Firebase_processResponse, false /* only for Stream */, "getEndSecTask");
  // Odgovor bo obdelan v funkciji Firebase_processResponse
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija ki prebere urnik iz Firebase in ga shrani v globalno spremenljivko firebase_kanal
void Firebase_readKanalUrnik(uint8_t kanalIndex)
{
  Firebase_get_Channel_Start_Seconds(kanalIndex + 1); // preberemo start_sec kanala
  Firebase_get_Channel_End_Seconds(kanalIndex + 1);   // preberemo end_sec kanala
  // Odgovor bo obdelan v funkciji Firebase_processResponse
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov v Firebase (eno polje kanala)

void Firebase_Update_Relay_State(int kanal, bool state)
{

  object_t json;
  JsonWriter writer;
  writer.create(json, statePath, string_t(state ? "ON" : "OFF")); //-> {"data":{"value":x}}

  // Parent path for each kanal data entry
  parentPath = KanaliPath + String(kanal);

  // Async call with callback function.
  Database.update(aClient, parentPath, json, Firebase_processResponse, "updateStateTask");

  // Odgovor bo obdelan v funkciji Firebase_processResponse

  // waits until the value was sucessfully set
  // status = Database.update(aClient, parentPath, json);
  // show_status(status);
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov senzorjev v Firebase

void Firebase_Update_Sensor_Data(unsigned long timestamp, float temp, float hum, float soil_moisture)
{

  object_t json, obj1, obj2, obj3, obj4;
  JsonWriter writer;

  // Library does not provide JSON parser library, the following JSON writer class will be used with
  // object_t for simple demonstration.
  writer.create(obj1, tempPath, string_t(temp));
  writer.create(obj2, humPath, string_t(hum));
  writer.create(obj3, soilMoisturePath, string_t(soil_moisture));
  writer.create(obj4, timePath, String(timestamp));
  writer.join(json, 4 /* no. of object_t (s) to join */, obj1, obj2, obj3, obj4);
  //.create(json, String(timestamp), obj4); //-> {"data":{"value":x}}

  // Parent path for each sensor data entry
  parentPath = SensorPath + "/" + String(timestamp);

  // Async call with callback function.
  Database.set<object_t>(aClient, parentPath, json, Firebase_processResponse, "updateSensorTask");

  // waits until the value was sucessfully set
  // status = Database.set<object_t>(aClient, parentPath, json);
  // show_status(status);
}

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov INA3221 senzorja v Firebase
// Ta funkcija ustvari gnezdeno strukturo kot je opisano v zahtevi:
// sensors/ina3221_device_1/readings/{timestamp}/...

void Firebase_Update_INA3221_Data(const char* device_id, unsigned long timestamp, 
                                  uint16_t alert_flags, float shunt_voltage_sum_mV, float total_current_mA,
                                  float ch0_bus_V, float ch0_shunt_mV, float ch0_current_mA, float ch0_power_mW,
                                  float ch1_bus_V, float ch1_shunt_mV, float ch1_current_mA, float ch1_power_mW,
                                  float ch2_bus_V, float ch2_shunt_mV, float ch2_current_mA, float ch2_power_mW)
{
  object_t json;
  object_t ch0_obj, ch1_obj, ch2_obj;
  object_t ch0_bus, ch0_shunt, ch0_current, ch0_power;
  object_t ch1_bus, ch1_shunt, ch1_current, ch1_power;
  object_t ch2_bus, ch2_shunt, ch2_current, ch2_power;
  object_t alert_obj, shunt_sum_obj, total_current_obj;
  
  JsonWriter writer;

  // Ustvari objekte za ch0
  writer.create(ch0_bus, "bus_voltage_V", number_t(ch0_bus_V, 3));
  writer.create(ch0_shunt, "shunt_voltage_mV", number_t(ch0_shunt_mV, 3));
  writer.create(ch0_current, "current_mA", number_t(ch0_current_mA, 3));
  writer.create(ch0_power, "power_mW", number_t(ch0_power_mW, 3));
  writer.join(ch0_obj, 4, ch0_bus, ch0_shunt, ch0_current, ch0_power);

  // Ustvari objekte za ch1
  writer.create(ch1_bus, "bus_voltage_V", number_t(ch1_bus_V, 3));
  writer.create(ch1_shunt, "shunt_voltage_mV", number_t(ch1_shunt_mV, 3));
  writer.create(ch1_current, "current_mA", number_t(ch1_current_mA, 3));
  writer.create(ch1_power, "power_mW", number_t(ch1_power_mW, 3));
  writer.join(ch1_obj, 4, ch1_bus, ch1_shunt, ch1_current, ch1_power);

  // Ustvari objekte za ch2
  writer.create(ch2_bus, "bus_voltage_V", number_t(ch2_bus_V, 3));
  writer.create(ch2_shunt, "shunt_voltage_mV", number_t(ch2_shunt_mV, 3));
  writer.create(ch2_current, "current_mA", number_t(ch2_current_mA, 3));
  writer.create(ch2_power, "power_mW", number_t(ch2_power_mW, 3));
  writer.join(ch2_obj, 4, ch2_bus, ch2_shunt, ch2_current, ch2_power);

  // Ustvari glavne vrednosti (alert_flags, shunt_voltage_sum_mV, total_current_mA)
  writer.create(alert_obj, "alert_flags", alert_flags);
  writer.create(shunt_sum_obj, "shunt_voltage_sum_mV", number_t(shunt_voltage_sum_mV, 3));
  writer.create(total_current_obj, "total_current_mA", number_t(total_current_mA, 3));

  // Preoblikuj ch0_obj, ch1_obj, ch2_obj v object_t s ključi
  object_t ch0_with_key, ch1_with_key, ch2_with_key;
  writer.create(ch0_with_key, "ch0", ch0_obj);
  writer.create(ch1_with_key, "ch1", ch1_obj);
  writer.create(ch2_with_key, "ch2", ch2_obj);

  // Združi vse v končni JSON objekt
  writer.join(json, 6, alert_obj, shunt_sum_obj, total_current_obj, 
              ch0_with_key, ch1_with_key, ch2_with_key);

  // Ustvari pot v Firebase: /UserData/{uid}/sensors/{device_id}/readings/{timestamp}
  String parentPath = databasePath + "/sensors/" + String(device_id) + "/readings/" + String(timestamp);

  // Pošlji podatke v Firebase
  Database.set<object_t>(aClient, parentPath, json, Firebase_processResponse, "updateINA3221Task");
}

//----------------------------------------------------------------------------------------------------------------------
// Funkcija za obdelavo odgovora iz Firebase
void Firebase_processResponse(AsyncResult &aResult)
{

  static uint8_t channelBeingProcessed = 0;
  static bool startSecReceived = false;
  static bool endSecReceived = false;

  if (!aResult.isResult())
    return;

  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

  // here you get the values from the database and save them in variables if you need to use them later
  if (aResult.available())
  {
    // Log the task and payload
    Firebase.printf("[FIREBASE] task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());

    // Handle int value from /Kanali/kanalX/start_sec
    if (aResult.uid() == "getStartSecTask")
    {
      // Extract the value as a int
      get_intValue = ParseToInt(aResult.payload());
      Firebase.printf("[FIREBASE] Kanal field data (async): %d\n", get_intValue);
      firebase_kanal[currentChannelInProcess].start_sec = get_intValue;
      // shranimo tudi v obliki "HH:MM"
      formatSecondsToTime(firebase_kanal[currentChannelInProcess].start, sizeof(firebase_kanal[currentChannelInProcess].start), get_intValue);
      // channelBeingProcessed++;
      startSecReceived = true; // označimo, da smo prejeli start_sec
    }
    // Handle int value from /Kanali/kanalX/end_sec
    else if (aResult.uid() == "getEndSecTask")
    {
      // Extract the value as a int
      get_intValue = ParseToInt(aResult.payload());
      Firebase.printf("[FIREBASE] Kanal field data (async): %d\n", get_intValue);
      firebase_kanal[currentChannelInProcess].end_sec = get_intValue;
      // shranimo tudi v obliki "HH:MM"
      formatSecondsToTime(firebase_kanal[currentChannelInProcess].end, sizeof(firebase_kanal[currentChannelInProcess].end), get_intValue);
      // channelBeingProcessed++;
      endSecReceived = true; // označimo, da smo prejeli end_sec
    }

    // ko preberemo obe vrednosti enega kanala, lahko nadaljujemo
    if (startSecReceived && endSecReceived)
    {
      Serial.printf("[FIREBASE] Prebrano iz Firebase za kanal %d: start=%d, end=%d\n",
                    currentChannelInProcess + 1,
                    firebase_kanal[currentChannelInProcess].start_sec,
                    firebase_kanal[currentChannelInProcess].end_sec);
      // channelBeingProcessed = 0; // reset za naslednji kanal
      startSecReceived = false;
      endSecReceived = false;
      firebase_response_received = true;
    }

    if (aResult.uid() == "updateSensorTask")
    {
      // Handle the updateSensorTask response
      Serial.println("[FIREBASE] Sensor data uploaded");
      PrikaziStanjeSenzorjevNaSerial();  // napišemo podatke
      firebase_response_received = true; // označimo da je Firebase prejel podatke
    }

    if (aResult.uid() == "updateStateTask")
    {
      // Handle the updateStateTask response
      Serial.println("[FIREBASE] State data uploaded");
      firebase_response_received = true; // označimo da je Firebase prejel podatke
    }

    if (aResult.uid() == "getChartIntervalTask")
    {
      uint8_t sensorReadIntervalMinutes = aResult.payload().toInt();
      set_Interval(sensorReadIntervalMinutes);  // Nastavimo interval branja senzorjev
      Serial.printf("[FIREBASE] Sensor read interval set to: %d minutes\n", sensorReadIntervalMinutes);
      firebase_response_received = true; // označimo da je Firebase prejel podatke
    }

    if (aResult.uid() == "updateINA3221Task")
    {
      // Handle the updateINA3221Task response
      Serial.println("[FIREBASE] INA3221 sensor data uploaded");
      firebase_response_received = true; // označimo da je Firebase prejel podatke
    }
    
  }
}
//------------------------------------------------------------------------------------------------------------------------
// Funkcija za posodobitev podatkov iz Firebase (chart interval)

// void Firebase_readInterval()
// {

//   int start_sec = ura * 3600 + minuta * 60;
//   String minuta_str;
//   // Dodaj vodilno ničlo, če je minuta manjša od 10
//   if (minuta < 10)
//   {
//     minuta_str = "0" + String(minuta);
//   }
//   else
//   {
//     minuta_str = String(minuta);
//   }

//   object_t json, obj1, obj2;
//   JsonWriter writer;
//   writer.create(obj1, startPath, String(String(ura) + ":" + minuta_str)); //-> {"start":"17:00""}
//   writer.create(obj2, startSecPath, String(start_sec));                  //-> {"start_sec":"61200"}
//   writer.join(json, 2 /* no. of object_t (s) to join */, obj1, obj2);   // -> {"start":"17:00","start_sec":"61200"}
//   Serial.println("Setting the JSON value for start time... ");
//   Serial.println(json);

//   // Async call with callback function.
//   // Database.update(aClient, "/kanali/kanal" + String(kanal), json, processData, "updateKanalStartTask");

//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);

//   // waits until the value was sucessfully set
//   status = Database.update(aClient, parentPath, json);
//   show_status(status);
// }

// //------------------------------------------------------------------------------------------------------------------------
// // Funkcija za posodobitev podatkov v Firebase (stop časa kanala)

// void Firebase_Update_Relay_End(int kanal, int ura, int minuta)
// {

//   int end_sec = ura * 3600 + minuta * 60;
//   String minuta_str;
//   // Dodaj vodilno ničlo, če je minuta manjša od 10
//   if (minuta < 10)
//   {
//     minuta_str = "0" + String(minuta);
//   }
//   else
//   {
//     minuta_str = String(minuta);
//   }

//   object_t json, obj1, obj2;
//   JsonWriter writer;
//   writer.create(obj1, endPath, String(String(ura) + ":" + minuta_str)); //-> {"data":{"value":x}}
//   writer.create(obj2, endSecPath, String(end_sec));                    //-> {"data":{"value":x}}
//   writer.join(json, 2 /* no. of object_t (s) to join */, obj1, obj2);
//   Serial.println("Setting the JSON value for end time... ");
//   Serial.println(json);

//   // Async call with callback function.
//   // Database.update(aClient, "/kanali/kanal" + String(kanal), json, processData, "updateKanalEndTask");

//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);

//   // waits until the value was sucessfully set
//   status = Database.update(aClient, parentPath, json);
//   show_status(status);
// }
//------------------------------------------------------------------------------------------------------------------------
// Funkcija za pošiljanje vseh podatkov o kanalih v Firebase
// void setAllChannelData()
// {
//   object_t json, obj1, obj2, obj3, obj4, obj5, obj6;
//   JsonWriter writer;
//   // Library does not provide JSON parser library, the following JSON writer class will be used with
//   // object_t for simple demonstration.

//   for (int i = 0; i < 8; i++)
//   {
//     obj1.clear();
//     obj2.clear();
//     obj3.clear();
//     obj4.clear();
//     json.clear();
//     writer.create(obj1, statePath, string_t(kanal[i].state ? "ON" : "OFF"));
//     writer.create(obj2, startPath, string_t(kanal[i].start));
//     writer.create(obj3, startSecPath, string_t(kanal[i].start_sec));
//     writer.create(obj4, endPath, string_t(kanal[i].end));
//     writer.create(obj5, endSecPath, string_t(kanal[i].end_sec));
//     writer.join(json, 5 /* no. of object_t (s) to join */, obj1, obj2, obj3, obj4, obj5);

//     // To print object_t
//     // Serial.println(json);
//     // Serial.println("Setting the JSON value... ");

//     // Async call with callback function.
//     // Database.set<object_t>(aClient, "/kanali/kanal" + String(i + 1), json, processData, "setAllKanalTask");

//     // Parent path for each kanal data entry
//     parentPath = KanaliPath + String(i + 1);

//     // waits until the value was sucessfully set
//     status = Database.set<object_t>(aClient, parentPath, json);
//     show_status(status);
//   }
// }

// }

// //------------------------------------------------------------------------------------------------------------------------
// // Funkcija za branje podatkov v Firebase (vse podatke kanala) z async

// void Firebase_get_Kanal_data_async(int kanal)
// {
//   Serial.println("Getting the String value of kanal " + String(kanal) + "... ");
//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);
//   // Async call with callback function.
//    Database.get(aClient, parentPath, processData, false /* only for Stream */, "getKanalTaskAsync");

// }

// //------------------------------------------------------------------------------------------------------------------------
// // Funkcija za branje podatkov v Firebase (vse podatke kanala) z čakanjem na odgovor

// String Firebase_get_Kanal_data(int kanal)
// {
//   // Async call with callback function.
//   // Database.get(aClient, "/kanali/kanal" + String(kanal), processData, false /* only for Stream */, "getKanalTask");

//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);

//   // waits until the value was sucessfully get
//   Serial.println("Getting the String value of kanal " + String(kanal) + "... ");
//   String value = Database.get<String>(aClient, parentPath);
//   if (check_and_print_value(value))
//   {
//     return value;
//   }
//   else
//   {
//     return String("");
//   }
// }

//------------------------------------------------------------------------------------------------------------------------
// // Funkcija za branje podatkov v Firebase (eno polje kanala)

// void Firebase_get_Channel_Field_Async(int kanal, String polje)
// {

//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);
//   // Async call with callback function.
//   Database.get(aClient, parentPath + "/" + polje, Firebase_processResponse, false /* only for Stream */, "getFieldTask");

// }

// //------------------------------------------------------------------------------------------------------------------------
// // Funkcija za branje podatkov v Firebase (start_sec kanala)

// void Firebase_get_Channel_Start_Seconds(int kanal)
// {
//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);
//   // Async call with callback function.
//   Database.get(aClient, parentPath + startSecPath, Firebase_processResponse, false /* only for Stream */, "getStartSecTask");

// }

// //------------------------------------------------------------------------------------------------------------------------
// // Funkcija za branje podatkov v Firebase (end_sec kanala)

// void Firebase_get_Channel_End_Seconds(int kanal)
// {
//   // Parent path for each kanal data entry
//   parentPath = KanaliPath + String(kanal);
//   // Async call with callback function.
//   Database.get(aClient, parentPath + endSecPath, Firebase_processResponse, false /* only for Stream */, "getEndSecTask");

// }

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za branje podatkov v Firebase (eno polje kanala)

// void Firebase_get_Channel_Field(int kanal, String polje)
// {
//   // Async call with callback function.
//   // Database.get(aClient, "/kanali/kanal" + String(kanal) + "/" + polje, processData, false /* only for Stream */, "getPoljeTask");

//     // Parent path for each kanal data entry
//     parentPath = KanaliPath + String(kanal);

//   // waits until the value was sucessfully get
//   String value = Database.get<String>(aClient, parentPath + "/" + polje);
//   check_and_print_value(value);
// }

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za branje podatkov v Firebase (stanje relejev)

// void Firebase_get_Relay_Status()
// {
//   // GET VALUES FROM DATABASE (using the callback async method method)
//   // you can then get the values on the processData function as soon as the results are available

//   for (int i = 1; i < 9; i++)
//   {
//     // Parent path for each kanal data entry
//     parentPath = KanaliPath + String(i) + statePath;

//     // Async call with callback function.
//     // Database.get(aClient, parentPath + "/state", processData, false /* only for Stream */, "getRelay" + String(i + 1) + "State");

//     // waits until the value was sucessfully get
//     String value = Database.get<String>(aClient, parentPath);
//     if (check_and_print_value(value))
//     {
//       // Do something with the valid value
//     }
//   }
// }

//----------------------------------------------------------------------------------------------------------------------------------------
// void processData(AsyncResult &aResult)
// {
//   if (!aResult.isResult())
//     return;

//   if (aResult.isEvent())
//     Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

//   if (aResult.isDebug())
//     Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

//   if (aResult.isError())
//     Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

//   // here you get the values from the database and save them in variables if you need to use them later
//   if (aResult.available())
//   {
//     // Log the task and payload
//     Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());

//     // Extract the payload as a String
//     String payload = aResult.c_str();

//     /// Handle int from /test/int
//     if (aResult.uid() == "updateKanalStartTask" || aResult.uid() == "updateKanalEndTask")
//     {
//       // Extract the value as an string
//       get_stringValue = payload;
//       Firebase.printf("Kanal time updated successfully: %s\n", get_stringValue.c_str());
//     }
//     // Handle float from /test/float
//     else if (aResult.uid() == "getPoljeTask")
//     {
//       // Extract the value as a string
//       get_stringValue = payload;
//       Firebase.printf("Kanal polje value: %s\n", get_stringValue.c_str());
//       // get_floatValue = payload.toFloat();
//       // Firebase.printf("Stored floatValue: %.2f\n", get_floatValue);
//     }

//     // Handle String from /test/string
//     else if (aResult.uid() == "getKanalTask")
//     {
//       // Extract the value as a String
//       get_stringValue = payload;
//       Firebase.printf("Kanal data: %s\n", get_stringValue.c_str());
//       // Firebase.printf("Stored stringValue: %s\n", get_stringValue.c_str());
//     }

//     // Handle all data from /Kanali/kanalX
//     else if (aResult.uid() == "getKanalTaskAsync")
//     {
//       // Extract the value as a String
//       get_stringValue = payload;
//       Firebase.printf("Kanal data (async): %s\n", get_stringValue.c_str());
//       // Firebase.printf("Stored stringValue: %s\n", get_stringValue.c_str());
//     }

//     // Handle int value from /Kanali/kanalX/end_sec
//     else if (aResult.uid() == "getFieldTask")
//     {
//       // Extract the value as a int
//       get_intValue = payload.toInt();
//       Firebase.printf("Kanal field data (async): %d\n", get_intValue);
//       // Firebase.printf("Stored stringValue: %s\n", get_stringValue.c_str());
//     }

//     // Handle Relay1 status from /rele1/status
//     else if (aResult.uid() == "GetRelay1State")
//     {
//       relayState[0] = (payload == "ON");
//       Firebase.printf("Relay 1 status: %s\n", relayState[0] ? "ON" : "OFF");
//     }
//     // Handle Relay2 status from /rele2/status
//     else if (aResult.uid() == "GetRelay2State")
//     {
//       relayState[1] = (payload == "ON");
//       Firebase.printf("Relay 2 status: %s\n", relayState[1] ? "ON" : "OFF");
//     }
//     // Handle Relay3 status from /rele3/status
//     else if (aResult.uid() == "GetRelay3State")
//     {
//       relayState[2] = (payload == "ON");
//       Firebase.printf("Relay 3 status: %s\n", relayState[2] ? "ON" : "OFF");
//     }
//     // Handle Relay4 status from /rele4/status
//     else if (aResult.uid() == "GetRelay4State")
//     {
//       relayState[3] = (payload == "ON");
//       Firebase.printf("Relay 4 status: %s\n", relayState[3] ? "ON" : "OFF");
//     }
//     // Handle Relay5 status from /rele5/status
//     else if (aResult.uid() == "GetRelay5State")
//     {
//       relayState[4] = (payload == "ON");
//       Firebase.printf("Relay 5 status: %s\n", relayState[4] ? "ON" : "OFF");
//     }
//     // Handle Relay6 status from /rele6/status
//     else if (aResult.uid() == "GetRelay6State")
//     {
//       relayState[5] = (payload == "ON");
//       Firebase.printf("Relay 6 status: %s\n", relayState[5] ? "ON" : "OFF");
//     }
//     // Handle Relay7 status from /rele7/status
//     else if (aResult.uid() == "GetRelay7State")
//     {
//       relayState[6] = (payload == "ON");
//       Firebase.printf("Relay 7 status: %s\n", relayState[6] ? "ON" : "OFF");
//     }
//     // Handle Relay8 status from /rele8/status
//     else if (aResult.uid() == "GetRelay8State")
//     {
//       relayState[7] = (payload == "ON");
//       Firebase.printf("Relay 8 status: %s\n", relayState[7] ? "ON" : "OFF");
//     }
//   }
// }

// //----------------------------------------------------------------------------------------------------------------------------------------
// void set_async()
// {
//     // Set the specific value (no waits)
//     // Using Database.set with the callback function or AsyncResult object

//     // Set int
//     Serial.println("Setting the int value... ");

//     Database.set<int>(aClient, "/examples/Set/Async1/int", 12345, processData, "setIntTask");

//     // Set bool
//     Serial.println("Setting the bool value... ");
//     Database.set<bool>(aClient, "/examples/Set/Async1/bool", true, processData, "setBoolTask");

//     // Set string
//     Serial.println("Setting the String value... ");
//     Database.set<String>(aClient, "/examples/Set/Async1/String", "hello", processData, "setStringTask");

//     // Set json
//     Serial.println("Setting the JSON value... ");
//     Database.set<object_t>(aClient, "/examples/Set/Async1/JSON", object_t("{\"data\":123}"), processData, "setJsonTask");

//     object_t json, obj1, obj2, obj3, obj4;
//     JsonWriter writer;

//     writer.create(obj1, "int/value", 9999);
//     writer.create(obj2, "string/value", string_t("hello"));
//     writer.create(obj3, "float/value", number_t(123.456, 2));
//     writer.join(obj4, 3 /* no. of object_t (s) to join */, obj1, obj2, obj3);
//     writer.create(json, "node/list", obj4);

//     // To print object_t
//     // Serial.println(json);
//     Serial.println("Setting the JSON value... ");
//     Database.set<object_t>(aClient, "/examples/Set/Async1/JSON", json, processData, "setJsonTask");

//     object_t arr;
//     arr.initArray(); // initialize to be used as array
//     writer.join(arr, 4 /* no. of object_t (s) to join */, object_t("[12,34]"), object_t("[56,78]"), object_t(string_t("steve")), object_t(888));

//     // Note that value that sets to object_t other than JSON ({}) and Array ([]) can be valid only if it
//     // used as array member value as above i.e. object_t(string_t("steve")) and object_t(888).

//     // Set array
//     Serial.println("Setting the Array value... ");
//     Database.set<object_t>(aClient, "/examples/Set/Async1/Array", arr, processData, "setArrayTask");

//     // Set float
//     Serial.println("Setting the float value... ");
//     Database.set<number_t>(aClient, "/examples/Set/Async1/float", number_t(123.456, 2), processData, "setFloatTask");

//     // Set double
//     Serial.println("Setting the double value... ");
//     Database.set<number_t>(aClient, "/examples/Set/Async1/double", number_t(1234.56789, 4), processData, "setDoubleTask");
// }

// void set_async2()
// {
//     // Set the specific value (no waits)
//     // Using Database.set with the callback function or AsyncResult object

//     // Set int
//     Serial.println("Setting the int value... ");

//     Database.set<int>(aClient, "/examples/Set/Async2/int", 12345, databaseResult);

//     // Set bool
//     Serial.println("Setting the bool value... ");
//     Database.set<bool>(aClient, "/examples/Set/Async2/bool", true, databaseResult);

//     // Set string
//     Serial.println("Setting the String value... ");
//     Database.set<String>(aClient, "/examples/Set/Async2/String", "hello", databaseResult);

//     // Set json
//     Serial.println("Setting the JSON value... ");
//     Database.set<object_t>(aClient, "/examples/Set/Async2/JSON", object_t("{\"data\":123}"), databaseResult);

//     object_t json, obj1, obj2, obj3, obj4;
//     JsonWriter writer;

//     writer.create(obj1, "int/value", 9999);
//     writer.create(obj2, "string/value", string_t("hello"));
//     writer.create(obj3, "float/value", number_t(123.456, 2));
//     writer.join(obj4, 3 /* no. of object_t (s) to join */, obj1, obj2, obj3);
//     writer.create(json, "node/list", obj4);

//     // To print object_t
//     // Serial.println(json);
//     Serial.println("Setting the JSON value... ");
//     Database.set<object_t>(aClient, "/examples/Set/Async2/JSON", json, databaseResult);

//     object_t arr;
//     arr.initArray(); // initialize to be used as array
//     writer.join(arr, 4 /* no. of object_t (s) to join */, object_t("[12,34]"), object_t("[56,78]"), object_t(string_t("steve")), object_t(888));

//     // Note that value that sets to object_t other than JSON ({}) and Array ([]) can be valid only if it
//     // used as array member value as above i.e. object_t(string_t("steve")) and object_t(888).

//     // Set array
//     Serial.println("Setting the Array value... ");
//     Database.set<object_t>(aClient, "/examples/Set/Async2/Array", arr, databaseResult);

//     // Set float
//     Serial.println("Setting the float value... ");
//     Database.set<number_t>(aClient, "/examples/Set/Async2/float", number_t(123.456, 2), databaseResult);

//     // Set double
//     Serial.println("Setting the double value... ");
//     Database.set<number_t>(aClient, "/examples/Set/Async2/double", number_t(1234.56789, 4), databaseResult);
// }

// void show_status(bool status)
// {
//   if (status)
//   {
//     Serial.println("🔼 Update task(await), complete!");
//   }
//   else
//   {
//     Firebase.printf("Error, msg: %s, code: %d\n", aClient.lastError().message().c_str(), aClient.lastError().code());
//   }
//   Serial.println();

// }

// void set_await()
// {
//     // Set the specific value (waits until the value was sucessfully set)
//     // Using Database.set<T>

//     // Set int
//     Serial.println("Setting the int value... ");
//     bool status = Database.set<int>(aClient, "/examples/Set/Await/int", 12345);
//     show_status(status);

//     // Set bool
//     Serial.println("Setting the bool value... ");
//     status = Database.set<bool>(aClient, "/examples/Set/Await/bool", true);
//     show_status(status);

//     // Set string
//     Serial.println("Setting the String value... ");
//     status = Database.set<String>(aClient, "/examples/Set/Await/String", "hello");
//     show_status(status);

//     // Set json
//     Serial.println("Setting the JSON value... ");
//     status = Database.set<object_t>(aClient, "/examples/Set/Await/JSON", object_t("{\"data\":123}"));
//     show_status(status);

//     // Library does not provide JSON parser library, the following JSON writer class will be used with
//     // object_t for simple demonstration.

//     object_t json, obj1, obj2, obj3, obj4;
//     JsonWriter writer;

//     writer.create(obj1, "int/value", 9999);
//     writer.create(obj2, "string/value", string_t("hello"));
//     writer.create(obj3, "float/value", number_t(123.456, 2));
//     writer.join(obj4, 3 /* no. of object_t (s) to join */, obj1, obj2, obj3);
//     writer.create(json, "node/list", obj4);

//     // To print object_t
//     // Serial.println(json);

//     Serial.println("Setting the JSON value... ");
//     status = Database.set<object_t>(aClient, "/examples/Set/Await/JSON", json);
//     show_status(status);

//     object_t arr;
//     arr.initArray(); // initialize to be used as array
//     writer.join(arr, 4 /* no. of object_t (s) to join */, object_t("[12,34]"), object_t("[56,78]"), object_t(string_t("steve")), object_t(888));

//     // Note that value that sets to object_t other than JSON ({}) and Array ([]) can be valid only if it
//     // used as array member value as above i.e. object_t(string_t("steve")) and object_t(888).

//     // Set array
//     Serial.println("Setting the Array value... ");
//     status = Database.set<object_t>(aClient, "/examples/Set/Await/Array", arr);
//     show_status(status);

//     // Set float
//     Serial.println("Setting the float value... ");
//     status = Database.set<number_t>(aClient, "/examples/Set/Await/float", number_t(123.456, 2));
//     show_status(status);

//     // Set double
//     Serial.println("Setting the double value... ");
//     status = Database.set<number_t>(aClient, "/examples/Set/Await/double", number_t(1234.56789, 4));
//     show_status(status);
// }

// void get_async(int kanal)
// {
//     // Get the generic value (no waits)
//     // Using Database.get with the callback function or AsyncResult object

//     Serial.println("Getting the value of kanal. ");

//     // Async call with callback function.
//     Database.get(aClient, "/kanali/kanal" + String(kanal), processData, false /* only for Stream */, "getTask");

//     // Apply the filter
//     // DatabaseOptions options;
//     // options.filter.orderBy("Data").startAt(105).endAt(120).limitToLast(8);

//     // Serial.println("Getting the value with filter... ");

//     // // Async call with callback function.
//     // Database.get(aClient, "/kanali/kanal3", options, processData, "queryTask");
// }

// void get_async2(int kanal, String polje)
// {
//     // Get the generic value (no waits)
//     // Using Database.get with the callback function or AsyncResult object

//     Serial.println("Getting the value of cell... ");

//     // Async call with AsyncResult for returning result.
//     Database.get(aClient, "/kanali/kanal" + String(kanal) + "/" + polje, processData, false /* only for Stream */, "getTask");

//     // Apply the filter
//     // DatabaseOptions options;
//     // options.filter.orderBy("Data").startAt(105).endAt(120).limitToLast(8);

//     // Serial.println("Getting the value with filter... ");

//     // // Async call with AsyncResult for returning result.
//     // Database.get(aClient, "/examples/Get/Async/data4", options, databaseResult);
// }

// template <typename T>
// bool check_and_print_value(T value)
// {
//   // To make sure that we actually get the result or error.
//   if (aClient.lastError().code() == 0)
//   {
//     Serial.print("Success!\n");
//     // Serial.println(value);
//     return true;
//   }
//   else
//   {
//     Firebase.printf("Error, msg: %s, code: %d\n", aClient.lastError().message().c_str(), aClient.lastError().code());
//     return false;
//   }
// }

// void get_await()
// {
//     // Get the specific value (waits until the value was received)
//     // Using Database.get<T>

//     Serial.println("Getting the int value... ");
//     int value1 = Database.get<int>(aClient, "/examples/Get/Await/data");
//     check_and_print_value(value1);

//     Serial.println("Getting the bool value... ");
//     bool value2 = Database.get<bool>(aClient, "/examples/Get/Await/data");
//     check_and_print_value(value2);

//     Serial.println("Getting the float value... ");
//     float value3 = Database.get<float>(aClient, "/examples/Get/Await/data");
//     check_and_print_value(value3);

//     Serial.println("Getting the double value... ");
//     double value4 = Database.get<double>(aClient, "/examples/Get/Await/data");
//     check_and_print_value(value4);

//     Serial.println("Getting the String value... ");
//     // The filter can be applied.
//     String value5 = Database.get<String>(aClient, "/examples/Get/Await/data");
//     check_and_print_value(value5);
// }

//------------------------------------------------------------------------------------------------------------------------
// Funkcija za vstavljanje časovnega žiga v Firebase

// void Insert_Timestamp()
// {
//     object_t ts_json;
//     JsonWriter writer;
//     writer.create(ts_json, ".sv", "timestamp"); // -> {".sv": "timestamp"}

//     Serial.println("Setting only timestamp... ");
//     Serial.println(ts_json);

//     bool status = Database.set<object_t>(aClient, "/timestamp", ts_json);
//     if (status)
//         Serial.println(String("Success"));
//     else
//         Firebase.printf("Error, msg: %s, code: %d\n", aClient.lastError().message().c_str(), aClient.lastError().code());

//     object_t data_json, Ts_json, ts_data_json;

//     writer.create(data_json, "data", "hello");        // -> {"data": "hello"}
//     writer.create(Ts_json, "Ts", ts_json);        // -> {"Ts":{".sv": "timestamp"}}
//     writer.join(ts_data_json, 2, data_json, Ts_json); // -> {"data":"hello","Ts":{".sv":"timestamp"}}

//     Serial.println("Setting timestamp and data... ");
//     status = Database.set<object_t>(aClient, "/timestamp2", ts_data_json);
//     if (status)
//         Serial.println(String("Success"));
//     else
//         Firebase.printf("Error, msg: %s, code: %d\n", aClient.lastError().message().c_str(), aClient.lastError().code());
// }
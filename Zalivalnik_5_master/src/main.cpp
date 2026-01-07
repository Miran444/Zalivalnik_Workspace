/**
 * The beare minimum code example for using Realtime Database service.
 *
 * The steps which are generally required are explained below.
 *
 * Step 1. Include the network, SSL client and Firebase libraries.
 * ===============================================================
 *
 * Step 2. Define the user functions that requred for the library usage.
 * =====================================================================
 *
 * Step 3. Define the authentication config (identifier) class.
 * ============================================================
 * In the Firebase/Google Cloud services REST APIs, the auth tokens are used for authentication/authorization.
 *
 * The auth token is a short-lived token that will be expired in 60 minutes and need to be refreshed or re-created when it expired.
 *
 * There can be some special use case that some services provided the non-authentication usages e.g. using database secret
 * in Realtime Database, setting the security rules in Realtime Database, Firestore and Firebase Storage to allow public read/write access.
 *
 * The UserAuth (user authentication with email/password) is the basic authentication for Realtime Database,
 * Firebase Storage and Firestore services except for some Firestore services that involved with the Google Cloud services.
 *
 * It stores the email, password and API keys for authentication process.
 *
 * In Google Cloud services e.g. Cloud Storage and Cloud Functions, the higest authentication level is required and
 * the ServiceAuth class (OAuth2.0 authen) and AccessToken class will be use for this case.
 *
 * While the CustomAuth provides the same authentication level as user authentication unless it allows the custom UID and claims.
 *
 * Step 4. Define the authentication handler class.
 * ================================================
 * The FirebaseApp actually works as authentication handler.
 * It also maintains the authentication or re-authentication when you place the FirebaseApp::loop() inside the main loop.
 *
 * Step 5. Define the SSL client.
 * ==============================
 * It handles server connection and data transfer works.
 *
 * In this beare minimum example we use only one SSL client for all processes.
 * In some use cases e.g. Realtime Database Stream connection, you may have to define the SSL client for it separately.
 *
 * Step 6. Define the Async Client.
 * ================================
 * This is the class that is used with the functions where the server data transfer is involved.
 * It stores all sync/async taks in its queue.
 *
 * It requires the SSL client and network config (identifier) data for its class constructor for its network re-connection
 * (e.g. WiFi and GSM), network connection status checking, server connection, and data transfer processes.
 *
 * This makes this library reliable and operates precisely under various server and network conditions.
 *
 * Step 7. Define the class that provides the Firebase/Google Cloud services.
 * ==========================================================================
 * The Firebase/Google Cloud services classes provide the member functions that works with AsyncClient.
 *
 * Step 8. Start the authenticate process.
 * ========================================
 * At this step, the authentication credential will be used to generate the auth tokens for authentication by
 * calling initializeApp.
 *
 * This allows us to use different authentications for each Firebase/Google Cloud services with different
 * FirebaseApps (authentication handler)s.
 *
 * When calling initializeApp with timeout, the authenication process will begin immediately and wait at this process
 * until it finished or timed out. It works in sync mode.
 *
 * If no timeout was assigned, it will work in async mode. The authentication task will be added to async client queue
 * to process later e.g. in the loop by calling FirebaseApp::loop.
 *
 * The workflow of authentication process.
 *
 * -----------------------------------------------------------------------------------------------------------------
 *  Setup   |    FirebaseApp [account credentials/tokens] ───> InitializeApp (w/wo timeout) ───> FirebaseApp::getApp
 * -----------------------------------------------------------------------------------------------------------------
 *  Loop    |    FirebaseApp::loop  ───> FirebaseApp::ready ───> Firebase Service API [auth token]
 * ---------------------------------------------------------------------------------------------------
 *
 * Step 9. Bind the FirebaseApp (authentication handler) with your Firebase/Google Cloud services classes.
 * ========================================================================================================
 * This allows us to use different authentications for each Firebase/Google Cloud services.
 *
 * It is easy to bind/unbind/change the authentication method for different Firebase/Google Cloud services APIs.
 *
 * Step 10. Set the Realtime Database URL (for Realtime Database only)
 * ===================================================================
 *
 * Step 11. Maintain the authentication and async tasks in the loop.
 * ==============================================================
 * This is required for authentication/re-authentication process and keeping the async task running.
 *
 * Step 12. Checking the authentication status before use.
 * =======================================================
 * Before calling the Firebase/Google Cloud services functions, the FirebaseApp::ready() of authentication handler that bined to it
 * should return true.
 *
 * Step 13. Process the results of async tasks the end of the loop.
 * ============================================================================
 * This requires only when async result was assigned to the Firebase/Google Cloud services functions.
 */

 /**
 * The example to stream changes to multiple locations in Realtime Database.
 *
 * This example uses the UserAuth class for authentication.
 * See examples/App/AppInitialization for more authentication examples.
 *
 * For the complete usage guidelines, please read README.md or visit https://github.com/mobizt/FirebaseClient
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#define ENABLE_ESP_SSLCLIENT

#include <FirebaseClient.h>
#include "ExampleFunctions.h" // Provides the functions used in the examples.
#include "credentials.h"  // Insert your network credentials


void processData(AsyncResult &aResult);

WiFiClient basic_client1, basic_client2;

// The ESP_SSLClient uses PSRAM by default (if it is available), for PSRAM usage, see https://github.com/mobizt/FirebaseClient#memory-options
// For ESP_SSLClient documentation, see https://github.com/mobizt/ESP_SSLClient
ESP_SSLClient ssl_client, stream_ssl_client1;

using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client), streamClient1(stream_ssl_client1);

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
RealtimeDatabase Database;
AsyncResult streamResult1, streamResult2;

char databasePath[48];
char sensorPath[64];
char inaPath[64];
char kanaliPath[64];
char chartIntervalPath[72];
char examplesPath[64];
char examplePath1[64];
char examplePath2[64];
char uid[32];
unsigned long ms = 0;
bool extract_uid = false;

// Dodajte globalno spremenljivko za sledenje minimalnega heap-a
uint32_t minHeapDuringAuth = 0xFFFFFFFF;
uint32_t heapBeforeAuth = 0;

void auth_debug_print(const char* info) 
{
    uint32_t currentHeap = ESP.getFreeHeap();
    
    // Beleži minimalni heap med avtentikacijo
    if (currentHeap < minHeapDuringAuth) {
        minHeapDuringAuth = currentHeap;
    }
    
    Firebase.printf("AUTH [Heap: %d, Min: %d]: %s\n", 
                    currentHeap, minHeapDuringAuth, info);
}

void setup()
{
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();
    
    heapBeforeAuth = ESP.getFreeHeap();
    Firebase.printf("Free Heap BEFORE AUTH: %d\n", heapBeforeAuth);
    Firebase.printf("Firebase Client v%s\n", FIREBASE_CLIENT_VERSION);

    ssl_client.setClient(&basic_client1);
    stream_ssl_client1.setClient(&basic_client2);
    // stream_ssl_client2.setClient(&basic_client3);

    ssl_client.setInsecure();
    stream_ssl_client1.setInsecure();
    // stream_ssl_client2.setInsecure();

    ssl_client.setBufferSizes(2048, 1024);
    stream_ssl_client1.setBufferSizes(2048, 1024);
    // stream_ssl_client2.setBufferSizes(2048, 1024);

    // In case using ESP8266 without PSRAM and you want to reduce the memory usage,
    // you can use WiFiClientSecure instead of ESP_SSLClient with minimum receive and transmit buffer size setting as following.
    // ssl_client1.setBufferSizes(1024, 512);
    // ssl_client2.setBufferSizes(1024, 512);
    // ssl_client3.setBufferSizes(1024, 512);
    // Note that, because the receive buffer size was set to minimum safe value, 1024, the large server response may not be able to handle.
    // The WiFiClientSecure uses 1k less memory than ESP_SSLClient.

    ssl_client.setDebugLevel(1);
    stream_ssl_client1.setDebugLevel(1);
    // stream_ssl_client2.setDebugLevel(1);

    // In ESP32, when using WiFiClient with ESP_SSLClient, the WiFiClient was unable to detect
    // the server disconnection in case server session timed out and the TCP session was kept alive for reusage.
    // The TCP session timeout in seconds (>= 60 seconds) can be set via `ESP_SSLClient::setSessionTimeout`.
    ssl_client.setSessionTimeout(150);
    stream_ssl_client1.setSessionTimeout(150);
    // stream_ssl_client2.setSessionTimeout(150);
    Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
    Serial.println("Initializing app...");
    minHeapDuringAuth = ESP.getFreeHeap(); // Reset
    initializeApp(aClient, app, getAuth(user_auth), auth_debug_print, "🔐 authTask");

    // Or intialize the app and wait.
    // initializeApp(aClient, app, getAuth(user_auth), 120 * 1000, auth_debug_print);

    app.getApp<RealtimeDatabase>(Database);

    Database.url(DATABASE_URL);

    // In SSE mode (HTTP Streaming) task, you can filter the Stream events by using AsyncClientClass::setSSEFilters(<keywords>),
    // which the <keywords> is the comma separated events.
    // The event keywords supported are:
    // get - To allow the http get response (first put event since stream connected).
    // put - To allow the put event.
    // patch - To allow the patch event.
    // keep-alive - To allow the keep-alive event.
    // cancel - To allow the cancel event.
    // auth_revoked - To allow the auth_revoked event.
    // To clear all prevousely set filter to allow all Stream events, use AsyncClientClass::setSSEFilters().
    streamClient1.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
    
    // streamClient2.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");

    // The "unauthenticate" error can be occurred in this case because we don't wait
    // the app to be authenticated before connecting the stream.
    // This is ok as stream task will be reconnected automatically when the app is authenticated.

    // Database.get(streamClient1, examplePath1, processData, true /* SSE mode (HTTP Streaming) */, "streamTask1");

    // Database.get(streamClient2, examplePath2, processData, true /* SSE mode (HTTP Streaming) */, "streamTask2");

    // Async call with AsyncResult for returning result.
    // Database.get(streamClient1, "/examples/Stream/data1", streamResult1, true /* SSE mode (HTTP Streaming) */);
    // Database.get(streamClient2, "/examples/Stream/data2", streamResult2, true /* SSE mode (HTTP Streaming) */);
}

void loop()
{
    app.loop();

    if (app.ready())
    { 
        // Testirajte re-avtentikacijo po 2 minutah
        static unsigned long forceReauthAt = millis() + 120000;
        if (millis() > forceReauthAt) {
            Firebase.printf("🔄 FORCING RE-AUTH [Heap before: %d]\n", ESP.getFreeHeap());
            app.authenticate(); // Force library to re-authenticate (refresh the auth token).
            forceReauthAt = millis() + 120000; // Naslednji test čez 2 min
        }
        
        if (!extract_uid)
        {
          // Pridobimo UID
          strncpy(uid, app.getUid().c_str(), sizeof(uid) - 1);
          uid[sizeof(uid) - 1] = '\0'; // Zagotovimo null-terminacijo
          Firebase.printf("User UID: %s\n", uid);
          snprintf(databasePath, sizeof(databasePath), "/UserData/%s", uid);
          snprintf(examplesPath, sizeof(examplesPath), "%s/examples", databasePath);

          snprintf(examplePath1, sizeof(examplePath1), "%s/1", examplesPath);
          snprintf(examplePath2, sizeof(examplePath2), "%s/2", examplesPath);

           // VZPOSTAVITE STREAM ŠELE TUKAJ, ko imate pravilne poti
          Database.get(streamClient1, examplesPath, processData, true, "streamTask");
          //Database.get(streamClient2, examplePath2, processData, true, "streamTask2");
          Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
          extract_uid = true;
        }

        if (millis() - ms > 20000)
        {
          ms = millis();

          JsonWriter writer;

          object_t json, obj1, obj2;

          writer.create(obj1, "ms", ms);
          writer.create(obj2, "rand", random(10000, 30000));
          writer.join(json, 2, obj1, obj2);

          Database.set<object_t>(aClient, examplePath1, json, processData, "setTask1");

          Database.set<int>(aClient, examplePath2, random(100000, 200000), processData, "setTask2");
        }
    }

    // For async call with AsyncResult.
    // processData(streamResult1);
    // processData(streamResult2);
}

void processData(AsyncResult &aResult)
{
    uint32_t heap = ESP.getFreeHeap();
    
    if (heap < minHeapDuringAuth) {
        minHeapDuringAuth = heap;
    }

    if (!aResult.isResult())
        return;

    if (aResult.isEvent())
    {
        Firebase.printf("Event task: %s, msg: %s, code: %d [Heap: %d]\n", 
                        aResult.uid().c_str(), 
                        aResult.eventLog().message().c_str(), 
                        aResult.eventLog().code(),
                        heap);
    }

    if (aResult.isDebug())
    {
        Firebase.printf("Debug task: %s, msg: %s [Heap: %d]\n", 
                        aResult.uid().c_str(), 
                        aResult.debug().c_str(),
                        heap);
    }

    if (aResult.isError())
    {
        Firebase.printf("❌ Error task: %s, msg: %s, code: %d [Heap: %d, Min: %d]\n", 
                        aResult.uid().c_str(), 
                        aResult.error().message().c_str(), 
                        aResult.error().code(),
                        heap, minHeapDuringAuth);
    }

    if (aResult.available())
    {
        RealtimeDatabaseResult &stream = aResult.to<RealtimeDatabaseResult>();
        if (stream.isStream())
        {
            Serial.println("----------------------------");
            Firebase.printf("task: %s\n", aResult.uid().c_str());
            Firebase.printf("event: %s\n", stream.event().c_str());
            Firebase.printf("path: %s\n", stream.dataPath().c_str());
            Firebase.printf("data: %s\n", stream.to<const char *>());
            Firebase.printf("type: %d\n", stream.type());

            // The stream event from RealtimeDatabaseResult can be converted to the values as following.
            bool v1 = stream.to<bool>();
            int v2 = stream.to<int>();
            float v3 = stream.to<float>();
            double v4 = stream.to<double>();
            String v5 = stream.to<String>();
        }
        else
        {
            Serial.println("----------------------------");
            Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
        }
#if defined(ESP32) || defined(ESP8266)
        Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
        Firebase.printf("Free Heap: %d\n", rp2040.getFreeHeap());
#endif
    }
}
//ESP8266 Required LIBs
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <Hash.h>
//

//Sensor libraries
#include <DHT.h>
//

//Serial Extender Libraries
#include <Wire.h>
#include <Adafruit_ADS1015.h>
//

//WebSocket libraries
#include <AsyncDelay.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
//

//STATIC PIN DEFINITIONS (DONT CHANGE THESE)
#define LED D0            // Led in NodeMCU at pin GPIO16 (D0).
#define USE_SERIAL Serial


//Sensor Pin and Config Definitions
#define PUMP_PIN 14        // The pin that our pump is plugged into
#define DHTPIN 1          // Digital Pin our DHT Sensor is plugged into
#define DHTTYPE DHT11     // Change to DHT22 if youre using that
#define ANALOGPIN A0  // ESP8266 Analog Pin ADC0 = A0
#define A_LIGHT_SENSOR 1 // Analog pin for the photoresistor sensor
#define A_MOISTURE_SENSOR 0 // Analog pin for the moisture sensor
#define WIFI_SSID "The LAN Before Time"
#define WIFI_PASSWORD "Ub3rS3cr3t"
#define SERVER_ADDRESS "192.168.2.40"
#define SERVER_PORT 3000
//

AsyncDelay attempt_login;
AsyncDelay run_pump;
ESP8266WiFiMulti WiFiMulti;
SocketIOclient socketIO;
DHT dht(DHTPIN, DHTTYPE);
Adafruit_ADS1115 ads;
bool loggedIn = false;
String mac;
bool pumping = false;
void socketIOEvent(socketIOmessageType_t type, uint8_t * payload, size_t length) {
  //Switch based on socketio event type
  switch (type) {
    case sIOtype_DISCONNECT:
      USE_SERIAL.printf("[IOc] Disconnected!\n");
      loggedIn = false;
      digitalWrite(LED, HIGH);
      break;
    case sIOtype_CONNECT:
      USE_SERIAL.printf("[IOc] Connected to url: %s\n", payload);
      break;
    case sIOtype_EVENT: {
        //Execute function relating to events name
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload, length); //doc[0] is the name and doc[1] is the data sent
        String eventName = doc[0];
        if(eventName == "login-success")
          handleLoginSuccess(doc[1]);
        if(eventName == "water-plant")
          handleWaterPlant(doc[1]);
        if(eventName == "send-stats")
          sendStatsJSON();
        break;
      }
    case sIOtype_ACK:
      hexdump(payload, length);
      break;
    case sIOtype_ERROR:
      USE_SERIAL.printf("[IOc] get error: %u\n", length);
      hexdump(payload, length);
      break;
    case sIOtype_BINARY_EVENT:
      USE_SERIAL.printf("[IOc] get binary: %u\n", length);
      hexdump(payload, length);
      break;
    case sIOtype_BINARY_ACK:
      USE_SERIAL.printf("[IOc] get binary ack: %u\n", length);
      hexdump(payload, length);
      break;
  }
}

void setup() {
  pinMode(PUMP_PIN,OUTPUT);

  dht.begin(); // start our DHT sensor watchdog
  ads.begin(); // start our Analog expander

  mac = WiFi.macAddress(); // set our mac address for authentication later
  USE_SERIAL.begin(115200); // begin serial for debugging
  USE_SERIAL.setDebugOutput(true); // output default WIFI logs to serial
  pinMode(LED, OUTPUT);    // set LED pin as output.
  digitalWrite(PUMP_PIN, HIGH);

  USE_SERIAL.println();
  USE_SERIAL.println();
  USE_SERIAL.println();
  for (uint8_t t = 4; t > 0; t--) {
    USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
    USE_SERIAL.flush();
    delay(1000);
  }

  // disable AP mode
  if (WiFi.getMode() & WIFI_AP) {
    WiFi.softAPdisconnect(true);
  }

  WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  //WiFi.disconnect();
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(100);
  }

  String ip = WiFi.localIP().toString();
  USE_SERIAL.printf("[SETUP] WiFi Connected %s\n", ip.c_str());


  // server address, port and URL
  socketIO.begin(SERVER_ADDRESS, SERVER_PORT);

  // event handler
  socketIO.onEvent(socketIOEvent);

  attempt_login.start(5000, AsyncDelay::MILLIS); // start trying to login to the server


}

unsigned long messageTimestamp = 0;
void loop() {
  socketIO.loop(); // listen for events on the socket
  //Check if we should pulse LED
  if (!loggedIn && attempt_login.isExpired()) { // if we are to try logging in again
    digitalWrite(LED, HIGH);
    sendLogInRequest();
  }
  if(pumping && run_pump.isExpired()) {
     log("PUMP SHUTTING OFF");
     pumping = false;
     digitalWrite(PUMP_PIN, HIGH);
  }


}


void sendLogInRequest(){
    DynamicJsonDocument doc(1024);
    JsonArray loginarray = doc.to<JsonArray>();
    loginarray.add("login"); // set event type to login
    JsonObject login = loginarray.createNestedObject();
    login["serial"] = mac;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output); // send our newly composed JSON over the socket
    attempt_login.repeat(); // Count from when the delay expired, not now
}

//log method for debugging
void log(String data) {
    DynamicJsonDocument doc(1024);
    JsonArray loginarray = doc.to<JsonArray>();
    loginarray.add("log");
    JsonObject login = loginarray.createNestedObject();
    login["data"] = data;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);

}

// send our JSON stats
void sendStatsJSON() {
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.to<JsonArray>();
  array.add("stats");
  JsonObject stats = array.createNestedObject();
  stats["humidity"] = dht.readHumidity();
  stats["temperature"] = dht.readTemperature();
  stats["light"] = ads.readADC_SingleEnded(A_LIGHT_SENSOR);
  stats["soil_moisture"] = ads.readADC_SingleEnded(A_MOISTURE_SENSOR);
  String output;
  serializeJson(doc, output);
  socketIO.sendEVENT(output);
}

// When the socket sends a login success event
void handleLoginSuccess(String success){
  if(success == "true"){
    loggedIn = true;
    digitalWrite(LED, LOW); //turn the light on to indicate connection
  }
    
}

void handleWaterPlant(String time){
  pumping = true;
  digitalWrite(PUMP_PIN, LOW);
  run_pump.start(time.toInt() * 1000, AsyncDelay::MILLIS);
}

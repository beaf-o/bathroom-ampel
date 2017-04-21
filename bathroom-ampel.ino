#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Timer.h"
#include <WiFiManager.h>
#include <ArduinoOTA.h>

Timer t;

Adafruit_NeoPixel ring = Adafruit_NeoPixel(24, D4, NEO_GRB + NEO_KHZ800);

const PROGMEM char* CLIENT_ID = "bathroom-traffic";

// CREDENTIALS SECTION - START
const PROGMEM int OTA_PORT = 8266;
const PROGMEM char* DEFAULT_PW = "****";
const PROGMEM char* MQTT_SERVER_IP = "****";
const PROGMEM char* MQTT_FALLBACK_SERVER_IP = "****";
const PROGMEM uint16_t MQTT_SERVER_PORT = 1883;
const PROGMEM char* MQTT_USER = "****";
const PROGMEM char* MQTT_PASSWORD = "****";
// CREDENTIALS SECTION - END

String bathroomIp = "192.168.0.121";

const PROGMEM char* LIGHT_COMMAND_TOPIC = "home-assistant/bathroom/traffic/set";
const PROGMEM char* LIGHT_STATE_TOPIC = "home-assistant/bathroom/traffic/status";
const PROGMEM char* NIGHT_MODE_TOPIC = "home-assistant/nightmode";
const PROGMEM char* PANIC_TOPIC = "home-assistant/panic";

const PROGMEM char* ESP_BATHROOM_STATE_TOPIC = "home-assistant/esp/bathroom/status";
const PROGMEM char* ESP_BATHROOM_IP_TOPIC = "home-assistant/esp/bathroom/ip";

const PROGMEM char* ESP_STATE_TOPIC = "home-assistant/esp/traffic/status";
const PROGMEM char* ESP_IP_TOPIC = "home-assistant/esp/traffic/ip";

const PROGMEM char* SPOTS_STATE_TOPIC = "home-assistant/bathroom/spots/status";
const PROGMEM char* STRIPES_STATE_TOPIC = "home-assistant/bathroom/stripes/status";

const uint8_t MSG_BUFFER_SIZE = 20;
char m_msg_buffer[MSG_BUFFER_SIZE]; 

const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);

const PROGMEM char* LIGHT_ON = "ON";
const PROGMEM char* LIGHT_OFF = "OFF";

const PROGMEM char* NIGHT_MODE_ON = "ON";
const PROGMEM char* NIGHT_MODE_OFF = "OFF";

const PROGMEM uint8_t DEFAULT_BRIGHTNESS = 50;

byte red = 255;
byte green = 43;
byte blue = 0;
byte brightness = DEFAULT_BRIGHTNESS;

boolean occupied;
int occupiedCount = 0;
int freeCount = 0;
boolean nightMode = false;
boolean panicMode = false;
boolean trafficLightState = false;

boolean isInitial = true;
int wait = 5;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void setup() {
  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  Serial.println();
  setupWifi();
  setupRing();
  setupMqtt();
  setupOTA();
  setupTimer();
}

void setupWifi() {
  delay(10);
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);

  if (!wifiManager.autoConnect(CLIENT_ID, DEFAULT_PW)) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
}

void setupRing() {
  Serial.println("Setup ring");
  ring.begin();
  ring.setBrightness(brightness);
  
  for (int i = 0; i < 5; i++) {
    around(ring.Color(red, green, blue));
  }

  setColor();
}

void setupMqtt() {
  Serial.println("Setup mqtt");
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  client.setCallback(callback);
}

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // concat the payload into a string
  String payload;
  for (uint8_t i = 0; i < p_length; i++) {
    payload.concat((char)p_payload[i]);
  }

  Serial.print("Handle topic: \"");
  Serial.print(p_topic);
  Serial.print("\" with payload: \"");
  Serial.print(payload);
  Serial.println("\"");

  if (String(PANIC_TOPIC).equals(p_topic)) {
    handlePanicTopic(payload);
  } else if (String(NIGHT_MODE_TOPIC).equals(p_topic)) {
    if (payload.equals(String(NIGHT_MODE_ON))) {
      Serial.println("Night mode switched on!");
      // nightMode = true;
      // trafficLightState = false;
      //black();
    } else if (payload.equals(String(NIGHT_MODE_OFF))) {
      nightMode = false;
      trafficLightState = true;
    }
  } else if (String(ESP_BATHROOM_IP_TOPIC).equals(p_topic)) {
    bathroomIp = (String) payload;
  } else if (String(LIGHT_COMMAND_TOPIC).equals(p_topic)) {
    // test if the payload is equal to "ON" or "OFF"
    char message[p_length + 1];
    for (uint8_t i = 0; i < p_length; i++) {
      message[i] = (char)payload[i];
    }
    message[p_length] = '\0';
  
    if (!processJson(message)) {
      return;
    }

    if (trafficLightState == false) {
      // Update lights
      red = 0;
      green = 0;
      blue = 0;
    }

    setColor();
    publishTrafficLightState();
    occupiedCount = 0;
    freeCount = 0;

    if (isInitial == false) {
      Serial.print("keep color for ");
      Serial.print((String) wait);
      Serial.println("s");
      for (int w = 0; w < wait; w++) {
        Serial.print(".");
        delay(1000);
      }
      Serial.println();  
    }
  }
}

void setupOTA() {
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(CLIENT_ID);
  ArduinoOTA.setPassword(DEFAULT_PW);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA");
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
}

bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      trafficLightState = true;
    } else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      trafficLightState = false;
    }
  }

  if (root.containsKey("color")) {
      red = root["color"]["r"];
      green = root["color"]["g"];
      blue = root["color"]["b"];
    }

    if (root.containsKey("brightness")) {
      brightness = root["brightness"];
    }

  return true;
}

String IpAddress2String(const IPAddress& ipAddress){
  return String(ipAddress[0]) + String(".") +\
    String(ipAddress[1]) + String(".") +\
    String(ipAddress[2]) + String(".") +\
    String(ipAddress[3]); 
}

void setupTimer() {
  t.every(5000, checkBathroom);
}

void handlePanicTopic(String payload) {
  if (payload.equals("ON")) {
    Serial.println("----------------------------------------");
    Serial.println("-----          PANIC MODE          -----");
    Serial.println("----------------------------------------");

    panicMode = true;
  } else if (panicMode != false && payload.equals("OFF")) {
    panicMode = false;
    Serial.println("Set panic mode = false");
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  if (isInitial) {
    IPAddress ipAddress = WiFi.localIP();
    String ipString = IpAddress2String(ipAddress);
      
    client.publish(ESP_STATE_TOPIC, "online", true);
    client.publish(ESP_IP_TOPIC, ipString.c_str(), true);
  }

  if (panicMode == true) {
    blinkRed();
  }

  updateCounts();

  t.update();

  client.loop();

  isInitial = false;
}

void updateCounts() {
  if (occupiedCount > 10) {
    occupiedCount = 0;
  }

  if (freeCount > 10) {
    freeCount = 0;
  }
}

void checkBathroom() {
  if (nightMode == true) {
    return;
  }

  //brightness = DEFAULT_BRIGHTNESS;

  String endpoint = "http://" + bathroomIp + "/";
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(endpoint);
  int httpCode = http.GET();
  Serial.println("[HTTP] GET " + endpoint);

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      //Serial.println(payload);
      showRed();
    }
  } else {
    Serial.printf("[HTTP] Failed; error: %s\n", http.errorToString(httpCode).c_str());
    showGreen();
  }

  http.end();
}

void black() {
  Serial.println("Turn off traffic lights");
  red = 0;
  green = 0;
  blue = 0;
  
  setColor();
  publishTrafficLightState();
}

void showRed() {
  if (nightMode == true) {
    return;
  }

  if (occupied == false) {
    occupiedCount = 0;
  }

  red = 255;
  green = 0;
  blue = 0;
  
  if (occupiedCount == 0) {
    around(ring.Color(red, green, blue));
    around(ring.Color(red, green, blue));
  }

  setColor();
  occupiedCount++;

  if (occupied == true) {
    return;
  }

  Serial.println("Show red");
  publishTrafficLightState();
  occupied = true;
}

void blinkRed() {
  byte currentRed = red;
  byte currentGreen = green;
  byte currentBlue = blue;
  byte currentBrightness = brightness;
  
  green = 0;
  blue = 0;
  brightness = 100;
    
  for (int br = 0; br < 100; br++) {
    red = 255;
    setColor();

    delay(500);
  
    red = 0;
    setColor();

    delay(500);
  }

  red = currentRed;
  green = currentGreen;
  blue = currentBlue;
  brightness = currentBrightness;
  setColor();
}

void showGreen() {
  if (nightMode == true) {
    return;
  }

  if (occupied == true) {
    freeCount = 0;
  }

  red = 0;
  green = 255;
  blue = 0;
  
  if (freeCount == 0) {
    around(ring.Color(red, green, blue));
    around(ring.Color(red, green, blue));
  }

  setColor();
  freeCount++;

  if (occupied == false) {
    return;
  }

  Serial.println("Show green");
  occupied = false;
  publishTrafficLightState();

  publishBathroomOfflineStates();
}

void publishBathroomOfflineStates() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = "OFF";
  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(STRIPES_STATE_TOPIC, buffer, true);
  client.publish(ESP_BATHROOM_STATE_TOPIC, "offline", true);
  client.publish(SPOTS_STATE_TOPIC, "OFF", true);
}

void setColor() {
  ring.setBrightness(brightness);
  for (int i = 0; i < ring.numPixels(); i++) {
    ring.setPixelColor(i, ring.Color(red, green, blue));
  }

  ring.show();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("INFO: Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("INFO: connected");
      client.subscribe(PANIC_TOPIC);
      client.subscribe(LIGHT_COMMAND_TOPIC);
      client.subscribe(NIGHT_MODE_TOPIC);
      client.subscribe(ESP_BATHROOM_IP_TOPIC);
    } else {
      Serial.print("ERROR: failed, rc=");
      Serial.println(client.state());
      Serial.println("DEBUG: try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void publishTrafficLightState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (nightMode) ? LIGHT_OFF : LIGHT_ON;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;

  root["brightness"] = brightness;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(LIGHT_STATE_TOPIC, buffer, true);
}

void around(uint32_t color) {
  int aroundDelay = 20;
  int t;
  for (int i = 0; i < ring.numPixels(); i++) {
    ring.setPixelColor(i, color);
    t = i - 1;
    if (t < 0) {
      t = ring.numPixels() - 1;
    }

    ring.setPixelColor(t, ring.Color( 0, 0, 0));
    ring.show();
    delay(aroundDelay);
  }

  return;

  for (int i = ring.numPixels() - 1; i >= 0; i--) {
    ring.setPixelColor(i, color);
    t = i + 1;
    if (t >= ring.numPixels()) {
      t = 0;
    }

    ring.setPixelColor(t, ring.Color( 0, 0, 0));
    ring.show();
    delay(aroundDelay);
  }
}

/**************************************************************************************
   Sketch to blink RGB LED strip in Finnish pre-christmas party called Pikkujoulut.
   ESP8266 connects to a MQTT broker and executes lightning effects base on commands from the broker.
   Copyright 2018-2019 Aapo Rista / Vekotinverstas / Forum Virium Helsinki Oy
   MIT license

  NOTE
  You must install libraries below using Arduino IDE's
  Sketch --> Include Library --> Manage Libraries... command

   PubSubClient (version >= 2.6.0 by Nick O'Leary)
   ArduinoJson (version > 5.13 < 6.0 by Benoit Blanchon)
   WiFiManager (version >= 0.14.0 by tzapu)

 **************************************************************************************/

#include "settings.h"             // Remember to copy settings-example.h to settings.h and check all values!
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#ifndef FastLED
#include <FastLED.h>
#endif


// I2C settings
// #define SDA     D2
// #define SCL     D1

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = MQTT_SERVER;
char mqtt_port[6] = MQTT_PORT;
char mqtt_user[34] = MQTT_USER;
char mqtt_password[34] = MQTT_PASSWORD;
char room_token[34] = ROOM_TOKEN;

// Move to settings, perhaps?
CRGBPalette16 currentPalette;
TBlendType    currentBlending;
uint8_t currentMode = '0';
uint8_t activeEffect = '0';
uint8_t brightness = BRIGHTNESS;
static uint8_t startIndex = 0;
uint8_t colorIndex = 0;

uint8_t r = 0;
uint8_t g = 0;
uint8_t b = 0;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message (");
  Serial.print(length);
  Serial.print("B):");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if (payload[0] == '0') {
    Serial.println("Protocol version was 0 as expected");
  }
  // Check that mode is valid
  if ((payload[1] < '0') || (payload[1] > '2')) {
    Serial.print("Invalid mode: ");
    Serial.println(payload[1]);
    return;
  }
  currentMode = payload[1];

  if (payload[0] == '0') {
    Serial.println("Protocol version was 0 as expected");
  }

  switch (currentMode) {
    case '0':
      Serial.println("Mode 0: predefined palette effect");
      switchMode(payload, length);
      break;
    case '1':
      Serial.println("Mode 1: Solid color");
      setSolidColor(payload, length);
      break;
    case '2':
      Serial.println("Mode 2: Active effect");
      setActiveEffect(payload, length);
      break;
    default:
      Serial.println("Unknown mode");
      break;
  }    

  Serial.println("-----------------------");
}

// Define and set up all variables / objects
WiFiClient wifiClient;
WiFiManager wifiManager;
PubSubClient client(MQTT_SERVER, 1883, callback, wifiClient);
String mac_str;
byte mac[6];
char macAddr[13];
unsigned long lastPing = 0;
char pingTopic[50];
char controlTopic[50];
char controlTopicBrc[50];

/* Sensor variables */

void MqttSetup() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi so no MQTT. Continuing.");
    return;
  }
  // Generate client name based on MAC address and last 8 bits of microsecond counter
  String clientName;
  clientName += "esp8266-";
  clientName += mac_str;
  clientName += "-";
  clientName += String(micros() & 0xff, 16);

  Serial.print("Connecting to ");
  Serial.print(MQTT_SERVER);
  Serial.print(" as ");
  Serial.println(clientName);
  client.setCallback(callback);

  if (client.connect((char*) clientName.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.println("Connected to MQTT broker");
    Serial.print("Publish topic is: ");
    Serial.println(pingTopic);
    Serial.println("Subscribe topics are: ");
    Serial.println(controlTopic);
    Serial.println(controlTopicBrc);
    SendPingToMQTT();
    client.subscribe(controlTopic);
    client.subscribe(controlTopicBrc);
  }
  else {
    Serial.println("MQTT connect failed");
    Serial.println("Will reset and try again...");
    delay(5000);
    // TODO: quit connecting after e.g. 20 seconds to enable standalone usage
    // abort();
  }
}

void setup() {
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 32);
  WiFiManagerParameter custom_room_token("token", "room token", room_token, 32);
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_room_token);
  // wifiManager.resetSettings();
  mac_str = WiFi.macAddress();
  WiFi.macAddress(mac);
  // Wire.begin(SDA, SCL);
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(room_token, custom_room_token.getValue());
  Serial.println(mqtt_server);
  Serial.println(mqtt_port);
  Serial.println(mqtt_user);
  Serial.println(mqtt_password);
  Serial.println(room_token);
  sprintf(macAddr, "%2X%2X%2X%2X%2X%2X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (strlen(room_token) == 0) {
    sprintf(pingTopic, "%s/%s", MQTT_PUB_TOPIC, macAddr);
    sprintf(controlTopic, "%s/%s", MQTT_SUB_TOPIC, macAddr);
    sprintf(controlTopicBrc, "%s/%s", MQTT_SUB_TOPIC, macAddr);
  } else {
    sprintf(pingTopic, "%s/%s/%s", MQTT_PUB_TOPIC, room_token, macAddr);
    sprintf(controlTopic, "%s/%s/%s", MQTT_SUB_TOPIC, room_token, macAddr);
    sprintf(controlTopicBrc, "%s/%s", MQTT_SUB_TOPIC, room_token);
  }
  char ap_name[30];
  sprintf(ap_name, "%s_%s", AP_NAME, macAddr);
  Serial.print("AP name would be: ");
  Serial.println(ap_name);
  wifiManager.autoConnect(ap_name);

  MqttSetup();
  Serial.println("Init FastLED");
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  // FastLED.addLeds<LED_TYPE, LED_PIN, CLK_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );

  FastLED.setBrightness(  BRIGHTNESS );

  currentPalette = RainbowColors_p;
  currentBlending = LINEARBLEND;

}

void loop() {
  unsigned long now = millis();
  if (!client.loop()) {
    Serial.print("Client disconnected...");
    // TODO: increase reconnect from every loop() to every 60 sec or so
    MqttSetup();
    return;
  }
  if (lastPing + 10000 < now) {
    lastPing = now;
    SendPingToMQTT();
  }
  runLedEffect();
  FastLED.show();
  FastLED.delay(1000 / UPDATES_PER_SECOND);
}

/**
   Mode is switched always when a valid MQTT message is received
*/
void switchMode(byte* payload, unsigned int length) {
  colorIndex = 0;
  // There are several different palettes of colors demonstrated here.
  //
  // FastLED provides several 'preset' palettes: RainbowColors_p, RainbowStripeColors_p,
  // OceanColors_p, CloudColors_p, LavaColors_p, ForestColors_p, and PartyColors_p.
  switch (payload[2]) {
    case '0':
      Serial.println("Switch to RainbowColors_p");
      currentPalette = RainbowColors_p;
      break;
    case '1':
      Serial.println("Switch to RainbowStripeColors_p");
      currentPalette = RainbowStripeColors_p;
      break;
    case '2':
      Serial.println("Switch to OceanColors_p");
      currentPalette = OceanColors_p;
      break;
    case '3':
      Serial.println("Switch to CloudColors_p");
      currentPalette = CloudColors_p;
      break;
    case '4':
      Serial.println("Switch to LavaColors_p");
      currentPalette = LavaColors_p;
      break;
    case '5':
      Serial.println("Switch to ForestColors_p");
      currentPalette = ForestColors_p;
      break;
    case '6':
      Serial.println("Switch to PartyColors_p");
      currentPalette = PartyColors_p;
      break;
    default:
      Serial.print("Invalid palette: ");
      Serial.println(payload[2]);
      break;
  }
}

/**
   Mode is switched always when a valid MQTT message is received
*/
void setSolidColor(byte* payload, unsigned int length) {
  r = payload[2];
  g = payload[3];
  b = payload[4];
  Serial.println(r);
  Serial.println(g);
  Serial.println(b);
}

void setActiveEffect(byte* payload, unsigned int length) {
  if ((payload[2] >= '0') && (payload[2] <= '2')) {
    activeEffect = payload[2];
    Serial.println("activeEffect set");
  } else {
    Serial.print("Invalid effect: ");
    Serial.println(payload[2]);
  }
}

void FillLEDsFromPaletteColors(uint8_t colorIndex) {
  for ( int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette( currentPalette, colorIndex, brightness, currentBlending);
    colorIndex += 3;
  }
}


void FillLEDsWithSolidColor() {
  for ( int i = 0; i < NUM_LEDS; i++) {
    leds[i].setRGB( r, g, b);
  }
}

void runActiveEffect() {
  switch (activeEffect) {
    case '0':
      // Serial.println("Switch to Cylon");
      Fire2012(); // run simulation frame
      break;
    case '1':
      // Serial.println("Switch to Fire");
      Fire2012(); // run simulation frame
      break;
    default:
      // Serial.print("Invalid effect: ");
      // Serial.println(activeEffect);
      break;
  }
  
}

bool gReverseDirection = false;




// Fire2012 by Mark Kriegsman, July 2012
// as part of "Five Elements" shown here: http://youtu.be/knWiGsmgycY
//// 
// This basic one-dimensional 'fire' simulation works roughly as follows:
// There's a underlying array of 'heat' cells, that model the temperature
// at each point along the line.  Every cycle through the simulation, 
// four steps are performed:
//  1) All cells cool down a little bit, losing heat to the air
//  2) The heat from each cell drifts 'up' and diffuses a little
//  3) Sometimes randomly new 'sparks' of heat are added at the bottom
//  4) The heat from each cell is rendered as a color into the leds array
//     The heat-to-color mapping uses a black-body radiation approximation.
//
// Temperature is in arbitrary units from 0 (cold black) to 255 (white hot).
//
// This simulation scales it self a bit depending on NUM_LEDS; it should look
// "OK" on anywhere from 20 to 100 LEDs without too much tweaking. 
//
// I recommend running this simulation at anywhere from 30-100 frames per second,
// meaning an interframe delay of about 10-35 milliseconds.
//
// Looks best on a high-density LED setup (60+ pixels/meter).
//
//
// There are two main parameters you can play with to control the look and
// feel of your fire: COOLING (used in step 1 above), and SPARKING (used
// in step 3 above).
//
// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100 
#define COOLING  55

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 120


void Fire2012()
{
// Array of temperature readings at each simulation cell
  static byte heat[NUM_LEDS];

  // Step 1.  Cool down every cell a little
    for( int i = 0; i < NUM_LEDS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }
  
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= NUM_LEDS - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }
    
    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < NUM_LEDS; j++) {
      CRGB color = HeatColor( heat[j]);
      int pixelnumber;
      if( gReverseDirection ) {
        pixelnumber = (NUM_LEDS-1) - j;
      } else {
        pixelnumber = j;
      }
      leds[pixelnumber] = color;
    }
}


void runLedEffect() {
  switch (currentMode) {
    case '0':
      static uint8_t startIndex = 0;
      startIndex = startIndex + 1; /* motion speed */
      FillLEDsFromPaletteColors(startIndex);
      break;
    case '1':
      FillLEDsWithSolidColor();
      break;      
    case '2':
      runActiveEffect();
      break;      
    default:
      Serial.println("Got invalid mode (in runLedEffect)");
      break;

  }
}

void SendPingToMQTT() {
  char cstr[64];
  snprintf(cstr, sizeof(cstr), "%d,%c,%d,%d", NUM_LEDS, currentMode, brightness, millis());
  Serial.print(pingTopic);
  Serial.print(" ");
  Serial.println(cstr);
  client.publish(pingTopic, cstr);
}

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "max6675.h"
#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino_JSON.h>
#define FORMAT_LITTLEFS_IF_FAILED true
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>

const char* ssid = "Your Network SSID Here";
const char* password = "Your Network Password Here";
const char* mqtt_server = "mqtt_ip";
const char* mqtt_username = "mqtt_username";
const char* mqtt_password = "mqtt_password";

WiFiClient espClient;
PubSubClient client(espClient);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

JSONVar readings;
JSONVar configs;

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 10000;

// thermo_0 setup
int thermoDO_0 = 19; // SO
int thermoCS_0 = 18; // CS
int thermoCLK_0 = 5; // SCK

// thermo_1 setu
int thermoDO_1 = 4; // SO
int thermoCS_1 = 2; // CS
int thermoCLK_1 = 15; // SCK

float thermoFahrenheit_0, thermoFahrenheit_1;

// pwm setup
const int pwmPin = 26; // PWM control pin
const int tachPin = 27; // RPM feedback pin

const int freq = 25000; // 25 kHz PWM frequency
const int ledChannel = 0; // Use LED channel 0
const int resolution = 8; // 8-bit resolution

// mosfet gate to toggle fan ground
const int MOSFET_GATE = 14;

MAX6675 thermocouple_0(thermoCLK_0, thermoCS_0, thermoDO_0);
MAX6675 thermocouple_1(thermoCLK_1, thermoCS_1, thermoDO_1);

Preferences preferences;

const int thermAdjDef_0 = 0; // -153;
const int thermAdjDef_1 = 0; // -145;

const char* polyKey_0 = "polyKey_0";
const char* polyKey_1 = "polyKey_1";

const char* temperature_0 = "temperature_0";
const char* temperature_1 = "temperature_1";

const char* thermo_0_adjustment = "tempAdjust_0";
const char* tempPolyCorrection_0 = "tempPolyCorrection_0";
const char* thermo_1_adjustment = "tempAdjust_1";
const char* tempPolyCorrection_1 = "tempPolyCorrection_1";

// get groups of readings for smoothing averages
unsigned long previousMillis = 0;  // Stores the last time readings were processed
const long processInterval = 30000;  // Interval to process readings (milliseconds, 10 seconds)
const int maxReadings = 30;  // Maximum number of readings to store for each thermocouple
float readings0[maxReadings];  // Array to store readings from thermocouple 1 as floats
float readings1[maxReadings];  // Array to store readings from thermocouple 2 as floats
int readingIndex0 = 0;  // Index for the next reading from thermocouple 1
int readingIndex1 = 0;  // Index for the next reading from thermocouple 2
unsigned long lastReadingMillis = 0; // Last time a reading was taken
const long readingInterval = 1000; // Interval to take readings (milliseconds, 1 second)

// Global variables to track the initialization period
bool isInitialized0 = false;
bool isInitialized1 = false;

int ignoredCount0 = 0; // Count of ignored initial readings for thermocouple 0
int ignoredCount1 = 0; // Count of ignored initial readings for thermocouple 1
const int maxIgnoredCount = 2; // Maximum initial readings to ignore for stabilization

// Moving average filter setup for thermocouple 0
const int filterWindowSize = 10; // Adjusted window size for more smoothing
float filterWindow0[filterWindowSize] = {0.0}; // Initialize all to zero
int filterIndex0 = 0;
bool filterInitialized0 = false;

// Moving average filter setup for thermocouple 1
float filterWindow1[filterWindowSize] = {0.0}; // Initialize all to zero
int filterIndex1 = 0;
bool filterInitialized1 = false;

// Function prototype
float applyMovingAverageFilter(float newReading, float filterWindow[], int &filterIndex, bool &filterInitialized);

// Coefficients for the polynomial correction for the first thermometer
float a0 = 0; // Coefficient for x^2
float b0 = 0;    // Coefficient for x
float c0 = 0; // Constant term

// Coefficients for the polynomial correction for the second thermometer
float a1 = 0; // Coefficient for x^2
float b1 = 0;    // Coefficient for x
float c1 = 0; // Constant term (change this only for simple +/- adjustment )

void setup() {
  Serial.begin(9600);

  Serial.println("Starting setup()");
  delay(1000);

  // Initialize readings arrays
  for(int i = 0; i < maxReadings; i++) {
    readings0[i] = 0.0;
    readings1[i] = 0.0;
  }

  initLittleFS();
  delay(3000);

  preferences.begin("thermo", false);
  float storedValue_a0 = preferences.getFloat("a0", 0.0);
  float storedValue_b0 = preferences.getFloat("b0", 0.0);
  float storedValue_c0 = preferences.getFloat("c0", 0.0);
  float storedValue_a1 = preferences.getFloat("a1", 0.0);
  float storedValue_b1 = preferences.getFloat("b1", 0.0);
  float storedValue_c1 = preferences.getFloat("c1", 0.0); 
  preferences.end(); 

  a0 = storedValue_a0;
  b0 = storedValue_b0;
  c0 = storedValue_c0;
  a1 = storedValue_a1;
  b1 = storedValue_b1;
  c1 = storedValue_c1;

  Serial.println("Stored Preferences:");

  Serial.print("a0: ");
  Serial.println(storedValue_a0, 20);
  Serial.print("b0: ");
  Serial.println(storedValue_b0, 20);
  Serial.print("c0: ");
  Serial.println(storedValue_c0, 20);
  Serial.print("a1: ");
  Serial.println(storedValue_a1, 20);
  Serial.print("b1: ");
  Serial.println(storedValue_b1, 20);
  Serial.print("c1: "); 
  Serial.println(storedValue_c1, 20);

  // Setup LEDC for PWM on pwmPin
  ledcSetup(ledChannel, freq, resolution);
  ledcAttachPin(pwmPin, ledChannel);
  pinMode(tachPin, INPUT_PULLUP); // Configure tachPin as input with internal pull-up resistor
  pinMode(MOSFET_GATE, OUTPUT);
  digitalWrite(MOSFET_GATE, LOW); // Turn Fan Off
  initWiFi();

  delay(1000);

  client.setServer(mqtt_server, 1883);
  client.setBufferSize(512);

  delay(1000);
  initHomeAssistantDiscovery();

  delay(1000);

  initWebSocket();

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "This is a test");
  });

  server.serveStatic("/", LittleFS, "/");
  server.begin();
}

void reconnect() {
  while (!client.connected()) {
    // Use the connect function with username and password
    if (client.connect("ESP32Client", mqtt_username, mqtt_password)) {
      // If you need to subscribe to topics upon connection, do it here
    } else {
      // If the connection fails, wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void initHomeAssistantDiscovery() {
    if (!client.connected()) {
        reconnect(); // Your function to reconnect to the MQTT broker
    }

    Serial.println("Starting HA Discovery...");

    // Temperature 0
    StaticJsonDocument<512> discoveryDoc0;
    discoveryDoc0["name"] = "PitmasterIOT Temperature Sensor 0";
    discoveryDoc0["state_topic"] = "home/pitmasteriot/temperature";
    discoveryDoc0["unit_of_measurement"] = "°F"; // or "\u00B0F"
    discoveryDoc0["value_template"] = "{{ value_json.temperature_0 }}";
    discoveryDoc0["device_class"] = "temperature";
    discoveryDoc0["unique_id"] = "pitmasteriot_temperature_sensor_0_unique_id";

    String discoveryMessage0;
    serializeJson(discoveryDoc0, discoveryMessage0);
    client.publish("homeassistant/sensor/pitmasteriot/temperature_0/config", discoveryMessage0.c_str(), true);

    // Temperature 1
    StaticJsonDocument<512> discoveryDoc1;
    discoveryDoc1["name"] = "PitmasterIOT Temperature Sensor 1";
    discoveryDoc1["state_topic"] = "home/pitmasteriot/temperature";
    discoveryDoc1["unit_of_measurement"] = "°F"; // or "\u00B0F"
    discoveryDoc1["value_template"] = "{{ value_json.temperature_1 }}";
    discoveryDoc1["device_class"] = "temperature";
    discoveryDoc1["unique_id"] = "pitmasteriot_temperature_sensor_1_unique_id";

    String discoveryMessage1;
    serializeJson(discoveryDoc1, discoveryMessage1);
    client.publish("homeassistant/sensor/pitmasteriot/temperature_1/config", discoveryMessage1.c_str(), true);

    Serial.println("Finished HA Discovery");
}

float applyMovingAverageFilter(float newReading, float filterWindow[], int &filterIndex, bool &filterInitialized) {
    filterWindow[filterIndex++] = newReading;
    if (filterIndex >= filterWindowSize) {
        filterIndex = 0; // Wrap index
        filterInitialized = true;
    }
    
    if (!filterInitialized) return newReading; // Not enough data for filtering

    float sum = 0.0;
    for (int i = 0; i < filterWindowSize; i++) {
        sum += filterWindow[i];
    }
    return sum / filterWindowSize;
}

void collectReadings() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReadingMillis >= readingInterval) {
        lastReadingMillis = currentMillis;

        // For thermocouple 0
        float rawReading0 = thermocouple_0.readFahrenheit(); // Assume this could be NaN
        if (!isnan(rawReading0)) { // Check if the reading is valid
            float filteredReading0 = applyMovingAverageFilter(rawReading0, filterWindow0, filterIndex0, filterInitialized0);
            readings0[readingIndex0++] = filteredReading0;
        }
        readingIndex0 %= maxReadings;

        // For thermocouple 1
        float rawReading1 = thermocouple_1.readFahrenheit(); // Assume this could be NaN
        if (!isnan(rawReading1)) { // Check if the reading is valid
            float filteredReading1 = applyMovingAverageFilter(rawReading1, filterWindow1, filterIndex1, filterInitialized1);
            readings1[readingIndex1++] = filteredReading1;
        }
        readingIndex1 %= maxReadings;
    }
}

void processReadings() {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= processInterval) {
        previousMillis = currentMillis;

        Serial.println("Raw Temperatures:");
        Serial.println(thermocouple_0.readFahrenheit());
        Serial.println(thermocouple_1.readFahrenheit());

        float correctedThermoFahrenheit_0 = NAN;
        float correctedThermoFahrenheit_1 = NAN;

        // Check and process readings for each thermocouple independently
        if (isInitialized0 || ignoredCount0++ >= maxIgnoredCount) {
            isInitialized0 = true;
            correctedThermoFahrenheit_0 = processThermocoupleReadings(readings0, "Thermocouple 0", a0, b0, c0);
            readingIndex0 = 0; // Reset index for new cycle
        }
        
        if (isInitialized1 || ignoredCount1++ >= maxIgnoredCount) {
            isInitialized1 = true;
            correctedThermoFahrenheit_1 = processThermocoupleReadings(readings1, "Thermocouple 1", a1, b1, c1);
            readingIndex1 = 0; // Reset index for new cycle
        }

        if (!isnan(correctedThermoFahrenheit_0) && !isnan(correctedThermoFahrenheit_1)) {
          Serial.println("Polynomial Corrections:");
          Serial.println(correctedThermoFahrenheit_0, 1);
          Serial.println(correctedThermoFahrenheit_1, 1);

          // Use a JSON document to structure the data
          StaticJsonDocument<256> doc;
          doc["temperature_0"] = correctedThermoFahrenheit_0;
          doc["temperature_1"] = correctedThermoFahrenheit_1;

          // Serialize the JSON document to a String
          String jsonString;
          serializeJson(doc, jsonString);
          notifyClients(jsonString); // Assuming this notifies local clients/subscribers

          // Prepare and publish the temperature_0 reading
          StaticJsonDocument<128> doc0;
          doc0["temperature_0"] = correctedThermoFahrenheit_0;
          String jsonString0;
          serializeJson(doc0, jsonString0);
          client.publish("home/pitmasteriot/temperature", jsonString0.c_str());

          // Prepare and publish the temperature_1 reading
          StaticJsonDocument<128> doc1;
          doc1["temperature_1"] = correctedThermoFahrenheit_1;
          String jsonString1;
          serializeJson(doc1, jsonString1);
          client.publish("home/pitmasteriot/temperature", jsonString1.c_str());
      }
        
    }
}

float processThermocoupleReadings(float readings[], const char* thermocoupleName, float a, float b, float c) {
    float sum = 0.0;
    int validCount = 0;
    for (int i = 0; i < maxReadings; i++) {
        if (!isnan(readings[i])) { // Ensure only valid readings are considered
            sum += readings[i];
            validCount++;
        }
    }
    if (validCount > 0) { // Proceed only if there are valid readings
        float averageReading = sum / validCount;
        float correctedTemperature = applyPolyCorrection(averageReading, a, b, c);
        Serial.print(thermocoupleName);
        Serial.print(" Corrected Temperature: ");
        Serial.println(correctedTemperature, 1);
        return correctedTemperature;
    } else {
        Serial.print(thermocoupleName);
        Serial.println(" - No valid readings available.");
        return NAN;
    }
}

// Function to apply the polynomial correction
float applyPolyCorrection(float rawTemp, float a, float b, float c) {
    // Check if all coefficients are zero
    if (a == 0 && b == 0 && c == 0) {
        // Return the raw temperature if no correction is desired
        //Serial.println("return raw...");
        return rawTemp;
    }

    //Serial.println("apply poly correction...");
    // Otherwise, apply the polynomial correction
    return a * rawTemp * rawTemp + b * rawTemp + c;
}

void initLittleFS(){
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
        Serial.println("LittleFS Mount Failed");
        return;
    }
    else{
        Serial.println("Little FS Mounted Successfully");
    }
}

void initWiFi() {

  // Optional: Set your Static IP address
  IPAddress local_IP(192, 168, 86, 184);

  // Optional: Set your Gateway IP address
  IPAddress gateway(192, 168, 86, 1);
  IPAddress subnet(255, 255, 255, 0);

  if(!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("bootleg_bbq_blower");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println(WiFi.localIP());
}

void notifyClients(String payload) {
  ws.textAll(payload);
}

void SendClientsConfiguration(){
  String jsonString = JSON.stringify(configs);
  notifyClients(jsonString);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    String message = String((char*)data);
    JSONVar jsonData = JSON.parse(message);

    if (jsonData.hasOwnProperty("event")) {
      String event = (const char*)jsonData["event"];
      Serial.print("event: ");
      Serial.println(event);

      if (event == "setFanSpeed" && jsonData.hasOwnProperty("speed")) {
        int newFanSpeed = (int)jsonData["speed"];
        Serial.print("Received setFanSpeed event. New fan speed: ");
        Serial.println(newFanSpeed);

        if(newFanSpeed == 0){
          // turn off fan
          digitalWrite(MOSFET_GATE, LOW); 
        }
        else{
          // turn on fan
          digitalWrite(MOSFET_GATE, HIGH); 
          setFanSpeed(newFanSpeed);
        }

        readings["fan_speed"] = newFanSpeed;
        String jsonString = JSON.stringify(readings);
        notifyClients(jsonString);
      }
      else if(event == "updateConfiguration"){ 

        // json payload example: {"tempPolyCorrection_0":"-0.0050831897503855195, 6.007914850092533, -1058.2550056688106","tempPolyCorrection_1":"-0.0050831897503855195, 6.007914850092533, -1058.2550056688107"}
        String jsonString = JSON.stringify(jsonData["configuration"]);
        Serial.println(jsonString);

        DynamicJsonDocument doc(256);

        // Deserialize the JSON string into the JsonDocument
        DeserializationError error = deserializeJson(doc, jsonString);

        if (!error) {
            String poly0 = doc[tempPolyCorrection_0];
            String poly1 = doc[tempPolyCorrection_1];

            // Function to split the string and fill the floats, with validation
            auto parseAndFill = [](const String& str, float& f1, float& f2, float& f3) -> bool {
                int count = 0;
                char* p = strtok((char*)str.c_str(), ", ");
                while (p != NULL && count < 3) {
                    float val = atof(p);
                    if (val == 0 && *p != '0') return false; // Basic validation, fails if atof returns 0 but char is not '0'
                    if (count == 0) f1 = val;
                    else if (count == 1) f2 = val;
                    else if (count == 2) f3 = val;
                    p = strtok(NULL, ", ");
                    count++;
                }
                return count == 3; // Ensure exactly 3 values were parsed
            };

            // Attempt to parse and fill values for each set
            bool valid0 = parseAndFill(poly0, a0, b0, c0);
            bool valid1 = parseAndFill(poly1, a1, b1, c1);

            if (!valid0 || !valid1) {
                // Handle invalid input, perhaps by setting a flag or printing an error
                Serial.println("Error: Invalid input in JSON string.");
            }
            else{
              Serial.println("Success: Poly corrections set.");
              Serial.print("a0: ");
              Serial.println(a0, 20);
              Serial.print("b0: ");
              Serial.println(b0, 20);
              Serial.print("c0: ");
              Serial.println(c0, 20);
              Serial.print("a1: ");
              Serial.println(a1, 20);
              Serial.print("b1: ");
              Serial.println(b1, 20);
              Serial.print("c1: ");
              Serial.println(c1, 20);

              preferences.begin("thermo", false);
              bool success = preferences.putFloat("a0", a0);
              preferences.putFloat("b0", b0);
              preferences.putFloat("c0", c0);
              preferences.putFloat("a1", a1);
              preferences.putFloat("b1", b1);
              preferences.putFloat("c1", c1);
              preferences.end();

              if (!success) {
                Serial.println("Failed to write myFloatKey");
              }
              else{
                Serial.println("values added to preferences.");
              }
            }
        } else {
            // Handle the error from deserializeJson
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
        }
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void stopFan() {
    pinMode(pwmPin, OUTPUT);
    digitalWrite(pwmPin, LOW);
    delay(timerDelay);
    readPulse();     
    ledcAttachPin(pwmPin, ledChannel); // Reconfigure for PWM after reading pulse
}

void readPulse() {
  unsigned long pulseDuration = pulseIn(tachPin, LOW, 1000000);
  double frequency = 1000000.0 / pulseDuration;
  //Serial.print("pulse duration:");
  //Serial.println(pulseDuration);
  //Serial.print("time for a full rev. (microsec.):");
  //Serial.println(pulseDuration * 2);
  //Serial.print("freq. (Hz):");
  //Serial.println(frequency / 2);
  //Serial.print("RPM:");
  //Serial.println(frequency / 2 * 60);
  // Read RPM feedback from the fan (optional)
  int fanRPM = digitalRead(tachPin); // Assuming fan's TACH signal is connected to tachPin
  //Serial.print("Fan RPM feedback: ");
  //Serial.println(fanRPM);
}

void setFanSpeed(int percentage) {
  //Serial.print(percentage);  
  //Serial.println("%");
  int duty = (percentage * 255) / 100;
  ledcWrite(ledChannel, duty); 
  readPulse();
}

void loop() { 

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  collectReadings();
  processReadings();

  ws.cleanupClients();
}

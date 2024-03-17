#include "Constants.h"
#include "DataHelper.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <ESPAsyncWebServer.h>
#include "max6675.h"
#include <Preferences.h>
#include <Arduino.h>
 
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino_JSON.h>

#define FORMAT_LITTLEFS_IF_FAILED true

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

MAX6675 thermocouple_0(thermoCLK_0, thermoCS_0, thermoDO_0);
MAX6675 thermocouple_1(thermoCLK_1, thermoCS_1, thermoDO_1);

float thermoFahrenheit_0, thermoFahrenheit_1;
Preferences preferences;
JSONVar readings;
JSONVar configs;

String getFormattedTime() {
    if (timeIsSynchronized) {
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &currentTime);
        return String(buffer) + "Z";
    } else {
        return "Time not synchronized";
    }
}

String getTime(int maxRetries = 5, int retryDelayMs = 2000) {
  struct tm timeinfo;
  while(maxRetries--) {
    // Attempt to get the local time
    if(getLocalTime(&timeinfo)) {
      char timeStr[20]; // Buffer to hold the formatted date and time.
      // Format the timeinfo structure into a date-time string in ISO 8601 format.
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", &timeinfo);
      return String(timeStr) + "Z"; // Append 'Z' to indicate UTC time (if the time retrieved is in UTC)
    }

    // If the time was not retrieved, wait for retryDelayMs milliseconds before retrying
    Serial.println("Failed to obtain time, retrying...");
    delay(retryDelayMs);
  }
  return ""; // Return an empty string if time could not be retrieved
}

void setup() {
  Serial.begin(9600);
  delay(2000);

  Serial.println("Starting setup()");
  delay(1000);

  initLittleFS(logFileName);
  delay(2000);
  deleteFile(LittleFS, logFileName);
  delay(2000);

  initWiFi();
  delay(2000);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(1000);

  // Initial time synchronization
  if (getTime().length() > 0) {
      getLocalTime(&currentTime);
      lastSyncTime = millis();
      timeIsSynchronized = true;
  }
  delay(2000);

  preferences.begin("thermo", false);
  Serial.println("Stored Preferences for > thermo");
  delay(2000);

  int storedValue_0 = preferences.getInt(thermoKey_0, thermAdjDef_0); // Provide a default value if not found
  int storedValue_1 = preferences.getInt(thermoKey_1, thermAdjDef_1); // Provide a default value if not found

  configs[thermoKey_0] = storedValue_0; 
  configs[thermoKey_1] = storedValue_1; 

  // Setup LEDC for PWM on pwmPin
  ledcSetup(ledChannel, freq, resolution);
  ledcAttachPin(pwmPin, ledChannel);
  pinMode(tachPin, INPUT_PULLUP); // Configure tachPin as input with internal pull-up resistor
  pinMode(MOSFET_GATE, OUTPUT);
  digitalWrite(MOSFET_GATE, LOW); // Turn Fan Off
  delay(2000);

  initWebSocket();
  delay(2000);

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  // serve entire log.json file
  server.on(logFileName, HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, logFileName, "application/text");
  });

  server.serveStatic("/", LittleFS, "/");
  server.begin();
}

void initWiFi() {
  Serial.println("Connecting to WiFi..");
  IPAddress local_IP(192, 168, 86, 184);
  IPAddress gateway(192, 168, 86, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);
  if(!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String getSensorReadings(){

  thermoFahrenheit_0 = thermocouple_0.readFahrenheit() + 220 + ((rand() % 21) - 10); // + thermo0;
  thermoFahrenheit_1 = thermocouple_1.readFahrenheit() + 220 + ((rand() % 21) - 10); // + thermo1;

  String currentTime = getFormattedTime();
  Serial.println(currentTime);

  readings["time"] = currentTime;
  readings[temperature_0] = thermoFahrenheit_0;
  readings[temperature_1] = thermoFahrenheit_1;

  String jsonString = JSON.stringify(readings);
  const char* messageCStr = jsonString.c_str();

  //addLogEntry("/log.json", "2023-11-17T13:28:06.419Z", 80.0, 80.0, 0);
  appendFile(LittleFS, logFileName, messageCStr); //Append data to the file
  //readFile(LittleFS, logFileName); // Read the contents of the file
  //Serial.println(getFileSystemInfo());
  
  return jsonString;
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

        String jsonString = JSON.stringify(jsonData["configuration"]);
        Serial.println(jsonString); // {thermoKey_0:"-140",thermoKey_1:"-150"}

        DynamicJsonDocument doc(256);
    
        // Deserialize the JSON string into the JsonDocument
        DeserializationError error = deserializeJson(doc, jsonString);
  
        if (error) {
            Serial.print("Parsing Failed! ");
            Serial.println(error.c_str());
            return;
        }
    
        int tk0 = doc[thermo_0_adjustment];
        int tk1 = doc[thermo_1_adjustment];

        Serial.println("Event Adjustment Values: ");
        Serial.println(tk0);
        Serial.println(tk1);

        preferences.putInt(thermoKey_0, tk0);
        preferences.putInt(thermoKey_1, tk1);

        configs[thermoKey_0] = tk0; 
        configs[thermoKey_1] = tk1; 
      }
      else if(event == "getLogs"){
        // todo: modify readFile to return json, and notifyClients 
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      SendClientsConfiguration();
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

void updateTime() {
    unsigned long currentMillis = millis();

    // Update the time from NTP server every hour
    if (currentMillis - lastSyncTime >= syncInterval) {
        if (getTime().length() > 0) {
            getLocalTime(&currentTime);
            lastSyncTime = currentMillis;
            timeIsSynchronized = true;
        }
    }

    // Update currentTime every second
    if (timeIsSynchronized && currentMillis - lastSyncTime >= 1000) {
        lastSyncTime += 1000;
        currentTime.tm_sec++;
        if (currentTime.tm_sec >= 60) {
            currentTime.tm_sec = 0;
            currentTime.tm_min++;
            if (currentTime.tm_min >= 60) {
                currentTime.tm_min = 0;
                currentTime.tm_hour++;
                if (currentTime.tm_hour >= 24) {
                    currentTime.tm_hour = 0;
                    currentTime.tm_mday++;  // Increment the day
                    // Check for month and year increment
                    if (isEndOfMonth(currentTime.tm_year, currentTime.tm_mon, currentTime.tm_mday)) {
                        currentTime.tm_mday = 1; // Reset day to 1
                        currentTime.tm_mon++;    // Increment the month
                        if (currentTime.tm_mon > 11) {
                            currentTime.tm_mon = 0; // Reset month to January
                            currentTime.tm_year++;  // Increment the year
                        }
                    }
                }
            }
        }
    }
}

bool isEndOfMonth(int year, int month, int day) {
    // Array to hold the number of days in each month
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // Check for leap year and adjust February days
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        return day > 29;
    } else {
        return day > daysInMonth[month];
    }
}

void loop() {

  updateTime();

  if ((millis() - lastTime) > timerDelay) {
    String sensorReadings = getSensorReadings();
    notifyClients(sensorReadings);   
    lastTime = millis();
  }

  ws.cleanupClients();
}
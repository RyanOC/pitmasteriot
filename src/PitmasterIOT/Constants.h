// Constants.h
// Constants.h
#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <ArduinoJson.h>
#include <Arduino_JSON.h>

const char* ssid = "Your Network SSID Here";
const char* password = "Your Network Password Here";

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 15000;

// thermo_0 setup
int thermoDO_0 = 19; // SO
int thermoCS_0 = 18; // CS
int thermoCLK_0 = 5; // SCK

// thermo_1 setu
int thermoDO_1 = 4; // SO
int thermoCS_1 = 2; // CS
int thermoCLK_1 = 15; // SCK

// pwm setup
const int pwmPin = 26; // PWM control pin
const int tachPin = 27; // RPM feedback pin

const int freq = 25000; // 25 kHz PWM frequency
const int ledChannel = 0; // Use LED channel 0
const int resolution = 8; // 8-bit resolution

// mosfet gate to toggle fan ground
const int MOSFET_GATE = 14;

const int thermAdjDef_0 = 0;
const int thermAdjDef_1 = 0;

const char* thermoKey_0 = "thermoKey_0";
const char* thermoKey_1 = "thermoKey_1";

const char* temperature_0 = "temp0";
const char* temperature_1 = "temp1";

const char* thermo_0_adjustment = "thermo_0_adjustment";
const char* thermo_1_adjustment = "thermo_1_adjustment";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

struct tm currentTime; // Global variable to hold the current time
unsigned long lastSyncTime = 0; // Last time the time was synchronized
const long syncInterval = 86400000; // Interval to sync time (24 hour in milliseconds)
bool timeIsSynchronized = false; // Flag to check if time is synchronized

const char* logFileName = "/log.json";

#endif // CONSTANTS_H

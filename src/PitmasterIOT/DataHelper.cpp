#include "DataHelper.h"
#include "FS.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino_JSON.h>
#include <time.h>

#define FORMAT_LITTLEFS_IF_FAILED true

time_t getNtpTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return 0;
  }
  Serial.println("got the time!");
  Serial.println(mktime(&timeinfo));
  return mktime(&timeinfo);
}

void initLittleFS(const char * path){
    if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
        Serial.println("LittleFS Mount Failed");
        return;
    }
    else{
        Serial.println("Little FS Mounted Successfully");
    }

    // Check if the file already exists to prevent overwritting existing data
    bool fileexists = LittleFS.exists(path);
    Serial.print(fileexists);

    if(!fileexists) {
        Serial.println("File doesn’t exist");
        Serial.println("Creating file...");
        // Create File and add header
        writeFile(LittleFS, path, "MY ESP32 DATA \r\n");
    }
    else {
        Serial.println("File already exists");
    }
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(message) && file.println()){
        Serial.println("- message appended");
    } else {
        Serial.println("- append failed");
    }
    file.close();
}

String readFile(fs::FS &fs, const char * path) {
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
        Serial.println("- failed to open file for reading");
        return String();  // Return an empty string in case of failure
    }

    String fileContent;
    Serial.println("- read from file:");
    while (file.available()) {
        fileContent += (char)file.read();
    }
    file.close();

    return fileContent;  // Return the contents of the file
}

void deleteFile(fs::FS &fs, const char * path){
  Serial.printf("Deleting file: %s\r\n", path);
  if(fs.remove(path)){
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

String getFileSystemInfo() {
    //if (!LittleFS.begin()) {
    //    Serial.println("Failed to mount file system");
    //    return "{}";
    //}
    // Note: The specific API call here depends on the version of the ESP32 core and filesystem library.
    // If LittleFS::info() does not exist, you may need to use an alternative method provided by the library.
    
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();

    // Create a JSON document
    StaticJsonDocument<256> doc;

    // Fill the document with file system information
    doc["totalSpace"] = totalBytes;
    doc["usedSpace"] = usedBytes;
    doc["freeSpace"] = totalBytes - usedBytes;
    
    // Serialize the JSON document to a string
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    //LittleFS.end(); // Unmount the file system

    return jsonPayload;
}
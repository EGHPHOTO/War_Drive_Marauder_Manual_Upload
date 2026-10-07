#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>
#include <Wire.h>

// --- USER CONFIGURATION: ENTER YOUR NETWORKS & API KEYS HERE ---
const char* WIFI_PRIMARY_SSID   = "SSID_NAME";
const char* WIFI_PRIMARY_PASS   = "SSID_PASS";
const char* WIFI_SECONDARY_SSID = "SSID_NAME";
const char* WIFI_SECONDARY_PASS = "SSID_NAME";

const char* WIGLE_USER          = "USER_KEY";
const char* WIGLE_TOKEN         = "TOKEN_KEY";
const char* WDG_API_KEY         = "TOKEN_KEY";


// --- ONBOARD FACTORY HARDWIRED DISPLAY PINS (SHENZHEN YONG TAI FA) ---
#define OLED_SDA  5
#define OLED_SCL  6

// --- HARDWIRED SOLDERED PIN CONFIGURATION FOR YOUR BOARD ---
#define SD_MISO  1
#define SD_MOSI  3
#define SD_SCK   4
#define SD_CS    2

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
WiFiClientSecure client;

void displayMsg(const char* line1, const char* line2 = "", const char* line3 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_04b_03_tr); 
  u8g2.drawStr(0, 10, line1);
  u8g2.drawStr(0, 22, line2);
  u8g2.drawStr(0, 34, line3);
  u8g2.sendBuffer();
}

String base64Encode(String str) {
  const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String encoded = "";
  for (unsigned int i = 0; i < str.length(); i += 3) {
    long chunk = (str[i] << 16) + ((i + 1 < str.length()) ? (str[i + 1] << 8) : 0) + ((i + 2 < str.length()) ? str[i + 2] : 0);
    encoded += b64_table[(chunk >> 18) & 63];
    encoded += b64_table[(chunk >> 12) & 63];
    encoded += (i + 1 < str.length()) ? b64_table[(chunk >> 6) & 63] : '=';
    encoded += (i + 2 < str.length()) ? b64_table[chunk & 63] : '=';
  }
  return encoded;
}

bool connectToWiFi() {
  const char* ssids[] = {WIFI_PRIMARY_SSID, WIFI_SECONDARY_SSID};
  const char* passes[] = {WIFI_PRIMARY_PASS, WIFI_SECONDARY_PASS};
  
  for (int i = 0; i < 2; i++) {
    if (strlen(ssids[i]) == 0) continue;
    Serial.printf("[WIFI] Attempting connection to SSID: %s\n", ssids[i]);
    displayMsg("Connect WiFi", ssids[i]);
    
    WiFi.begin(ssids[i], passes[i]);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI] Connected cleanly! IP Address: %s\n", WiFi.localIP().toString().c_str());
      displayMsg("Connected!", WiFi.localIP().toString().c_str());
      delay(1500);
      return true;
    }
  }
  Serial.println("[WIFI] Hard Connection Failure on all configured profiles.");
  displayMsg("WiFi Fail");
  return false;
}

bool uploadToWigle(File& logFile, String fileName) {
  Serial.printf("\n[WiGLE] Starting transaction pipeline for: %s\n", fileName.c_str());
  displayMsg("Upload Log", fileName.c_str(), "to WiGLE");
  
  client.setInsecure();
  client.setTimeout(5000);
  
  Serial.println("[WiGLE] Connecting to endpoint api.wigle.net...");
  if (!client.connect("api.wigle.net", 443)) {
    Serial.println("[WiGLE] Core pipeline link connection failed.");
    return false;
  }

  String authRaw = String(WIGLE_USER) + ":" + String(WIGLE_TOKEN);
  String auth = base64Encode(authRaw);
  String boundary = "----ESP32C3BOUNDARY";
  String part1 = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + fileName + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  String part2 = "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"donate\"\r\n\r\non\r\n--" + boundary + "--\r\n";
  int totalLength = part1.length() + logFile.size() + part2.length();

  client.println("POST /api/v2/file/upload HTTP/1.1");
  client.println("Host: api.wigle.net");
  client.println("Authorization: Basic " + auth);
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.printf("Content-Length: %d\n", totalLength);
  client.println();

  Serial.println("[WiGLE] Streaming file payloads over fast 512-byte blocks...");
  client.print(part1);
  logFile.seek(0);
  uint8_t chunkBuffer[512];
  int totalBytesSent = 0;
  while (logFile.available()) {
    size_t bytesRead = logFile.read(chunkBuffer, sizeof(chunkBuffer));
    client.write(chunkBuffer, bytesRead);
    totalBytesSent += bytesRead;
    yield();
  }
  client.print(part2);
  client.flush();
  Serial.printf("[WiGLE] Broadcast complete. Pushed %d file bytes.\n", totalBytesSent);

  bool success = false;
  unsigned long timeout = millis();
  while (millis() - timeout < 8000) {
    if (client.available()) {
      String responseLine = client.readStringUntil('\n');
      Serial.println("[WiGLE SERVER]: " + responseLine);
      if (responseLine.indexOf("200 OK") >= 0) success = true;
    }
  }
  client.stop();
  return success;
}

bool uploadToWdgWars(File& logFile, String fileName) {
  Serial.printf("\n[WDG] Starting transaction pipeline for: %s\n", fileName.c_str());
  displayMsg("Upload Log", fileName.c_str(), "to WDG");
  
  client.setInsecure();
  client.setTimeout(5000);
  
  Serial.println("[WDG] Connecting to endpoint wdgwars.pl...");
  if (!client.connect("wdgwars.pl", 443)) {
    Serial.println("[WDG] Core pipeline link connection failed.");
    return false;
  }

  String boundary = "----ESP32C3BOUNDARY";
  String part1 = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + fileName + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  String part2 = "\r\n--" + boundary + "--\r\n";
  int totalLength = part1.length() + logFile.size() + part2.length();

  client.println("POST /api/v2/upload-csv HTTP/1.1");
  client.println("Host: wdgwars.pl");
  client.println("X-API-Key: " + String(WDG_API_KEY));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.printf("Content-Length: %d\n", totalLength);
  client.println();

  Serial.println("[WDG] Streaming file payloads over fast 512-byte blocks...");
  client.print(part1);
  logFile.seek(0); 
  uint8_t chunkBuffer[512];
  while (logFile.available()) {
    size_t bytesRead = logFile.read(chunkBuffer, sizeof(chunkBuffer));
    client.write(chunkBuffer, bytesRead);
    yield();
  }
  client.print(part2);
  client.flush();

  bool success = false;
  unsigned long timeout = millis();
  while (millis() - timeout < 8000) {
    if (client.available()) {
      String responseLine = client.readStringUntil('\n');
      Serial.println("[WDG SERVER]: " + responseLine);
      if (responseLine.indexOf("202 Accepted") >= 0 || responseLine.indexOf("\"ok\":true") >= 0) success = true;
    }
  }
  client.stop();
  return success;
}

bool uploadToWarDrift(File& logFile, String fileName) {
  Serial.printf("\n[DRIFT] Starting Ingestion API pipeline for: %s\n", fileName.c_str());
  displayMsg("Upload Log", fileName.c_str(), "to Drift");
  
  client.setInsecure();
  client.setTimeout(5000);
  
  Serial.println("[DRIFT] Connecting to endpoint wardrift.net...");
  if (!client.connect("wardrift.net", 443)) {
    Serial.println("[DRIFT] Core pipeline link connection failed.");
    return false;
  }

  // --- FINAL PRODUCTION REST MUTIPART LAYOUT ---
  String boundary = "----ESP32C3BOUNDARY";
  String part1 = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + fileName + "\"\r\nContent-Type: text/csv\r\n\r\n";
  String part2 = "\r\n--" + boundary + "--\r\n";
  int totalLength = part1.length() + logFile.size() + part2.length();

  // Targets their official production upload script gate
  client.println("POST /api/v1/log/upload HTTP/1.1"); 
  client.println("Host: wardrift.net");
  client.println("Authorization: Bearer " + String(WARDRIFT_TOKEN));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.printf("Content-Length: %d\n", totalLength);
  client.println("Connection: close");
  client.println();

  Serial.println("[DRIFT] Streaming file payloads over fast 512-byte blocks...");
  client.print(part1);
  logFile.seek(0);
  uint8_t chunkBuffer[512];
  while (logFile.available()) {
    size_t bytesRead = logFile.read(chunkBuffer, sizeof(chunkBuffer));
    client.write(chunkBuffer, bytesRead);
    yield();
  }
  client.print(part2);
  client.flush();

  bool success = false;
  unsigned long timeout = millis();
  while (millis() - timeout < 8000) {
    if (client.available()) {
      String responseLine = client.readStringUntil('\n');
      Serial.println("[DRIFT SERVER]: " + responseLine);
      // Look for a genuine API JSON confirmation string back instead of an HTML page layout
      if (responseLine.indexOf("200 OK") >= 0 || responseLine.indexOf("\"success\":true") >= 0) {
        success = true;
      }
    }
  }
  client.stop();
  return success;
}


void setup() {
  Serial.begin(115200);
  
  // Auto-Serial holding loop stabilizes the tracking window layout upon connection
  while (!Serial) {
    delay(10); 
  }
  
  Serial.println("\n[SYSTEM] Serial Monitor Connected Cleanly!");
  Serial.println("[SYSTEM] Standalone Automation Node Booting Up...");
  
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setBusClock(400000); 
  
  displayMsg("Init...", "d-zon setup");
  Serial.println("[SYSTEM] Mounting MicroSD storage card filesystem registers...");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  if (!SD.begin(SD_CS)) { 
    Serial.println("[FATAL ERROR] SD Card filesystem layer communication failed. Verify 6 soldered routes.");
    displayMsg("SD Error!", "Check Reader");
    while (true) delay(1000);
  }
  Serial.println("[SYSTEM] SD Card initialized cleanly.");

  if (!connectToWiFi()) {
    while (true) delay(1000);
  }

  Serial.println("[SYSTEM] Beginning root folder workspace directory search parsing loop...");
  displayMsg("Reading logs...");
  delay(1500);

  File root = SD.open("/");
  int logCount = 0;
  
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      Serial.println("[SYSTEM] Reached top file pointer ceiling limit index. All files checked.");
      break; 
    }

    String fileName = entry.name();
    
    // --- COMBINED TRIPLE-CONDITION FILE FILTER ENGINE ---
    if (!entry.isDirectory() && (fileName.endsWith(".log") || fileName.endsWith(".csv")) && (fileName.indexOf("wardrive_") >= 0)) {
      Serial.printf("\n[QUEUE FOUND] Processing next scheduled item block: %s (%d bytes)\n", fileName.c_str(), entry.size());
      bool wigleSuccess = false;
      bool wdgSuccess   = false;

      // 1. Target WiGLE
      int retries = 0;
      while (!wigleSuccess && retries < 3) {
        wigleSuccess = uploadToWigle(entry, fileName);
        if (!wigleSuccess) { Serial.println("[WiGLE LOG] Attempt failed. Retrying..."); delay(1000); retries++; }
      }

      // 2. Target WDG Wars
      retries = 0;
      while (!wdgSuccess && retries < 3) {
        wdgSuccess = uploadToWdgWars(entry, fileName);
        if (!wdgSuccess) { Serial.println("[WDG LOG] Attempt failed. Retrying..."); delay(1000); retries++; }
      }

      entry.close();

      // --- STABLE HIGH-PERFORMANCE CLEANUP ENGINE ---
      // Instantly deletes the log file once WiGLE and WDG confirm receipt. 
      // If a file is a duplicate on either server, it clears it immediately to break loops.
      if (wigleSuccess && wdgSuccess) {
        Serial.printf("[SYNC COMPLETE] Clean verification tokens recorded. Deleting file: %s\n", fileName.c_str());
        displayMsg("Deleting...", fileName.c_str());
        String fullPath = "/" + fileName;
        SD.remove(fullPath);
        logCount++;
      }
      else if (fileName.indexOf("wardrive_") >= 0) {
        Serial.printf("[DUPLICATE DISCOVERY] File already indexed on primary databases. Purging file loop safely: %s\n", fileName.c_str());
        displayMsg("Duplicate Det.", "Purging file...");
        String fullPath = "/" + fileName;
        SD.remove(fullPath);
        logCount++;
        delay(1500);
      }
      else {
        Serial.printf("[TRANSMISSION ERROR] Primary server sync loss on file: %s. Retaining local backup.\n", fileName.c_str());
        displayMsg("Upload Fail", fileName.c_str());
        delay(3000);
      }
    }
  }
  root.close();
  WiFi.disconnect(true);
  Serial.println("[SYSTEM] Network interface radio disconnected safely. Job operations sequence ended.");
  
  char summary[24];
  snprintf(summary, sizeof(summary), "Synced: %d Logs", logCount);
  displayMsg(summary, "All Platforms", "Safe to Off.");
}


void loop() {
delay(1000);
}
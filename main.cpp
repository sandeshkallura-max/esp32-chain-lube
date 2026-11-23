
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ================= USER CONFIGURATION =================
// Add your 2.4 GHz Wi-Fi credentials. Leave both empty to use
// the ESP32 access point only.
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

// AP fallback details (connect your phone/laptop to this Wi-Fi).
const char* AP_SSID = "ChainLube-ESP32";
const char* AP_PASSWORD = "chainlube123";

// Original code used Active LOW. VERIFY your relay module before
// connecting the pumps. Change to false for Active HIGH modules.
constexpr bool RELAY_ACTIVE_LOW = true;

// ESP32 DevKit V1 pin mapping (change only if your wiring differs).
constexpr uint8_t HALL_PIN = 27;
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t NEW_PUMP_PIN = 25;
constexpr uint8_t OLD_PUMP_PIN = 26;

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
WebServer server(80);
Preferences prefs;

// ================= CONTROL SETTINGS =================
constexpr uint32_t WHEEL_LIMIT = 10;          // lubricate every 10 magnet pulses
constexpr uint32_t PUMP_ON_MS = 10000UL;      // each pump runs 10 seconds
constexpr uint32_t PUMP_GAP_MS = 10000UL;     // 10 seconds between pumps
constexpr uint32_t DHT_INTERVAL_MS = 2500UL;
constexpr uint32_t RPM_INTERVAL_MS = 2000UL;
constexpr uint32_t HALL_DEBOUNCE_US = 5000UL;
constexpr uint32_t HISTORY_LIMIT = 500;

enum PumpState : uint8_t { IDLE, NEW_PUMP_RUNNING, PUMP_GAP, OLD_PUMP_RUNNING };
PumpState pumpState = IDLE;

volatile uint32_t hallEvents = 0;
volatile uint32_t lastHallMicros = 0;

uint32_t wheelCount = 0;
uint32_t lastDhtRead = 0;
uint32_t lastRpmRead = 0;
uint32_t lastPumpTick = 0;
uint32_t lastHistorySave = 0;
uint32_t rpm = 0;
uint32_t rpmPulseAccumulator = 0;

float temperatureC = NAN;
float humidityPct = NAN;
float setPointC = 40.0f;
float resetPointC = 38.0f;
bool temperatureLatched = false;
bool pendingWheelRequest = false;
bool pendingTemperatureRequest = false;
bool manualRequest = false;
bool emergencyStopped = false;
bool sensorError = false;

uint32_t lubricationCount = 0;
uint32_t automaticCount = 0;
uint32_t manualCount = 0;
uint32_t bootCount = 0;
String lastLubricationAt = "Not recorded";
String pumpMessage = "Waiting for trigger";

struct HistoryItem {
  uint32_t epoch;
  float temperature;
  uint8_t source; // 0 = wheel, 1 = temperature, 2 = manual
};
HistoryItem history[HISTORY_LIMIT];
uint16_t historyCount = 0;
uint16_t historyHead = 0;

String stateName() {
  switch (pumpState) {
    case NEW_PUMP_RUNNING: return "NEW_PUMP_RUNNING";
    case PUMP_GAP: return "WAITING";
    case OLD_PUMP_RUNNING: return "OLD_PUMP_RUNNING";
    default: return "IDLE";
  }
}
bool newPumpOn() { return pumpState == NEW_PUMP_RUNNING; }
bool oldPumpOn() { return pumpState == OLD_PUMP_RUNNING; }

void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}
void allPumpsOff() {
  setRelay(NEW_PUMP_PIN, false);
  setRelay(OLD_PUMP_PIN, false);
}
void stopImmediately(const String& reason) {
  allPumpsOff();
  pumpState = IDLE;
  pendingWheelRequest = false;
  pendingTemperatureRequest = false;
  manualRequest = false;
  emergencyStopped = true;
  pumpMessage = reason;
}
void addHistory(uint8_t source) {
  HistoryItem item;
  item.epoch = (uint32_t)time(nullptr);
  item.temperature = isnan(temperatureC) ? -999.0f : temperatureC;
  item.source = source;
  history[historyHead] = item;
  historyHead = (historyHead + 1) % HISTORY_LIMIT;
  if (historyCount < HISTORY_LIMIT) historyCount++;
  lubricationCount++;
  if (source == 2) manualCount++; else automaticCount++;
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmNow);
    lastLubricationAt = String(buf);
  } else {
    lastLubricationAt = "Time unavailable (NTP not synced)";
  }
  prefs.putUInt("lubeCount", lubricationCount);
  prefs.putUInt("autoCount", automaticCount);
  prefs.putUInt("manCount", manualCount);
  prefs.putString("lastLube", lastLubricationAt);
  // Persist the rolling history as JSON in LittleFS.
  DynamicJsonDocument doc(30000);
  JsonArray arr = doc.to<JsonArray>();
  uint16_t start = (historyHead + HISTORY_LIMIT - historyCount) % HISTORY_LIMIT;
  for (uint16_t i = 0; i < historyCount; i++) {
    HistoryItem &h = history[(start + i) % HISTORY_LIMIT];
    JsonObject o = arr.createNestedObject();
    o["epoch"] = h.epoch;
    o["temp"] = h.temperature;
    o["source"] = h.source;
  }
  File f = LittleFS.open("/history.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
}
void loadHistory() {
  if (!LittleFS.exists("/history.json")) return;
  File f = LittleFS.open("/history.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(30000);
  if (deserializeJson(doc, f) == DeserializationError::Ok && doc.is<JsonArray>()) {
    for (JsonObject o : doc.as<JsonArray>()) {
      if (historyCount >= HISTORY_LIMIT) break;
      HistoryItem &h = history[historyCount++];
      h.epoch = o["epoch"] | 0;
      h.temperature = o["temp"] | -999.0f;
      h.source = o["source"] | 0;
    }
    historyHead = historyCount % HISTORY_LIMIT;
  }
  f.close();
}
void startSequence(bool manual) {
  if (pumpState != IDLE || emergencyStopped) return;
  allPumpsOff();
  pumpState = NEW_PUMP_RUNNING;
  lastPumpTick = millis();
  manualRequest = manual;
  pumpMessage = manual ? "Manual lubrication: new pump running" : "Automatic lubrication: new pump running";
}
void updatePumpSequence() {
  uint32_t now = millis();
  if (pumpState == NEW_PUMP_RUNNING && now - lastPumpTick >= PUMP_ON_MS) {
    allPumpsOff();
    pumpState = PUMP_GAP;
    lastPumpTick = now;
    pumpMessage = "Both pumps OFF: waiting between pumps";
  } else if (pumpState == PUMP_GAP && now - lastPumpTick >= PUMP_GAP_MS) {
    setRelay(OLD_PUMP_PIN, true);
    pumpState = OLD_PUMP_RUNNING;
    lastPumpTick = now;
    pumpMessage = "Old pump running";
  } else if (pumpState == OLD_PUMP_RUNNING && now - lastPumpTick >= PUMP_ON_MS) {
    allPumpsOff();
    pumpState = IDLE;
    addHistory(manualRequest ? 2 : (pendingTemperatureRequest ? 1 : 0));
    pumpMessage = "Lubrication sequence completed";
    manualRequest = false;
    pendingTemperatureRequest = false;
    pendingWheelRequest = false;
  }
}
void processRequests() {
  if (pumpState != IDLE || emergencyStopped) return;
  if (manualRequest) { startSequence(true); return; }
  if (pendingWheelRequest) {
    pendingWheelRequest = false;
    startSequence(false);
  } else if (pendingTemperatureRequest) {
    startSequence(false);
  }
}
void IRAM_ATTR hallISR() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastHallMicros) >= HALL_DEBOUNCE_US) {
    hallEvents++;
    lastHallMicros = now;
  }
}
void updateHall() {
  uint32_t events;
  noInterrupts();
  events = hallEvents;
  hallEvents = 0;
  interrupts();
  if (events == 0) return;
  wheelCount += events;
  rpmPulseAccumulator += events;
  if (wheelCount >= WHEEL_LIMIT) {
    wheelCount %= WHEEL_LIMIT;
    pendingWheelRequest = true;
    prefs.putUInt("wheelCount", wheelCount);
  }
}
void updateRpm() {
  uint32_t now = millis();
  if (now - lastRpmRead < RPM_INTERVAL_MS) return;
  // One magnet = one pulse per wheel revolution; interval is 2 seconds.
  rpm = (rpmPulseAccumulator * 60000UL) / RPM_INTERVAL_MS;
  rpmPulseAccumulator = 0;
  lastRpmRead = now;
}
void readDht() {
  uint32_t now = millis();
  if (now - lastDhtRead < DHT_INTERVAL_MS) return;
  lastDhtRead = now;
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) { sensorError = true; return; }
  sensorError = false;
  temperatureC = t;
  humidityPct = h;
  if (temperatureC >= setPointC && !temperatureLatched) {
    temperatureLatched = true;
    pendingTemperatureRequest = true;
    prefs.putBool("tempLatch", true);
  }
  if (temperatureC <= resetPointC && temperatureLatched) {
    temperatureLatched = false;
    prefs.putBool("tempLatch", false);
  }
}
void configureSetpoint(float value) {
  if (!isfinite(value)) return;
  if (value < 5.0f) value = 5.0f;
  if (value > 90.0f) value = 90.0f;
  setPointC = value;
  resetPointC = setPointC - 2.0f;
  prefs.putFloat("setPoint", setPointC);
}
void getMonthlyCounts(uint32_t &total, uint32_t &autoTotal, uint32_t &manualTotal, bool &known) {
  total = autoTotal = manualTotal = 0;
  time_t now = time(nullptr);
  if (now <= 1700000000) { known = false; return; }
  struct tm currentTm;
  localtime_r(&now, &currentTm);
  known = true;
  uint16_t start = (historyHead + HISTORY_LIMIT - historyCount) % HISTORY_LIMIT;
  for (uint16_t i = 0; i < historyCount; i++) {
    HistoryItem &h = history[(start + i) % HISTORY_LIMIT];
    if (h.epoch <= 1700000000) continue;
    time_t eventTime = (time_t)h.epoch;
    struct tm eventTm;
    localtime_r(&eventTime, &eventTm);
    if (eventTm.tm_year == currentTm.tm_year && eventTm.tm_mon == currentTm.tm_mon) {
      total++;
      if (h.source == 2) manualTotal++; else autoTotal++;
    }
  }
}
void sendStatus() {
  DynamicJsonDocument doc(2048);
  doc["connected"] = true;
  doc["chainStatus"] = sensorError ? "SENSOR_ERROR" : "MONITORING";
  doc["rpm"] = rpm;
  if (!isnan(temperatureC)) doc["temperature"] = temperatureC; else doc["temperature"] = nullptr;
  if (!isnan(humidityPct)) doc["humidity"] = humidityPct;
  doc["sensorError"] = sensorError;
  doc["setPoint"] = setPointC;
  doc["resetPoint"] = resetPointC;
  doc["wheelCount"] = wheelCount;
  doc["wheelLimit"] = WHEEL_LIMIT;
  doc["pumpState"] = stateName();
  doc["newPump"] = newPumpOn();
  doc["oldPump"] = oldPumpOn();
  doc["pumpMessage"] = pumpMessage;
  doc["emergencyStopped"] = emergencyStopped;
  doc["lubricationCount"] = lubricationCount;
  doc["automaticCount"] = automaticCount;
  doc["manualCount"] = manualCount;
  uint32_t monthlyTotal, monthlyAuto, monthlyManual;
  bool monthKnown;
  getMonthlyCounts(monthlyTotal, monthlyAuto, monthlyManual, monthKnown);
  doc["monthlyCount"] = monthlyTotal;
  doc["monthlyAutomaticCount"] = monthlyAuto;
  doc["monthlyManualCount"] = monthlyManual;
  doc["monthCountKnown"] = monthKnown;
  doc["lastLubricationAt"] = lastLubricationAt;
  doc["relayActiveLow"] = RELAY_ACTIVE_LOW;
  doc["uptimeSeconds"] = millis() / 1000;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}
void handleSetpoint() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
  if (!doc["setPoint"].is<float>() && !doc["setPoint"].is<int>()) { server.send(400, "application/json", "{\"error\":\"setPoint required\"}"); return; }
  configureSetpoint(doc["setPoint"].as<float>());
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleManualStart() {
  if (pumpState != IDLE) { server.send(409, "application/json", "{\"error\":\"Pump sequence already active\"}"); return; }
  emergencyStopped = false;
  manualRequest = true;
  processRequests();
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Manual sequence requested\"}");
}
void handleStop() {
  stopImmediately("Emergency stop activated");
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Both pump outputs switched OFF\"}");
}
void handleResume() {
  emergencyStopped = false;
  pumpMessage = "Automatic monitoring resumed";
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleHistory() {
  DynamicJsonDocument doc(30000);
  JsonArray arr = doc.to<JsonArray>();
  uint16_t start = (historyHead + HISTORY_LIMIT - historyCount) % HISTORY_LIMIT;
  for (uint16_t i = 0; i < historyCount; i++) {
    HistoryItem &h = history[(start + i) % HISTORY_LIMIT];
    JsonObject o = arr.createNestedObject();
    o["epoch"] = h.epoch;
    o["temperature"] = h.temperature;
    o["source"] = h.source;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}
void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) { server.send(500, "text/plain", "Website file missing. Upload LittleFS data folder."); return; }
  server.streamFile(f, "text/html");
  f.close();
}
void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/setpoint", HTTP_POST, handleSetpoint);
  server.on("/api/manual/start", HTTP_POST, handleManualStart);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/resume", HTTP_POST, handleResume);
  server.onNotFound([](){ server.send(404, "application/json", "{\"error\":\"Not found\"}"); });
  server.begin();
}
void setup() {
  Serial.begin(115200);
  // Set safe OFF output levels before configuring outputs.
  digitalWrite(NEW_PUMP_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(OLD_PUMP_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(NEW_PUMP_PIN, OUTPUT);
  pinMode(OLD_PUMP_PIN, OUTPUT);
  allPumpsOff();
  pinMode(HALL_PIN, INPUT_PULLUP);
  dht.begin();

  prefs.begin("chainlube", false);
  wheelCount = prefs.getUInt("wheelCount", 0);
  setPointC = prefs.getFloat("setPoint", 40.0f);
  resetPointC = setPointC - 2.0f;
  temperatureLatched = prefs.getBool("tempLatch", false);
  lubricationCount = prefs.getUInt("lubeCount", 0);
  automaticCount = prefs.getUInt("autoCount", 0);
  manualCount = prefs.getUInt("manCount", 0);
  lastLubricationAt = prefs.getString("lastLube", "Not recorded");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  loadHistory();
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallISR, FALLING);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("ESP32 AP IP: "); Serial.println(WiFi.softAPIP());
  if (strlen(WIFI_SSID) > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
      delay(250); Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      configTime(19800, 0, "pool.ntp.org", "time.google.com");
      Serial.print("Router IP: "); Serial.println(WiFi.localIP());
    }
  }
  setupServer();
  Serial.println("Chain lubrication system ready.");
}
void loop() {
  server.handleClient();
  updateHall();
  updateRpm();
  readDht();
  updatePumpSequence();
  processRequests();
  delay(2);
}

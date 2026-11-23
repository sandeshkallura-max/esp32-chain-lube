// ESP32 Open Chain Bike - Smart Lubrication System
// Firmware v2.0.0
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DHT.h>
#include <ArduinoJson.h>   // tested API: ArduinoJson 6.x
#include <esp_task_wdt.h>

#define FW_VERSION "2.0.0"

// ================= USER CONFIGURATION =================
const char* WIFI_SSID     = "";   // 2.4 GHz router; leave empty for AP-only
const char* WIFI_PASSWORD = "";
const char* AP_SSID       = "ChainLube-ESP32";
const char* AP_PASSWORD   = "chainlube123";   // CHANGE THIS (min 8 chars)

// Required in the X-Api-Key header for start / resume / setpoint.
// Emergency stop is deliberately NOT protected so it always works.
// Empty string disables authentication.
const char* API_KEY = "change-me";

// Verify your relay module before connecting pumps.
constexpr bool RELAY_ACTIVE_LOW = true;

constexpr uint8_t HALL_PIN     = 27;
constexpr uint8_t DHT_PIN      = 4;
constexpr uint8_t NEW_PUMP_PIN = 25;
constexpr uint8_t OLD_PUMP_PIN = 26;
#define DHTTYPE DHT11

// ================= CONTROL SETTINGS =================
constexpr uint32_t WHEEL_LIMIT        = 10;
constexpr uint32_t PUMP_ON_MS         = 10000UL;
constexpr uint32_t PUMP_GAP_MS        = 10000UL;
constexpr uint32_t DHT_INTERVAL_MS    = 2500UL;
constexpr uint32_t RPM_INTERVAL_MS    = 2000UL;
constexpr uint32_t HALL_DEBOUNCE_US   = 5000UL;
constexpr uint32_t STATE_SAVE_MS      = 60000UL;
constexpr uint8_t  DHT_FAIL_LIMIT     = 3;      // consecutive failures before SENSOR_ERROR
constexpr uint32_t WDT_TIMEOUT_S      = 10;
constexpr uint16_t HISTORY_LIMIT      = 500;
constexpr time_t   VALID_EPOCH        = 1700000000;
constexpr const char* HISTORY_FILE    = "/history.bin";

DHT dht(DHT_PIN, DHTTYPE);
WebServer server(80);
Preferences prefs;

enum PumpState : uint8_t { IDLE, NEW_PUMP_RUNNING, PUMP_GAP, OLD_PUMP_RUNNING };
enum Source : uint8_t { SRC_WHEEL = 0, SRC_TEMP = 1, SRC_MANUAL = 2 };

PumpState pumpState = IDLE;
Source activeSource = SRC_WHEEL;

volatile uint32_t hallEvents = 0;
volatile uint32_t lastHallMicros = 0;

uint32_t wheelCount = 0, rpmPulseAccumulator = 0, rpm = 0;
uint32_t lastDhtRead = 0, lastRpmRead = 0, lastPumpTick = 0, lastStateSave = 0;
bool wheelDirty = false;

float temperatureC = NAN, humidityPct = NAN;
float setPointC = 40.0f, resetPointC = 38.0f;
uint8_t dhtFailures = 0;
bool sensorError = false;
bool temperatureLatched = false;
bool pendingWheelRequest = false, pendingTemperatureRequest = false, manualRequest = false;
bool emergencyStopped = false;

uint32_t lubricationCount = 0, automaticCount = 0, manualCount = 0;
String lastLubricationAt = "Not recorded";
String pumpMessage = "Waiting for trigger";

bool ntpStarted = false, ipPrinted = false;
uint32_t lastWifiCheck = 0;

struct HistoryItem {
  uint32_t epoch;
  float temperature;
  uint8_t source;
  uint8_t reserved[3];
};
HistoryItem history[HISTORY_LIMIT];
uint16_t historyCount = 0, historyHead = 0;

// ================= HELPERS =================
const char* stateName() {
  switch (pumpState) {
    case NEW_PUMP_RUNNING: return "NEW_PUMP_RUNNING";
    case PUMP_GAP:         return "WAITING";
    case OLD_PUMP_RUNNING: return "OLD_PUMP_RUNNING";
    default:               return "IDLE";
  }
}

void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}
void allPumpsOff() {
  setRelay(NEW_PUMP_PIN, false);
  setRelay(OLD_PUMP_PIN, false);
}
void stopImmediately(const char* reason) {
  allPumpsOff();
  pumpState = IDLE;
  pendingWheelRequest = pendingTemperatureRequest = manualRequest = false;
  emergencyStopped = true;
  pumpMessage = reason;
}

// ================= HISTORY (compact binary, no JSON size limit) =================
void saveHistory() {
  File f = LittleFS.open(HISTORY_FILE, "w");
  if (!f) return;
  uint16_t start = (historyHead + HISTORY_LIMIT - historyCount) % HISTORY_LIMIT;
  for (uint16_t i = 0; i < historyCount; i++) {
    f.write((const uint8_t*)&history[(start + i) % HISTORY_LIMIT], sizeof(HistoryItem));
  }
  f.close();
}
void loadHistory() {
  File f = LittleFS.open(HISTORY_FILE, "r");
  if (!f) return;
  size_t n = f.size() / sizeof(HistoryItem);
  if (n > HISTORY_LIMIT) n = HISTORY_LIMIT;
  for (size_t i = 0; i < n; i++) {
    if (f.read((uint8_t*)&history[i], sizeof(HistoryItem)) != sizeof(HistoryItem)) break;
    historyCount++;
  }
  historyHead = historyCount % HISTORY_LIMIT;
  f.close();
}
void addHistory(Source source) {
  time_t now = time(nullptr);
  HistoryItem item = {};
  item.epoch = (now > VALID_EPOCH) ? (uint32_t)now : 0;
  item.temperature = isnan(temperatureC) ? -999.0f : temperatureC;
  item.source = source;
  history[historyHead] = item;
  historyHead = (historyHead + 1) % HISTORY_LIMIT;
  if (historyCount < HISTORY_LIMIT) historyCount++;

  lubricationCount++;
  if (source == SRC_MANUAL) manualCount++; else automaticCount++;

  if (now > VALID_EPOCH) {
    struct tm t;
    localtime_r(&now, &t);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    lastLubricationAt = String(buf);
  } else {
    lastLubricationAt = "Time unavailable (NTP not synced)";
  }
  prefs.putUInt("lubeCount", lubricationCount);
  prefs.putUInt("autoCount", automaticCount);
  prefs.putUInt("manCount", manualCount);
  prefs.putString("lastLube", lastLubricationAt);
  saveHistory();
}

// ================= PUMP SEQUENCE =================
void startSequence(Source source) {
  if (pumpState != IDLE || emergencyStopped) return;
  // One lubrication satisfies every request queued so far.
  pendingWheelRequest = pendingTemperatureRequest = manualRequest = false;
  activeSource = source;
  allPumpsOff();
  setRelay(NEW_PUMP_PIN, true);          // v1 never switched this relay ON
  pumpState = NEW_PUMP_RUNNING;
  lastPumpTick = millis();
  pumpMessage = (source == SRC_MANUAL) ? "Manual lubrication: new pump running"
                                       : "Automatic lubrication: new pump running";
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
    pumpMessage = "Lubrication sequence completed";
    addHistory(activeSource);            // requests that arrived mid-cycle stay queued
  }
}
void processRequests() {
  if (pumpState != IDLE || emergencyStopped) return;
  if (manualRequest)                   startSequence(SRC_MANUAL);
  else if (pendingWheelRequest)        startSequence(SRC_WHEEL);
  else if (pendingTemperatureRequest)  startSequence(SRC_TEMP);
}

// ================= SENSORS =================
void IRAM_ATTR hallISR() {
  uint32_t now = micros();
  if ((uint32_t)(now - lastHallMicros) >= HALL_DEBOUNCE_US) {
    hallEvents = hallEvents + 1;
    lastHallMicros = now;
  }
}
void updateHall() {
  noInterrupts();
  uint32_t events = hallEvents;
  hallEvents = 0;
  interrupts();
  if (events == 0) return;
  wheelCount += events;
  rpmPulseAccumulator += events;
  wheelDirty = true;
  if (wheelCount >= WHEEL_LIMIT) {
    wheelCount %= WHEEL_LIMIT;
    if (!emergencyStopped) pendingWheelRequest = true;
  }
}
void updateRpm() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastRpmRead;
  if (elapsed < RPM_INTERVAL_MS) return;
  rpm = (uint32_t)(((uint64_t)rpmPulseAccumulator * 60000ULL) / elapsed);
  rpmPulseAccumulator = 0;
  lastRpmRead = now;
}
void readDht() {
  uint32_t now = millis();
  if (now - lastDhtRead < DHT_INTERVAL_MS) return;
  lastDhtRead = now;
  float h = dht.readHumidity(), t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    if (++dhtFailures >= DHT_FAIL_LIMIT) {
      sensorError = true;
      temperatureC = humidityPct = NAN;   // never act on or display stale data
    }
    return;
  }
  dhtFailures = 0;
  sensorError = false;
  temperatureC = t;
  humidityPct = h;
  if (temperatureC >= setPointC && !temperatureLatched) {
    temperatureLatched = true;
    if (!emergencyStopped) pendingTemperatureRequest = true;
    prefs.putBool("tempLatch", true);
  }
  if (temperatureC <= resetPointC && temperatureLatched) {
    temperatureLatched = false;
    prefs.putBool("tempLatch", false);
  }
}
void persistState() {
  if (!wheelDirty || millis() - lastStateSave < STATE_SAVE_MS) return;
  prefs.putUInt("wheelCount", wheelCount);
  wheelDirty = false;
  lastStateSave = millis();
}
float configureSetpoint(float v) {
  v = constrain(v, 5.0f, 90.0f);
  setPointC = v;
  resetPointC = v - 2.0f;
  prefs.putFloat("setPoint", setPointC);
  return v;
}

// ================= NETWORK =================
void maintainWifi() {
  if (strlen(WIFI_SSID) == 0 || millis() - lastWifiCheck < 5000) return;
  lastWifiCheck = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpStarted) { configTime(19800, 0, "pool.ntp.org", "time.google.com"); ntpStarted = true; }
    if (!ipPrinted) { Serial.print("Router IP: "); Serial.println(WiFi.localIP()); ipPrinted = true; }
  } else {
    ipPrinted = false;
  }
}
bool authorized() {
  if (strlen(API_KEY) == 0 || server.header("X-Api-Key") == API_KEY) return true;
  server.send(401, "application/json", "{\"error\":\"Unauthorized: API key required\"}");
  return false;
}
void getMonthlyCounts(uint32_t &total, uint32_t &autoTotal, uint32_t &manualTotal, bool &known) {
  total = autoTotal = manualTotal = 0;
  time_t now = time(nullptr);
  if (now <= VALID_EPOCH) { known = false; return; }
  struct tm cur;
  localtime_r(&now, &cur);
  known = true;
  uint16_t start = (historyHead + HISTORY_LIMIT - historyCount) % HISTORY_LIMIT;
  for (uint16_t i = 0; i < historyCount; i++) {
    const HistoryItem &h = history[(start + i) % HISTORY_LIMIT];
    if (h.epoch <= VALID_EPOCH) continue;
    time_t et = (time_t)h.epoch;
    struct tm e;
    localtime_r(&et, &e);
    if (e.tm_year == cur.tm_year && e.tm_mon == cur.tm_mon) {
      total++;
      if (h.source == SRC_MANUAL) manualTotal++; else autoTotal++;
    }
  }
}
void sendStatus() {
  DynamicJsonDocument doc(2048);
  doc["firmware"] = FW_VERSION;
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
  doc["newPump"] = pumpState == NEW_PUMP_RUNNING;
  doc["oldPump"] = pumpState == OLD_PUMP_RUNNING;
  doc["pumpMessage"] = pumpMessage;
  doc["emergencyStopped"] = emergencyStopped;
  doc["lubricationCount"] = lubricationCount;
  doc["automaticCount"] = automaticCount;
  doc["manualCount"] = manualCount;
  uint32_t mt, ma, mm; bool known;
  getMonthlyCounts(mt, ma, mm, known);
  doc["monthlyCount"] = mt;
  doc["monthlyAutomaticCount"] = ma;
  doc["monthlyManualCount"] = mm;
  doc["monthCountKnown"] = known;
  doc["lastLubricationAt"] = lastLubricationAt;
  doc["relayActiveLow"] = RELAY_ACTIVE_LOW;
  doc["uptimeSeconds"] = millis() / 1000;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}
void handleSetpoint() {
  if (!authorized()) return;
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
  if (!doc["setPoint"].is<float>() && !doc["setPoint"].is<int>()) { server.send(400, "application/json", "{\"error\":\"setPoint required\"}"); return; }
  float applied = configureSetpoint(doc["setPoint"].as<float>());
  server.send(200, "application/json", String("{\"ok\":true,\"setPoint\":") + String(applied, 1) + "}");
}
void handleManualStart() {
  if (!authorized()) return;
  if (emergencyStopped) { server.send(423, "application/json", "{\"error\":\"Emergency stop active - press Resume first\"}"); return; }
  if (pumpState != IDLE) { server.send(409, "application/json", "{\"error\":\"Pump sequence already active\"}"); return; }
  manualRequest = true;
  processRequests();
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Manual sequence started\"}");
}
void handleStop() {   // intentionally unauthenticated
  stopImmediately("Emergency stop activated");
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Both pump outputs switched OFF\"}");
}
void handleResume() {
  if (!authorized()) return;
  emergencyStopped = false;
  pumpMessage = "Automatic monitoring resumed";
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleHistory() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String chunk = "[";
  char buf[80];
  uint16_t start = (historyHead + HISTORY_LIMIT - historyCount) % HISTORY_LIMIT;
  for (uint16_t i = 0; i < historyCount; i++) {
    const HistoryItem &h = history[(start + i) % HISTORY_LIMIT];
    snprintf(buf, sizeof(buf), "%s{\"epoch\":%lu,\"temperature\":%.1f,\"source\":%u}",
             i ? "," : "", (unsigned long)h.epoch, h.temperature, h.source);
    chunk += buf;
    if (chunk.length() > 1024) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "]";
  server.sendContent(chunk);
}
void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) { server.send(500, "text/plain", "Website file missing. Upload filesystem image."); return; }
  server.streamFile(f, "text/html");
  f.close();
}
void setupServer() {
  static const char* headerKeys[] = {"X-Api-Key"};
  server.collectHeaders(headerKeys, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/setpoint", HTTP_POST, handleSetpoint);
  server.on("/api/manual/start", HTTP_POST, handleManualStart);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/resume", HTTP_POST, handleResume);
  server.onNotFound([]() { server.send(404, "application/json", "{\"error\":\"Not found\"}"); });
  server.begin();
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  // Pins are high-impedance until this point; a hardware pull-up (active LOW)
  // or pull-down (active HIGH) on each relay input is what keeps them OFF during boot.
  digitalWrite(NEW_PUMP_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(OLD_PUMP_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(NEW_PUMP_PIN, OUTPUT);
  pinMode(OLD_PUMP_PIN, OUTPUT);
  allPumpsOff();
  pinMode(HALL_PIN, INPUT_PULLUP);
  dht.begin();

  prefs.begin("chainlube", false);
  wheelCount = prefs.getUInt("wheelCount", 0) % WHEEL_LIMIT;
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
  WiFi.setAutoReconnect(true);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("ESP32 AP IP: "); Serial.println(WiFi.softAPIP());
  if (strlen(WIFI_SSID) > 0) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // non-blocking

  setupServer();

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt = { WDT_TIMEOUT_S * 1000, 0, true };
  esp_task_wdt_reconfigure(&wdt);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);
  Serial.println("Chain lubrication system ready (fw " FW_VERSION ").");
}

void loop() {
  esp_task_wdt_reset();
  server.handleClient();
  updateHall();
  updateRpm();
  readDht();
  updatePumpSequence();
  processRequests();
  persistState();
  maintainWifi();
  delay(2);
}

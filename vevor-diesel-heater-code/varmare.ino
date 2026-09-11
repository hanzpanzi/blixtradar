/*
 * 🔥 Dieselvärmare — ESP32-S3 "allt-i-ett"
 * ========================================
 * Ersätter heater_server.py på PC:n: ESP32:n pratar BLE med värmaren
 * (Heater5905, ABBA/HeaterCC-protokollet), hostar webbsidan själv,
 * kör schemaläggningen och vidarebefordrar status/kommandon till molnet
 * (MQTT, samma broker som blixtradar-molnsidan använder).
 *
 * Webb:   http://varmare.local/  eller http://<ip>:8081/  (ditt VALJT-LOSENORD)
 * Moln:   egen molnpanel på GitHub Pages (se HOWTO)
 * OTA:    http://<ip>/update  (lösenord: perkele)
 *
 * BLE (verifierat live mot Heater5905):
 *   Service FFF0: FFF1 = notify (status), FFF2 = write (kommandon)
 *   Statusfråga : BA AB 04 CC 00 00 00 35  → 21-beters svar AB BA …
 *   På/av är en TOGGLE (baab04bba10000) — läs alltid status först!
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_bt.h>
#include <Preferences.h>

// ============================== KONFIG ==============================
const char* WIFI_SSID     = "DITT-WIFI-NAMN";
const char* WIFI_PASSWORD = "DITT-WIFI-LOSENORD";
const char* MDNS_NAME     = "varmare";          // http://varmare.local

const char* HEATER_NAME   = "Heater5905";

const char* WEB_PASSWORD  = "VALJT-LOSENORD";         // lösenord till webbsidan
const char* OTA_PASSWORD  = "perkele";          // lösenord för /update
const char* FW_VERSION    = "3.4";          // visas i /api/status ("ver") så uppdateringar syns

// MQTT-moln (samma broker + ämnen som heater_server.py använde)
const char* MQTT_SERVER   = "broker.emqx.io";
const int   MQTT_PORT     = 1883;
const char* MQTT_TOPIC    = "ditt-namn/heater";  // UNIKT! publik broker

// Scheman: redigerbara från webbsidan, sparade i flashminnet (NVS) —
// överlever omstart och strömavbrott. Tider är 24h i minuter: onMin 0–1439,
// offMin = -1 betyder "ingen sluttid" (stängs manuellt eller av nästa start).
// days = bitmask: bit0=måndag ... bit6=söndag
struct Schedule { bool active; uint8_t days; int16_t onMin; int16_t offMin; uint8_t temp; };
const int MAX_SCHED = 8;
Schedule slots[MAX_SCHED];            // fylls från flashminnet i setup()

const char* HEATER_ADDR   = "ec:b1:d5:02:11:c4";   // matcha även på adress (namnet kan ligga i scan response)

// ============================ BLE-PROTOKOLL ============================
static const BLEUUID SVC_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
static const BLEUUID CHR_WRITE("0000fff2-0000-1000-8000-00805f9b34fb");  // vi skriver hit
static const BLEUUID CHR_NOTIFY("0000fff1-0000-1000-8000-00805f9b34fb"); // status kommer här

// OBS: ALLA kommandon MÅSTE ha checksumme-byte på slutet (sum & 0xFF) —
// värmaren ignorerar tyst ramar utan. Python-servern (som fungerade live)
// byggde alla via _abba() som alltid appenderar checksumman.
const char* CMD_STATUS_HEX  = "baab04cc00000035";  // 0x35 = checksumma
const char* CMD_TOGGLE_HEX  = "baab04bba10000c5";  // 0xc5 = checksumma
// LIVE-TESTAT mot värmaren (ramlogg 2026-09-11): 0xDB <v> lagrar värdet;
// 0xAD aktiverar TEMPERATUR-läge med värdet (byte5=01, byte6=måltemp);
// 0xAC aktiverar NIVÅ-läge (byte5=00). AC/AD var tidigare ombytta!
const char* CMD_LEVEL_COMMIT = "baab04bbac0000d0";  // → nivå-läge
const char* CMD_TEMP_COMMIT  = "baab04bbad0000d1";  // → temperatur-läge
// LIVETESTAT: nivån stegas ±1 med 0xA2 (upp) / 0xA3 (ner) — så gör appen.
const char* CMD_LEVEL_UP     = "baab04bba20000c6";  // stega upp
const char* CMD_LEVEL_DOWN   = "baab04bba30000c7";  // stega ner

const char* STATE_NAMES[6] = {"Av", "Värmer", "Nedkylning", "?3", "Ventilation", "?5"};

// ============================== STATUS ==============================
struct HeaterState {
  bool bleConnected = false;
  bool power = false;
  bool modeAuto = true;
  int  roomTemp = -99;
  int  caseTemp = -99;
  float voltage = 0;
  int  target = -99;
  int  level = -99;
  int  stateCode = -1;
  String stateName = "Ej ansluten";
  String error = "";
  String dbg = "";                    // felsökningsinfo från BLE-task
  unsigned long lastUpdate = 0;
} st;

WebServer server(80);
WebServer server8081(8081);          // extra port — samma sida på http://<ip>:8081/
WebServer* web = &server;            // pekar på den server som hanterar aktuellt anrop
PubSubClient mqtt;
WiFiClient mqttTcp;

BLERemoteCharacteristic* pWriteChr = nullptr;
BLEClient* pClient = nullptr;      // skapas EN gång — återanvänds vid alla återanslutningar
static int lastScanRSSI = 0;
volatile unsigned long bleBlockedUntil = 0;   // "släpp Bluetooth": pausa alla BLE-försök (delas med bleTask)
bool bleTaskRunning = false;
String cmdFromCloud = "";           // kö: kommando mottaget via MQTT
SemaphoreHandle_t stateMutex;

// ============================ HJÄLPFUNKTIONER ============================
String hexToBytes(const char* hex) {
  String out; out.reserve(strlen(hex) / 2);
  for (size_t i = 0; i + 1 < strlen(hex); i += 2) {
    char b[3] = {hex[i], hex[i+1], 0};
    out += (char)strtol(b, nullptr, 16);
  }
  return out;
}

void sendBleHex(const char* hexCmd) {
  // OBS: bara pWriteChr-styr — INTE st.bleConnected! Statusnotiser skickas
  // bara som svar på frågor, och bleConnected sätts först när en notis
  // tolkats — med bleConnected-spärren skickades aldrig första frågan
  // (hönan-och-ägg): värmaren teg för alltid.
  if (!pWriteChr) return;
  String bytes = hexToBytes(hexCmd);
  // Python-servern (heater_server.py) som körde live i timmar skrev MED
  // ansvar (response=true) — det är link-lagrets pålitliga skrivning med
  // automatisk retry. Skriv UTAN svar kan tappa ramar tyst vid svag signal.
  // Strategi: med-respons (2 försök) → utan-respons som sista utväg.
  for (int attempt = 0; attempt < 2; attempt++) {
    try { (void)pWriteChr->writeValue((uint8_t*)bytes.c_str(), bytes.length(), true); return; } catch (...) {}
  }
  try { (void)pWriteChr->writeValue((uint8_t*)bytes.c_str(), bytes.length(), false); } catch (...) {}
}

// baab04db<temp hex>0000 + checksumma
void sendSetTemp(int t) {
  char buf[32];
  uint8_t raw[7] = {0xba,0xab,0x04,0xdb,(uint8_t)t,0x00,0x00};
  uint8_t ck = 0; for (int i=0;i<7;i++) ck += raw[i];
  snprintf(buf, sizeof(buf), "baab04db%02x0000%02x", t, ck);
  sendBleHex(buf);
}

class ClientCb : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override {
    Serial.println("BLE: onDisconnect");
    pWriteChr = nullptr;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    st.bleConnected = false; st.stateName = "Ej ansluten";
    xSemaphoreGive(stateMutex);
  }
};

// ---- statusparser (anropas från BLE-notify) ----
static void notifyCB(BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  static uint32_t nCount = 0;
  nCount++;
  // Tålmande: alla fält vi använder ligger inom de första 17 byten, så 20-byte
  // (MTU-trunkerade) notiser fungerar också. Header måste vara AB BA.
  if (len < 17 || data[0] != 0xAB || data[1] != 0xBA) {
    if (nCount % 5 == 1) {                    // logga var femte — undvik spam
      char tmp[56];
      snprintf(tmp, sizeof(tmp), "notis #%u len=%u [%02X %02X %02X %02X]",
               (unsigned)nCount, (unsigned)len,
               len > 0 ? data[0] : 0, len > 1 ? data[1] : 0,
               len > 2 ? data[2] : 0, len > 3 ? data[3] : 0);
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      st.dbg = tmp;
      xSemaphoreGive(stateMutex);
    }
    return;
  }
  if (nCount == 1) Serial.println("BLE: första statusnotisen mottagen!");
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  // Senaste råramen (första 20 B) i dbg — oskattbar vid protokollfelsökning
  {
    char tmp[100];
    int p = snprintf(tmp, sizeof(tmp), "ram #%u:", (unsigned)nCount);
    for (size_t i = 0; i < len && i < 20 && p < (int)sizeof(tmp) - 3; i++)
      p += snprintf(tmp + p, sizeof(tmp) - p, " %02X", data[i]);
    st.dbg = tmp;
  }
  st.bleConnected = true;
  st.power      = data[4] == 0x01;
  st.stateCode  = data[4];
  st.stateName  = (data[4] < 6) ? STATE_NAMES[data[4]] : ("Kod " + String(data[4]));
  st.modeAuto   = data[5] != 0x00;   // 0=nivå, 1=temperatur
  // LIVE-kartlagda rambetydelser: data[6]=aktuell modulering (nivå-läge) eller
  // måltemp (temp-läge); data[7]=satt nivå (nivå-läge) eller satt temp.
  if (data[5] == 0x00) { st.level = data[7]; st.target = data[6]; }   // nivå-läge
  else                 { st.level = data[7]; st.target = data[6]; }   // temp-läge
  st.voltage    = data[9];
  st.roomTemp   = data[11] - 30;
  st.caseTemp   = (data[12] << 8) | data[13];
  if (data[5] == 0xFF) { st.error = "E" + String(data[6]); }
  else                  { st.error = ""; }
  st.lastUpdate = millis();
  xSemaphoreGive(stateMutex);
}

void wantedApply();   // återställ senast önskad temp efter BLE-uppkoppling (def. längre ner)

bool bleConnect() {
  Serial.print("BLE: söker ");
  Serial.println(HEATER_NAME);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  st.dbg = "skannar";
  xSemaphoreGive(stateMutex);
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);            // KRITISKT: hämta scan response — namnet ligger där!
  scan->setInterval(45);
  scan->setWindow(40);                  // nästan 100 % skanduty för starkast möjlighet
  scan->clearResults();
  BLEScanResults* results = scan->start(12, false);
  BLEAdvertisedDevice foundDev;
  bool have = false;
  int n = results->getCount();
  String matchedBy = "";
  for (int i = 0; i < n; i++) {
    BLEAdvertisedDevice d = results->getDevice(i);
    String nm = d.getName();
    nm.toLowerCase();
    String addr = d.getAddress().toString();
    addr.toLowerCase();
    if (nm == HEATER_NAME) { foundDev = d; have = true; matchedBy = "namn"; break; }
    if (addr == HEATER_ADDR) { foundDev = d; have = true; matchedBy = "adress"; break; }
    if (d.isAdvertisingService(SVC_UUID)) { foundDev = d; have = true; matchedBy = "svc FFF0"; break; }
  }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (!have) {
    st.dbg = "skan: " + String(n) + " enh, ingen match";
    xSemaphoreGive(stateMutex);
    Serial.printf("BLE: hittades inte (%d enheter synliga)\n", n);
    return false;
  }
  lastScanRSSI = foundDev.getRSSI();
  xSemaphoreGive(stateMutex);
  scan->clearResults();               // frigör skanningsminnet — viktigt i retry-loop

  Serial.println("BLE: ansluter ...");
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  st.dbg = "hittad (" + matchedBy + ", RSSI " + String(foundDev.getRSSI()) + ") — ansluter";
  xSemaphoreGive(stateMutex);
  if (!pClient) {
    pClient = BLEDevice::createClient();               // skapas EN gång
    pClient->setClientCallbacks(new ClientCb());       // även callbacken — EN gång
  }
  if (!pClient->connect(foundDev.getAddress(), foundDev.getAddressType())) {
    // INTE disconnect här — bluedroid kan krascha av disconnect efter
    // misslyckad connect. Nästa connect()-försök städar upp GAP-state.
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    st.dbg = "connect misslyckades (RSSI " + String(foundDev.getRSSI()) + ")";
    xSemaphoreGive(stateMutex);
    Serial.println("BLE: anslutning misslyckades");
    return false;
  }
  BLERemoteService* svc = pClient->getService(SVC_UUID);
  if (!svc) {
    pClient->disconnect();
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    st.dbg = "ansluten men service FFF0 saknas";
    xSemaphoreGive(stateMutex);
    Serial.println("BLE: service FFF0 saknas");
    return false;
  }
  pWriteChr = svc->getCharacteristic(CHR_WRITE);
  BLERemoteCharacteristic* pN = svc->getCharacteristic(CHR_NOTIFY);
  if (!pWriteChr || !pN) {
    pWriteChr = nullptr;
    pClient->disconnect();
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    st.dbg = "service ok men karakteristiker saknas";
    xSemaphoreGive(stateMutex);
    Serial.println("BLE: karakteristiker saknas");
    return false;
  }
  const uint8_t both[2] = {1, 0};
  pN->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)both, 2, true);
  pN->registerForNotify(notifyCB);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  st.dbg = "";
  xSemaphoreGive(stateMutex);
  Serial.println("BLE: ansluten!");
  // Statusnotiserna är 21 byte — standard MTU 23 räcker bara till 20 byte.
  // Förhandla upp MTU så notiserna kommer fram (Python-stacken gjorde detta
  // automatiskt, därför fungerade det från PC:n).
  if (pClient->setMTU(100)) Serial.println("BLE: MTU förhandlat");
  else                      Serial.println("BLE: MTU-förhandling MISSLYCKADES");
  sendBleHex(CMD_STATUS_HEX);       // första statusfrågan direkt
  // wantedApply körs INTE här — den kördes vid varje återkoppling och kunde
  // väcka/ställa om värmaren i onödan (t.ex. mitt i app-användning efter
  // "Släpp Bluetooth"). Schemamotorn + manuella val sköter temperaturen i stället.
  return true;
}

void bleTask(void* param) {
  unsigned long backoff = 5000;
  // OBS: använd den GLOBALA bleBlockedUntil — en lokal kopia här skuggar den
  // (gammal bugg): då ignorerar BLE-tasken "Släpp Bluetooth" helt.
  for (;;) {
    if (millis() < bleBlockedUntil) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      st.bleConnected = false;
      st.stateName = "Släppt " + String((bleBlockedUntil - millis()) / 60000 + 1) + " min";
      xSemaphoreGive(stateMutex);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (bleConnect()) {
      backoff = 5000;
      sendBleHex(CMD_STATUS_HEX);
      unsigned long lastPoll = 0;
      while (pClient && pClient->isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (millis() - lastPoll > 3000) {          // hämta status var 3:e sekund
          lastPoll = millis();
          sendBleHex(CMD_STATUS_HEX);
        }
        // kommandon från molnet/webben (kan vara flera, sep med ;)
        if (bleBlockedUntil > millis()) break;   // användaren vill släppa BLE
        if (cmdFromCloud.length()) {
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          String all = cmdFromCloud; cmdFromCloud = "";
          xSemaphoreGive(stateMutex);
          int start = 0;
          while (start < (int)all.length()) {
            int semi = all.indexOf(';', start);
            String one = (semi < 0) ? all.substring(start) : all.substring(start, semi);
            one.trim();
            if (one.length()) sendBleHex(one.c_str());
            if (semi < 0) break;
            start = semi + 1;
            vTaskDelay(pdMS_TO_TICKS(250));
          }
        }
      }
      Serial.println("BLE: kopplingen tappad");
      pWriteChr = nullptr;
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      st.bleConnected = false; st.stateName = "Ej ansluten";
      xSemaphoreGive(stateMutex);
    }
    if (millis() < bleBlockedUntil) continue;    // gå direkt till paus-vakten ovan
    vTaskDelay(pdMS_TO_TICKS(backoff));
    // svag signal (sämre än -90): skanna om sällan — sparar minne/BLE-churn
    backoff = (lastScanRSSI < 0 && lastScanRSSI > -90) ? 5000 : 30000;
    backoff = min(backoff, (unsigned long)30000);
  }
}

// ============================ SCHEMAMOTOR ============================
Preferences prefs;

int minutesNow() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  return t->tm_hour * 60 + t->tm_min;
}
int weekdayNow() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  return (t->tm_wday + 6) % 7;   // 0=måndag ... 6=söndag
}

void schedulesLoad() {
  prefs.begin("varmare", false);
  prefs.getBytes("sched", slots, sizeof(slots));
  // ogiltig/ny flash → rensa: active-flaggan avgör, allt annat nollställs
  for (int i = 0; i < MAX_SCHED; i++)
    if (!slots[i].active) slots[i] = {false, 0, 0, -1, 21};
  prefs.end();
}
void schedulesSave() {
  prefs.begin("varmare", false);
  prefs.putBytes("sched", slots, sizeof(slots));
  prefs.end();
}
int freeSlot() { for (int i = 0; i < MAX_SCHED; i++) if (!slots[i].active) return i; return -1; }
int activeCount() { int n = 0; for (int i = 0; i < MAX_SCHED; i++) if (slots[i].active) n++; return n; }

// ---- önskad temperatur: sparas i flashminnet, återställs när BLE kopplar ----
uint8_t wantedTemp = 21;
bool wantedTempSet = false;          // true först när användaren valt temp
void wantedLoad() {
  prefs.begin("varmare", false);
  wantedTemp = prefs.getUChar("wtemp", 21);
  wantedTempSet = prefs.getBool("wtempSet", false);
  if (wantedTemp < 8 || wantedTemp > 36) { wantedTemp = 21; wantedTempSet = false; }
  prefs.end();
}
void wantedSave(int t) {
  wantedTemp = t; wantedTempSet = true;
  prefs.begin("varmare", false);
  prefs.putUChar("wtemp", (uint8_t)t);
  prefs.putBool("wtempSet", true);
  prefs.end();
}
// Kallas när BLE-kopplingen precis kom ihop: återställ senast önskade temp.
// ABBA-protokollet: skicka sekvensen TVÅ gånger — första skicket växlar läge,
// andra sätter värdet (annars fastnar target på 0 i värmarens status).
void wantedApply() {
  if (!wantedTempSet) return;
  sendSetTemp(wantedTemp);  delay(300);   // lagra värdet först
  sendBleHex(CMD_TEMP_COMMIT);            // sedan aktivera temperatur-läge
  Serial.printf("BLE ikopplad — återställer önskad temp %d °C\n", wantedTemp);
}

// Kör var 15:e sekund. Exakt minutmatch (±0) — med 15 s kontrollintervall
// passerar varje minut minst 3 gånger, så inget tillfälle missas. En minut
// triggas bara en gång per schema. FLaggorna nollställs vid minutbytet —
// annars skulle samma starttid bara funka EN gång någonsin (gammal bugg).
// Sluttid < starttid = fönstret går över midnatt (t.ex. 22:00→06:15):
// stoppet gäller då igångaens start-dag.
void scheduleCheck() {
  static unsigned long lastCheck = 0;
  static int lastSeenMin = -1;
  static int firedOn[MAX_SCHED], firedOff[MAX_SCHED];
  if (millis() - lastCheck < 15000) return;
  if (time(nullptr) < 100000) return;      // vänta på NTP — scheman kräver korrekt klocka
  lastCheck = millis();

  int nowMin = minutesNow();
  int wday = weekdayNow();
  if (nowMin != lastSeenMin) {             // ny minut → nya triggerar tillåts
    lastSeenMin = nowMin;
    for (int i = 0; i < MAX_SCHED; i++) { firedOn[i] = -1; firedOff[i] = -1; }
  }

  for (int i = 0; i < MAX_SCHED; i++) {
    Schedule& s = slots[i];
    if (!s.active || !s.days) continue;

    // START — exakt minut, rätt veckodag. Sekvens (med luft emellan så
    // värmaren hinner registrera varje steg — samma mönster som handleTemp):
    // 1) toggla på  2) lagra önskad temp  3) aktivera temperatur-läge.
    if ((s.days & (1 << wday)) && nowMin == s.onMin && firedOn[i] != nowMin) {
      firedOn[i] = nowMin;
      if (!st.power) {
        sendBleHex(CMD_TOGGLE_HEX);
        delay(800);
        sendSetTemp(s.temp);
        delay(300);
        sendBleHex(CMD_TEMP_COMMIT);   // värmaren väljer temp-läge för 8-36
        Serial.printf("Schema %d: STARTAR %02d:%02d (%d grader)\n", i, s.onMin/60, s.onMin%60, s.temp);
      }
    }
    // STOPP — exakt minut; över-midnatt-fönster kräver att startdagen (igår) matchar
    if (s.offMin >= 0 && nowMin == s.offMin && firedOff[i] != nowMin) {
      int startDay = (s.offMin < s.onMin) ? (wday + 6) % 7 : wday;
      if (s.days & (1 << startDay)) {
        firedOff[i] = nowMin;
        if (st.power) { sendBleHex(CMD_TOGGLE_HEX); Serial.printf("Schema %d: STOPPAR %02d:%02d\n", i, s.offMin/60, s.offMin%60); }
      }
    }
  }
}

// ============================ MQTT-MOLN ============================
void mqttPublishStatus() {
  if (!mqtt.connected()) return;
  char buf[300];
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char tstr[24];
  snprintf(tstr, sizeof(tstr), "%04d-%02d-%02dT%02d:%02d:%02d",
           t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  snprintf(buf, sizeof(buf),
    "{\"connected\":%s,\"power\":%s,\"mode\":\"%s\",\"temp_room\":%d,\"temp_heater\":%d,"
    "\"voltage\":%.1f,\"level\":%d,\"target\":%d,\"state\":\"%s\",\"error\":\"%s\","
    "\"updated\":\"%s\",\"heap\":%u,\"rssi\":%d,\"ver\":\"%s\"}",
    st.bleConnected ? "true" : "false",
    st.power ? "true" : "false",
    st.modeAuto ? "auto" : "level",
    st.roomTemp, st.caseTemp, st.voltage,
    st.level, st.target, st.stateName.c_str(),
    st.error.length() ? st.error.c_str() : "Inget fel", tstr,
    (unsigned)ESP.getFreeHeap(), lastScanRSSI, FW_VERSION);
  xSemaphoreGive(stateMutex);
  mqtt.publish((String(MQTT_TOPIC) + "/status").c_str(), buf, true);
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, len)) return;
  if (strcmp(doc["pwd"] | "", WEB_PASSWORD) != 0) { Serial.println("MQTT: fel lösenord"); return; }
  const char* action = doc["action"] | "";

  if (!strcmp(action, "power")) {
    bool want = doc["on"] | false;
    if (st.power != want) {                       // toggle — bara vid behov
      cmdFromCloud = CMD_TOGGLE_HEX;
    }
  } else if (!strcmp(action, "temp")) {
    int t = doc["value"] | 21;
    if (t < 8 || t > 36) return;
    // två kommandon i tur: lagra temp-värdet + aktivera temperatur-läge
    char buf[32];
    uint8_t raw[7] = {0xba,0xab,0x04,0xdb,(uint8_t)t,0x00,0x00};
    uint8_t ck = 0; for (int i=0;i<7;i++) ck += raw[i];
    snprintf(buf, sizeof(buf), "baab04db%02x0000%02x", t, ck);
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    cmdFromCloud = String(buf) + ";" + String(CMD_TEMP_COMMIT);
    xSemaphoreGive(stateMutex);
  } else if (!strcmp(action, "level")) {
    int l = doc["value"] | 0;
    if (l < 1 || l > 10) return;
    char buf[32];
    uint8_t raw[7] = {0xba,0xab,0x04,0xdb,(uint8_t)l,0x00,0x00};
    uint8_t ck = 0; for (int i=0;i<7;i++) ck += raw[i];
    snprintf(buf, sizeof(buf), "baab04db%02x0000%02x", l, ck);
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    cmdFromCloud = String(buf) + ";" + String(CMD_LEVEL_COMMIT);   // värdet, sedan nivå-läge
    xSemaphoreGive(stateMutex);
  } else if (!strcmp(action, "shutdown")) {
    if (st.power) cmdFromCloud = CMD_TOGGLE_HEX;
  } else if (!strcmp(action, "sched_get")) {
    // Svara med schemalistan på /sched-ämnet (molnsidan läser den)
    String out; schedListJson(out);
    mqtt.publish((String(MQTT_TOPIC) + "/sched").c_str(), out.c_str(), true);
  } else if (!strcmp(action, "sched_add")) {
    int days   = doc["days"] | 0;
    const char* onS  = doc["on"]  | "";
    const char* offS = doc["off"] | "";
    int onM  = parseHM(String(onS));
    int offM = offS[0] ? parseHM(String(offS)) : -1;
    int temp = doc["temp"] | 21;
    if (days && onM >= 0 && offM != onM && temp >= 8 && temp <= 36) {
      int slot = freeSlot();
      if (slot >= 0) {
        slots[slot] = {true, (uint8_t)days, (int16_t)onM, (int16_t)offM, (uint8_t)temp};
        schedulesSave();
      }
    }
    String out; schedListJson(out);
    mqtt.publish((String(MQTT_TOPIC) + "/sched").c_str(), out.c_str(), true);
  } else if (!strcmp(action, "sched_del")) {
    int i = doc["i"] | -1;
    if (i >= 0 && i < MAX_SCHED) {
      slots[i] = {false, 0, 0, -1, 21};
      schedulesSave();
    }
    String out; schedListJson(out);
    mqtt.publish((String(MQTT_TOPIC) + "/sched").c_str(), out.c_str(), true);
  }
}

void mqttLoop() {
  static unsigned long lastReconnect = 0;
  static unsigned long lastPub = 0;
  static String lastPayload;
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (millis() - lastReconnect > 30000) {
      lastReconnect = millis();
      String cid = "varmare-" + String((uint32_t)ESP.getEfuseMac(), HEX);
      mqtt.connect(cid.c_str());
      if (mqtt.connected()) {
        mqtt.subscribe((String(MQTT_TOPIC) + "/cmd").c_str());
        Serial.println("MQTT: ansluten");
      }
    }
    return;
  }
  mqtt.loop();
  if (millis() - lastPub > 5000) {   // status var 5:e sekund
    lastPub = millis();
    mqttPublishStatus();
  }
}

// ============================ WEBB-API ============================
bool authOk() {
  if (web->hasArg("pwd") && web->arg("pwd") == WEB_PASSWORD) return true;
  if (web->hasHeader("Authorization")) {
    String a = web->header("Authorization");
    // Basic auth-header genereras av sidan utifrån VALJT-LOSENORD
    if (a == "Basic dXNlcjpwZXJrZWwx") return true;
  }
  return false;
}

void sendJson(int code, const String& s) {
  web->send(code, "application/json", s);
}

void handleStatus() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  char buf[480];   // rymmer även dbg (råram för felsökning)
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char tstr[24];
  snprintf(tstr, sizeof(tstr), "%04d-%02d-%02dT%02d:%02d:%02d",
           t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  snprintf(buf, sizeof(buf),
    "{\"connected\":%s,\"power\":%s,\"mode\":\"%s\",\"temp_room\":%d,\"temp_heater\":%d,"
    "\"voltage\":%.1f,\"level\":%d,\"target\":%d,\"state\":\"%s\",\"error\":\"%s\",\"updated\":\"%s\",\"heap\":%u,\"rssi\":%d,\"ver\":\"%s\"",
    st.bleConnected ? "true" : "false", st.power ? "true" : "false",
    st.modeAuto ? "auto" : "level",
    st.roomTemp, st.caseTemp, st.voltage, st.level, st.target,
    st.stateName.c_str(), st.error.length() ? st.error.c_str() : "Inget fel", tstr,
    (unsigned)ESP.getFreeHeap(), lastScanRSSI, FW_VERSION);
  if (st.dbg.length()) {
    // dbg kan innehålla tecken som inte är JSON-säkra — rensa citattecken och bakstreck
    String safe = st.dbg; safe.replace("\"", "'"); safe.replace("\\", "/");
    strncat(buf, ",\"dbg\":\"", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, safe.c_str(), sizeof(buf) - strlen(buf) - 1);
    strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
  }
  strncat(buf, "}", sizeof(buf) - strlen(buf) - 1);
  xSemaphoreGive(stateMutex);
  sendJson(200, buf);
}

void handlePower() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false}"); return; }
  bool want = doc["on"] | false;
  if (st.power != want) sendBleHex(CMD_TOGGLE_HEX);
  delay(300);
  sendJson(200, "{\"ok\":true}");
}

// Diagnostik: skicka valfri rå ram till FFF2 (endast lösenordsskyddad).
// body: {"hex":"baab04bbac0000d0"} — checksumma läggs till om den saknas.
void handleRaw() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false}"); return; }
  String hex = doc["hex"] | "";
  hex.trim(); hex.toLowerCase();
  if (hex.length() < 14 || hex.length() % 2 != 0) { sendJson(422, "{\"ok\":false,\"e\":\"hex\"}"); return; }
  if (hex.length() == 14) {                       // komplett checksumma
    uint8_t sum = 0; int cks = 0;
    for (size_t i = 0; i < hex.length(); i += 2) {
      char b[3] = {hex[i], hex[i+1], 0};
      sum += (uint8_t)strtol(b, nullptr, 16); cks++;
    }
    if (cks == 7) hex += String(sum < 16 ? "0" : "") + String(sum, HEX);
  }
  sendBleHex(hex.c_str());
  sendJson(200, "{\"ok\":true,\"sent\":\"" + hex + "\"}");
}

void handleTemp() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false}"); return; }
  int t = doc["value"] | 0;
  if (t < 8 || t > 36) { sendJson(422, "{\"ok\":false,\"e\":\"temp 8-36\"}"); return; }
  wantedSave(t);                    // sparas i flashminnet — överlever omstart
    // LIVE-TESTAT: 0xDB <temp> → 0_AD aktiverar temperatur-läge med målet
    sendSetTemp(t);
    delay(300);
    sendBleHex(CMD_TEMP_COMMIT);
    sendJson(200, "{\"ok\":true}");
}

void handleLevel() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false}"); return; }
  int l = doc["value"] | 0;
  if (l < 1 || l > 10) { sendJson(422, "{\"ok\":false,\"e\":\"level 1-10\"}"); return; }
  // LIVETESTAT: nivån stegas ±1 med 0xA2/0xA3 — 0xDB-verdet sätter bara mål
  // i temp-läget. Stega från aktuell nivå (data[6]) till önskad.
  int cur = st.target;   // i temp-läge = måltemp; i nivå-läge = aktuell modulering
  if (cur < 1 || cur > 10) cur = 5;
  int steps = l - cur;
  int n = steps < 0 ? -steps : steps;
  if (n > 12) n = 12;
  for (int k = 0; k < n; k++) {
    sendBleHex(steps > 0 ? CMD_LEVEL_UP : CMD_LEVEL_DOWN);
    delay(400);
  }
  sendJson(200, "{\"ok\":true}");
}

void handleShutdown() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  if (st.power) sendBleHex(CMD_TOGGLE_HEX);
  sendJson(200, "{\"ok\":true}");
}

// Släpp Bluetooth: koppla från ESP32:n i N minuter så att AirHeaterPro-appen
// kan använda värmaren. ESP32:n återansluter automatiskt efter tiden.
void handleBleRelease() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<96> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false}"); return; }
  long mins = doc["mins"] | 10;
  if (mins < 1 || mins > 120) { sendJson(422, "{\"ok\":false,\"e\":\"mins 1-120\"}"); return; }
  bleBlockedUntil = millis() + (unsigned long)mins * 60000UL;
  if (pClient && pClient->isConnected()) pClient->disconnect();   // släpp direkt
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  st.bleConnected = false; st.stateName = "Släppt " + String(mins) + " min";
  xSemaphoreGive(stateMutex);
  Serial.printf("BLE: släppt i %ld min (app kan ansluta)\n", mins);
  sendJson(200, "{\"ok\":true}");
}

void handleBleReleaseStatus() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  unsigned long left = (bleBlockedUntil > millis()) ? (bleBlockedUntil - millis()) / 1000 : 0;
  char buf[48];
  snprintf(buf, sizeof(buf), "{\"blocked\":%lu}", (unsigned long)left);
  sendJson(200, buf);
}

// ============================ SCHEMA-API ============================
void schedListJson(String& out) {
  out = "{\"n\":" + String(activeCount()) + ",\"max\":" + String(MAX_SCHED) + ",\"schedules\":[";
  bool first = true;
  for (int i = 0; i < MAX_SCHED; i++) {
    Schedule& s = slots[i];
    if (!s.active) continue;
    if (!first) out += ",";
    first = false;
    char onT[8], offT[8] = "";
    snprintf(onT, sizeof(onT), "%02d:%02d", s.onMin / 60, s.onMin % 60);
    if (s.offMin >= 0) snprintf(offT, sizeof(offT), "%02d:%02d", s.offMin / 60, s.offMin % 60);
    out += "{\"i\":" + String(i) + ",\"days\":" + String(s.days) +
           ",\"on\":\"" + onT + "\",\"off\":\"" + offT + "\",\"temp\":" + String(s.temp) + "}";
  }
  out += "]}";
}

int parseHM(const String& s) {          // "HH:MM" (24h) → minuter, -1 vid fel
  int c = s.indexOf(':');
  if (c != 2) return -1;
  int h = s.substring(0, c).toInt(), m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

void handleSchedules() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  String out; schedListJson(out); sendJson(200, out);
}

void handleScheduleAdd() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false,\"e\":\"json\"}"); return; }
  int days   = doc["days"] | 0;
  const char* onS  = doc["on"]  | "";
  const char* offS = doc["off"] | "";
  int onM  = parseHM(String(onS));
  int offM = offS[0] ? parseHM(String(offS)) : -1;
  int temp = doc["temp"] | 21;
  if (!days)                    { sendJson(422, "{\"ok\":false,\"e\":\"välj dagar\"}"); return; }
  if (onM < 0)                  { sendJson(422, "{\"ok\":false,\"e\":\"on HH:MM\"}"); return; }
  if (offM >= 0 && offM == onM) { sendJson(422, "{\"ok\":false,\"e\":\"av och på kan inte vara samma minut\"}"); return; }
  // offM < onM är TILLÅTET — betyder att fönstret går över midnatt (t.ex. 22:00→06:15)
  if (temp < 8 || temp > 36)    { sendJson(422, "{\"ok\":false,\"e\":\"temp 8-36\"}"); return; }
  int slot = freeSlot();
  if (slot < 0)                 { sendJson(422, "{\"ok\":false,\"e\":\"max 8 scheman\"}"); return; }
  slots[slot] = {true, (uint8_t)days, (int16_t)onM, (int16_t)offM, (uint8_t)temp};
  schedulesSave();
  String out; schedListJson(out); sendJson(200, out);
}

void handleScheduleDel() {
  if (!authOk()) { web->send(401, "text/plain", "Låst"); return; }
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, web->arg("plain"))) { sendJson(400, "{\"ok\":false}"); return; }
  int i = doc["i"] | -1;
  if (i < 0 || i >= MAX_SCHED) { sendJson(422, "{\"ok\":false,\"e\":\"index\"}"); return; }
  slots[i] = {false, 0, 0, -1, 21};
  schedulesSave();
  String out; schedListJson(out); sendJson(200, out);
}

// ============================ WEBB-UI ============================
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Dieselvärmare</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; background: #0b1220; color: #e2e8f0; margin: 0; padding: 1.2rem; max-width: 520px; margin-inline: auto; }
  h1 { font-size: 1.3rem; display: flex; align-items: center; gap: .7rem; }
  .pill { padding: .15rem .7rem; border-radius: 999px; font-size: .8rem; border: 1px solid #35456b; }
  .on  { background: #14532d; color: #86efac; border-color: #16a34a; }
  .off { background: #450a0a; color: #fca5a5; border-color: #b91c1c; }
  .cards { display: grid; grid-template-columns: 1fr 1fr; gap: .7rem; margin-top: 1rem; }
  .card { background: #16213a; border: 1px solid #24304d; border-radius: 12px; padding: .9rem 1.1rem; }
  .label { color: #7c8db0; font-size: .72rem; text-transform: uppercase; letter-spacing: .06em; }
  .value { font-size: 1.9rem; font-weight: 700; margin-top: .2rem; }
  button { font: inherit; background: #24304d; color: #e2e8f0; border: 1px solid #35456b; border-radius: 10px; padding: .6rem 1rem; cursor: pointer; }
  button.primary { background: #2563eb; border-color: #2563eb; }
  button.fire { background: #b45309; border-color: #b45309; }
  button.danger { background: #b91c1c; border-color: #b91c1c; }
  .row { display: flex; gap: .6rem; margin: .9rem 0; }
  .row button { flex: 1; }
  .tp { display: flex; flex-wrap: wrap; gap: 5px; margin-top: .6rem; }
  .tp button.sel { background: #38bdf8; color: #0f172a; border-color: #38bdf8; }
  .days { display: flex; gap: 4px; justify-content: center; margin-top: 8px; flex-wrap: wrap; }
  .days button { width: 40px; padding: .5rem 0; border-radius: 8px; font-weight: 700; }
  .days button.sel { background: #38bdf8; color: #0f172a; border-color: #38bdf8; }
  input[type=time], select { font: inherit; background: #0b1220; color: #e2e8f0; border: 1px solid #35456b; border-radius: 8px; padding: .45rem; }
  .sched-item { display: flex; justify-content: space-between; align-items: center; background: #16213a; border: 1px solid #24304d; border-radius: 10px; padding: .6rem .8rem; margin: .45rem 0; font-size: .95em; text-align: left; }
  .sched-item button { padding: .25rem .6rem; }
  .tp button { width: 44px; height: 38px; border-radius: 8px; padding: 0; font-weight: 700; }
  .tp button.sel { background: #38bdf8; color: #0f172a; border-color: #38bdf8; }
  h2 { font-size: .95rem; color: #93a4c4; margin: 1.2rem 0 .4rem; }
  .small { color: #7c8db0; font-size: .78rem; }
  #log { background: #0b1220; border: 1px solid #24304d; border-radius: 10px; padding: .7rem; font: 11px/1.5 ui-monospace, monospace; color: #93a4c4; height: 110px; overflow-y: auto; }
</style>
</head>
<body>
<h1>🔥 Dieselvärmare <span id="conn" class="pill off">—</span></h1>
<div class="cards">
  <div class="card"><div class="label">Inne</div><div class="value" id="tRoom">–</div></div>
  <div class="card"><div class="label">Värmare</div><div class="value" id="tHeat">–</div></div>
  <div class="card"><div class="label">Batteri</div><div class="value" id="volt">–</div></div>
  <div class="card"><div class="label">Status</div><div class="value" id="state" style="font-size:1.2rem">–</div></div>
</div>
<div class="row">
  <button class="fire" onclick="post('/api/power',{on:true})">▶ Starta</button>
  <button onclick="post('/api/power',{on:false})">■ Stäng av</button>
  <button class="danger" onclick="post('/api/shutdown')">🛑</button>
</div>
<div class="row">
  <button onclick="releaseBle()">📲 Släpp Bluetooth (app i 10 min)</button>
</div>
<h2>Måltemperatur</h2>
<div class="tp" id="tp"></div>
<h2>Effektnivå</h2>
<div class="tp" id="lp"></div>
<h2>⏰ Schemaläggning</h2>
<div id="schedList"></div>
<div class="card" id="schedForm">
  <div class="small">Nytt schema — välj veckodagar, starttid och valfri sluttid (24h):</div>
  <div class="days" id="dayBtns"></div>
  <div style="display:flex;gap:8px;align-items:center;justify-content:center;flex-wrap:wrap;margin-top:.6rem">
    <span class="small">På</span> <input type="time" id="onTime" value="06:30">
    <span class="small">Av</span> <input type="time" id="offTime">
    <label class="small"><input type="checkbox" id="noOff"> ingen</label>
    <select id="tempSel"></select>
  </div>
  <button class="primary" style="width:100%;margin-top:.6rem" onclick="addSched()">+ Lägg till schema</button>
  <button style="width:100%;margin-top:.4rem" onclick="alwaysOn()">♻ Dygnet runt (alla dagar 00:00, ingen stopptid)</button>
  <div class="small">Sluttid före starttid = över midnatt (t.ex. 22:00→06:15). Sparas i ESP32:ns minne — gäller även efter strömavbrott. Max 8.</div>
</div>
<h2>⬆ Firmware (OTA)</h2>
<div class="card">
  <div class="small">Uppdatera kortet trådlöst — ingen USB behövs. Välj .bin-fil (t.ex. <b>varmare.ino.bin</b>) och ladda upp. Kortet startar om automatiskt efteråt.</div>
  <form method="POST" action="/update" enctype="multipart/form-data" style="display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:.6rem">
    <input type="password" name="pass" placeholder="OTA-lösenord" size="10">
    <input type="file" name="fw" accept=".bin">
    <button class="primary">Ladda upp</button>
  </form>
  <div class="small">Nuvarande version: <b id="fwver">–</b></div>
</div>
<h2>Logg</h2>
<div id="log"></div>
<script>
// Lösenord från sessionen — aldrig i adressfältet. ?pwd= accepteras men
// plockas omedelbart bort från URL:en så det inte syns/lagras offentligt.
let PWD = sessionStorage.getItem('hp') || '';
(function(){ const qp = new URLSearchParams(location.search).get('pwd');
  if (qp) { PWD = qp; sessionStorage.setItem('hp', qp); history.replaceState(null, '', '/'); }
})();
if (!PWD) { PWD = prompt('Lösenord:') || ''; if (PWD) sessionStorage.setItem('hp', PWD); }
const q = '?pwd=' + encodeURIComponent(PWD);
async function api(p, body) {
  const r = await fetch(p + q, body ? {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)} : {});
  if (r.status === 401) { sessionStorage.removeItem('hp'); location.reload(); throw 0; }
  return r.json().catch(()=>({}));
}
async function post(p, b) { await api(p, b); refresh(); }
async function refresh() {
  try {
    const s = await api('/api/status');
    const conn = document.getElementById('conn');
    conn.textContent = s.connected ? 'ansluten' : 'ej ansluten';
    conn.className = 'pill ' + (s.connected ? 'on' : 'off');
    document.getElementById('tRoom').textContent = s.temp_room > -50 ? s.temp_room + ' °C' : '–';
    document.getElementById('tHeat').textContent = s.temp_heater > -50 ? s.temp_heater + ' °C' : '–';
    document.getElementById('volt').textContent  = s.voltage ? s.voltage + ' V' : '–';
    document.getElementById('state').textContent = s.power ? '🔥 ' + s.state : '⚫ ' + s.state;
    document.getElementById('state').style.color = s.power ? '#4ade80' : '#93a4c4';
    const fv = document.getElementById('fwver'); if (fv) fv.textContent = s.ver || '?';
  } catch (e) {}
}
const tp = document.getElementById('tp');
for (let t = 8; t <= 36; t++) {
  const b = document.createElement('button'); b.textContent = t;
  b.onclick = () => { post('/api/temp', {value: t}); [...tp.children].forEach(c=>c.classList.remove('sel')); b.classList.add('sel'); };
  tp.appendChild(b);
}
const lp = document.getElementById('lp');
for (let l = 1; l <= 10; l++) {
  const b = document.createElement('button'); b.textContent = l;
  b.onclick = () => { post('/api/level', {value: l}); [...lp.children].forEach(c=>c.classList.remove('sel')); b.classList.add('sel'); };
  lp.appendChild(b);
}
// veckodagar + 24h-tider för scheman
const DAYS = ['Må','Ti','On','To','Fr','Lö','Sö'];
let schedDays = 0;
const db = document.getElementById('dayBtns');
DAYS.forEach((d, i) => {
  const b = document.createElement('button'); b.textContent = d;
  b.onclick = () => { schedDays ^= (1 << i); b.classList.toggle('sel', !!(schedDays & (1 << i))); };
  db.appendChild(b);
});
const ts = document.getElementById('tempSel');
for (let t = 8; t <= 36; t++) { const o = document.createElement('option'); o.value = t; o.textContent = t + ' °C'; ts.appendChild(o); }
ts.value = 21;
async function refreshSched() {
  try {
    const s = await api('/api/schedules');
    const el = document.getElementById('schedList'); el.innerHTML = '';
    (s.schedules || []).forEach(sc => {
      const row = document.createElement('div'); row.className = 'sched-item';
      const dn = DAYS.map((d, i) => (sc.days & (1 << i)) ? d : '·').join(' ');
      const span = document.createElement('span');
      span.innerHTML = '<b>' + sc.on + '</b>–' + (sc.off || '—') + ' · ' + sc.temp + ' °C<br><span style="font-size:.8em;color:#7c8db0">' + dn + '</span>';
      row.appendChild(span);
      const del = document.createElement('button'); del.textContent = '🗑';
      del.onclick = () => delSched(sc.i);
      row.appendChild(del);
      el.appendChild(row);
    });
    if (!(s.schedules || []).length) el.innerHTML = '<div class="small">Inga scheman än — lägg till nedan.</div>';
  } catch (e) {}
}
async function addSched() {
  const on = document.getElementById('onTime').value;
  const off = document.getElementById('noOff').checked ? '' : document.getElementById('offTime').value;
  if (!on) { alert('Välj starttid.'); return; }
  if (!schedDays) { alert('Välj minst en veckodag.'); return; }
  const r = await api('/api/schedules', { days: schedDays, on: on, off: off, temp: parseInt(ts.value) });
  if (r.e) { alert(r.e); return; }
  refreshSched();
}
function alwaysOn() {
  schedDays = 127;
  [...db.children].forEach((b, i) => b.classList.toggle('sel', true));
  document.getElementById('onTime').value = '00:00';
  document.getElementById('noOff').checked = true;
}
async function delSched(i) {
  await api('/api/schedules/del', { i: i });
  refreshSched();
}
async function releaseBle() {
  await post('/api/ble/release', { mins: 10 });
  setTimeout(refresh, 500);
}
refreshSched();
refresh(); setInterval(refresh, 3000);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  web->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  web->sendHeader("Pragma", "no-cache");
  if (!authOk()) {
    // Låssida med riktig lösenordsruta (går via ?pwd= — samma auth som API:t)
    web->send(401, "text/html",
      "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<body style='font-family:system-ui;background:#0b1220;color:#e2e8f0;padding:3rem;text-align:center'>"
      "<h1>🔥 Dieselvärmare</h1><p>Ange lösenord:</p>"
      "<input id=p type=password inputmode=text style='font:inherit;padding:.6rem;border-radius:8px;border:1px solid #35456b;background:#16213a;color:#e2e8f0' "
      "onkeydown='if(event.key==\"Enter\")go()'> "
      "<button style='font:inherit;padding:.6rem 1rem;border-radius:8px;border:1px solid #2563eb;background:#2563eb;color:#fff;cursor:pointer' onclick='go()'>Öppna</button>"
      "<script>function go(){var v=document.getElementById('p').value;if(v){sessionStorage.setItem('hp',v);location.replace('/');}}</script>"
      "</body>");
    return;
  }
  String html = FPSTR(PAGE);
  web->send(200, "text/html", html);
}

void registerRoutes(WebServer& s) {
  s.enableCORS(true);
  s.on("/", [&s]() { web = &s; handleRoot(); });
  s.on("/api/status", [&s]() { web = &s; handleStatus(); });
  s.on("/api/power", HTTP_POST, [&s]() { web = &s; handlePower(); });
  s.on("/api/temp", HTTP_POST, [&s]() { web = &s; handleTemp(); });
  s.on("/api/raw", HTTP_POST, [&s]() { web = &s; handleRaw(); });   // diagnostik
  s.on("/api/level", HTTP_POST, [&s]() { web = &s; handleLevel(); });
  s.on("/api/shutdown", HTTP_POST, [&s]() { web = &s; handleShutdown(); });
  s.on("/api/ble/release", HTTP_POST, [&s]() { web = &s; handleBleRelease(); });
  s.on("/api/ble/release", HTTP_GET, [&s]() { web = &s; handleBleReleaseStatus(); });
  s.on("/api/schedules", HTTP_GET,  [&s]() { web = &s; handleSchedules(); });
  s.on("/api/schedules", HTTP_POST, [&s]() { web = &s; handleScheduleAdd(); });
  s.on("/api/schedules/del", HTTP_POST, [&s]() { web = &s; handleScheduleDel(); });

  s.on("/update", HTTP_GET, [&s]() {
    s.send_P(200, "text/html",
      "<!DOCTYPE html><html lang='sv'><head><meta charset='UTF-8'><title>OTA</title></head>"
      "<body style='font-family:system-ui;background:#0f172a;color:#f8fafc;text-align:center;padding:2rem'>"
      "<h2>⚡ Uppdatera firmware (varmare)</h2><form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='password' name='pass' placeholder='Lösenord'><br><br>"
      "<input type='file' name='fw' accept='.bin'><br><br>"
      "<button>Ladda upp</button></form></body></html>");
  });
  s.on("/update", HTTP_POST, [&s]() {
    s.send(200, "text/plain", Update.hasError() ? "FEL" : "OK - startar om...");
    delay(800);
    ESP.restart();
  }, [&s]() {
    HTTPUpload& up = s.upload();
    if (up.status == UPLOAD_FILE_START) {
      if (s.arg("pass") != OTA_PASSWORD) { Update.abort(); return; }
      // Bara app-partitionen tar emot firmware (Eagle.Flash.Mode i .bin:en
      // kan lura Update att räkna fel och avbryta med "ERROR[UPDATE_ERROR_SIZE]")
      uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketch)) { Update.abort(); return; }
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) { Update.abort(); }
    } else if (up.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
  });
}

void setupWeb() {
  registerRoutes(server);      // port 80
  registerRoutes(server8081);  // port 8081 — samma sida/API/OTA
  server.begin();
  server8081.begin();
}

// ============================ SETUP/LOOP ============================
void setup() {
  Serial.begin(115200);
  delay(500);
  stateMutex = xSemaphoreCreateMutex();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? " ansluten: " + WiFi.localIP().toString() : " MISSLYCKADES");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);

  // BLE som klient (eget task, högsta prio får inte svälta WiFi)
  BLEDevice::init("VarmareESP32");
  // Max sändningseffekt (+20 dBm mot standard +9) — trång BLE-räckvidd mot värmaren:
  // gäller både annonsskanningen och själva anslutningen.
  BLEDevice::setPower(ESP_PWR_LVL_P20, ESP_BLE_PWR_TYPE_DEFAULT);
  BLEDevice::setPower(ESP_PWR_LVL_P20, ESP_BLE_PWR_TYPE_CONN_HDL0);
  xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 1, nullptr, 0);   // kärna 0

  schedulesLoad();     // scheman från flashminnet — överlever omstart
  wantedLoad();        // senast önskad temperatur från flashminnet
  setupWeb();

  ArduinoOTA.setHostname(MDNS_NAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setClient(mqttTcp);
  mqtt.setBufferSize(512);   // status-JSON med ver/heap/rssi ryms inte i standard-256 — publicering tystnade

  Serial.println("Värmare-ESP32 klar: http://" + WiFi.localIP().toString() + "/ och :8081/ (http://" + String(MDNS_NAME) + ".local)");
}

void loop() {
  server.handleClient();
  server8081.handleClient();
  ArduinoOTA.handle();
  mqttLoop();
  scheduleCheck();
  delay(5);
}

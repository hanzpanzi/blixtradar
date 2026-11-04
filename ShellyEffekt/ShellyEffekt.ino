// =====================================================================
//  Husets effekt – ESP32-S3 läser en Shelly Gen1-effektmätare
// =====================================================================
//  Kortet och Shelly-mätaren måste vara på samma nätverk. Kortet hämtar
//  Shellyns /status var 2:a sekund och visar:
//
//    • Mätarens totala förbrukning (meters[].total / emeters[].total)
//    • Förbrukning sedan kortet startade (integrerad i firmware)
//    • Rullande senaste 60 minuterna
//    • Dagens förbrukning (nollställs vid midnatt, lokal tid)
//    • Momentan effekt, spänning och ström (om mätaren rapporterar det)
//    • Stapeldiagram för de senaste 24 timmarna
//
//  Inloggning krävs för alla sidor och API:er. Sessionen ligger i en
//  HttpOnly-cookie. Standardkonto: admin / perkele (byt i webbsidan).
//
//  Fjärråtkomst: fyll i DuckDNS-domän + token i webbsidan, öppna sedan
//  port 80 i routern mot kortets IP. Då nås sidan på
//  http://<namn>.duckdns.org  – med inloggning.
//
//  Webbsida:  http://husetseffekt.local   (eller kortets IP)
//  JSON:      /api/power   /api/history   /api/config   /api/remote
//  OTA:       /update  (inloggning + OTA-lösenord)
// =====================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

// ==========================================
// 1. WI-FI & GRUNDINSTÄLLNINGAR
// ==========================================
// FORCE_AP_MODE = 1 -> kortet startar ALLTID eget Wi-Fi (Husets effekt)
// FORCE_AP_MODE = 0 -> försöker ansluta till hem-Wi-Fi först
#define FORCE_AP_MODE 0

const char* WIFI_SSID     = "getingbo";
const char* WIFI_PASSWORD = "bykt1f8c";

const char* AP_SSID = "Husets effekt";
const char* AP_PASS = "12345678";

const char* MDNS_NAME   = "husetseffekt";      // http://husetseffekt.local
const char* OTA_PASSWORD = "perkele";
#include "live_page.h"   // live-sidan som kortet serverar pa / (genererad fran husetseffekt.html)

const char* FW_VERSION  = "1.6";

// MQTT till molnsidan (samma publika broker som Blixtradar använder).
// Den publika sidan hanzpanzi.github.io/blixtradar/husetseffekt.html lyssnar här.
const char*    MQTT_HOST  = "broker.emqx.io";
const uint16_t MQTT_PORT  = 1883;
const char*    MQTT_TOPIC = "husetseffekt/hanzpanzi/7d41c9/live";
const char*    MQTT_CLIENT_ID = "husetseffekt-esp32";
const uint32_t MQTT_PUB_MS  = 2000;

// Standardkonto för webbsidan (ändras i webbsidan, sparas i flash)
const char* DEFAULT_WEB_USER = "hanzpanzi";
const char* DEFAULT_WEB_PASS = "SixtenCozmo2026!";

// Standardadress om inget sparats i NVS än. Ändra här eller i webbsidan.
// Exempel: "192.168.1.50", "shellyplug-s-1a2b3c.local" eller "10.0.0.5:80"
const char* DEFAULT_SHELLY_HOST = "192.168.0.154";

// Shelly Gen1-login (krävs när "Restrict login" är påslaget)
const char* DEFAULT_SHELLY_USER = "hanzpanzi";
const char* DEFAULT_SHELLY_PASS = "SixtenCozmo2026!";

// Tidszon för "idag" och timdiagrammet (Sverige)
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";

const uint32_t POLL_MS         = 2000;   // fråga Shelly var 2:a sekund
const uint32_t WIFI_RETRY_MS   = 15000;  // nytt anslutningsförsök medan kortet är offline
const int      WIFI_WEAK_RSSI  = -78;    // under detta: leta efter starkare accesspunkt
const uint32_t WIFI_REBOOT_MS  = 30UL * 60UL * 1000UL;  // så länge offline innan kortet startas om (ej med klient på AP:t)
const uint32_t POLL_FAIL_MS    = 5000;   // väntetid efter misslyckad läsning
const uint32_t HTTP_TIMEOUT_MS = 1500;
const uint32_t DDNS_INTERVAL_MS = 5UL * 60UL * 1000UL;   // DuckDNS var 5:e min
const uint32_t SESSION_IDLE_MS  = 12UL * 60UL * 60UL * 1000UL;  // 12 h

// ==========================================
// 2. TILLSTÅND (skyddas av mutex – läses från webbläsaren i annan task)
// ==========================================
#define MIN_SLOTS  60    // en minut per slot -> rullande 1 h
#define HOUR_SLOTS 24    // en timme per slot -> 24 h-graf
#define LIVE_SLOTS 150   // 150 x 2 s = 5 min live-effekt

struct State {
  // konfiguration
  String host;
  String user;
  String pass;

  // senaste läsning
  bool     online     = false;   // senaste polllyckades
  bool     valid      = false;   // har vi någonsin fått ett bra svar?
  uint16_t fails      = 0;
  uint32_t rttMs      = 0;
  uint32_t lastTryMs  = 0;
  uint32_t lastOkMs   = 0;
  String   err        = "startar";

  float    powerW     = 0;
  float    voltageV   = 0;
  float    currentA   = 0;
  float    tempC      = NAN;
  double   totalWh    = 0;       // mätarens totala energi
  double   returnedWh = 0;       // mätarens total_returned (sol/export)
  bool     relayOn    = false;

  // per fas/kanal (Shelly 3EM rapporterar tre)
  uint8_t  phases     = 0;
  float    phW[3]     = {0, 0, 0};
  float    phV[3]     = {0, 0, 0};
  float    phA[3]     = {0, 0, 0};
  double   phWh[3]    = {0, 0, 0};

  // energiräkning
  double   whBoot     = 0;       // sedan start
  double   whHour1    = 0;       // rullande 60 min
  double   whToday    = 0;       // sedan midnatt
  float    whMinute[MIN_SLOTS];
  uint8_t  minuteIdx  = 0;
  uint32_t minuteStartMs = 0;

  // 24 h-buckets
  float    whHour[HOUR_SLOTS];
  int32_t  hourKey[HOUR_SLOTS];   // yday*24 + timme
  int8_t   hourLabel[HOUR_SLOTS]; // 0-23, -1 = tom
  int8_t   curSlot     = -1;
  int32_t  lastHourKey = -1;
  int32_t  dayKey      = -1;
};

struct RemoteState {
  String   sub;                  // duckdns-subdomän (utan .duckdns.org)
  String   token;                // duckdns-token
  String   wanIp   = "-";
  String   msg     = "ej konfigurerad";
  bool     ok      = false;
  bool     force   = false;
  uint32_t lastCheckMs = 0;
};

static State st;
static RemoteState rs;

// Live-buffer för momentan effekt (skyddas av stateMux)
static float    liveW[LIVE_SLOTS];
static float    liveV[LIVE_SLOTS];
static float    livePh[3][LIVE_SLOTS];
static uint8_t  liveIdx   = 0;
static uint16_t liveCount = 0;
static SemaphoreHandle_t stateMux  = nullptr;
static SemaphoreHandle_t remoteMux = nullptr;
static Preferences prefs;
static WebServer web(80);

// MQTT-klient som skickar livedata till molnsidan
static WiFiClient   mqttNet;
static PubSubClient mqttClient;
static uint32_t     lastMqttTry = 0;
static uint32_t     lastMqttPub = 0;

// Inloggning (lagras som salt + SHA-256, aldrig klartext)
static String webUser;
static String webSalt;
static String webHash;

// Wi-Fi-uppgifterna sparas i flash sa att kortet kan flyttas till ett annat
// nat utan USB. WIFI_SSID/WIFI_PASSWORD ar bara forval vid forsta starten.
static String wifiSsid;
static String wifiPass;
static bool   apWanted = false;   // anvandaren har begart eget nat via /wifi

// ==========================================
// 3. HJÄLPFUNKTIONER
// ==========================================
static void resetArrays() {
  for (int i = 0; i < MIN_SLOTS; i++) st.whMinute[i] = 0;
  for (int i = 0; i < HOUR_SLOTS; i++) {
    st.whHour[i]    = 0;
    st.hourKey[i]   = -1;
    st.hourLabel[i] = -1;
  }
  st.curSlot = -1;
  st.lastHourKey = -1;
  st.dayKey = -1;
  st.minuteIdx = 0;
  st.minuteStartMs = 0;
  st.whBoot = st.whHour1 = st.whToday = 0;

  for (int i = 0; i < LIVE_SLOTS; i++) {
    liveW[i] = 0;
    liveV[i] = 0;
    livePh[0][i] = livePh[1][i] = livePh[2][i] = 0;
  }
  liveIdx = 0;
  liveCount = 0;
}

// Bara tecken som får förekomma i en host (skyddar JSON/HTML mot skräp)
static String sanitizeHost(String h) {
  h.trim();
  h.toLowerCase();
  if (h.startsWith("http://"))  h = h.substring(7);
  if (h.startsWith("https://")) h = h.substring(8);
  int slash = h.indexOf('/');
  if (slash >= 0) h = h.substring(0, slash);
  String out;
  for (size_t i = 0; i < h.length(); i++) {
    char c = h[i];
    if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == ':' || c == '_') out += c;
  }
  return out;
}

static String sanitizeSub(String s) {
  s.trim();
  s.toLowerCase();
  int dot = s.indexOf('.');
  if (dot > 0) s = s.substring(0, dot);
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-') out += c;
  }
  return out;
}

static String sanitizeToken(String s) {
  s.trim();
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-') out += c;
  }
  return out;
}

static String randomHex(int bytes) {
  static const char* HEXCHARS = "0123456789abcdef";
  String out;
  for (int i = 0; i < bytes; i++) {
    uint8_t b = (uint8_t)(esp_random() & 0xFF);
    out += HEXCHARS[b >> 4];
    out += HEXCHARS[b & 0x0F];
  }
  return out;
}

static String sha256hex(const String& in) {
  unsigned char out[32];
  mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), out, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", out[i]);
  hex[64] = 0;
  return String(hex);
}

static bool ctEq(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t r = 0;
  for (size_t i = 0; i < a.length(); i++) r |= (uint8_t)(a[i] ^ b[i]);
  return r == 0;
}

// Roterar minutringbufferten (måste hållas inom mutex)
static void rotateMinutes(uint32_t now) {
  if (st.minuteStartMs == 0) { st.minuteStartMs = now; return; }
  uint8_t guard = 0;
  while ((uint32_t)(now - st.minuteStartMs) >= 60000UL && guard++ < MIN_SLOTS) {
    st.whHour1 -= st.whMinute[st.minuteIdx];
    st.whMinute[st.minuteIdx] = 0;
    st.minuteIdx = (st.minuteIdx + 1) % MIN_SLOTS;
    st.minuteStartMs += 60000UL;
  }
  if (st.whHour1 < 0) st.whHour1 = 0;
}

// Håller tim- och dygnsbuckets i takt med klockan (måste hållas inom mutex)
static void updateClockBuckets() {
  time_t t = time(nullptr);
  if (t < 1700000000) return;                 // NTP ej synkad än

  struct tm lt;
  localtime_r(&t, &lt);

  int32_t dayKey  = (int32_t)(lt.tm_year + 1900) * 400 + lt.tm_yday;
  int32_t hourKey = (int32_t)lt.tm_yday * 24 + lt.tm_hour;

  if (st.dayKey < 0 || st.dayKey != dayKey) {
    if (st.dayKey >= 0) st.whToday = 0;        // nytt dygn
    st.dayKey = dayKey;
  }

  if (st.lastHourKey < 0) {
    st.curSlot = 0;
    st.hourKey[st.curSlot]   = hourKey;
    st.hourLabel[st.curSlot] = (int8_t)lt.tm_hour;
    st.whHour[st.curSlot]    = 0;
    st.lastHourKey = hourKey;
  } else if (hourKey != st.lastHourKey) {
    uint8_t guard = 0;
    while (st.lastHourKey != hourKey && guard++ < HOUR_SLOTS) {
      st.lastHourKey++;
      st.curSlot = (st.curSlot + 1) % HOUR_SLOTS;
      st.hourKey[st.curSlot]   = st.lastHourKey;
      st.hourLabel[st.curSlot] = (int8_t)(((st.lastHourKey % 24) + 24) % 24);
      st.whHour[st.curSlot]    = 0;
    }
    st.lastHourKey = hourKey;
  }
}

// Lägger till energi (måste hållas inom mutex)
static void addEnergy(double dWh) {
  st.whBoot  += dWh;
  st.whToday += dWh;
  st.whHour1 += dWh;
  st.whMinute[st.minuteIdx] += (float)dWh;
  if (st.curSlot >= 0) st.whHour[st.curSlot] += (float)dWh;
}

// ==========================================
// 4. INLOGGNING / SESSIONER
// ==========================================
#define MAX_SESSIONS 4
#define MAX_LOGIN_FAILS 5
#define LOGIN_LOCK_MS 60000UL

struct Session {
  String   token;
  uint32_t lastUse = 0;
};

static Session sessions[MAX_SESSIONS];
static uint8_t loginFails = 0;
static uint32_t loginLockUntil = 0;

static int findSession(const String& tok) {
  if (tok.length() == 0) return -1;
  uint32_t now = millis();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token.length() == 0) continue;
    if (now - sessions[i].lastUse > SESSION_IDLE_MS) { sessions[i].token = ""; continue; }
    if (ctEq(sessions[i].token, tok)) return i;
  }
  return -1;
}

static String createSession() {
  String tok = randomHex(16);
  int slot = -1;
  uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token.length() == 0) { slot = i; break; }
    if (sessions[i].lastUse < oldest) { oldest = sessions[i].lastUse; slot = i; }
  }
  if (slot < 0) slot = 0;
  sessions[slot].token = tok;
  sessions[slot].lastUse = millis();
  return tok;
}

static void clearSessions() {
  for (int i = 0; i < MAX_SESSIONS; i++) sessions[i].token = "";
}

static String cookieToken() {
  if (!web.hasHeader("Cookie")) return "";
  String c = web.header("Cookie");
  int i = c.indexOf("sesid=");
  if (i < 0) return "";
  String tok = c.substring(i + 6);
  int semi = tok.indexOf(';');
  if (semi >= 0) tok = tok.substring(0, semi);
  tok.trim();
  return tok;
}

static bool isAuthed() {
  int idx = findSession(cookieToken());
  if (idx < 0) return false;
  sessions[idx].lastUse = millis();
  return true;
}

// Skickar 401 (API) eller omdirigerar till /login. Returnerar false om ej inloggad.
static bool requireAuth() {
  if (isAuthed()) return true;
  if (web.uri().startsWith("/api/")) {
    web.send(401, "application/json", "{\"ok\":false,\"err\":\"login krävs\"}");
  } else {
    web.sendHeader("Location", "/login");
    web.send(302, "text/plain", "");
  }
  return false;
}

// ==========================================
// 5. HTTP-HJÄLP FÖR DUCKDNS / IP
// ==========================================
static bool httpGetString(const String& url, String& out, uint16_t timeoutMs = 8000) {
  WiFiClientSecure c;
  c.setInsecure();                       // certifikatkedjan verifieras inte
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setReuse(false);
  if (!http.begin(c, url)) return false;
  int code = http.GET();
  if (code == 200) out = http.getString();
  http.end();
  return code == 200;
}

static bool httpGetStringPlain(const String& url, String& out, uint16_t timeoutMs = 8000) {
  WiFiClient c;
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setReuse(false);
  if (!http.begin(c, url)) return false;
  int code = http.GET();
  if (code == 200) out = http.getString();
  http.end();
  return code == 200;
}

// Uppdaterar WAN-IP och DuckDNS. Körs i egen task.
static void remoteTask(void* param) {
  (void)param;

  for (;;) {
    String sub, token;
    bool configured, force;
    xSemaphoreTake(remoteMux, portMAX_DELAY);
    sub   = rs.sub;
    token = rs.token;
    force = rs.force;
    rs.force = false;
    configured = (sub.length() > 0 && token.length() > 0);
    xSemaphoreGive(remoteMux);

    bool due = false;
    if (configured) {
      xSemaphoreTake(remoteMux, portMAX_DELAY);
      due = (rs.lastCheckMs == 0) || ((uint32_t)(millis() - rs.lastCheckMs) >= DDNS_INTERVAL_MS);
      xSemaphoreGive(remoteMux);
    }

    if (configured && (force || due)) {
      String wan, resp, msg;
      bool ok = false;

      if (WiFi.status() == WL_CONNECTED) {
        if (!httpGetString("https://api.ipify.org", wan)) {
          if (!httpGetStringPlain("http://api.ipify.org", wan)) wan = "";
        }
        wan.trim();
        if (wan.length() > 32) wan = "";

        String url = "https://www.duckdns.org/update?domains=" + sub + "&token=" + token;
        if (wan.length() > 0) url += "&ip=" + wan;

        if (httpGetString(url, resp)) {
          resp.trim();
          ok = (resp == "OK");
          if (ok) msg = "OK";
          else if (resp == "KO") msg = "KO – fel token eller domän";
          else msg = "svar: " + resp;
        } else if (httpGetStringPlain(url, resp)) {
          resp.trim();
          ok = (resp == "OK");
          msg = ok ? "OK" : ("svar: " + resp);
        } else {
          msg = "nådde inte duckdns.org";
        }
      } else {
        msg = "Wi-Fi saknas";
      }

      xSemaphoreTake(remoteMux, portMAX_DELAY);
      rs.wanIp       = (wan.length() > 0) ? wan : "-";
      rs.ok          = ok;
      msg.replace("\"", "'");
      msg.replace("\\", "/");
      rs.msg         = msg;
      rs.lastCheckMs = millis();
      xSemaphoreGive(remoteMux);

      Serial.printf("DuckDNS: %s (%s)\n", msg.c_str(), wan.c_str());
    } else if (!configured) {
      xSemaphoreTake(remoteMux, portMAX_DELAY);
      if (rs.msg != "ej konfigurerad") rs.msg = "ej konfigurerad";
      xSemaphoreGive(remoteMux);
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// ==========================================
// 6. HÄMTA /status FRÅN SHELLY (GEN1)
// ==========================================
static bool shellyFetch(const String& host, const String& user, const String& pass,
                        float& powerW, float& voltV, float& ampA,
                        double& totalWh, double& returnedWh,
                        float& tempC, bool& relayOn,
                        float phW[3], float phV[3], float phA[3], double phWh[3],
                        uint8_t& phases,
                        uint32_t& rttMs, String& err) {

  powerW = 0; voltV = 0; ampA = 0; totalWh = 0; returnedWh = 0;
  tempC = NAN; relayOn = false; rttMs = 0; phases = 0;
  for (int i = 0; i < 3; i++) { phW[i] = 0; phV[i] = 0; phA[i] = 0; phWh[i] = 0; }

  if (host.length() == 0) { err = "Ingen Shelly-IP"; return false; }

  String url = "http://" + host + "/status";
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  if (!http.begin(url)) { err = "Ogiltig adress"; return false; }
  if (user.length() > 0) http.setAuthorization(user.c_str(), pass.c_str());

  uint32_t t0 = millis();
  int code = http.GET();
  rttMs = millis() - t0;

  if (code != 200) {
    http.end();
    err = "HTTP " + String(code);
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, body);  if (je) { err = String("JSON: ") + je.c_str(); return false; }

  // Plug S / 2.5 -> "meters". EM / 3EM -> "emeters". Summera alla kanaler/faser.
  JsonVariant meters = doc["meters"];
  if (meters.isNull()) meters = doc["emeters"];
  if (meters.isNull() || !meters.is<JsonArray>()) {
    err = "meters saknas i /status";
    return false;
  }

  uint8_t n = 0;
  float   voltSum = 0;
  uint8_t voltN = 0;

  for (JsonVariant m : meters.as<JsonArray>()) {
    float  pw = m["power"]   | 0.0f;
    float  vv = m["voltage"] | 0.0f;
    float  aa = m["current"] | 0.0f;
    double tw = m["total"]   | 0.0;

    powerW     += pw;
    totalWh    += tw;
    returnedWh += m["total_returned"] | 0.0;
    ampA       += aa;

    if (vv > 0.5f) { voltSum += vv; if (voltN < 255) voltN++; }

    if (n < 3) {
      phW[n]  = pw;
      phV[n]  = vv;
      phA[n]  = aa;
      phWh[n] = tw;
    }
    if (n < 255) n++;
  }
  phases = (n > 3) ? 3 : n;
  voltV  = (voltN > 0) ? (voltSum / (float)voltN) : 0;   // medel, inte summa (3EM)

  // Intern temperatur finns som "temperature" (Plug S) eller tmp.tC
  if (!doc["temperature"].isNull())    tempC = doc["temperature"].as<float>();
  else if (!doc["tmp"]["tC"].isNull()) tempC = doc["tmp"]["tC"].as<float>();

  relayOn = doc["relays"][0]["ison"] | false;

  err = "";
  return true;
}

// ==========================================
// 7. BAKGRUNDSTASK SOM POLLAR SHELLY
// ==========================================
static void shellyTask(void* param) {
  (void)param;
  uint32_t lastGoodMs = 0;

  for (;;) {
    String host, user, pass;
    xSemaphoreTake(stateMux, portMAX_DELAY);
    host = st.host; user = st.user; pass = st.pass;
    xSemaphoreGive(stateMux);

    if (WiFi.status() != WL_CONNECTED || host.length() == 0) {
      xSemaphoreTake(stateMux, portMAX_DELAY);
      st.online = false;
      st.fails++;
      st.err = (WiFi.status() != WL_CONNECTED) ? "Wi-Fi saknas" : "Ingen Shelly-IP";
      rotateMinutes(millis());
      xSemaphoreGive(stateMux);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    float pW, vV, aA, tC;
    float phW[3], phV[3], phA[3];
    double totWh, retWh, phWh[3];
    uint8_t phases;
    bool relay;
    uint32_t rtt;
    String err;
    bool ok = shellyFetch(host, user, pass, pW, vV, aA, totWh, retWh, tC, relay,
                          phW, phV, phA, phWh, phases, rtt, err);

    uint32_t now = millis();

    xSemaphoreTake(stateMux, portMAX_DELAY);
    st.lastTryMs = now;
    rotateMinutes(now);

    if (ok) {
      updateClockBuckets();

      if (st.valid && lastGoodMs != 0) {
        uint32_t dt = now - lastGoodMs;
        // Integrera bara rimliga intervall – vid längre avbrott är datan okänd
        if (dt > 0 && dt <= 15000) {
          double dWh = (double)(st.powerW + pW) * 0.5 * ((double)dt / 3600000.0);
          addEnergy(dWh);
        }
      }

      st.powerW     = pW;
      st.voltageV   = vV;
      st.currentA   = aA;
      st.tempC      = tC;
      st.totalWh    = totWh;
      st.returnedWh = retWh;
      st.phases     = phases;
      for (int i = 0; i < 3; i++) {
        st.phW[i]  = phW[i];
        st.phV[i]  = phV[i];
        st.phA[i]  = phA[i];
        st.phWh[i] = phWh[i];
      }
      st.relayOn    = relay;
      st.rttMs      = rtt;
      st.online     = true;
      st.valid      = true;
      st.fails      = 0;
      st.err        = "";
      st.lastOkMs   = now;
      lastGoodMs    = now;

      // spara ett live-prov (äldst -> nyast läses ut i handleLive)
      liveW[liveIdx] = pW;
      liveV[liveIdx] = vV;
      for (int i = 0; i < 3; i++) livePh[i][liveIdx] = phW[i];
      liveIdx = (liveIdx + 1) % LIVE_SLOTS;
      if (liveCount < LIVE_SLOTS) liveCount++;
    } else {
      if (st.fails < 65000) st.fails++;
      if (st.fails >= 2) st.online = false;
      st.err = err;
    }
    xSemaphoreGive(stateMux);

    vTaskDelay(pdMS_TO_TICKS(ok ? POLL_MS : POLL_FAIL_MS));
  }
}

// ==========================================
// 8. JSON-API
// ==========================================
static float clean(float v) { return isnan(v) ? -999.0f : v; }

void handlePower() {
  if (!requireAuth()) return;

  State s;
  xSemaphoreTake(stateMux, portMAX_DELAY);
  s = st;
  xSemaphoreGive(stateMux);

  uint32_t now = millis();
  long age = s.valid ? (long)((now - s.lastOkMs) / 1000) : -1;

  String json;
  json.reserve(512);
  json += "{\"online\":";      json += s.online ? "true" : "false";
  json += ",\"valid\":";       json += s.valid ? "true" : "false";
  json += ",\"host\":\"";      json += s.host;
  json += "\",\"err\":\"";     json += s.err;
  json += "\",\"power\":";     json += String(s.powerW, 1);
  json += ",\"volt\":";        json += String(s.voltageV, 1);
  json += ",\"amp\":";         json += String(s.currentA, 2);
  json += ",\"totalWh\":";     json += String(s.totalWh, 1);
  json += ",\"returnedWh\":";  json += String(s.returnedWh, 1);
  json += ",\"bootWh\":";      json += String(s.whBoot, 2);
  json += ",\"h1Wh\":";        json += String(s.whHour1, 2);
  json += ",\"todayWh\":";     json += String(s.whToday, 2);
  json += ",\"temp\":";        json += String(clean(s.tempC), 1);
  json += ",\"relay\":";       json += s.relayOn ? "true" : "false";
  json += ",\"rtt\":";         json += String(s.rttMs);
  json += ",\"fails\":";       json += String(s.fails);
  json += ",\"age\":";         json += String(age);
  json += ",\"uptime\":";      json += String(now / 1000UL);
  json += ",\"rssi\":";        json += String(WiFi.RSSI());
  json += ",\"phases\":";      json += String(s.phases);
  json += ",\"ph\":[";
  for (int i = 0; i < 3; i++) {
    if (i) json += ",";
    json += "{\"w\":"  + String(s.phW[i], 1) +
            ",\"v\":"  + String(s.phV[i], 1) +
            ",\"a\":"  + String(s.phA[i], 2) +
            ",\"wh\":" + String(s.phWh[i], 1) + "}";
  }
  json += "]}";

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", json);
}

// Live: senaste ~5 minuterna (150 prov à 2 s), äldst först.
// Returnerar effekt, snittspänning och snitteffekt per fas.
void handleLive() {
  if (!requireAuth()) return;

  float bufW[LIVE_SLOTS];
  float bufV[LIVE_SLOTS];
  float phAvg[3] = { 0, 0, 0 };
  uint16_t n;

  xSemaphoreTake(stateMux, portMAX_DELAY);
  n = liveCount;
  for (uint16_t i = 0; i < n; i++) {
    int idx = ((int)liveIdx - (int)n + (int)i + LIVE_SLOTS) % LIVE_SLOTS;
    bufW[i] = liveW[idx];
    bufV[i] = liveV[idx];
    for (int k = 0; k < 3; k++) phAvg[k] += livePh[k][idx];
  }
  xSemaphoreGive(stateMux);

  if (n > 0) {
    for (int k = 0; k < 3; k++) phAvg[k] /= (float)n;
  }

  String json;
  json.reserve(64 + n * 14);
  json += "{\"w\":[";
  for (uint16_t i = 0; i < n; i++) {
    if (i) json += ",";
    json += String(bufW[i], 1);
  }
  json += "],\"v\":[";
  for (uint16_t i = 0; i < n; i++) {
    if (i) json += ",";
    json += String(bufV[i], 1);
  }
  json += "],\"phAvg\":[";
  for (int k = 0; k < 3; k++) {
    if (k) json += ",";
    json += String(phAvg[k], 1);
  }
  json += "]}";

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", json);
}

void handleHistory() {
  if (!requireAuth()) return;

  State s;
  xSemaphoreTake(stateMux, portMAX_DELAY);
  s = st;
  xSemaphoreGive(stateMux);

  String json = "{\"labels\":[";
  for (int i = 0; i < HOUR_SLOTS; i++) {
    if (i) json += ",";
    int slot = (s.curSlot >= 0) ? (s.curSlot + 1 + i) % HOUR_SLOTS : i;
    int lbl  = (s.curSlot >= 0) ? s.hourLabel[slot] : -1;
    if (lbl >= 0) json += "\"" + String(lbl) + "\"";
    else          json += "\"-\"";
  }
  json += "],\"kwh\":[";
  for (int i = 0; i < HOUR_SLOTS; i++) {
    if (i) json += ",";
    int slot = (s.curSlot >= 0) ? (s.curSlot + 1 + i) % HOUR_SLOTS : i;
    double k = (s.curSlot >= 0 && s.hourLabel[slot] >= 0) ? (double)s.whHour[slot] / 1000.0 : 0.0;
    json += String(k, 3);
  }
  json += "]}";

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", json);
}

void handleConfigGet() {
  if (!requireAuth()) return;

  State s;
  xSemaphoreTake(stateMux, portMAX_DELAY);
  s = st;
  xSemaphoreGive(stateMux);

  String json = "{\"host\":\"" + s.host + "\",\"user\":\"" + s.user + "\"}";
  web.send(200, "application/json", json);
}

void handleConfigSave() {
  if (!requireAuth()) return;

  String host = sanitizeHost(web.arg("host"));
  String user = web.arg("user");
  String pass = web.arg("pass");
  user.trim();

  if (host.length() == 0) {
    web.send(400, "application/json", "{\"ok\":false,\"err\":\"host krävs\"}");
    return;
  }

  xSemaphoreTake(stateMux, portMAX_DELAY);
  st.host = host;
  st.user = user;
  if (pass.length() > 0) st.pass = pass;   // tomt fält = behåll gamla lösenordet
  st.fails = 0;
  st.online = false;
  st.err = "ny adress, väntar på svar";
  st.lastTryMs = millis();
  xSemaphoreGive(stateMux);

  prefs.putString("host", host);
  prefs.putString("user", user);
  if (pass.length() > 0) prefs.putString("pass", pass);

  Serial.printf("Shelly-adress sparad: %s\n", host.c_str());
  web.send(200, "application/json", "{\"ok\":true}");
}

// Testar en Shelly-adress direkt (utan att spara) så man ser att inloggningen fungerar
void handleTest() {
  if (!requireAuth()) return;

  String host = sanitizeHost(web.arg("host"));
  String user = web.arg("user");
  String pass = web.arg("pass");
  user.trim();

  State s;
  xSemaphoreTake(stateMux, portMAX_DELAY);
  s = st;
  xSemaphoreGive(stateMux);

  if (host.length() == 0) host = s.host;
  if (user.length() == 0) user = s.user;
  if (pass.length() == 0) pass = s.pass;

  float pW, vV, aA, tC;
  float phW[3], phV[3], phA[3];
  double totWh, retWh, phWh[3];
  uint8_t phases;
  bool relay;
  uint32_t rtt;
  String err;
  bool ok = shellyFetch(host, user, pass, pW, vV, aA, totWh, retWh, tC, relay,
                        phW, phV, phA, phWh, phases, rtt, err);

  String json;
  if (ok) {
    json = "{\"ok\":true,\"power\":" + String(pW, 1) +
           ",\"total\":" + String(totWh, 1) +
           ",\"phases\":" + String(phases) +
           ",\"rtt\":" + String(rtt) + "}";
  } else {
    err.replace("\"", "'");
    err.replace("\\", "/");
    json = "{\"ok\":false,\"err\":\"" + err + "\"}";
  }
  web.send(200, "application/json", json);
}

void handleRemoteGet() {  if (!requireAuth()) return;

  RemoteState r;
  xSemaphoreTake(remoteMux, portMAX_DELAY);
  r = rs;
  xSemaphoreGive(remoteMux);

  String json = "{";
  json += "\"lan\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"sub\":\"" + r.sub + "\"";
  json += ",\"configured\":"; json += (r.sub.length() > 0 && r.token.length() > 0) ? "true" : "false";
  json += ",\"wanIp\":\"" + r.wanIp + "\"";
  json += ",\"msg\":\"" + r.msg + "\"";
  json += ",\"ok\":"; json += r.ok ? "true" : "false";
  json += ",\"checkedAgo\":";
  json += (r.lastCheckMs == 0) ? String(-1) : String((millis() - r.lastCheckMs) / 1000UL);
  json += "}";
  web.send(200, "application/json", json);
}

void handleRemoteSave() {
  if (!requireAuth()) return;

  String sub   = sanitizeSub(web.arg("sub"));
  String token = sanitizeToken(web.arg("token"));

  xSemaphoreTake(remoteMux, portMAX_DELAY);
  if (sub.length() > 0) rs.sub = sub;
  if (token.length() > 0) rs.token = token;
  if (web.hasArg("clear")) { rs.sub = ""; rs.token = ""; }
  rs.force = true;
  xSemaphoreGive(remoteMux);

  prefs.putString("ddnsSub", rs.sub);
  prefs.putString("ddnsTok", rs.token);

  web.send(200, "application/json", "{\"ok\":true}");
}

void handleAccountSave() {
  if (!requireAuth()) return;

  String user = web.arg("user");
  String pass = web.arg("pass");
  user.trim();

  if (user.length() < 3) {
    web.send(400, "application/json", "{\"ok\":false,\"err\":\"användarnamn minst 3 tecken\"}");
    return;
  }
  if (pass.length() < 6) {
    web.send(400, "application/json", "{\"ok\":false,\"err\":\"lösenord minst 6 tecken\"}");
    return;
  }

  webSalt = randomHex(8);
  webHash = sha256hex(webSalt + pass);
  webUser = user;

  prefs.putString("wuser", webUser);
  prefs.putString("wsalt", webSalt);
  prefs.putString("whash", webHash);

  clearSessions();          // alla måste logga in igen
  Serial.println("Webbkonto uppdaterat.");

  web.sendHeader("Set-Cookie", "sesid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  web.send(200, "application/json", "{\"ok\":true}");
}

void handleRestart() {
  if (!requireAuth()) return;
  web.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

// ==========================================
// 9. INLOGGNING
// ==========================================
const char HTML_LOGIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
<title>Logga in – Husets effekt</title>
<style>
  body { font-family:system-ui,-apple-system,sans-serif; background:#0f172a; color:#f8fafc;
         display:flex; align-items:center; justify-content:center; min-height:100vh; margin:0; padding:16px;
         box-sizing:border-box; }
  .card { background:#1e293b; border:1px solid #334155; border-radius:20px; padding:26px;
          width:100%; max-width:360px; box-shadow:0 10px 30px rgba(0,0,0,.55); }
  h1 { color:#38bdf8; font-size:1.35em; text-align:center; margin:0 0 18px; }
  label { display:block; color:#94a3b8; font-size:.75em; text-transform:uppercase;
          letter-spacing:.9px; margin-top:12px; }
  input { width:100%; box-sizing:border-box; margin-top:6px; padding:12px; border-radius:11px;
          border:1px solid #334155; background:#0f172a; color:#f8fafc; font-size:1em; }
  button { width:100%; margin-top:18px; padding:12px; border:0; border-radius:11px;
           background:#38bdf8; color:#0f172a; font-weight:700; font-size:1em; cursor:pointer; }
  button:active { background:#0ea5e9; }
  .err { background:#3f1d1d; border:1px solid #7f1d1d; color:#fca5a5; padding:10px;
         border-radius:10px; font-size:.85em; text-align:center; margin-bottom:6px; }
  .foot { color:#64748b; font-size:.75em; text-align:center; margin-top:16px; }
</style>
</head>
<body>
<div class="card">
  <h1>⚡ Husets effekt</h1>
  __ERR__
  <form method="POST" action="/login">
    <label>Användare</label>
    <input name="user" autocomplete="username" autocapitalize="none" required autofocus>
    <label>Lösenord</label>
    <input name="pass" type="password" autocomplete="current-password" required>
    <button type="submit">Logga in</button>
  </form>
  <div class="foot">Husets effekt · ESP32-S3</div>
</div>
</body>
</html>
)rawliteral";

const char HTML_OTA[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Husets effekt – OTA</title>
<style>body{font-family:system-ui,sans-serif;background:#0f172a;color:#f8fafc;text-align:center;padding:20px}
.card{background:#1e293b;border-radius:16px;padding:20px;max-width:420px;margin:30px auto;border:1px solid #334155}
h1{color:#38bdf8;font-size:1.3em}input,button{width:100%;box-sizing:border-box;margin-top:10px;padding:11px;
border-radius:10px;border:1px solid #334155;background:#0f172a;color:#f8fafc;font-size:1em}
button{background:#38bdf8;color:#0f172a;font-weight:700;border:0}
a{color:#38bdf8;font-size:.9em;text-decoration:none}p{color:#94a3b8;font-size:.85em}</style></head>
<body><div class="card">
<h1>OTA – ny firmware</h1>
<form method="POST" action="/update" enctype="multipart/form-data">
  <input type="password" name="pass" placeholder="OTA-lösenord" required>
  <input type="file" name="firmware" accept=".bin" required>
  <button type="submit">Uppdatera</button>
</form>
<p>Filen hittas i Arduino IDE under Sketch → Export Compiled Binary,
eller i byggmappen (ShellyEffekt.ino.bin).</p>
<a href="/">← Tillbaka</a>
</div></body></html>
)rawliteral";

void handleLoginPage() {
  if (isAuthed()) {
    web.sendHeader("Location", "/");
    web.send(302, "text/plain", "");
    return;
  }

  String page = FPSTR(HTML_LOGIN);
  if (web.hasArg("e") && web.arg("e") == "2") {
    page.replace("__ERR__", "<div class=\"err\">För många försök – vänta en minut.</div>");
  } else if (web.hasArg("e")) {
    page.replace("__ERR__", "<div class=\"err\">Fel användarnamn eller lösenord.</div>");
  } else {
    page.replace("__ERR__", "");
  }
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html", page);
}

void handleLoginPost() {
  uint32_t now = millis();
  if (loginLockUntil != 0 && (int32_t)(now - loginLockUntil) < 0) {
    web.sendHeader("Location", "/login?e=2");
    web.send(302, "text/plain", "");
    return;
  }

  String u = web.arg("user");
  String p = web.arg("pass");
  u.trim();
  if (u.length() > 32) u = u.substring(0, 32);

  bool ok = ctEq(u, webUser) && ctEq(sha256hex(webSalt + p), webHash);

  if (!ok) {
    if (loginFails < 255) loginFails++;
    if (loginFails >= MAX_LOGIN_FAILS) {
      loginLockUntil = millis() + LOGIN_LOCK_MS;
      loginFails = 0;
      Serial.println("Login: för många försök, låst i 60 s.");
    }
    web.sendHeader("Location", "/login?e=1");
    web.send(302, "text/plain", "");
    return;
  }

  loginFails = 0;
  loginLockUntil = 0;

  String tok = createSession();
  char cookie[96];
  snprintf(cookie, sizeof(cookie),
           "sesid=%s; Path=/; Max-Age=43200; HttpOnly; SameSite=Lax", tok.c_str());
  web.sendHeader("Set-Cookie", cookie);
  web.sendHeader("Location", "/");
  web.send(302, "text/plain", "");
}

void handleLogout() {
  int idx = findSession(cookieToken());
  if (idx >= 0) sessions[idx].token = "";
  web.sendHeader("Set-Cookie", "sesid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  web.sendHeader("Location", "/login");
  web.send(302, "text/plain", "");
}

// ==========================================
// 10. DASHBOARD
// ==========================================
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
<title>⚡ Husets effekt</title>
<style>
  body { font-family:system-ui,-apple-system,sans-serif; background:#0f172a; color:#f8fafc;
         padding:12px; margin:0; -webkit-text-size-adjust:100%; -webkit-tap-highlight-color:transparent; }
  .card { background:#1e293b; border-radius:20px; padding:18px; max-width:440px; margin:12px auto;
          box-shadow:0 10px 25px rgba(0,0,0,.5); border:1px solid #334155; }
  .topline { display:flex; align-items:center; justify-content:space-between; gap:8px; }
  h1 { color:#38bdf8; margin:0; font-size:1.45em; }
  .nav { display:flex; gap:10px; }
  .nav a { color:#94a3b8; text-decoration:none; font-size:.8em; border:1px solid #334155;
           padding:5px 9px; border-radius:9px; }
  .nav a:hover { color:#f8fafc; border-color:#475569; }
  .label { color:#94a3b8; font-size:.75em; text-transform:uppercase; letter-spacing:1.1px; }
  .status { background:#0f172a; padding:10px 12px; border-radius:12px; font-weight:600;
            color:#4ade80; border:1px solid #1e293b; word-break:break-word; font-size:.9em;
            text-align:center; margin-top:14px; }
  .status.bad { color:#f87171; }
  .metric { font-size:2.7em; font-weight:800; color:#fbbf24; text-align:center; margin:10px 0 0;
            text-shadow:0 0 18px rgba(251,191,36,.35); }
  .sublabel { text-align:center; color:#94a3b8; font-size:.8em; margin-top:2px; }
  .grid { display:grid; grid-template-columns:1fr 1fr 1fr; gap:8px; margin-top:16px; }
  .tile { background:#0f172a; border:1px solid #263449; border-radius:12px; padding:10px 6px; text-align:center; }
  .tile .v { font-size:1.05em; font-weight:700; margin-top:5px; color:#e2e8f0; }
  .block { background:#0f172a; border:1px solid #263449; border-radius:14px; padding:12px; margin-top:14px; }
  .chart { display:flex; align-items:flex-end; gap:2px; height:110px; margin-top:10px; }
  .bar { flex:1; display:flex; flex-direction:column; justify-content:flex-end; height:100%; }
  .fill { background:linear-gradient(180deg,#38bdf8,#0ea5e9); border-radius:3px 3px 0 0; min-height:1px; }
  .ticks { display:flex; gap:2px; margin-top:4px; }
  .ticks span { flex:1; text-align:center; color:#64748b; font-size:9px; }
  .foot { color:#64748b; font-size:.75em; text-align:center; margin-top:14px; line-height:1.6; }
  details { margin-top:16px; background:#0f172a; border:1px solid #263449; border-radius:12px; padding:10px 12px; }
  summary { cursor:pointer; color:#94a3b8; font-weight:600; font-size:.9em; }
  form { margin-top:10px; display:flex; flex-direction:column; gap:8px; }
  label { color:#94a3b8; font-size:.75em; text-transform:uppercase; letter-spacing:.8px; }
  input { width:100%; box-sizing:border-box; margin-top:4px; padding:10px; border-radius:10px;
          border:1px solid #334155; background:#1e293b; color:#f8fafc; font-size:1em; }
  button { padding:11px; border:0; border-radius:10px; background:#38bdf8; color:#0f172a;
           font-weight:700; font-size:1em; cursor:pointer; }
  button:active { background:#0ea5e9; }
  button.ghost { background:#334155; color:#f8fafc; }
  .kv { display:flex; justify-content:space-between; gap:10px; font-size:.85em; margin-top:7px; }
  .kv span:first-child { color:#94a3b8; }
  .kv span:last-child { color:#e2e8f0; word-break:break-all; text-align:right; }
  .ok { color:#4ade80; } .no { color:#f87171; }
  .hint { color:#64748b; font-size:.75em; line-height:1.5; margin-top:8px; }
  .livebox { background:#0f172a; border:1px solid #263449; border-radius:14px;
             padding:12px 12px 8px; margin-top:14px; }
  .livelbl { color:#94a3b8; font-size:.75em; text-transform:uppercase; letter-spacing:1.1px;
             text-align:center; }
  .livedot { display:inline-block; width:9px; height:9px; border-radius:50%; background:#4ade80;
             margin-right:6px; animation:pulse 1.6s infinite; }
  .livedot.off { background:#f87171; animation:none; }
  @keyframes pulse {
    0%,100% { opacity:1; box-shadow:0 0 0 0 rgba(74,222,128,.55); }
    50%     { opacity:.6; box-shadow:0 0 0 7px rgba(74,222,128,0); }
  }
  .metric { margin:2px 0 0; }
  .livegraph { width:100%; height:88px; display:block; margin-top:6px; }
  .liverow { display:flex; justify-content:space-between; color:#64748b; font-size:.72em; margin-top:4px; }
  .phaserow { display:grid; grid-template-columns:1fr 1fr 1fr; gap:8px; margin-top:12px; }
  .phase { background:#0f172a; border:1px solid #263449; border-radius:12px; padding:10px 6px; text-align:center; }
  .phase .pname { color:#38bdf8; font-weight:700; font-size:.8em; letter-spacing:.5px; }
  .phase .pw { font-size:1.25em; font-weight:800; color:#fbbf24; margin:3px 0 2px; }
  .phase .pmeta { color:#94a3b8; font-size:.72em; line-height:1.55; }
  .voltbox { background:#0f172a; border:1px solid #263449; border-radius:14px;
             padding:10px 12px 8px; margin-top:10px; }
  .voltnow { text-align:center; font-size:1.4em; font-weight:800; color:#34d399; margin-top:2px; }
  .voltgraph { width:100%; height:56px; display:block; margin-top:2px; }
  .phasehdr { color:#94a3b8; font-size:.75em; text-align:center; margin-top:12px; }
  .phasehdr b { color:#fbbf24; }
  .phase.top { border-color:#fbbf24; box-shadow:0 0 12px rgba(251,191,36,.25); }
  .phase .pavg { color:#64748b; font-size:.68em; margin-top:1px; }
  .topbadge { display:inline-block; background:#fbbf24; color:#0f172a; font-size:.6em; font-weight:800;
              border-radius:5px; padding:1px 5px; margin-left:4px; vertical-align:middle; letter-spacing:.5px; }
  @supports (padding:max(0px)) {
    body { padding-left:max(12px,env(safe-area-inset-left)); padding-right:max(12px,env(safe-area-inset-right));
           padding-top:max(12px,env(safe-area-inset-top)); padding-bottom:max(12px,env(safe-area-inset-bottom)); }
  }
</style>
</head>
<body>
<div class="card">
  <div class="topline">
    <h1>⚡ Husets effekt</h1>
    <div class="nav"><a href="/update">OTA</a><a href="/logout">Logga ut</a></div>
  </div>

  <div class="status" id="status">Ansluter…</div>

  <div class="livebox">
    <div class="livelbl"><span class="livedot" id="liveDot"></span>Just nu</div>
    <div class="metric" id="nowW">–</div>
    <svg class="livegraph" viewBox="0 0 360 90" preserveAspectRatio="none">
      <defs>
        <linearGradient id="liveFillGrad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stop-color="#38bdf8" stop-opacity="0.5"></stop>
          <stop offset="100%" stop-color="#38bdf8" stop-opacity="0"></stop>
        </linearGradient>
      </defs>
      <path id="liveFill" fill="url(#liveFillGrad)"></path>
      <polyline id="liveLine" fill="none" stroke="#38bdf8" stroke-width="2"
                vector-effect="non-scaling-stroke"></polyline>
    </svg>
    <div class="liverow">
      <span>senaste 5 min</span>
      <span id="liveMin">–</span>
      <span id="liveAvg">–</span>
      <span id="liveMax">–</span>
    </div>
  </div>

  <div class="voltbox">
    <div class="livelbl">Spänning · snitt över faserna</div>
    <div class="voltnow" id="voltNow">–</div>
    <svg class="voltgraph" viewBox="0 0 360 50" preserveAspectRatio="none">
      <polyline id="voltLine" fill="none" stroke="#34d399" stroke-width="2"
                vector-effect="non-scaling-stroke"></polyline>
    </svg>
    <div class="liverow">
      <span>senaste 5 min</span>
      <span id="voltMin">–</span>
      <span id="voltAvg">–</span>
      <span id="voltMax">–</span>
    </div>
  </div>

  <div class="phasehdr" id="phaseHdr"></div>
  <div class="phaserow" id="phases"></div>

  <div class="grid">
    <div class="tile"><div class="label">Mätarens total</div><div class="v" id="total">–</div></div>
    <div class="tile"><div class="label">Sedan start</div><div class="v" id="boot">–</div></div>
    <div class="tile"><div class="label">Senaste 1 h</div><div class="v" id="h1">–</div></div>
    <div class="tile"><div class="label">Idag</div><div class="v" id="today">–</div></div>
    <div class="tile"><div class="label">Spänning</div><div class="v" id="vv">–</div></div>
    <div class="tile"><div class="label">Ström</div><div class="v" id="a">–</div></div>
  </div>

  <div class="block">
    <div class="label">Förbrukning per timme (kWh, senaste 24 h)</div>
    <div class="chart" id="chart"></div>
    <div class="ticks" id="ticks"></div>
  </div>

  <div class="foot" id="foot">–</div>
</div>

<script>
function fmtWh(wh) {
  if (wh === null || wh === undefined || isNaN(wh)) return '–';
  if (Math.abs(wh) >= 1000) return (wh / 1000).toFixed(2) + ' kWh';
  return wh.toFixed(0) + ' Wh';
}
function fmtKwh(wh) {
  if (wh === null || wh === undefined || isNaN(wh)) return '–';
  return (wh / 1000).toFixed(2) + ' kWh';
}
function $(id) { return document.getElementById(id); }

var phAvg = [0, 0, 0];   // snitteffekt per fas, senaste 5 min (från /api/live)

function poll() {
  fetch('/api/power', { cache: 'no-store' })
    .then(function (r) {
      if (r.status === 401) { location.href = '/login'; return null; }
      return r.json();
    })
    .then(function (d) {
      if (!d) return;
      var s = $('status');
      if (d.online) {
        s.className = 'status';
        s.textContent = '🟢 ' + d.host + ' · ' + d.rtt + ' ms';
      } else {
        s.className = 'status bad';
        s.textContent = '🔴 ' + d.err + (d.host ? ' · ' + d.host : '');
      }

      $('total').textContent = fmtKwh(d.totalWh);
      $('boot').textContent  = fmtWh(d.bootWh);
      $('h1').textContent    = fmtWh(d.h1Wh);
      $('today').textContent = fmtWh(d.todayWh);
      $('nowW').innerHTML    = (d.online ? d.power.toFixed(0) : '–') + '<small> W</small>';
      $('liveDot').className = 'livedot' + (d.online ? '' : ' off');
      $('vv').textContent    = (d.volt > 1 ? d.volt.toFixed(0) + ' V' : '–');
      $('a').textContent     = (d.amp > 0.005 ? d.amp.toFixed(2) + ' A' : '–');

      // Per fas (Shelly 3EM): visa bara om mätaren rapporterar flera faser
      var ph = d.ph || [];
      if (d.phases >= 2) {
        var topIdx = 0, topVal = -1;
        for (var i = 0; i < 3; i++) if (phAvg[i] > topVal) { topVal = phAvg[i]; topIdx = i; }
        var phHtml = '';
        for (var i = 0; i < ph.length && i < d.phases; i++) {
          phHtml += '<div class="phase' + (i === topIdx && topVal > 0 ? ' top' : '') + '">' +
                    '<div class="pname">L' + (i + 1) +
                    (i === topIdx && topVal > 0 ? '<span class="topbadge">MEST</span>' : '') + '</div>' +
                    '<div class="pw">' + ph[i].w.toFixed(0) + ' W</div>' +
                    '<div class="pavg">snitt ' +
                    (phAvg[i] > 0 ? phAvg[i].toFixed(0) + ' W' : '–') + '</div>' +
                    '<div class="pmeta">' +
                    (ph[i].v > 1 ? ph[i].v.toFixed(0) + ' V' : '–') + ' · ' +
                    (ph[i].a > 0.005 ? ph[i].a.toFixed(1) + ' A' : '–') +
                    '<br>' + fmtKwh(ph[i].wh) + '</div></div>';
        }
        $('phases').innerHTML = phHtml;
        $('phases').style.display = 'grid';
      } else {
        $('phases').style.display = 'none';
        var hdr = $('phaseHdr'); if (hdr) hdr.textContent = '';
      }

      var bits = [];
      if (d.temp > -100) bits.push('Mätare ' + d.temp.toFixed(1) + ' °C');
      bits.push('Relä ' + (d.relay ? 'på' : 'av'));
      bits.push('Uptime ' + Math.floor(d.uptime / 3600) + ' h ' +
                Math.floor(d.uptime % 3600 / 60) + ' min');
      if (!d.online && d.age >= 0) bits.push('senast OK för ' + d.age + ' s sedan');
      $('foot').textContent = bits.join(' · ');
    })
    .catch(function () {
      var s = $('status');
      s.className = 'status bad';
      s.textContent = '🔴 Kortet svarar inte';
    });
}

function graph() {
  fetch('/api/history', { cache: 'no-store' })
    .then(function (r) { return r.json(); })
    .then(function (d) {
      var max = 0.001;
      for (var i = 0; i < d.kwh.length; i++) if (d.kwh[i] > max) max = d.kwh[i];
      var chart = '', ticks = '';
      for (var i = 0; i < d.kwh.length; i++) {
        var h = Math.round(d.kwh[i] / max * 100);
        chart += '<div class="bar"><div class="fill" style="height:' + h +
                 '%" title="' + d.kwh[i].toFixed(3) + ' kWh"></div></div>';
        ticks += '<span>' + (i % 4 === 0 ? d.labels[i] : '') + '</span>';
      }
      $('chart').innerHTML = chart;
      $('ticks').innerHTML = ticks;
    });
}

// Ritar live-grafen (senaste ~5 min) från /api/live
function live() {
  fetch('/api/live', { cache: 'no-store' })
    .then(function (r) { return r.json(); })
    .then(function (d) {
      var w = d.w || [];
      var n = w.length;
      if (n < 2) return;

      var mx = w[0], mn = w[0], sum = 0;
      for (var i = 0; i < n; i++) {
        if (w[i] > mx) mx = w[i];
        if (w[i] < mn) mn = w[i];
        sum += w[i];
      }

      var top = Math.max(mx, 1);
      var bot = Math.min(mn, 0);
      var range = Math.max(top - bot, 1);
      var pts = [];

      for (var i = 0; i < n; i++) {
        var x = (i / (n - 1)) * 360;
        var y = 86 - ((w[i] - bot) / range) * 78;
        pts.push(x.toFixed(1) + ',' + y.toFixed(1));
      }

      $('liveLine').setAttribute('points', pts.join(' '));
      $('liveFill').setAttribute('d', 'M0,90 L' + pts.join(' L') + ' L360,90 Z');
      $('liveMin').textContent = 'min ' + mn.toFixed(0) + ' W';
      $('liveAvg').textContent = 'snitt ' + (sum / n).toFixed(0) + ' W';
      $('liveMax').textContent = 'max ' + mx.toFixed(0) + ' W';

      // Snittspänning över faserna – egen liten graf
      var v = d.v || [];
      if (v.length >= 2) {
        var vmx = v[0], vmn = v[0], vsum = 0;
        for (var i = 0; i < v.length; i++) {
          if (v[i] > vmx) vmx = v[i];
          if (v[i] < vmn) vmn = v[i];
          vsum += v[i];
        }
        var vavg = vsum / v.length;
        // Lite luft runt kurvan så den inte ligger platt mot kanten
        var vpad = Math.max((vmx - vmn) * 0.15, 0.8);
        var vtop = vmx + vpad, vbot = vmn - vpad;
        var vrange = Math.max(vtop - vbot, 0.1);
        var vpts = [];
        for (var i = 0; i < v.length; i++) {
          var vx = (i / (v.length - 1)) * 360;
          var vy = 44 - ((v[i] - vbot) / vrange) * 38;
          vpts.push(vx.toFixed(1) + ',' + vy.toFixed(1));
        }
        $('voltLine').setAttribute('points', vpts.join(' '));
        $('voltNow').textContent = v[v.length - 1].toFixed(1) + ' V';
        $('voltMin').textContent = 'min ' + vmn.toFixed(1) + ' V';
        $('voltAvg').textContent = 'snitt ' + vavg.toFixed(1) + ' V';
        $('voltMax').textContent = 'max ' + vmx.toFixed(1) + ' V';
      }

      // Snitt per fas (5 min) – visa vilken fas som drar mest
      var pa = d.phAvg || [0, 0, 0];
      phAvg = pa;
      if (pa.length >= 2) {
        var hi = 0;
        for (var i = 1; i < pa.length; i++) if (pa[i] > pa[hi]) hi = i;
        var tot = pa[0] + pa[1] + (pa[2] || 0);
        var hdr = $('phaseHdr');
        if (hdr) {
          hdr.innerHTML = (tot > 0)
            ? 'Snitt 5 min · <b>L' + (hi + 1) + ' drar mest</b> (' +
              pa[hi].toFixed(0) + ' W · ' + Math.round(pa[hi] / tot * 100) + ' % av lasten)'
            : 'Snitt 5 min';
        }
      }
    });
}

poll();
live();
graph();
setInterval(poll, 2000);
setInterval(live, 2000);
setInterval(graph, 60000);
</script>
</body>
</html>
)rawliteral";

// Kortets egen live-sida: hamtar /api/power och ritar samma vy som den publika sidan.
void handleLivePage() {
  if (!requireAuth()) return;
  String page = FPSTR(HTML_LIVE);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html", page);
}

void handleRoot() {
  if (!requireAuth()) return;
  String page = FPSTR(HTML_INDEX);
  page.replace("__USER__", webUser);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html", page);
}

void handleOTA() {
  if (!requireAuth()) return;
  web.sendHeader("Connection", "close");
  web.send(200, "text/html", FPSTR(HTML_OTA));
}

void handleOTAUpload() {
  if (!isAuthed()) {
    web.send(401, "text/plain", "Login krävs.");
    return;
  }
  web.sendHeader("Connection", "close");
  if (Update.hasError()) {
    web.send(500, "text/plain", "Uppdateringen misslyckades – är filen en giltig ESP32-S3 .bin?");
    return;
  }
  web.send(200, "text/plain", "OK – startar om...");
  delay(1000);
  ESP.restart();
}

void handleOTAProgress() {
  HTTPUpload& upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    bool authOk = isAuthed();
    bool passOk = web.hasArg("pass") && web.arg("pass") == OTA_PASSWORD;
    if (!authOk || !passOk) {
      Serial.println("OTA: nekad (inloggning/lösenord).");
      Update.abort();
      return;
    }
    Serial.printf("OTA: tar emot %s (%u bytes)...\n", upload.filename.c_str(), upload.totalSize);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("OTA: klar (%u bytes).\n", upload.totalSize);
    else Update.printError(Serial);
  }
}

// OTA dar kortet sjalvt hamtar .bin-filen. Webbserverns uppladdning har en
// hardkodad tidsgrans pa 5 sekunder och faller pa en svag Wi-Fi-lank; har ar
// kortet i stallet TCP-klient och kan vanta hur lange som helst.
void handleOtaFromUrl() {
  if (!requireAuth()) return;
  String url  = web.arg("url");
  String pass = web.arg("pass");
  if (pass != OTA_PASSWORD) { web.send(403, "text/plain", "Fel OTA-losenord."); return; }
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    web.send(400, "text/plain", "Ange en http(s)-URL till .bin-filen.");
    return;
  }

  web.sendHeader("Connection", "close");
  web.send(200, "text/plain", "Hamtar firmware - kortet startar om nar det ar klart.");

  WiFiClient plain;
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(60000);
  http.setConnectTimeout(15000);
  bool begun = url.startsWith("https://") ? http.begin(tls, url) : http.begin(plain, url);
  if (!begun) { Serial.println("OTA-URL: kunde inte starta HTTP"); return; }

  int code = http.GET();
  if (code != 200) { Serial.printf("OTA-URL: HTTP %d", code); Serial.println(); http.end(); return; }

  int len = http.getSize();
  Serial.printf("OTA-URL: %d byte", len); Serial.println();
  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); http.end(); return; }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;
  int remaining = len;
  while (http.connected() && (remaining > 0 || len <= 0)) {
    size_t avail = stream->available();
    if (avail) {
      int c = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (c > 0) {
        if (Update.write(buf, c) != (size_t)c) { Update.printError(Serial); break; }
        written += c;
        if (remaining > 0) remaining -= c;
      }
    } else {
      delay(1);
    }
  }
  bool ok = Update.end(true);
  http.end();
  Serial.printf("OTA-URL: %u byte skrivna, %s", (unsigned)written, ok ? "OK" : "FEL"); Serial.println();
  if (ok) { delay(500); ESP.restart(); }
}

void handleFavicon() {
  web.send(204, "text/plain", "");
}

void handleNotFound() {
  web.send(404, "text/plain", "404");
}

// ==========================================
// 11. WI-FI
// ==========================================
// AP-läget är bara en reservväg in: kortet fortsätter försöka nå hemnätet och
// stänger AP:t så fort STA-anslutningen fungerar (se wifiTask längre ner).
static bool     apRunning          = false;
static bool     lanServicesStarted = false;
static uint32_t lastWifiTry        = 0;

static void startAP() {
  // AP_STA i stället för AP: STA kan fortsätta försöka medan AP:t är uppe
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  apRunning = true;
  Serial.printf("AP: %s  ->  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

// mDNS, OTA och NTP kräver en fungerande STA-anslutning. Anropas både vid
// uppstart och när kortet kommer tillbaka efter ett avbrott.
static void startLanServices() {
  if (lanServicesStarted) return;
  lanServicesStarted = true;

  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local\n", MDNS_NAME);
  }
  ArduinoOTA.setHostname(MDNS_NAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("OTA: start"); });
  ArduinoOTA.onEnd([]()   { Serial.println("\nOTA: klar"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.printf("OTA: %u%%\r", t ? p * 100 / t : 0);
  });
  ArduinoOTA.begin();

  Serial.printf("Wi-Fi OK: %s\n", WiFi.localIP().toString().c_str());
}

// Stabilt och starkt: ingen strömsparning, full sändningseffekt och ett aktivt
// val av den starkaste accesspunkten (routern eller meshen kan ha flera noder
// med samma SSID, och kortet ska inte fastna på den svagaste).
static void prepareRadio() {
  WiFi.persistent(false);
  WiFi.setSleep(false);                  // annars stallar polling, OTA och webben
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);   // max sändningseffekt
}

// Skannar och ansluter till den nod som hörs starkast för vårt SSID.
static void beginSTA() {
  prepareRadio();

  if (wifiSsid.length() == 0) {
    Serial.println("Wi-Fi: inget nät sparat – öppnar eget nät.");
    startAP();
    return;
  }

  int n = WiFi.scanNetworks(false, true);
  int best = -1, bestRssi = -1000;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != wifiSsid) continue;
    int r = WiFi.RSSI(i);
    if (r > bestRssi) { bestRssi = r; best = i; }
  }

  if (best >= 0) {
    uint8_t b[6];
    memcpy(b, WiFi.BSSID(best), 6);
    int ch = WiFi.channel(best);
    WiFi.scanDelete();
    Serial.printf("Wi-Fi: starkaste %s  RSSI %d dBm  kanal %d\n", wifiSsid.c_str(), bestRssi, ch);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str(), ch, b);
  } else {
    WiFi.scanDelete();
    Serial.printf("Wi-Fi: %s syns inte i scanningen – provar ändå\n", wifiSsid.c_str());
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  }
}

static void connectWiFi() {
#if FORCE_AP_MODE
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apRunning = true;
  Serial.printf("AP: %s  ->  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  return;
#else
  WiFi.mode(WIFI_STA);
  Serial.printf("Ansluter till %s", wifiSsid.c_str());
  beginSTA();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    startLanServices();
  } else {
    Serial.println("Ingen Wi-Fi – startar eget nätverk och försöker igen i bakgrunden.");
    startAP();
  }
#endif
}

// ==========================================
// 11c. SJÄLVLÄKNING: håller Wi-Fi vid liv
// ==========================================
// Tidigare försökte kortet bara en gång i setup() och blev kvar i AP-läget om
// routern inte hann svara (t.ex. efter ett strömavbrott). Här försöker vi igen
// var 30:e sekund och stänger AP:t så fort vi är online.
static void wifiTask(void* param) {
  (void)param;
  uint32_t downSince = 0;
  uint32_t lastRssiLog = 0;

  for (;;) {
    if (apWanted && !apRunning) startAP();
    if (WiFi.status() == WL_CONNECTED) {
      downSince = 0;
      if (!lanServicesStarted) startLanServices();
      if (apRunning && !apWanted) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        apRunning = false;
        Serial.println("Wi-Fi tillbaka – AP-läget stängt.");
      }

      int rssi = WiFi.RSSI();
      if (millis() - lastRssiLog >= 60000) {
        lastRssiLog = millis();
        Serial.printf("Wi-Fi: %d dBm\n", rssi);
      }

      // Svag länk: byt till en starkare nod i stället för att tappa paket.
      if (rssi < WIFI_WEAK_RSSI && millis() - lastWifiTry >= 60000) {
        lastWifiTry = millis();
        Serial.printf("Wi-Fi: svag signal (%d dBm) – söker bättre accesspunkt\n", rssi);
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
        beginSTA();
      }
    } else {
      if (!downSince) downSince = millis();
      bool apInUse = apRunning && WiFi.softAPgetStationNum() > 0;
      if (!apInUse && millis() - downSince > WIFI_REBOOT_MS) {
        Serial.println("Wi-Fi: offline för länge – startar om kortet.");
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP.restart();
      }
      if (millis() - lastWifiTry >= WIFI_RETRY_MS) {
        lastWifiTry = millis();
        Serial.printf("Wi-Fi: nytt försök mot %s...\n", wifiSsid.c_str());
        beginSTA();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ==========================================
// 11b. MOLNSIDAN (MQTT)
// ==========================================
// Skickar ett kompakt JSON-objekt till den publika MQTT-brokern.
// Sidan hanzpanzi.github.io/blixtradar/husetseffekt.html lyssnar på samma topic.
static void mqttPublishLive() {
  State s;
  xSemaphoreTake(stateMux, portMAX_DELAY);
  s = st;
  xSemaphoreGive(stateMux);

  if (!s.valid) return;

  String json;
  json.reserve(320);
  json += "{\"w\":"   + String(s.powerW, 1);
  json += ",\"v\":"   + String(s.voltageV, 1);
  json += ",\"a\":"   + String(s.currentA, 2);
  json += ",\"tot\":" + String(s.totalWh, 1);
  json += ",\"boot\":" + String(s.whBoot, 0);
  json += ",\"h1\":"  + String(s.whHour1, 0);
  json += ",\"today\":" + String(s.whToday, 0);
  json += ",\"n\":"   + String(s.phases);
  json += ",\"ok\":1";
  json += ",\"up\":"  + String(millis() / 1000UL);
  json += ",\"ph\":[";
  for (int i = 0; i < 3; i++) {
    if (i) json += ",";
    json += "[" + String(s.phW[i], 1) + "," + String(s.phV[i], 1) + "," +
            String(s.phA[i], 2) + "," + String(s.phWh[i], 0) + "]";
  }
  json += "]}";

  // Retained: en webbsida som ansluter sent får ändå senaste värdet direkt
  mqttClient.publish(MQTT_TOPIC, json.c_str(), true);
}

static void mqttTask(void*) {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(640);
  mqttClient.setKeepAlive(30);

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqttClient.connected()) {
        if (millis() - lastMqttTry > 5000) {
          lastMqttTry = millis();
          if (mqttClient.connect(MQTT_CLIENT_ID)) {
            Serial.println("MQTT: ansluten till broker.emqx.io");
          } else {
            Serial.printf("MQTT: misslyckades (%d)\n", mqttClient.state());
          }
        }
      } else {
        mqttClient.loop();
        if (millis() - lastMqttPub >= MQTT_PUB_MS) {
          lastMqttPub = millis();
          mqttPublishLive();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ==========================================
// 11d. WI-FI-INSTÄLLNINGAR (utan USB)
// ==========================================
// Kortet ska kunna flyttas till ett annat nät utan att kompileras om: här byts
// SSID/lösenord, och här öppnas det egna nätet på begäran när man står bredvid
// kortet utan router.
const char HTML_WIFI[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Wi-Fi – Husets effekt</title>
<style>
  body{font-family:system-ui,sans-serif;background:#0f172a;color:#f8fafc;padding:16px;margin:0}
  .card{background:#1e293b;border:1px solid #334155;border-radius:18px;padding:20px;max-width:420px;margin:0 auto}
  h1{color:#38bdf8;font-size:1.25em;margin:0 0 6px}
  label{display:block;color:#94a3b8;font-size:.75em;text-transform:uppercase;letter-spacing:.8px;margin-top:14px}
  input{width:100%;box-sizing:border-box;margin-top:6px;padding:11px;border-radius:10px;
        border:1px solid #334155;background:#0f172a;color:#f8fafc;font-size:1em}
  button{width:100%;margin-top:12px;padding:12px;border:0;border-radius:10px;
         background:#38bdf8;color:#0f172a;font-weight:700;font-size:1em}
  button.ghost{background:transparent;color:#38bdf8;border:1px solid #38bdf8}
  #msg{color:#94a3b8;font-size:.85em;min-height:1.2em;margin-top:10px}
  .foot{color:#64748b;font-size:.78em;margin-top:14px;line-height:1.5}
  a{color:#38bdf8;font-size:.9em;text-decoration:none}
</style></head>
<body><div class="card">
<h1>Wi-Fi</h1>
<div id="msg">läser…</div>
<label>Nätverk (SSID)</label>
<input id="ssid" autocapitalize="none" autocorrect="off" spellcheck="false">
<label>Lösenord</label>
<input id="pass" type="password" placeholder="lämna tomt för att behålla nuvarande">
<button onclick="save()">Spara och starta om</button>
<button class="ghost" onclick="openAp()">Öppna eget nät</button>
<p class="foot">Eget nät: <b>Husets effekt</b> / 12345678<br>Anslut och öppna <b>http://192.168.4.1/</b></p>
<a href="/installningar">← Inställningar</a>
</div>
<script>
const $ = id => document.getElementById(id);
function say(t){ $('msg').textContent = t; }
async function load(){
  const r = await fetch('/api/wifi', {cache:'no-store'});
  const d = await r.json();
  $('ssid').value = d.ssid || '';
  say(d.ip ? ('Ansluten till ' + d.ip + (d.rssi ? ' (' + d.rssi + ' dBm)' : '')) : 'Inte ansluten till något nät');
}
async function post(body){
  const r = await fetch('/api/wifi', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:new URLSearchParams(body)});
  return r.json();
}
async function save(){
  say('Sparar…');
  const d = await post({ssid:$('ssid').value, pass:$('pass').value});
  say(d.ok ? 'Sparat – kortet startar om och ansluter till det nya nätet.' : (d.err || 'Fel'));
}
async function openAp(){
  const d = await post({ap:'1'});
  say(d.ok ? 'Eget nät öppnat: Husets effekt / 12345678' : (d.err || 'Fel'));
}
load();
</script></body></html>
)rawliteral";

void handleWifiPage() {
  if (!requireAuth()) return;
  web.send_P(200, "text/html", HTML_WIFI);
}

void handleWifiGet() {
  if (!requireAuth()) return;

  String json = "{";
  json += "\"ssid\":\"" + wifiSsid + "\"";
  json += ",\"ip\":\"";
  json += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  json += "\"";
  json += ",\"rssi\":";
  json += String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  json += ",\"ap\":";
  json += apRunning ? "true" : "false";
  json += ",\"clients\":";
  json += String(WiFi.softAPgetStationNum());
  json += "}";
  web.send(200, "application/json", json);
}

void handleWifiSave() {
  if (!requireAuth()) return;

  // Begäran om att öppna eller stänga det egna nätet
  if (web.hasArg("ap")) {
    bool on = web.arg("ap") == "1";
    apWanted = on;
    if (!on && apRunning) {
      WiFi.softAPdisconnect(true);
      apRunning = false;
      if (WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);
    }
    web.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  String ssid = web.arg("ssid");
  String pass = web.arg("pass");
  ssid.trim();

  if (ssid.length() < 1 || ssid.length() > 32) {
    web.send(400, "application/json", "{\"ok\":false,\"err\":\"SSID måste vara 1-32 tecken\"}");
    return;
  }
  if (pass.length() > 0 && pass.length() < 8) {
    web.send(400, "application/json", "{\"ok\":false,\"err\":\"Lösenordet måste vara minst 8 tecken\"}");
    return;
  }

  prefs.putString("ssid", ssid);
  if (pass.length() > 0) prefs.putString("wpass", pass);
  wifiSsid = ssid;
  if (pass.length() > 0) wifiPass = pass;
  apWanted = false;
  Serial.printf("Wi-Fi: nytt nät sparat (%s) – startar om.\n", ssid.c_str());

  web.send(200, "application/json", "{\"ok\":true}");
  delay(800);
  ESP.restart();
}

// ==========================================
// 12. SETUP / LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Husets effekt (ESP32-S3) ===");

  prefs.begin("shellyeffekt", false);

  wifiSsid = prefs.getString("ssid", WIFI_SSID);
  wifiPass = prefs.getString("wpass", WIFI_PASSWORD);

  resetArrays();
  st.host = prefs.getString("host", DEFAULT_SHELLY_HOST);
  st.user = prefs.getString("user", DEFAULT_SHELLY_USER);
  st.pass = prefs.getString("pass", DEFAULT_SHELLY_PASS);
  st.err  = "startar";

  rs.sub   = prefs.getString("ddnsSub", "");
  rs.token = prefs.getString("ddnsTok", "");

  // Webbkonto: salt + SHA-256, aldrig klartext
  webUser = prefs.getString("wuser", DEFAULT_WEB_USER);
  webSalt = prefs.getString("wsalt", "");
  webHash = prefs.getString("whash", "");
  if (webSalt.length() == 0 || webHash.length() == 0) {
    webSalt = randomHex(8);
    webHash = sha256hex(webSalt + DEFAULT_WEB_PASS);
    prefs.putString("wuser", webUser);
    prefs.putString("wsalt", webSalt);
    prefs.putString("whash", webHash);
  }

  stateMux  = xSemaphoreCreateMutex();
  remoteMux = xSemaphoreCreateMutex();
  if (!stateMux || !remoteMux) {
    Serial.println("Kunde inte skapa mutex – startar om.");
    ESP.restart();
  }

  connectWiFi();

  // WebServer måste explicit samla in Cookie-headern för att web.header("Cookie") ska fungera
  static const char* AUTH_HEADERS[] = { "Cookie" };
  web.collectHeaders(AUTH_HEADERS, 1);

  web.on("/", handleLivePage);
  web.on("/installningar", handleRoot);
  web.on("/login", HTTP_GET, handleLoginPage);  web.on("/login", HTTP_POST, handleLoginPost);
  web.on("/logout", handleLogout);
  web.on("/api/power", handlePower);
  web.on("/api/live", handleLive);
  web.on("/api/history", handleHistory);
  web.on("/api/config", HTTP_GET, handleConfigGet);
  web.on("/api/config", HTTP_POST, handleConfigSave);
  web.on("/api/test", HTTP_POST, handleTest);
  web.on("/api/remote", HTTP_GET, handleRemoteGet);
  web.on("/api/remote", HTTP_POST, handleRemoteSave);
  web.on("/api/account", HTTP_POST, handleAccountSave);
  web.on("/api/restart", HTTP_POST, handleRestart);
  web.on("/update", HTTP_GET, handleOTA);
  web.on("/update", HTTP_POST, handleOTAUpload, handleOTAProgress);
  web.on("/api/ota", HTTP_POST, handleOtaFromUrl);
  web.on("/wifi", HTTP_GET, handleWifiPage);
  web.on("/api/wifi", HTTP_GET, handleWifiGet);
  web.on("/api/wifi", HTTP_POST, handleWifiSave);
  web.on("/favicon.ico", HTTP_GET, handleFavicon);
  web.onNotFound(handleNotFound);
  web.begin();
  Serial.printf("Webbserver igång. Shelly: %s\n", st.host.c_str());
  Serial.printf("Logga in som %s (byt lösenord i webbsidan)\n", webUser.c_str());

  xTaskCreatePinnedToCore(shellyTask, "shelly", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(remoteTask, "remote", 12288, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(mqttTask, "mqtt", 8192, nullptr, 1, nullptr, 0);
#if !FORCE_AP_MODE
  // Håller Wi-Fi vid liv och tar kortet ur AP-läget när routern svarar igen
  xTaskCreatePinnedToCore(wifiTask, "wifi", 4096, nullptr, 1, nullptr, 0);
#endif
}

void loop() {
  web.handleClient();
  if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

  // Håll minutbuckets i takt även om Shelly skulle vara offline
  static uint32_t lastRotate = 0;
  uint32_t now = millis();
  if (now - lastRotate >= 1000) {
    lastRotate = now;
    xSemaphoreTake(stateMux, portMAX_DELAY);
    rotateMinutes(now);
    updateClockBuckets();
    xSemaphoreGive(stateMux);
  }

  delay(2);
}

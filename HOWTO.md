# 🔧 How-to: Bygg din egen dieselvärmare-styrning med ESP32-S3

Guide för dig som vill fjärrstyra en billig dieselvärmare (Vevor/liknande
med Bluetooth-app) med en ESP32-S3: webbsida, schemaläggning med
veckodagar, moln via MQTT och trådlösa uppdateringar — utan att tappa
app-funktionen. Inga förkunskaper utöver grundläggande Arduino-IDE.

> Allt nedan är generiskt — byt ut lösenord, ämnesnamn och IP mot dina egna.

---

## 1. Det du behöver

| Del | Kommentar |
|-----|-----------|
| ESP32-S3-utvecklingskort | Med Bluetooth LE 5 (t.ex. DevKitC-1 eller klon, ~100 kr) |
| Dieselvärmare med BLE-app | Vevor "Heater5905"/AirHeaterPro-typ — värmaren tar bara **en** BLE-klient |
| 5 V USB-försörjning | Kortet ska sitta nära värmaren (BLE-antennen är liten) |
| Arduino IDE 2.x + arduino-cli | ESP32-paketet: Boards Manager URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json` |
| Bibliotek | BLE, ESPAsyncWebServer (+ AsyncTCP), PubSubClient, ArduinoJson, Preferences |

## 2. Så fungerar upplägget

```
[Telefon-app]--(BLE, vid behov)--[Värmaren]
                                     |
[ESP32-S3]----(BLE, alltid på)-------+
    |
    +--(WiFi hemma)--> Webbsida + REST-API + schemamotor (flashminne)
    +--(MQTT/TLS)----> Publik broker --> Moln-webbsida var som helst
```

- **ESP32:n äger BLE-kopplingen** dygnet runt och översätter kommandon
  från webb/moln till värmarens råa protokoll.
- **Appen kan fortfarande användas**: en "Släpp Bluetooth"-funktion kopplar
  ifrån i N minuter så appen får värmaren för sig själv (värmaren tar bara
  en klient!).
- **Scheman** lagras i flashminnet (Preferences) och triggas av kortets
  NTP-synkade klocka — de överlever strömavbrott.

## 3. Kärnan i BLE-protokollet (Vevor "ABBA")

Värmaren annonserar som t.ex. `Heater5905`. Skriv råa ramar till
skriv-karakteristiken; status kommer som notiser. Ramformat:

```
ba ab 04 <cmd> <arg> 00 00 <checksum>     checksum = summa av alla byte & 0xFF
```

| Kommando | Betydelse | Testat beteende |
|----------|-----------|-----------------|
| `0xA0` | Statusfråga | Svarar med 20-byte statusram |
| `0xA1` | På/av-toggla | Växlar driftläge |
| `0xA2` / `0xA3` | Nivå +1 / −1 | Stegar brännarnivån (så gör appen) |
| `0xDB <v>` | Lagra värde | Sätter **mål**, men växlar inte läge |
| `0xAD` | Aktivera temperatur-läge | Följ alltid efter `0xDB <8–36>` |
| `0xAC` | Aktivera nivå-läge | Nivå styrs i praktiken med `0xA2/0xA3` |

**Statusramen** (efter `0xA0`): byte 6 = aktuell modulering/nivå,
byte 7 = satt värde, byte 5 = läge (0 = nivå, 1 = temperatur),
byte 8 = auto-start/stop-flagga, plus innetemp, värmartemp, spänning.

⚠️ **Lärdomar som sparar dig timmar:**
1. **Checksumma på ALLT** — ramar utan checksumma ignoreras tyst.
2. **Lägeskommandona är lätta att förväxla** — `0xAD` är temperatur-läge,
   `0xAC` nivå-läge. Bevisa med råa statusramar, inte antaganden.
3. **Skriv utan respons** (WRITE_NO_RSP) mot skriv-karakteristiken.
4. **Max TX-effekt**: `esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20)`
   — standard +9 dBm vs +20 dBm är en fördubbling av räckvidden.
5. **Logga råramar** under utveckling (i vårt fall ett `dbg`-fält i API:t) —
   protokollet är odokumenterat och bara jämförelse av ramar avslöjar buggar.

## 4. Byggstenar i firmware (`varmare.ino` som mall)

1. **BLE-klient** (BLELib): skanna → matcha namn → connect → MTU 185 →
   prenumerera notiser → skriv kommandon. Auto-återanslut i en FreeRTOS-task.
2. **Statusparser** → struct med temp/volt/läge/mål → senaste värden cache.
3. **Webb-API** (ESPAsyncWebServer):
   - `GET /api/status` — JSON med alla värden + fw-version
   - `POST /api/power|temp|level` — styrning (temp: `0xDB v` + paus + `0xAD`)
   - `GET/POST/DELETE /api/schedules` — scheman med veckodags-bitmask,
     `on`/`off` i minuter (off < on = över midnatt)
   - `POST /api/ble-release` — app-fönster: koppla ifrån N minuter
   - Lösenord via `?pwd=`-parameter + no-cache-headers (annars fastnar
     webbläsaren på gammal sida!)
4. **Schemamotor**: NTP (`configTime`, TZ) + minut-tick som matchar
   veckodag + tid, startsekvens *på → vänta 800 ms → läge → vänta → temp*
   (värmaren hinner annars inte registrera snabba kommandon).
5. **MQTT** (PubSubClient, **`setBufferSize(512)`** — standard 256 räcker
   inte för status-JSON!): publicera status var 5:e s, ta emot kommandon
   på `<topic>/cmd`, scheman via `sched_get/add/del`.
6. **OTA** (`AsyncElegantOTA` eller WebServer-uppdatering) — uppdatera
   aldrig igen via USB-kabel.

## 5. Kom igång steg för steg

1. **Installera** Arduino IDE + ESP32-paket + biblioteken ovan.
2. **Koppla upp firmware** till ditt nät: WiFi-SSID/lösenord, webb-lösenord,
   MQTT-ämnen (unika! skriv inte över någon annans — byt t.ex. till
   `dittnamn/heater`), NTP och TZ.
3. **Programmera kortet** via USB (ESP32-S3: "ESP32S3 Dev Module",
   USB-CDC enabled). Notera IP:n från serie-monitor.
4. **Testa BLE** först med ett Python/bleak-skript mot värmaren — verifiera
   att du kan toggla på/av och läsa status innan du bygger resten.
5. **Bygg webbsidan** — börja med status + start/stopp, lägg scheman och
   moln sedan. Kopiera gärna panelens layout (dark card + pickers).
6. **Molnet**: skapa ett GitHub-repo med sidan, GitHub Pages på; MQTT:
   broker.emqx.io (publik) eller din egen Mosquitto — använd TLS-porten
   och ett lösenord i kommandon, **inga hemligheter i sidkoden** (Pages
   är publikt för världen!).
7. **Sätt kortet nära värmaren** (1–2 m, fritt, inte mot metall) och låt
   det köra.

## 6. Säkerhet & drift

- **Lösenord**: aldrig i publika filer eller kommentarer — och aldrig
  inkompatibla binärer i publika repo (firmware innehåller strängarna!).
- **MQTT**: unika ämnen + pwd i varje kommando; tro publik broker är avlyssnad.
- **Värmaren är eld**: testregeln — styr aldrig uppvärmning du inte kan
  avbryta fysiskt; behåll alltid appen som reservväg ("Släpp BT").
- **Klockan**: scheman kräver NTP — verifiera att kortet visar rätt tid
  innan du litar på starttiden.

## 7. Felsökning (de klassiska)

| Symptom | Trolig orsak |
|---|---|
| Kommandon "försvinner" | Saknad checksumma, eller skriv med respons till karaktäristik som bara tar utan |
| Värmaren syns inte i skanning | Någon är redan ansluten (app öppen?) eller för svag signal |
| Koppling ok men temp –99 | Fel statusbytes-parsning — jämför råramar |
| Moln-status tystnar | PubSubClient-bufferfull: `setBufferSize(512)` |
| Sidan visar gammal låssida | Webbläsarcache — skicka `Cache-Control: no-store` i firmware |
| Schemat startar inte | Fel klocka (NTP), eller värmaren utan ström just då |
| "Låst"-deadlock | Utan no-cache-header + lösenordsruta med faktiskt inputfält |

---

*Byggt och levt: hela flödet (BLE-protokoll med checksummor, lägeskommandon,
nivå-stegning, släpp-BT-fönster, scheman med över-midnatt, moln via MQTT,
OTA) är verifiersat mot en riktig Vevor — se README.md i samma mapp för
protokolldetaljer och START.md för användarguide.*

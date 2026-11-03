# Husets effekt (ESP32-S3)

Läser en **Shelly Gen1**-effektmätare över Wi-Fi och visar förbrukning på en
egen webbsida – med inloggning, 3-fasvy, live-graf och OTA-uppdatering.

Hittad mätare på nätet `getingbo`: **Shelly 3EM** på `192.168.0.154`
(`type: SHEM-3`, 3 mätare). Inloggningen är påslagen och förifylld
(`hanzpanzi` / `SixtenCozmo2026!`).

## Vad sidan visar

| Värde | Källa |
|---|---|
| **Just nu (W)** + live-graf 5 min | `emeters[].power`, pollas var 2 s |
| **Snittspänning** + spänningsgraf 5 min | `emeters[].voltage` |
| **Vilken fas som drar mest** (snitt 5 min) | snitt av `emeters[].power` per fas |
| Mätarens total (kWh) | `emeters[].total` (Wh) |
| Sedan start (Wh/kWh) | integrerat i firmware sedan boot |
| Senaste 1 h | rullande 60-minutersfönster |
| Idag | integrerat per dygn (lokal tid, NTP) |
| Per fas L1/L2/L3 | `power`, `voltage`, `current`, `total` per `emeter` |
| 24 h-diagram | energibuckets per timme |

Fungerar även med Shelly Plug S / Shelly EM (då visas bara en fas).

## Första start

1. Kortet ansluter till `getingbo` (ändra `WIFI_SSID` / `WIFI_PASSWORD`
   högst upp i `ShellyEffekt.ino`).
2. Misslyckas det startar kortet ett eget Wi-Fi: **Husets effekt / 12345678**.
3. Öppna `http://husetseffekt.local` (mDNS) eller kortets IP.
4. Logga in: **hanzpanzi / SixtenCozmo2026!** – byt vid behov under
   *Konto & säkerhet*.

Shelly-adressen är förifylld med `192.168.0.154`. Vill du byta mätare ändrar du
`DEFAULT_SHELLY_HOST` / `DEFAULT_SHELLY_USER` / `DEFAULT_SHELLY_PASS` högst upp i
koden och laddar upp igen (OTA). Sidan visar bara mätvärden – inga
inställningspaneler.

## Fjärråtkomst ("online")

DuckDNS-inställningarna finns kvar i firmwaren men inte längre i webbsidan.
Enklaste vägen:

1. Skaffa ett gratisnamn på [duckdns.org](https://www.duckdns.org) och kopiera
   din token.
2. Lägg in namn + token med API:et när du är inloggad:
   `POST /api/remote` med fälten `sub` och `token`. Kortet uppdaterar DNS:en
   var 5:e minut.
3. Öppna port **80/TCP** i routern mot kortets LAN-IP.
4. Surfa till `http://<namn>.duckdns.org`.

Har du CGNAT (vanligt hos vissa fiber/4G-leverantörer) funkar inte portforward.
Använd då en tunnel från en dator som alltid är på, t.ex.:

```
ssh -T -R 80:192.168.0.xxx:80 serveo.net
# eller Cloudflare Tunnel: cloudflared tunnel --url http://192.168.0.xxx
```

> HTTP utan TLS. Använd ett långt lösenord, och lägg gärna Cloudflare/HTTPS
> framför om du exponerar sidan mot internet.

## OTA

- **Via sidan:** `http://husetseffekt.local/update` → välj `.bin` + OTA-lösenord
  (`perkele` i koden). Kräver inloggning.
- **Via Arduino IDE:** välj porten `husetseffekt` (nätverksport) och ladda upp.
- Binärfil: *Sketch → Export Compiled Binary* eller
  `build/esp32.esp32.esp32s3/ShellyEffekt.ino.bin`.

## API (kräver inloggning)

| Endpoint | Innehåll |
|---|---|
| `GET /api/power` | allt: effekt, spänning, ström, total, 1 h, idag, faser |
| `GET /api/live` | `{"w":[...],"v":[...],"phAvg":[...]}` senaste ~5 min (150 prov à 2 s) |
| `GET /api/history` | `{"labels":[...],"kwh":[...]}` 24 timmar |
| `GET/POST /api/config` | Shelly host/användare/lösenord |
| `POST /api/test` | testar anslutning utan att spara |
| `GET/POST /api/remote` | DuckDNS-inställningar |
| `POST /api/account` | byt webbkonto |
| `POST /api/restart` | starta om kortet |

## Bygga & flasha

```powershell
$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli compile --upload -p COM6 --fqbn esp32:esp32:esp32s3 "C:\Users\Hanzpanzi\Desktop\Blixt\ShellyEffekt"
```

Kräver ESP32-kärnan (testad med 3.3.11) och ArduinoJson 7.x.

## Filer

- `ShellyEffekt.ino` – allt i en fil (Wi-Fi, Shelly-polling, inloggning,
  DuckDNS, webb-UI, OTA).

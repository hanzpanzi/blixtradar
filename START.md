# 🚀 Startguide — Dieselvärmare (Vevor Heater5905 + ESP32-S3)

Kortfattat: ESP32-kortet sitter vid värmaren, håller Bluetooth-kopplingen
till värmaren och visar allt på webbsidor. Du kan styra temp, nivå,
start/stopp och schemalägga veckodagar + tider.

---

## 1. Flasha firmware (första gången, eller om kortet är nytt)

**Via USB-kabel (endast gång det behövs):**

1. Koppla kortet till datorn med USB-kabel.
2. Dubbelklicka på **`flash_varmare_usb.bat`** — klart på någon minut.
   (Kräver arduino-cli; ligger i Arduino-IDE-installationen.)

**Via webbsidan (OTA — alla senare uppdateringar):**

1. Öppna webbsidan (adress nedan).
2. Rulla till **⬆ Firmware (OTA)** → välj `varmare.ino.bin` → **Ladda upp**.
3. Kortet startar om automatiskt. Versionen syns i meta-raden ("fw 3.4" osv.)

Färdig firmware-binär som motsvarar vad som körs just nu:
`varmare.merged.bin` (hela flashminnet, för esptool) — OTA-filen
`varmare.ino.bin` byggs varje gång koden kompileras.

---

## 2. Öppna webbsidan (hemma på WiFi)

**Adress:** http://<kortets-ip>:8081/
**Lösenord:** `ditt lösenord` (finns förifyllt i panelen hemma)

Allt du behöver finns på sidan:

- **Temperatur, värmartemp, batteri** — live var 5:e sekund
- **▶ Starta / ■ Stäng av** — direkt styrning
- **Måltemperatur (8–36 °C)** och **Effektnivå (1–10)**
- **⏰ Schemaläggning** — veckodagar, starttid, valfri sluttid
  (sluttid före starttid = över midnatt, t.ex. 22:00→06:15), temperatur.
  Sparas i flashminnet — gäller även efter strömavbrott. Max 8 scheman.
- **📲 Släpp Bluetooth** — kopplar ifrån i valda minuter (1–120) så att
  AirHeaterPro-appen kan använda värmaren. Kortet återansluter självt efteråt.
- **⬆ Firmware (OTA)** — trådlösa uppdateringar

## 3. Använda molnet (utanför hemmet — 4G, jobb, semester)

**Adress:** https://<ditt-github-användarnamn>.github.io/<ditt-repo>/varmare/cloud.html
**Lösenord:** `ditt lösenord` (frågas en gång per session)

Fungerar var som helst i världen via MQTT-brokern (broker.emqx.io):

- Samma status som hemma (innetemp, batteri, driftstatus)
- Starta/stäng av, nödstopp, temperatur, nivå
- **⏰ Schemaläggning — även från molnet** (kräver fw 3.4+)

Kommandon körs när kortet når brokern (kortet skickar status var 5:e sekund).
Publik panel hemma: https://<ditt-github-användarnamn>.github.io/<ditt-repo>/varmare/bilvarmare.html

## 4. Vardagsrutin + tips

- **Schemat sköter starten** — t.ex. Fr 05:50 startar med 21 °C på minuten,
  förutsatt att värmaren har ström och kortet är kopplat.
- **Ska du använda appen?** Tryck först "📲 Släpp Bluetooth" på webbsidan.
  Värmaren tar bara EN Bluetooth-klient åt gången.
- **Temp = –99 eller "offline"?** Värmaren har tappat ström eller signalen
  är för svag — flytta kortet närmare värmaren (1–2 m fritt).
- **Batteriet:** kortet klarar sig, men värmaren behöver 12 V-källan.

## 5. Felsökning

| Problem | Åtgärd |
|---|---|
| Webbsidan når inte kortet | Kolla att kortet har ström och sitter på WiFi (samma nät som telefonen/datorn) |
| "Låst" på sidan | Lösenordet är `ditt lösenord`, eller öppna http://<kortets-ip>:8081/?pwd=ditt lösenord |
| Temp visas ej (–99) | Värmaren är utanför Bluetooth-räckvidd eller avstängd; vänta 1 min, kolla strömmen |
| Appen hittar inte värmaren | Tryck "📲 Släpp Bluetooth" 10 min på webbsidan först |
| Molnsidan visar "offline" | Kortet hemma tappat WiFi/ström — kolla hemma; molnet är nästan alltid uppe |
| Uppdateringen "tog inte" | Kolla fw-versionen i meta-raden; kompilera om och OTA:a igen |

Testskript (från dator, valfri tid): `python mqtt_check.py` visar live-status
från molnet; `python mqtt_sched_test.py` hämtar schemalistan via molnet.

Mer detaljer (protokoll, kommandon, arkitektur): se `README.md`.

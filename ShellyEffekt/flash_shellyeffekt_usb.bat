@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
title Flasha ShellyEffekt (Husets effekt) via USB

REM ============================================================
REM  Flashar ShellyEffekt v1.6 till ESP32-S3 via USB-kabel.
REM  1. Koppla ESP32:n till PC:n med USB-datakabel
REM  2. Dubbelklicka pa den har filen
REM ============================================================

set "ESPTOOL=C:\Users\Hanzpanzi\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe"
set "BIN=%~dp0build\ShellyEffekt.ino.merged.bin"

if not exist "%ESPTOOL%" set "ESPTOOL=C:\Users\Hanzpanzi\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
if not exist "%BIN%" (
  echo FEL: Hittar inte firmware-filen:
  echo   %BIN%
  echo Bygg om firmwaren forst.
  pause
  exit /b 1
)

echo.
echo ==============================================================
echo   Flashar SHELLYEFFEKT (Husets effekt) v1.6
echo   Fil: %BIN%
echo ==============================================================
echo.
echo Letar efter ESP32 pa USB... (koppla in kortet om det inte redan ar det)

set FOUND=
for /f "tokens=3" %%P in ('"%ESPTOOL%" --chip esp32s3 chip_id 2^>nul ^| findstr /C:"Serial port"') do set FOUND=%%P

if not defined FOUND (
  echo.
  echo FEL: Ingen ESP32 hittad. Kontrollera:
  echo   - USB-kabeln sitter i (datakabel, inte laddningskabel)
  echo   - Dra ur och i kabeln; hall BOOT intryckt medan du trycker RESET
  echo   - Installera CP210x- eller CH340-drivrutin om porten saknas
  pause
  exit /b 1
)

echo ESP32 hittad pa %FOUND% - flashar... (ca 30-60 sekunder, bryt inte)
"%ESPTOOL%" --chip esp32s3 --port %FOUND% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x0 "%BIN%"
if errorlevel 1 (
  echo.
  echo FLASH MISSLYCKADES - prova igen med en annan kabel/port.
  pause
  exit /b 1
)

echo.
echo ==============================================================
echo   KLART! Kortet startar om med v1.6.
echo   - Det ansluter till getingbo och stanger sitt eget AP
echo   - Tappar det nagot, forsoker det igen var 15:e sekund
echo   - Wi-Fi byts utan USB pa http://kortets-ip/wifi
echo   - Publika sidan: https://hanzpanzi.github.io/blixtradar/husetseffekt.html
echo ==============================================================
pause

// Hämtar Greenelys kvartspriser för SE3 och skriver priser.json.
// Greenelys backend släpper bara in anrop med deras egen Referer, så det här
// skriptet körs av GitHub Actions (se .github/workflows/priser.yml) i stället
// för direkt i webbläsaren. Sidan läser sedan filen från samma domän.
import { readFile, writeFile } from 'node:fs/promises';

const AREA = 'SE3';
const ENDPOINT = 'https://backend.greenely.com/v1/website/spot-price';
const HEADERS = {
  Accept: 'application/json',
  Referer: 'https://www.greenely.com/elpriser/se3',
  'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130 Safari/537.36'
};

// Greenelys lokala datum i Stockholm, oberoende av serverns tidszon.
function stockholmDay(offset = 0) {
  const d = new Date();
  d.setUTCHours(12, 0, 0, 0);                 // undvik strul vid sommartid
  d.setUTCDate(d.getUTCDate() + offset);
  return new Intl.DateTimeFormat('sv-SE', { timeZone: 'Europe/Stockholm' }).format(d);
}

async function fetchDay(day) {
  const url = `${ENDPOINT}?include_vat=False&date=${day}&country=se`;
  let lastError;
  for (let attempt = 1; attempt <= 3; attempt++) {
    try {
      const res = await fetch(url, { headers: HEADERS });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const json = await res.json();
      const hours = json[AREA];
      if (!Array.isArray(hours) || hours.length < 24) throw new Error('dagen är inte publicerad än');
      const quarters = [];
      for (const hour of hours) {
        for (const key of ['quarter1', 'quarter2', 'quarter3', 'quarter4']) {
          const value = Number(hour && hour.quarters_prices && hour.quarters_prices[key]);
          quarters.push(value > 0 ? Math.round(value) / 100000 : 0);
        }
      }
      if (quarters.filter((q) => q > 0).length < 90) throw new Error('för få kvartar');
      return quarters.slice(0, 96);
    } catch (err) {
      lastError = err;
      if (attempt < 3) await new Promise((r) => setTimeout(r, 2000 * attempt));
    }
  }
  throw lastError;
}

const days = [stockholmDay(-1), stockholmDay(0), stockholmDay(1)];
const out = { source: 'Greenely', area: AREA, unit: 'kr/kWh', days: {} };

let todayFailed = false;
for (const day of days) {
  try {
    out.days[day] = await fetchDay(day);
    console.log(`${day}: 96 kvartar`);
  } catch (err) {
    console.warn(`${day}: ${err.message}`);
    if (day === days[1]) todayFailed = true;
  }
}

// Behåll dagar som redan finns om ett anrop misslyckades, så filen inte
// töms när t.ex. morgondagens priser ännu inte är publicerade.
try {
  const old = JSON.parse(await readFile('priser.json', 'utf8'));
  for (const [day, quarters] of Object.entries(old.days || {})) {
    if (!out.days[day] && Array.isArray(quarters)) out.days[day] = quarters;
  }
} catch (err) {
  // Första körningen saknar fil – det är helt normalt.
}

if (!out.days[days[1]] || todayFailed) {
  console.error('Kunde inte hämta dagens priser – behåller den gamla filen.');
  process.exit(1);
}

await writeFile('priser.json', JSON.stringify(out) + '\n');
console.log(`Skrev priser.json med ${Object.keys(out.days).length} dagar.`);

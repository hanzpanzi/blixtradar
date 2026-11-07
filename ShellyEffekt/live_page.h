// Genererad fran husetseffekt.html - kor om verktyget om sidan andras.
#pragma once
const char HTML_LIVE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#0f172a">
  <meta name="color-scheme" content="dark">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="apple-mobile-web-app-title" content="Husets effekt">
  <title>⚡ Husets effekt – live</title>
  <style>
    body { font-family: system-ui, -apple-system, sans-serif; background-color: #0f172a; color: #f8fafc; text-align: center; padding: 12px; margin: 0; -webkit-text-size-adjust: 100%; -webkit-tap-highlight-color: transparent; }
    .card { background: #1e293b; border-radius: 20px; padding: 18px; max-width: 460px; margin: 12px auto; box-shadow: 0 10px 25px rgba(0,0,0,0.5); border: 1px solid #334155; }
    h1 { color: #38bdf8; margin: 0 0 4px; font-size: 1.6em; }
    .sub { color: #94a3b8; font-size: 0.8em; }
    .label { color: #94a3b8; font-size: 0.8em; text-transform: uppercase; letter-spacing: 1.1px; }
    .status { background: #0f172a; padding: 12px; border-radius: 12px; font-weight: 600; color: #4ade80; margin: 14px 0; border: 1px solid #1e293b; word-break: break-word; }
    .status.bad { color: #f87171; }
    .seg-value { display:flex; align-items:stretch; justify-content:center; gap:3px; width:100%; max-width:280px; margin:10px auto 0; padding:7px 6px 5px; background:radial-gradient(ellipse at 50% 0%,#171303,#080806 72%); border:1px solid #3a2d1a; border-radius:9px; box-shadow:inset 0 0 18px #000; }
    .seg-value .seg-digit { flex:0 1 46px; min-width:0; max-width:46px; width:auto; height:auto; overflow:visible; filter:drop-shadow(0 0 4px rgba(251,191,36,.28)); }
    .seg-value .seg { fill:rgba(251,191,36,.10); }
    .seg-value .seg.on { fill:#fbbf24; }
    .seg-value.hot .seg.on { fill:#ff4b2e; }
    .seg-value .seg-dp { opacity:0; }
    .seg-value .has-dp .seg-dp { opacity:1; fill:#fbbf24; }
    .seg-value.hot .has-dp .seg-dp { fill:#ff4b2e; }
    .seg-value .seg-digit.blank .seg { fill:rgba(251,191,36,.025); }
    .seg-unit { margin-top:3px; text-align:center; color:#94a3b8; font-size:.72em; letter-spacing:1.1px; text-transform:uppercase; }
    .seg-graph { display:flex; align-items:stretch; justify-content:center; gap:2px; width:100%; min-height:52px; margin-top:8px; padding:6px; background:#0f172a; border:1px solid #263449; border-radius:9px; }
    .seg-graph .seg-digit { flex:1 1 0; min-width:0; max-width:44px; width:auto; height:auto; overflow:visible; }
    .seg-graph .seg { fill:rgba(56,189,248,.10); }
    .seg-graph .lvl-0 .seg.on, .seg-graph .lvl-1 .seg.on { fill:#4ade80; }
    .seg-graph .lvl-2 .seg.on { fill:#a3e635; }
    .seg-graph .lvl-3 .seg.on { fill:#fbbf24; }
    .seg-graph .lvl-4 .seg.on { fill:#fb923c; }
    .seg-graph .lvl-5 .seg.on, .seg-graph .hot .seg.on { fill:#f87171; }
    .seg-graph .seg-digit.now { filter:drop-shadow(0 0 6px rgba(251,191,36,.8)); }
    .seg-graph .seg-digit.empty .seg { fill:rgba(56,189,248,.025); }
    .block.over { border-color: #f87171; box-shadow: 0 0 18px rgba(248,113,113,0.35); }
    .meas { cursor: pointer; transition: border-color .2s, box-shadow .2s; }
    .meas.active { border-color: #38bdf8; box-shadow: 0 0 16px rgba(56,189,248,0.25); }
    .meas.done { border-color: #4ade80; box-shadow: 0 0 16px rgba(74,222,128,0.22); }
    .meas-row { display: flex; justify-content: space-between; align-items: flex-end; gap: 10px; margin-top: 10px; }
    .meas-v { font-size: 1.35em; font-weight: 800; color: #e2e8f0; }
    .grid { display: flex; justify-content: space-around; gap: 8px; }
    .row { display: flex; justify-content: space-around; gap: 10px; }
    .small { color: #94a3b8; font-size: 0.8em; margin-top: 8px; }
    .topbar { display: flex; flex-direction: column; }
    .dash { display: flex; flex-direction: column; gap: 12px; margin-top: 14px; text-align: left; }
    .dash-left { display: flex; flex-direction: column; gap: 12px; }
    .block { background: #0f172a; border: 1px solid #263449; border-radius: 14px; padding: 14px; }

    .badge { display: inline-block; padding: 2px 10px; border-radius: 99px; font-size: 0.78em; font-weight: 700; }
    .on  { background: #14532d; color: #4ade80; }
    .off { background: #7f1d1d; color: #fca5a5; }
    .dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; background: #4ade80; margin-right: 7px; vertical-align: middle; animation: pulse 1.6s infinite; }
    .dot.grey { background: #475569; animation: none; }
    @keyframes pulse { 0%,100% { opacity: 1; box-shadow: 0 0 0 0 rgba(74,222,128,0.55); } 50% { opacity: 0.6; box-shadow: 0 0 0 7px rgba(74,222,128,0); } }
    .row3 { display: flex; justify-content: space-between; color: #64748b; font-size: 0.72em; margin-top: 6px; }
    .tiles { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 12px; }
    .tile { background: #0f172a; border: 1px solid #263449; border-radius: 12px; padding: 10px 6px; text-align: center; }
    .tile .v { font-size: 1.05em; font-weight: 700; margin-top: 5px; color: #e2e8f0; }
    .hourlist { margin-top: 8px; max-height: 300px; overflow-y: auto; }
    .hrow { display: flex; gap: 8px; align-items: baseline; padding: 5px 2px; border-bottom: 1px solid #1e293b; font-size: 0.85em; }
    .hrow:last-child { border-bottom: none; }
    .hrow .ht { width: 64px; text-align: left; color: #94a3b8; }
    .hrow .hk { flex: 1; text-align: right; color: #e2e8f0; }
    .hrow .hp { width: 58px; text-align: right; color: #64748b; }
    .hrow .hc { width: 72px; text-align: right; color: #fbbf24; font-weight: 600; }
    .hrow.head { border-bottom: 1px solid #334155; padding-bottom: 4px; }
    .hrow.head .ht, .hrow.head .hk, .hrow.head .hp, .hrow.head .hc { color: #64748b; font-size: 0.8em; text-transform: uppercase; letter-spacing: 0.5px; font-weight: 600; }
    .hrow.now { background: rgba(56,189,248,0.10); border-radius: 6px; }
    .hrow.sum { border-top: 1px solid #334155; border-bottom: none; font-weight: 700; margin-top: 2px; }
    .foot { color: #64748b; font-size: 0.78em; text-align: center; margin-top: 14px; line-height: 1.6; }
    .nav { display: flex; gap: 10px; justify-content: center; margin-top: 12px; flex-wrap: wrap; }
    .nav a { color: #94a3b8; text-decoration: none; font-size: 0.8em; border: 1px solid #334155; padding: 6px 10px; border-radius: 9px; }
    @media (hover: hover) {
      .nav a:hover { color: #f8fafc; border-color: #475569; }
    }
    /* ---- Installationsrad: ratt steg for iPhone, Android och Windows ---- */
    .install { display: flex; align-items: center; gap: 10px; max-width: 1200px; margin: 0 auto 8px; padding: 10px 12px; background: #1e293b; border: 1px solid #334155; border-radius: 12px; color: #cbd5e1; font-size: 0.82em; text-align: left; }
    .install[hidden] { display: none; }
    .install button { font: inherit; border-radius: 8px; border: 1px solid #38bdf8; background: #0ea5e9; color: #041018; font-weight: 700; padding: 7px 12px; cursor: pointer; }
    .install button.x { background: transparent; border-color: #334155; color: #94a3b8; padding: 3px 9px; font-size: 1.1em; line-height: 1; }
    /* ---- Rullande notis: lägsta priset nästa 24 h ---- */
    .ticker { max-width: 1200px; margin: 0 auto 8px; overflow: hidden; border: 1px solid #263449; background: #0b1220; border-radius: 12px; }
    .ticker[hidden] { display: none; }
    .ticker-inner { display: inline-flex; white-space: nowrap; will-change: transform; animation: ticker 26s linear infinite; }
    .ticker-inner span { padding: 8px 24px; color: #fbbf24; font-size: 0.84em; font-weight: 600; }
    @keyframes ticker { from { transform: translateX(0); } to { transform: translateX(-50%); } }
    @media (hover: hover) { .ticker:hover .ticker-inner { animation-play-state: paused; } }
    @media (prefers-reduced-motion: reduce) { .ticker-inner { animation: none; } .ticker-inner span + span { display: none; } }
    @media (pointer: fine) {
      .hourlist { scrollbar-width: thin; scrollbar-color: #334155 transparent; }
      .hourlist::-webkit-scrollbar { width: 8px; }
      .hourlist::-webkit-scrollbar-thumb { background: #334155; border-radius: 4px; }
    }
    /* ---- Dator: bred panel med flera spalter ---- */
    @media (min-width: 760px) {
      .card { max-width: 960px; padding: 24px; }
      .topbar { flex-direction: row; align-items: center; gap: 16px; }
      .topbar .status { flex: 1; margin: 0; }
      .dash-left { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; align-items: start; }
      .dash-left > .block:nth-child(1), .dash-left > .block:nth-child(7) { grid-column: 1 / -1; }
      .tiles { grid-template-columns: repeat(4, 1fr); }
    }
    @media (min-width: 1180px) {
      .card { max-width: 1200px; }
      .dash-left { grid-template-columns: 1fr 1fr 1fr; }
      .dash-left > .block:nth-child(1) { grid-column: span 2; }
      .dash-left > .block:nth-child(7) { grid-column: 1 / -1; }
    }
    /* ---- Telefon: fingervanliga ytor, ingen iOS-zoom i falt ---- */
    @media (max-width: 480px) {
      .hrow { font-size: 0.8em; gap: 6px; }
      .hrow .ht { width: 56px; }
      .hrow .hp { width: 52px; }
      .hrow .hc { width: 64px; }
      .nav a { padding: 10px 12px; }
      .hourlist { max-height: none; }
    }
    #feeInput { font-size: 16px; }
    @supports (padding: max(0px)) {
      body { padding-left: max(12px, env(safe-area-inset-left)); padding-right: max(12px, env(safe-area-inset-right)); padding-top: max(12px, env(safe-area-inset-top)); padding-bottom: max(12px, env(safe-area-inset-bottom)); }
    }
  </style>
</head>
<body>
  <div class="install" id="installHint" hidden>
    <span id="installText"></span>
    <button type="button" id="installGo" hidden>Installera</button>
    <button type="button" class="x" id="installClose" aria-label="Stäng">×</button>
  </div>
  <div class="ticker" id="ticker" hidden aria-live="polite">
    <div class="ticker-inner" id="tickerText"><span></span><span aria-hidden="true"></span></div>
  </div>
  <div class="card">
    <div class="topbar">
      <h1>⚡ Husets effekt</h1>
      <div class="status" id="status">Laddar status…</div>
    </div>

    <div class="dash">
      <div class="dash-left">

        <div class="block">
          <div class="label"><span class="dot" id="espDot"></span>Använd effekt · nu</div>
          <div class="seg-value" id="nowW" aria-label="Använd effekt just nu"></div>
          <div class="seg-unit">watt · används nu</div>
          <div class="small" style="text-align:center">det huset drar just nu</div>
          <div class="row3"><span id="espInfo">Använd effekt · spänning · ström</span></div>
          <div class="small" id="lastCalc" style="text-align:center">senast räknat –</div>
          <div class="seg-graph" id="liveSeg" role="img" aria-label="Effekt senaste 30 minuter som segmentdisplay"></div>
          <div class="row3"><span>senaste 30 min</span><span id="liveAvg">snitt --</span><span id="liveMax">max --</span></div>
        </div>

        <div class="block meas" id="measCard" role="button" tabindex="0">
          <div class="label">Förbrukad energi · mätning</div>
          <div class="meas-row">
            <div><div class="meas-v" id="measKwh">0.000 kWh</div><div class="small" style="margin-top:2px;">förbrukad energi</div></div>
            <div style="text-align:right;"><div class="meas-v" id="measKr" style="color:#fbbf24;">–</div><div class="small" style="margin-top:2px;">kostnad inkl överföring</div></div>
          </div>
          <div class="row3"><span id="measTime">00:00 / 60:00</span><span id="measState">tryck för att nollställa</span></div>
        </div>

        <div class="block">
          <div class="label">Nätspänning · snitt</div>
          <div class="row" style="margin-top:6px; align-items:center;">
            <div style="flex:1 1 190px;">
              <div class="seg-value" id="voltAvg" aria-label="Spänning snitt"></div>
              <div class="seg-unit">volt · snitt</div>
            </div>
            <div style="text-align:right; color:#94a3b8; font-size:0.85em;">
              <div>min <span id="voltMin">--</span></div>
              <div>max <span id="voltMax">--</span></div>
            </div>
          </div>
          <div class="seg-graph" id="voltSeg" role="img" aria-label="Nätspänning senaste 30 minuter som segmentdisplay"></div>
          <div class="small" style="margin-top:3px">Nätets spänning kan variera något.</div>
        </div>

        <div class="block">
          <div class="label">Förbrukad energi</div>
          <div class="tiles">
            <div class="tile"><div class="label">Mätarens total</div><div class="v" id="meterTotal">--</div></div>
            <div class="tile"><div class="label">Sedan start</div><div class="v" id="sinceBoot">--</div></div>
            <div class="tile"><div class="label">Senaste 1 timme</div><div class="v" id="hour1">--</div></div>
            <div class="tile"><div class="label">Idag</div><div class="v" id="today">--</div></div>
          </div>
          <div class="small" id="sampleInfo">Senaste 30 minuterna visas som segment när sidan är öppen. Använd effekt mäts i W; förbrukad energi i kWh över tid.</div>
        </div>
        <div class="block">
          <div class="label">Elpris SE3 · idag</div>
          <div class="row" style="margin-top:6px;">
            <div style="flex:1; text-align:left;">
              <div class="seg-value" id="priceNow" aria-label="Elpris nu"></div>
              <div class="seg-unit">kr/kWh · spot ex moms</div>
              <div class="small" id="priceRange">pris saknas</div>
              <div class="small" id="priceTotal"></div>
              <div class="small" id="costToday">idag –</div>
            </div>
            <div style="text-align:right; color:#94a3b8; font-size:0.85em;">
              <div>Max/timme <b id="maxHour" style="color:#e2e8f0;">--</b></div>
              <div class="small" id="maxHourWhen">--</div>
            </div>
          </div>
          <div class="seg-graph" id="priceSeg" role="img" aria-label="Elpris som segmentdisplay"></div>
          <div class="chart-note" style="margin-top:5px;color:#64748b;font-size:9px;letter-spacing:.7px;">Nivå 0–9 · ex moms</div>
        </div>

        <div class="block">
          <div class="label">Förbrukad energi per timme · SE3 + elöverföring</div>
          <div class="small" style="margin-top:6px;display:flex;align-items:center;gap:6px;flex-wrap:wrap;">
            <span>Elöverföring</span>
            <input id="feeInput" type="number" min="0" step="0.1" inputmode="decimal"
                   style="width:64px;background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:6px;padding:3px 6px;font-size:16px;">
            <span>öre/kWh · kostnaden nedan är spot + överföring</span>
          </div>
          <div class="hourlist">
            <div class="hrow head"><span class="ht">timme</span><span class="hk">energi</span><span class="hp">kr/kWh</span><span class="hc">kostnad</span></div>
            <div id="hourList"></div>
          </div>
        </div>

      </div>
    </div>

    <div class="foot" id="foot">Uppgiftshämtning: – –</div>

    <div class="nav">
      <a href="/installningar">Inställningar</a>
      <a href="/logg">Logg</a>
      <a href="/update">Uppdatera</a>
      <a href="/logout">Logga ut</a>
    </div>
  </div>
  <script>
    var MAXPUNK = 900;              // 30 min data i 2 sekuters takt = 900 prov
    var samples = [];               // {w, v, t}
    var latest = null;
    var lastSeenTm = 0;

    function byId(id) { return document.getElementById(id); }

    // Ett saknat eller trasigt fält ska bli 0 – inte krascha .toFixed() nedan.
    function num(v) { return (typeof v === 'number' && isFinite(v)) ? v : 0; }

    function fmtWh(wh) {
      if (wh === null || wh === undefined || isNaN(wh)) return '–';
      if (Math.abs(wh) >= 1000) return (wh / 1000).toFixed(2) + ' kWh';
      return Math.round(wh) + ' Wh';
    }
    function fmtKwh(wh) {
      if (wh === null || wh === undefined || isNaN(wh)) return '–';
      return (wh / 1000).toFixed(2) + ' kWh';
    }

    // Alla mätare och historikrader är 7-segment. Siffrorna byggs som SVG
    // så att de fungerar lika bra på iPhone, Android och Windows.
    var SEG_MAP = {
      '0': 'abcdef', '1': 'bc', '2': 'abdeg', '3': 'abcdg', '4': 'bcfg',
      '5': 'acdfg', '6': 'acdefg', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg'
    };

    function segRects() {
      var r = '', segs = [
        ['a', 10, 4, 36, 8], ['b', 45, 11, 8, 32], ['c', 45, 51, 8, 32],
        ['d', 10, 83, 36, 8], ['e', 3, 51, 8, 32], ['f', 3, 11, 8, 32], ['g', 10, 44, 36, 8]
      ], i;
      for (i = 0; i < segs.length; i++) {
        r += '<rect class="seg" data-seg="' + segs[i][0] + '" x="' + segs[i][1] + '" y="' + segs[i][2] +
             '" width="' + segs[i][3] + '" height="' + segs[i][4] + '" rx="3"/>';
      }
      return r;
    }

    function segMarkup(extra) {
      return '<svg class="seg-digit ' + (extra || '') + '" viewBox="0 0 56 96" aria-hidden="true">' +
             segRects() + '<circle class="seg-dp" cx="51" cy="88" r="4.2"/></svg>';
    }

    function paintSeg(svg, ch) {
      if (!svg) return;
      var on = SEG_MAP[ch] || '', rects = svg.getElementsByTagName('rect'), i;
      for (i = 0; i < rects.length; i++) {
        var seg = rects[i].getAttribute('data-seg');
        rects[i].setAttribute('class', 'seg' + (on.indexOf(seg) >= 0 ? ' on' : ''));
      }
    }

    function segDigits(host, count, extra) {
      if (!host) return [];
      if (!host._segDigits || host._segDigits.length !== count) {
        var html = '', i;
        for (i = 0; i < count; i++) html += segMarkup(extra);
        host.innerHTML = html;
        host._segDigits = host.getElementsByTagName('svg');
      }
      return host._segDigits;
    }

    function setSegMeter(host, value, digits, opt) {
      if (!host) return;
      opt = opt || {};
      var svgs = segDigits(host, digits, 'seg-num'), txt = '';
      if (typeof value === 'number' && isFinite(value)) {
        txt = opt.decimals ? value.toFixed(opt.decimals) : String(Math.round(value));
      }
      var tokens = [], i, c;
      for (i = 0; i < txt.length; i++) {
        c = txt.charAt(i);
        if (c === '.' || c === ',') { if (tokens.length) tokens[tokens.length - 1].dp = true; }
        else if (c >= '0' && c <= '9') tokens.push({ ch: c, dp: false, blank: false });
      }
      while (tokens.length > digits) tokens.shift();
      while (tokens.length < digits) tokens.unshift({ ch: '', dp: false, blank: true });
      for (i = 0; i < digits; i++) {
        var t = tokens[i], svg = svgs[i];
        paintSeg(svg, t.ch);
        svg.setAttribute('class', 'seg-digit seg-num' + (t.dp ? ' has-dp' : '') + (t.blank ? ' blank' : ''));
      }
      host.setAttribute('data-value', txt);
      host.setAttribute('aria-label', (opt.unit || 'värde') + ': ' + (txt ? txt.replace('.', ',') : 'saknas'));
    }

    function segHistory(host, vals, opt) {
      if (!host) return;
      opt = opt || {};
      vals = vals || [];
      var cols = opt.cols || 12, data = [], c, i;
      for (c = 0; c < cols; c++) {
        var a = vals.length ? Math.floor(c * vals.length / cols) : 0;
        var b = vals.length ? Math.floor((c + 1) * vals.length / cols) : 0;
        var sum = 0, top = -Infinity, n = 0;
        for (i = a; i < b; i++) {
          var raw = vals[i];
          if (typeof raw !== 'number' || !isFinite(raw)) continue;
          if (opt.ignoreZero && !(raw > 0)) continue;
          sum += raw; n++;
          if (raw > top) top = raw;
        }
        data.push(opt.agg === 'max' ? (top === -Infinity ? 0 : top) : (n ? sum / n : 0));
      }
      var mn = opt.min, mx = opt.max, f;
      if (mn === undefined || mx === undefined) {
        mn = 1e9; mx = -1e9;
        for (i = 0; i < data.length; i++) {
          if (opt.ignoreZero && !(data[i] > 0)) continue;
          if (data[i] < mn) mn = data[i];
          if (data[i] > mx) mx = data[i];
        }
        if (!(mx >= mn)) { mn = 0; mx = 1; }
        var pad = Math.max((mx - mn) * .25, 0);
        mn -= pad; mx += pad;
      }
      if (!(mx > mn)) mx = mn + 1;
      var svgs = segDigits(host, cols, 'seg-cell');
      var mark = opt.markCol === undefined ? -1 : Math.max(0, Math.min(cols - 1, opt.markCol));
      for (c = 0; c < cols; c++) {
        var svg = svgs[c];
        if (opt.blankZero && !(data[c] > 0)) {
          paintSeg(svg, '');
          svg.setAttribute('class', 'seg-digit seg-cell empty');
          continue;
        }
        f = Math.max(0, Math.min(1, (data[c] - mn) / (mx - mn)));
        paintSeg(svg, String(Math.round(f * 9)));
        var lvl = Math.min(5, Math.max(0, Math.ceil(f * 5)));
        var cls = 'seg-digit seg-cell lvl-' + lvl + (c === mark ? ' now' : '');
        if (opt.kind === 'power' && data[c] >= 2000) cls += ' hot';
        if (opt.kind === 'price' && f >= .82) cls += ' hot';
        svg.setAttribute('class', cls);
      }
      host.setAttribute('data-values', data.map(function (x) { return Math.round(x * 10) / 10; }).join(','));
      if (opt.aria) host.setAttribute('aria-label', opt.aria);
    }
    // Fullständig fras för statusraden: "just nu", "12 sek sedan", "3 min sedan"…
    function liveSince(t) {
      var s = Math.floor((Date.now() - t) / 1000);
      if (s < 5) return 'just nu';
      if (s < 60) return s + ' sek sedan';
      if (s < 3600) return Math.floor(s / 60) + ' min sedan';
      return Math.floor(s / 3600) + ' tim sedan';
    }

    // Statusraden visar elpriset som gäller just nu (SE3, pågående kvart).
    function priceStatusText() {
      var q = priceCache.q || [], t = new Date();
      var p = num(q[t.getHours() * 4 + Math.floor(t.getMinutes() / 15)]);
      if (!(p > 0)) return 'Live – ' + liveSince(lastSeenTm);
      return 'Elpris nu ' + p.toFixed(2) + ' kr/kWh · SE3';
    }

    function setEspBadge(ok) {
      var b = byId('espDot');
      if (b) { b.className = 'dot' + (ok ? '' : ' grey'); }
      var i = byId('espInfo');
      if (i) { i.innerText = ok ? 'Använd effekt · spänning · ström' : 'ESP32: väntar på data'; }
    }

    function setStatus(txt, ok) {
      var s = byId('status');
      s.className = 'status' + (ok ? '' : ' bad');
      s.innerText = txt;
    }

    // ---------- rita segmenthistorik ----------
    function drawGraphs() {
      if (!samples.length) return;
      var ws = [], vs = [], i;
      for (i = 0; i < samples.length; i++) {
        ws.push(samples[i].w);
        if (typeof samples[i].v === 'number' && samples[i].v > 1) vs.push(samples[i].v);
      }

      var mn = 1e9, mx = -1e9, sum = 0;
      for (i = 0; i < ws.length; i++) { if (ws[i] < mn) mn = ws[i]; if (ws[i] > mx) mx = ws[i]; sum += ws[i]; }
      byId('liveAvg').innerText = 'snitt ' + (sum / ws.length).toFixed(0) + ' W';
      byId('liveMax').innerText = 'max ' + mx.toFixed(0) + ' W';

      var vmin = 1e9, vmax = -1e9, vsum = 0, n = 0;
      for (i = 0; i < vs.length; i++) {
        if (vs[i] < vmin) vmin = vs[i];
        if (vs[i] > vmax) vmax = vs[i];
        vsum += vs[i]; n++;
      }
      setSegMeter(byId('voltAvg'), n ? vsum / n : null, 3, { unit: 'volt' });
      byId('voltMin').innerText = n ? vmin.toFixed(1) : '–';
      byId('voltMax').innerText = n ? vmax.toFixed(1) : '–';

      segHistory(byId('liveSeg'), ws, {
        cols: 12, min: 0, max: 5000, agg: 'max', kind: 'power', blankZero: true, markCol: 11,
        aria: 'Effekt senaste 30 minuter som segmentdisplay'
      });
      segHistory(byId('voltSeg'), vs, {
        cols: 12, agg: 'avg', ignoreZero: true, blankZero: true, markCol: 11,
        aria: 'Spänning senaste 30 minuter som segmentdisplay'
      });
    }

    // ---------- uppdaterar vyn ----------
    // ---------- elpris (SE3) och maxtimme ----------

    // Spotpriset hämtas direkt från elprisetjustnu.se (CORS öppen). Kortets
    // priser används om de finns, annars den här kopian.
    var priceCache = { day: '', q: [], q2: [], arr: [], at: 0 };
    var priceLoading = false;

    // Elprisetjustnu ger 96 kvartspriser per dag. Kvartspriset är det som
    // gäller just nu, så både visning och kostnad räknas på kvartar.
    function quarterList(list) {
      var q = [], i;
      for (i = 0; i < 96; i++) q.push(0);
      for (i = 0; i < list.length; i++) {
        var m = String(list[i].time_start || '').match(/T(\d\d):(\d\d)/);
        if (!m) continue;
        var qi = parseInt(m[1], 10) * 4 + Math.floor(parseInt(m[2], 10) / 15);
        if (qi >= 0 && qi < 96) q[qi] = Number(list[i].SEK_per_kWh) || 0;
      }
      return q;
    }

    function loadPrices() {
      var now = new Date();
      var mk = function (d) {
        return d.getFullYear() + '/' + ('0' + (d.getMonth() + 1)).slice(-2) + '-' +
               ('0' + d.getDate()).slice(-2) + '_SE3';
      };
      var day = mk(now);
      var day2 = mk(new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1));
      if (priceLoading) return;
      if (priceCache.day === day && priceCache.q.length && Date.now() - priceCache.at < 900000) return;
      priceLoading = true;
      var get = function (d) {
        return fetch('https://www.elprisetjustnu.se/api/v1/prices/' + d + '.json')
          .then(function (r) { return r.ok ? r.json() : []; })
          .catch(function () { return []; });
      };
      Promise.all([get(day), get(day2)]).then(function (both) {
        var q = quarterList(both[0]), q2 = quarterList(both[1]);
        var arr = [], sum, cnt, h, i, v;
        for (h = 0; h < 24; h++) {
          sum = 0; cnt = 0;
          for (i = 0; i < 4; i++) { v = q[h * 4 + i]; if (v > 0) { sum += v; cnt++; } }
          arr.push(cnt ? sum / cnt : 0);
        }
        priceCache = { day: day, q: q, q2: q2, arr: arr, at: Date.now() };
        priceLoading = false;
        if (latest) render(latest);
      }).catch(function () { priceLoading = false; });
    }

    // Lägsta priset under de kommande 24 timmarna: dagens återstående kvartar
    // plus morgondagens publicerade kvartar (de kommer vid 13-tiden).
    // Algoritmen väljer den billigaste kvarten och, vid lika pris, den första.
    function nextLowest() {
      var now = new Date();
      var q0 = now.getHours() * 4 + Math.floor(now.getMinutes() / 15);
      var q = priceCache.q || [], q2 = priceCache.q2 || [];
      var best = null, i, p;
      for (i = q0; i < 96; i++) {
        p = num(q[i]);
        if (p > 0 && (!best || p < best.p)) best = { p: p, qi: i, dayOff: 0 };
      }
      if (q2.length >= 96) {
        for (i = 0; i < q0; i++) {
          p = num(q2[i]);
          if (p > 0 && (!best || p < best.p)) best = { p: p, qi: i, dayOff: 1 };
        }
      }
      return best;
    }

    function tickerText() {
      var b = nextLowest();
      if (!b) return '';
      var now = new Date();
      var hh = Math.floor(b.qi / 4), mm = (b.qi % 4) * 15;
      var t = new Date(now.getFullYear(), now.getMonth(), now.getDate() + b.dayOff, hh, mm, 0, 0);
      var mins = Math.max(0, Math.round((t - now) / 60000));
      var när = mins < 1 ? 'just nu' :
                (mins < 60 ? 'om ' + mins + ' min' :
                 'om ' + Math.floor(mins / 60) + ' h' + (mins % 60 ? ' ' + (mins % 60) + ' min' : ''));
      var scope = (priceCache.q2 && priceCache.q2.length >= 96) ? 'nästa 24 h' : 'resten av dagen';
      return 'Lägst pris ' + scope + ': ' + b.p.toFixed(2) + ' kr/kWh · ' +
             (b.dayOff ? 'imorgon ' : '') + ('0' + hh).slice(-2) + ':' + ('0' + mm).slice(-2) +
             ' (' + när + ') · SE3';
    }

    function tickerUpdate() {
      var box = byId('ticker'), inner = byId('tickerText');
      if (!box || !inner) return;
      var txt = tickerText();
      if (!txt) { box.hidden = true; return; }
      box.hidden = false;
      if (inner.getAttribute('data-t') === txt) return;
      inner.setAttribute('data-t', txt);
      var spans = inner.getElementsByTagName('span');
      for (var i = 0; i < spans.length; i++) spans[i].textContent = txt;
    }

    function renderPrice(d) {
      loadPrices();
      var qp = priceCache.q;                             // kvartspriser (kr/kWh)
      var qe = (d.q && d.q.length >= 96) ? d.q : null;  // kvartsenergi (Wh) från kortet
      var arr = (d.pr && d.pr.length >= 12 && d.pr.some(function (v) { return v > 0; })) ? d.pr : priceCache.arr;
      var now = new Date();
      var cur = now.getHours();
      var qIdx = cur * 4 + Math.floor(now.getMinutes() / 15);
      var feeKr = feeOre() / 100;
      var pn = num(qp[qIdx]) > 0 ? num(qp[qIdx]) : (num(d.pn) > 0 ? num(d.pn) : num(arr[cur]));
      var mn = 1e9, mx = -1e9, i, have = false;
      for (i = 0; i < qp.length; i++) {
        if (!(qp[i] > 0)) continue;
        have = true;
        if (qp[i] < mn) mn = qp[i];
        if (qp[i] > mx) mx = qp[i];
      }
      if (!have) {
        for (i = 0; i < arr.length; i++) {
          if (!(arr[i] > 0)) continue;
          have = true;
          if (arr[i] < mn) mn = arr[i];
          if (arr[i] > mx) mx = arr[i];
        }
      }
      setSegMeter(byId('priceNow'), pn > 0 ? pn : null, 4, { decimals: 2, unit: 'kr/kWh' });
      byId('priceRange').innerText = have ? ('min ' + mn.toFixed(2) + ' · max ' + mx.toFixed(2) + ' kr') : 'pris saknas';
      curPriceTotal = pn > 0 ? pn + feeKr : 0;
      var pt = byId('priceTotal');
      if (pt) pt.innerText = pn > 0
        ? ((pn + feeKr).toFixed(2) + ' kr/kWh inkl elöverföring · kvart ' +
           ('0' + cur).slice(-2) + ':' + ('0' + (Math.floor(now.getMinutes() / 15) * 15)).slice(-2))
        : '';
      var chartArr = qp.length >= 12 ? qp : arr;
      var priceMark = chartArr.length >= 48
        ? Math.min(11, Math.floor(qIdx / (chartArr.length / 12)))
        : Math.min(11, Math.floor(cur / 2));
      segHistory(byId('priceSeg'), chartArr, {
        cols: 12, kind: 'price', blankZero: true, markCol: priceMark,
        aria: 'Elpris som segmentdisplay, två timmar per siffra'
      });

      var mw = num(d.maxW), mh = (typeof d.maxH === 'number' ? d.maxH : -1);
      // En rad per klockslag. Kvartarnas energi (q) gånger kvartarnas pris
      // ger exakt kostnad; hr/pr används bara om kortet saknar kvartsdata.
      var hr = d.hr || [];
      var rows = '', kwhDay = 0, costDay = 0;
      for (i = 0; i < 24; i++) {
        var kwh = 0, kr = 0;
        if (qe) {
          for (var k = 0; k < 4; k++) {
            var wh = num(qe[i * 4 + k]);
            if (!(wh > 0)) continue;
            var p = num(qp[i * 4 + k]);
            kwh += wh / 1000;
            if (p > 0) kr += (wh / 1000) * (p + feeKr);
          }
        } else if (hr.length) {
          var whh = num(hr[i]);
          var p = num(arr[i]);
          kwh = whh / 1000;
          if (p > 0) kr = kwh * (p + feeKr);
        }
        if (!(kwh > 0) && i !== cur) continue;
        var hp = kwh > 0 && kr > 0 ? kr / kwh : 0;
        kwhDay += kwh;
        costDay += kr;
        rows += '<div class="hrow' + (i === cur ? ' now' : '') + '">' +
                '<span class="ht">' + ('0' + i).slice(-2) + '–' + ('0' + ((i + 1) % 24)).slice(-2) + '</span>' +
                '<span class="hk">' + kwh.toFixed(2) + ' kWh</span>' +
                '<span class="hp">' + (hp > 0 ? hp.toFixed(2) : '–') + '</span>' +
                '<span class="hc">' + (kr > 0 ? kr.toFixed(2) + ' kr' : '–') + '</span></div>';
      }
      var hl = byId('hourList');
      if (hl) {
        hl.innerHTML = rows
          ? rows + '<div class="hrow sum"><span class="ht">idag</span><span class="hk">' + kwhDay.toFixed(2) +
            ' kWh</span><span class="hp"></span><span class="hc">' + costDay.toFixed(2) + ' kr</span></div>'
          : '<div class="small">ingen förbrukning räknad än</div>';
      }
      var ct = byId('costToday');
      if (ct) ct.innerText = kwhDay > 0
        ? ('idag ' + kwhDay.toFixed(2) + ' kWh · ' + costDay.toFixed(2) + ' kr inkl elöverföring')
        : 'idag 0 kWh';

      var me = byId('maxHour'), mwe = byId('maxHourWhen');
      if (me) me.innerText = mw > 0 ? Math.round(mw) + ' W' : '–';
      if (mwe) mwe.innerText = (mw > 0 && mh >= 0) ? ('kl ' + (mh < 10 ? '0' : '') + mh + ':00') : '';

      tickerUpdate();
    }

    // Elöverföring i öre/kWh är nätägarens avgift. Värdet sparas i webbläsaren
    // (localStorage); standardvärdet är bara en rimlig utgångspunkt.
    function feeOre() {
      try {
        var v = parseFloat(localStorage.getItem('husetseffekt.fee'));
        if (isFinite(v) && v >= 0) return v;
      } catch (e) {}
      return 24.5;
    }
    var feeInput = byId('feeInput');
    if (feeInput) {
      feeInput.value = feeOre();
      feeInput.addEventListener('input', function () {
        var v = parseFloat(feeInput.value);
        if (!isFinite(v) || v < 0) v = 0;
        try { localStorage.setItem('husetseffekt.fee', String(v)); } catch (e) {}
        if (latest) render(latest);
      });
    }

    var lastCalcAt = 0;
    function tickClock() {
      var el = byId('lastCalc');
      if (!el || !lastCalcAt) return;
      var t = new Date(lastCalcAt);
      var p = function (n) { return (n < 10 ? '0' : '') + n; };
      var age = Math.max(0, Math.round((Date.now() - lastCalcAt) / 1000));
      el.innerText = 'senast räknat ' + p(t.getHours()) + ':' + p(t.getMinutes()) + ':' + p(t.getSeconds()) +
                     ' · för ' + age + ' s sedan';
    }
    setInterval(tickClock, 1000);

    // Hög last: över 2000 W lyser "Just nu" rött så det syns direkt.
    var HIGH_W = 2000;
    function markHigh(w) {
      var el = byId('nowW');
      if (!el) return;
      var hot = w > HIGH_W;
      el.className = 'seg-value' + (hot ? ' hot' : '');
      var b = el.closest ? el.closest('.block') : null;
      if (b) b.className = hot ? 'block over' : 'block';
    }

    // ---- Mätning: nollställ, mät i upp till en timme, visa kWh och kr ----
    var MEAS_MAX = 3600;                    // sekunder
    var curPriceTotal = 0;                  // kr/kWh inkl överföring, sätts i renderPrice
    var meas = { active: false, done: false, t0: 0, secs: 0, kwh: 0, kr: 0, price: 0, lastWh: 0 };

    function measStart() {
      var d = latest || {};
      meas.active = true;
      meas.done = false;
      meas.t0 = Date.now();
      meas.secs = 0;
      meas.kwh = 0;
      meas.kr = 0;
      meas.price = curPriceTotal;
      meas.lastWh = num(d.boot);
      measTick();
    }

    function measAccum(d) {
      if (!meas.active) return;
      var wh = num(d.boot);
      if (meas.lastWh <= 0) { meas.lastWh = wh; return; }
      if (wh + 1 < meas.lastWh) {           // kortet startade om – avbryt
        meas.secs = (Date.now() - meas.t0) / 1000;
        meas.active = false;
        meas.done = true;
        measTick();
        return;
      }
      meas.price = curPriceTotal > 0 ? curPriceTotal : meas.price;
      var dWh = wh - meas.lastWh;
      meas.lastWh = wh;
      if (dWh > 0) {
        meas.kwh += dWh / 1000;
        if (meas.price > 0) meas.kr += (dWh / 1000) * meas.price;
      }
    }

    function measTick() {
      var el = byId('measCard');
      if (!el) return;
      if (meas.active) {
        meas.secs = (Date.now() - meas.t0) / 1000;
        if (meas.secs >= MEAS_MAX) { meas.secs = MEAS_MAX; meas.active = false; meas.done = true; }
      }
      var mm = Math.floor((meas.secs || 0) / 60), ss = Math.floor((meas.secs || 0) % 60);
      var p = function (n) { return (n < 10 ? '0' : '') + n; };
      byId('measKwh').innerText = meas.kwh.toFixed(3) + ' kWh';
      byId('measKr').innerText = meas.price > 0 ? (meas.kr < 1 ? meas.kr.toFixed(3) : meas.kr.toFixed(2)) + ' kr' : '–';
      byId('measTime').innerText = p(mm) + ':' + p(ss) + ' / 60:00';
      var st = byId('measState');
      if (st) st.innerText = meas.active ? 'mäter… tryck för att börja om'
                                       : (meas.done ? 'klar – tryck för nästa' : 'tryck för att nollställa');
      el.className = 'block meas' + (meas.active ? ' active' : '') + (meas.done ? ' done' : '');
    }

    (function () {
      var c = byId('measCard');
      if (!c) return;
      c.addEventListener('click', measStart);
      c.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); measStart(); }
      });
    })();
    setInterval(measTick, 1000);

    function render(d) {
      d = d || {};
      latest = d;
      lastCalcAt = (typeof d.at === 'number' && d.at > 0) ? d.at * 1000 : Date.now();
      setSegMeter(byId('nowW'), d.w, 4, { unit: 'watt' });
      markHigh(num(d.w));
      measAccum(d);
      byId('meterTotal').innerText = fmtKwh(d.tot);
      byId('sinceBoot').innerText = fmtWh(d.boot);
      byId('hour1').innerText = fmtWh(d.h1);
      byId('today').innerText = fmtWh(d.today);
      renderPrice(d);
      measTick();

      var up = d.up || 0;
      byId('foot').innerText = 'Uppringd tid hos kortet: ' + Math.floor(up / 3600) + ' h ' +
        Math.floor((up % 3600) / 60) + ' min' +
        (d.rssi ? ' · Wi-Fi ' + d.rssi + ' dBm' : '');

      if (Date.now() - lastSeenTm > 30000) {
        setStatus('Kortet svarar inte – väntar på data…', false);
        setEspBadge(false);
      } else {
        setStatus(priceStatusText(), true);
        setEspBadge(true);
      }

      drawGraphs();
    }

    function heartbeatCheck() {
      if (latest && Date.now() - lastSeenTm > 30000) {
        setStatus('Mottar inte data – ' + liveSince(lastSeenTm), false);
        setEspBadge(false);
      }
    }

    // ---- data hamtas direkt fran kortet ----
    var POLL_MS = 2000;
    var lastErr = '';

    function mapPower(d) {
      return {
        w: num(d.power), v: num(d.volt), a: num(d.amp),
        tot: num(d.totalWh), boot: num(d.bootWh), h1: num(d.h1Wh), today: num(d.todayWh),
        ok: d.valid ? 1 : 0, up: num(d.uptime), rssi: num(d.rssi),
        maxW: num(d.maxW), maxH: num(d.maxH), pn: num(d.priceNow), pr: (d.price || []).map(num), at: num(d.at),
        hr: (d.hr || []).map(num), q: (d.q || []).map(num)
      };
    }

    var busy = false;
    function poll() {
      if (busy) return;
      busy = true;
      fetch('/api/power?t=' + Date.now(), { cache: 'no-store' })
        .then(function (r) {
          if (r.status === 401) { location.href = '/login'; throw new Error('inloggning kravs'); }
          if (!r.ok) throw new Error('HTTP ' + r.status);
          return r.json();
        })
        .then(function (raw) {
          var d = mapPower(raw);
          lastSeenTm = Date.now();
          samples.push({ w: d.w, v: d.v, t: lastSeenTm });
          if (samples.length > MAXPUNK) samples.shift();
          render(d);
        })
        .catch(function (e) {
          if (lastErr !== e.message) { lastErr = e.message; setStatus('Kortet svarar inte just nu (' + e.message + ')', false); setEspBadge(false); }
        })
        .then(function () { busy = false; });
    }

    poll();
    setInterval(poll, POLL_MS);
    setInterval(function () { if (latest) render(latest); heartbeatCheck(); }, 20000);

    // ---------- installation: ratt steg for iPhone, Android och Windows ----------
    var installBar = byId('installHint');
    var installGo = byId('installGo');
    var installPrompt = null;

    function deviceKind() {
      var ua = navigator.userAgent || '';
      if (/iPhone|iPad|iPod/i.test(ua) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1)) return 'ios';
      if (/Android/i.test(ua)) return 'android';
      if (/Windows/i.test(ua)) return 'windows';
      if (/Macintosh/i.test(ua)) return 'mac';
      return 'other';
    }

    function setupInstallHint() {
      if (!installBar) return;
      var steps = {
        ios: 'iPhone/iPad: tryck på Dela och välj "Lägg till på hemskärmen".',
        android: 'Android: öppna menyn ⋮ och välj "Installera app" eller "Lägg till på startskärmen".',
        windows: 'Windows: klicka på installationsikonen i adressfältet i Edge eller Chrome.',
        mac: 'Mac: välj Arkiv → "Lägg till i Dock" i Safari eller installationsikonen i Chrome.',
        other: 'Installera sidan som app via webbläsarens meny.'
      };
      var standalone = (window.matchMedia && window.matchMedia('(display-mode: standalone)').matches) || navigator.standalone === true;
      try { if (localStorage.getItem('husetseffekt.install') === '1') return; } catch (e) {}
      if (standalone) return;
      byId('installText').innerText = steps[deviceKind()] || steps.other;
      installBar.hidden = false;
      byId('installClose').onclick = function () {
        installBar.hidden = true;
        try { localStorage.setItem('husetseffekt.install', '1'); } catch (e) {}
      };
      window.addEventListener('beforeinstallprompt', function (e) {
        e.preventDefault();
        installPrompt = e;
        if (installGo) installGo.hidden = false;
      });
      if (installGo) installGo.onclick = function () {
        if (!installPrompt) return;
        installPrompt.prompt();
        installPrompt.userChoice.then(function () { installPrompt = null; installGo.hidden = true; });
      };
      window.addEventListener('appinstalled', function () {
        installBar.hidden = true;
        try { localStorage.setItem('husetseffekt.install', '1'); } catch (e) {}
      });
    }
    setupInstallHint();
  </script>
</body>
</html>
)rawliteral";

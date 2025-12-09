#ifndef UTIL_HTML_FB_H
#define UTIL_HTML_FB_H

#include "esp_http_server.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Serve this string at your Wi‑Fi Setup route.
// It fetches /api/scan and POSTs to /connect, then swaps in your existing
// inline "Connecting..." HTML response so the status polling continues.





static const char *fb_wifi_html =
"<!doctype html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"utf-8\" />\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
"  <title>Wi‑Fi Setup</title>\n"
"  <style>\n"
"    :root {\n"
"      --bg: #0b0f14;\n"
"      --panel: #121822;\n"
"      --text: #e8eef9;\n"
"      --muted: #a7b3c4;\n"
"      --accent: #4ea1ff;\n"
"      --ok: #4ad97b;\n"
"      --warn: #ffcc66;\n"
"      --err: #ff6b6b;\n"
"      --border: #263042;\n"
"      --input-bg: #0f131a;\n"
"    }\n"
"    * { box-sizing: border-box; }\n"
"    body {\n"
"      margin: 0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;\n"
"      color: var(--text); background: radial-gradient(1200px 800px at 20% -10%, #152033 0%, var(--bg) 40%) no-repeat, var(--bg);\n"
"    }\n"
"    header {\n"
"      padding: 20px 16px 12px; border-bottom: 1px solid var(--border);\n"
"      background: linear-gradient(180deg, rgba(78,161,255,0.08), rgba(0,0,0,0));\n"
"    }\n"
"    h1 { margin: 0 0 6px; font-size: 20px; font-weight: 650; letter-spacing: .2px; }\n"
"    p.sub { margin: 0; color: var(--muted); font-size: 13px; }\n"
"\n"
"    .controls {\n"
"      display: flex; align-items: center; gap: 8px; padding: 12px 16px;\n"
"    }\n"
"    button, .btn {\n"
"      appearance: none; border: 1px solid var(--border); background: #0c121b;\n"
"      color: var(--text); padding: 8px 12px; border-radius: 8px; font: inherit; cursor: pointer;\n"
"    }\n"
"    button.primary { background: #123056; border-color: #1d477e; color: #e9f2ff; }\n"
"    button:disabled { opacity: .6; cursor: default; }\n"
"    .spinner {\n"
"      width: 16px; height: 16px; border: 2px solid #2a3a55; border-top-color: var(--accent);\n"
"      border-radius: 50%; animation: spin 1s linear infinite; display: inline-block; vertical-align: middle;\n"
"    }\n"
"    @keyframes spin { to { transform: rotate(360deg); } }\n"
"\n"
"    .panel {\n"
"      margin: 12px 16px 24px; background: var(--panel); border: 1px solid var(--border);\n"
"      border-radius: 12px; overflow: hidden;\n"
"    }\n"
"    .list {\n"
"      max-height: 60vh; overflow-y: auto;\n"
"    }\n"
"    .row {\n"
"      display: grid; grid-template-columns: 1fr auto auto; gap: 12px; align-items: center;\n"
"      padding: 12px 14px; border-bottom: 1px solid var(--border);\n"
"    }\n"
"    .row:last-child { border-bottom: 0; }\n"
"    .row.expanded { flex-direction: column; align-items: stretch; gap: 10px;\n" 
"    }\n"
"    .ssid { font-weight: 600; word-break: break-word; }\n"
"    .meta { color: var(--muted); font-size: 12px; }\n"
"    .badges { display: inline-flex; gap: 8px; align-items: center; }\n"
"    .lock { font-size: 12px; color: var(--muted); border: 1px solid var(--border); padding: 2px 6px; border-radius: 6px; }\n"
"    .signal { display: inline-flex; gap: 2px; align-items: flex-end; margin-left: 6px; }\n"
"    .signal i { width: 3px; background: #335071; opacity: .35; display: block; }\n"
"    .signal i:nth-child(1){ height: 4px; }\n"
"    .signal i:nth-child(2){ height: 7px; }\n"
"    .signal i:nth-child(3){ height: 10px; }\n"
"    .signal i:nth-child(4){ height: 13px; }\n"
"    .signal i.on { background: var(--ok); opacity: 1; }\n"
"\n"
"    .connect {\n"
"      display: flex; gap: 8px; align-items: center;\n"
"    }\n"
"    .pw-wrap { display: none; width: min(360px, 60vw); }\n"
"    .pw-wrap.show { display: flex; gap: 8px; align-items: center; }\n"
"    input[type=\"password\"], input[type=\"text\"] {\n"
"      background: var(--input-bg); border: 1px solid var(--border); color: var(--text);\n"
"      padding: 8px 10px; border-radius: 8px; width: 100%;\n"
"    }\n"
"    .hint { color: var(--muted); font-size: 12px; margin-top: 8px; padding: 0 14px 12px; }\n"
"    .empty, .error { padding: 16px; color: var(--muted); }\n"
"    .error { color: var(--err); }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <header>\n"
"    <h1>Wi‑Fi Setup</h1>\n"
"    <p class=\"sub\">Choose a network to connect your device.</p>\n"
"  </header>\n"
"\n"
"  <div class=\"controls\">\n"
"    <button id=\"refreshBtn\" class=\"primary\">Scan Networks</button>\n"
"    <span id=\"scanning\" style=\"display:none;\"><span class=\"spinner\"></span> Scanning…</span>\n"
"  </div>\n"
"\n"
"  <section class=\"panel\">\n"
"    <div id=\"list\" class=\"list\" role=\"list\" aria-label=\"Available Wi‑Fi networks\"></div>\n"
"    <div id=\"listEmpty\" class=\"empty\" style=\"display:none;\">No networks found.</div>\n"
"    <div id=\"listError\" class=\"error\" style=\"display:none;\">Failed to scan. Please try again.</div>\n"
"  </section>\n"
"\n"
"  <script>\n"
"    // --- Utilities -----------------------------------------------------------\n"
"    function rssiToBars(rssi) {\n"
"      if (rssi >= -55) return 4;\n"
"      if (rssi >= -67) return 3;\n"
"      if (rssi >= -75) return 2;\n"
"      if (rssi >= -82) return 1;\n"
"      return 0;\n"
"    }\n"
"\n"
"    // Deduplicate by SSID, keep strongest RSSI\n"
"    function dedupeBySSID(items) {\n"
"      const map = new Map();\n"
"      for (const ap of items) {\n"
"        const k = ap.ssid || \"(hidden)\";\n"
"        if (!map.has(k) || ap.rssi > map.get(k).rssi) {\n"
"          map.set(k, ap);\n"
"        }\n"
"      }\n"
"      return Array.from(map.values());\n"
"    }\n"
"\n"
"    function lockLabel(ap) {\n"
"      if (ap.open) return \"Open\";\n"
"      // If you also provide ap.auth as string, show it\n"
"      return (ap.auth || \"Secured\").toUpperCase();\n"
"    }\n"
"\n"
"    function signalBarsHTML(ap) {\n"
"      const bars = rssiToBars(ap.rssi);\n"
"      return `\n"
"        <span class=\"signal\" aria-label=\"Signal ${bars}/4\">\n"
"          <i class=\"${bars>=1?'on':''}\"></i>\n"
"          <i class=\"${bars>=2?'on':''}\"></i>\n"
"          <i class=\"${bars>=3?'on':''}\"></i>\n"
"          <i class=\"${bars>=4?'on':''}\"></i>\n"
"        </span>`;\n"
"    }\n"
"\n"
"    // --- Rendering -----------------------------------------------------------\n"
"    let expandedRow = null; // row index with password field visible\n"
"\n"
"    function renderList(items) {\n"
"      const list = document.getElementById('list');\n"
"      const empty = document.getElementById('listEmpty');\n"
"      const err = document.getElementById('listError');\n"
"      list.innerHTML = '';\n"
"      empty.style.display = items.length ? 'none' : 'block';\n"
"      err.style.display = 'none';\n"
"\n"
"      items.forEach((ap, idx) => {\n"
"        const row = document.createElement('div');\n"
"        row.className = 'row';\n"
"        if (expandedRow === idx) row.classList.add('expanded');\n"
"        row.setAttribute('role', 'listitem');\n"
"\n"
"        const left = document.createElement('div');\n"
"        left.innerHTML = `\n"
"          <div class=\"ssid\">${ap.ssid || '<em>(hidden)</em>'}</div>\n"
"          <div class=\"meta\">\n"
"            <span class=\"badges\">\n"
"              <span class=\"lock\">${lockLabel(ap)}</span>\n"
"              ${signalBarsHTML(ap)}\n"
"            </span>\n"
"            <span style=\"margin-left:10px;\">CH ${ap.chan}</span>\n"
"            <span style=\"margin-left:10px;\">${ap.bssid}</span>\n"
"          </div>\n"
"        `;\n"
"\n"
"        const connectWrap = document.createElement('div');\n"
"        connectWrap.className = 'connect';\n"
"\n"
"        const btn = document.createElement('button');\n"
"        btn.textContent = ap.open ? 'Connect' : (expandedRow === idx ? 'Join' : 'Connect');\n"
"        btn.className = 'primary';\n"
"        btn.addEventListener('click', () => onConnectClick(idx, ap));\n"
"\n"
"        const pwWrap = document.createElement('div');\n"
"        pwWrap.className = 'pw-wrap' + ((expandedRow === idx && !ap.open) ? ' show' : '');\n"
"        pwWrap.innerHTML = `\n"
"          <input type=\"password\" id=\"pw_${idx}\" autocomplete=\"current-password\" placeholder=\"Wi‑Fi Password\" />\n"
"          <label style=\"display:flex;align-items:center;gap:6px;color:var(--muted);font-size:12px;\">\n"
"            <input type=\"checkbox\" id=\"show_${idx}\" />\n"
"            Show\n"
"          </label>\n"
"        `;\n"
"        row.appendChild(left);\n"
"        connectWrap.appendChild(pwWrap);\n"
"        connectWrap.appendChild(btn);\n"
"\n"
"        // Toggle show password\n"
"        pwWrap.querySelector('#show_'+idx)?.addEventListener('change', (e) => {\n"
"          const pw = pwWrap.querySelector('#pw_'+idx);\n"
"          pw.type = e.target.checked ? 'text' : 'password';\n"
"        });\n"
"\n"
"        // Enter to submit\n"
"        pwWrap.querySelector('#pw_'+idx)?.addEventListener('keydown', (e) => {\n"
"          if (e.key === 'Enter') onConnectClick(idx, ap);\n"
"        });\n"
"\n"
"        row.appendChild(left);\n"
"        row.appendChild(connectWrap);\n"
"        list.appendChild(row);\n"
"      });\n"
"    }\n"
"\n"
"    // --- Behavior ------------------------------------------------------------\n"
"    async function submitConnect(ssid, pass) {\n"
"      // POST JSON to /api/connect; server responds with full HTML \"Connecting...\" page\n"
"      const res = await fetch('/api/connect', {\n"
"        method: 'POST',\n"
"        headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' },\n"
"        cache: 'no-store',\n"
"        body: JSON.stringify({ ssid, pass: pass || '' })\n"
"      });\n"
"      const html = await res.text();\n"
"      // Replace document with server's connecting page so your existing flow continues\n"
"      document.open(); document.write(html); document.close();\n"
"    }\n"
"\n"
"    function onConnectClick(idx, ap) {\n"
"      if (ap.open) {\n"
"        submitConnect(ap.ssid, '');\n"
"        return;\n"
"      }\n"
"\n"
"      // First click → expand password field\n"
"      if (expandedRow !== idx) {\n"
"        expandedRow = idx;\n"
"        // Re-render to show the password field for this row\n"
"        renderList(currentItems);\n"
"        // Focus the password input\n"
"        setTimeout(() => document.getElementById('pw_'+idx)?.focus(), 0);\n"
"        return;\n"
"      }\n"
"\n"
"      // Second click → submit (validate minimal length)\n"
"      const pwEl = document.getElementById('pw_'+idx);\n"
"      const pass = (pwEl?.value ?? '').trim();\n"
"      if (pass.length < 8) {\n"
"        alert('Please enter a valid password (minimum 8 characters for WPA2).');\n"
"        pwEl?.focus();\n"
"        return;\n"
"      }\n"
"      submitConnect(ap.ssid, pass);\n"
"    }\n"
"\n"
"    // --- Scanning ------------------------------------------------------------\n"
"    let currentItems = [];\n"
"    let inFlight = null;\n"
"\n"
"    async function scan() {\n"
"      if (inFlight) {\n"
"        try { inFlight.abort(); } catch {}\n"
"      }\n"
"      const controller = new AbortController();\n"
"      inFlight = controller;\n"
"\n"
"      const refreshBtn = document.getElementById('refreshBtn');\n"
"      const scanning = document.getElementById('scanning');\n"
"      const err = document.getElementById('listError');\n"
"      const empty = document.getElementById('listEmpty');\n"
"      err.style.display = 'none';\n"
"      empty.style.display = 'none';\n"
"      scanning.style.display = 'inline-block';\n"
"      refreshBtn.disabled = true;\n"
"\n"
"      try {\n"
"        const res = await fetch('/api/scan', { cache: 'no-store', signal: controller.signal });\n"
"        if (!res.ok) throw new Error('HTTP '+res.status);\n"
"        let items = await res.json();\n"
"\n"
"        // Defensive: make sure it's an array of objects\n"
"        if (!Array.isArray(items)) items = [];\n"
"        // Deduplicate SSIDs and sort by RSSI descending\n"
"        items = dedupeBySSID(items).sort((a,b) => (b.rssi||-999) - (a.rssi||-999));\n"
"\n"
"        currentItems = items;\n"
"        expandedRow = null; // collapse any open row on refresh\n"
"        renderList(items);\n"
"        if (items.length === 0) empty.style.display = 'block';\n"
"      } catch (e) {\n"
"        console.warn('Scan failed:', e);\n"
"        err.textContent = 'Failed to scan. Please try again.';\n"
"        err.style.display = 'block';\n"
"      } finally {\n"
"        scanning.style.display = 'none';\n"
"        refreshBtn.disabled = false;\n"
"        inFlight = null;\n"
"      }\n"
"    }\n"
"\n"
"    document.getElementById('refreshBtn').addEventListener('click', scan);\n"
"\n"
"    // Initial scan on load\n"
"    scan();\n"
"\n"
"    // Optional: auto-refresh every 10s while on this page\n"
"    // setInterval(scan, 10000);\n"
"  </script>\n"
"</body>\n"; 


/*
static const char *fb_wifi_html =
"<!doctype html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"utf-8\" />\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
"  <title>Wi‑Fi Setup</title>\n"
"  <style>\n"
"    :root {\n"
"      --bg: #0b0f14;\n"
"      --panel: #121822;\n"
"      --text: #e8eef9;\n"
"      --muted: #a7b3c4;\n"
"      --accent: #4ea1ff;\n"
"      --ok: #4ad97b;\n"
"      --warn: #ffcc66;\n"
"      --err: #ff6b6b;\n"
"      --border: #263042;\n"
"      --input-bg: #0f131a;\n"
"    }\n"
"    * { box-sizing: border-box; }\n"
"    body {\n"
"      margin: 0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;\n"
"      color: var(--text); background: radial-gradient(1200px 800px at 20% -10%, #152033 0%, var(--bg) 40%) no-repeat, var(--bg);\n"
"    }\n"
"    header {\n"
"      padding: 20px 16px 12px; border-bottom: 1px solid var(--border);\n"
"      background: linear-gradient(180deg, rgba(78,161,255,0.08), rgba(0,0,0,0));\n"
"    }\n"
"    h1 { margin: 0 0 6px; font-size: 20px; font-weight: 650; letter-spacing: .2px; }\n"
"    p.sub { margin: 0; color: var(--muted); font-size: 13px; }\n"
"\n"
"    .controls {\n"
"      display: flex; align-items: center; gap: 8px; padding: 12px 16px;\n"
"    }\n"
"    button, .btn {\n"
"      appearance: none; border: 1px solid var(--border); background: #0c121b;\n"
"      color: var(--text); padding: 8px 12px; border-radius: 8px; font: inherit; cursor: pointer;\n"
"    }\n"
"    button.primary { background: #123056; border-color: #1d477e; color: #e9f2ff; }\n"
"    button:disabled { opacity: .6; cursor: default; }\n"
"    .spinner {\n"
"      width: 16px; height: 16px; border: 2px solid #2a3a55; border-top-color: var(--accent);\n"
"      border-radius: 50%; animation: spin 1s linear infinite; display: inline-block; vertical-align: middle;\n"
"    }\n"
"    @keyframes spin { to { transform: rotate(360deg); } }\n"
"\n"
"    .panel {\n"
"      margin: 12px 16px 24px; background: var(--panel); border: 1px solid var(--border);\n"
"      border-radius: 12px; overflow: hidden;\n"
"    }\n"
"    .list {\n"
"      max-height: 60vh; overflow-y: auto;\n"
"    }\n"
"    .row {\n"
"      display: grid; grid-template-columns: 1fr auto auto; gap: 12px; align-items: center;\n"
"      padding: 12px 14px; border-bottom: 1px solid var(--border);\n"
"    }\n"
"    .row:last-child { border-bottom: 0; }\n"
"    .ssid { font-weight: 600; word-break: break-word; }\n"
"    .meta { color: var(--muted); font-size: 12px; }\n"
"    .badges { display: inline-flex; gap: 8px; align-items: center; }\n"
"    .lock { font-size: 12px; color: var(--muted); border: 1px solid var(--border); padding: 2px 6px; border-radius: 6px; }\n"
"    .signal { display: inline-flex; gap: 2px; align-items: flex-end; margin-left: 6px; }\n"
"    .signal i { width: 3px; background: #335071; opacity: .35; display: block; }\n"
"    .signal i:nth-child(1){ height: 4px; }\n"
"    .signal i:nth-child(2){ height: 7px; }\n"
"    .signal i:nth-child(3){ height: 10px; }\n"
"    .signal i:nth-child(4){ height: 13px; }\n"
"    .signal i.on { background: var(--ok); opacity: 1; }\n"
"\n"
"    .connect {\n"
"      display: flex; gap: 8px; align-items: center;\n"
"    }\n"
"    .pw-wrap { display: none; width: min(360px, 60vw); }\n"
"    .pw-wrap.show { display: flex; gap: 8px; align-items: center; }\n"
"    input[type=\"password\"], input[type=\"text\"] {\n"
"      background: var(--input-bg); border: 1px solid var(--border); color: var(--text);\n"
"      padding: 8px 10px; border-radius: 8px; width: 100%;\n"
"    }\n"
"    .hint { color: var(--muted); font-size: 12px; margin-top: 8px; padding: 0 14px 12px; }\n"
"    .empty, .error { padding: 16px; color: var(--muted); }\n"
"    .error { color: var(--err); }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <header>\n"
"    <h1>Wi‑Fi Setup</h1>\n"
"    <p class=\"sub\">Choose a network to connect your device.</p>\n"
"  </header>\n"
"\n"
"  <div class=\"controls\">\n"
"    <button id=\"refreshBtn\" class=\"primary\">Scan Networks</button>\n"
"    <span id=\"scanning\" style=\"display:none;\"><span class=\"spinner\"></span> Scanning…</span>\n"
"  </div>\n"
"\n"
"  <section class=\"panel\">\n"
"    <div id=\"list\" class=\"list\" role=\"list\" aria-label=\"Available Wi‑Fi networks\"></div>\n"
"    <div id=\"listEmpty\" class=\"empty\" style=\"display:none;\">No networks found.</div>\n"
"    <div id=\"listError\" class=\"error\" style=\"display:none;\">Failed to scan. Please try again.</div>\n"
"  </section>\n"
"\n"
"  <script>\n"
"    // --- Utilities -----------------------------------------------------------\n"
"    function rssiToBars(rssi) {\n"
"      if (rssi >= -55) return 4;\n"
"      if (rssi >= -67) return 3;\n"
"      if (rssi >= -75) return 2;\n"
"      if (rssi >= -82) return 1;\n"
"      return 0;\n"
"    }\n"
"\n"
"    // Deduplicate by SSID, keep strongest RSSI\n"
"    function dedupeBySSID(items) {\n"
"      const map = new Map();\n"
"      for (const ap of items) {\n"
"        const k = ap.ssid || \"(hidden)\";\n"
"        if (!map.has(k) || ap.rssi > map.get(k).rssi) {\n"
"          map.set(k, ap);\n"
"        }\n"
"      }\n"
"      return Array.from(map.values());\n"
"    }\n"
"\n"
"    function lockLabel(ap) {\n"
"      if (ap.open) return \"Open\";\n"
"      // If you also provide ap.auth as string, show it\n"
"      return (ap.auth || \"Secured\").toUpperCase();\n"
"    }\n"
"\n"
"    function signalBarsHTML(ap) {\n"
"      const bars = rssiToBars(ap.rssi);\n"
"      return `\n"
"        <span class=\"signal\" aria-label=\"Signal ${bars}/4\">\n"
"          <i class=\"${bars>=1?'on':''}\"></i>\n"
"          <i class=\"${bars>=2?'on':''}\"></i>\n"
"          <i class=\"${bars>=3?'on':''}\"></i>\n"
"          <i class=\"${bars>=4?'on':''}\"></i>\n"
"        </span>`;\n"
"    }\n"
"\n"
"    // --- Rendering -----------------------------------------------------------\n"
"    let expandedRow = null; // row index with password field visible\n"
"\n"
"    function renderList(items) {\n"
"      const list = document.getElementById('list');\n"
"      const empty = document.getElementById('listEmpty');\n"
"      const err = document.getElementById('listError');\n"
"      list.innerHTML = '';\n"
"      empty.style.display = items.length ? 'none' : 'block';\n"
"      err.style.display = 'none';\n"
"\n"
"      items.forEach((ap, idx) => {\n"
"        const row = document.createElement('div');\n"
"        row.className = 'row';\n"
"        row.setAttribute('role', 'listitem');\n"
"\n"
"        const left = document.createElement('div');\n"
"        left.innerHTML = `\n"
"          <div class=\"ssid\">${ap.ssid || '<em>(hidden)</em>'}</div>\n"
"          <div class=\"meta\">\n"
"            <span class=\"badges\">\n"
"              <span class=\"lock\">${lockLabel(ap)}</span>\n"
"              ${signalBarsHTML(ap)}\n"
"            </span>\n"
"            <span style=\"margin-left:10px;\">CH ${ap.chan}</span>\n"
"            <span style=\"margin-left:10px;\">${ap.bssid}</span>\n"
"          </div>\n"
"        `;\n"
"\n"
"        const connectWrap = document.createElement('div');\n"
"        connectWrap.className = 'connect';\n"
"\n"
"        const btn = document.createElement('button');\n"
"        btn.textContent = ap.open ? 'Connect' : (expandedRow === idx ? 'Join' : 'Connect');\n"
"        btn.className = 'primary';\n"
"        btn.addEventListener('click', () => onConnectClick(idx, ap));\n"
"\n"
"        const pwWrap = document.createElement('div');\n"
"        pwWrap.className = 'pw-wrap' + ((expandedRow === idx && !ap.open) ? ' show' : '');\n"
"        pwWrap.innerHTML = `\n"
"          <input type=\"password\" id=\"pw_${idx}\" autocomplete=\"current-password\" placeholder=\"Wi‑Fi Password\" />\n"
"          <label style=\"display:flex;align-items:center;gap:6px;color:var(--muted);font-size:12px;\">\n"
"            <input type=\"checkbox\" id=\"show_${idx}\" />\n"
"            Show\n"
"          </label>\n"
"        `;\n"
"        connectWrap.appendChild(pwWrap);\n"
"        connectWrap.appendChild(btn);\n"
"\n"
"        // Toggle show password\n"
"        pwWrap.querySelector('#show_'+idx)?.addEventListener('change', (e) => {\n"
"          const pw = pwWrap.querySelector('#pw_'+idx);\n"
"          pw.type = e.target.checked ? 'text' : 'password';\n"
"        });\n"
"\n"
"        // Enter to submit\n"
"        pwWrap.querySelector('#pw_'+idx)?.addEventListener('keydown', (e) => {\n"
"          if (e.key === 'Enter') onConnectClick(idx, ap);\n"
"        });\n"
"\n"
"        row.appendChild(left);\n"
"        row.appendChild(connectWrap);\n"
"        list.appendChild(row);\n"
"      });\n"
"    }\n"
"\n"
"    // --- Behavior ------------------------------------------------------------\n"
"    async function submitConnect(ssid, pass) {\n"
"      // POST JSON to /api/connect; server responds with full HTML \"Connecting...\" page\n"
"      const res = await fetch('/api/connect', {\n"
"        method: 'POST',\n"
"        headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' },\n"
"        cache: 'no-store',\n"
"        body: JSON.stringify({ ssid, pass: pass || '' })\n"
"      });\n"
"      const html = await res.text();\n"
"      // Replace document with server's connecting page so your existing flow continues\n"
"      document.open(); document.write(html); document.close();\n"
"    }\n"
"\n"
"    function onConnectClick(idx, ap) {\n"
"      if (ap.open) {\n"
"        submitConnect(ap.ssid, '');\n"
"        return;\n"
"      }\n"
"\n"
"      // First click → expand password field\n"
"      if (expandedRow !== idx) {\n"
"        expandedRow = idx;\n"
"        // Re-render to show the password field for this row\n"
"        renderList(currentItems);\n"
"        // Focus the password input\n"
"        setTimeout(() => document.getElementById('pw_'+idx)?.focus(), 0);\n"
"        return;\n"
"      }\n"
"\n"
"      // Second click → submit (validate minimal length)\n"
"      const pwEl = document.getElementById('pw_'+idx);\n"
"      const pass = (pwEl?.value ?? '').trim();\n"
"      if (pass.length < 8) {\n"
"        alert('Please enter a valid password (minimum 8 characters for WPA2).');\n"
"        pwEl?.focus();\n"
"        return;\n"
"      }\n"
"      submitConnect(ap.ssid, pass);\n"
"    }\n"
"\n"
"    // --- Scanning ------------------------------------------------------------\n"
"    let currentItems = [];\n"
"    let inFlight = null;\n"
"\n"
"    async function scan() {\n"
"      if (inFlight) {\n"
"        try { inFlight.abort(); } catch {}\n"
"      }\n"
"      const controller = new AbortController();\n"
"      inFlight = controller;\n"
"\n"
"      const refreshBtn = document.getElementById('refreshBtn');\n"
"      const scanning = document.getElementById('scanning');\n"
"      const err = document.getElementById('listError');\n"
"      const empty = document.getElementById('listEmpty');\n"
"      err.style.display = 'none';\n"
"      empty.style.display = 'none';\n"
"      scanning.style.display = 'inline-block';\n"
"      refreshBtn.disabled = true;\n"
"\n"
"      try {\n"
"        const res = await fetch('/api/scan', { cache: 'no-store', signal: controller.signal });\n"
"        if (!res.ok) throw new Error('HTTP '+res.status);\n"
"        let items = await res.json();\n"
"\n"
"        // Defensive: make sure it's an array of objects\n"
"        if (!Array.isArray(items)) items = [];\n"
"        // Deduplicate SSIDs and sort by RSSI descending\n"
"        items = dedupeBySSID(items).sort((a,b) => (b.rssi||-999) - (a.rssi||-999));\n"
"\n"
"        currentItems = items;\n"
"        expandedRow = null; // collapse any open row on refresh\n"
"        renderList(items);\n"
"        if (items.length === 0) empty.style.display = 'block';\n"
"      } catch (e) {\n"
"        console.warn('Scan failed:', e);\n"
"        err.textContent = 'Failed to scan. Please try again.';\n"
"        err.style.display = 'block';\n"
"      } finally {\n"
"        scanning.style.display = 'none';\n"
"        refreshBtn.disabled = false;\n"
"        inFlight = null;\n"
"      }\n"
"    }\n"
"\n"
"    document.getElementById('refreshBtn').addEventListener('click', scan);\n"
"\n"
"    // Initial scan on load\n"
"    scan();\n"
"\n"
"    // Optional: auto-refresh every 10s while on this page\n"
"    // setInterval(scan, 10000);\n"
"  </script>\n"
"</body>\n"; 
*/



static const char *fb_connecting_html =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>Connecting…</title>"
"<style>"
"  :root{--bg:#0f1216;--panel:#151a20;--accent:#7bd389;--text:#e8eef5;--muted:#99a3ad;--danger:#ff6b6b}"
"  *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Cantarell,Noto Sans,Arial,sans-serif}"
"  .wrap{max-width:680px;margin:0 auto;padding:24px}"
"  h1{margin:0 0 12px;font-size:1.4rem} .sub{color:var(--muted);font-size:.95rem;margin-bottom:16px}"
"  .card{background:var(--panel);border:1px solid #1f252d;border-radius:12px;padding:16px;margin:10px 0}"
"  .row{display:flex;align-items:center;gap:10px}"
"  .label{font-weight:600} .value{color:var(--muted)}"
"  .state{font-weight:700} .ok{color:var(--accent)} .fail{color:var(--danger)} .muted{color:var(--muted)}"
"  .actions{margin-top:12px;display:flex;gap:10px}"
"  button.primary{appearance:none;border:0;border-radius:10px;padding:10px 14px;background:var(--accent);color:#0b0f12;font-weight:700;cursor:pointer}"
"  button.primary:disabled{opacity:.6;cursor:not-allowed}"
"</style>"
"</head><body><div class=\"wrap\">"
"  <h1>Connecting to Wi‑Fi…</h1>"
"  <div class=\"sub\">Hang tight while we connect your device to the selected network.</div>"
"  <div class=\"card\">"
"    <div class=\"row\"><div class=\"label\">Status:</div><div id=\"st\" class=\"state muted\">connecting</div></div>"
"    <div class=\"row\"><div class=\"label\">SSID:</div><div id=\"ssid\" class=\"value\"></div></div>"
"    <div class=\"row\"><div class=\"label\">IP:</div><div id=\"ip\" class=\"value\"></div></div>"
"    <div class=\"actions\">"
"      <button id=\"open\" class=\"primary\" disabled>Open App</button>"
"    </div>"
"  </div>"
"</div>"
"<script>"
"const st   = document.getElementById('st');"
"const ssid = document.getElementById('ssid');"
"const ip   = document.getElementById('ip');"
"const btn  = document.getElementById('open');"
"let timer = null;"
"let openApp = () => { console.log(\"opening App\"); };"
"function updateUI(j){"
"  ssid.textContent = j.ssid || '';"
"  ip.textContent   = j.ip   || '';"
"  const s = (j.state||'').toLowerCase();"
"  if (s === 'connected'){"
"    st.textContent = 'connected'; st.className='state ok';"
"    btn.addEventListener('click', ()=>{ console.log('going to: http://'+(j.ip||'')+'/'); location.assign('http://'+(j.ip||'')+'/'); });"
"    btn.disabled = false;"
"  } else if (s === 'failed'){"
"    st.textContent = 'failed'; st.className='state fail';"
"    btn.disabled = true;"
"  } else {"
"    st.textContent = 'connecting'; st.className='state muted';"
"    btn.disabled = true;"
"  }"
"}"
"async function poll(){"
"  try{"
"    const r = await fetch('/api/status');"
"    if (!r.ok) throw new Error('status error');"
"    const j = await r.json();"
"    updateUI(j);"
"  }catch(e){ /* keep showing last state */ }"
"}"
"timer = setInterval(poll, 1500);"
"poll();"
"</script></body></html>";



/* // const char *fb_connecting_html = " \
//     <!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'> \
//     <title>Connecting…</title> \
//     <style> \
//     :root { \
//     --drk: rgb(0, 0, 23); \
//     --lit: rgb(230, 230, 230); \
//     --lit06: rgb(230, 230, 230, 0.6); \
//     --lit01: rgb(230, 230, 230, 0.1); \
//     --lit005: rgb(230, 230, 230, 0.05); \
//     --sea07: rgb(18, 218, 110, 0.7); \
//     --sea05: rgb(18, 218, 110, 0.5); \
//     --sea03: rgb(18, 218, 110, 0.3); \
//     --sea01: rgb(18, 218, 110, 0.1); \
//     } \
//     * { background-color: transparent; box-sizing: border-box; padding: 0em; margin: 0em; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; font-weight: 300; font-size: 20px; color: var(--lit06); } \
//     h1 { font-size: 2.5em; font-weight: 300; margin: 0.75em;} \
//     form { display: flex; flex-direction: column; align-items: center; height: auto; width:100%; padding: 0;} \
//     body { background-color: var(--drk); display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; padding: 1.5em; gap:1em; } \
//     .content { display: flex; flex-direction: column; align-items: center; max-width: 30em; width: 100%; height: 100%; gap: 0.5em; } \
//     .lbl { display: flex; flex-direction: row; justify-content: flex-start; color: var(--sea07); margin: -0.3em; width: 100%; } \
//     input { color: var(--lit06); background-color: var(--sea01); padding: 0.25em 0.5rem; border-radius: 0.25rem; border-top: solid 0.05em transparent; border-left: solid 0.05em transparent; border-right: solid 0.05em var(--lit01); border-bottom: solid 0.05em var(--lit01); width: 100%; } \
//     input:disabled { color: var(--sea05); background-color: var(--lit005); padding: 0.25rem 0.5rem; border: 0.1rem solid transparent; } \
//     input:focus { outline: 0.05rem solid transparent; outline-offset: -0.15rem; } \
//     input::-webkit-outer-spin-button, \
//     input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; } \
//     .btn { background: var(--sea01); color: var(--sea07); cursor: pointer; text-align: center; font-size: 1.25em; border: solid 0.05em var(--sea03); border-radius: 1.0em; padding: 0.35em 0.75em; width: 100%; } \
//     .btn:hover { background-color: var(--sea02); border: solid 0.05em var(--sea05); color: var(--lit);} \
//     .ip{color:var(--sea07); font-size:2.0em: } \
//     .err{color:var(--red07)}; \
//     .muted{color:var(--lit01);} \
//     @media screen and (orientation: landscape)  and (height <= 500px) { \
//     h1 { margin: 0; } \
//     } \
//     @media screen and (orientation: portrait) and (width <= 1000px) { \
//     * { font-size: 33px; } \
//     h1 { font-size: 2.3em; margin-bottom: 0; } \
//     body { justify-content: flex-start; } \
//     .content { max-width: none; padding: 0 2rem; } \
//     } \
//     @media screen and (orientation: portrait) and (width <= 550px) { \
//     * { font-size: 23px; } \
//     h1 { font-size: 2.3em; margin-bottom: 0; } \
//     body { justify-content: flex-start; } \
//     .content { max-width: none; padding: 0 2rem; } \
//     } \
//     </style> \
//     <h1 id=title>Connecting to Wi‑Fi…</h1> \
//     <p id=s class=muted>Waiting for IP…</p> \
//     <div id=done class=content style='display:none'> \
//     <p id=ip class=ip></p> \
//     <p>Switch your phone/computer to your normal Wi‑Fi network, then open:</p> \
//     <a id=link href=# target=_self>(loading…)</a> \
//     </div> \
//     <script> \
//     function delay(ms) { return new Promise(resolve => setTimeout(resolve, ms)); } \
//     async function poll(){ \
//       try{ \
//         const r=await fetch('/api/status',{cache:'no-store'}); \
//         const j=await r.json(); \
//         console.log(j); \
//         const tit=document.getElementById('title'); \
//         const s=document.getElementById('s'); \
//         if(j.state==='connected'){ \
//           tit.textContent='Connected!'; \
//           document.getElementById('done').style.display='flex'; \
//           s.textContent='Connected to '+(j.ssid||''); \
//           document.getElementById('ip').textContent=j.ip||''; \
//           const url='http://'+(j.ip||'')+'/'; \
//           const a=document.getElementById('link'); a.href=url; a.textContent=url; \
//           clearInterval(t); \
//         }else if(j.state==='failed'){ \
//           tit.textContent='Sign-in failed'; \
//           tit.style.color='var(--red007)'; \
//           s.className='err'; s.textContent='This is your fault; my code is TOIGHT!'; \
//           alert('Authentication failed. Please check your password.'); \
//           clearInterval(t); \
//           await delay(3000); \
//           window.location.href = '/'; \
//         }else{ \
//           s.textContent='Connecting to '+(j.ssid||'')+'…'; \
//         } \
//       }catch(e){} \
//     } \
//     const t=setInterval(poll,1000); \
//     </script>"; */

/* // const char *fb_home_html = " \
//     <!DOCTYPE html> \
//     <html><head name=viewport> \
//     <title>Wi-Fi Setup</title> \
//     <style> \
//     :root { \
//     --drk: rgb(0, 0, 23); \
//     --lit: rgb(230, 230, 230); \
//     --lit06: rgb(230, 230, 230, 0.6); \
//     --lit01: rgb(230, 230, 230, 0.1); \
//     --lit005: rgb(230, 230, 230, 0.05); \
//     --sea07: rgb(18, 218, 110, 0.7); \
//     --sea05: rgb(18, 218, 110, 0.5); \
//     --sea03: rgb(18, 218, 110, 0.3); \
//     --sea01: rgb(18, 218, 110, 0.1); \
//     } \
//     * { background-color: transparent; box-sizing: border-box; padding: 0em; margin: 0em; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; font-weight: 300; font-size: 20px; color: var(--lit06); } \
//     h1 { font-size: 2.5em; font-weight: 300; margin: 0.75em;} \
//     form { display: flex; flex-direction: column; align-items: center; height: auto; width:100%; padding: 0;} \
//     body { background-color: var(--drk); display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; padding: 1.5em; gap:1em; } \
//     .content { display: flex; flex-direction: column; justify-content: space-around; max-width: 30em; width: 100%; height: 100%; gap: 0.5em; } \
//     .lbl { display: flex; flex-direction: row; justify-content: flex-start; color: var(--sea07); margin: -0.3em; width: 100%; } \
//     input { color: var(--lit06); background-color: var(--sea01); padding: 0.25em 0.5rem; border-radius: 0.25rem; border-top: solid 0.05em transparent; border-left: solid 0.05em transparent; border-right: solid 0.05em var(--lit01); border-bottom: solid 0.05em var(--lit01); width: 100%; } \
//     input:disabled { color: var(--sea05); background-color: var(--lit005); padding: 0.25rem 0.5rem; border: 0.1rem solid transparent; } \
//     input:focus { outline: 0.05rem solid transparent; outline-offset: -0.15rem; } \
//     input::-webkit-outer-spin-button, \
//     input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; } \
//     .btn { background: var(--sea01); color: var(--sea07); cursor: pointer; text-align: center; font-size: 1.25em; border: solid 0.05em var(--sea03); border-radius: 1.0em; padding: 0.35em 0.75em; width: 100%; } \
//     .btn:hover { background-color: var(--sea02); border: solid 0.05em var(--sea05); color: var(--lit);} \
//     @media screen and (orientation: landscape)  and (height <= 500px) { \
//     h1 { margin: 0; } \
//     } \
//     @media screen and (orientation: portrait) and (width <= 1000px) { \
//     * { font-size: 33px; } \
//     h1 { font-size: 2.3em; margin-bottom: 0; } \
//     body { justify-content: flex-start; } \
//     .content { max-width: none; padding: 0 2rem; } \
//     } \
//     @media screen and (orientation: portrait) and (width <= 550px) { \
//     * { font-size: 23px; } \
//     h1 { font-size: 2.3em; margin-bottom: 0; } \
//     body { justify-content: flex-start; } \
//     .content { max-width: none; padding: 0 2rem; } \
//     } \
//     </style> \
//     </head> \
//     <body> \
//     <h1>JaQC Settings</h1> \
//     <form id=f><div class=content> \
//     <div class=lbl>Setting 1:</div><input type=text name=s1 ><br> \
//     <div class=lbl>Setting 2:</div><input type=text name=s2 ><br> \
//     <div class=lbl>Setting 3:</div><input type=text name=s3 ><br> \
//     <div class=lbl>Setting 4:</div><input type=text name=s4 ><br> \
//     <div class=lbl>Setting 5:</div><input type=text name=s5 ><br> \
//     <button class=btn>Save</button> \
//     </div></form><pre id=o></pre> \
//     </body> \
//     <script> \
//     f.onsubmit=async e=> {e.preventDefault() \
//     const b=JSON.stringify({ \
//     setting 1: f.s1.value, \
//     setting 2: f.s2.value, \
//     setting 3: f.s3.value, \
//     setting 4: f.s4.value, \
//     setting 5: f.s5.value, \
//     }) \
//     console.log(b) \
//     const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:b}) \
//     o.textContent=await r.text()} \
//     </script> \
//     <style> \
//     </style> \
//     </html>"; */

#ifdef __cplusplus
}
#endif

#endif // UTIL_HTML_FB_H
/**
 * ShiftLight Web UI - Settings Page JavaScript
 * Handles the settings configuration page functionality
 */

(function () {
  "use strict";

  // ============================================
  // Configuration
  // ============================================
  const MANUAL_CONNECT_RETRY_COUNT = 1;
  const WIFI_SCAN_COOLDOWN_MS = 6000;
  const STATUS_POLL_MS = 2500;
  const WIFI_POLL_MS = 3200;
  const BLE_POLL_MS = 3400;

  // ============================================
  // State Management
  // ============================================
  let settingsDirty = false;
  let refreshActive = false;
  let refreshStart = 0;
  let dotIntervals = {};
  let consoleLastTx = "";
  let consoleLastObd = "";
  let initialSettingsState = null;
  let lastWifiScanStart = 0;
  let wifiInitialScanDone = false;
  let wifiStatusCache = null;
  let wifiConnectInFlight = false;
  let uiModeOverride = false;
  let currentUiMode = null;
  let lastWifiStatus = null;
  let wifiLastScanResults = [];
  let bleDeviceMap = new Map();

  // ============================================
  // DOM Element References
  // ============================================
  const $ = (id) => document.getElementById(id);

  // ============================================
  // Utility Functions
  // ============================================
  function setAnimatedDots(el, loading) {
    if (!el) return;
    const key = el.id;
    if (loading) {
      if (dotIntervals[key]) return;
      let step = 0;
      dotIntervals[key] = setInterval(() => {
        step = (step + 1) % 4;
        el.innerText = (el.dataset.base || "") + ".".repeat(step);
      }, 400);
    } else {
      if (dotIntervals[key]) {
        clearInterval(dotIntervals[key]);
        dotIntervals[key] = null;
      }
      el.innerText = el.dataset.base || "";
    }
  }

  // ============================================
  // OBD Console
  // ============================================
  function appendConsole(line) {
    const box = $("obdConsole");
    if (!box || !line) return;
    const div = document.createElement("div");
    const txt = String(line).trim();
    let cls = "console-line";
    if (txt.charAt(0) === ">") {
      cls += " console-line-tx";
    } else if (txt.charAt(0) === "!") {
      cls += " console-line-err";
    } else if (/NO DATA|ERROR|UNABLE TO CONNECT|STOPPED|BUS INIT/i.test(txt)) {
      cls += " console-line-err";
    } else {
      cls += " console-line-rx";
    }
    div.className = cls;
    div.textContent = txt;
    box.appendChild(div);
    box.scrollTop = box.scrollHeight;
  }

  function formatObdLine(lastTx, resp) {
    if (!resp) return null;
    const cleanResp = resp.trim();
    const parts = cleanResp.split(/\s+/);
    if (parts.length < 2) return null;
    const mode = parts[0].toUpperCase();
    const pid = parts[1].toUpperCase();
    if (mode !== "41") return null;
    if (pid === "0C" && parts.length >= 4) {
      const A = parseInt(parts[2], 16);
      const B = parseInt(parts[3], 16);
      if (isNaN(A) || isNaN(B)) return null;
      const rpm = (A * 256 + B) / 4;
      return "< " + cleanResp + "   (RPM ≈ " + rpm + ")";
    }
    if (pid === "0D" && parts.length >= 3) {
      const A = parseInt(parts[2], 16);
      if (isNaN(A)) return null;
      return "< " + cleanResp + "   (Speed ≈ " + A + " km/h)";
    }
    return "< " + cleanResp;
  }

  // ============================================
  // Dirty State Tracking
  // ============================================
  function captureInitialSettingsState() {
    const form = $("settingsForm");
    if (!form) return;
    initialSettingsState = {};
    const elements = form.querySelectorAll("input,select,textarea");
    elements.forEach((el) => {
      if (!el.name) return;
      let val;
      if (el.type === "checkbox") {
        val = el.checked ? "on" : "";
      } else {
        val = el.value;
      }
      initialSettingsState[el.name] = val;
    });
  }

  function recomputeSettingsDirty() {
    const form = $("settingsForm");
    if (!form || !initialSettingsState) return;
    let changed = false;
    const elements = form.querySelectorAll("input,select,textarea");
    const current = {};
    elements.forEach((el) => {
      if (!el.name) return;
      let val;
      if (el.type === "checkbox") {
        val = el.checked ? "on" : "";
      } else {
        val = el.value;
      }
      current[el.name] = val;
    });
    for (const k in initialSettingsState) {
      if (initialSettingsState[k] !== current[k]) {
        changed = true;
        break;
      }
    }
    settingsDirty = changed;
    const s = $("settingsSave");
    const r = $("settingsReset");
    if (s) s.disabled = !changed;
    if (r) r.style.display = changed ? "block" : "none";
  }

  function markSettingsDirty() {
    recomputeSettingsDirty();
  }

  // ============================================
  // Status & Error Display
  // ============================================
  function setStatus(text, loading) {
    const el = $("vehicleStatus");
    if (el) {
      el.dataset.base = text;
      setAnimatedDots(el, loading);
    }
  }

  function setRefreshActive(on) {
    refreshActive = on;
    const btn = $("btnVehicleRefresh");
    if (btn) btn.disabled = on;
    const err = $("settingsError");
    if (err && !on) err.innerText = "";
  }

  function showError(msg) {
    const err = $("settingsError");
    if (err) err.innerText = msg;
  }

  // ============================================
  // WiFi Functions
  // ============================================
  function wifiModeValue() {
    const sel = $("wifiMode");
    const v = sel ? sel.value : "0";
    if (v === "1") return "STA_ONLY";
    if (v === "2") return "STA_WITH_AP_FALLBACK";
    return "AP_ONLY";
  }

  function wifiModeOption(mode) {
    switch (mode) {
      case "STA_ONLY":
        return "1";
      case "STA_WITH_AP_FALLBACK":
        return "2";
      default:
        return "0";
    }
  }

  function wifiModeLabel(mode) {
    switch (mode) {
      case "STA_ONLY":
        return "Heim-WLAN (nur STA)";
      case "STA_WITH_AP_FALLBACK":
        return "Heim-WLAN mit AP-Fallback";
      default:
        return "Access Point (nur AP)";
    }
  }

  function isApOnlyMode(mode) {
    const val = mode || wifiModeValue();
    return val === "0" || val === "AP_ONLY";
  }

  function wifiBars(rssi) {
    const level = parseInt(rssi, 10);
    const steps = [-90, -75, -60];
    const heights = [8, 12, 16];
    let html = "";
    for (let i = 0; i < steps.length; i++) {
      const active = level >= steps[i];
      const h = heights[i] || 12;
      html += `<span class="wifi-bar${
        active ? " active" : ""
      }" style="height:${h}px"></span>`;
    }
    return html;
  }

  function updateWifiScanUi(running, label) {
    const scanBtn = $("wifiScanBtn");
    const scanStatus = $("wifiScanStatus");
    const text = label || "Suche...";
    if (scanBtn) {
      scanBtn.disabled = !!running;
      scanBtn.classList.toggle("loading", !!running);
      if (running) {
        scanBtn.innerHTML =
          '<span class="spinner"></span><span class="btn-label">' +
          text +
          "</span>";
      } else {
        scanBtn.innerHTML = '<span class="btn-label">Netzwerke suchen</span>';
      }
    }
    if (scanStatus) {
      if (running) {
        scanStatus.innerHTML =
          '<span class="spinner"></span><span>' + text + "</span>";
      } else {
        scanStatus.textContent = label || "Bereit";
      }
    }
  }

  function renderWifiResults(data) {
    const list = $("wifiResultsList");
    const empty = $("wifiScanEmpty");
    const wrapper = $("wifiResults");
    if (!list || !wrapper) return;

    const scanning = !!(data && data.scanRunning);
    const resultsRaw = (data && data.scanResults) || [];
    if (!scanning) {
      wifiLastScanResults = Array.isArray(resultsRaw) ? resultsRaw.slice() : [];
    }
    const results = scanning ? wifiLastScanResults || [] : resultsRaw;
    list.textContent = "";

    const busySsid = data && data.staConnecting ? data.currentSsid || "" : "";
    const connectedSsid =
      data && data.staConnected ? data.currentSsid || "" : "";

    results.forEach((res) => {
      const item = document.createElement("button");
      item.type = "button";
      item.className = "device-item";

      const meta = document.createElement("div");
      meta.className = "device-meta";

      const name = document.createElement("span");
      name.className = "device-name";
      name.textContent = res.ssid || "(unbekannt)";

      const rssi = document.createElement("div");
      rssi.className = "wifi-rssi";
      rssi.innerHTML = wifiBars(res.rssi);

      meta.appendChild(name);
      meta.appendChild(rssi);

      const isBusy = busySsid && res.ssid === busySsid;
      const isConnected = connectedSsid && res.ssid === connectedSsid;

      const pill = document.createElement("span");
      pill.className = "device-pill";
      if (isConnected) {
        pill.innerHTML = '<span class="pill ok">Verbunden</span>';
        item.classList.add("disabled");
      } else if (isBusy) {
        pill.innerHTML =
          '<span class="spinner"></span><span>Verbinde...</span>';
      } else {
        pill.innerHTML = '<span class="pill">Verbinden</span>';
      }

      if ((busySsid && !isBusy) || isConnected) {
        item.classList.add("disabled");
      }

      item.appendChild(meta);
      item.appendChild(pill);

      if (!isBusy) {
        item.addEventListener("click", () =>
          openWifiPasswordModal(res.ssid || "")
        );
      }

      list.appendChild(item);
    });

    if (empty) {
      empty.style.display =
        results.length === 0 && !data.scanRunning ? "block" : "none";
    }

    if (wrapper) {
      const showList =
        !isApOnlyMode(data.mode) &&
        (results.length > 0 || data.scanRunning || busySsid);
      wrapper.classList.toggle("collapsed", !showList);
    }
  }

  function setWifiStatusUi(data, opts) {
    data = data || {};
    const preview = !!(opts && opts.preview);
    const dot = $("wifiStatusDot");

    if (!preview) {
      wifiStatusCache = data;
    }

    const backendMode =
      (preview && wifiStatusCache ? wifiStatusCache.mode : data.mode) ||
      wifiModeValue();
    if (!currentUiMode) {
      currentUiMode = backendMode;
    }

    const prev = lastWifiStatus || {};
    const modeChanged =
      !preview && prev.mode && data.mode && prev.mode !== data.mode;
    const connectSuccess = !preview && data.staConnected && !prev.staConnected;
    const disconnectEvent =
      !preview &&
      prev.staConnected &&
      !data.staConnected &&
      !data.staConnecting;
    const connectError = !preview && data.staLastError && !data.staConnecting;

    if (connectSuccess || connectError || modeChanged || disconnectEvent) {
      uiModeOverride = false;
      currentUiMode = backendMode;
    }

    if (!uiModeOverride && !preview) {
      currentUiMode = backendMode;
    }

    const effectiveMode = currentUiMode || backendMode || "AP_ONLY";
    const statusMode = data.mode || backendMode || effectiveMode;
    const uiApMode = isApOnlyMode(effectiveMode);

    if (dot) {
      dot.classList.remove("ok", "warn", "bad");
    }

    const ssidLabel = data.currentSsid || "(unbekannt)";
    const apActive = !!data.apActive;
    const staConnected = !!data.staConnected;
    const connecting = !!data.staConnecting;
    const scanning = !!data.scanRunning;
    const clients = Number(data.apClients || 0);

    let statusText = "Keine Verbindung";
    let dotClass = "bad";

    if (isApOnlyMode(statusMode)) {
      statusText = apActive ? "Access Point aktiv" : "Access Point inaktiv";
      dotClass = apActive ? "ok" : "bad";
    } else if (staConnected) {
      statusText = "STA verbunden mit " + ssidLabel;
      dotClass = "ok";
    } else if (connecting || scanning) {
      statusText = connecting
        ? "Verbindung wird aufgebaut... " + ssidLabel
        : "Suche nach Netzwerken...";
      dotClass = "warn";
    } else if (apActive) {
      statusText = "AP aktiv, STA nicht verbunden";
      dotClass = "ok";
    }

    if (dot && dotClass) {
      dot.classList.add(dotClass);
    }

    const text = $("wifiStatusText");
    if (text) text.textContent = statusText;

    const sub = $("wifiStatusSub");
    if (sub) sub.textContent = "Modus: " + wifiModeLabel(statusMode);

    const meta = $("wifiStatusMeta");
    if (meta) {
      const parts = [];
      if (data.staIp) parts.push("STA-IP: " + data.staIp);
      if (apActive && data.apIp) parts.push("AP-IP: " + data.apIp);
      if (apActive) parts.push("AP-Clients: " + clients);
      meta.textContent = parts.length
        ? parts.join(" • ")
        : "Keine IP verfügbar";
    }

    const err = $("wifiStatusError");
    if (err) {
      err.textContent = data.staLastError || "";
      err.style.display = data.staLastError ? "block" : "none";
    }

    const disconnect = $("wifiDisconnectBtn");
    if (disconnect) {
      const show = !isApOnlyMode(statusMode) && staConnected;
      disconnect.style.display = show ? "inline-block" : "none";
      disconnect.disabled = !staConnected;
      disconnect.style.opacity = disconnect.disabled ? "0.7" : "1";
    }

    const scanLabel = connecting
      ? "Verbinde..."
      : scanning
      ? "Suche..."
      : uiApMode
      ? "AP-Modus aktiv"
      : "Netzwerke suchen";
    updateWifiScanUi(scanning || connecting, scanLabel);
    renderWifiResults(Object.assign({}, data, { mode: effectiveMode }));

    const interactive = $("wifiInteractive");
    if (interactive) {
      interactive.classList.toggle("expanded", !uiApMode);
      interactive.classList.toggle("collapsed", uiApMode);
    }

    const modeSel = $("wifiMode");
    if (modeSel) {
      const val = wifiModeOption(effectiveMode);
      if (modeSel.value !== val) {
        modeSel.value = val;
      }
    }

    if (
      !wifiInitialScanDone &&
      !data.scanRunning &&
      !data.staConnecting &&
      !uiApMode
    ) {
      wifiInitialScanDone = true;
      if (!data.scanResults || data.scanResults.length === 0) {
        startWifiScan(true);
      }
    }

    if (!preview) {
      lastWifiStatus = data;
    }
  }

  function startWifiScan(force) {
    const now = Date.now();
    const mode =
      currentUiMode ||
      (wifiStatusCache ? wifiStatusCache.mode : null) ||
      wifiModeValue();

    if (isApOnlyMode(mode)) {
      updateWifiScanUi(false, "AP-Modus aktiv");
      return;
    }

    if (!force && now - lastWifiScanStart < WIFI_SCAN_COOLDOWN_MS) {
      return;
    }

    if (
      wifiStatusCache &&
      (wifiStatusCache.staConnecting || wifiStatusCache.scanRunning)
    ) {
      lastWifiScanStart = now;
      updateWifiScanUi(
        true,
        wifiStatusCache.staConnecting ? "Verbinde..." : "Suche..."
      );
      return;
    }

    lastWifiScanStart = now;
    updateWifiScanUi(true, "Suche...");

    fetch("/wifi/scan", { method: "POST" })
      .then((r) => r.json())
      .then((d) => {
        if (d && d.status === "busy") {
          updateWifiScanUi(true, "Scan läuft...");
        }
      })
      .catch(() => {
        updateWifiScanUi(false, "Bereit");
      });
  }

  function fetchWifiStatus() {
    fetch("/wifi/status")
      .then((r) => r.json())
      .then(setWifiStatusUi)
      .catch(() => {});
  }

  function closeWifiPasswordModal() {
    const modal = $("wifiPasswordModal");
    if (modal) modal.classList.add("hidden");
    const pw = $("wifiModalPassword");
    if (pw) pw.value = "";
  }

  function openWifiPasswordModal(ssid) {
    const modal = $("wifiPasswordModal");
    const label = $("wifiModalSsid");
    if (label) label.textContent = ssid || "";
    const pwd = $("wifiModalPassword");
    if (pwd) {
      pwd.value = "";
      setTimeout(() => pwd.focus(), 100);
    }
    if (modal) modal.classList.remove("hidden");
  }

  function submitWifiPassword() {
    const ssidEl = $("wifiModalSsid");
    const passEl = $("wifiModalPassword");
    if (!ssidEl || !passEl) return;

    const ssid = ssidEl.textContent || "";
    const password = passEl.value || "";
    const modeEl = $("wifiMode");
    const modeVal = modeEl ? modeEl.value : "0";

    const params = new URLSearchParams();
    params.append("ssid", ssid);
    params.append("password", password);
    params.append("mode", modeVal);

    if (wifiConnectInFlight) return;
    wifiConnectInFlight = true;

    updateWifiScanUi(true, "Verbinde...");

    const btn = $("wifiModalConnect");
    if (btn) btn.disabled = true;

    fetch("/wifi/connect", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: params.toString(),
    })
      .catch(() => {})
      .finally(() => {
        wifiConnectInFlight = false;
        if (btn) btn.disabled = false;
        closeWifiPasswordModal();
        fetchWifiStatus();
      });
  }

  function disconnectWifi() {
    uiModeOverride = false;
    currentUiMode = null;
    const btn = $("wifiDisconnectBtn");
    if (btn) btn.disabled = true;
    updateWifiScanUi(true, "Trenne...");
    fetch("/wifi/disconnect", { method: "POST" }).finally(() => {
      updateWifiScanUi(false, "Bereit");
      fetchWifiStatus();
    });
  }

  // ============================================
  // BLE Functions
  // ============================================
  function setBleError(msg) {
    const el = $("bleError");
    if (el) el.innerText = msg || "";
  }

  function setBleStatusUi(data) {
    data = data || {};
    const dot = $("bleStatusDot");

    if (dot) {
      dot.classList.remove("ok", "warn");
      if (data.connected) {
        dot.classList.add("ok");
      } else if (data.connectInProgress) {
        dot.classList.add("warn");
      }
    }

    const txt = $("bleStatusText");
    if (txt) {
      let t = "Keine Verbindung";
      if (data.connectError) {
        t = data.connectError;
      } else if (data.manualFailed) {
        t = "Verbindung fehlgeschlagen";
      } else if (data.connectInProgress) {
        t = "Verbindung wird aufgebaut...";
      } else if (data.connected) {
        t = "Verbunden";
      }
      txt.textContent = t;
    }

    const tgt = $("bleTargetName");
    if (tgt && data.targetName !== undefined) {
      tgt.textContent = data.targetName || data.targetAddr || "(unbekannt)";
    }

    const addr = $("bleTargetAddr");
    if (addr && data.targetAddr !== undefined) {
      addr.textContent = data.targetAddr || "–";
    }

    const scanStatus = $("bleScanStatus");
    if (scanStatus) {
      if (data.scanRunning) {
        scanStatus.innerHTML =
          '<span class="spinner"></span><span>Suche Geräte...</span>';
      } else if (data.connectInProgress) {
        scanStatus.textContent = "Verbinden...";
      } else if (data.manualFailed) {
        scanStatus.textContent = "Keine Verbindung";
      } else if (data.scanAge >= 0) {
        scanStatus.textContent = "Letzter Scan: " + data.scanAge + "s";
      } else {
        scanStatus.textContent = "Bereit";
      }
    }

    const scanBtn = $("bleScanBtn");
    if (scanBtn) {
      scanBtn.disabled = !!data.scanRunning || !!data.connectInProgress;
      scanBtn.classList.toggle("loading", !!data.scanRunning);
      scanBtn.innerHTML = data.scanRunning
        ? '<span class="spinner"></span><span class="btn-label">Suche Geräte...</span>'
        : '<span class="btn-label">Geräte suchen</span>';
    }

    renderBleResults(data);
  }

  function renderBleResults(data) {
    const list = $("bleResultsList");
    const empty = $("bleScanEmpty");
    const wrapper = $("bleResults");
    const results = (data && data.results) || [];
    const busyAddr = (data && data.connectTargetAddr) || "";
    const busy = data && data.connectInProgress;
    const scanning = !!(data && data.scanRunning);

    if (empty) {
      const count = results.length || bleDeviceMap.size;
      empty.style.display = count === 0 && !scanning ? "block" : "none";
    }

    if (wrapper) {
      const showList =
        scanning ||
        !data.connected ||
        data.manualActive ||
        data.manualFailed ||
        results.length > 0 ||
        bleDeviceMap.size > 0;
      if (showList) {
        wrapper.classList.remove("collapsed");
        wrapper.style.maxHeight = "800px";
      } else {
        wrapper.classList.add("collapsed");
        wrapper.style.maxHeight = "0px";
      }
    }

    if (!list) return;

    const seen = new Set();
    results.forEach((dev) => {
      const key =
        dev.addr ||
        dev.name ||
        "unknown-" + Math.random().toString(16).slice(2);
      seen.add(key);

      let btn = bleDeviceMap.get(key);
      if (!btn) {
        btn = document.createElement("button");
        btn.className = "device-item ble-device";
        btn.dataset.key = key;
        btn.dataset.addr = dev.addr || "";
        btn.dataset.name = dev.name || "";
        list.appendChild(btn);
        bleDeviceMap.set(key, btn);
      }

      btn.classList.remove("ble-device-fadeout", "disabled");
      btn.dataset.addr = dev.addr || "";
      btn.dataset.name = dev.name || "";

      const isBusy = busy && !!dev.addr && dev.addr === busyAddr;
      if (busy && !isBusy) {
        btn.classList.add("disabled");
      }

      const pill = isBusy
        ? '<span class="device-pill"><span class="spinner"></span><span>Verbinde...</span></span>'
        : '<span class="pill">Verbinden</span>';
      btn.innerHTML = `<span class="device-meta"><span class="device-name">${
        dev.name || "(unbekannt)"
      }</span><span class="device-addr">${dev.addr || ""}</span></span>${pill}`;

      if (!busy || isBusy) {
        btn.onclick = () => {
          requestBleConnect(dev.addr || "", dev.name || "");
        };
      } else {
        btn.onclick = null;
      }
    });

    if (!scanning) {
      bleDeviceMap.forEach((el, key) => {
        if (!seen.has(key)) {
          el.classList.add("ble-device-fadeout");
          const remove = () => {
            el.removeEventListener("transitionend", remove);
            if (el.parentElement) {
              el.parentElement.removeChild(el);
            }
          };
          el.addEventListener("transitionend", remove);
          setTimeout(remove, 300);
          bleDeviceMap.delete(key);
        }
      });
    }
  }

  function requestBleConnect(addr, name) {
    const btn = $("bleScanBtn");
    if (btn) btn.disabled = true;
    setBleError("");

    const list = $("bleResultsList");
    if (list) {
      list.querySelectorAll(".device-item").forEach((el) => {
        const isTarget = (el.dataset && el.dataset.addr) === addr;
        if (isTarget) {
          const meta = el.querySelector(".device-meta");
          const metaHtml = meta ? meta.innerHTML : "";
          el.classList.remove("disabled");
          el.innerHTML =
            '<span class="device-meta">' +
            metaHtml +
            '</span><span class="device-pill"><span class="spinner"></span><span>Verbinde...</span></span>';
        } else {
          el.classList.add("disabled");
        }
      });
    }

    fetch("/ble/connect-device", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: `address=${encodeURIComponent(addr)}&name=${encodeURIComponent(
        name || ""
      )}&attempts=${MANUAL_CONNECT_RETRY_COUNT}`,
    })
      .then((r) => {
        if (!r.ok) throw new Error();
        return r.json ? r.json() : null;
      })
      .catch(() => {
        setBleError("Verbindung konnte nicht gestartet werden.");
      });
  }

  function startBleScan() {
    setBleError("");
    const btn = $("bleScanBtn");
    if (btn) {
      btn.disabled = true;
      btn.classList.add("loading");
      btn.innerHTML =
        '<span class="spinner"></span><span class="btn-label">Suche Geräte...</span>';
    }

    fetch("/ble/scan", { method: "POST" })
      .then((r) => r.json())
      .then((d) => {
        if (d && d.status === "busy") {
          setBleError("Scan läuft bereits oder Verbindung wird aufgebaut.");
        }
      })
      .catch(() => {
        setBleError("Scan konnte nicht gestartet werden.");
      });
  }

  function fetchBleStatus() {
    fetch("/ble/status")
      .then((r) => r.json())
      .then((d) => {
        setBleStatusUi(d || {});
        if (
          !d.connected &&
          !d.scanRunning &&
          (!d.results || d.results.length === 0)
        ) {
          startBleScan();
        }
      })
      .catch(() => {});
  }

  // ============================================
  // Vehicle Info
  // ============================================
  function updateVehicleInfo(data) {
    ["vehicleModel", "vehicleVin", "vehicleDiag"].forEach((id) => {
      const el = $(id);
      if (el && data[id] !== undefined) {
        el.dataset.base = data[id];
        if (!data.vehicleInfoRequestRunning) el.innerText = data[id];
      }
    });

    let statusText = "Noch keine Daten";
    let loading = false;
    if (data.vehicleInfoRequestRunning) {
      statusText = "Abruf läuft";
      loading = true;
    } else if (data.vehicleInfoReady) {
      statusText =
        data.vehicleInfoAge <= 1
          ? "Gerade aktualisiert (0s)"
          : "Letztes Update vor " + data.vehicleInfoAge + "s";
    }

    setStatus(statusText, loading);
    setAnimatedDots($("vehicleModel"), loading);
    setAnimatedDots($("vehicleVin"), loading);
    setAnimatedDots($("vehicleDiag"), loading);

    if (refreshActive && !data.vehicleInfoRequestRunning) {
      if (data.vehicleInfoReady && data.vehicleInfoAge <= 2) {
        setRefreshActive(false);
        setStatus("Gerade aktualisiert (0s)", false);
        showError("");
      } else if (Date.now() - refreshStart > 7000) {
        setRefreshActive(false);
        showError("Keine Antwort vom Fahrzeug.");
      }
    }

    // Console auto-log
    const autoLog = $("obdAutoLog");
    const allowLog = !autoLog || autoLog.checked;

    if (
      allowLog &&
      data.lastTx !== undefined &&
      data.lastTx !== consoleLastTx
    ) {
      consoleLastTx = data.lastTx;
      appendConsole("> " + data.lastTx);
    }

    if (
      allowLog &&
      data.lastObd !== undefined &&
      data.lastObd !== consoleLastObd
    ) {
      consoleLastObd = data.lastObd;
      const pretty = formatObdLine(consoleLastTx, data.lastObd);
      appendConsole(pretty || "< " + data.lastObd);
    }
  }

  // ============================================
  // Status Polling
  // ============================================
  function poll() {
    fetch("/status")
      .then((r) => r.json())
      .then(updateVehicleInfo)
      .catch(() => {});
  }

  // ============================================
  // Dev Mode Sections
  // ============================================
  function updateDevSection() {
    const devToggle = $("devModeToggle");
    const devSection = $("devObdSection");
    if (!devSection) return;

    if (devToggle && devToggle.checked) {
      devSection.classList.remove("dev-collapsed");
      devSection.classList.add("dev-expanded");
    } else {
      devSection.classList.remove("dev-expanded");
      devSection.classList.add("dev-collapsed");
    }
  }

  // ============================================
  // OBD Console Init
  // ============================================
  function initObdAutoLog() {
    const chk = $("obdAutoLog");
    if (!chk) return;
    const stored = localStorage.getItem("obdAutoLog");
    if (stored === null) {
      chk.checked = true;
    } else {
      chk.checked = stored === "1";
    }
    chk.addEventListener("change", () => {
      localStorage.setItem("obdAutoLog", chk.checked ? "1" : "0");
    });
  }

  function initObdConsole() {
    const box = $("obdConsole");
    const btn = $("obdSendBtn");
    const input = $("obdCmdInput");
    const clearBtn = $("obdClearBtn");

    if (clearBtn) {
      clearBtn.addEventListener("click", () => {
        if (box) box.textContent = "";
      });
    }

    if (!box || !btn || !input) return;

    let sending = false;

    function setSending(on) {
      sending = on;
      btn.disabled = on;
      input.disabled = on;
      if (on) {
        if (!btn.dataset.label) btn.dataset.label = btn.textContent;
        btn.innerHTML = '<span class="spinner"></span>';
      } else {
        btn.textContent = btn.dataset.label || "Senden";
      }
    }

    function doSend() {
      const cmd = input.value.trim();
      if (!cmd || sending) return;
      setSending(true);
      appendConsole("> " + cmd);

      fetch("/dev/obd-send", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "cmd=" + encodeURIComponent(cmd),
      })
        .then((resp) => {
          if (!resp.ok) {
            return resp.text().then((t) => {
              appendConsole("! " + (t || "Fehler beim Senden"));
            });
          }
          return resp.json().then((data) => {
            if (data && data.lastObd) {
              const txt =
                formatObdLine(data.lastTx || "", data.lastObd) ||
                "< " + data.lastObd;
              appendConsole(txt);
              if (data.lastTx !== undefined) consoleLastTx = data.lastTx;
              if (data.lastObd !== undefined) consoleLastObd = data.lastObd;
            }
          });
        })
        .catch(() => {
          appendConsole("! Fehler beim Senden");
        })
        .finally(() => {
          setSending(false);
        });
    }

    btn.addEventListener("click", doSend);
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        doSend();
      }
    });
  }

  // ============================================
  // Load Initial Config
  // ============================================
  function loadConfig() {
    fetch("/api/config")
      .then((r) => r.json())
      .then((c) => {
        // Set form values
        if ($("devModeToggle")) $("devModeToggle").checked = c.devMode;
        if ($("wifiMode")) $("wifiMode").value = c.wifiMode;

        // Vehicle info
        if ($("vehicleModel")) {
          $("vehicleModel").dataset.base = c.vehicleModel || "-";
          $("vehicleModel").innerText = c.vehicleModel || "-";
        }
        if ($("vehicleVin")) {
          $("vehicleVin").dataset.base = c.vehicleVin || "-";
          $("vehicleVin").innerText = c.vehicleVin || "-";
        }
        if ($("vehicleDiag")) {
          $("vehicleDiag").dataset.base = c.vehicleDiag || "-";
          $("vehicleDiag").innerText = c.vehicleDiag || "-";
        }

        // BLE info
        if ($("bleTargetName")) {
          $("bleTargetName").dataset.base = c.currentTargetName || "-";
          $("bleTargetName").innerText = c.currentTargetName || "-";
        }
        if ($("bleTargetAddr")) {
          $("bleTargetAddr").dataset.base = c.currentTargetAddr || "-";
          $("bleTargetAddr").innerText = c.currentTargetAddr || "-";
        }

        // Update dev section visibility
        updateDevSection();

        // Capture initial state after loading
        setTimeout(() => {
          captureInitialSettingsState();
          recomputeSettingsDirty();
        }, 100);
      })
      .catch((err) => {
        console.error("Failed to load config:", err);
      });
  }

  // ============================================
  // Initialize UI
  // ============================================
  function initUI() {
    // Form change tracking
    const form = $("settingsForm");
    if (form) {
      form.addEventListener("change", markSettingsDirty);
    }

    // Reset button
    const resetBtn = $("settingsReset");
    if (resetBtn) {
      resetBtn.addEventListener("click", () => window.location.reload());
    }

    // WiFi mode selector
    const wifiModeSel = $("wifiMode");
    if (wifiModeSel) {
      wifiModeSel.addEventListener("change", () => {
        wifiInitialScanDone = false;
        uiModeOverride = true;
        currentUiMode = wifiModeValue();
        markSettingsDirty();
        setWifiStatusUi(
          Object.assign({}, wifiStatusCache || {}, { mode: currentUiMode }),
          { preview: true }
        );
      });
      setWifiStatusUi(wifiStatusCache || { mode: wifiModeValue() }, {
        preview: true,
      });
    }

    // WiFi buttons
    const wifiScan = $("wifiScanBtn");
    if (wifiScan) {
      wifiScan.addEventListener("click", () => startWifiScan(true));
    }

    const wifiDisconnect = $("wifiDisconnectBtn");
    if (wifiDisconnect) {
      wifiDisconnect.addEventListener("click", disconnectWifi);
    }

    // WiFi modal
    const wifiModalCancel = $("wifiModalCancel");
    if (wifiModalCancel) {
      wifiModalCancel.addEventListener("click", closeWifiPasswordModal);
    }

    const wifiModalConnect = $("wifiModalConnect");
    if (wifiModalConnect) {
      wifiModalConnect.addEventListener("click", submitWifiPassword);
    }

    // Close modal on backdrop click
    const modalBackdrop = document.querySelector(".modal-backdrop");
    if (modalBackdrop) {
      modalBackdrop.addEventListener("click", closeWifiPasswordModal);
    }

    // Vehicle refresh button
    const vehicleRefresh = $("btnVehicleRefresh");
    if (vehicleRefresh) {
      vehicleRefresh.addEventListener("click", () => {
        setRefreshActive(true);
        refreshStart = Date.now();
        setStatus("Abruf läuft", true);

        fetch("/settings/vehicle-refresh", { method: "POST" })
          .then((r) => r.json())
          .then((d) => {
            if (!d || d.status !== "started") {
              setRefreshActive(false);
              if (d && d.reason === "no-connection") {
                showError("Keine OBD-Verbindung vorhanden.");
                setStatus("Sync nicht möglich", false);
              } else {
                showError("Sync konnte nicht gestartet werden.");
              }
            }
          })
          .catch(() => {
            setRefreshActive(false);
            showError("Sync fehlgeschlagen.");
          });
      });
    }

    // BLE scan button
    const bleBtn = $("bleScanBtn");
    if (bleBtn) {
      bleBtn.addEventListener("click", () => {
        startBleScan();
      });
    }

    // Form submission
    if (form) {
      form.addEventListener("submit", function (ev) {
        ev.preventDefault();
        const fd = new FormData(this);
        const params = new URLSearchParams();
        fd.forEach((v, k) => {
          params.append(k, v);
        });

        fetch("/settings", {
          method: "POST",
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body: params.toString(),
        })
          .then(() => {
            captureInitialSettingsState();
            recomputeSettingsDirty();
          })
          .catch(() => {});
      });
    }

    // Dev mode toggle
    const devToggle = $("devModeToggle");
    if (devToggle) {
      devToggle.addEventListener("change", () => {
        markSettingsDirty();
        updateDevSection();
      });
      updateDevSection();
    }

    // Initialize OBD console
    initObdAutoLog();
    initObdConsole();

    // Load config
    loadConfig();

    // Start polling
    poll();
    fetchWifiStatus();
    fetchBleStatus();

    setInterval(poll, STATUS_POLL_MS);
    setInterval(fetchWifiStatus, WIFI_POLL_MS);
    setInterval(fetchBleStatus, BLE_POLL_MS);

    // Capture initial state
    captureInitialSettingsState();
    recomputeSettingsDirty();
  }

  // ============================================
  // Entry Point
  // ============================================
  document.addEventListener("DOMContentLoaded", initUI);
})();

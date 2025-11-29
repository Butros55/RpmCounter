/**
 * ShiftLight Web UI - Main Page JavaScript
 * Handles the main configuration page functionality
 */

(function () {
  "use strict";

  // ============================================
  // Configuration (loaded from /api/config)
  // ============================================
  let CONFIG = {
    testSweepDuration: 5000,
    numLeds: 30,
  };

  // ============================================
  // State Management
  // ============================================
  let saveDirty = false;
  let initialMainState = null;
  let pendingSpinner = 0;
  let lastSpinnerTs = 0;
  let statusTimer = null;
  let dotIntervals = {};
  let ledBlinkState = false;
  let lastLedBlinkTs = 0;
  let blinkPreviewActive = false;
  let blinkPreviewEnd = 0;
  let testSweepActive = false;
  let testSweepStart = 0;
  let previewTimerId = null;
  let devModeEnabled = false;

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

  function htmlEscape(str) {
    const div = document.createElement("div");
    div.textContent = str;
    return div.innerHTML;
  }

  // ============================================
  // Dirty State Tracking
  // ============================================
  function captureInitialMainState() {
    const form = $("mainForm");
    if (!form) return;
    initialMainState = {};
    const elements = form.querySelectorAll("input,select,textarea");
    elements.forEach((el) => {
      if (!el.name) return;
      let val;
      if (el.type === "checkbox") {
        val = el.checked ? "on" : "";
      } else {
        val = el.value;
      }
      initialMainState[el.name] = val;
    });
  }

  function recomputeMainDirty() {
    const form = $("mainForm");
    if (!form) {
      saveDirty = false;
      return;
    }
    if (!initialMainState) captureInitialMainState();
    let changed = false;
    const current = {};
    const elements = form.querySelectorAll("input,select,textarea");
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
    for (const k in initialMainState) {
      if (initialMainState[k] !== current[k]) {
        changed = true;
        break;
      }
    }
    saveDirty = changed;
    const b = $("btnSave");
    if (b) b.disabled = !changed;
    updateResetVisibility();
  }

  function markDirty() {
    recomputeMainDirty();
  }

  function updateResetVisibility() {
    const r = $("btnReset");
    if (r) r.style.display = saveDirty ? "block" : "none";
  }

  // ============================================
  // Spinner Management
  // ============================================
  function beginRequest() {
    pendingSpinner++;
    lastSpinnerTs = Date.now();
    updateSpinnerVisibility();
    return () => {
      pendingSpinner = Math.max(0, pendingSpinner - 1);
      updateSpinnerVisibility();
    };
  }

  function updateSpinnerVisibility() {
    const sp = $("debugSpinner");
    if (!sp) return;
    const idle = Date.now() - lastSpinnerTs > 3000;
    if (pendingSpinner <= 0 || idle) {
      sp.classList.add("hidden");
    } else {
      sp.classList.remove("hidden");
    }
  }

  function postSimple(url) {
    const done = beginRequest();
    fetch(url, { method: "POST" }).finally(done);
  }

  // ============================================
  // Brightness Control
  // ============================================
  function onBrightnessChange(v) {
    $("bval").innerText = v;
    $("brightness").value = v;
    markDirty();
    fetch("/brightness?val=" + v).catch(() => {});
  }

  // ============================================
  // Slider Display & Logic
  // ============================================
  function updateSliderDisplay(el) {
    const target = el.dataset.display;
    const span = $(target);
    if (span) span.innerText = el.value + "%";
  }

  function enforceSliderOrder(changedId) {
    const g = $("greenEndSlider");
    const y = $("yellowEndSlider");
    const b = $("blinkStartSlider");
    if (!g || !y || !b) return;
    let gv = parseInt(g.value || "0");
    let yv = parseInt(y.value || "0");
    let bv = parseInt(b.value || "0");

    const sync = (el, val) => {
      if (parseInt(el.value) !== val) {
        el.value = val;
        updateSliderDisplay(el);
      }
    };

    if (changedId === "greenEndSlider") {
      if (yv < gv) {
        yv = gv;
        sync(y, yv);
      }
      if (bv < yv) {
        bv = yv;
        sync(b, bv);
      }
    } else if (changedId === "yellowEndSlider") {
      if (yv < gv) {
        gv = yv;
        sync(g, gv);
      }
      if (bv < yv) {
        bv = yv;
        sync(b, bv);
      }
    } else if (changedId === "blinkStartSlider") {
      if (bv < yv) {
        yv = bv;
        sync(y, yv);
      }
      if (yv < gv) {
        gv = yv;
        sync(g, gv);
      }
    }
  }

  // ============================================
  // Autoscale UI
  // ============================================
  function updateAutoscaleUi() {
    const chk = $("autoscaleToggle");
    const cont = $("fixedMaxContainer");
    const inp = $("fixedMaxRpmInput");
    if (!chk || !cont || !inp) return;
    const on = chk.checked;
    inp.disabled = on;
    cont.classList.toggle("disabled-field", on);
  }

  // ============================================
  // LED Preview
  // ============================================
  function getThresholds() {
    const gv = parseInt($("greenEndSlider").value || "0");
    const yv = parseInt($("yellowEndSlider").value || "0");
    const bv = parseInt($("blinkStartSlider").value || "0");
    let greenEnd = gv / 100.0;
    let yellowEnd = yv / 100.0;
    let blinkStart = bv / 100.0;
    if (greenEnd < 0) greenEnd = 0;
    if (greenEnd > 1) greenEnd = 1;
    if (yellowEnd < greenEnd) yellowEnd = greenEnd;
    if (yellowEnd > 1) yellowEnd = 1;
    if (blinkStart < yellowEnd) blinkStart = yellowEnd;
    if (blinkStart > 1) blinkStart = 1;
    return { greenEnd, yellowEnd, blinkStart };
  }

  function computeSimFraction(t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    let pct = 0;
    if (t < 0.3) {
      let tt = t / 0.3;
      pct = tt * tt * (3 - 2 * tt);
    } else if (t < 0.6) {
      let tt = (t - 0.3) / 0.3;
      let base = 1.0 - 0.6 * tt;
      let wobble = 0.1 * Math.sin(tt * Math.PI * 4.0);
      pct = base + wobble;
      if (pct < 0.4) pct = 0.4;
      if (pct > 1.0) pct = 1.0;
    } else if (t < 0.85) {
      let tt = (t - 0.6) / 0.25;
      let base = 0.4 + 0.6 * (tt * tt * (3 - 2 * tt));
      let wobble = 0.05 * Math.sin(tt * Math.PI * 2.0);
      pct = base + wobble;
      if (pct < 0.4) pct = 0.4;
      if (pct > 1.0) pct = 1.0;
    } else {
      let tt = (t - 0.85) / 0.15;
      let base = 1.0 - tt;
      let wobble = 0.05 * Math.sin(tt * Math.PI * 2.0);
      pct = base + wobble;
      if (pct < 0.0) pct = 0.0;
      if (pct > 1.0) pct = 1.0;
    }
    return pct;
  }

  function layoutLedDots() {
    const cont = $("ledPreview");
    if (!cont) return;
    const dots = cont.querySelectorAll(".led-dot");
    if (!dots.length) return;
    const w = cont.clientWidth;
    const spacing = 2;
    const maxSize = Math.floor((w - spacing * (dots.length - 1)) / dots.length);
    const size = Math.max(4, Math.min(14, maxSize));
    dots.forEach((d) => {
      d.style.width = size + "px";
      d.style.height = size + "px";
      d.style.marginLeft = spacing / 2 + "px";
      d.style.marginRight = spacing / 2 + "px";
    });
  }

  function initLedPreview() {
    const cont = $("ledPreview");
    if (!cont) return;
    const count = parseInt(cont.dataset.ledCount || "0");
    cont.innerHTML = "";
    for (let i = 0; i < count; i++) {
      const d = document.createElement("div");
      d.className = "led-dot";
      cont.appendChild(d);
    }
    layoutLedDots();
    updateLedPreview();
    window.addEventListener("resize", layoutLedDots);
  }

  function renderLedBarFraction(fraction, useBlink) {
    const cont = $("ledPreview");
    if (!cont) return;
    const dots = cont.querySelectorAll(".led-dot");
    const count = dots.length;
    if (!count) return;
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;

    const modeVal = $("modeSelect").value;
    const mode = parseInt(modeVal || "0");
    const thr = getThresholds();
    const greenEnd = thr.greenEnd;
    const yellowEnd = thr.yellowEnd;
    const blinkStart = thr.blinkStart;

    let ledsOn = Math.round(fraction * count);
    if (ledsOn < 0) ledsOn = 0;
    if (ledsOn > count) ledsOn = count;

    let shiftBlink = false;
    if (useBlink && (mode === 1 || mode === 2) && fraction >= blinkStart) {
      const now = Date.now();
      if (now - lastLedBlinkTs > 100) {
        lastLedBlinkTs = now;
        ledBlinkState = !ledBlinkState;
      }
      shiftBlink = true;
    } else {
      ledBlinkState = false;
    }

    const mode2FullBlink = useBlink && mode === 2 && fraction >= blinkStart;

    const gCol = $("greenColorInput").value;
    const yCol = $("yellowColorInput").value;
    const rCol = $("redColorInput").value;

    for (let i = 0; i < count; i++) {
      let col = "#000000";
      if (i < ledsOn) {
        let pos = count > 1 ? i / (count - 1) : 0;
        if (mode2FullBlink) {
          col = ledBlinkState ? rCol : "#000000";
        } else {
          if (pos < greenEnd) {
            col = gCol;
          } else if (pos < yellowEnd) {
            col = yCol;
          } else {
            if (useBlink && mode === 1 && shiftBlink) {
              col = ledBlinkState ? rCol : "#000000";
            } else {
              col = rCol;
            }
          }
        }
      }
      dots[i].style.backgroundColor = col;
    }

    const blinkContainer = $("blinkStartContainer");
    if (mode === 0) {
      blinkContainer.style.display = "none";
    } else {
      blinkContainer.style.display = "block";
    }
  }

  function updateLedPreview() {
    if (testSweepActive) {
      const elapsed = Date.now() - testSweepStart;
      let t = elapsed / CONFIG.testSweepDuration;
      if (t >= 1) {
        t = 1;
        testSweepActive = false;
      }
      const frac = computeSimFraction(t);
      renderLedBarFraction(frac, true);
    } else if (blinkPreviewActive) {
      const now = Date.now();
      if (now >= blinkPreviewEnd) {
        blinkPreviewActive = false;
        renderLedBarFraction(1.0, false);
      } else {
        renderLedBarFraction(1.0, true);
      }
    } else {
      renderLedBarFraction(1.0, false);
    }
  }

  function ensurePreviewTimer() {
    if (!previewTimerId) {
      previewTimerId = setInterval(updateLedPreview, 30);
    }
  }

  function handleSliderChange(ev) {
    enforceSliderOrder(ev.target.id);
    updateSliderDisplay(ev.target);
    if (ev.target.id === "blinkStartSlider") {
      triggerBlinkPreview();
    } else {
      updateLedPreview();
    }
    markDirty();
  }

  function triggerBlinkPreview() {
    const modeVal = $("modeSelect").value;
    const mode = parseInt(modeVal || "0");
    if (mode === 0) return;
    blinkPreviewActive = true;
    blinkPreviewEnd = Date.now() + 2500;
    testSweepActive = false;
    ensurePreviewTimer();
    updateLedPreview();
  }

  // ============================================
  // Color UI
  // ============================================
  function classifyColor(slot, value) {
    if (slot === 1) return "Farbe 1 – Grün";
    if (slot === 2) return "Farbe 2 – Gelb";
    return "Farbe 3 – Rot";
  }

  function updateColorUi() {
    const cfg = [
      {
        k: "green",
        slot: 1,
        labelId: "greenEndLabel",
        hiddenId: "greenLabelHidden",
        nameId: "color1Name",
      },
      {
        k: "yellow",
        slot: 2,
        labelId: "yellowEndLabel",
        hiddenId: "yellowLabelHidden",
        nameId: "color2Name",
      },
      {
        k: "red",
        slot: 3,
        labelId: "blinkStartLabel",
        hiddenId: "redLabelHidden",
        nameId: "color3Name",
      },
    ];
    cfg.forEach((c) => {
      const inp = $(c.k + "ColorInput");
      if (!inp) return;
      const name = classifyColor(c.slot, inp.value);
      const label = name.split("–")[1].trim();
      const span = $(c.nameId);
      if (span) span.innerText = label;
      const hid = $(c.hiddenId);
      if (hid) hid.value = label;
      const lbl = $(c.labelId);
      if (lbl) lbl.style.color = inp.value;
    });
    updateLedPreview();
  }

  // ============================================
  // Dev Mode Sections
  // ============================================
  function updateDevSections(enabled) {
    devModeEnabled = enabled;
    const sections = ["devObdSection", "displayStatusBlock", "debugSection"];
    sections.forEach((id) => {
      const el = $(id);
      if (!el) return;
      if (enabled) {
        el.classList.remove("dev-collapsed");
        el.classList.add("dev-expanded");
      } else {
        el.classList.remove("dev-expanded");
        el.classList.add("dev-collapsed");
      }
    });
  }

  // ============================================
  // Vehicle Info Update
  // ============================================
  function updateVehicleDots(loading) {
    ["vehicleModel", "vehicleVin", "vehicleDiag"].forEach((id) => {
      setAnimatedDots($(id), loading);
    });
  }

  // ============================================
  // Display Status (Dev Mode)
  // ============================================
  function boolText(v) {
    return v ? "ja" : "nein";
  }

  function updateDisplayStatusUi(s) {
    const map = [
      ["dispInit", boolText(s.initAttempted)],
      ["dispReady", boolText(s.ready)],
      ["dispPanel", boolText(s.panelInitialized)],
      ["dispBuf", boolText(s.buffersAllocated)],
      ["dispTouch", boolText(s.touchReady)],
      ["dispTickMode", s.tickFallback ? "loop" : "timer"],
      ["dispDebugUi", boolText(s.debugSimpleUi)],
      ["dispLvgl", s.lastLvglRunMs !== undefined ? s.lastLvglRunMs : "-"],
      ["dispError", s.lastError || "-"],
    ];
    map.forEach(([id, val]) => {
      const el = $(id);
      if (el) el.innerText = val;
    });
  }

  function toggleDisplaySpinner(active) {
    const sp = $("displaySpinner");
    if (!sp) return;
    if (active) {
      sp.classList.remove("hidden");
    } else {
      sp.classList.add("hidden");
    }
  }

  function fetchDisplayStatus() {
    toggleDisplaySpinner(true);
    const done = beginRequest();
    fetch("/dev/display-status")
      .then((r) => r.json())
      .then((s) => {
        updateDisplayStatusUi(s);
      })
      .catch(() => {
        updateDisplayStatusUi({
          initAttempted: false,
          ready: false,
          lastError: "Request fehlgeschlagen",
        });
      })
      .finally(() => {
        toggleDisplaySpinner(false);
        done();
      });
  }

  function triggerDisplayPattern(pattern) {
    toggleDisplaySpinner(true);
    const done = beginRequest();
    const body = "pattern=" + encodeURIComponent(pattern || "bars");
    fetch("/dev/display-pattern", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    }).finally(() => {
      toggleDisplaySpinner(false);
      done();
      fetchDisplayStatus();
    });
  }

  // ============================================
  // Status Polling
  // ============================================
  function fetchStatus() {
    const done = beginRequest();
    fetch("/status")
      .then((r) => r.json())
      .then((s) => {
        const rpm = $("rpmVal");
        if (rpm && s.rpm !== undefined) rpm.innerText = s.rpm;

        const rpmMax = $("rpmMaxVal");
        if (rpmMax && s.maxRpm !== undefined) rpmMax.innerText = s.maxRpm;

        const lastTx = $("lastTx");
        if (lastTx && s.lastTx !== undefined) {
          lastTx.dataset.base = s.lastTx;
          lastTx.innerText = s.lastTx;
        }

        const lastObd = $("lastObd");
        if (lastObd && s.lastObd !== undefined) {
          lastObd.dataset.base = s.lastObd;
          lastObd.innerText = s.lastObd;
        }

        const ble = $("bleStatus");
        if (ble && s.bleText) ble.innerText = s.bleText;

        const vm = $("vehicleModel");
        if (vm && s.vehicleModel !== undefined) {
          vm.dataset.base = s.vehicleModel;
          vm.innerText = s.vehicleModel;
        }

        const vv = $("vehicleVin");
        if (vv && s.vehicleVin !== undefined) {
          vv.dataset.base = s.vehicleVin;
          vv.innerText = s.vehicleVin;
        }

        const vd = $("vehicleDiag");
        if (vd && s.vehicleDiag !== undefined) {
          vd.dataset.base = s.vehicleDiag;
          vd.innerText = s.vehicleDiag;
        }

        updateVehicleDots(s.vehicleInfoRequestRunning);

        const btnC = $("btnConnect");
        const btnD = $("btnDisconnect");
        if (btnC && btnD) {
          if (s.connected) {
            btnC.style.display = "none";
            btnD.style.display = "block";
          } else {
            btnC.style.display = "block";
            btnD.style.display = "none";
          }
        }

        // Update dev mode
        if (s.devMode !== undefined) {
          updateDevSections(s.devMode);
        }
      })
      .catch(() => {})
      .finally(done);
  }

  // ============================================
  // Save & Test Actions
  // ============================================
  function onSaveClicked() {
    const done = beginRequest();
    const fd = new FormData($("mainForm"));
    fetch("/save", { method: "POST", body: fd })
      .then(() => {
        captureInitialMainState();
        recomputeMainDirty();
      })
      .finally(done);
  }

  function onTestClicked() {
    const done = beginRequest();
    const fd = new FormData($("mainForm"));
    fetch("/test", { method: "POST", body: fd }).finally(done);
    testSweepActive = true;
    testSweepStart = Date.now();
    blinkPreviewActive = false;
    ensurePreviewTimer();
    updateLedPreview();
  }

  // ============================================
  // Load Initial Config
  // ============================================
  function loadConfig() {
    fetch("/api/config")
      .then((r) => r.json())
      .then((c) => {
        CONFIG.testSweepDuration = c.testSweepDuration || 5000;
        CONFIG.numLeds = c.numLeds || 30;

        // Update LED preview count
        const preview = $("ledPreview");
        if (preview) {
          preview.dataset.ledCount = CONFIG.numLeds;
          initLedPreview();
        }

        // Set form values
        if ($("modeSelect")) $("modeSelect").value = c.mode;
        if ($("brightness_slider")) {
          $("brightness_slider").value = c.brightness;
          $("brightness").value = c.brightness;
          $("bval").innerText = c.brightness;
        }
        if ($("autoscaleToggle"))
          $("autoscaleToggle").checked = c.autoScaleMaxRpm;
        if ($("fixedMaxRpmInput")) $("fixedMaxRpmInput").value = c.fixedMaxRpm;
        if ($("greenEndSlider")) $("greenEndSlider").value = c.greenEndPct;
        if ($("yellowEndSlider")) $("yellowEndSlider").value = c.yellowEndPct;
        if ($("blinkStartSlider"))
          $("blinkStartSlider").value = c.blinkStartPct;
        if ($("greenColorInput")) $("greenColorInput").value = c.greenColor;
        if ($("yellowColorInput")) $("yellowColorInput").value = c.yellowColor;
        if ($("redColorInput")) $("redColorInput").value = c.redColor;
        if ($("greenLabelHidden")) $("greenLabelHidden").value = c.greenLabel;
        if ($("yellowLabelHidden"))
          $("yellowLabelHidden").value = c.yellowLabel;
        if ($("redLabelHidden")) $("redLabelHidden").value = c.redLabel;
        if ($("logoIgnOn")) $("logoIgnOn").checked = c.logoOnIgnitionOn;
        if ($("logoEngStart")) $("logoEngStart").checked = c.logoOnEngineStart;
        if ($("logoIgnOff")) $("logoIgnOff").checked = c.logoOnIgnitionOff;
        if ($("autoReconnect")) $("autoReconnect").checked = c.autoReconnect;

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
        if ($("currentIp")) {
          // IP will be updated from WiFi status
        }

        // Debug info
        if ($("lastTx")) {
          $("lastTx").dataset.base = c.lastTx || "-";
          $("lastTx").innerText = c.lastTx || "-";
        }
        if ($("lastObd")) {
          $("lastObd").dataset.base = c.lastObd || "-";
          $("lastObd").innerText = c.lastObd || "-";
        }
        if ($("rpmVal")) $("rpmVal").innerText = c.currentRpm || "0";
        if ($("rpmMaxVal")) $("rpmMaxVal").innerText = c.maxSeenRpm || "0";

        // Dev mode
        updateDevSections(c.devMode);

        // Update UI
        updateAutoscaleUi();
        updateColorUi();
        ["greenEndSlider", "yellowEndSlider", "blinkStartSlider"].forEach(
          (id) => {
            const el = $(id);
            if (el) updateSliderDisplay(el);
          }
        );

        // Capture initial state after loading
        setTimeout(() => {
          captureInitialMainState();
          recomputeMainDirty();
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
    const form = $("mainForm");
    if (form) {
      form.querySelectorAll("input,select").forEach((el) => {
        if (el.id === "brightness_slider") return;
        if (el.type === "range") {
          el.addEventListener("input", handleSliderChange);
          el.addEventListener("change", handleSliderChange);
        } else {
          el.addEventListener("change", markDirty);
          el.addEventListener("input", markDirty);
        }
      });
    }

    // Autoscale toggle
    const auto = $("autoscaleToggle");
    if (auto) {
      auto.addEventListener("change", () => {
        markDirty();
        updateAutoscaleUi();
      });
    }
    updateAutoscaleUi();

    // Color inputs
    ["green", "yellow", "red"].forEach((n) => {
      const c = $(n + "ColorInput");
      if (c) {
        c.addEventListener("input", () => {
          updateColorUi();
          markDirty();
        });
        c.addEventListener("change", () => {
          updateColorUi();
          markDirty();
        });
      }
    });

    // Sliders
    ["greenEndSlider", "yellowEndSlider", "blinkStartSlider"].forEach((id) => {
      const el = $(id);
      if (el) {
        updateSliderDisplay(el);
        el.addEventListener("input", handleSliderChange);
        el.addEventListener("change", handleSliderChange);
      }
    });

    // Brightness slider
    const b = $("brightness_slider");
    if (b) {
      b.addEventListener("input", (e) => onBrightnessChange(e.target.value));
      b.addEventListener("change", (e) => onBrightnessChange(e.target.value));
    }

    // Mode select
    const ms = $("modeSelect");
    if (ms) {
      ms.addEventListener("change", () => {
        markDirty();
        updateLedPreview();
      });
    }

    // Buttons
    const sb = $("btnSave");
    if (sb) sb.addEventListener("click", onSaveClicked);

    const tb = $("btnTest");
    if (tb) tb.addEventListener("click", onTestClicked);

    const bc = $("btnConnect");
    if (bc) bc.addEventListener("click", () => postSimple("/connect"));

    const bd = $("btnDisconnect");
    if (bd) bd.addEventListener("click", () => postSimple("/disconnect"));

    const br = $("btnReset");
    if (br) br.addEventListener("click", () => window.location.reload());

    // Display buttons (dev mode)
    const bdsp = $("btnDisplayLogo");
    if (bdsp) {
      bdsp.addEventListener("click", () => {
        toggleDisplaySpinner(true);
        const done = beginRequest();
        fetch("/dev/display-logo", { method: "POST" }).finally(() => {
          toggleDisplaySpinner(false);
          done();
          fetchDisplayStatus();
        });
      });
    }

    const bds = $("btnDisplayStatus");
    if (bds) bds.addEventListener("click", fetchDisplayStatus);

    const bdb = $("btnDisplayBars");
    if (bdb) bdb.addEventListener("click", () => triggerDisplayPattern("bars"));

    const bdg = $("btnDisplayGrid");
    if (bdg) bdg.addEventListener("click", () => triggerDisplayPattern("grid"));

    // Load config and initialize
    loadConfig();

    // Start polling
    fetchStatus();
    statusTimer = setInterval(fetchStatus, 2200);
    setInterval(updateSpinnerVisibility, 1000);

    // Fetch display status if block exists
    if ($("displayStatusBlock")) {
      setTimeout(fetchDisplayStatus, 500);
    }
  }

  // ============================================
  // Entry Point
  // ============================================
  document.addEventListener("DOMContentLoaded", initUI);
})();

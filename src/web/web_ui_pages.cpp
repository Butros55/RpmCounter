#include "web_ui_pages.h"

#include <Arduino.h>

#include "core/config.h"
#include "core/state.h"
#include "core/vehicle_info.h"
#include "core/wifi.h"
#include "hardware/ambient_light.h"
#include "hardware/display.h"
#include "hardware/gesture_sensor.h"
#include "web_helpers.h"

namespace
{
    String checkedAttr(bool value)
    {
        return value ? String(F(" checked")) : String();
    }

    String selectedAttr(bool value)
    {
        return value ? String(F(" selected")) : String();
    }

    String activeTelemetryLabel()
    {
        switch (g_activeTelemetrySource)
        {
        case ActiveTelemetrySource::Obd:
            return F("OBD");
        case ActiveTelemetrySource::SimHubNetwork:
            return F("SimHub");
        case ActiveTelemetrySource::UsbSim:
            return F("USB Sim");
        case ActiveTelemetrySource::None:
        default:
            return F("Keine");
        }
    }

    String simHubStateLabel()
    {
        switch (g_simHubConnectionState)
        {
        case SimHubConnectionState::WaitingForHost:
            return F("Host fehlt");
        case SimHubConnectionState::WaitingForNetwork:
            return F("WLAN fehlt");
        case SimHubConnectionState::WaitingForData:
            return F("Warte auf Daten");
        case SimHubConnectionState::Live:
            return F("Live");
        case SimHubConnectionState::Error:
            return F("Fehler");
        case SimHubConnectionState::Disabled:
        default:
            return F("Deaktiviert");
        }
    }

    String currentIpString()
    {
        const WifiStatus wifi = getWifiStatus();
        if (!wifi.ip.isEmpty())
        {
            return wifi.ip;
        }
        if (!wifi.staIp.isEmpty())
        {
            return wifi.staIp;
        }
        if (!wifi.apIp.isEmpty())
        {
            return wifi.apIp;
        }
        return F("Nicht verbunden");
    }

    String staSummary(const WifiStatus &wifi)
    {
        if (wifi.staConnected)
        {
            return wifi.currentSsid.isEmpty() ? String(F("Verbunden")) : wifi.currentSsid;
        }
        if (wifi.staConnecting)
        {
            return F("Verbindung laeuft");
        }
        if (!wifi.staLastError.isEmpty())
        {
            return wifi.staLastError;
        }
        return F("Offline");
    }

    String bleSummary()
    {
        if (g_bleConnectInProgress)
        {
            return F("Verbindet");
        }
        if (g_connected)
        {
            return g_currentTargetName.isEmpty() ? String(F("Verbunden")) : g_currentTargetName;
        }
        return F("Nicht verbunden");
    }

    String displaySummary(const DisplayDebugInfo &info)
    {
        if (info.ready)
        {
            return F("Bereit");
        }
        if (!info.lastError.isEmpty())
        {
            return info.lastError;
        }
        if (info.initAttempted)
        {
            return F("Init laeuft");
        }
        return F("Unbekannt");
    }

    constexpr unsigned long AMBIENT_UI_FRESH_MS = 2500UL;
    constexpr unsigned long GESTURE_UI_FRESH_MS = 4000UL;

    bool statusAgeFresh(unsigned long nowMs, unsigned long eventMs, unsigned long freshnessMs)
    {
        return eventMs > 0 && (nowMs < eventMs || (nowMs - eventMs) <= freshnessMs);
    }

    bool ambientUiFresh(const AmbientLightDebugInfo &info, unsigned long nowMs)
    {
        return info.readCount > 0 && statusAgeFresh(nowMs, info.lastReadMs, AMBIENT_UI_FRESH_MS);
    }

    bool ambientUiOnline(const AmbientLightDebugInfo &info, unsigned long nowMs)
    {
        return info.sensorDetected || info.deviceResponding || ambientUiFresh(info, nowMs);
    }

    String ambientUiSensorText(const AmbientLightDebugInfo &info, unsigned long nowMs)
    {
        if (ambientUiOnline(info, nowMs))
        {
            return F("VEML7700 online");
        }
        if (info.busInitialized)
        {
            return F("Kein Sensor an 0x10");
        }
        return F("Bus nicht bereit");
    }

    String ambientUiLuxText(const AmbientLightDebugInfo &info, unsigned long nowMs)
    {
        if (info.readCount > 0 || ambientUiOnline(info, nowMs))
        {
            return String(info.filteredLux, 1) + F(" lx");
        }
        return F("-");
    }

    String ambientUiStatusText(const AmbientLightDebugInfo &info, unsigned long nowMs)
    {
        if (ambientUiOnline(info, nowMs))
        {
            return ambientUiFresh(info, nowMs) ? String(F("Sensor liefert Daten."))
                                               : String(F("Sensor erkannt, letzter Messwert bleibt sichtbar."));
        }
        if (!info.lastError.isEmpty())
        {
            return info.lastError;
        }
        return F("Noch keine Antwort vom Sensor.");
    }

    const char *ambientUiPillTone(const AmbientLightDebugInfo &info, bool autoEnabled, unsigned long nowMs)
    {
        if (autoEnabled && ambientUiFresh(info, nowMs))
        {
            return "ok";
        }
        if (ambientUiOnline(info, nowMs))
        {
            return autoEnabled ? "warn" : "neutral";
        }
        return autoEnabled ? "bad" : "neutral";
    }

    String ambientUiPillText(const AmbientLightDebugInfo &info, bool autoEnabled, unsigned long nowMs)
    {
        if (autoEnabled && ambientUiFresh(info, nowMs))
        {
            return F("Auto-Helligkeit live");
        }
        if (ambientUiOnline(info, nowMs))
        {
            return F("Sensor bereit");
        }
        return autoEnabled ? String(F("Sensor fehlt")) : String(F("Auto aus"));
    }

    bool gestureUiFresh(const GestureSensorDebugInfo &info, unsigned long nowMs)
    {
        return statusAgeFresh(nowMs, info.lastReadMs, GESTURE_UI_FRESH_MS) ||
               statusAgeFresh(nowMs, info.lastProbeMs, GESTURE_UI_FRESH_MS);
    }

    bool gestureUiOnline(const GestureSensorDebugInfo &info, unsigned long nowMs)
    {
        return info.sensorDetected || info.deviceResponding || info.idReadOk ||
               (info.initSuccessCount > 0 && gestureUiFresh(info, nowMs));
    }

    String upperHexByte(uint8_t value)
    {
        String hex = String(value, HEX);
        hex.toUpperCase();
        if (hex.length() < 2)
        {
            hex = String(F("0")) + hex;
        }
        return hex;
    }

    String gestureUiSensorText(const GestureSensorDebugInfo &info, bool enabled, unsigned long nowMs)
    {
        if (!enabled)
        {
            return F("Gesten aus");
        }
        if (gestureUiOnline(info, nowMs))
        {
            String text = F("APDS-9960 online");
            if (info.deviceId > 0)
            {
                text += F(" (0x");
                text += upperHexByte(info.deviceId);
                text += ')';
            }
            return text;
        }
        if (info.ackResponding)
        {
            return F("ACK ok, Init fehlgeschlagen");
        }
        if (info.busInitialized)
        {
            return F("Kein ACK an 0x39");
        }
        return F("Bus nicht bereit");
    }

    String gestureUiStatusText(const GestureSensorDebugInfo &info, bool enabled, unsigned long nowMs)
    {
        if (!enabled)
        {
            return F("Gestensteuerung deaktiviert.");
        }
        if (gestureUiOnline(info, nowMs))
        {
            return F("Sensor bereit.");
        }
        if (info.ackResponding)
        {
            return F("ACK vorhanden, aber Init noch nicht sauber.");
        }
        if (!info.lastError.isEmpty())
        {
            return info.lastError;
        }
        return F("Noch keine Antwort vom Sensor.");
    }

    String wifiModeLabel(WifiMode mode)
    {
        switch (mode)
        {
        case STA_ONLY:
            return F("Nur Heim-WLAN");
        case STA_WITH_AP_FALLBACK:
            return F("WLAN + AP Fallback");
        case AP_ONLY:
        default:
            return F("Nur Access Point");
        }
    }

    void appendShellHead(String &page, const __FlashStringHelper *title, bool settingsActive)
    {
        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>");
        page += F("<meta name='theme-color' content='#08111b'>");
        page += F("<title>");
        page += title;
        page += F("</title><style>");
        page += F(R"CSS(
:root{--bg:#08111b;--panel:#101a27;--panel-2:#121f2f;--text:#eef5ff;--muted:#8da2ba;--accent:#4bb7ff;--success:#40d39c;--warn:#ffb84d;--danger:#ff6f81;--border:#24364a;--border-strong:#33506d;--radius:18px;--radius-sm:12px;--savebar-space:150px}
*{box-sizing:border-box}html{scroll-behavior:smooth}
body{margin:0;min-height:100vh;font:500 15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;background:radial-gradient(circle at top right,rgba(56,118,255,.18),transparent 28%),radial-gradient(circle at top left,rgba(43,208,255,.10),transparent 24%),linear-gradient(180deg,#060c14 0%,#0a121d 100%);color:var(--text)}
a{color:inherit;text-decoration:none}button,input,select,textarea{font:inherit}
.app{width:min(1180px,100%);margin:0 auto;padding:calc(18px + env(safe-area-inset-top)) 16px calc(var(--savebar-space) + env(safe-area-inset-bottom))}
.topbar{display:flex;flex-direction:column;gap:14px;margin-bottom:18px}.topbar-row{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;flex-wrap:wrap}
.brand{display:flex;flex-direction:column;gap:4px}.eyebrow{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.brand h1{margin:0;font-size:clamp(28px,8vw,42px);line-height:1;letter-spacing:-.04em}.brand p{margin:0;color:var(--muted);font-size:14px}
.tabs{display:inline-flex;gap:6px;padding:6px;border-radius:999px;background:rgba(12,21,32,.86);border:1px solid var(--border)}
.tab{min-height:42px;padding:10px 16px;border-radius:999px;color:var(--muted);font-weight:700}.tab.active{background:linear-gradient(180deg,#18293d,#142435);color:var(--text);box-shadow:inset 0 0 0 1px rgba(99,165,255,.18)}
.hero{display:grid;gap:14px;margin-bottom:18px}.hero-card{border:1px solid var(--border);background:linear-gradient(180deg,rgba(16,26,39,.95),rgba(12,20,31,.95));border-radius:22px;padding:18px}
.hero-card--accent{background:linear-gradient(180deg,rgba(20,31,48,.98),rgba(14,23,35,.98)),linear-gradient(135deg,rgba(75,183,255,.18),transparent 46%)}
.hero-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.hero-kicker{font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.hero-title{font-size:20px;font-weight:800;line-height:1.1}.hero-sub{margin-top:6px;color:var(--muted);font-size:13px}
.metric-grid{display:grid;gap:10px;grid-template-columns:repeat(2,minmax(0,1fr))}.metric{padding:12px;border-radius:16px;background:rgba(8,14,22,.56);border:1px solid rgba(255,255,255,.05)}
.metric-label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}.metric-value{margin-top:6px;font-size:clamp(22px,7vw,34px);line-height:1;font-weight:800;letter-spacing:-.04em}.metric-value--compact{font-size:24px}.metric-note{margin-top:4px;color:var(--muted);font-size:12px}
.status-list,.status-inline{display:flex;gap:8px;flex-wrap:wrap}.pill{display:inline-flex;align-items:center;gap:8px;min-height:34px;padding:8px 12px;border-radius:999px;border:1px solid var(--border);background:rgba(8,14,22,.55);color:var(--text);font-size:12px;font-weight:700}
.pill::before{content:"";width:8px;height:8px;border-radius:50%;background:#64768b}.pill.ok::before{background:var(--success)}.pill.warn::before{background:var(--warn)}.pill.bad::before{background:var(--danger)}.pill.neutral::before{background:var(--accent)}
.app-grid,.panel-grid,.field-grid,.stack,.info-list,.dashboard-layout,.dashboard-col{display:grid;gap:16px}.dashboard-col{align-content:start}.panel{border:1px solid var(--border);background:linear-gradient(180deg,rgba(16,26,39,.95),rgba(12,20,31,.95));border-radius:var(--radius);padding:18px}
.panel-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.panel-title{margin:0;font-size:18px;line-height:1.1;letter-spacing:-.02em}.panel-copy{margin-top:4px;color:var(--muted);font-size:13px}
.field-grid.two{grid-template-columns:repeat(1,minmax(0,1fr))}.field label,.field-label{display:block;margin-bottom:8px;font-size:12px;font-weight:700;color:var(--muted);letter-spacing:.06em;text-transform:uppercase}
.field input[type=text],.field input[type=number],.field input[type=password],.field select,.field textarea{width:100%;min-height:48px;padding:12px 14px;border-radius:14px;border:1px solid var(--border);background:#0b121b;color:var(--text);outline:none}
.field input:focus,.field select:focus,.field textarea:focus{border-color:rgba(75,183,255,.72);box-shadow:0 0 0 1px rgba(75,183,255,.2)}.field-note,.seg-note{margin-top:8px;color:var(--muted);font-size:12px}
.field-inline,.range-wrap,.color-card,.info-row,.badge-card,.device-item{padding:14px;border-radius:16px;border:1px solid var(--border);background:#0b121b}
.field-inline,.info-row,.device-item-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.field-inline strong,.device-name{display:block;font-weight:800}.field-inline span,.info-label,.device-meta,.badge-card span{display:block;color:var(--muted);font-size:13px}
.switch{position:relative;width:56px;height:32px;display:inline-block;flex:0 0 auto}.switch input{opacity:0;width:0;height:0}.slider{position:absolute;inset:0;border-radius:999px;background:#223243;border:1px solid rgba(255,255,255,.06);transition:.18s ease}.slider::before{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#f5f8fb;transition:.18s ease}.switch input:checked + .slider{background:linear-gradient(180deg,#329bff,#2bd0ff)}.switch input:checked + .slider::before{transform:translateX(24px)}
.range-wrap{display:grid;gap:10px}.range-head{display:flex;align-items:baseline;justify-content:space-between;gap:12px}.range-title{font-weight:700}.range-value{font-size:12px;color:var(--muted)}input[type=range]{width:100%;margin:0;accent-color:var(--accent);background:transparent}
.color-grid{display:grid;gap:12px;grid-template-columns:repeat(1,minmax(0,1fr))}.color-card input[type=color]{width:100%;min-height:52px;border:none;padding:0;border-radius:12px;background:transparent}.color-card input[type=color]::-webkit-color-swatch-wrapper{padding:0}.color-card input[type=color]::-webkit-color-swatch{border:none;border-radius:12px}.color-card input[type=color]::-moz-color-swatch{border:none;border-radius:12px}
.led-preview{display:grid;grid-template-columns:repeat(15,minmax(0,1fr));gap:6px;padding:14px;border-radius:16px;border:1px solid var(--border);background:#0b121b}.led-dot{width:100%;aspect-ratio:1/1;border-radius:999px;background:#26384d}
.button-row{display:flex;flex-wrap:wrap;gap:10px}.btn{appearance:none;border:none;min-height:48px;padding:12px 16px;border-radius:14px;font-weight:800;letter-spacing:-.01em;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:8px}
.btn:disabled{opacity:.55;cursor:not-allowed}.btn-primary{background:linear-gradient(180deg,#46b0ff,#2bd0ff);color:#06111c}.btn-secondary{background:#1b2938;color:var(--text);border:1px solid var(--border)}.btn-danger{background:#26141a;color:#ffd7de;border:1px solid rgba(255,111,129,.28)}.btn-ghost{background:transparent;color:var(--text);border:1px solid var(--border)}
.badge-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.badge-card strong{display:block;margin-top:6px;font-size:16px}.info-value{text-align:right;font-weight:700;word-break:break-word}.device-list{display:grid;gap:10px;margin-top:12px}.device-empty{margin-top:12px;padding:14px;border-radius:14px;border:1px dashed var(--border);color:var(--muted);text-align:center;font-size:13px}
.callout,.toast{padding:14px;border-radius:16px;font-size:13px}.callout{border:1px solid rgba(75,183,255,.28);background:rgba(37,74,110,.14);color:#bfd9ff}.toast{margin-top:12px;border:1px solid rgba(64,211,156,.28);background:rgba(16,59,48,.36);color:#d9fff0}
.savebar{position:fixed;left:12px;right:12px;bottom:calc(12px + env(safe-area-inset-bottom));z-index:40;display:flex;flex-direction:column;gap:12px;padding:14px;border-radius:18px;border:1px solid var(--border-strong);background:rgba(7,12,18,.96)}
.savebar-title{font-size:14px;font-weight:800}.savebar-copy{margin-top:4px;color:var(--muted);font-size:12px}.savebar-actions{display:grid;grid-template-columns:1fr 1fr;gap:10px}.savebar-title span{display:none}.savebar[data-dirty='0'] .savebar-title::before{content:"Gespeichert"}.savebar[data-dirty='1'] .savebar-title::before{content:"Ungespeicherte Aenderungen"}
.details summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between;gap:12px}.details summary::-webkit-details-marker{display:none}.details summary::after{content:"+";font-size:18px;color:var(--muted)}.details[open] summary::after{content:"-"}
.console{min-height:180px;max-height:280px;overflow:auto;padding:12px;border-radius:16px;border:1px solid var(--border);background:#071019;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}.console-line{margin:0 0 4px}.console-line.tx{color:#8fd0ff}.console-line.rx{color:#aef5bf}.console-line.err{color:#ff9cab}
.spinner{display:inline-block;width:14px;height:14px;border-radius:50%;border:2px solid rgba(255,255,255,.18);border-top-color:currentColor;animation:spin 1s linear infinite}@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}
.hidden{display:none!important}
@media(min-width:720px){.hero{grid-template-columns:1.3fr .9fr}.panel-grid.two,.field-grid.two{grid-template-columns:repeat(2,minmax(0,1fr))}.color-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.info-list.compact{grid-template-columns:repeat(2,minmax(0,1fr))}.savebar{left:50%;right:auto;width:min(820px,calc(100% - 24px));transform:translateX(-50%);flex-direction:row;align-items:center;justify-content:space-between}.savebar-actions{width:min(340px,100%);flex:0 0 auto}}
@media(min-width:980px){.app-grid{grid-template-columns:1.05fr .95fr;align-items:start}.dashboard-layout{grid-template-columns:1.05fr .95fr;align-items:start}.hero-card{padding:22px}}
)CSS");
        page += F("</style></head><body><div class='app'><header class='topbar'><div class='topbar-row'><div class='brand'><span class='eyebrow'>RPMCounter / ShiftLight</span><h1>ShiftLight</h1><p>Leichtgewichtige Embedded-Weboberflaeche mit Live-Status, Telemetrie und Konfiguration.</p></div><nav class='tabs'>");
        page += settingsActive ? F("<a class='tab' href='/'>Dashboard</a><a class='tab active' href='/settings'>Verbindung</a>")
                               : F("<a class='tab active' href='/'>Dashboard</a><a class='tab' href='/settings'>Verbindung</a>");
        page += F("</nav></div></header>");
    }

    void appendShellFooter(String &page)
    {
        page += F("</div></body></html>");
    }

    void appendDashboardScript(String &page)
    {
        page += F("<script>");
        page += "const NUM_LEDS=" + String(NUM_LEDS) + ";";
        page += "const TEST_SWEEP_DURATION=" + String(TEST_SWEEP_DURATION) + ";";
        page += F(R"JS(
const $ = (selector, scope=document) => scope.querySelector(selector);
const $$ = (selector, scope=document) => Array.from(scope.querySelectorAll(selector));
const dashboardState = { dirty:false, pending:0, initial:null, previewTimer:null, testSweepActive:false, testSweepMode:'show', testSweepStart:0, blinkPreview:false, blinkPreviewUntil:0 };
function setLoading(active){ dashboardState.pending = Math.max(0, dashboardState.pending + (active ? 1 : -1)); }
function beginRequest(){ setLoading(true); return () => setLoading(false); }
function serializeForm(form){ const out = {}; $$('input,select,textarea', form).forEach((el) => { if(!el.name){ return; } out[el.name] = el.type === 'checkbox' ? (el.checked ? 'on' : '') : el.value; }); return out; }
function captureInitialState(){ const form = $('#mainForm'); if(form){ dashboardState.initial = serializeForm(form); } }
function recomputeDirty(){ const form = $('#mainForm'); if(!form){ return; } if(!dashboardState.initial){ captureInitialState(); } const current = serializeForm(form); let dirty = false; Object.keys(dashboardState.initial).forEach((key) => { if(dashboardState.initial[key] !== current[key]){ dirty = true; } }); Object.keys(current).forEach((key) => { if(dashboardState.initial[key] !== current[key]){ dirty = true; } }); dashboardState.dirty = dirty; const savebar = $('#saveBar'); if(savebar){ savebar.dataset.dirty = dirty ? '1' : '0'; $('.savebar-copy', savebar).textContent = dirty ? 'Bereit zum Speichern. Live-Werte laufen weiter.' : 'Konfiguration ist synchron.'; } const saveBtn = $('#btnSave'); if(saveBtn){ saveBtn.disabled = !dirty; } const resetBtn = $('#btnReset'); if(resetBtn){ resetBtn.disabled = !dirty; } }
function syncSavebarSpace(id){ const bar = document.getElementById(id); if(!bar){ return; } const space = Math.ceil(bar.getBoundingClientRect().height + 28); document.documentElement.style.setProperty('--savebar-space', space + 'px'); }
function markDirty(){ recomputeDirty(); }
function updateText(id, value){ const el = document.getElementById(id); if(el && value !== undefined && value !== null){ el.textContent = String(value); } }
function setPill(id, tone, text){ const el = document.getElementById(id); if(!el){ return; } el.classList.remove('ok','warn','bad','neutral'); el.classList.add(tone || 'neutral'); el.textContent = text; }
function updateSliderValue(el){ const target = document.getElementById(el.dataset.valueTarget); if(target){ target.textContent = el.value + (el.dataset.suffix || ''); } }
function enforceOrder(changedId){ const green = $('#greenEndSlider'); const yellow = $('#yellowEndSlider'); const red = $('#redEndSlider'); const blink = $('#blinkStartSlider'); let g = parseInt(green.value || '0', 10); let y = parseInt(yellow.value || '0', 10); let r = parseInt(red.value || '0', 10); let b = parseInt(blink.value || '0', 10); if(changedId === 'greenEndSlider' && y < g){ y = g; } if((changedId === 'greenEndSlider' || changedId === 'yellowEndSlider') && r < y){ r = y; } if(changedId === 'redEndSlider' && r < y){ r = y; } if(changedId === 'yellowEndSlider' && y < g){ y = g; } b = Math.max(0, Math.min(100, b)); green.value = String(g); yellow.value = String(y); red.value = String(r); blink.value = String(b); [green,yellow,red,blink].forEach(updateSliderValue); }
function syncAutoscaleUi(){ const enabled = $('#autoscaleToggle')?.checked; $('#fixedMaxWrap')?.classList.toggle('hidden', !!enabled); }
function syncAmbientUi(){ const ceiling = parseInt($('#brightnessSlider')?.value || '255', 10); const minInput = $('#autoBrightnessMinSlider'); if(minInput){ minInput.max = String(ceiling); if(parseInt(minInput.value || '0', 10) > ceiling){ minInput.value = String(ceiling); updateSliderValue(minInput); } } }
function currentLedCount(){ const input = $('#activeLedCountSlider'); const raw = parseInt(input?.value || String(NUM_LEDS), 10); if(Number.isNaN(raw)){ return NUM_LEDS; } return Math.max(1, Math.min(NUM_LEDS, raw)); }
function syncLedPreviewCount(){ const preview = $('#ledPreview'); if(!preview){ return []; } const count = currentLedCount(); let dots = $$('.led-dot', preview); if(dots.length !== count){ preview.innerHTML = ''; for(let i=0;i<count;i += 1){ const dot = document.createElement('span'); dot.className = 'led-dot'; preview.appendChild(dot); } dots = $$('.led-dot', preview); } const value = $('#activeLedCountValue'); if(value){ value.textContent = String(count); } return dots; }
function ledFractionAt(t){ if(t < 0.34){ return t / 0.34 * 0.72; } if(t < 0.72){ return 0.72 + ((t - 0.34) / 0.38) * 0.20; } return Math.min(1, 0.92 + ((t - 0.72) / 0.28) * 0.08); }
function diagnosticFractionAt(t){ if(t < 0.45){ const tt = t / 0.45; return tt * tt * (3 - 2 * tt); } if(t < 0.65){ return 1; } const tt = (t - 0.65) / 0.35; return Math.max(0, 1 - (tt * tt * (3 - 2 * tt))); }
function renderLedPreview(fraction, blinking){ const dots = syncLedPreviewCount(); if(!dots.length){ return; } const mode = parseInt($('#modeSelect')?.value || '0', 10); const greenEnd = parseInt($('#greenEndSlider')?.value || '0', 10) / 100; const yellowEnd = parseInt($('#yellowEndSlider')?.value || '0', 10) / 100; const redEnd = parseInt($('#redEndSlider')?.value || '0', 10) / 100; const blinkStart = parseInt($('#blinkStartSlider')?.value || '0', 10) / 100; const safeRedEnd = Math.max(0.01, redEnd); const blinkTrigger = Math.max(0, Math.min(1, blinkStart)); const fillEnd = Math.max(0.001, blinkTrigger * safeRedEnd); const zoneGreen = Math.max(0, Math.min(1, greenEnd / safeRedEnd)); const zoneYellow = Math.max(zoneGreen, Math.min(1, yellowEnd / safeRedEnd)); const startRpm = parseInt($('#rpmStartSlider')?.value || '0', 10); const previewMaxRpm = 8000; const startFraction = Math.min(0.95, Math.max(0, startRpm / previewMaxRpm)); const colors = { green: $('#greenColorInput')?.value || '#2DFF7A', yellow: $('#yellowColorInput')?.value || '#FFC34D', red: $('#redColorInput')?.value || '#FF5A72', pit: '#ff3fd2' }; const blinkOn = blinkPreviewState(); const rawFrac = Math.max(0, Math.min(1, fraction)); const frac = rawFrac <= startFraction ? 0 : Math.max(0, Math.min(1, (rawFrac - startFraction) / Math.max(0.01, 1 - startFraction))); const displayFrac = frac <= 0 ? 0 : Math.max(0, Math.min(1, frac / fillEnd)); const dark = '#26384d'; if(mode === 3){ const pairCount = Math.ceil(dots.length / 2); const pairsOn = Math.round(displayFrac * pairCount); const finalBlink = blinking && frac >= blinkTrigger; dots.forEach((dot, index) => { let color = dark; if(finalBlink){ color = blinkOn ? colors.red : '#091019'; } else { const rank = Math.min(index, dots.length - 1 - index); if(rank < pairsOn){ const pos = pairCount <= 1 ? 1 : rank / (pairCount - 1); color = pos < zoneGreen ? colors.green : (pos < zoneYellow ? colors.yellow : colors.red); } } dot.style.backgroundColor = color; }); if(frac <= 0){ [0,1,dots.length-2,dots.length-1].forEach((idx) => { if(dots[idx]){ dots[idx].style.backgroundColor = dark; } }); } return; } const onCount = Math.round(displayFrac * dots.length); dots.forEach((dot, index) => { let color = dark; if(index < onCount){ const pos = dots.length <= 1 ? 1 : index / (dots.length - 1); if(mode === 2 && frac >= blinkTrigger){ color = blinkOn ? colors.red : '#091019'; } else if(pos < zoneGreen){ color = colors.green; } else if(pos < zoneYellow){ color = colors.yellow; } else if(blinking && mode === 1 && frac >= blinkTrigger){ color = blinkOn ? colors.red : '#091019'; } else { color = colors.red; } } dot.style.backgroundColor = color; }); }
function updatePreviewLoop(){ if(dashboardState.testSweepActive){ const elapsed = Date.now() - dashboardState.testSweepStart; const t = Math.min(1, elapsed / TEST_SWEEP_DURATION); renderLedPreview(dashboardState.testSweepMode === 'diagnostic' ? diagnosticFractionAt(t) : ledFractionAt(t), true); if(t >= 1){ dashboardState.testSweepActive = false; } return; } if(dashboardState.blinkPreview){ renderLedPreview(1, true); if(Date.now() >= dashboardState.blinkPreviewUntil){ dashboardState.blinkPreview = false; } return; } renderLedPreview(1, false); }
function ensurePreviewLoop(){ if(!dashboardState.previewTimer){ dashboardState.previewTimer = setInterval(updatePreviewLoop, 80); } }
function triggerBlinkPreview(){ const mode = parseInt($('#modeSelect')?.value || '0', 10); if(mode === 0){ return; } dashboardState.blinkPreview = true; dashboardState.blinkPreviewUntil = Date.now() + 1800; ensurePreviewLoop(); }
function debounce(fn, wait){ let timer = null; return (...args) => { clearTimeout(timer); timer = setTimeout(() => fn(...args), wait); }; }
const sendBrightness = debounce((value) => { fetch('/brightness?val=' + encodeURIComponent(value)).catch(() => {}); }, 90);
function onBrightnessInput(value){ updateText('brightnessValue', value); const hidden = $('#brightnessHidden'); if(hidden){ hidden.value = value; } sendBrightness(value); markDirty(); }
function setButtonLoading(id, active, label){ const btn = document.getElementById(id); if(!btn){ return; } btn.classList.toggle('loading', !!active); btn.disabled = !!active; if(active){ btn.dataset.label = btn.dataset.label || btn.textContent; btn.innerHTML = '<span class=\"spinner\"></span><span>' + (label || btn.dataset.label) + '</span>'; } else { btn.textContent = btn.dataset.label || btn.textContent; } }
function refreshDisplayStatus(){ const done = beginRequest(); setButtonLoading('btnDisplayStatus', true, 'Lade Status'); fetch('/dev/display-status').then((r) => r.json()).then((data) => { updateText('displayReadyValue', data.ready ? 'Bereit' : 'Nicht bereit'); updateText('displayTouchValue', data.touchReady ? 'Touch aktiv' : 'Touch fehlt'); updateText('displayBuffersValue', data.buffersAllocated ? 'DMA ok' : 'Keine Buffer'); updateText('displayTickValue', data.tickFallback ? 'Loop' : 'Timer'); updateText('displayErrorValue', data.lastError || 'Kein Fehler'); setPill('displayStatusPill', data.ready ? 'ok' : (data.lastError ? 'bad' : 'warn'), data.ready ? 'Display bereit' : 'Display pruefen'); }).catch(() => { updateText('displayErrorValue', 'Request fehlgeschlagen'); setPill('displayStatusPill', 'bad', 'Display Fehler'); }).finally(() => { setButtonLoading('btnDisplayStatus', false); done(); }); }
function probeAmbientSensor(){ const done = beginRequest(); setButtonLoading('btnAmbientProbe', true, 'Pruefe Sensor'); fetch('/dev/ambient-probe', { method:'POST' }).then((r) => r.json()).then(() => fetchStatus()).catch(() => {}).finally(() => { setButtonLoading('btnAmbientProbe', false); done(); }); }
function probeGestureSensor(){ const done = beginRequest(); setButtonLoading('btnGestureProbe', true, 'Pruefe Sensor'); fetch('/dev/gesture-probe', { method:'POST' }).then((r) => r.json()).then(() => fetchStatus()).catch(() => {}).finally(() => { setButtonLoading('btnGestureProbe', false); done(); }); }
function postSimple(url){ const done = beginRequest(); return fetch(url, { method:'POST' }).finally(done); }
function telemetryTransportMode(data){ if(data?.simTransportMode){ return data.simTransportMode; } if(data?.simTransport === 'USB'){ return 'USB_ONLY'; } if(data?.simTransport === 'NETWORK'){ return 'NETWORK_ONLY'; } return 'AUTO'; }
function blinkPreviewState(){ const speed = parseInt($('#blinkSpeedSlider')?.value || '80', 10); if(speed <= 0){ return false; } if(speed >= 100){ return true; } const intervalMs = Math.round(480 - ((speed / 99) * 440)); return Math.floor(Date.now() / Math.max(40, intervalMs)) % 2 === 0; }
function freshAge(ageMs, limitMs){ const age = Number(ageMs ?? -1); return Number.isFinite(age) && age >= 0 && age <= limitMs; }
function ambientSensorOnline(data){ return !!(data?.ambientLightDetected || data?.ambientDeviceResponding || ((Number(data?.ambientReadCount ?? 0) > 0) && freshAge(data?.ambientLastReadAgeMs, 2500))); }
function ambientSensorFresh(data){ return Number(data?.ambientReadCount ?? 0) > 0 && freshAge(data?.ambientLastReadAgeMs, 2500); }
function ambientSensorText(data){ if(ambientSensorOnline(data)){ return 'VEML7700 online'; } if(data?.ambientBusInitialized){ return 'Kein Sensor an 0x10'; } return 'Bus nicht bereit'; }
function ambientStatusText(data){ if(ambientSensorOnline(data)){ return ambientSensorFresh(data) ? 'Sensor liefert Daten.' : 'Sensor erkannt, letzter Messwert bleibt sichtbar.'; } return data?.ambientLastError || 'Noch keine Antwort vom Sensor.'; }
function gestureSensorOnline(data){ return !!(data?.gestureSensorDetected || data?.gestureDeviceResponding || data?.gestureIdReadOk || ((Number(data?.gestureInitSuccess ?? 0) > 0) && (freshAge(data?.gestureLastReadAgeMs, 4000) || freshAge(data?.gestureLastProbeAgeMs, 4000)))); }
function gestureSensorText(data){ if(!data?.gestureControlEnabled){ return 'Gesten aus'; } if(gestureSensorOnline(data)){ const id = Number(data?.gestureDeviceId ?? 0); return 'APDS-9960 online' + (id > 0 ? ' (0x' + id.toString(16).toUpperCase().padStart(2, '0') + ')' : ''); } if(data?.gestureAckResponding){ return 'ACK ok, Init fehlgeschlagen'; } if(data?.gestureBusInitialized){ return 'Kein ACK an 0x39'; } return 'Bus nicht bereit'; }
function gestureStatusText(data){ if(!data?.gestureControlEnabled){ return 'Gestensteuerung deaktiviert.'; } if(gestureSensorOnline(data)){ return 'Sensor bereit.'; } if(data?.gestureAckResponding){ return 'ACK vorhanden, aber Init noch nicht sauber.'; } return data?.gestureLastError || 'Noch keine Antwort vom Sensor.'; }
function updateStatus(data){
  if(!data){ return; }
  updateText('heroRpm', data.rpm ?? 0);
  updateText('heroMaxRpm', data.maxRpm ?? 0);
  updateText('heroSpeed', (data.speed ?? 0) + ' km/h');
  updateText('heroGear', data.gear ?? 0);
  updateText('gearSourceValue', data.gearSource || 'Unbekannt');
  updateText('rpmStartLiveValue', (data.rpmStartRpm ?? 0) + ' rpm');
  updateText('ledDiagModeValue', data.ledDiagnosticMode || 'live');
  updateText('ledDebugValue', `show ${data.ledFrameShows ?? 0} | skip ${data.ledFrameSkips ?? 0} | lvl ${(data.ledDisplayedLevel ?? 0)}/${(data.ledDesiredLevel ?? 0)} of ${(data.ledLevelCount ?? 0)} | fx ${(data.ledActiveEffect || '-')}/${(data.ledQueuedEffect || '-')} | writer ${(data.ledLastWriter || '-')}`);
  updateText('debugLastTx', data.lastTx || 'Noch kein TX');
  updateText('debugLastObd', data.lastObd || 'Noch keine Antwort');
  updateText('vehicleModelValue', data.vehicleModel || 'Noch kein Modell');
  updateText('vehicleVinValue', data.vehicleVin || 'Noch keine VIN');
  updateText('vehicleDiagValue', data.vehicleDiag || 'Keine Diagnose');
  updateText('bleSummaryValue', data.usbBridgeConnected ? (data.usbHost || 'USB Bridge') : (data.bleText || 'Getrennt'));
  const ambientOnline = ambientSensorOnline(data);
  const ambientFresh = ambientSensorFresh(data);
  const ambientBusReady = ambientOnline || !!data.ambientBusInitialized;
  const gestureOnline = gestureSensorOnline(data);
  const gestureBusReady = gestureOnline || !!data.gestureBusInitialized;
  updateText('ambientModeValue', data.autoBrightnessEnabled ? 'Automatisch' : 'Manuell');
  updateText('ambientSensorValue', ambientSensorText(data));
  updateText('ambientLuxValue', (Number(data.ambientReadCount ?? 0) > 0 || ambientOnline) ? (Number(data.ambientLux ?? 0).toFixed(1) + ' lx') : '-');
  updateText('ambientBrightnessValue', (data.ledAppliedBrightness ?? 0) + ' / ' + (data.ledManualBrightness ?? 0));
  updateText('ambientTargetValue', data.autoBrightnessEnabled ? String(data.ambientTargetBrightness ?? data.ledManualBrightness ?? 0) : 'Manuell');
  updateText('ambientPinsValue', 'SDA ' + (data.ambientLightSdaPin ?? '-') + ' / SCL ' + (data.ambientLightSclPin ?? '-'));
  updateText('ambientBusValue', ambientBusReady ? ((data.ambientUsingSharedBus ? 'Shared 47/48' : 'Privater I2C Bus') + (ambientOnline ? ' ok' : ' ohne ACK')) : 'Kein Bus');
  updateText('ambientProbeValue', ambientOnline ? 'Ja, 0x10 antwortet' : 'Nein');
  updateText('ambientRawValue', (data.ambientRawAls ?? 0) + ' / ' + (data.ambientRawWhite ?? 0));
  updateText('ambientConfigValue', '0x' + Number(data.ambientConfigReg ?? 0).toString(16).toUpperCase().padStart(4, '0'));
  updateText('ambientReadStatsValue', (data.ambientReadCount ?? 0) + ' ok / ' + (data.ambientReadErrors ?? 0) + ' err');
  updateText('ambientAgeValue', (data.ambientLastReadAgeMs ?? 0) + ' ms read / ' + (data.ambientLastInitAgeMs ?? 0) + ' ms init');
  updateText('ambientStatusNote', 'Sensorstatus: ' + ambientStatusText(data));
  setPill('ambientPill', data.autoBrightnessEnabled ? (ambientFresh ? 'ok' : (ambientOnline ? 'warn' : 'bad')) : 'neutral', data.autoBrightnessEnabled ? (ambientFresh ? 'Auto-Helligkeit live' : (ambientOnline ? 'Sensor bereit' : 'Sensor fehlt')) : (ambientOnline ? 'Sensor bereit' : 'Auto aus'));
  updateText('gestureStatusValue', gestureSensorText(data));
  updateText('gestureLastValue', (data.gestureLastDirection || 'none') + ' / ' + (data.gestureLastGestureAgeMs ?? 0) + ' ms');
  updateText('gestureCountValue', (data.gestureCount ?? 0) + ' erkannt / ' + (data.gestureModeSwitchCount ?? 0) + ' gewechselt');
  updateText('gestureErrorValue', data.gestureLastError || 'Kein Fehler');
  updateText('gestureBusValue', gestureBusReady ? ((data.gestureUsingSharedBus ? 'Shared 47/48' : 'Eigener Bus') + ((gestureOnline || data.gestureAckResponding) ? ' ok' : ' ohne ACK')) : 'Kein Bus');
  updateText('gestureProbeValue', (gestureOnline || data.gestureAckResponding) ? 'Ja, 0x39 antwortet' : 'Nein');
  updateText('gestureIdValue', data.gestureIdReadOk ? ('0x' + Number(data.gestureDeviceId ?? 0).toString(16).toUpperCase().padStart(2, '0') + (data.gestureConfigApplied ? ' konfiguriert' : ' gelesen')) : '-');
  updateText('gestureIntValue', 'GPIO ' + (data.gestureIntPin ?? '-') + ' / ' + (data.gestureIntConfigured ? (data.gestureIntLineLow ? 'LOW' : 'HIGH') : 'aus'));
  updateText('gestureRawValue', 'GSTATUS 0x' + Number(data.gestureLastStatusReg ?? 0).toString(16).toUpperCase().padStart(2, '0') + ' / FIFO ' + (data.gestureLastFifoLevel ?? 0));
  updateText('gestureStatsValue', (data.gestureInitSuccess ?? 0) + '/' + (data.gestureInitAttempts ?? 0) + ' init | ' + (data.gestureProbeCount ?? 0) + ' probe | ' + (data.gesturePollCount ?? 0) + ' poll | ' + (data.gestureReadErrors ?? 0) + ' err | ' + (data.gestureIntTriggerCount ?? 0) + ' irq');
  updateText('gestureAgeValue', (data.gestureLastProbeAgeMs ?? 0) + ' ms probe / ' + (data.gestureLastReadAgeMs ?? 0) + ' ms read / ' + (data.gestureLastIntAgeMs ?? 0) + ' ms irq');
  updateText('gestureStatusNote', 'Gestenstatus: ' + gestureStatusText(data));
  const transportMode = telemetryTransportMode(data);
  const usbOnly = transportMode === 'USB_ONLY';
  const networkOnly = transportMode === 'NETWORK_ONLY';
  const autoMode = transportMode === 'AUTO';
  const activeTelemetry = data.activeTelemetry || 'Keine';
  const fallbackSuffix = autoMode && data.telemetryFallback ? ' Fallback' : '';
  const telemetryTone = activeTelemetry === 'USB Sim' ? 'ok' : (activeTelemetry === 'SimHub' ? 'neutral' : (activeTelemetry === 'OBD' ? 'warn' : 'bad'));
  let simTone = data.simHubConfigured ? 'warn' : 'bad';
  let simText = data.simHubState || 'Keine SimHub-Daten';
  if(usbOnly){
    simTone = data.usbState === 'USB live' ? 'ok' : (data.usbConnected ? 'warn' : 'bad');
    simText = data.usbState || 'USB wartet';
  } else if(networkOnly){
    simTone = data.simHubState === 'Live' ? 'ok' : (data.simHubConfigured ? 'warn' : 'bad');
    simText = data.simHubState || 'Netzwerk wartet';
  } else if(activeTelemetry === 'USB Sim'){
    simTone = data.usbState === 'USB live' ? 'ok' : 'warn';
    simText = data.usbState || 'USB aktiv';
  } else if(activeTelemetry === 'SimHub'){
    simTone = data.simHubState === 'Live' ? 'ok' : 'warn';
    simText = (data.simHubState || 'Netzwerk') + fallbackSuffix;
  } else if(activeTelemetry === 'OBD'){
    simTone = 'warn';
    simText = data.telemetryFallback ? 'OBD Fallback' : 'OBD aktiv';
  } else if(data.usbConnected || data.usbBridgeConnected){
    simTone = 'warn';
    simText = data.usbState || 'USB wartet';
  }
  setPill('telemetryPill', telemetryTone, activeTelemetry + fallbackSuffix);
  setPill('simHubPill', simTone, simText);
  if(data.obdAllowed === false){
    setPill('blePill', 'neutral', 'OBD deaktiviert');
  } else {
    setPill('blePill', data.connected ? 'ok' : (data.bleConnectInProgress ? 'warn' : 'bad'), data.connected ? 'OBD verbunden' : (data.bleConnectInProgress ? 'OBD verbindet' : 'OBD offline'));
  }
  const connectBtn = $('#btnConnect');
  const disconnectBtn = $('#btnDisconnect');
  if(connectBtn && disconnectBtn){
    if(data.obdAllowed === false){
      connectBtn.classList.add('hidden');
      disconnectBtn.classList.add('hidden');
    } else {
      connectBtn.classList.toggle('hidden', !!data.connected);
      disconnectBtn.classList.toggle('hidden', !data.connected);
    }
  }
}
function fetchStatus(){ const done = beginRequest(); fetch('/status').then((r) => r.json()).then(updateStatus).catch(() => {}).finally(done); }
function saveForm(url){ const form = $('#mainForm'); if(!form){ return Promise.resolve(); } const done = beginRequest(); setButtonLoading('btnSave', true, 'Speichere'); return fetch(url, { method:'POST', body:new FormData(form) }).then(() => { captureInitialState(); recomputeDirty(); }).finally(() => { setButtonLoading('btnSave', false); done(); }); }
function startSweep(mode='show'){ const form = $('#mainForm'); if(!form){ return; } const done = beginRequest(); const btnId = mode === 'diagnostic' ? 'btnTestDiagnostic' : 'btnTest'; const url = mode === 'diagnostic' ? '/test-diagnostic' : '/test'; setButtonLoading(btnId, true, mode === 'diagnostic' ? 'Diagnose laeuft' : 'Sweep laeuft'); fetch(url, { method:'POST', body:new FormData(form) }).finally(() => { setButtonLoading(btnId, false); done(); }); dashboardState.testSweepActive = true; dashboardState.testSweepMode = mode; dashboardState.testSweepStart = Date.now(); dashboardState.blinkPreview = false; ensurePreviewLoop(); }
function setLedDiagnosticMode(mode){ const done = beginRequest(); fetch('/dev/led-mode', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'mode=' + encodeURIComponent(mode) }).then((r) => r.json()).then(() => fetchStatus()).catch(() => {}).finally(done); }
function initLedPreview(){ syncLedPreviewCount(); renderLedPreview(1, false); ensurePreviewLoop(); }
function initDashboard(){ captureInitialState(); recomputeDirty(); syncSavebarSpace('saveBar'); initLedPreview(); syncAutoscaleUi(); syncAmbientUi(); updatePreviewLoop(); $$('input,select,textarea', $('#mainForm')).forEach((el) => { if(el.type === 'range'){ updateSliderValue(el); el.addEventListener('input', () => { if(el.id === 'greenEndSlider' || el.id === 'yellowEndSlider' || el.id === 'redEndSlider' || el.id === 'blinkStartSlider'){ enforceOrder(el.id); if(el.id === 'redEndSlider' || el.id === 'blinkStartSlider'){ triggerBlinkPreview(); } updatePreviewLoop(); } else { updateSliderValue(el); if(el.id === 'autoBrightnessMinSlider' || el.id === 'brightnessSlider'){ syncAmbientUi(); } if(el.id === 'rpmStartSlider' || el.id === 'activeLedCountSlider'){ syncLedPreviewCount(); updatePreviewLoop(); } if(el.id === 'blinkSpeedSlider'){ triggerBlinkPreview(); updatePreviewLoop(); } } markDirty(); syncSavebarSpace('saveBar'); }); el.addEventListener('change', () => { if(el.id === 'activeLedCountSlider'){ syncLedPreviewCount(); updatePreviewLoop(); } if(el.id === 'blinkSpeedSlider'){ triggerBlinkPreview(); updatePreviewLoop(); } markDirty(); syncSavebarSpace('saveBar'); }); } else { el.addEventListener('input', () => { markDirty(); syncSavebarSpace('saveBar'); }); el.addEventListener('change', () => { markDirty(); syncSavebarSpace('saveBar'); }); } }); $('#autoscaleToggle')?.addEventListener('change', () => { syncAutoscaleUi(); markDirty(); syncSavebarSpace('saveBar'); }); $('#autoBrightnessToggle')?.addEventListener('change', () => { syncAmbientUi(); markDirty(); syncSavebarSpace('saveBar'); }); $('#modeSelect')?.addEventListener('change', () => { updatePreviewLoop(); markDirty(); syncSavebarSpace('saveBar'); }); $('#brightnessSlider')?.addEventListener('input', (ev) => { onBrightnessInput(ev.target.value); syncAmbientUi(); syncSavebarSpace('saveBar'); }); $('#brightnessSlider')?.addEventListener('change', (ev) => { onBrightnessInput(ev.target.value); syncAmbientUi(); syncSavebarSpace('saveBar'); }); ['greenColorInput','yellowColorInput','redColorInput'].forEach((id) => { document.getElementById(id)?.addEventListener('input', () => { updatePreviewLoop(); markDirty(); syncSavebarSpace('saveBar'); }); }); $('#btnSave')?.addEventListener('click', () => saveForm('/save').finally(() => syncSavebarSpace('saveBar'))); $('#btnReset')?.addEventListener('click', () => window.location.reload()); $('#btnTest')?.addEventListener('click', () => startSweep('show')); $('#btnTestDiagnostic')?.addEventListener('click', () => startSweep('diagnostic')); $('#btnConnect')?.addEventListener('click', () => postSimple('/connect').then(fetchStatus)); $('#btnDisconnect')?.addEventListener('click', () => postSimple('/disconnect').then(fetchStatus)); $('#btnLedDiagLive')?.addEventListener('click', () => setLedDiagnosticMode('live')); $('#btnLedDiagOff')?.addEventListener('click', () => setLedDiagnosticMode('off')); $('#btnLedDiagGreen')?.addEventListener('click', () => setLedDiagnosticMode('static-green')); $('#btnLedDiagWhite')?.addEventListener('click', () => setLedDiagnosticMode('static-white')); $('#btnLedDiagPit')?.addEventListener('click', () => setLedDiagnosticMode('pit-markers')); $('#btnAmbientProbe')?.addEventListener('click', probeAmbientSensor); $('#btnGestureProbe')?.addEventListener('click', probeGestureSensor); $('#btnDisplayStatus')?.addEventListener('click', refreshDisplayStatus); $('#btnDisplayBars')?.addEventListener('click', () => { const done = beginRequest(); fetch('/dev/display-pattern', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'pattern=bars' }).finally(() => { done(); refreshDisplayStatus(); }); }); $('#btnDisplayGrid')?.addEventListener('click', () => { const done = beginRequest(); fetch('/dev/display-pattern', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'pattern=grid' }).finally(() => { done(); refreshDisplayStatus(); }); }); $('#btnDisplayLogo')?.addEventListener('click', () => postSimple('/dev/display-logo').then(refreshDisplayStatus)); window.addEventListener('resize', () => syncSavebarSpace('saveBar')); fetchStatus(); setInterval(fetchStatus, 2200); if($('#displayStatusPill')){ refreshDisplayStatus(); } }
document.addEventListener('DOMContentLoaded', initDashboard);
)JS");
        page += F("</script>");
    }
}

String buildDashboardPage()
{
    const WifiStatus wifi = getWifiStatus();
    const DisplayDebugInfo displayInfo = displayGetDebugInfo();
    const AmbientLightDebugInfo ambientInfo = ambientLightGetDebugInfo();
    const GestureSensorDebugInfo gestureInfo = gestureSensorGetDebugInfo();
    const unsigned long now = millis();
    const String greenHex = colorToHex(cfg.greenColor);
    const String yellowHex = colorToHex(cfg.yellowColor);
    const String redHex = colorToHex(cfg.redColor);
    const String greenLabel = safeLabel(cfg.greenLabel, F("Low RPM"));
    const String yellowLabel = safeLabel(cfg.yellowLabel, F("Mid RPM"));
    const String redLabel = safeLabel(cfg.redLabel, F("Shift"));
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    const String ambientPinsHint = F("Waveshare S3: Die externen Board-Pins SDA/SCL liegen auf GPIO 47/48. GPIO 21 ist hier AMOLED-Reset.");
#else
    const String ambientPinsHint = F("Standard-ESP32 Default: SDA 21 / SCL 22.");
#endif
    const bool ambientOnline = ambientUiOnline(ambientInfo, now);
    const bool ambientBusReady = ambientInfo.busInitialized || ambientOnline;
    const String ambientSensorText = ambientUiSensorText(ambientInfo, now);
    const String ambientLuxText = ambientUiLuxText(ambientInfo, now);
    const String ambientStatusText = ambientUiStatusText(ambientInfo, now);
    const String ambientBusText = ambientBusReady ? String(ambientInfo.usingSharedBus ? "Shared 47/48" : "Privater I2C Bus") + (ambientOnline ? " ok" : " ohne ACK")
                                                  : String(F("Kein Bus"));
    const String ambientProbeText = ambientOnline ? String(F("Ja, 0x10 antwortet")) : String(F("Nein"));
    const String ambientPillText = ambientUiPillText(ambientInfo, cfg.autoBrightnessEnabled, now);
    const String gestureSensorText = gestureUiSensorText(gestureInfo, cfg.gestureControlEnabled, now);
    const bool gestureOnline = gestureUiOnline(gestureInfo, now);
    const bool gestureBusReady = gestureInfo.busInitialized || gestureOnline;
    const String gestureStatusText = gestureUiStatusText(gestureInfo, cfg.gestureControlEnabled, now);
    const String gestureBusText = gestureBusReady ? String(gestureInfo.usingSharedBus ? "Shared 47/48" : "Eigener Bus") + ((gestureOnline || gestureInfo.ackResponding) ? " ok" : " ohne ACK")
                                                  : String(F("Kein Bus"));
    const String gestureProbeText = (gestureOnline || gestureInfo.ackResponding) ? String(F("Ja, 0x39 antwortet")) : String(F("Nein"));

    String page;
    page.reserve(26000);
    appendShellHead(page, F("ShiftLight Dashboard"), false);
    page += F("<section class='hero'><div class='hero-card hero-card--accent'><div class='hero-head'><div><div class='hero-kicker'>Live Dashboard</div><div class='hero-title'>RPM, Status und ShiftLight</div><div class='hero-sub'>Die Hauptkonfiguration bleibt leichtgewichtig, livefaehig und mobil gut bedienbar.</div></div><div class='status-list'>");
    page += "<span class='pill neutral' id='telemetryPill'>" + htmlEscape(activeTelemetryLabel()) + "</span>";
    page += "<span class='pill " + String(g_connected ? "ok" : "warn") + "' id='blePill'>" + (g_connected ? F("OBD verbunden") : F("OBD offline")) + "</span>";
    page += "<span class='pill " + String(displayInfo.ready ? "ok" : "warn") + "' id='displayStatusPill'>" + htmlEscape(displaySummary(displayInfo)) + "</span>";
    page += F("</div></div><div class='metric-grid'>");
    page += "<div class='metric'><div class='metric-label'>RPM</div><div class='metric-value' id='heroRpm'>" + String(g_currentRpm) + "</div><div class='metric-note'>Live vom aktiven Telemetriepfad</div></div>";
    page += "<div class='metric'><div class='metric-label'>Max gesehen</div><div class='metric-value metric-value--compact' id='heroMaxRpm'>" + String(g_maxSeenRpm) + "</div><div class='metric-note'>Auto-Scale Referenz</div></div>";
    page += "<div class='metric'><div class='metric-label'>Geschwindigkeit</div><div class='metric-value metric-value--compact' id='heroSpeed'>" + String(g_vehicleSpeedKmh) + " km/h</div><div class='metric-note'>Aktiver Fahrzustand</div></div>";
    page += "<div class='metric'><div class='metric-label'>Gang</div><div class='metric-value metric-value--compact' id='heroGear'>" + String(g_estimatedGear) + "</div><div class='metric-note' id='gearSourceValue'>" + htmlEscape(g_activeTelemetrySource == ActiveTelemetrySource::Obd ? String(F("OBD berechnet")) : String(F("SimHub direkt"))) + "</div></div></div></div>";
    page += F("<div class='hero-card'><div class='hero-head'><div><div class='hero-kicker'>Verbindung</div><div class='hero-title'>Status in Echtzeit</div><div class='hero-sub'>Wichtige Systemzustaende bleiben sichtbar, ohne die Seite mit Debug-Rohdaten zu ueberladen.</div></div></div><div class='badge-grid'>");
    page += "<div class='badge-card'><span>Aktive Telemetrie</span><strong>" + htmlEscape(activeTelemetryLabel()) + "</strong></div>";
    page += "<div class='badge-card'><span>SimHub</span><strong id='simHubPill'>" + htmlEscape(simHubStateLabel()) + "</strong></div>";
    page += "<div class='badge-card'><span>BLE / OBD</span><strong id='bleSummaryValue'>" + htmlEscape(bleSummary()) + "</strong></div>";
    page += "<div class='badge-card'><span>WLAN</span><strong>" + htmlEscape(staSummary(wifi)) + "</strong></div></div>";
    page += "<div class='status-inline'><span class='pill " + String(wifi.staConnected ? "ok" : (wifi.apActive ? "warn" : "bad")) + "'>" + htmlEscape(wifiModeLabel(wifi.mode)) + "</span><span class='pill " + String(g_simHubConnectionState == SimHubConnectionState::Live ? "ok" : (cfg.simHubHost.length() > 0 ? "warn" : "bad")) + "'>" + htmlEscape(simHubStateLabel()) + "</span></div></div></section>";
    page += F("<form id='mainForm' action='/save' method='POST'><div class='dashboard-layout'><div class='dashboard-col'><section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Allgemein</h2><div class='panel-copy'>Die haeufigsten Einstellungen bleiben oben und klar erreichbar.</div></div></div><div class='field-grid two'><div class='field'><label for='modeSelect'>Schaltmodus</label><select id='modeSelect' name='mode'>");
    page += "<option value='0'" + selectedAttr(cfg.mode == 0) + ">Casual</option><option value='1'" + selectedAttr(cfg.mode == 1) + ">F1-Style</option><option value='2'" + selectedAttr(cfg.mode == 2) + ">Aggressiv</option><option value='3'" + selectedAttr(cfg.mode == 3) + ">GT3 / Endurance</option></select><div class='field-note'>GT3 laeuft von aussen nach innen, blinkt am Ende komplett rot und zeigt bei aktivem Pit-Limiter nur noch die magenta Marker aussen.</div></div>";
    page += "<div class='field'><label for='brightnessSlider'>LED-Helligkeit</label><input type='range' id='brightnessSlider' min='0' max='255' value='" + String(cfg.brightness) + "'><div class='field-note'>Manueller Max-Wert. Auto-Helligkeit bleibt immer darunter. Aktuell <span id='brightnessValue'>" + String(cfg.brightness) + "</span> / 255</div><input type='hidden' id='brightnessHidden' name='brightness' value='" + String(cfg.brightness) + "'></div></div><div class='button-row'><button type='button' class='btn btn-secondary' id='btnTest'>RPM-Sweep testen</button><button type='button' class='btn btn-secondary' id='btnTestDiagnostic'>Diagnose-Sweep</button>";
    page += "<button type='button' class='btn btn-secondary " + String(g_connected ? "hidden" : "") + "' id='btnConnect'>Mit OBD verbinden</button>";
    page += "<button type='button' class='btn btn-danger " + String(g_connected ? "" : "hidden") + "' id='btnDisconnect'>OBD trennen</button></div></section>";
    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Live-Fahrzeugstatus</h2><div class='panel-copy'>Wichtige Live-Werte bleiben kompakt sichtbar und werden gepollt, nicht ueber-rendered.</div></div></div><div class='info-list'>");
    page += "<div class='info-row'><div class='info-label'>Fahrzeug</div><div class='info-value' id='vehicleModelValue'>" + htmlEscape(readVehicleModel()) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>VIN</div><div class='info-value mono' id='vehicleVinValue'>" + htmlEscape(readVehicleVin()) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Diagnose</div><div class='info-value' id='vehicleDiagValue'>" + htmlEscape(readVehicleDiagStatus()) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>BLE Status</div><div class='info-value' id='bleSummaryValue'>" + htmlEscape(bleSummary()) + "</div></div></div></section>";
    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>RPM / ShiftLight</h2><div class='panel-copy'>Grenzen, Farben und Vorschau sind logisch gruppiert und fuer Touch optimiert.</div></div></div><div class='field-grid two'><div class='field-inline'><div><strong>Auto-Scale Max RPM</strong><span>Nutze die hoechste beobachtete Drehzahl als Referenz.</span></div><label class='switch'><input type='checkbox' id='autoscaleToggle' name='autoscale'");
    page += checkedAttr(cfg.autoScaleMaxRpm);
    page += F("><span class='slider'></span></label></div>");
    page += "<div class='field hidden' id='fixedMaxWrap'><label for='fixedMaxRpmInput'>Feste Max RPM</label><input type='number' id='fixedMaxRpmInput' name='fixedMaxRpm' min='1000' max='8000' value='" + String(cfg.fixedMaxRpm) + "'><div class='field-note'>Wird nur verwendet, wenn Auto-Scale aus ist.</div></div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>LED Start</div><div class='range-value' id='rpmStartValue'>" + String(cfg.rpmStartRpm) + " rpm</div></div><input type='range' name='rpmStartRpm' id='rpmStartSlider' min='0' max='6000' value='" + String(cfg.rpmStartRpm) + "' data-value-target='rpmStartValue' data-suffix=' rpm'><div class='seg-note'>Unterhalb dieser Drehzahl bleibt die LED-Bar komplett aus.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>" + htmlEscape(greenLabel) + "</div><div class='range-value' id='greenEndValue'>" + String(cfg.greenEndPct) + "%</div></div><input type='range' name='greenEndPct' id='greenEndSlider' min='0' max='100' value='" + String(cfg.greenEndPct) + "' data-value-target='greenEndValue' data-suffix='%'><div class='seg-note'>Ende des ruhigen Bereichs.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>" + htmlEscape(yellowLabel) + "</div><div class='range-value' id='yellowEndValue'>" + String(cfg.yellowEndPct) + "%</div></div><input type='range' name='yellowEndPct' id='yellowEndSlider' min='0' max='100' value='" + String(cfg.yellowEndPct) + "' data-value-target='yellowEndValue' data-suffix='%'><div class='seg-note'>Uebergang in die Warnzone.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>" + htmlEscape(redLabel) + "</div><div class='range-value' id='redEndValue'>" + String(cfg.redEndPct) + "%</div></div><input type='range' name='redEndPct' id='redEndSlider' min='0' max='100' value='" + String(cfg.redEndPct) + "' data-value-target='redEndValue' data-suffix='%'><div class='seg-note'>Wie weit die feste dritte Farbe innerhalb des Vor-Blink-Bereichs aufspannt.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Blink Start</div><div class='range-value' id='blinkStartValue'>" + String(cfg.blinkStartPct) + "%</div></div><input type='range' name='blinkStartPct' id='blinkStartSlider' min='0' max='100' value='" + String(cfg.blinkStartPct) + "' data-value-target='blinkStartValue' data-suffix='%'><div class='seg-note'>Diese Grenze ist die komplette Vor-Blink-Skala. Gruen, Gelb und Rot werden anteilig in diesen Bereich eingepasst; darueber blinkt die Bar.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Blink Tempo</div><div class='range-value' id='blinkSpeedValue'>" + String(cfg.blinkSpeedPct) + "%</div></div><input type='range' name='blinkSpeedPct' id='blinkSpeedSlider' min='0' max='100' value='" + String(cfg.blinkSpeedPct) + "' data-value-target='blinkSpeedValue' data-suffix='%'><div class='seg-note'>0 = Blinkphase aus, 100 = quasi dauerhaft an. Dazwischen wird das rote Endblinken immer schneller.</div></div>";
    page += "<div><div class='field-label'>LED Vorschau</div><div id='ledPreview' class='led-preview'></div></div><div class='info-list'><div class='info-row'><div class='info-label'>Aktueller Startpunkt</div><div class='info-value mono' id='rpmStartLiveValue'>" + String(cfg.rpmStartRpm) + " rpm</div></div><div class='info-row'><div class='info-label'>LED Diagnosemodus</div><div class='info-value mono' id='ledDiagModeValue'>live</div></div><div class='info-row'><div class='info-label'>LED Render Debug</div><div class='info-value mono' id='ledDebugValue'>show 0 | skip 0 | mode -</div></div></div><div class='range-wrap'><div class='range-head'><div class='range-title'>Aktive LEDs</div><div class='range-value' id='activeLedCountValue'>" + String(cfg.activeLedCount) + "</div></div><input type='range' name='activeLedCount' id='activeLedCountSlider' min='1' max='" + String(NUM_LEDS) + "' value='" + String(cfg.activeLedCount) + "' data-value-target='activeLedCountValue'><div class='seg-note'>Nur fuer Tests. LEDs oberhalb dieser Anzahl bleiben komplett aus.</div></div><div class='button-row'><button type='button' class='btn btn-ghost' id='btnLedDiagLive'>Live</button><button type='button' class='btn btn-ghost' id='btnLedDiagOff'>Aus</button><button type='button' class='btn btn-ghost' id='btnLedDiagGreen'>Gruen statisch</button><button type='button' class='btn btn-ghost' id='btnLedDiagWhite'>Weiss statisch</button><button type='button' class='btn btn-ghost' id='btnLedDiagPit'>Pit Marker</button></div><div class='field-note'>Wenn selbst in einem statischen Diagnosemodus sichtbares Flackern bleibt, liegt das Problem sehr wahrscheinlich an Hardware, Versorgung oder Strip-Signal und nicht an wechselnden Telemetriedaten.</div></section>";
    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Farbzonen</h2><div class='panel-copy'>Direkt, klar und ohne ueberfluessige UI-Last.</div></div></div><div class='color-grid'>");
    page += "<div class='color-card'><label for='greenColorInput'>" + htmlEscape(greenLabel) + "</label><input type='color' id='greenColorInput' name='greenColor' value='" + greenHex + "'><div class='seg-note'>Low RPM Bereich</div><input type='hidden' name='greenLabel' value='" + htmlEscape(greenLabel) + "'></div>";
    page += "<div class='color-card'><label for='yellowColorInput'>" + htmlEscape(yellowLabel) + "</label><input type='color' id='yellowColorInput' name='yellowColor' value='" + yellowHex + "'><div class='seg-note'>Mid RPM Bereich</div><input type='hidden' name='yellowLabel' value='" + htmlEscape(yellowLabel) + "'></div>";
    page += "<div class='color-card'><label for='redColorInput'>" + htmlEscape(redLabel) + "</label><input type='color' id='redColorInput' name='redColor' value='" + redHex + "'><div class='seg-note'>Shift / Warnung</div><input type='hidden' name='redLabel' value='" + htmlEscape(redLabel) + "'></div></div></section>";
    page += F("<details class='panel details'><summary><div><h2 class='panel-title'>Display & Debug</h2><div class='panel-copy'>Seltener benoetigte Diagnose-Infos bleiben aufgeraeumt, sind aber weiter verfuegbar.</div></div></summary><div class='stack' style='margin-top:14px'><div class='status-inline'>");
    page += "<span class='pill " + String(displayInfo.ready ? "ok" : "warn") + "' id='displayStatusPill'>" + htmlEscape(displaySummary(displayInfo)) + "</span></div><div class='info-list'>";
    page += "<div class='info-row'><div class='info-label'>Display</div><div class='info-value' id='displayReadyValue'>" + String(displayInfo.ready ? "Bereit" : "Nicht bereit") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Touch</div><div class='info-value' id='displayTouchValue'>" + String(displayInfo.touchReady ? "Touch aktiv" : "Touch fehlt") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Buffer</div><div class='info-value' id='displayBuffersValue'>" + String(displayInfo.buffersAllocated ? "DMA ok" : "Keine Buffer") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>LVGL Tick</div><div class='info-value' id='displayTickValue'>" + String(displayInfo.tickFallback ? "Loop" : "Timer") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Fehler</div><div class='info-value' id='displayErrorValue'>" + htmlEscape(displayInfo.lastError.isEmpty() ? String(F("Kein Fehler")) : displayInfo.lastError) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Letzter TX</div><div class='info-value mono' id='debugLastTx'>" + htmlEscape(g_lastTxInfo.isEmpty() ? String(F("Noch kein TX")) : g_lastTxInfo) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Letzte OBD Zeile</div><div class='info-value mono' id='debugLastObd'>" + htmlEscape(g_lastObdInfo.isEmpty() ? String(F("Noch keine Antwort")) : g_lastObdInfo) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Gestensensor</div><div class='info-value' id='gestureStatusValue'>" + gestureSensorText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Letzte Geste</div><div class='info-value mono' id='gestureLastValue'>" + String(gestureSensorDirectionName(gestureInfo.lastGesture)) + " / " + String(gestureInfo.lastGestureMs > 0 ? (millis() - gestureInfo.lastGestureMs) : 0) + " ms</div></div>";
    page += "<div class='info-row'><div class='info-label'>Gesten-Zaehler</div><div class='info-value' id='gestureCountValue'>" + String(gestureInfo.gestureCount) + " erkannt / " + String(gestureInfo.modeSwitchCount) + " gewechselt</div></div>";
    page += "<div class='info-row'><div class='info-label'>Gestenfehler</div><div class='info-value mono' id='gestureErrorValue'>" + htmlEscape(gestureInfo.lastError.isEmpty() ? String(F("Kein Fehler")) : gestureInfo.lastError) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Gesten-Bus</div><div class='info-value' id='gestureBusValue'>" + gestureBusText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>0x39 Antwort</div><div class='info-value' id='gestureProbeValue'>" + gestureProbeText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>ID / Init</div><div class='info-value mono' id='gestureIdValue'>" + String(gestureInfo.idReadOk ? (String("0x") + String(gestureInfo.deviceId, HEX) + (gestureInfo.configApplied ? " konfiguriert" : " gelesen")) : "-") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>INT</div><div class='info-value mono' id='gestureIntValue'>GPIO " + String(gestureInfo.intPin) + " / " + String(gestureInfo.intConfigured ? (gestureInfo.intLineLow ? "LOW" : "HIGH") : "aus") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>GSTATUS / FIFO</div><div class='info-value mono' id='gestureRawValue'>GSTATUS 0x" + String(gestureInfo.lastStatusReg, HEX) + " / FIFO " + String(gestureInfo.lastFifoLevel) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Reads</div><div class='info-value' id='gestureStatsValue'>" + String(gestureInfo.initSuccessCount) + "/" + String(gestureInfo.initAttempts) + " init / " + String(gestureInfo.probeCount) + " probe / " + String(gestureInfo.pollCount) + " poll / " + String(gestureInfo.readErrorCount) + " err / " + String(gestureInfo.intTriggerCount) + " irq</div></div>";
    page += "<div class='info-row'><div class='info-label'>Alter</div><div class='info-value' id='gestureAgeValue'>" + String(gestureInfo.lastProbeMs > 0 ? (millis() - gestureInfo.lastProbeMs) : 0) + " ms probe / " + String(gestureInfo.lastReadMs > 0 ? (millis() - gestureInfo.lastReadMs) : 0) + " ms read / " + String(gestureInfo.lastIntMs > 0 ? (millis() - gestureInfo.lastIntMs) : 0) + " ms irq</div></div>";
    page += F("</div><div class='button-row'><button type='button' class='btn btn-secondary' id='btnGestureProbe'>Gestensensor pruefen</button><button type='button' class='btn btn-secondary' id='btnDisplayStatus'>Status aktualisieren</button><button type='button' class='btn btn-secondary' id='btnDisplayBars'>Farb-Balken</button><button type='button' class='btn btn-secondary' id='btnDisplayGrid'>Raster</button><button type='button' class='btn btn-secondary' id='btnDisplayLogo'>Logo anzeigen</button></div>");
    page += "<div class='field-note' id='gestureStatusNote'>Gestenstatus: " + htmlEscape(gestureStatusText) + "</div></div></details></div><div class='dashboard-col'>";
    page += "<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Auto-Helligkeit</h2><div class='panel-copy'>VEML7700 passt die LED-Helligkeit weich an. Die manuelle Helligkeit bleibt immer die Obergrenze.</div></div><span class='pill " + String(ambientUiPillTone(ambientInfo, cfg.autoBrightnessEnabled, now)) + "' id='ambientPill'>" + ambientPillText + "</span></div>";
    page += F("<div class='field-inline'><div><strong>Automatische Helligkeit</strong><span>OEM-artige Anpassung ueber das Umgebungslicht.</span></div><label class='switch'><input type='checkbox' id='autoBrightnessToggle' name='autoBrightnessEnabled'");
    page += checkedAttr(cfg.autoBrightnessEnabled);
    page += F("><span class='slider'></span></label></div>");
    page += F("<div id='ambientAutoFields'><div class='field-grid two'>");
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Minimum</div><div class='range-value' id='autoBrightnessMinValue'>" + String(cfg.autoBrightnessMin) + "</div></div><input type='range' id='autoBrightnessMinSlider' name='autoBrightnessMin' min='0' max='" + String(cfg.brightness) + "' value='" + String(cfg.autoBrightnessMin) + "' data-value-target='autoBrightnessMinValue'><div class='seg-note'>Untergrenze im Dunkeln.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Faktor</div><div class='range-value' id='autoBrightnessStrengthValue'>" + String(cfg.autoBrightnessStrengthPct) + "%</div></div><input type='range' id='autoBrightnessStrengthSlider' name='autoBrightnessStrengthPct' min='25' max='200' value='" + String(cfg.autoBrightnessStrengthPct) + "' data-value-target='autoBrightnessStrengthValue' data-suffix='%'><div class='seg-note'>Macht die gesamte Auto-Kurve heller oder dunkler.</div></div>";
    page += "<div class='range-wrap'><div class='range-head'><div class='range-title'>Reaktion</div><div class='range-value' id='autoBrightnessResponseValue'>" + String(cfg.autoBrightnessResponsePct) + "%</div></div><input type='range' id='autoBrightnessResponseSlider' name='autoBrightnessResponsePct' min='1' max='100' value='" + String(cfg.autoBrightnessResponsePct) + "' data-value-target='autoBrightnessResponseValue' data-suffix='%'><div class='seg-note'>Hoeher reagiert schneller, niedriger wirkt ruhiger.</div></div>";
    page += "<div class='field'><label for='autoBrightnessLuxMin'>Lux Start</label><input type='number' id='autoBrightnessLuxMin' name='autoBrightnessLuxMin' min='0' max='120000' value='" + String(cfg.autoBrightnessLuxMin) + "'><div class='field-note'>Unter diesem Wert bleibt die Helligkeit nahe Minimum.</div></div>";
    page += "<div class='field'><label for='autoBrightnessLuxMax'>Lux Voll</label><input type='number' id='autoBrightnessLuxMax' name='autoBrightnessLuxMax' min='1' max='120000' value='" + String(cfg.autoBrightnessLuxMax) + "'><div class='field-note'>Ab hier nutzt das System praktisch den manuellen Max-Wert.</div></div>";
    page += "<div class='field'><label for='ambientLightSdaPin'>VEML7700 SDA Pin</label><input type='number' id='ambientLightSdaPin' name='ambientLightSdaPin' min='0' max='48' value='" + String(cfg.ambientLightSdaPin) + "'></div>";
    page += "<div class='field'><label for='ambientLightSclPin'>VEML7700 SCL Pin</label><input type='number' id='ambientLightSclPin' name='ambientLightSclPin' min='0' max='48' value='" + String(cfg.ambientLightSclPin) + "'></div></div><div class='field-note'>" + htmlEscape(ambientPinsHint) + "</div></div>";
    page += F("<div class='info-list compact'>");
    page += "<div class='info-row'><div class='info-label'>Modus</div><div class='info-value' id='ambientModeValue'>" + String(cfg.autoBrightnessEnabled ? "Automatisch" : "Manuell") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Sensor</div><div class='info-value' id='ambientSensorValue'>" + ambientSensorText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Lux</div><div class='info-value' id='ambientLuxValue'>" + ambientLuxText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>LED aktuell / Max</div><div class='info-value' id='ambientBrightnessValue'>" + String(ambientInfo.appliedBrightness) + " / " + String(cfg.brightness) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Auto Ziel</div><div class='info-value' id='ambientTargetValue'>" + String(cfg.autoBrightnessEnabled ? ambientInfo.targetBrightness : cfg.brightness) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Pins</div><div class='info-value mono' id='ambientPinsValue'>SDA " + String(cfg.ambientLightSdaPin) + " / SCL " + String(cfg.ambientLightSclPin) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Bus</div><div class='info-value' id='ambientBusValue'>" + ambientBusText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>0x10 Antwort</div><div class='info-value' id='ambientProbeValue'>" + ambientProbeText + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>ALS / White</div><div class='info-value mono' id='ambientRawValue'>" + String(ambientInfo.rawAls) + " / " + String(ambientInfo.rawWhite) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Config</div><div class='info-value mono' id='ambientConfigValue'>0x" + String(ambientInfo.configReg, HEX) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Reads</div><div class='info-value' id='ambientReadStatsValue'>" + String(ambientInfo.readCount) + " ok / " + String(ambientInfo.readErrorCount) + " err</div></div>";
    page += "<div class='info-row'><div class='info-label'>Alter</div><div class='info-value' id='ambientAgeValue'>" + String(ambientInfo.lastReadMs > 0 ? (millis() - ambientInfo.lastReadMs) : 0) + " ms read / " + String(ambientInfo.lastInitMs > 0 ? (millis() - ambientInfo.lastInitMs) : 0) + " ms init</div></div>";
    page += F("</div><div class='button-row'><button type='button' class='btn btn-secondary' id='btnAmbientProbe'>Sensor neu pruefen</button></div>");
    page += "<div class='field-note' id='ambientStatusNote'>Sensorstatus: " + htmlEscape(ambientStatusText) + "</div>";
    page += F("</div></section>");
    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Animationen</h2><div class='panel-copy'>Seltene Features sind gebuendelt, aber nicht versteckt.</div></div></div><div class='field-inline'><div><strong>Logo bei Zuendung an</strong><span>Zeigt das M-Logo beim Einschalten.</span></div><label class='switch'><input type='checkbox' name='logoIgnOn'");
    page += checkedAttr(cfg.logoOnIgnitionOn);
    page += F("><span class='slider'></span></label></div><div class='field-inline'><div><strong>Logo bei Motorstart</strong><span>Signalisiert den aktiven Startvorgang.</span></div><label class='switch'><input type='checkbox' name='logoEngStart'");
    page += checkedAttr(cfg.logoOnEngineStart);
    page += F("><span class='slider'></span></label></div><div class='field-inline'><div><strong>Leaving Animation</strong><span>Kurzer Abschluss beim Abschalten.</span></div><label class='switch'><input type='checkbox' name='logoIgnOff'");
    page += checkedAttr(cfg.logoOnIgnitionOff);
    page += F("><span class='slider'></span></label></div><div class='field-inline'><div><strong>Gestensteuerung</strong><span>APDS-9960 links/rechts wechselt die vier LED-Modi ueber den Shared-I2C-Bus 47/48.</span></div><label class='switch'><input type='checkbox' name='gestureControlEnabled'");
    page += checkedAttr(cfg.gestureControlEnabled);
    page += F("><span class='slider'></span></label></div><div class='field-inline'><div><strong>Session-Transition-Effekte</strong><span>One-Shot LED-Effekte fuer SimHub/USB. Standardmaessig aus, damit RPM stabil bleibt.</span></div><label class='switch'><input type='checkbox' name='simFxLed'");
    page += checkedAttr(cfg.simSessionLedEffectsEnabled);
    page += F("><span class='slider'></span></label></div></section></div></div></form>");
    page += F("<div class='savebar' id='saveBar' data-dirty='0'><div><div class='savebar-title'><span>Status</span></div><div class='savebar-copy'>Konfiguration ist synchron.</div></div><div class='savebar-actions'><button type='button' class='btn btn-ghost' id='btnReset'>Zuruecksetzen</button><button type='button' class='btn btn-primary' id='btnSave' disabled>Speichern</button></div></div>");
    appendDashboardScript(page);
    appendShellFooter(page);
    return page;
}

namespace
{
    void appendSettingsScript(String &page)
    {
        page += F("<script>");
        page += F(R"JS(
const $ = (selector, scope=document) => scope.querySelector(selector);
const $$ = (selector, scope=document) => Array.from(scope.querySelectorAll(selector));
const settingsState = { dirty:false, pending:0, initial:null, refreshActive:false, wifiModalSsid:'', transportMode:'AUTO', wifiSuspended:false };
function setLoading(active){ settingsState.pending = Math.max(0, settingsState.pending + (active ? 1 : -1)); }
function beginRequest(){ setLoading(true); return () => setLoading(false); }
function serializeSettings(form){ const out = {}; $$('input,select,textarea', form).forEach((el) => { if(!el.name){ return; } out[el.name] = el.type === 'checkbox' ? (el.checked ? 'on' : '') : el.value; }); return out; }
function captureInitialSettingsState(){ const form = $('#settingsForm'); if(form){ settingsState.initial = serializeSettings(form); } }
function recomputeSettingsDirty(){ const form = $('#settingsForm'); if(!form){ return; } if(!settingsState.initial){ captureInitialSettingsState(); } const current = serializeSettings(form); let dirty = false; Object.keys(settingsState.initial).forEach((key) => { if(settingsState.initial[key] !== current[key]){ dirty = true; } }); Object.keys(current).forEach((key) => { if(settingsState.initial[key] !== current[key]){ dirty = true; } }); settingsState.dirty = dirty; const savebar = $('#settingsSaveBar'); if(savebar){ savebar.dataset.dirty = dirty ? '1' : '0'; $('.savebar-copy', savebar).textContent = dirty ? 'Netzwerk- und Telemetrie-Einstellungen sind geaendert.' : 'Alles ist synchron.'; } $('#settingsSave').disabled = !dirty; $('#settingsReset').disabled = !dirty; syncSavebarSpace('settingsSaveBar'); }
function markDirty(){ recomputeSettingsDirty(); }
function updateText(id, value){ const el = document.getElementById(id); if(el && value !== undefined && value !== null){ el.textContent = String(value); } }
function setPill(id, tone, text){ const el = document.getElementById(id); if(!el){ return; } el.classList.remove('ok','warn','bad','neutral'); el.classList.add(tone || 'neutral'); el.textContent = text; }
function syncSavebarSpace(id){ const bar = document.getElementById(id); if(!bar){ return; } const space = Math.ceil(bar.getBoundingClientRect().height + 28); document.documentElement.style.setProperty('--savebar-space', space + 'px'); }
function setButtonLoading(id, active, label){ const btn = document.getElementById(id); if(!btn){ return; } btn.classList.toggle('loading', !!active); btn.disabled = !!active; if(active){ btn.dataset.label = btn.dataset.label || btn.textContent; btn.innerHTML = '<span class=\"spinner\"></span><span>' + (label || btn.dataset.label) + '</span>'; } else { btn.textContent = btn.dataset.label || btn.textContent; } }
function setDevSection(){ $('#devSection')?.classList.toggle('hidden', !$('#devModeToggle')?.checked); }
function saveSettings(){ const form = $('#settingsForm'); if(!form){ return Promise.resolve(); } const fd = new FormData(form); const params = new URLSearchParams(); fd.forEach((value, key) => params.append(key, value)); const done = beginRequest(); setButtonLoading('settingsSave', true, 'Speichere'); return fetch('/settings', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:params.toString() }).then(() => { captureInitialSettingsState(); recomputeSettingsDirty(); $('#savedToast')?.classList.remove('hidden'); }).finally(() => { setButtonLoading('settingsSave', false); done(); }); }
function setVehicleStatus(text, error){ updateText('vehicleStatus', text); updateText('settingsError', error || ''); }
function telemetryTransportMode(data){ if(data?.simTransportMode){ return data.simTransportMode; } if(data?.simTransport === 'USB'){ return 'USB_ONLY'; } if(data?.simTransport === 'NETWORK'){ return 'NETWORK_ONLY'; } return 'AUTO'; }
function friendlyWifiMode(mode){ if(mode === 'STA_ONLY'){ return 'Nur Heim-WLAN'; } if(mode === 'STA_WITH_AP_FALLBACK'){ return 'WLAN + AP Fallback'; } return 'Nur Access Point'; }
function updateStatus(data){
  if(!data){ return; }
  const transportMode = telemetryTransportMode(data);
  settingsState.transportMode = transportMode;
  settingsState.wifiSuspended = !!data.wifiSuspended;
  const usbOnly = transportMode === 'USB_ONLY';
  const networkOnly = transportMode === 'NETWORK_ONLY';
  const autoMode = transportMode === 'AUTO';
  const activeTelemetry = data.activeTelemetry || 'Keine';
  const fallbackSuffix = autoMode && data.telemetryFallback ? ' Fallback' : '';
  let simTone = data.simHubConfigured ? 'warn' : 'bad';
  let simText = data.simHubState || 'Deaktiviert';
  if(usbOnly){
    simTone = data.usbState === 'USB live' ? 'ok' : (data.usbConnected ? 'warn' : 'bad');
    simText = data.usbState || 'USB wartet';
  } else if(networkOnly){
    simTone = data.simHubState === 'Live' ? 'ok' : (data.simHubConfigured ? 'warn' : 'bad');
    simText = data.simHubState || 'Netzwerk wartet';
  } else if(activeTelemetry === 'USB Sim'){
    simTone = data.usbState === 'USB live' ? 'ok' : 'warn';
    simText = data.usbState || 'USB aktiv';
  } else if(activeTelemetry === 'SimHub'){
    simTone = data.simHubState === 'Live' ? 'ok' : 'warn';
    simText = (data.simHubState || 'Netzwerk') + fallbackSuffix;
  } else if(activeTelemetry === 'OBD'){
    simTone = 'warn';
    simText = data.telemetryFallback ? 'OBD Fallback' : 'OBD aktiv';
  }
  setPill('settingsTelemetryPill', activeTelemetry === 'USB Sim' ? 'ok' : (activeTelemetry === 'SimHub' ? 'neutral' : (activeTelemetry === 'OBD' ? 'warn' : 'bad')), activeTelemetry + fallbackSuffix);
  setPill('settingsSimHubPill', simTone, simText);
  updateText('activeTelemetryValue', activeTelemetry + fallbackSuffix);
  updateText('simHubStateValue', simText);
  updateText('vehicleModel', data.vehicleModel || 'Noch kein Modell');
  updateText('vehicleVin', data.vehicleVin || 'Noch keine VIN');
  updateText('vehicleDiag', data.vehicleDiag || 'Keine Diagnose');
  if(usbOnly && data.wifiSuspended){
    updateText('wifiModeValue', 'USB Bridge');
    updateText('wifiIpValue', data.usbHost || 'USB lokal');
  }
  if(data.vehicleInfoRequestRunning){
    setVehicleStatus('Abruf laeuft', '');
  } else if(data.vehicleInfoReady){
    setVehicleStatus(data.vehicleInfoAge <= 1 ? 'Gerade aktualisiert' : ('Letztes Update vor ' + data.vehicleInfoAge + 's'), '');
  } else {
    setVehicleStatus('Noch keine Daten', '');
  }
  if(settingsState.refreshActive && !data.vehicleInfoRequestRunning){
    settingsState.refreshActive = false;
    if(data.vehicleInfoReady){
      setVehicleStatus('Gerade aktualisiert', '');
    } else {
      setVehicleStatus('Sync nicht moeglich', 'Keine Antwort vom Fahrzeug.');
    }
  }
}
function fetchStatus(){ fetch('/status').then((r) => r.json()).then(updateStatus).catch(() => {}); }
function wifiModeValue(){ return $('#wifiMode')?.value || '0'; }
function updateWifiPersistFields(ssid, password){ if($('#staSsid') && ssid !== undefined){ $('#staSsid').value = ssid; } if($('#staPassword') && password !== undefined){ $('#staPassword').value = password; } markDirty(); }
function renderWifiResults(data){ const wrapper = $('#wifiResults'); const empty = $('#wifiScanEmpty'); const list = $('#wifiResultsList'); if(!wrapper || !empty || !list){ return; } const results = (data && data.scanResults) || []; list.innerHTML = ''; results.forEach((item) => { const button = document.createElement('button'); button.type = 'button'; button.className = 'device-item'; button.innerHTML = '<div class=\"device-item-head\"><div><div class=\"device-name\">' + (item.ssid || '(versteckt)') + '</div><div class=\"device-meta\">' + item.rssi + ' dBm</div></div><span class=\"pill neutral\">Verbinden</span></div>'; button.addEventListener('click', () => openWifiModal(item.ssid || '')); list.appendChild(button); }); empty.classList.toggle('hidden', results.length !== 0 || !!data?.scanRunning); wrapper.classList.toggle('hidden', results.length === 0 && !data?.scanRunning); }
function updateWifiUi(data){ if(!data){ return; } const usbOnly = settingsState.transportMode === 'USB_ONLY' && settingsState.wifiSuspended; const summary = data.staConnected ? (data.currentSsid || 'Verbunden') : (data.staConnecting ? 'Verbindung laeuft' : (usbOnly ? 'USB-only aktiv, Web-UI bleibt erreichbar' : 'Offline')); updateText('wifiSummaryValue', summary); updateText('wifiModeValue', usbOnly ? 'USB Bridge + WLAN' : friendlyWifiMode(data.mode || 'AP_ONLY')); updateText('wifiIpValue', data.ip || data.apIp || '-'); setPill('settingsWifiPill', usbOnly ? 'neutral' : (data.staConnected ? 'ok' : (data.apActive || data.staConnecting ? 'warn' : 'bad')), usbOnly ? 'WLAN verfuegbar' : (data.staConnected ? 'WLAN online' : (data.apActive ? 'AP aktiv' : 'WLAN offline'))); updateText('wifiScanStatus', data.scanRunning ? 'Netzwerksuche laeuft' : (data.staConnecting ? 'Verbindung wird aufgebaut' : (data.staLastError || 'Bereit'))); if(data.currentSsid && !$('#staSsid').value){ $('#staSsid').value = data.currentSsid; } renderWifiResults(data); }
function fetchWifiStatus(){ fetch('/wifi/status').then((r) => r.json()).then(updateWifiUi).catch(() => {}); }
function openWifiModal(ssid){ settingsState.wifiModalSsid = ssid; updateText('wifiModalSsid', ssid || 'Netzwerk'); $('#wifiModalPassword').value = ''; $('#wifiPasswordModal').classList.remove('hidden'); $('#wifiModalPassword').focus(); }
function closeWifiModal(){ $('#wifiPasswordModal').classList.add('hidden'); }
function submitWifiModal(){ const ssid = settingsState.wifiModalSsid || $('#staSsid')?.value || ''; const password = $('#wifiModalPassword')?.value || ''; if(!ssid){ return; } updateWifiPersistFields(ssid, password); const done = beginRequest(); setButtonLoading('wifiScanBtn', true, 'Verbinde'); fetch('/wifi/connect', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password) + '&mode=' + encodeURIComponent(wifiModeValue()) }).finally(() => { setButtonLoading('wifiScanBtn', false); closeWifiModal(); fetchWifiStatus(); done(); }); }
function startWifiScan(){ const done = beginRequest(); setButtonLoading('wifiScanBtn', true, 'Suche'); fetch('/wifi/scan', { method:'POST' }).finally(() => { setButtonLoading('wifiScanBtn', false); fetchWifiStatus(); done(); }); }
function disconnectWifi(){ const done = beginRequest(); fetch('/wifi/disconnect', { method:'POST' }).finally(() => { fetchWifiStatus(); done(); }); }
function renderBleResults(data){ const wrapper = $('#bleResults'); const list = $('#bleResultsList'); const empty = $('#bleScanEmpty'); if(!wrapper || !list || !empty){ return; } const results = (data && data.results) || []; list.innerHTML = ''; results.forEach((item) => { const button = document.createElement('button'); button.type = 'button'; button.className = 'device-item' + (data.connectInProgress ? ' disabled' : ''); button.innerHTML = '<div class=\"device-item-head\"><div><div class=\"device-name\">' + (item.name || '(unbekannt)') + '</div><div class=\"device-meta\">' + (item.addr || '') + '</div></div><span class=\"pill ' + (data.connectInProgress ? 'warn' : 'neutral') + '\">' + (data.connectInProgress ? 'Verbinde' : 'Koppeln') + '</span></div>'; if(!data.connectInProgress){ button.addEventListener('click', () => requestBleConnect(item.addr || '', item.name || '')); } list.appendChild(button); }); empty.classList.toggle('hidden', results.length !== 0 || !!data?.scanRunning); wrapper.classList.toggle('hidden', results.length === 0 && !data?.scanRunning && !data?.manualFailed); }
function updateBleUi(data){ if(!data){ return; } setPill('settingsBlePill', data.connected ? 'ok' : (data.connectInProgress ? 'warn' : 'bad'), data.connected ? 'BLE / OBD online' : (data.connectInProgress ? 'Verbinde BLE' : 'BLE offline')); updateText('bleTargetName', data.targetName || '(unbekannt)'); updateText('bleTargetAddr', data.targetAddr || '–'); updateText('bleScanStatus', data.scanRunning ? 'Suche Geraete' : (data.connectInProgress ? 'Verbindung wird aufgebaut' : (data.manualFailed ? 'Verbindung fehlgeschlagen' : (data.scanAge >= 0 ? ('Letzter Scan vor ' + data.scanAge + 's') : 'Bereit')))); renderBleResults(data || {}); }
function fetchBleStatus(){ fetch('/ble/status').then((r) => r.json()).then(updateBleUi).catch(() => {}); }
function startBleScan(){ const done = beginRequest(); setButtonLoading('bleScanBtn', true, 'Suche'); fetch('/ble/scan', { method:'POST' }).finally(() => { setButtonLoading('bleScanBtn', false); fetchBleStatus(); done(); }); }
function requestBleConnect(address, name){ const done = beginRequest(); fetch('/ble/connect-device', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'address=' + encodeURIComponent(address) + '&name=' + encodeURIComponent(name) + '&attempts=1' }).finally(() => { fetchBleStatus(); done(); }); }
function refreshVehicle(){ settingsState.refreshActive = true; setVehicleStatus('Abruf laeuft', ''); const done = beginRequest(); setButtonLoading('btnVehicleRefresh', true, 'Sync'); fetch('/settings/vehicle-refresh', { method:'POST' }).then((r) => r.json()).then((data) => { if(!data || data.status !== 'started'){ settingsState.refreshActive = false; setVehicleStatus('Sync nicht moeglich', data && data.reason === 'no-connection' ? 'Keine OBD-Verbindung vorhanden.' : 'Sync konnte nicht gestartet werden.'); } }).catch(() => { settingsState.refreshActive = false; setVehicleStatus('Sync fehlgeschlagen', 'Sync fehlgeschlagen.'); }).finally(() => { setButtonLoading('btnVehicleRefresh', false); done(); }); }
function appendConsoleLine(text){ const box = $('#obdConsole'); if(!box || !text){ return; } const line = document.createElement('div'); const normalized = String(text).trim(); let className = 'console-line rx'; if(normalized.startsWith('>')){ className = 'console-line tx'; } else if(normalized.startsWith('!')){ className = 'console-line err'; } line.className = className; line.textContent = normalized; box.appendChild(line); box.scrollTop = box.scrollHeight; }
function sendObdCommand(){ const input = $('#obdCmdInput'); const cmd = input?.value.trim(); if(!cmd){ return; } appendConsoleLine('> ' + cmd); const done = beginRequest(); setButtonLoading('obdSendBtn', true, 'Sende'); fetch('/dev/obd-send', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'cmd=' + encodeURIComponent(cmd) }).then((r) => r.json()).then((data) => { if(data?.lastObd){ appendConsoleLine('< ' + data.lastObd); } }).catch(() => appendConsoleLine('! Fehler beim Senden')).finally(() => { setButtonLoading('obdSendBtn', false); done(); }); }
function refreshAdvancedDisplay(){ const done = beginRequest(); fetch('/dev/display-status').then((r) => r.json()).then((data) => { updateText('advDisplayReady', data.ready ? 'Bereit' : 'Nicht bereit'); updateText('advDisplayTouch', data.touchReady ? 'Touch aktiv' : 'Touch fehlt'); updateText('advDisplayLastError', data.lastError || 'Kein Fehler'); }).finally(done); }
function initSettings(){ captureInitialSettingsState(); recomputeSettingsDirty(); syncSavebarSpace('settingsSaveBar'); setDevSection(); $$('input,select,textarea', $('#settingsForm')).forEach((el) => { el.addEventListener('input', () => { markDirty(); syncSavebarSpace('settingsSaveBar'); }); el.addEventListener('change', () => { markDirty(); syncSavebarSpace('settingsSaveBar'); }); }); $('#settingsSave')?.addEventListener('click', () => saveSettings().finally(() => syncSavebarSpace('settingsSaveBar'))); $('#settingsReset')?.addEventListener('click', () => window.location.reload()); $('#devModeToggle')?.addEventListener('change', () => { setDevSection(); markDirty(); syncSavebarSpace('settingsSaveBar'); }); $('#wifiScanBtn')?.addEventListener('click', startWifiScan); $('#wifiDisconnectBtn')?.addEventListener('click', disconnectWifi); $('#wifiModalCancel')?.addEventListener('click', closeWifiModal); $('#wifiModalConnect')?.addEventListener('click', submitWifiModal); $('#bleScanBtn')?.addEventListener('click', startBleScan); $('#btnVehicleRefresh')?.addEventListener('click', refreshVehicle); $('#obdSendBtn')?.addEventListener('click', sendObdCommand); $('#obdCmdInput')?.addEventListener('keydown', (ev) => { if(ev.key === 'Enter'){ ev.preventDefault(); sendObdCommand(); } }); $('#obdClearBtn')?.addEventListener('click', () => { $('#obdConsole').innerHTML = ''; }); $('#btnDisplayStatusAdvanced')?.addEventListener('click', refreshAdvancedDisplay); $('#btnDisplayBarsAdvanced')?.addEventListener('click', () => { const done = beginRequest(); fetch('/dev/display-pattern', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'pattern=bars' }).finally(() => { refreshAdvancedDisplay(); done(); }); }); $('#btnDisplayGridAdvanced')?.addEventListener('click', () => { const done = beginRequest(); fetch('/dev/display-pattern', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'pattern=grid' }).finally(() => { refreshAdvancedDisplay(); done(); }); }); $('#btnDisplayLogoAdvanced')?.addEventListener('click', () => { const done = beginRequest(); fetch('/dev/display-logo', { method:'POST' }).finally(() => { refreshAdvancedDisplay(); done(); }); }); window.addEventListener('resize', () => syncSavebarSpace('settingsSaveBar')); fetchStatus(); fetchWifiStatus(); fetchBleStatus(); refreshAdvancedDisplay(); setInterval(fetchStatus, 2200); setInterval(fetchWifiStatus, 2800); setInterval(fetchBleStatus, 3200); }
document.addEventListener('DOMContentLoaded', initSettings);
)JS");
        page += F("</script>");
    }
}

String buildSettingsPage(bool savedNotice)
{
    const WifiStatus wifi = getWifiStatus();
    const DisplayDebugInfo displayInfo = displayGetDebugInfo();

    String page;
    page.reserve(26000);
    appendShellHead(page, F("ShiftLight Verbindung"), true);
    page += F("<section class='hero'><div class='hero-card hero-card--accent'><div class='hero-head'><div><div class='hero-kicker'>Verbindung & Telemetrie</div><div class='hero-title'>Netzwerk, SimHub und Fahrzeug</div><div class='hero-sub'>Alle Verbindungswege bleiben funktional, sind aber klarer gruppiert und mobil deutlich ruhiger.</div></div><div class='status-list'>");
    page += "<span class='pill neutral' id='settingsTelemetryPill'>" + htmlEscape(activeTelemetryLabel()) + "</span>";
    page += "<span class='pill " + String(wifi.staConnected ? "ok" : (wifi.apActive ? "warn" : "bad")) + "' id='settingsWifiPill'>" + (wifi.staConnected ? F("WLAN online") : (wifi.apActive ? F("AP aktiv") : F("WLAN offline"))) + "</span>";
    page += "<span class='pill " + String(g_connected ? "ok" : (g_bleConnectInProgress ? "warn" : "bad")) + "' id='settingsBlePill'>" + (g_connected ? F("BLE online") : (g_bleConnectInProgress ? F("BLE verbindet") : F("BLE offline"))) + "</span>";
    page += F("</div></div><div class='badge-grid'>");
    page += "<div class='badge-card'><span>Aktive Quelle</span><strong id='activeTelemetryValue'>" + htmlEscape(activeTelemetryLabel()) + "</strong></div>";
    page += "<div class='badge-card'><span>SimHub</span><strong id='simHubStateValue'>" + htmlEscape(simHubStateLabel()) + "</strong></div>";
    page += "<div class='badge-card'><span>WLAN Modus</span><strong id='wifiModeValue'>" + htmlEscape(wifiModeLabel(wifi.mode)) + "</strong></div>";
    page += "<div class='badge-card'><span>Aktuelle IP</span><strong id='wifiIpValue'>" + htmlEscape(currentIpString()) + "</strong></div></div></div>";
    page += F("<div class='hero-card'><div class='hero-head'><div><div class='hero-kicker'>Hinweis</div><div class='hero-title'>Sim / PC Modus</div><div class='hero-sub'>Netzwerk-SimHub bleibt moeglich. Fuer USB-only dient die lokale PC-Bridge als exklusiver Transport, Auto darf sauber auf Netzwerk ausweichen.</div></div></div><div class='callout'><strong>USB-only</strong> sperrt jetzt nur noch den Sim-Transport auf USB. Die Web-UI ueber WLAN bleibt verfuegbar, damit Setup und Debug nicht abbrechen.</div></div></section>");

    page += F("<form id='settingsForm' action='/settings' method='POST'><div class='app-grid'><section class='panel stack' id='telemetry-settings'><div class='panel-head'><div><h2 class='panel-title'>Telemetrie</h2><div class='panel-copy'>Quelle, SimHub Host und Polling sind zentral gebuendelt.</div></div></div><div class='field-grid'>");
    page += F("<div class='field'><label for='telemetryPreference'>Bevorzugte Quelle</label><select id='telemetryPreference' name='telemetryPreference'>");
    page += "<option value='0'" + selectedAttr(cfg.telemetryPreference == TelemetryPreference::Auto) + ">Automatisch (USB / Sim bevorzugen, sonst OBD)</option>";
    page += "<option value='1'" + selectedAttr(cfg.telemetryPreference == TelemetryPreference::Obd) + ">Nur OBD / BLE</option>";
    page += "<option value='2'" + selectedAttr(cfg.telemetryPreference == TelemetryPreference::SimHub) + ">Nur Sim / PC</option>";
    page += F("</select></div>");
    page += F("<div class='field'><label for='simTransport'>Sim Link</label><select id='simTransport' name='simTransport'>");
    page += "<option value='0'" + selectedAttr(cfg.simTransportPreference == SimTransportPreference::Auto) + ">Automatisch (USB bevorzugen, sonst Netzwerk)</option>";
    page += "<option value='1'" + selectedAttr(cfg.simTransportPreference == SimTransportPreference::UsbSerial) + ">USB only / Serial Bridge</option>";
    page += "<option value='2'" + selectedAttr(cfg.simTransportPreference == SimTransportPreference::Network) + ">Network only / SimHub API</option>";
    page += F("</select><div class='field-note'>Auto darf zwischen USB und Netzwerk wechseln. USB-only fixiert nur die Telemetrie auf USB, laesst die Web-UI ueber WLAN aber erreichbar.</div></div>");
    page += "<div class='field'><label for='simHubHost'>SimHub Host / PC-IP</label><input type='text' id='simHubHost' name='simHubHost' placeholder='z.B. 192.168.178.50' value='" + htmlEscape(cfg.simHubHost) + "'><div class='field-note'>Normalerweise die IP des SimHub-PCs im lokalen Netzwerk.</div></div>";
    page += "<div class='field-grid two'><div class='field'><label for='simHubPort'>SimHub Port</label><input type='number' id='simHubPort' name='simHubPort' min='1' max='65535' value='" + String(cfg.simHubPort) + "'></div><div class='field'><label for='simHubPollMs'>Poll Intervall (ms)</label><input type='number' id='simHubPollMs' name='simHubPollMs' min='25' max='1000' value='" + String(cfg.simHubPollMs) + "'></div></div>";
    page += F("<div class='field' id='display-settings'><label for='displayFocus'>Fahr-Layout auf dem Display</label><select id='displayFocus' name='displayFocus'>");
    page += "<option value='0'" + selectedAttr(cfg.uiDisplayFocus == DisplayFocusMetric::Rpm) + ">RPM gross</option>";
    page += "<option value='1'" + selectedAttr(cfg.uiDisplayFocus == DisplayFocusMetric::Gear) + ">Gang gross</option>";
    page += "<option value='2'" + selectedAttr(cfg.uiDisplayFocus == DisplayFocusMetric::Speed) + ">Geschwindigkeit gross</option>";
    page += F("</select><div class='field-note'>Steuert die grosse Hauptanzeige auf dem ESP-Display waehrend der Fahrt.</div></div></div></section>");

    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>WLAN</h2><div class='panel-copy'>Status, Quick Connect und persistente Zugangsdaten in einem Bereich.</div></div></div>");
    page += F("<div class='field'><label for='wifiMode'>WLAN Modus</label><select id='wifiMode' name='wifiMode'>");
    page += "<option value='0'" + selectedAttr(cfg.wifiMode == AP_ONLY) + ">Access Point (nur AP)</option>";
    page += "<option value='1'" + selectedAttr(cfg.wifiMode == STA_ONLY) + ">Heim-WLAN (nur STA)</option>";
    page += "<option value='2'" + selectedAttr(cfg.wifiMode == STA_WITH_AP_FALLBACK) + ">Heim-WLAN + AP Fallback</option></select><div class='field-note'>Speichern startet WLAN kurz neu. Schnell-Aktionen unten arbeiten direkt gegen den laufenden WiFi-Manager.</div></div>";
    page += "<div class='info-list'><div class='info-row'><div class='info-label'>Aktuelles WLAN</div><div class='info-value' id='wifiSummaryValue'>" + htmlEscape(staSummary(wifi)) + "</div></div><div class='info-row'><div class='info-label'>IP Adresse</div><div class='info-value mono' id='wifiIpValue'>" + htmlEscape(currentIpString()) + "</div></div></div>";
    page += F("<div class='button-row'><button type='button' class='btn btn-secondary' id='wifiScanBtn'>Netzwerke suchen</button><button type='button' class='btn btn-secondary' id='wifiDisconnectBtn'>Trennen</button></div><div class='field-note' id='wifiScanStatus'>Bereit</div><div id='wifiResults' class='hidden'><div id='wifiResultsList' class='device-list'></div><div id='wifiScanEmpty' class='device-empty'>Keine Netzwerke gefunden.</div></div>");
    page += "<div class='field-grid two'><div class='field'><label for='staSsid'>Gespeichertes STA SSID</label><input type='text' id='staSsid' name='staSsid' value='" + htmlEscape(cfg.staSsid) + "'></div>";
    page += "<div class='field'><label for='staPassword'>STA Passwort</label><input type='password' id='staPassword' name='staPassword' value='" + htmlEscape(cfg.staPassword) + "' placeholder='Nur aendern wenn noetig'></div>";
    page += "<div class='field'><label for='apSsid'>AP SSID</label><input type='text' id='apSsid' name='apSsid' value='" + htmlEscape(cfg.apSsid) + "'></div>";
    page += "<div class='field'><label for='apPassword'>AP Passwort</label><input type='password' id='apPassword' name='apPassword' value='" + htmlEscape(cfg.apPassword) + "'></div></div></section></div>";

    page += F("<div class='panel-grid two'><section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Fahrzeug</h2><div class='panel-copy'>Synchronisation und Diagnose bleiben kompakt sichtbar.</div></div></div><div class='info-list'>");
    page += "<div class='info-row'><div class='info-label'>Fahrzeug</div><div class='info-value' id='vehicleModel'>" + htmlEscape(readVehicleModel()) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>VIN</div><div class='info-value mono' id='vehicleVin'>" + htmlEscape(readVehicleVin()) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Diagnose</div><div class='info-value' id='vehicleDiag'>" + htmlEscape(readVehicleDiagStatus()) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Status</div><div class='info-value' id='vehicleStatus'>Noch keine Daten</div></div></div><div class='button-row'><button type='button' class='btn btn-secondary' id='btnVehicleRefresh'>Fahrzeugdaten neu laden</button></div><div class='field-note' id='settingsError'></div></section>";

    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Bluetooth / OBD</h2><div class='panel-copy'>Scannen, koppeln und aktuellen Zieladapter sehen.</div></div></div><div class='info-list'>");
    page += "<div class='info-row'><div class='info-label'>Zielgeraet</div><div class='info-value' id='bleTargetName'>" + htmlEscape(g_currentTargetName.isEmpty() ? String(F("(unbekannt)")) : g_currentTargetName) + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>MAC</div><div class='info-value mono' id='bleTargetAddr'>" + htmlEscape(g_currentTargetAddr.isEmpty() ? String(F("-")) : g_currentTargetAddr) + "</div></div></div>";
    page += F("<div class='button-row'><button type='button' class='btn btn-secondary' id='bleScanBtn'>BLE Geraete suchen</button></div><div class='field-note' id='bleScanStatus'>Bereit</div><div id='bleResults' class='hidden'><div id='bleResultsList' class='device-list'></div><div id='bleScanEmpty' class='device-empty'>Keine Geraete gefunden.</div></div></section></div>");

    page += F("<section class='panel stack'><div class='panel-head'><div><h2 class='panel-title'>Entwickler & Diagnose</h2><div class='panel-copy'>Weniger prominent, aber weiter direkt erreichbar.</div></div></div><div class='field-inline'><div><strong>Entwicklermodus</strong><span>Blendet OBD-Konsole und tiefere Displaydiagnose ein.</span></div><label class='switch'><input type='checkbox' id='devModeToggle' name='devMode'");
    page += checkedAttr(g_devMode);
    page += F("><span class='slider'></span></label></div><div id='devSection'");
    page += g_devMode ? String() : String(F(" class='hidden'"));
    page += F("><details class='panel details' style='padding:16px'><summary><div><h3 class='panel-title'>Display Diagnose</h3><div class='panel-copy'>Display-Tests fuer Panel, Touch und LVGL.</div></div></summary><div class='stack' style='margin-top:14px'><div class='info-list'>");
    page += "<div class='info-row'><div class='info-label'>Display</div><div class='info-value' id='advDisplayReady'>" + String(displayInfo.ready ? "Bereit" : "Nicht bereit") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Touch</div><div class='info-value' id='advDisplayTouch'>" + String(displayInfo.touchReady ? "Touch aktiv" : "Touch fehlt") + "</div></div>";
    page += "<div class='info-row'><div class='info-label'>Fehler</div><div class='info-value' id='advDisplayLastError'>" + htmlEscape(displayInfo.lastError.isEmpty() ? String(F("Kein Fehler")) : displayInfo.lastError) + "</div></div></div><div class='button-row'><button type='button' class='btn btn-secondary' id='btnDisplayStatusAdvanced'>Status aktualisieren</button><button type='button' class='btn btn-secondary' id='btnDisplayBarsAdvanced'>Farb-Balken</button><button type='button' class='btn btn-secondary' id='btnDisplayGridAdvanced'>Raster</button><button type='button' class='btn btn-secondary' id='btnDisplayLogoAdvanced'>Logo</button></div></div></details>";
    page += F("<details class='panel details' style='padding:16px'><summary><div><h3 class='panel-title'>OBD Konsole</h3><div class='panel-copy'>Direkte AT- und PID-Kommandos fuer Debug und Fehlersuche.</div></div></summary><div class='stack' style='margin-top:14px'><div id='obdConsole' class='console'></div><div class='field-grid two'><div class='field'><label for='obdCmdInput'>Befehl</label><input type='text' id='obdCmdInput' placeholder='z.B. 010C oder ATZ'></div><div class='button-row'><button type='button' class='btn btn-secondary' id='obdSendBtn'>Senden</button><button type='button' class='btn btn-ghost' id='obdClearBtn'>Leeren</button></div></div></div></details></div></section></form>");

    page += F("<div class='savebar' id='settingsSaveBar' data-dirty='0'><div><div class='savebar-title'><span>Status</span></div><div class='savebar-copy'>Alles ist synchron.</div></div><div class='savebar-actions'><button type='button' class='btn btn-ghost' id='settingsReset'>Zuruecksetzen</button><button type='button' class='btn btn-primary' id='settingsSave' disabled>Speichern</button></div></div>");
    page += "<div class='toast" + String(savedNotice ? "" : " hidden") + "' id='savedToast'>Einstellungen wurden gespeichert.</div>";
    page += F("<div id='wifiPasswordModal' class='hidden'><div class='panel' style='max-width:420px;margin:24px auto'><div class='panel-head'><div><h2 class='panel-title'>Mit WLAN verbinden</h2><div class='panel-copy'>Passwort fuer <span id='wifiModalSsid'></span></div></div></div><div class='field'><label for='wifiModalPassword'>Passwort</label><input type='password' id='wifiModalPassword' placeholder='WLAN Passwort'></div><div class='button-row'><button type='button' class='btn btn-secondary' id='wifiModalCancel'>Abbrechen</button><button type='button' class='btn btn-primary' id='wifiModalConnect'>Verbinden</button></div></div></div>");

    appendSettingsScript(page);
    appendShellFooter(page);
    return page;
}

#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ble_obd.h"
#include "config.h"
#include "led_bar.h"
#include "logo_anim.h"
#include "vehicle_info.h"
#include "state.h"
#include "display.h"

namespace
{
    WebServer server(80);

    String colorToHex(const RgbColor &color)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", color.r, color.g, color.b);
        return String(buf);
    }

    RgbColor parseHexColor(const String &value, const RgbColor &fallback)
    {
        if (value.length() != 7 || value[0] != '#')
            return fallback;
        RgbColor c{};
        c.r = static_cast<uint8_t>(strtol(value.substring(1, 3).c_str(), nullptr, 16));
        c.g = static_cast<uint8_t>(strtol(value.substring(3, 5).c_str(), nullptr, 16));
        c.b = static_cast<uint8_t>(strtol(value.substring(5, 7).c_str(), nullptr, 16));
        return c;
    }

    void handleDevObdSend()
    {
        g_lastHttpMs = millis();

        if (!g_connected)
        {
            server.send(400, "text/plain", "Nicht mit OBD verbunden.");
            return;
        }

        if (!server.hasArg("cmd"))
        {
            server.send(400, "text/plain", "Parameter 'cmd' fehlt.");
            return;
        }

        String cmd = server.arg("cmd");
        cmd.trim();
        if (cmd.length() == 0)
        {
            server.send(400, "text/plain", "Befehl ist leer.");
            return;
        }

        // Command ohne CR senden – sendObdCommand hängt CR dran
        sendObdCommand(cmd);
        server.send(200, "text/plain", "OK");
    }

    String safeLabel(const String &value, const String &fallback)
    {
        String trimmed = value;
        trimmed.trim();
        if (trimmed.isEmpty())
            return fallback;
        return trimmed;
    }

    String jsonEscape(const String &input)
    {
        String out;
        out.reserve(input.length());
        for (size_t i = 0; i < input.length(); ++i)
        {
            char c = input[i];
            switch (c)
            {
            case '\\':
            case '"':
                out += '\\';
                out += c;
                break;
            case '\n':
                out += F("\\n");
                break;
            case '\r':
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    String htmlEscape(const String &input)
    {
        String out;
        out.reserve(input.length());
        for (size_t i = 0; i < input.length(); ++i)
        {
            char c = input[i];
            switch (c)
            {
            case '&':
                out += F("&amp;");
                break;
            case '<':
                out += F("&lt;");
                break;
            case '>':
                out += F("&gt;");
                break;
            case '\"':
                out += F("&quot;");
                break;
            case '\'':
                out += F("&#39;");
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    int clampInt(int v, int lo, int hi)
    {
        if (v < lo)
            return lo;
        if (v > hi)
            return hi;
        return v;
    }

    void enforceOrder(int &g, int &y, int &b)
    {
        g = clampInt(g, 0, 100);
        if (y < g)
            y = g;
        y = clampInt(y, 0, 100);
        if (b < y)
            b = y;
        b = clampInt(b, 0, 100);
    }

    String htmlPage()
    {
        String color1Name = safeLabel(cfg.greenLabel, F("Farbe 1"));
        String color2Name = safeLabel(cfg.yellowLabel, F("Farbe 2"));
        String color3Name = safeLabel(cfg.redLabel, F("Farbe 3"));

        String greenHex = colorToHex(cfg.greenColor);
        String yellowHex = colorToHex(cfg.yellowColor);
        String redHex = colorToHex(cfg.redColor);

        String page;
        page.reserve(19000);

        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        page += F("<title>ShiftLight Setup</title>");
        page += F(
            "<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;color:#eee;padding:16px;margin:0;}"
            "h1{font-size:20px;margin:0 0 12px 0;display:flex;align-items:center;justify-content:space-between;}"
            "label{display:block;margin-top:8px;}"
            "input,select{width:100%;padding:6px;margin-top:4px;border-radius:6px;border:1px solid #444;background:#222;color:#eee;}"
            "input[type=range]{padding:0;margin-top:4px;}"
            "button{margin-top:12px;width:100%;padding:10px;border:none;border-radius:6px;background:#0af;color:#000;font-weight:bold;font-size:14px;}"
            "button:disabled{background:#555;color:#888;}"
            ".btn-danger{background:#d33;color:#fff;}"
            ".row{margin-bottom:6px;}"
            ".small{font-size:12px;color:#aaa;}"
            ".section{margin-top:12px;padding:10px 12px;border-radius:8px;background:#181818;border:1px solid #333;}"
            ".section-title{font-weight:600;margin-bottom:8px;font-size:18px;letter-spacing:0.3px;}"
            ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".toggle-label{font-size:14px;}"
            ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
            ".switch input{opacity:0;width:0;height:0;}"
            ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#555;transition:.2s;border-radius:24px;}"
            ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;background:#fff;transition:.2s;border-radius:50%;}"
            ".switch input:checked + .slider{background:#0af;}"
            ".switch input:checked + .slider:before{transform:translateX(22px);}"
            ".status-line{font-size:12px;color:#ccc;margin-top:4px;}"
            ".spinner{display:inline-block;width:12px;height:12px;border-radius:50%;border:2px solid rgba(255,255,255,0.2);border-top-color:#0af;animation:spin 1s linear infinite;margin-left:6px;}"
            ".hidden{display:none;}"
            ".color-row{display:flex;justify-content:space-between;gap:12px;margin-top:10px;padding:10px;border-radius:8px;background:#151515;border:1px solid #222;}"
            ".color-swatch{flex:1;text-align:center;}"
            ".color-swatch-title{font-size:12px;color:#bbb;margin-bottom:4px;}"
            ".color-circle{padding:0;border-radius:50%;border:2px solid #444;background:#111;width:44px;height:44px;}"
            ".color-circle::-webkit-color-swatch{border:none;border-radius:50%;padding:0;}"
            ".color-circle::-moz-color-swatch{border:none;border-radius:50%;padding:0;}"
            ".color-name{margin-top:4px;font-size:11px;color:#aaa;}"
            "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
            ".range-group{margin-top:10px;padding-top:8px;border-top:1px solid #222;}"
            ".range-group:first-child{border-top:none;padding-top:0;}"
            ".disabled-field{opacity:0.5;}"
            ".led-preview-title{margin-top:10px;margin-bottom:4px;font-size:12px;color:#aaa;}"
            ".led-preview{display:flex;flex-wrap:nowrap;align-items:center;justify-content:center;padding:6px 4px;border-radius:6px;background:#151515;border:1px solid #222;}"
            ".led-dot{border-radius:50%;background:#333;flex:0 0 auto;}"
            "</style></head><body>");

        page += "<h1><span>ShiftLight Setup</span>";
        page += "<a href=\"/settings\" style='text-decoration:none;color:#0af;font-size:20px;'>⚙️</a></h1>";

        page += F("<form id='mainForm' method='POST' action='/save'>");

        // --- Allgemein ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Allgemein</div>");
        page += F("<label for='modeSelect'>Mode</label><select name='mode' id='modeSelect'>");
        page += "<option value='0'";
        if (cfg.mode == 0)
            page += " selected";
        page += ">Casual</option>";
        page += "<option value='1'";
        if (cfg.mode == 1)
            page += " selected";
        page += ">F1-Style</option>";
        page += "<option value='2'";
        if (cfg.mode == 2)
            page += " selected";
        page += ">Überempfindlich</option>";
        page += "</select>";

        page += F("<label for='brightness_slider'>Brightness (0-255)</label>");
        page += "<input type='range' min='0' max='255' value='" + String(cfg.brightness) + "' id='brightness_slider'>";
        page += "<div class='small'>Wert: <span id='bval'>" + String(cfg.brightness) + "</span></div>";
        page += "<input type='hidden' name='brightness' id='brightness' value='" + String(cfg.brightness) + "'>";

        page += F("<div class='color-row'>");
        page += F("<div class='color-swatch'><div class='color-swatch-title'>Low RPM</div>");
        page += "<input type='color' class='color-circle' id='greenColorInput' name='greenColor' value='" + greenHex + "'>";
        page += "<div class='color-name' id='color1Name'>" + htmlEscape(color1Name) + "</div>";
        page += "<input type='hidden' name='greenLabel' id='greenLabelHidden' value='" + htmlEscape(color1Name) + "'>";
        page += F("</div>");

        page += F("<div class='color-swatch'><div class='color-swatch-title'>Mid RPM</div>");
        page += "<input type='color' class='color-circle' id='yellowColorInput' name='yellowColor' value='" + yellowHex + "'>";
        page += "<div class='color-name' id='color2Name'>" + htmlEscape(color2Name) + "</div>";
        page += "<input type='hidden' name='yellowLabel' id='yellowLabelHidden' value='" + htmlEscape(color2Name) + "'>";
        page += F("</div>");

        page += F("<div class='color-swatch'><div class='color-swatch-title'>Shift / Warnung</div>");
        page += "<input type='color' class='color-circle' id='redColorInput' name='redColor' value='" + redHex + "'>";
        page += "<div class='color-name' id='color3Name'>" + htmlEscape(color3Name) + "</div>";
        page += "<input type='hidden' name='redLabel' id='redLabelHidden' value='" + htmlEscape(color3Name) + "'>";
        page += F("</div></div>");
        page += F("</div>");

        // --- Drehzahl-Bereich ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Drehzahl-Bereich</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>Auto-Scale Max RPM (benutze max gesehene Drehzahl)</span><label class='switch'>");
        page += "<input type='checkbox' id='autoscaleToggle' name='autoscale'";
        if (cfg.autoScaleMaxRpm)
            page += " checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='range-group' id='fixedMaxContainer'>");
        page += F("<label for='fixedMaxRpmInput'>Fixed Max RPM</label>");
        page += "<input type='number' id='fixedMaxRpmInput' name='fixedMaxRpm' min='1000' max='8000' value='" + String(cfg.fixedMaxRpm) + "'>";
        page += F("</div>");

        page += F("<div class='range-group'>");
        page += "<label class='rpm-label' id='greenEndLabel'><span class='rpm-label-title'>Low RPM</span><span class='rpm-label-range'>End (% von Max RPM)</span></label>";
        page += "<input type='range' name='greenEndPct' min='0' max='100' value='" + String(cfg.greenEndPct) + "' id='greenEndSlider' data-display='greenEndVal'>";
        page += "<div class='small'>Wert: <span id='greenEndVal'>" + String(cfg.greenEndPct) + "%</span></div></div>";

        page += F("<div class='range-group'>");
        page += "<label class='rpm-label' id='yellowEndLabel'><span class='rpm-label-title'>Mid RPM</span><span class='rpm-label-range'>End (% von Max RPM)</span></label>";
        page += "<input type='range' name='yellowEndPct' min='0' max='100' value='" + String(cfg.yellowEndPct) + "' id='yellowEndSlider' data-display='yellowEndVal'>";
        page += "<div class='small'>Wert: <span id='yellowEndVal'>" + String(cfg.yellowEndPct) + "%</span></div></div>";

        page += "<div class='range-group' id='blinkStartContainer'>";
        page += "<label class='rpm-label' id='blinkStartLabel'><span class='rpm-label-title'>Shift / Warnung</span><span class='rpm-label-range'>Start (% von Max RPM)</span></label>";
        page += "<input type='range' name='blinkStartPct' min='0' max='100' value='" + String(cfg.blinkStartPct) + "' id='blinkStartSlider' data-display='blinkStartVal'>";
        page += "<div class='small'>Wert: <span id='blinkStartVal'>" + String(cfg.blinkStartPct) + "%</span></div></div>";

        page += F("<div class='led-preview-title small'>LED-Vorschau</div>");
        page += "<div id='ledPreview' class='led-preview' data-led-count='" + String(NUM_LEDS) + "'></div>";
        // Test-Button jetzt im Block Drehzahl-Bereich
        page += F("<button type='button' id='btnTest'>Testlauf: RPM-Sweep</button>");
        page += F("</div>");

        // --- Coming-Home / Leaving ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Coming-Home / Leaving</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Zündung an</span><label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOn'";
        if (cfg.logoOnIgnitionOn)
            page += " checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Motorstart</span><label class='switch'>");
        page += "<input type='checkbox' name='logoEngStart'";
        if (cfg.logoOnEngineStart)
            page += " checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='toggle-row'><span class='toggle-label'>Leaving-Animation bei Zündung aus</span><label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOff'";
        if (cfg.logoOnIgnitionOff)
            page += " checked";
        page += "><span class='slider'></span></label></div>";
        page += F("</div>");

        // --- Mein Fahrzeug ---
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>Fahrzeug: <strong id='vehicleModel' data-base='" + htmlEscape(g_vehicleModel) + "'>" + htmlEscape(g_vehicleModel) + "</strong></div>";
        page += "<div class='row small'>VIN: <strong id='vehicleVin' data-base='" + htmlEscape(g_vehicleVin) + "'>" + htmlEscape(g_vehicleVin) + "</strong></div>";
        page += "<div class='row small'>Diagnose: <strong id='vehicleDiag' data-base='" + htmlEscape(g_vehicleDiagStatus) + "'>" + htmlEscape(g_vehicleDiagStatus) + "</strong></div>";
        page += F("</div>");

        if (g_devMode)
        {
            // --- OBD / Verbindung ---
            page += F("<div class='section'>");
            page += F("<div class='section-title'>OBD / Verbindung</div>");
            page += F("<div class='toggle-row'><span class='toggle-label'>OBD automatisch verbinden (Reconnect)</span><label class='switch'>");
            page += "<input type='checkbox' name='autoReconnect'";
            if (g_autoReconnect)
                page += " checked";
            page += "><span class='slider'></span></label></div>";
            page += F("<div class='status-line'>BLE-Status: <span id='bleStatus'>");
            page += g_connected ? "Verbunden" : "Getrennt";
            page += g_autoReconnect ? " (Auto-Reconnect AN)" : " (Auto-Reconnect AUS)";
            page += F("</span></div>");

            // Buttons jetzt in diesem Block:
            page += "<button type='button' id='btnConnect'";
            if (g_connected)
                page += " style='display:none'";
            page += ">Jetzt mit OBD verbinden</button>";
            page += "<button type='button' class='btn-danger' id='btnDisconnect'";
            if (!g_connected)
                page += " style='display:none'";
            page += ">OBD trennen</button>";

            page += F("</div>");

            // --- Display ---
            page += F("<div class='section'><div class='section-title'>Display</div>");
            page += F("<button type='button' id='btnDisplayLogo'>BMW Logo auf Display anzeigen</button>");
            page += F("<div class='small'>Zeigt kurz das BMW-Logo auf dem Display (nur im Entwicklermodus).</div></div>");

            // --- Debug ---
            page += F("<div class='section'><div class='section-title'>Debug<span id='debugSpinner' class='spinner hidden'></span></div>");
            page += F("<div class='row small'>Letzter TX: <span id='lastTx'>");
            page += htmlEscape(g_lastTxInfo);
            page += F("</span></div>");
            page += F("<div class='row small'>Letzte OBD-Zeile: <span id='lastObd'>");
            page += htmlEscape(g_lastObdInfo);
            page += F("</span></div>");
            // RPM/Max jetzt hier unter Debug
            page += F("<div class='row small'>Aktuelle RPM: <span id='rpmVal'>");
            page += String(g_currentRpm);
            page += F("</span> / Max gesehen: <span id='rpmMaxVal'>");
            page += String(g_maxSeenRpm);
            page += F("</span></div></div>");
        }

        // Save + Reset unten
        page += F("<button type='button' id='btnSave' disabled>Speichern</button>");
        page += F("<button type='button' class='btn-danger' id='btnReset' style='display:none'>Zurücksetzen</button>");
        page += F("</form>");

        page += "<div class='small' style='text-align:center;margin-top:16px;'>" + WiFi.softAPIP().toString() + "</div>";

        // --- Script ---
        page += F(
            "<script>");
        page += "const TEST_SWEEP_DURATION = " + String(TEST_SWEEP_DURATION) + ";";
        page += F("let saveDirty=false;"
                  "let initialMainState=null;"
                  "let pendingSpinner=0;"
                  "let lastSpinnerTs=0;"
                  "let statusTimer=null;"
                  "let dotIntervals={};"
                  "let ledBlinkState=false;"
                  "let lastLedBlinkTs=0;"
                  "let blinkPreviewActive=false;"
                  "let blinkPreviewEnd=0;"
                  "let testSweepActive=false;"
                  "let testSweepStart=0;"
                  "let previewTimerId=null;"

                  "function setAnimatedDots(el,loading){"
                  " if(!el) return;"
                  " const key=el.id;"
                  " if(loading){"
                  "  if(dotIntervals[key]) return;"
                  "  let step=0;"
                  "  dotIntervals[key]=setInterval(()=>{"
                  "    step=(step+1)%4;"
                  "    el.innerText=(el.dataset.base||'')+'.'.repeat(step);"
                  "  },400);"
                  " }else{"
                  "  if(dotIntervals[key]){clearInterval(dotIntervals[key]);dotIntervals[key]=null;}"
                  "  el.innerText=el.dataset.base||'';"
                  " }"
                  "}"

                  "function updateResetVisibility(){"
                  " const r=document.getElementById('btnReset');"
                  " if(r) r.style.display=saveDirty?'block':'none';"
                  "}"

                  "function captureInitialMainState(){"
                  " const form=document.getElementById('mainForm');"
                  " if(!form) return;"
                  " initialMainState={};"
                  " const elements=form.querySelectorAll('input,select,textarea');"
                  " elements.forEach(el=>{"
                  "   if(!el.name) return;"
                  "   let val;"
                  "   if(el.type==='checkbox'){"
                  "     val=el.checked?'on':'';"
                  "   }else{"
                  "     val=el.value;"
                  "   }"
                  "   initialMainState[el.name]=val;"
                  " });"
                  "}"

                  "function recomputeMainDirty(){"
                  " const form=document.getElementById('mainForm');"
                  " if(!form){saveDirty=false;return;}"
                  " if(!initialMainState) captureInitialMainState();"
                  " let changed=false;"
                  " const current={};"
                  " const elements=form.querySelectorAll('input,select,textarea');"
                  " elements.forEach(el=>{"
                  "   if(!el.name) return;"
                  "   let val;"
                  "   if(el.type==='checkbox'){"
                  "     val=el.checked?'on':'';"
                  "   }else{"
                  "     val=el.value;"
                  "   }"
                  "   current[el.name]=val;"
                  " });"
                  " for(const k in initialMainState){"
                  "   if(initialMainState[k]!==current[k]){"
                  "     changed=true;"
                  "     break;"
                  "   }"
                  " }"
                  " saveDirty=changed;"
                  " const b=document.getElementById('btnSave');"
                  " if(b) b.disabled=!changed;"
                  " updateResetVisibility();"
                  "}"

                  "function markDirty(){"
                  " recomputeMainDirty();"
                  "}"

                  "function onBrightnessChange(v){"
                  " document.getElementById('bval').innerText=v;"
                  " document.getElementById('brightness').value=v;"
                  " markDirty();"
                  " fetch('/brightness?val='+v).catch(()=>{});"
                  "}"

                  "function updateSliderDisplay(el){"
                  " const target=el.dataset.display;"
                  " const span=document.getElementById(target);"
                  " if(span) span.innerText=el.value+'%';"
                  "}"

                  "function enforceSliderOrder(changedId){"
                  " const g=document.getElementById('greenEndSlider');"
                  " const y=document.getElementById('yellowEndSlider');"
                  " const b=document.getElementById('blinkStartSlider');"
                  " if(!g||!y||!b) return;"
                  " let gv=parseInt(g.value||'0');"
                  " let yv=parseInt(y.value||'0');"
                  " let bv=parseInt(b.value||'0');"
                  " const sync=(el,val)=>{"
                  "   if(parseInt(el.value)!=val){"
                  "     el.value=val;"
                  "     updateSliderDisplay(el);"
                  "   }"
                  " };"
                  " if(changedId==='greenEndSlider'){"
                  "   if(yv<gv){yv=gv;sync(y,yv);}"
                  "   if(bv<yv){bv=yv;sync(b,bv);}"
                  " }else if(changedId==='yellowEndSlider'){"
                  "   if(yv<gv){gv=yv;sync(g,gv);}"
                  "   if(bv<yv){bv=yv;sync(b,bv);}"
                  " }else if(changedId==='blinkStartSlider'){"
                  "   if(bv<yv){yv=bv;sync(y,yv);}"
                  "   if(yv<gv){gv=yv;sync(g,gv);}"
                  " }"
                  "}"

                  "function updateAutoscaleUi(){"
                  " const chk=document.getElementById('autoscaleToggle');"
                  " const cont=document.getElementById('fixedMaxContainer');"
                  " const inp=document.getElementById('fixedMaxRpmInput');"
                  " if(!chk||!cont||!inp) return;"
                  " const on=chk.checked;"
                  " inp.disabled=on;"
                  " cont.classList.toggle('disabled-field',on);"
                  "}"

                  "function beginRequest(){"
                  " pendingSpinner++;"
                  " lastSpinnerTs=Date.now();"
                  " updateSpinnerVisibility();"
                  " return ()=>{"
                  "   pendingSpinner=Math.max(0,pendingSpinner-1);"
                  "   updateSpinnerVisibility();"
                  " };"
                  "}"

                  "function updateSpinnerVisibility(){"
                  " const sp=document.getElementById('debugSpinner');"
                  " if(!sp) return;"
                  " const idle=(Date.now()-lastSpinnerTs)>3000;"
                  " if(pendingSpinner<=0||idle){"
                  "   sp.classList.add('hidden');"
                  " }else{"
                  "   sp.classList.remove('hidden');"
                  " }"
                  "}"

                  "function postSimple(url){"
                  " const done=beginRequest();"
                  " fetch(url,{method:'POST'}).finally(done);"
                  "}"

                  "function getThresholds(){"
                  " const gv=parseInt(document.getElementById('greenEndSlider').value||'0');"
                  " const yv=parseInt(document.getElementById('yellowEndSlider').value||'0');"
                  " const bv=parseInt(document.getElementById('blinkStartSlider').value||'0');"
                  " let greenEnd=gv/100.0;"
                  " let yellowEnd=yv/100.0;"
                  " let blinkStart=bv/100.0;"
                  " if(greenEnd<0) greenEnd=0;"
                  " if(greenEnd>1) greenEnd=1;"
                  " if(yellowEnd<greenEnd) yellowEnd=greenEnd;"
                  " if(yellowEnd>1) yellowEnd=1;"
                  " if(blinkStart<yellowEnd) blinkStart=yellowEnd;"
                  " if(blinkStart>1) blinkStart=1;"
                  " return {greenEnd,yellowEnd,blinkStart};"
                  "}"

                  "function computeSimFraction(t){"
                  " if(t<0) t=0;"
                  " if(t>1) t=1;"
                  " let pct=0;"
                  " if(t<0.25){"
                  "   let tt=t/0.25;"
                  "   pct=Math.sin(tt*Math.PI);"
                  " }else if(t<0.70){"
                  "   let tt=(t-0.25)/0.45;"
                  "   if(tt<0) tt=0;"
                  "   if(tt>1) tt=1;"
                  "   pct=tt*tt*(3-2*tt);"
                  " }else{"
                  "   let tt=(t-0.70)/0.30;"
                  "   if(tt<0) tt=0;"
                  "   if(tt>1) tt=1;"
                  "   let base=1-tt;"
                  "   let wobble=0.05*Math.sin(tt*Math.PI*4.0);"
                  "   pct=base+wobble;"
                  " }"
                  " if(pct<0) pct=0;"
                  " if(pct>1) pct=1;"
                  " return pct;"
                  "}"

                  "function layoutLedDots(){"
                  " const cont=document.getElementById('ledPreview');"
                  " if(!cont) return;"
                  " const dots=cont.querySelectorAll('.led-dot');"
                  " if(!dots.length) return;"
                  " const w=cont.clientWidth;"
                  " const spacing=2;"
                  " const maxSize=Math.floor((w-spacing*(dots.length-1))/dots.length);"
                  " const size=Math.max(4,Math.min(14,maxSize));"
                  " dots.forEach(d=>{"
                  "   d.style.width=size+'px';"
                  "   d.style.height=size+'px';"
                  "   d.style.marginLeft=(spacing/2)+'px';"
                  "   d.style.marginRight=(spacing/2)+'px';"
                  " });"
                  "}"

                  "function initLedPreview(){"
                  " const cont=document.getElementById('ledPreview');"
                  " if(!cont) return;"
                  " const count=parseInt(cont.dataset.ledCount||'0');"
                  " cont.innerHTML='';"
                  " for(let i=0;i<count;i++){"
                  "   const d=document.createElement('div');"
                  "   d.className='led-dot';"
                  "   cont.appendChild(d);"
                  " }"
                  " layoutLedDots();"
                  " updateLedPreview();"
                  " window.addEventListener('resize',layoutLedDots);"
                  "}"

                  "function renderLedBarFraction(fraction,useBlink){"
                  " const cont=document.getElementById('ledPreview');"
                  " if(!cont) return;"
                  " const dots=cont.querySelectorAll('.led-dot');"
                  " const count=dots.length;"
                  " if(!count) return;"
                  " if(fraction<0) fraction=0;"
                  " if(fraction>1) fraction=1;"
                  " const modeVal=document.getElementById('modeSelect').value;"
                  " const mode=parseInt(modeVal||'0');"
                  " const thr=getThresholds();"
                  " const greenEnd=thr.greenEnd;"
                  " const yellowEnd=thr.yellowEnd;"
                  " const blinkStart=thr.blinkStart;"
                  " let ledsOn=Math.round(fraction*count);"
                  " if(ledsOn<0) ledsOn=0;"
                  " if(ledsOn>count) ledsOn=count;"
                  " let shiftBlink=false;"
                  " if(useBlink && (mode===1||mode===2) && fraction>=blinkStart){"
                  "   const now=Date.now();"
                  "   if(now-lastLedBlinkTs>100){"
                  "     lastLedBlinkTs=now;"
                  "     ledBlinkState=!ledBlinkState;"
                  "   }"
                  "   shiftBlink=true;"
                  " }else{"
                  "   ledBlinkState=false;"
                  " }"
                  " const mode2FullBlink=useBlink && mode===2 && fraction>=blinkStart;"
                  " const gCol=document.getElementById('greenColorInput').value;"
                  " const yCol=document.getElementById('yellowColorInput').value;"
                  " const rCol=document.getElementById('redColorInput').value;"
                  " for(let i=0;i<count;i++){"
                  "   let col='#000000';"
                  "   if(i<ledsOn){"
                  "     let pos=count>1 ? (i/(count-1)) : 0;"
                  "     if(mode2FullBlink){"
                  "       col=ledBlinkState?rCol:'#000000';"
                  "     }else{"
                  "       if(pos<greenEnd){"
                  "         col=gCol;"
                  "       }else if(pos<yellowEnd){"
                  "         col=yCol;"
                  "       }else{"
                  "         if(useBlink && mode===1 && shiftBlink){"
                  "           col=ledBlinkState?rCol:'#000000';"
                  "         }else{"
                  "           col=rCol;"
                  "         }"
                  "       }"
                  "     }"
                  "   }"
                  "   dots[i].style.backgroundColor=col;"
                  " }"
                  " const blinkContainer=document.getElementById('blinkStartContainer');"
                  " if(mode===0){"
                  "   blinkContainer.style.display='none';"
                  " }else{"
                  "   blinkContainer.style.display='block';"
                  " }"
                  "}"

                  "function updateLedPreview(){"
                  " if(testSweepActive){"
                  "   const elapsed=Date.now()-testSweepStart;"
                  "   let t=elapsed/TEST_SWEEP_DURATION;"
                  "   if(t>=1){"
                  "     t=1;"
                  "     testSweepActive=false;"
                  "   }"
                  "   const frac=computeSimFraction(t);"
                  "   renderLedBarFraction(frac,true);"
                  " }else if(blinkPreviewActive){"
                  "   const now=Date.now();"
                  "   if(now>=blinkPreviewEnd){"
                  "     blinkPreviewActive=false;"
                  "     renderLedBarFraction(1.0,false);"
                  "   }else{"
                  "     renderLedBarFraction(1.0,true);"
                  "   }"
                  " }else{"
                  "   renderLedBarFraction(1.0,false);"
                  " }"
                  "}"

                  "function ensurePreviewTimer(){"
                  " if(!previewTimerId){"
                  "   previewTimerId=setInterval(updateLedPreview,30);"
                  " }"
                  "}"

                  "function handleSliderChange(ev){"
                  " enforceSliderOrder(ev.target.id);"
                  " updateSliderDisplay(ev.target);"
                  " if(ev.target.id==='blinkStartSlider'){"
                  "   triggerBlinkPreview();"
                  " }else{"
                  "   updateLedPreview();"
                  " }"
                  " markDirty();"
                  "}"

                  "function triggerBlinkPreview(){"
                  " const modeVal=document.getElementById('modeSelect').value;"
                  " const mode=parseInt(modeVal||'0');"
                  " if(mode===0) return;"
                  " blinkPreviewActive=true;"
                  " blinkPreviewEnd=Date.now()+2500;"
                  " testSweepActive=false;"
                  " ensurePreviewTimer();"
                  " updateLedPreview();"
                  "}"

                  "function classifyColor(slot,value){"
                  " if(slot===1) return 'Farbe 1 – Grün';"
                  " if(slot===2) return 'Farbe 2 – Gelb';"
                  " return 'Farbe 3 – Rot';"
                  "}"

                  "function updateColorUi(){"
                  " const cfg=["
                  "   {k:'green',slot:1,labelId:'greenEndLabel',hiddenId:'greenLabelHidden',nameId:'color1Name'},"
                  "   {k:'yellow',slot:2,labelId:'yellowEndLabel',hiddenId:'yellowLabelHidden',nameId:'color2Name'},"
                  "   {k:'red',slot:3,labelId:'blinkStartLabel',hiddenId:'redLabelHidden',nameId:'color3Name'}"
                  " ];"
                  " cfg.forEach(c=>{"
                  "   const inp=document.getElementById(c.k+'ColorInput');"
                  "   if(!inp) return;"
                  "   const name=classifyColor(c.slot,inp.value);"
                  "   const label=name.split('–')[1].trim();"
                  "   const span=document.getElementById(c.nameId);"
                  "   if(span) span.innerText=label;"
                  "   const hid=document.getElementById(c.hiddenId);"
                  "   if(hid) hid.value=label;"
                  "   const lbl=document.getElementById(c.labelId);"
                  "   if(lbl) lbl.style.color=inp.value;"
                  " });"
                  " updateLedPreview();"
                  "}"

                  "function updateVehicleDots(loading){"
                  " ['vehicleModel','vehicleVin','vehicleDiag'].forEach(id=>{"
                  "   setAnimatedDots(document.getElementById(id),loading);"
                  " });"
                  "}"

                  "function fetchStatus(){"
                  " const done=beginRequest();"
                  " fetch('/status').then(r=>r.json()).then(s=>{"
                  "   const rpm=document.getElementById('rpmVal');"
                  "   if(rpm&&s.rpm!==undefined) rpm.innerText=s.rpm;"
                  "   const rpmMax=document.getElementById('rpmMaxVal');"
                  "   if(rpmMax&&s.maxRpm!==undefined) rpmMax.innerText=s.maxRpm;"
                  "   const lastTx=document.getElementById('lastTx');"
                  "   if(lastTx&&s.lastTx!==undefined){"
                  "     lastTx.dataset.base=s.lastTx;"
                  "     lastTx.innerText=s.lastTx;"
                  "   }"
                  "   const lastObd=document.getElementById('lastObd');"
                  "   if(lastObd&&s.lastObd!==undefined){"
                  "     lastObd.dataset.base=s.lastObd;"
                  "     lastObd.innerText=s.lastObd;"
                  "   }"
                  "   const ble=document.getElementById('bleStatus');"
                  "   if(ble&&s.bleText) ble.innerText=s.bleText;"
                  "   const vm=document.getElementById('vehicleModel');"
                  "   if(vm&&s.vehicleModel!==undefined){"
                  "     vm.dataset.base=s.vehicleModel;"
                  "     vm.innerText=s.vehicleModel;"
                  "   }"
                  "   const vv=document.getElementById('vehicleVin');"
                  "   if(vv&&s.vehicleVin!==undefined){"
                  "     vv.dataset.base=s.vehicleVin;"
                  "     vv.innerText=s.vehicleVin;"
                  "   }"
                  "   const vd=document.getElementById('vehicleDiag');"
                  "   if(vd&&s.vehicleDiag!==undefined){"
                  "     vd.dataset.base=s.vehicleDiag;"
                  "     vd.innerText=s.vehicleDiag;"
                  "   }"
                  "   updateVehicleDots(s.vehicleInfoRequestRunning);"
                  "   const btnC=document.getElementById('btnConnect');"
                  "   const btnD=document.getElementById('btnDisconnect');"
                  "   if(btnC&&btnD){"
                  "     if(s.connected){"
                  "       btnC.style.display='none';"
                  "       btnD.style.display='block';"
                  "     }else{"
                  "       btnC.style.display='block';"
                  "       btnD.style.display='none';"
                  "     }"
                  "   }"
                  " }).catch(()=>{}).finally(done);"
                  "}"

                  "function onSaveClicked(){"
                  " const done=beginRequest();"
                  " const fd=new FormData(document.getElementById('mainForm'));"
                  " fetch('/save',{method:'POST',body:fd}).then(()=>{"
                  "   captureInitialMainState();"
                  "   recomputeMainDirty();"
                  " }).finally(done);"
                  "}"

                  "function onTestClicked(){"
                  " const done=beginRequest();"
                  " const fd=new FormData(document.getElementById('mainForm'));"
                  " fetch('/test',{method:'POST',body:fd}).finally(done);"
                  " testSweepActive=true;"
                  " testSweepStart=Date.now();"
                  " blinkPreviewActive=false;"
                  " ensurePreviewTimer();"
                  " updateLedPreview();"
                  "}"

                  "function initUI(){"
                  " const form=document.getElementById('mainForm');"
                  " if(form){"
                  "   form.querySelectorAll('input,select').forEach(el=>{"
                  "     if(el.id==='brightness_slider') return;"
                  "     if(el.type==='range'){"
                  "       el.addEventListener('input',handleSliderChange);"
                  "       el.addEventListener('change',handleSliderChange);"
                  "     }else{"
                  "       el.addEventListener('change',markDirty);"
                  "       el.addEventListener('input',markDirty);"
                  "     }"
                  "   });"
                  " }"
                  " const auto=document.getElementById('autoscaleToggle');"
                  " if(auto) auto.addEventListener('change',()=>{markDirty();updateAutoscaleUi();});"
                  " updateAutoscaleUi();"
                  " ['green','yellow','red'].forEach(n=>{"
                  "   const c=document.getElementById(n+'ColorInput');"
                  "   if(c){"
                  "     c.addEventListener('input',()=>{updateColorUi();markDirty();});"
                  "     c.addEventListener('change',()=>{updateColorUi();markDirty();});"
                  "   }"
                  " });"
                  " ['greenEndSlider','yellowEndSlider','blinkStartSlider'].forEach(id=>{"
                  "   const el=document.getElementById(id);"
                  "   if(el){"
                  "     updateSliderDisplay(el);"
                  "     el.addEventListener('input',handleSliderChange);"
                  "     el.addEventListener('change',handleSliderChange);"
                  "   }"
                  " });"
                  " const b=document.getElementById('brightness_slider');"
                  " if(b){"
                  "   b.addEventListener('input',e=>onBrightnessChange(e.target.value));"
                  "   b.addEventListener('change',e=>onBrightnessChange(e.target.value));"
                  " }"
                  " const ms=document.getElementById('modeSelect');"
                  " if(ms){"
                  "   ms.addEventListener('change',()=>{markDirty();updateLedPreview();});"
                  " }"
                  " const sb=document.getElementById('btnSave');"
                  " if(sb) sb.addEventListener('click',onSaveClicked);"
                  " const tb=document.getElementById('btnTest');"
                  " if(tb) tb.addEventListener('click',onTestClicked);"
                  " const bc=document.getElementById('btnConnect');"
                  " if(bc) bc.addEventListener('click',()=>postSimple('/connect'));"
                  " const bd=document.getElementById('btnDisconnect');"
                  " if(bd) bd.addEventListener('click',()=>postSimple('/disconnect'));"
                  " const br=document.getElementById('btnReset');"
                  " if(br) br.addEventListener('click',()=>window.location.reload());"
                  " const bdsp=document.getElementById('btnDisplayLogo');"
                  " if(bdsp) bdsp.addEventListener('click',()=>postSimple('/dev/display-logo'));"
                  " initLedPreview();"
                  " updateColorUi();"
                  " fetchStatus();"
                  " statusTimer=setInterval(fetchStatus,1500);"
                  " setInterval(updateSpinnerVisibility,1000);"
                  " captureInitialMainState();"
                  " recomputeMainDirty();"
                  "}"
                  "document.addEventListener('DOMContentLoaded',initUI);"
                  "</script>");

        page += F("</body></html>");
        return page;
    }

    String settingsPage()
    {
        String vin = readVehicleVin();
        String model = readVehicleModel();
        String diag = readVehicleDiagStatus();

        String page;
        page.reserve(9000);

        page += F(
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>ShiftLight Einstellungen</title>"
            "<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
            "background:#111;color:#eee;padding:16px;margin:0;}"
            "h1{font-size:20px;margin:0 0 12px 0;display:flex;align-items:center;justify-content:space-between;}"
            "a{color:#0af;text-decoration:none;}"
            ".row{margin-bottom:6px;}"
            ".small{font-size:12px;color:#aaa;}"
            ".section{margin-top:12px;padding:10px 12px;border-radius:8px;background:#181818;border:1px solid #333;}"
            ".section-title{font-weight:600;margin-bottom:8px;font-size:18px;letter-spacing:0.3px;}"
            ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".toggle-label{font-size:14px;}"
            ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
            ".switch input{opacity:0;width:0;height:0;}"
            ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#555;transition:.2s;border-radius:24px;}"
            ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;background:#fff;transition:.2s;border-radius:50%;}"
            ".switch input:checked + .slider{background:#0af;}"
            ".switch input:checked + .slider:before{transform:translateX(22px);}"
            ".btn{margin-top:12px;width:100%;padding:10px;border:none;border-radius:6px;background:#0af;color:#000;font-weight:bold;font-size:14px;}"
            ".btn-danger{background:#d33;color:#fff;}"
            ".btn:disabled{background:#555;color:#888;}"
            ".error{color:#f77;font-size:12px;margin-top:4px;}"
            ".spinner{display:inline-block;width:12px;height:12px;border-radius:50%;"
            "border:2px solid rgba(255,255,255,0.2);border-top-color:#0af;"
            "animation:spin 1s linear infinite;margin-left:6px;}"
            "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
            ".console-box{background:#000;border-radius:6px;border:1px solid #333;"
            "padding:6px;font-family:SFMono-Regular,Menlo,monospace;font-size:11px;"
            "line-height:1.3;max-height:160px;overflow-y:auto;white-space:pre-wrap;}"
            ".console-input{flex:1;padding:6px;border-radius:6px;border:1px solid #444;"
            "background:#111;color:#eee;font-family:inherit;}"
            ".console-row{display:flex;gap:8px;margin-top:8px;}"
            ".console-footer{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
            ".console-footer-left{display:flex;align-items:center;gap:6px;font-size:11px;color:#aaa;}"
            ".dev-section{"
            "  overflow:hidden;"
            "  transition:max-height 0.25s ease,opacity 0.25s ease,"
            "             margin-top 0.25s ease,padding-top 0.25s ease,"
            "             padding-bottom 0.25s ease,border-width 0.25s ease;"
            "}"
            ".dev-collapsed{"
            "  max-height:0;"
            "  opacity:0;"
            "  margin-top:0;"
            "  padding-top:0;"
            "  padding-bottom:0;"
            "  border-top-width:0;"
            "  border-bottom-width:0;"
            "}"
            ".dev-expanded{"
            "  max-height:500px;"
            "  opacity:1;"
            "}"
            ".console-box{"
            "  background:#000;"
            "  border-radius:6px;"
            "  border:1px solid #333;"
            "  padding:6px;"
            "  font-family:SFMono-Regular,Menlo,monospace;"
            "  font-size:11px;"
            "  line-height:1.3;"
            "  max-height:160px;"
            "  overflow-y:auto;"
            "  white-space:pre-wrap;"
            "}"
            ".console-input{"
            "  flex:1;"
            "  padding:6px;"
            "  border-radius:6px;"
            "  border:1px solid #444;"
            "  background:#111;"
            "  color:#eee;"
            "  font-family:inherit;"
            "}"
            "</style></head><body>");

        page += "<h1><a href='/'>&larr; Zurück</a><span>Einstellungen</span></h1>";
        page += F("<form id='settingsForm' method='POST' action='/settings'>");

        // --- Modus / Dev-Mode ---
        page += F("<div class='section'><div class='section-title'>Modus</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>Entwicklermodus</span><label class='switch'>");
        page += "<input type='checkbox' name='devMode' id='devModeToggle'";
        if (g_devMode)
            page += " checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div class='small'>Schaltet zusätzliche Debug- und OBD-Einstellungen frei.</div></div>");

        // --- Mein Fahrzeug ---
        page += F("<div class='section'><div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>Fahrzeug: <strong id='vehicleModel' data-base='" + htmlEscape(model) + "'>" + htmlEscape(model) + "</strong></div>";
        page += "<div class='row small'>VIN: <strong id='vehicleVin' data-base='" + htmlEscape(vin) + "'>" + htmlEscape(vin) + "</strong></div>";
        page += "<div class='row small'>Diagnose: <strong id='vehicleDiag' data-base='" + htmlEscape(diag) + "'>" + htmlEscape(diag) + "</strong></div>";
        page += "<div class='row small'>Status: <span id='vehicleStatus' data-base='Noch keine Daten'>Noch keine Daten</span></div>";
        page += F("<button type='button' class='btn' id='btnVehicleRefresh'>Fahrzeugdaten neu synchronisieren</button>");
        page += F("<div id='settingsError' class='error'></div></div>");

        // --- Dev-OBD-Konsole: ausblendbar über Entwicklermodus ---
        page += "<div id='devObdSection' class='section dev-section ";
        if (g_devMode)
            page += "dev-expanded";
        else
            page += "dev-collapsed";
        page += "'>";

        page += F("<div class='section-title'>OBD-Konsole</div>");
        page += F("<div class='small'>"
                  "Direkte AT-/OBD-Befehle senden (z.&nbsp;B. <code>010C</code>, <code>010D</code>, <code>ATZ</code>). "
                  "Antworten erscheinen unten."
                  "</div>");
        page += F("<div id='obdConsole' class='console-box'></div>");
        page += F("<div class='console-row'>");
        page += F("<input type='text' id='obdCmdInput' class='console-input' placeholder='Befehl, z.B. 010C'>");
        page += F("<button type='button' class='btn' id='obdSendBtn'>Senden</button>");
        page += F("</div>");
        page += F("<div class='console-footer'>");
        page += F("<div class='console-footer-left'>");
        page += F("<label style='display:flex;align-items:center;gap:4px;'>");
        // Auto-Log: kein "checked" mehr, wir machen das per localStorage im JS
        page += F("<input type='checkbox' id='obdAutoLog'> Auto-Log");
        page += F("</label>");
        page += F("</div>");
        page += F("<button type='button' class='btn btn-danger' id='obdClearBtn' style='width:auto;padding:6px 10px;'>Clear</button>");
        page += F("</div>");
        page += F("</div>");

        // --- Buttons ---
        page += F("<button type='submit' class='btn' id='settingsSave' disabled>Speichern</button>");
        page += F("<button type='button' class='btn btn-danger' id='settingsReset' style='display:none'>Zurücksetzen</button>");
        page += F("</form>");

        // --- Script ---
        page += F(
            "<script>"
            "let settingsDirty=false;"
            "let refreshActive=false;"
            "let refreshStart=0;"
            "let dotIntervals={};"
            "let consoleLastTx='';"
            "let consoleLastObd='';"
            "let initialSettingsState=null;"

            "function setAnimatedDots(el,loading){"
            "  if(!el) return;"
            "  const key=el.id;"
            "  if(loading){"
            "    if(dotIntervals[key]) return;"
            "    let step=0;"
            "    dotIntervals[key]=setInterval(()=>{"
            "      step=(step+1)%4;"
            "      el.innerText=(el.dataset.base||'')+'.'.repeat(step);"
            "    },400);"
            "  }else{"
            "    if(dotIntervals[key]){clearInterval(dotIntervals[key]);dotIntervals[key]=null;}"
            "    el.innerText=el.dataset.base||'';"
            "  }"
            "}"

            "function appendConsole(line){"
            "  const box=document.getElementById('obdConsole');"
            "  if(!box || !line) return;"
            "  box.textContent += (box.textContent ? '\\n' : '') + line;"
            "  box.scrollTop = box.scrollHeight;"
            "}"

            "function formatObdLine(lastTx, resp){"
            "  if(!resp) return null;"
            "  const cleanResp=resp.trim();"
            "  const parts=cleanResp.split(/\\s+/);"
            "  if(parts.length<2) return null;"
            "  const mode=parts[0].toUpperCase();"
            "  const pid=parts[1].toUpperCase();"
            "  if(mode!=='41') return null;"
            "  if(pid==='0C' && parts.length>=4){"
            "    const A=parseInt(parts[2],16);"
            "    const B=parseInt(parts[3],16);"
            "    if(isNaN(A)||isNaN(B)) return null;"
            "    const rpm=((A*256)+B)/4;"
            "    return '< '+cleanResp+'   (RPM ≈ '+rpm+')';"
            "  }"
            "  if(pid==='0D' && parts.length>=3){"
            "    const A=parseInt(parts[2],16);"
            "    if(isNaN(A)) return null;"
            "    return '< '+cleanResp+'   (Speed ≈ '+A+' km/h)';"
            "  }"
            "  return '< '+cleanResp;"
            "}"

            "function captureInitialSettingsState(){"
            "  const form=document.getElementById('settingsForm');"
            "  if(!form) return;"
            "  initialSettingsState={};"
            "  const elements=form.querySelectorAll('input,select,textarea');"
            "  elements.forEach(el=>{"
            "    if(!el.name) return;"
            "    let val;"
            "    if(el.type==='checkbox'){"
            "      val=el.checked?'on':'';"
            "    }else{"
            "      val=el.value;"
            "    }"
            "    initialSettingsState[el.name]=val;"
            "  });"
            "}"

            "function recomputeSettingsDirty(){"
            "  const form=document.getElementById('settingsForm');"
            "  if(!form || !initialSettingsState) return;"
            "  let changed=false;"
            "  const elements=form.querySelectorAll('input,select,textarea');"
            "  const current={};"
            "  elements.forEach(el=>{"
            "    if(!el.name) return;"
            "    let val;"
            "    if(el.type==='checkbox'){"
            "      val=el.checked?'on':'';"
            "    }else{"
            "      val=el.value;"
            "    }"
            "    current[el.name]=val;"
            "  });"
            "  for(const k in initialSettingsState){"
            "    if(initialSettingsState[k]!==current[k]){"
            "      changed=true;"
            "      break;"
            "    }"
            "  }"
            "  settingsDirty=changed;"
            "  const s=document.getElementById('settingsSave');"
            "  const r=document.getElementById('settingsReset');"
            "  if(s) s.disabled=!changed;"
            "  if(r) r.style.display=changed?'block':'none';"
            "}"

            "function markSettingsDirty(){"
            "  recomputeSettingsDirty();"
            "}"

            "function setStatus(text,loading){"
            "  const el=document.getElementById('vehicleStatus');"
            "  if(el){el.dataset.base=text;setAnimatedDots(el,loading);}"
            "  const sp=document.getElementById('statusSpinner');"
            "  if(sp){if(loading) sp.classList.remove('hidden'); else sp.classList.add('hidden');}"
            "}"

            "function setRefreshActive(on){"
            "  refreshActive=on;"
            "  const btn=document.getElementById('btnVehicleRefresh');"
            "  if(btn) btn.disabled=on;"
            "  const err=document.getElementById('settingsError');"
            "  if(err && !on) err.innerText='';"
            "}"

            "function showError(msg){"
            "  const err=document.getElementById('settingsError');"
            "  if(err) err.innerText=msg;"
            "}"

            "function updateVehicleInfo(data){"
            "  ['vehicleModel','vehicleVin','vehicleDiag'].forEach(id=>{"
            "    const el=document.getElementById(id);"
            "    if(el && data[id]!==undefined){"
            "      el.dataset.base=data[id];"
            "      if(!data.vehicleInfoRequestRunning) el.innerText=data[id];"
            "    }"
            "  });"
            "  let statusText='Noch keine Daten';"
            "  let loading=false;"
            "  if(data.vehicleInfoRequestRunning){"
            "    statusText='Abruf läuft';"
            "    loading=true;"
            "  }else if(data.vehicleInfoReady){"
            "    statusText=data.vehicleInfoAge<=1"
            "      ?'Gerade aktualisiert (0s)'"
            "      :'Letztes Update vor '+data.vehicleInfoAge+'s';"
            "  }"
            "  setStatus(statusText,loading);"
            "  setAnimatedDots(document.getElementById('vehicleModel'),loading);"
            "  setAnimatedDots(document.getElementById('vehicleVin'),loading);"
            "  setAnimatedDots(document.getElementById('vehicleDiag'),loading);"
            "  if(refreshActive && !data.vehicleInfoRequestRunning){"
            "    if(data.vehicleInfoReady && data.vehicleInfoAge<=2){"
            "      setRefreshActive(false);"
            "      setStatus('Gerade aktualisiert (0s)',false);"
            "      showError('');"
            "    }else if(Date.now()-refreshStart>7000){"
            "      setRefreshActive(false);"
            "      showError('Keine Antwort vom Fahrzeug.');"
            "    }"
            "  }"
            "  const autoLog=document.getElementById('obdAutoLog');"
            "  const allowLog=!autoLog || autoLog.checked;"
            "  if(allowLog && data.lastTx!==undefined && data.lastTx!==consoleLastTx){"
            "    consoleLastTx=data.lastTx;"
            "    appendConsole('> '+data.lastTx);"
            "  }"
            "  if(allowLog && data.lastObd!==undefined && data.lastObd!==consoleLastObd){"
            "    consoleLastObd=data.lastObd;"
            "    const pretty=formatObdLine(consoleLastTx,data.lastObd);"
            "    appendConsole(pretty || ('< '+data.lastObd));"
            "  }"
            "}"

            "function poll(){"
            "  fetch('/status')"
            "    .then(r=>r.json())"
            "    .then(updateVehicleInfo)"
            "    .catch(()=>{});"
            "}"

            "function initObdAutoLog(){"
            "  const chk=document.getElementById('obdAutoLog');"
            "  if(!chk) return;"
            "  const stored=localStorage.getItem('obdAutoLog');"
            "  if(stored===null){"
            "    chk.checked=true;"
            "  }else{"
            "    chk.checked=(stored==='1');"
            "  }"
            "  chk.addEventListener('change',()=>{"
            "    localStorage.setItem('obdAutoLog',chk.checked?'1':'0');"
            "  });"
            "}"

            "document.getElementById('settingsForm').addEventListener('change',markSettingsDirty);"
            "document.getElementById('settingsReset').addEventListener('click',()=>window.location.reload());"

            "document.getElementById('btnVehicleRefresh').addEventListener('click',()=>{"
            "  setRefreshActive(true);"
            "  refreshStart=Date.now();"
            "  setStatus('Abruf läuft',true);"
            "  fetch('/settings/vehicle-refresh',{method:'POST'})"
            "    .then(r=>r.json())"
            "    .then(d=>{"
            "      if(!d || d.status!=='started'){"
            "        setRefreshActive(false);"
            "        if(d && d.reason==='no-connection'){"
            "          showError('Keine OBD-Verbindung vorhanden.');"
            "          setStatus('Sync nicht möglich',false);"
            "        }else{"
            "          showError('Sync konnte nicht gestartet werden.');"
            "        }"
            "      }"
            "    })"
            "    .catch(()=>{setRefreshActive(false);showError('Sync fehlgeschlagen.');});"
            "});"

            "document.getElementById('settingsForm').addEventListener('submit',function(ev){"
            "  ev.preventDefault();"
            "  const fd=new FormData(this);"
            "  const params=new URLSearchParams();"
            "  fd.forEach((v,k)=>{params.append(k,v);});"
            "  fetch('/settings',{"
            "    method:'POST',"
            "    headers:{'Content-Type':'application/x-www-form-urlencoded'},"
            "    body:params.toString()"
            "  }).then(()=>{"
            "    captureInitialSettingsState();"
            "    recomputeSettingsDirty();"
            "  }).catch(()=>{});"
            "});"

            "poll();"
            "setInterval(poll,1500);"
            "initObdAutoLog();"
            "captureInitialSettingsState();"
            "recomputeSettingsDirty();"

            "const devToggle=document.getElementById('devModeToggle');"
            "const devSection=document.getElementById('devObdSection');"
            "function updateDevSection(){"
            "  if(!devSection) return;"
            "  if(devToggle && devToggle.checked){"
            "    devSection.classList.remove('dev-collapsed');"
            "    devSection.classList.add('dev-expanded');"
            "  }else{"
            "    devSection.classList.remove('dev-expanded');"
            "    devSection.classList.add('dev-collapsed');"
            "  }"
            "}"
            "if(devToggle){"
            "  devToggle.addEventListener('change',()=>{"
            "    markSettingsDirty();"
            "    updateDevSection();"
            "  });"
            "  updateDevSection();"
            "}"

            "const obdBtn=document.getElementById('obdSendBtn');"
            "const obdInput=document.getElementById('obdCmdInput');"
            "const obdClear=document.getElementById('obdClearBtn');"
            "if(obdClear){"
            "  obdClear.addEventListener('click',()=>{"
            "    const box=document.getElementById('obdConsole');"
            "    if(box) box.textContent='';"
            "  });"
            "}"
            "if(obdBtn && obdInput){"
            "  obdBtn.addEventListener('click',()=>{"
            "    const cmd=obdInput.value.trim();"
            "    if(!cmd) return;"
            "    appendConsole('> '+cmd);"
            "    fetch('/dev/obd-send',{"
            "      method:'POST',"
            "      headers:{'Content-Type':'application/x-www-form-urlencoded'},"
            "      body:'cmd='+encodeURIComponent(cmd)"
            "    }).catch(()=>appendConsole('! Fehler beim Senden'));"
            "  });"
            "  obdInput.addEventListener('keydown',e=>{"
            "    if(e.key==='Enter'){e.preventDefault();obdBtn.click();}"
            "  });"
            "}"
            "</script>"
            "</body></html>");

        return page;
    }

    void handleRoot()
    {
        g_lastHttpMs = millis();
        server.send(200, "text/html", htmlPage());
    }

    void handleBrightness()
    {
        g_lastHttpMs = millis();
        if (server.hasArg("val"))
        {
            int v = clampInt(server.arg("val").toInt(), 0, 255);
            cfg.brightness = v;
            strip.setBrightness(cfg.brightness);
            strip.show();
            g_brightnessPreviewActive = true;
            g_lastBrightnessChangeMs = millis();
            showMLogoPreview();
        }
        server.send(200, "text/plain", "OK");
    }

    void applyConfigFromRequest(bool allowAutoReconnect)
    {
        if (server.hasArg("mode"))
        {
            int m = server.arg("mode").toInt();
            if (m < 0 || m > 2)
                m = 0;
            cfg.mode = m;
        }
        if (server.hasArg("brightness"))
        {
            cfg.brightness = clampInt(server.arg("brightness").toInt(), 0, 255);
            strip.setBrightness(cfg.brightness);
            strip.show();
        }

        cfg.autoScaleMaxRpm = server.hasArg("autoscale");
        if (server.hasArg("fixedMaxRpm"))
            cfg.fixedMaxRpm = clampInt(server.arg("fixedMaxRpm").toInt(), 1000, 8000);

        int gPct = cfg.greenEndPct;
        int yPct = cfg.yellowEndPct;
        int bPct = cfg.blinkStartPct;
        if (server.hasArg("greenEndPct"))
            gPct = server.arg("greenEndPct").toInt();
        if (server.hasArg("yellowEndPct"))
            yPct = server.arg("yellowEndPct").toInt();
        if (server.hasArg("blinkStartPct"))
            bPct = server.arg("blinkStartPct").toInt();
        enforceOrder(gPct, yPct, bPct);
        cfg.greenEndPct = gPct;
        cfg.yellowEndPct = yPct;
        cfg.blinkStartPct = bPct;

        if (server.hasArg("greenColor"))
            cfg.greenColor = parseHexColor(server.arg("greenColor"), cfg.greenColor);
        if (server.hasArg("yellowColor"))
            cfg.yellowColor = parseHexColor(server.arg("yellowColor"), cfg.yellowColor);
        if (server.hasArg("redColor"))
            cfg.redColor = parseHexColor(server.arg("redColor"), cfg.redColor);

        cfg.greenLabel = safeLabel(server.arg("greenLabel"), "Farbe 1");
        cfg.yellowLabel = safeLabel(server.arg("yellowLabel"), "Farbe 2");
        cfg.redLabel = safeLabel(server.arg("redLabel"), "Farbe 3");

        cfg.logoOnIgnitionOn = server.hasArg("logoIgnOn");
        cfg.logoOnEngineStart = server.hasArg("logoEngStart");
        cfg.logoOnIgnitionOff = server.hasArg("logoIgnOff");

        if (allowAutoReconnect)
        {
            if (g_devMode)
            {
                bool prevReconnect = g_autoReconnect;
                g_autoReconnect = server.hasArg("autoReconnect");
                if (g_autoReconnect && !prevReconnect)
                {
                    g_forceImmediateReconnect = true;
                    g_lastBleRetryMs = 0;
                }
            }
            else
            {
                if (!g_autoReconnect)
                {
                    g_autoReconnect = true;
                    g_forceImmediateReconnect = true;
                    g_lastBleRetryMs = 0;
                }
            }
        }
    }

    void handleSave()
    {
        g_lastHttpMs = millis();
        applyConfigFromRequest(true);
        server.send(200, "text/plain", "OK");
    }

    void handleTest()
    {
        g_lastHttpMs = millis();
        if (server.method() == HTTP_POST)
        {
            applyConfigFromRequest(true);
            g_testActive = true;
            g_testStartMs = millis();
            if (cfg.autoScaleMaxRpm)
                g_testMaxRpm = (g_maxSeenRpm > 0) ? g_maxSeenRpm : 4000;
            else
                g_testMaxRpm = (cfg.fixedMaxRpm > 0) ? cfg.fixedMaxRpm : 4000;
            server.send(200, "text/plain", "OK");
            return;
        }
        server.send(405, "text/plain", "Method Not Allowed");
    }

    void handleConnect()
    {
        g_lastHttpMs = millis();
        g_autoReconnect = true;
        g_forceImmediateReconnect = true;
        g_lastBleRetryMs = 0;
        server.send(200, "text/plain", "OK");
    }

    void handleDisconnect()
    {
        g_lastHttpMs = millis();
        if (g_client != nullptr)
            g_client->disconnect();
        server.send(200, "text/plain", "OK");
    }

    void handleStatus()
    {
        g_lastHttpMs = millis();
        unsigned long now = millis();
        int vehicleAge = 0;
        bool ready = g_vehicleInfoAvailable;
        if (g_vehicleInfoAvailable && g_vehicleInfoLastUpdate > 0)
            vehicleAge = static_cast<int>((now - g_vehicleInfoLastUpdate) / 1000UL);

        String json = "{";
        json += "\"rpm\":" + String(g_currentRpm);
        json += ",\"maxRpm\":" + String(g_maxSeenRpm);
        json += ",\"speed\":" + String(g_vehicleSpeedKmh);
        json += ",\"gear\":" + String(g_estimatedGear);
        json += ",\"lastTx\":\"" + jsonEscape(g_lastTxInfo) + "\"";
        json += ",\"lastObd\":\"" + jsonEscape(g_lastObdInfo) + "\"";
        json += ",\"connected\":" + String(g_connected ? "true" : "false");
        json += ",\"autoReconnect\":" + String(g_autoReconnect ? "true" : "false");
        json += ",\"devMode\":" + String(g_devMode ? "true" : "false");
        String bleText = g_connected ? "Verbunden" : "Getrennt";
        bleText += g_autoReconnect ? " (Auto-Reconnect AN)" : " (Auto-Reconnect AUS)";
        json += ",\"bleText\":\"" + jsonEscape(bleText) + "\"";
        json += ",\"vehicleVin\":\"" + jsonEscape(g_vehicleVin) + "\"";
        json += ",\"vehicleModel\":\"" + jsonEscape(g_vehicleModel) + "\"";
        json += ",\"vehicleDiag\":\"" + jsonEscape(g_vehicleDiagStatus) + "\"";
        json += ",\"vehicleInfoRequestRunning\":" + String(g_vehicleInfoRequestRunning ? "true" : "false");
        json += ",\"vehicleInfoReady\":" + String(ready ? "true" : "false");
        json += ",\"vehicleInfoAge\":" + String(vehicleAge);
        json += "}";
        server.send(200, "application/json", json);
    }

    void handleSettingsGet()
    {
        g_lastHttpMs = millis();
        server.send(200, "text/html", settingsPage());
    }

    void handleSettingsSave()
    {
        g_lastHttpMs = millis();
        bool newDev = server.hasArg("devMode");
        g_devMode = newDev;
        if (!g_devMode)
        {
            g_autoReconnect = true;
            g_forceImmediateReconnect = true;
            g_lastBleRetryMs = 0;
        }
        server.sendHeader("Location", "/settings", true);
        server.send(303, "text/plain", "Redirect");
    }

    void handleSettingsVehicleRefresh()
    {
        g_lastHttpMs = millis();
        if (!g_connected)
        {
            server.send(200, "application/json", "{\"status\":\"error\",\"reason\":\"no-connection\"}");
            return;
        }
        requestVehicleInfo(true);
        server.send(200, "application/json", "{\"status\":\"started\"}");
    }

    void handleDevDisplayLogo()
    {
        g_lastHttpMs = millis();
        displayShowTestLogo();
        server.send(200, "text/plain", "OK");
    }
}

void initWifiAP()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
}

void initWebUi()
{
    server.on("/", HTTP_GET, handleRoot);
    server.on("/brightness", HTTP_GET, handleBrightness);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/test", HTTP_POST, handleTest);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/disconnect", HTTP_POST, handleDisconnect);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/settings", HTTP_GET, handleSettingsGet);
    server.on("/settings", HTTP_POST, handleSettingsSave);
    server.on("/settings/vehicle-refresh", HTTP_POST, handleSettingsVehicleRefresh);
    server.on("/dev/display-logo", HTTP_POST, handleDevDisplayLogo);
    server.on("/dev/obd-send", HTTP_POST, handleDevObdSend);

    server.begin();
}

void webUiLoop()
{
    server.handleClient();
}

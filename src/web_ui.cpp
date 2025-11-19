#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ble_obd.h"
#include "config.h"
#include "led_bar.h"
#include "logo_anim.h"
#include "utils.h"
#include "vehicle_info.h"
#include "state.h"

namespace
{
    WebServer server(80);

    String htmlPage()
    {
        String page;
        String color1Hex = colorToHtmlHex(cfg.color1);
        String color2Hex = colorToHtmlHex(cfg.color2);
        String color3Hex = colorToHtmlHex(cfg.color3);
        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        page += F("<title>ShiftLight Setup</title>");
        page += F("<style>"
                  "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;"
                  "color:#eee;padding:16px;margin:0;}"
                  "h1{font-size:20px;margin:0 0 12px 0;display:flex;"
                  "align-items:center;justify-content:space-between;}"
                  "h2{font-size:16px;margin:16px 0 8px 0;}"
                  "label{display:block;margin-top:8px;}"
                  "input,select{width:100%;padding:6px;margin-top:4px;"
                  "border-radius:6px;border:1px solid #444;background:#222;color:#eee;}"
                  "input[type=color]{padding:0;height:38px;cursor:pointer;}"
                  "input[type=range]{padding:0;margin-top:4px;}"
                  "button{margin-top:12px;width:100%;padding:10px;border:none;border-radius:6px;"
                  "background:#0af;color:#000;font-weight:bold;font-size:14px;}"
                  "button:disabled{background:#555;color:#888;}"
                  ".btn-danger{background:#d33;color:#fff;}"
                  ".row{margin-bottom:6px;}"
                  ".small{font-size:12px;color:#aaa;}"
                  ".section{margin-top:12px;padding:10px 12px;border-radius:8px;"
                  "background:#181818;border:1px solid #333;}"
                  ".section-title{font-weight:600;margin-bottom:4px;font-size:14px;}"
                  ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
                  ".toggle-label{font-size:14px;}"
                  ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
                  ".switch input{opacity:0;width:0;height:0;}"
                  ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;"
                  "background:#555;transition:.2s;border-radius:24px;}"
                  ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;"
                  "background:#fff;transition:.2s;border-radius:50%;}"
                  ".switch input:checked + .slider{background:#0af;}"
                  ".switch input:checked + .slider:before{transform:translateX(22px);}"
                  ".status-line{font-size:12px;color:#ccc;margin-top:4px;}"
                  ".spinner{display:inline-block;width:12px;height:12px;border-radius:50%;"
                  "border:2px solid rgba(255,255,255,0.2);border-top-color:#0af;"
                  "animation:spin 1s linear infinite;margin-left:6px;}"
                  ".hidden{display:none;}"
                  ".field-desc{font-size:12px;color:#888;margin-top:2px;}"
                  "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
                  "</style></head><body>");

        page += "<h1><span>ShiftLight Setup</span>"
                "<a href=\"/settings\" style='text-decoration:none;color:#0af;font-size:20px;'>⚙️</a>"
                "</h1>";

        page += F("<form id='mainForm' method='POST' action='/save'>");

        page += F("<div class='section'>");
        page += F("<div class='section-title'>Allgemein</div>");

        page += F("<label>Mode</label>");
        page += F("<div class='field-desc'>Wähle den gewünschten Darstellungsmodus für den LED-Balken.</div>");
        page += F("<select name='mode'>");
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

        page += F("<label>Brightness (0-255)</label>");
        page += F("<div class='field-desc'>Steuert die Helligkeit aller LEDs; Änderungen werden kurz als Vorschau angezeigt.</div>");
        page += "<input type='range' min='0' max='255' value='";
        page += String(cfg.brightness);
        page += "' id='brightness_slider' oninput='onBrightnessChange(this.value)'>";
        page += "<div class='small'>Wert: <span id='bval'>";
        page += String(cfg.brightness);
        page += "</span></div>";
        page += "<input type='hidden' name='brightness' id='brightness' value='";
        page += String(cfg.brightness);
        page += "'>";

        page += F("<label for='color1'>Farbe 1 (unterer Bereich)</label>");
        page += F("<div class='field-desc'>Definiert die Farbe der unteren Drehzahl-LEDs.</div>");
        page += "<input type='color' name='color1' id='color1' value='" + color1Hex + "'>";

        page += F("<label for='color2'>Farbe 2 (Übergangsbereich)</label>");
        page += F("<div class='field-desc'>Wird genutzt, wenn der Balken in den Mittelbereich wechselt.</div>");
        page += "<input type='color' name='color2' id='color2' value='" + color2Hex + "'>";

        page += F("<label for='color3'>Farbe 3 (Warnbereich)</label>");
        page += F("<div class='field-desc'>Diese Farbe markiert den kritischen Bereich inklusive Blinkanimation.</div>");
        page += "<input type='color' name='color3' id='color3' value='" + color3Hex + "'>";

        page += F("</div>");

        page += F("<div class='section'>");
        page += F("<div class='section-title'>Drehzahl-Bereich</div>");

        page += F("<div class='toggle-row'><span class='toggle-label'>"
                  "Auto-Scale Max RPM (benutze max gesehene Drehzahl)"
                  "</span><label class='switch'>");
        page += "<input type='checkbox' name='autoscale' ";
        if (cfg.autoScaleMaxRpm)
            page += "checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div class='field-desc'>Wenn aktiv, richtet sich der Balken automatisch nach der höchsten gesehenen Drehzahl.</div>");

        page += F("<label>Fixed Max RPM (wenn Auto-Scale aus)</label>");
        page += F("<div class='field-desc'>Dieser Wert wird nur verwendet, wenn Auto-Scale deaktiviert ist.</div>");
        page += "<input type='number' name='fixedMaxRpm' min='1000' max='8000' value='";
        page += String(cfg.fixedMaxRpm);
        page += "'>";

        page += F("<label id='color1Label'>Bereich 1 Ende (% von Max RPM)</label>");
        page += F("<div class='field-desc'>Legt fest, bis zu welcher Prozentzahl die erste Farbe genutzt wird.</div>");
        page += "<input type='range' name='greenEndPct' min='0' max='100' value='";
        page += String(cfg.greenEndPct);
        page += "' id='greenEndSlider'>";
        page += "<div class='small'>Wert: <span id='greenEndVal'>";
        page += String(cfg.greenEndPct);
        page += "%</span></div>";

        page += F("<label id='color2Label'>Bereich 2 Ende (% von Max RPM)</label>");
        page += F("<div class='field-desc'>Dieser Wert bestimmt den Übergang zur dritten Farbe.</div>");
        page += "<input type='range' name='yellowEndPct' min='0' max='100' value='";
        page += String(cfg.yellowEndPct);
        page += "' id='yellowEndSlider'>";
        page += "<div class='small'>Wert: <span id='yellowEndVal'>";
        page += String(cfg.yellowEndPct);
        page += "%</span></div>";

        page += F("<label id='color3Label'>Warnbereich/Blink Start (% von Max RPM)</label>");
        page += F("<div class='field-desc'>Ab diesem Punkt beginnt die Blinkanimation bzw. der rote Bereich.</div>");
        page += "<input type='range' name='blinkStartPct' min='0' max='100' value='";
        page += String(cfg.blinkStartPct);
        page += "' id='blinkStartSlider'>";
        page += "<div class='small'>Wert: <span id='blinkStartVal'>";
        page += String(cfg.blinkStartPct);
        page += "%</span></div>";

        page += F("</div>");

        page += F("<div class='section'>");
        page += F("<div class='section-title'>Coming-Home / Leaving</div>");

        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Zündung an</span>"
                  "<label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOn' ";
        if (cfg.logoOnIgnitionOn)
            page += "checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div class='field-desc'>Blendt das Logo ein, sobald die Zündung eingeschaltet wird.</div>");

        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Motorstart</span>"
                  "<label class='switch'>");
        page += "<input type='checkbox' name='logoEngStart' ";
        if (cfg.logoOnEngineStart)
            page += "checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div class='field-desc'>Zeigt einmalig die Animation, wenn der Motor startet.</div>");

        page += F("<div class='toggle-row'><span class='toggle-label'>Leaving-Animation bei Zündung aus</span>"
                  "<label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOff' ";
        if (cfg.logoOnIgnitionOff)
            page += "checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div class='field-desc'>Spielt die Leaving-Animation nach dem Herunterdimmen ab.</div>");

        page += F("</div>");

        if (g_devMode)
        {
            page += F("<div class='section'>");
            page += F("<div class='section-title'>OBD / Verbindung</div>");

            page += F("<div class='toggle-row'><span class='toggle-label'>"
                      "OBD automatisch verbinden (Reconnect)"
                      "</span><label class='switch'>");
            page += "<input type='checkbox' name='autoReconnect' ";
            if (g_autoReconnect)
                page += "checked";
            page += "><span class='slider'></span></label></div>";
            page += F("<div class='field-desc'>Aktiviert einen automatischen Verbindungsversuch mit mehreren Retries.</div>");

            page += F("<label>Manuelle Verbindungsversuche</label>");
            page += F("<div class='field-desc'>Bestimmt, wie oft der ESP32 bei einem manuellen Klick erneut koppelt (5-10).</div>");
            page += "<input type='number' name='manualAttempts' min='5' max='10' value='";
            page += String(cfg.manualConnectAttempts);
            page += "'>";

            page += F("<div class='status-line'>BLE-Status: <span id='bleStatus'>");
            if (g_connected)
                page += "Verbunden";
            else
                page += "Getrennt";
            if (g_autoReconnect)
                page += " (Auto-Reconnect AN)";
            else
                page += " (Auto-Reconnect AUS)";
            page += F("</span></div>");

            page += F("<div class='small' id='connectInfo'></div>");

            page += F("<div class='status-line'>Aktuelle RPM: <span id='rpmVal'>");
            page += String(g_currentRpm);
            page += F("</span> / Max gesehen: <span id='rpmMaxVal'>");
            page += String(g_maxSeenRpm);
            page += F("</span></div>");

            page += F("</div>");

            page += F("<div class='section'>");
            page += F("<div class='section-title'>Debug"
                      "<span id='debugSpinner' class='spinner hidden'></span>"
                      "</div>");
            page += F("<div class='row small'>Letzter TX: <span id='lastTx'>");
            page += g_lastTxInfo;
            page += F("</span></div>");
            page += F("<div class='row small'>Letzte OBD-Zeile: <span id='lastObd'>");
            page += g_lastObdInfo;
            page += F("</span></div>");
            page += F("</div>");
        }

        page += F("<button type='button' id='btnSave' disabled>Speichern</button>");
        page += F("<button type='button' id='btnTest'>Testlauf: RPM-Sweep</button>");

        if (g_devMode)
        {
            page += "<button type='button' id='btnConnect' ";
            if (g_connected)
                page += "style='display:none'";
            page += ">Jetzt mit OBD verbinden</button>";

            page += "<button type='button' class='btn-danger' id='btnDisconnect' ";
            if (!g_connected)
                page += "style='display:none'";
            page += ">OBD trennen</button>";
        }

        page += F("</form>");

        page += F("<div class='small' style='text-align:center;margin-top:16px;'>");
        page += WiFi.softAPIP().toString();
        page += F("</div>");

        page += F(
            "<script>"
            "let saveDirty=false;"
            "function markDirty(){saveDirty=true;var b=document.getElementById('btnSave');if(b)b.disabled=false;}"
            "function onBrightnessChange(v){document.getElementById('bval').innerText=v;"
            "document.getElementById('brightness').value=v;markDirty();"
            "fetch('/brightness?val='+v).catch(()=>{});}"
            "function getColorName(hex){"
            "if(!hex)return'Farbe';"
            "hex=hex.trim();"
            "if(hex.length<7)return'Farbe';"
            "let r=parseInt(hex.substr(1,2),16)/255;"
            "let g=parseInt(hex.substr(3,2),16)/255;"
            "let b=parseInt(hex.substr(5,2),16)/255;"
            "let max=Math.max(r,g,b);"
            "let min=Math.min(r,g,b);"
            "let d=max-min;"
            "if(max<0.1)return'Schwarz';"
            "if(max>0.92&&d<0.05)return'Weiß';"
            "if(d<0.08)return'Grau';"
            "let h=0;"
            "if(d>0){"
            "if(max===r){h=((g-b)/d+(g<b?6:0));}"
            "else if(max===g){h=((b-r)/d+2);}"
            "else{h=((r-g)/d+4);}h*=60;"
            "}"
            "if(h<30)return'Rot';"
            "if(h<60)return'Orange';"
            "if(h<90)return'Gelb';"
            "if(h<150)return'Grün';"
            "if(h<210)return'Cyan';"
            "if(h<270)return'Blau';"
            "if(h<330)return'Violett';"
            "return'Magenta';"
            "}"
            "function updateColorLabels(){"
            "var c1=document.getElementById('color1');"
            "var c2=document.getElementById('color2');"
            "var c3=document.getElementById('color3');"
            "var l1=document.getElementById('color1Label');"
            "var l2=document.getElementById('color2Label');"
            "var l3=document.getElementById('color3Label');"
            "if(c1&&l1) l1.innerText=getColorName(c1.value)+' Ende (% von Max RPM)';"
            "if(c2&&l2) l2.innerText=getColorName(c2.value)+' Ende (% von Max RPM)';"
            "if(c3&&l3) l3.innerText=getColorName(c3.value)+' / Blink Start (% von Max RPM)';"
            "}"
            "function refreshSliderValues(){"
            "var g=document.getElementById('greenEndSlider');"
            "var y=document.getElementById('yellowEndSlider');"
            "var b=document.getElementById('blinkStartSlider');"
            "var gv=document.getElementById('greenEndVal');"
            "var yv=document.getElementById('yellowEndVal');"
            "var bv=document.getElementById('blinkStartVal');"
            "if(g&&gv) gv.innerText=g.value+'%';"
            "if(y&&yv) yv.innerText=y.value+'%';"
            "if(b&&bv) bv.innerText=b.value+'%';"
            "}"
            "function enforceSliderOrder(source){"
            "var g=document.getElementById('greenEndSlider');"
            "var y=document.getElementById('yellowEndSlider');"
            "var b=document.getElementById('blinkStartSlider');"
            "if(!g||!y||!b)return;"
            "var gv=parseInt(g.value,10);"
            "var yv=parseInt(y.value,10);"
            "var bv=parseInt(b.value,10);"
            "if(source==='green'){"
            "if(gv>yv){yv=gv;y.value=yv;}"
            "if(yv>bv){bv=yv;b.value=bv;}"
            "}else if(source==='yellow'){"
            "if(yv<gv){yv=gv;y.value=yv;}"
            "if(yv>bv){bv=yv;b.value=bv;}"
            "}else{"
            "if(bv<yv){bv=yv;b.value=bv;}"
            "if(yv<gv){yv=gv;y.value=yv;}"
            "}"
            "refreshSliderValues();"
            "}"
            "function onSaveClicked(){"
            "var btn=document.getElementById('btnSave');"
            "btn.disabled=true;"
            "var form=document.getElementById('mainForm');"
            "var data=new FormData(form);"
            "fetch('/save',{method:'POST',body=data})"
            ".then(r=>r.text())"
            ".then(_=>{saveDirty=false;btn.disabled=true;})"
            ".catch(_=>{btn.disabled=false;});"
            "}"
            "function onTestClicked(){"
            "var btn=document.getElementById('btnTest');"
            "btn.disabled=true;"
            "var form=document.getElementById('mainForm');"
            "var data=new FormData(form);"
            "fetch('/test',{method:'POST',body=data})"
            ".then(r=>r.text())"
            ".finally(()=>{btn.disabled=false;});"
            "}"
            "function postSimple(path){fetch(path,{method:'POST'}).catch(()=>{});}"
            "function updateConnectUi(s){"
            "var btnC=document.getElementById('btnConnect');"
            "var btnD=document.getElementById('btnDisconnect');"
            "if(btnC&&btnD){"
            "if(s.connected){"
            "btnC.style.display='none';"
            "btnD.style.display='block';"
            "btnD.disabled=false;"
            "}else{"
            "btnC.style.display='block';"
            "btnD.style.display='none';"
            "}"
            "}"
            "if(btnC){"
            "if(s.connectLoop){"
            "btnC.disabled=true;"
            "btnC.innerText='Verbindungsversuch läuft...';"
            "}else{"
            "btnC.disabled=false;"
            "btnC.innerText='Jetzt mit OBD verbinden';"
            "}"
            "}"
            "var info=document.getElementById('connectInfo');"
            "if(info){"
            "if(s.connectLoop&&s.connectTotal>0){"
            "var active=s.connectTotal-s.connectRemaining+1;"
            "if(active<1) active=1;"
            "info.innerText=(s.manualLoop?'Manueller ':'Auto-')+'Versuch '+active+' / '+s.connectTotal;"
            "}else{info.innerText='';}"
            "}"
            "}"
            "function fetchStatus(){"
            "fetch('/status').then(r=>r.json()).then(s=>{"
            "var e;"
            "if((e=document.getElementById('rpmVal')))e.innerText=s.rpm;"
            "if((e=document.getElementById('rpmMaxVal')))e.innerText=s.maxRpm;"
            "if((e=document.getElementById('lastTx')))e.innerText=s.lastTx;"
            "if((e=document.getElementById('lastObd')))e.innerText=s.lastObd;"
            "if((e=document.getElementById('bleStatus')))e.innerText=s.bleText;"
            "var sp=document.getElementById('debugSpinner');"
            "if(sp){if(s.obdLoading){sp.classList.remove('hidden');}"
            "else{sp.classList.add('hidden');}}"
            "updateConnectUi(s);"
            "}).catch(()=>{});"
            "}"
            "function initUI(){"
            "var form=document.getElementById('mainForm');"
            "if(form){"
            "form.querySelectorAll('input,select').forEach(el=>{"
            "if(el.id==='brightness_slider')return;"
            "el.addEventListener('change',markDirty);"
            "el.addEventListener('input',markDirty);"
            "});"
            "}"
            "var g=document.getElementById('greenEndSlider');"
            "if(g)g.addEventListener('input',()=>{enforceSliderOrder('green');markDirty();});"
            "var y=document.getElementById('yellowEndSlider');"
            "if(y)y.addEventListener('input',()=>{enforceSliderOrder('yellow');markDirty();});"
            "var b=document.getElementById('blinkStartSlider');"
            "if(b)b.addEventListener('input',()=>{enforceSliderOrder('blink');markDirty();});"
            "var c1=document.getElementById('color1');"
            "if(c1)c1.addEventListener('input',()=>{updateColorLabels();markDirty();});"
            "var c2=document.getElementById('color2');"
            "if(c2)c2.addEventListener('input',()=>{updateColorLabels();markDirty();});"
            "var c3=document.getElementById('color3');"
            "if(c3)c3.addEventListener('input',()=>{updateColorLabels();markDirty();});"
            "var sb=document.getElementById('btnSave');"
            "if(sb)sb.addEventListener('click',onSaveClicked);"
            "var tb=document.getElementById('btnTest');"
            "if(tb)tb.addEventListener('click',onTestClicked);"
            "var bc=document.getElementById('btnConnect');"
            "if(bc)bc.addEventListener('click',()=>postSimple('/connect'));"
            "var bd=document.getElementById('btnDisconnect');"
            "if(bd)bd.addEventListener('click',()=>postSimple('/disconnect'));"
            "refreshSliderValues();"
            "updateColorLabels();"
            "fetchStatus();"
            "setInterval(fetchStatus,1000);"
            "}"
            "document.addEventListener('DOMContentLoaded',initUI);"
            "</script>");

        page += F("</body></html>");
        return page;
    }

    String settingsPage()
    {
        String page;
        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        page += F("<title>ShiftLight Einstellungen</title>");
        page += F("<style>"
                  "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;"
                  "color:#eee;padding:16px;margin:0;}"
                  "h1{font-size:20px;margin:0 0 12px 0;display:flex;"
                  "align-items:center;justify-content:space-between;}"
                  "a{color:#0af;text-decoration:none;}"
                  ".section{margin-top:12px;padding:10px 12px;border-radius:8px;"
                  "background:#181818;border:1px solid #333;}"
                  ".section-title{font-weight:600;margin-bottom:4px;font-size:14px;}"
                  ".toggle-row{display:flex;justify-content:space-between;align-items:center;margin-top:8px;}"
                  ".toggle-label{font-size:14px;}"
                  ".switch{position:relative;display:inline-block;width:46px;height:24px;margin-left:8px;}"
                  ".switch input{opacity:0;width:0;height:0;}"
                  ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;"
                  "background:#555;transition:.2s;border-radius:24px;}"
                  ".slider:before{position:absolute;content:'';height:18px;width:18px;left:3px;top:3px;"
                  "background:#fff;transition:.2s;border-radius:50%;}"
                  ".switch input:checked + .slider{background:#0af;}"
                  ".switch input:checked + .slider:before{transform:translateX(22px);}"
                  "button{margin-top:16px;width:100%;padding:10px;border:none;border-radius:6px;"
                  "background:#0af;color:#000;font-weight:bold;font-size:14px;}"
                  "</style></head><body>");

        page += "<h1><a href=\"/\">‹ Zurück</a><span>Einstellungen</span></h1>";

        page += F("<form id='settingsForm' method='POST' action='/settings'>");
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Modus</div>");
        page += F("<div class='toggle-row'><span class='toggle-label'>Entwicklermodus</span>"
                  "<label class='switch'>");
        page += "<input type='checkbox' name='devMode' ";
        if (g_devMode)
            page += "checked";
        page += "><span class='slider'></span></label></div>";
        page += F("<div style='font-size:12px;color:#aaa;margin-top:8px;'>"
                  "Wenn der Entwicklermodus deaktiviert ist, werden Debug-Infos und OBD-Steuerung "
                  "im Hauptbildschirm ausgeblendet und OBD-Auto-Reconnect bleibt immer aktiv."
                  "</div>");
        page += F("</div>");
        page += F("<button type='submit' id='btnSettingsSave' disabled>Speichern</button>");
        page += F("</form>");

        page += F("<div class='section'>");
        page += F("<div class='section-title'>Mein Fahrzeug</div>");
        page += F("<div class='field-desc'>Die Angaben werden beim Herstellen der OBD-Verbindung ermittelt.</div>");
        page += "<div class='row small'>VIN: <strong>" + readVehicleVin() + "</strong></div>";
        page += "<div class='row small'>Modell: <strong>" + readVehicleModel() + "</strong></div>";
        page += "<div class='row small'>Marke: <strong>" + readVehicleBrand() + "</strong></div>";
        String diagIcon = readVehicleDiagOk() ? "✅" : "❌";
        String diagText = readVehicleDiagOk() ? "Diagnose OK" : "Keine Diagnose verfügbar";
        page += "<div class='row small'>Diagnose: " + diagIcon + " " + diagText + "</div>";
        page += F("</div>");

        page += F("<script>document.addEventListener('DOMContentLoaded',()=>{const form=document.getElementById('settingsForm');const btn=document.getElementById('btnSettingsSave');if(!form||!btn)return;form.querySelectorAll('input').forEach(el=>{el.addEventListener('change',()=>{btn.disabled=false;});});});</script>");

        page += F("</body></html>");
        return page;
    }

    void handleRoot()
    {
        g_lastHttpMs = millis();
        server.send(200, "text/html", htmlPage());
    }

    void handleSave()
    {
        g_lastHttpMs = millis();

        if (server.hasArg("mode"))
        {
            cfg.mode = server.arg("mode").toInt();
            if (cfg.mode < 0 || cfg.mode > 2)
                cfg.mode = 1;
        }

        if (server.hasArg("brightness"))
        {
            int b = server.arg("brightness").toInt();
            if (b < 0)
                b = 0;
            if (b > 255)
                b = 255;
            cfg.brightness = b;
            setLedTargetBrightness(cfg.brightness);
        }

        cfg.autoScaleMaxRpm = server.hasArg("autoscale");

        if (server.hasArg("fixedMaxRpm"))
        {
            int fm = server.arg("fixedMaxRpm").toInt();
            if (fm < 1000)
                fm = 1000;
            if (fm > 8000)
                fm = 8000;
            cfg.fixedMaxRpm = fm;
        }

        if (server.hasArg("greenEndPct"))
        {
            int v = server.arg("greenEndPct").toInt();
            cfg.greenEndPct = clampInt(v, 0, 100);
        }

        if (server.hasArg("yellowEndPct"))
        {
            int v = server.arg("yellowEndPct").toInt();
            cfg.yellowEndPct = clampInt(v, 0, 100);
        }

        if (server.hasArg("blinkStartPct"))
        {
            int v = server.arg("blinkStartPct").toInt();
            cfg.blinkStartPct = clampInt(v, 0, 100);
        }

        if (cfg.greenEndPct > cfg.yellowEndPct)
            cfg.yellowEndPct = cfg.greenEndPct;
        if (cfg.yellowEndPct > cfg.blinkStartPct)
            cfg.blinkStartPct = cfg.yellowEndPct;

        cfg.logoOnIgnitionOn = server.hasArg("logoIgnOn");
        cfg.logoOnEngineStart = server.hasArg("logoEngStart");
        cfg.logoOnIgnitionOff = server.hasArg("logoIgnOff");

        if (server.hasArg("color1"))
        {
            cfg.color1 = parseHtmlColor(server.arg("color1"), cfg.color1);
        }
        if (server.hasArg("color2"))
        {
            cfg.color2 = parseHtmlColor(server.arg("color2"), cfg.color2);
        }
        if (server.hasArg("color3"))
        {
            cfg.color3 = parseHtmlColor(server.arg("color3"), cfg.color3);
        }

        if (server.hasArg("manualAttempts"))
        {
            int attempts = clampInt(server.arg("manualAttempts").toInt(), 5, 10);
            cfg.manualConnectAttempts = attempts;
        }

        bool prevAuto = g_autoReconnect;
        if (g_devMode)
        {
            g_autoReconnect = server.hasArg("autoReconnect");
        }
        else
        {
            g_autoReconnect = true;
        }

        if (!g_autoReconnect)
        {
            cancelConnectLoop();
        }
        else if (!prevAuto && !g_connected)
        {
            scheduleConnectLoop(cfg.manualConnectAttempts, false);
        }

        server.send(200, "text/plain", "OK");
    }

    void handleTest()
    {
        g_lastHttpMs = millis();

        if (server.hasArg("fixedMaxRpm"))
        {
            int fm = server.arg("fixedMaxRpm").toInt();
            if (fm < 1000)
                fm = 1000;
            if (fm > 8000)
                fm = 8000;
            cfg.fixedMaxRpm = fm;
        }
        cfg.autoScaleMaxRpm = server.hasArg("autoscale");

        if (server.hasArg("greenEndPct"))
        {
            int v = server.arg("greenEndPct").toInt();
            if (v < 0)
                v = 0;
            if (v > 100)
                v = 100;
            cfg.greenEndPct = v;
        }

        if (server.hasArg("yellowEndPct"))
        {
            int v = server.arg("yellowEndPct").toInt();
            if (v < 0)
                v = 0;
            if (v > 100)
                v = 100;
            cfg.yellowEndPct = v;
        }

        if (server.hasArg("blinkStartPct"))
        {
            int v = server.arg("blinkStartPct").toInt();
            if (v < 0)
                v = 0;
            if (v > 100)
                v = 100;
            cfg.blinkStartPct = v;
        }

        if (cfg.greenEndPct > cfg.yellowEndPct)
            cfg.yellowEndPct = cfg.greenEndPct;
        if (cfg.yellowEndPct > cfg.blinkStartPct)
            cfg.blinkStartPct = cfg.yellowEndPct;

        if (!cfg.autoScaleMaxRpm)
        {
            g_testMaxRpm = (cfg.fixedMaxRpm > 1000) ? cfg.fixedMaxRpm : 4000;
        }
        else
        {
            g_testMaxRpm = (g_maxSeenRpm > 0) ? g_maxSeenRpm : 4000;
        }

        g_testActive = true;
        g_testStartMs = millis();

        Serial.print("Starte Test-Sweep bis ");
        Serial.print(g_testMaxRpm);
        Serial.println(" RPM");

        server.send(200, "text/plain", "OK");
    }

    void handleBrightness()
    {
        g_lastHttpMs = millis();

        if (server.hasArg("val"))
        {
            int b = server.arg("val").toInt();
            if (b < 0)
                b = 0;
            if (b > 255)
                b = 255;

            cfg.brightness = b;
            setLedTargetBrightness(cfg.brightness);

            showMLogoPreview();

            g_brightnessPreviewActive = true;
            g_brightnessPreviewFading = false;
            g_lastBrightnessChangeMs = millis();
        }

        server.send(200, "text/plain", "OK");
    }

    void handleConnect()
    {
        g_lastHttpMs = millis();

        Serial.println("[WEB] Manueller Connect-Button gedrückt.");
        scheduleConnectLoop(cfg.manualConnectAttempts, true);
        server.send(200, "text/plain", "OK");
    }

    void handleDisconnect()
    {
        g_lastHttpMs = millis();

        Serial.println("[WEB] Manueller Disconnect-Button gedrückt.");
        cancelConnectLoop();
        if (g_client && g_connected)
        {
            g_client->disconnect();
        }

        server.send(200, "text/plain", "OK");
    }

    void handleStatus()
    {
        g_lastHttpMs = millis();

        unsigned long now = millis();
        bool obdLoading = g_connected && (now - g_lastObdMs < 1200);

        String json = "{";
        json += "\"rpm\":" + String(g_currentRpm);
        json += ",\"maxRpm\":" + String(g_maxSeenRpm);
        json += ",\"lastTx\":\"" + g_lastTxInfo + "\"";
        json += ",\"lastObd\":\"" + g_lastObdInfo + "\"";
        json += ",\"connected\":" + String(g_connected ? "true" : "false");
        json += ",\"autoReconnect\":" + String(g_autoReconnect ? "true" : "false");
        json += ",\"devMode\":" + String(g_devMode ? "true" : "false");
        json += ",\"bleText\":\"";
        if (g_connected)
            json += "Verbunden";
        else
            json += "Getrennt";
        if (g_autoReconnect)
            json += " (Auto-Reconnect AN)";
        else
            json += " (Auto-Reconnect AUS)";
        json += "\"";
        json += ",\"obdLoading\":";
        json += obdLoading ? "true" : "false";
        json += ",\"connectLoop\":";
        json += g_connectLoopActive ? "true" : "false";
        json += ",\"manualLoop\":";
        json += g_manualConnectLoop ? "true" : "false";
        json += ",\"connectRemaining\":" + String(g_connectLoopRemaining);
        json += ",\"connectTotal\":" + String(g_connectLoopTotal);
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

        g_devMode = server.hasArg("devMode");
        if (!g_devMode)
        {
            g_autoReconnect = true;
        }

        server.sendHeader("Location", "/settings");
        server.send(303);
    }
}

void initWifiAP()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    IPAddress ip = WiFi.softAPIP();
    Serial.print("Access Point gestartet: ");
    Serial.println(AP_SSID);
    Serial.print("AP IP: ");
    Serial.println(ip);
}

void initWebUi()
{
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/test", HTTP_POST, handleTest);
    server.on("/brightness", HTTP_GET, handleBrightness);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/disconnect", HTTP_POST, handleDisconnect);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/settings", HTTP_GET, handleSettingsGet);
    server.on("/settings", HTTP_POST, handleSettingsSave);

    server.begin();
    Serial.println("Webserver gestartet (http://192.168.4.1/)");
}

void webUiLoop()
{
    server.handleClient();
}

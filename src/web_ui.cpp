#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ble_obd.h"
#include "config.h"
#include "led_bar.h"
#include "logo_anim.h"
#include "state.h"
#include "display.h"

namespace
{
    WebServer server(80);

    String htmlPage()
    {
        String page;
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
                  "@keyframes spin{from{transform:rotate(0deg);}to{transform:rotate(360deg);}}"
                  "</style></head><body>");

        page += "<h1><span>ShiftLight Setup</span>"
                "<a href=\"/settings\" style='text-decoration:none;color:#0af;font-size:20px;'>⚙️</a>"
                "</h1>";

        page += F("<form id='mainForm' method='POST' action='/save'>");

        page += F("<div class='section'>");
        page += F("<div class='section-title'>Allgemein</div>");

        page += F("<label>Mode</label><select name='mode'>");
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
        page += "<input type='range' min='0' max='255' value='";
        page += String(cfg.brightness);
        page += "' id='brightness_slider' oninput='onBrightnessChange(this.value)'>";
        page += "<div class='small'>Wert: <span id='bval'>";
        page += String(cfg.brightness);
        page += "</span></div>";
        page += "<input type='hidden' name='brightness' id='brightness' value='";
        page += String(cfg.brightness);
        page += "'>";

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

        page += F("<label>Fixed Max RPM (wenn Auto-Scale aus)</label>");
        page += "<input type='number' name='fixedMaxRpm' min='1000' max='8000' value='";
        page += String(cfg.fixedMaxRpm);
        page += "'>";

        page += F("<label>Green End (% von Max RPM)</label>");
        page += "<input type='range' name='greenEndPct' min='0' max='100' value='";
        page += String(cfg.greenEndPct);
        page += "' id='greenEndSlider'>";
        page += "<div class='small'>Wert: <span id='greenEndVal'>";
        page += String(cfg.greenEndPct);
        page += "%</span></div>";

        page += F("<label>Yellow End (% von Max RPM)</label>");
        page += "<input type='range' name='yellowEndPct' min='0' max='100' value='";
        page += String(cfg.yellowEndPct);
        page += "' id='yellowEndSlider'>";
        page += "<div class='small'>Wert: <span id='yellowEndVal'>";
        page += String(cfg.yellowEndPct);
        page += "%</span></div>";

        page += F("<label>Blink Start (% von Max RPM)</label>");
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

        page += F("<div class='toggle-row'><span class='toggle-label'>M-Logo bei Motorstart</span>"
                  "<label class='switch'>");
        page += "<input type='checkbox' name='logoEngStart' ";
        if (cfg.logoOnEngineStart)
            page += "checked";
        page += "><span class='slider'></span></label></div>";

        page += F("<div class='toggle-row'><span class='toggle-label'>Leaving-Animation bei Zündung aus</span>"
                  "<label class='switch'>");
        page += "<input type='checkbox' name='logoIgnOff' ";
        if (cfg.logoOnIgnitionOff)
            page += "checked";
        page += "><span class='slider'></span></label></div>";

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

            page += F("<div class='status-line'>Aktuelle RPM: <span id='rpmVal'>");
            page += String(g_currentRpm);
            page += F("</span> / Max gesehen: <span id='rpmMaxVal'>");
            page += String(g_maxSeenRpm);
            page += F("</span></div>");
            page += F("<button type='button' id='btnDisplayLogo'>BMW Logo auf Display anzeigen</button>");
            page += F("<div class='small'>Zeigt kurz das BMW-Logo auf dem Display (nur im Entwicklermodus).</div>");

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

        page += F("<script>"
                  "let saveDirty=false;"
                  "function markDirty(){"
                  " saveDirty=true;"
                  " var b=document.getElementById('btnSave');"
                  " if(b) b.disabled=false;"
                  "}"
                  "function onBrightnessChange(v){"
                  " document.getElementById('bval').innerText=v;"
                  " document.getElementById('brightness').value=v;"
                  " markDirty();"
                  " fetch('/brightness?val='+v).catch(()=>{});"
                  "}"
                  "function onSaveClicked(){"
                  " var btn=document.getElementById('btnSave');"
                  " btn.disabled=true;"
                  " var form=document.getElementById('mainForm');"
                  " var data=new FormData(form);"
                  " fetch('/save',{method:'POST',body:data})"
                  "  .then(r=>r.text())"
                  "  .then(_=>{saveDirty=false;btn.disabled=true;})"
                  "  .catch(_=>{btn.disabled=false;});"
                  "}"
                  "function onTestClicked(){"
                  " var btn=document.getElementById('btnTest');"
                  " btn.disabled=true;"
                  " var form=document.getElementById('mainForm');"
                  " var data=new FormData(form);"
                  " fetch('/test',{method:'POST',body:data})"
                  "  .then(r=>r.text())"
                  "  .finally(()=>{btn.disabled=false;});"
                  "}"
                  "function postSimple(path){"
                  " var sp=document.getElementById('debugSpinner');"
                  " if(sp) sp.classList.remove('hidden');"
                  " fetch(path,{method:'POST'}).catch(()=>{});"
                  "}"
                  "function fetchStatus(){"
                  " var sp=document.getElementById('debugSpinner');"
                  " if(sp) sp.classList.remove('hidden');"
                  " fetch('/status')"
                  "  .then(r=>r.json())"
                  "  .then(s=>{"
                  "    var e;"
                  "    if((e=document.getElementById('rpmVal'))) e.innerText=s.rpm;"
                  "    if((e=document.getElementById('rpmMaxVal'))) e.innerText=s.maxRpm;"
                  "    if((e=document.getElementById('lastTx'))) e.innerText=s.lastTx;"
                  "    if((e=document.getElementById('lastObd'))) e.innerText=s.lastObd;"
                  "    if((e=document.getElementById('bleStatus'))) e.innerText=s.bleText;"
                  "    var btnC=document.getElementById('btnConnect');"
                  "    var btnD=document.getElementById('btnDisconnect');"
                  "    if(btnC && btnD){"
                  "      if(s.connected){btnC.style.display='none';btnD.style.display='block';}"
                  "      else{btnC.style.display='block';btnD.style.display='none';}"
                  "    }"
                  "  })"
                  "  .finally(()=>{if(sp) sp.classList.add('hidden');});"
                  "}"
                  "function initUI(){"
                  " var form=document.getElementById('mainForm');"
                  " if(form){"
                  "  form.querySelectorAll('input,select').forEach(el=>{"
                  "    if(el.id==='brightness_slider') return;"
                  "    el.addEventListener('change',markDirty);"
                  "    el.addEventListener('input',markDirty);"
                  "  });"
                  " }"
                  " var g=document.getElementById('greenEndSlider');"
                  " if(g) g.addEventListener('input',e=>{"
                  "   var v=e.target.value;var t=document.getElementById('greenEndVal');"
                  "   if(t) t.innerText=v+'%';});"
                  " var y=document.getElementById('yellowEndSlider');"
                  " if(y) y.addEventListener('input',e=>{"
                  "   var v=e.target.value;var t=document.getElementById('yellowEndVal');"
                  "   if(t) t.innerText=v+'%';});"
                  " var b=document.getElementById('blinkStartSlider');"
                  " if(b) b.addEventListener('input',e=>{"
                  "   var v=e.target.value;var t=document.getElementById('blinkStartVal');"
                  "   if(t) t.innerText=v+'%';});"
                  " var sb=document.getElementById('btnSave');"
                  " if(sb) sb.addEventListener('click',onSaveClicked);"
                  " var tb=document.getElementById('btnTest');"
                  " if(tb) tb.addEventListener('click',onTestClicked);"
                  " var bc=document.getElementById('btnConnect');"
                  " if(bc) bc.addEventListener('click',()=>postSimple('/connect'));"
                  " var bd=document.getElementById('btnDisconnect');"
                  " if(bd) bd.addEventListener('click',()=>postSimple('/disconnect'));"
                  " var bdsp=document.getElementById('btnDisplayLogo');"
                  " if(bdsp) bdsp.addEventListener('click',()=>postSimple('/dev/display-logo'));"
                  " fetchStatus();"
                  " setInterval(fetchStatus,1000);"
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

        page += F("<form method='POST' action='/settings'>");
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
        page += F("<button type='submit'>Speichern</button>");
        page += F("</form>");

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
            strip.setBrightness(cfg.brightness);
            strip.show();
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

        cfg.logoOnIgnitionOn = server.hasArg("logoIgnOn");
        cfg.logoOnEngineStart = server.hasArg("logoEngStart");
        cfg.logoOnIgnitionOff = server.hasArg("logoIgnOff");

        if (g_devMode)
        {
            g_autoReconnect = server.hasArg("autoReconnect");
        }
        else
        {
            g_autoReconnect = true;
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
            strip.setBrightness(cfg.brightness);

            showMLogoPreview();

            g_brightnessPreviewActive = true;
            g_lastBrightnessChangeMs = millis();
        }

        server.send(200, "text/plain", "OK");
    }

    void handleConnect()
    {
        g_lastHttpMs = millis();

        Serial.println("[WEB] Manueller Connect-Button gedrückt.");
        bool ok = connectToObd();
        if (!ok)
        {
            Serial.println("[WEB] Manueller Connect fehlgeschlagen.");
        }

        server.send(200, "text/plain", ok ? "OK" : "FAIL");
    }

    void handleDisconnect()
    {
        g_lastHttpMs = millis();

        Serial.println("[WEB] Manueller Disconnect-Button gedrückt.");
        if (g_client && g_connected)
        {
            g_client->disconnect();
        }

        server.send(200, "text/plain", "OK");
    }

    void handleStatus()
    {
        g_lastHttpMs = millis();

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
        json += "\"}";
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
    
    void handleDevDisplayLogo()
    {
        g_lastHttpMs = millis();

        if (!g_devMode)
        {
            server.send(403, "text/plain", "Forbidden");
            return;
        }

        displayShowTestLogo();
        // Kein Redirect mehr, einfache OK-Antwort für fetch()
        server.send(200, "text/plain", "OK");
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
    server.on("/dev/display-logo", HTTP_POST, handleDevDisplayLogo);

    server.begin();
    Serial.println("Webserver gestartet (http://192.168.4.1/)");
}

void webUiLoop()
{
    server.handleClient();
}

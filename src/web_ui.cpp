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
        RgbColor c;
        c.r = (uint8_t)strtol(value.substring(1, 3).c_str(), nullptr, 16);
        c.g = (uint8_t)strtol(value.substring(3, 5).c_str(), nullptr, 16);
        c.b = (uint8_t)strtol(value.substring(5, 7).c_str(), nullptr, 16);
        return c;
    }

    String safeLabel(const String &value, const String &fallback)
    {
        String trimmed = value;
        trimmed.trim();
        if (trimmed.length() == 0)
            return fallback;
        return trimmed;
    }

    String jsonEscape(const String &input)
    {
        String out;
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
                out += "\\n";
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

    String htmlPage()
    {
        String page;
        String greenName = (cfg.greenLabel.length() > 0) ? cfg.greenLabel : String("Green");
        String yellowName = (cfg.yellowLabel.length() > 0) ? cfg.yellowLabel : String("Yellow");
        String redName = (cfg.redLabel.length() > 0) ? cfg.redLabel : String("Red");
        String greenHex = colorToHex(cfg.greenColor);
        String yellowHex = colorToHex(cfg.yellowColor);
        String redHex = colorToHex(cfg.redColor);
        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        page += F("<title>ShiftLight Setup</title>");
        page += F("<style>"
                  "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;"
                  "color:#eee;padding:16px;margin:0;}"
                  "h1{font-size:20px;margin:0 0 12px 0;display:flex;"
                  "align-items:center;justify-content:space-between;}"
                  "h2{font-size:18px;margin:18px 0 10px 0;}"
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
                  ".section-title{font-weight:600;margin-bottom:8px;font-size:18px;letter-spacing:0.3px;}"
                  ".section-title small{display:block;font-size:12px;font-weight:400;color:#aaa;}"
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
                  ".color-row{border:1px solid #222;border-radius:6px;padding:8px;margin-top:8px;background:#151515;}"
                  ".color-row h3{margin:0 0 6px 0;font-size:14px;color:#bbb;}"
                  ".color-row label{margin-top:6px;font-size:12px;color:#aaa;}"
                  ".color-row input[type=color]{padding:0;height:40px;}")
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

        page += F("<div class='color-row'>");
        page += F("<h3>Farbe 1 – Low RPM</h3>");
        page += "<label for='greenLabelInput'>Bezeichnung</label><input type='text' id='greenLabelInput' name='greenLabel' value='";
        page += greenName;
        page += "'>";
        page += "<label for='greenColorInput'>Farbe</label><input type='color' id='greenColorInput' name='greenColor' value='";
        page += greenHex;
        page += "'>";
        page += F("</div>");

        page += F("<div class='color-row'>");
        page += F("<h3>Farbe 2 – Mid RPM</h3>");
        page += "<label for='yellowLabelInput'>Bezeichnung</label><input type='text' id='yellowLabelInput' name='yellowLabel' value='";
        page += yellowName;
        page += "'>";
        page += "<label for='yellowColorInput'>Farbe</label><input type='color' id='yellowColorInput' name='yellowColor' value='";
        page += yellowHex;
        page += "'>";
        page += F("</div>");

        page += F("<div class='color-row'>");
        page += F("<h3>Farbe 3 – Shift / Warnung</h3>");
        page += "<label for='redLabelInput'>Bezeichnung</label><input type='text' id='redLabelInput' name='redLabel' value='";
        page += redName;
        page += "'>";
        page += "<label for='redColorInput'>Farbe</label><input type='color' id='redColorInput' name='redColor' value='";
        page += redHex;
        page += "'>";
        page += F("</div>");

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

        page += "<label id='greenEndLabel' data-fallback='Green'>";
        page += greenName;
        page += " End (% von Max RPM)</label>";
        page += "<input type='range' name='greenEndPct' min='0' max='100' value='"; 
        page += String(cfg.greenEndPct);
        page += "' id='greenEndSlider' data-display='greenEndVal'>";
        page += "<div class='small'>Wert: <span id='greenEndVal'>";
        page += String(cfg.greenEndPct);
        page += "%</span></div>";

        page += "<label id='yellowEndLabel' data-fallback='Yellow'>";
        page += yellowName;
        page += " End (% von Max RPM)</label>";
        page += "<input type='range' name='yellowEndPct' min='0' max='100' value='"; 
        page += String(cfg.yellowEndPct);
        page += "' id='yellowEndSlider' data-display='yellowEndVal'>";
        page += "<div class='small'>Wert: <span id='yellowEndVal'>";
        page += String(cfg.yellowEndPct);
        page += "%</span></div>";

        page += "<label id='blinkStartLabel' data-fallback='Red'>";
        page += redName;
        page += " Start (% von Max RPM)</label>";
        page += "<input type='range' name='blinkStartPct' min='0' max='100' value='"; 
        page += String(cfg.blinkStartPct);
        page += "' id='blinkStartSlider' data-display='blinkStartVal'>";
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
                  "let lastStatus=null;"
                  "let pendingSpinner=0;"
                  "let lastSpinnerTs=0;"
                  "function updateSpinnerVisibility(forceHide){"
                  " const sp=document.getElementById('debugSpinner');"
                  " if(!sp) return;"
                  " const idle=(Date.now()-lastSpinnerTs)>3000;"
                  " if(forceHide || pendingSpinner<=0 || idle || (lastStatus && lastStatus.connected===false)){"
                  "  sp.classList.add('hidden');"
                  " }else{"
                  "  sp.classList.remove('hidden');"
                  " }"
                  "}"
                  "function beginRequest(opts={}){"
                  " const requireConn=opts.onlyWhenConnected||false;"
                  " if(requireConn && lastStatus && lastStatus.connected===false){"
                  "  updateSpinnerVisibility(true);"
                  "  return ()=>{};"
                  " }"
                  " pendingSpinner++;"
                  " lastSpinnerTs=Date.now();"
                  " updateSpinnerVisibility(false);"
                  " return ()=>{pendingSpinner=Math.max(0,pendingSpinner-1);updateSpinnerVisibility(false);};"
                  "}"
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
                  "function updateSliderDisplay(el){"
                  " var targetId=el.dataset.display;"
                  " if(!targetId) return;"
                  " var t=document.getElementById(targetId);"
                  " if(t) t.innerText=el.value+'%';"
                  "}"
                  "function enforceSliderOrder(changedId){"
                  " var g=document.getElementById('greenEndSlider');"
                  " var y=document.getElementById('yellowEndSlider');"
                  " var b=document.getElementById('blinkStartSlider');"
                  " if(!g||!y||!b) return;"
                  " var gv=parseInt(g.value,10);"
                  " var yv=parseInt(y.value,10);"
                  " var bv=parseInt(b.value,10);"
                  " function sync(el,val){"
                  "  if(parseInt(el.value,10)!==val){"
                  "   el.value=val;"
                  "   updateSliderDisplay(el);"
                  "  }"
                  " }"
                  " if(changedId==='greenEndSlider'){"
                  "  if(yv<gv){ yv=gv; sync(y,yv); }"
                  "  if(bv<yv){ bv=yv; sync(b,bv); }"
                  " }else if(changedId==='yellowEndSlider'){"
                  "  if(yv<gv){ gv=yv; sync(g,gv); }"
                  "  if(bv<yv){ bv=yv; sync(b,bv); }"
                  " }else if(changedId==='blinkStartSlider'){"
                  "  if(bv<yv){ yv=bv; sync(y,yv); }"
                  "  if(yv<gv){ gv=yv; sync(g,gv); }"
                  " }"
                  "}"
                  "function handleSliderChange(e){"
                  " enforceSliderOrder(e.target.id);"
                  " updateSliderDisplay(e.target);"
                  " markDirty();"
                  "}"
                  "function updateRangeLabels(){"
                  " var entries=["
                  "  {key:'green',label:'greenEndLabel',suffix:' End (% von Max RPM)'},"
                  "  {key:'yellow',label:'yellowEndLabel',suffix:' End (% von Max RPM)'},"
                  "  {key:'red',label:'blinkStartLabel',suffix:' Start (% von Max RPM)'}"
                  " ];"
                  " entries.forEach(entry=>{"
                  "  var lbl=document.getElementById(entry.label);"
                  "  if(!lbl) return;"
                  "  var nameInput=document.getElementById(entry.key+'LabelInput');"
                  "  var colorInput=document.getElementById(entry.key+'ColorInput');"
                  "  var fallback=lbl.dataset.fallback||'';"
                  "  var base=(nameInput && nameInput.value.trim().length>0)?nameInput.value.trim():fallback;"
                  "  lbl.innerText=base+entry.suffix;"
                  "  if(colorInput && colorInput.value){"
                  "   lbl.style.color=colorInput.value;"
                  "  }"
                  " });"
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
                  " fetch('/test',{method:'POST',body=data})"
                  "  .then(r=>r.text())"
                  "  .finally(()=>{btn.disabled=false;});"
                  "}"
                  "function postSimple(path){"
                  " var done=beginRequest({});"
                  " fetch(path,{method:'POST'})"
                  "  .catch(()=>{})"
                  "  .finally(()=>{done();});"
                  "}"
                  "function fetchStatus(){"
                  " var done=beginRequest({onlyWhenConnected:true});"
                  " fetch('/status')"
                  "  .then(r=>r.json())"
                  "  .then(s=>{"
                  "    lastStatus=s;"
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
                  "  .catch(()=>{})"
                  "  .finally(()=>{done();updateSpinnerVisibility(false);});"
                  "}"
                  "function initUI(){"
                  " var form=document.getElementById('mainForm');"
                  " if(form){"
                  "  form.querySelectorAll('input,select').forEach(el=>{"
                  "    if(el.id==='brightness_slider') return;"
                  "    if(el.type==='range'){"
                  "      el.addEventListener('input',handleSliderChange);"
                  "      el.addEventListener('change',handleSliderChange);"
                  "    }else{"
                  "      el.addEventListener('change',markDirty);"
                  "      el.addEventListener('input',markDirty);"
                  "    }"
                  "  });"
                  " }"
                  " ['green','yellow','red'].forEach(name=>{"
                  "  var color=document.getElementById(name+'ColorInput');"
                  "  if(color){"
                  "    color.addEventListener('input',()=>{updateRangeLabels();markDirty();});"
                  "    color.addEventListener('change',()=>{updateRangeLabels();markDirty();});"
                  "  }"
                  "  var labelInput=document.getElementById(name+'LabelInput');"
                  "  if(labelInput){"
                  "    labelInput.addEventListener('input',()=>{updateRangeLabels();markDirty();});"
                  "  }"
                  " });"
                  " ['greenEndSlider','yellowEndSlider','blinkStartSlider'].forEach(id=>{"
                  "  var el=document.getElementById(id);"
                  "  if(el){"
                  "    updateSliderDisplay(el);"
                  "  }"
                  " });"
                  " updateRangeLabels();"
                  " var sb=document.getElementById('btnSave');"
                  " if(sb) sb.addEventListener('click',onSaveClicked);"
                  " var tb=document.getElementById('btnTest');"
                  " if(tb) tb.addEventListener('click',onTestClicked);"
                  " var bc=document.getElementById('btnConnect');"
                  " if(bc) bc.addEventListener('click',()=>postSimple('/connect'));"
                  " var bd=document.getElementById('btnDisconnect');"
                  " if(bd) bd.addEventListener('click',()=>postSimple('/disconnect'));"
                  " fetchStatus();"
                  " setInterval(fetchStatus,1000);"
                  " setInterval(()=>updateSpinnerVisibility(false),1000);"
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
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>VIN: <strong>" + readVehicleVin() + "</strong></div>";
        page += "<div class='row small'>Modell: <strong>" + readVehicleModel() + "</strong></div>";
        page += "<div class='row small'>Diagnose: <strong>" + readVehicleDiagStatus() + "</strong></div>";
        String vehStatus;
        if (g_vehicleInfoRequestRunning)
        {
            vehStatus = "Abruf läuft ...";
        }
        else if (g_vehicleInfoAvailable && g_vehicleInfoLastUpdate > 0)
        {
            unsigned long age = (millis() - g_vehicleInfoLastUpdate) / 1000;
            vehStatus = String("Letztes Update vor ") + String(age) + String("s");
        }
        else
        {
            vehStatus = "Noch keine Daten (wartet auf OBD-Verbindung)";
        }
        page += "<div class='row small'>Status: " + vehStatus + "</div>";
        page += F("</div>");
        page += F("<button type='submit' id='settingsSave' disabled>Speichern</button>");
        page += F("</form>");

        page += F("<script>"
                  "document.addEventListener('DOMContentLoaded',()=>{"
                  " var form=document.getElementById('settingsForm');"
                  " var btn=document.getElementById('settingsSave');"
                  " if(form && btn){"
                  "  form.querySelectorAll('input').forEach(el=>{"
                  "    el.addEventListener('change',()=>{btn.disabled=false;});"
                  "  });"
                  " }"
                  "});"
                  "</script>");

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

        if (server.hasArg("greenColor"))
        {
            cfg.greenColor = parseHexColor(server.arg("greenColor"), cfg.greenColor);
        }
        if (server.hasArg("yellowColor"))
        {
            cfg.yellowColor = parseHexColor(server.arg("yellowColor"), cfg.yellowColor);
        }
        if (server.hasArg("redColor"))
        {
            cfg.redColor = parseHexColor(server.arg("redColor"), cfg.redColor);
        }

        if (server.hasArg("greenLabel"))
        {
            cfg.greenLabel = safeLabel(server.arg("greenLabel"), "Green");
        }
        if (server.hasArg("yellowLabel"))
        {
            cfg.yellowLabel = safeLabel(server.arg("yellowLabel"), "Yellow");
        }
        if (server.hasArg("redLabel"))
        {
            cfg.redLabel = safeLabel(server.arg("redLabel"), "Red");
        }

        bool previousAutoReconnect = g_autoReconnect;
        if (g_devMode)
        {
            g_autoReconnect = server.hasArg("autoReconnect");
        }
        else
        {
            g_autoReconnect = true;
        }

        if (!previousAutoReconnect && g_autoReconnect)
        {
            g_forceImmediateReconnect = true;
            g_lastBleRetryMs = 0;
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
        bool ok = false;
        for (int attempt = 1; attempt <= MANUAL_CONNECT_RETRY_COUNT; ++attempt)
        {
            Serial.printf("[WEB] Verbinde Versuch %d/%d...\n", attempt, MANUAL_CONNECT_RETRY_COUNT);
            ok = connectToObd();
            if (ok)
            {
                Serial.println("[WEB] Manueller Connect erfolgreich.");
                break;
            }
            delay(MANUAL_CONNECT_RETRY_DELAY_MS);
        }

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
        json += "\"";
        json += ",\"vehicleVin\":\"" + jsonEscape(g_vehicleVin) + "\"";
        json += ",\"vehicleModel\":\"" + jsonEscape(g_vehicleModel) + "\"";
        json += ",\"vehicleDiag\":\"" + jsonEscape(g_vehicleDiagStatus) + "\"";
        json += ",\"vehicleInfoReady\":" + String(g_vehicleInfoAvailable ? "true" : "false");
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

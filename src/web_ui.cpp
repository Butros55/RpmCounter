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

    String htmlEscape(const String &input)
    {
        String out;
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

    String htmlPage()
    {
        String page;
        page.reserve(16000);

        // Namen der Farben – aus Config, sonst Standard
        String color1Name = (cfg.greenLabel.length() > 0) ? cfg.greenLabel : String("Farbe 1");
        String color2Name = (cfg.yellowLabel.length() > 0) ? cfg.yellowLabel : String("Farbe 2");
        String color3Name = (cfg.redLabel.length() > 0) ? cfg.redLabel : String("Farbe 3");

        String greenHex = colorToHex(cfg.greenColor);
        String yellowHex = colorToHex(cfg.yellowColor);
        String redHex = colorToHex(cfg.redColor);

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
            ".section-title small{display:block;font-size:12px;font-weight:400;color:#aaa;}"
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
            ".rpm-label{display:inline-block;margin-bottom:4px;font-weight:500;}"
            ".rpm-label-title{font-size:12px;opacity:0.9;display:block;}"
            ".rpm-label-range{font-size:12px;opacity:0.8;display:block;}"
            ".led-preview-title{margin-top:10px;margin-bottom:4px;font-size:12px;color:#aaa;}"
            ".led-preview{display:flex;flex-wrap:nowrap;align-items:center;justify-content:center;padding:6px 4px;border-radius:6px;background:#151515;border:1px solid #222;}"
            ".led-dot{border-radius:50%;background:#333;flex:0 0 auto;}"
            "</style></head><body>");

        page += "<h1><span>ShiftLight Setup</span>"
                "<a href=\"/settings\" style='text-decoration:none;color:#0af;font-size:20px;'>⚙️</a>"
                "</h1>";

        page += F("<form id='mainForm' method='POST' action='/save'>");

        //
        // Allgemein
        //
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

        // Farben – Color-Picker als Kreise nebeneinander
        page += F("<div class='color-row'>");

        // Low RPM
        page += F("<div class='color-swatch'>");
        page += F("<div class='color-swatch-title'>Low RPM</div>");
        page += "<input type='color' class='color-circle' id='greenColorInput' name='greenColor' value='";
        page += greenHex;
        page += "'>";
        page += "<div class='color-name' id='color1Name'>";
        page += color1Name;
        page += "</div>";
        page += "<input type='hidden' name='greenLabel' id='greenLabelHidden' value='";
        page += color1Name;
        page += "'>";
        page += F("</div>");

        // Mid RPM
        page += F("<div class='color-swatch'>");
        page += F("<div class='color-swatch-title'>Mid RPM</div>");
        page += "<input type='color' class='color-circle' id='yellowColorInput' name='yellowColor' value='";
        page += yellowHex;
        page += "'>";
        page += "<div class='color-name' id='color2Name'>";
        page += color2Name;
        page += "</div>";
        page += "<input type='hidden' name='yellowLabel' id='yellowLabelHidden' value='";
        page += color2Name;
        page += "'>";
        page += F("</div>");

        // Shift / Warnung
        page += F("<div class='color-swatch'>");
        page += F("<div class='color-swatch-title'>Shift / Warnung</div>");
        page += "<input type='color' class='color-circle' id='redColorInput' name='redColor' value='";
        page += redHex;
        page += "'>";
        page += "<div class='color-name' id='color3Name'>";
        page += color3Name;
        page += "</div>";
        page += "<input type='hidden' name='redLabel' id='redLabelHidden' value='";
        page += color3Name;
        page += "'>";
        page += F("</div>");

        page += F("</div>"); // color-row

        page += F("</div>"); // section Allgemein

        //
        // Drehzahl-Bereich
        //
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Drehzahl-Bereich</div>");

        // Auto-Scale Toggle
        page += F("<div class='toggle-row'><span class='toggle-label'>"
                  "Auto-Scale Max RPM (benutze max gesehene Drehzahl)"
                  "</span><label class='switch'>");
        page += "<input type='checkbox' name='autoscale' id='autoscaleToggle' ";
        if (cfg.autoScaleMaxRpm)
            page += "checked";
        page += "><span class='slider'></span></label></div>";

        // Fixed Max RPM Block
        page += F("<div class='range-group' id='fixedMaxContainer'>");
        page += F("<label for='fixedMaxRpmInput'>Fixed Max RPM</label>");
        page += "<input type='number' id='fixedMaxRpmInput' name='fixedMaxRpm' min='1000' max='8000' value='";
        page += String(cfg.fixedMaxRpm);
        page += "'>";
        page += F("</div>");

        // Low RPM Bereich
        page += F("<div class='range-group'>");
        page += "<label id='greenEndLabel' class='rpm-label' data-slot='1'>";
        page += "<span class='rpm-label-title'>Low RPM</span>";
        page += "<span class='rpm-label-range'>End (% von Max RPM)</span>";
        page += "</label>";
        page += "<input type='range' name='greenEndPct' min='0' max='100' value='";
        page += String(cfg.greenEndPct);
        page += "' id='greenEndSlider' data-display='greenEndVal'>";
        page += "<div class='small'>Wert: <span id='greenEndVal'>";
        page += String(cfg.greenEndPct);
        page += "%</span></div>";
        page += F("</div>");

        // Mid RPM Bereich
        page += F("<div class='range-group'>");
        page += "<label id='yellowEndLabel' class='rpm-label' data-slot='2'>";
        page += "<span class='rpm-label-title'>Mid RPM</span>";
        page += "<span class='rpm-label-range'>End (% von Max RPM)</span>";
        page += "</label>";
        page += "<input type='range' name='yellowEndPct' min='0' max='100' value='";
        page += String(cfg.yellowEndPct);
        page += "' id='yellowEndSlider' data-display='yellowEndVal'>";
        page += "<div class='small'>Wert: <span id='yellowEndVal'>";
        page += String(cfg.yellowEndPct);
        page += "%</span></div>";
        page += F("</div>");

        // Shift / Warnung Bereich
        page += F("<div class='range-group'>");
        page += "<label id='blinkStartLabel' class='rpm-label' data-slot='3'>";
        page += "<span class='rpm-label-title'>Shift / Warnung</span>";
        page += "<span class='rpm-label-range'>Start (% von Max RPM)</span>";
        page += "</label>";
        page += "<input type='range' name='blinkStartPct' min='0' max='100' value='";
        page += String(cfg.blinkStartPct);
        page += "' id='blinkStartSlider' data-display='blinkStartVal'>";
        page += "<div class='small'>Wert: <span id='blinkStartVal'>";
        page += String(cfg.blinkStartPct);
        page += "%</span></div>";
        page += F("</div>");

        // LED-Preview auf Basis von NUM_LEDS
        page += F("<div class='led-preview-title small'>LED-Vorschau</div>");
        page += "<div id='ledPreview' class='led-preview' data-led-count='";
        page += String(NUM_LEDS);
        page += "'></div>";

        page += F("</div>"); // section Drehzahl-Bereich

        //
        // Coming-Home / Leaving
        //
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

        page += F("</div>"); // section Coming-Home / Leaving

        //
        // Mein Fahrzeug – Live-Info (wird von /status aktualisiert)
        //
        page += F("<div class='section'>");
        page += F("<div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>Fahrzeug: <strong id='vehicleModel'>-</strong></div>";
        page += "<div class='row small'>VIN: <strong id='vehicleVin'>-</strong></div>";
        page += "<div class='row small'>Diagnose: <strong id='vehicleDiag'>-</strong></div>";
        page += F("</div>");

        //
        // Dev-Mode: OBD, Display, Debug
        //
        if (g_devMode)
        {
            // OBD / Verbindung
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

            page += F("</div>"); // section OBD / Verbindung

            // Display – BMW Logo Button in eigenem Block
            page += F("<div class='section'>");
            page += F("<div class='section-title'>Display</div>");
            page += F("<button type='button' id='btnDisplayLogo'>BMW Logo auf Display anzeigen</button>");
            page += F("<div class='small'>Zeigt kurz das BMW-Logo auf dem Display (nur im Entwicklermodus).</div>");
            page += F("</div>");

            // Debug
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

        // Haupt-Buttons unten
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

        //
        // JavaScript – Teil 1
        //
        page += F(
            "<script>"
            "let saveDirty=false;"
            "let lastStatus=null;"
            "let pendingSpinner=0;"
            "let lastSpinnerTs=0;"
            "let dotCounter=0;"
            "let vehicleDotsTimer=null;"
            "let vehicleDotsActive=false;"
            "let vehicleDotsStep=0;"

            "function animateDots(text){"
            " dotCounter=(dotCounter+1)%4;"
            " return text+'.'.repeat(dotCounter);"
            "}"

            "function updateSpinnerVisibility(forceHide){"
            " const sp=document.getElementById('debugSpinner');"
            " if(!sp) return;"
            " const idle=(Date.now()-lastSpinnerTs)>3000;"
            " if(forceHide||pendingSpinner<=0||idle||(lastStatus&&lastStatus.connected===false)){"
            "  sp.classList.add('hidden');"
            " }else{sp.classList.remove('hidden');}"
            "}"

            "function beginRequest(opts){"
            " opts=opts||{};"
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
            "  if(parseInt(el.value,10)!==val){el.value=val;updateSliderDisplay(el);}"
            " }"
            " if(changedId==='greenEndSlider'){"
            "  if(yv<gv){yv=gv;sync(y,yv);}if(bv<yv){bv=yv;sync(b,bv);}"
            " }else if(changedId==='yellowEndSlider'){"
            "  if(yv<gv){gv=yv;sync(g,gv);}if(bv<yv){bv=yv;sync(b,bv);}"
            " }else if(changedId==='blinkStartSlider'){"
            "  if(bv<yv){yv=bv;sync(y,yv);}if(yv<gv){gv=yv;sync(g,gv);}"
            " }"
            "}"

            "function handleSliderChange(e){"
            " enforceSliderOrder(e.target.id);"
            " updateSliderDisplay(e.target);"
            " markDirty();"
            "}"

            "function hexToRgb(hex){"
            " if(!hex||hex[0]!=='#')return null;"
            " if(hex.length===4){"
            "  let r=hex[1],g=hex[2],b=hex[3];"
            "  hex='#'+r+r+g+g+b+b;"
            " }"
            " if(hex.length!==7)return null;"
            " const r=parseInt(hex.substr(1,2),16);"
            " const g=parseInt(hex.substr(3,2),16);"
            " const b=parseInt(hex.substr(5,2),16);"
            " if(isNaN(r)||isNaN(g)||isNaN(b))return null;"
            " return{r,g,b};"
            "}"

            "function rgbToHsv(r,g,b){"
            " r/=255;g/=255;b/=255;"
            " const max=Math.max(r,g,b),min=Math.min(r,g,b);"
            " const d=max-min;"
            " let h=0;"
            " if(d!==0){"
            "  if(max===r){h=((g-b)/d)%6;}"
            "  else if(max===g){h=(b-r)/d+2;}"
            "  else{h=(r-g)/d+4;}"
            "  h*=60;if(h<0)h+=360;"
            " }"
            " const s=max===0?0:d/max;"
            " const v=max;"
            " return{h,s,v};"
            "}"

            "function classifyColor(slot,hex){"
            " const rgb=hexToRgb(hex);"
            " if(!rgb)return 'Farbe '+slot;"
            " const hsv=rgbToHsv(rgb.r,rgb.g,rgb.b);"
            " let base='';"
            " if(hsv.s<0.15){"
            "  if(hsv.v>0.8)base='Hell';"
            "  else if(hsv.v>0.4)base='Mittel';"
            "  else base='Dunkel';"
            " }else{"
            "  const h=hsv.h;"
            "  if(h<20||h>=340)base='Rot';"
            "  else if(h<60)base='Orange';"
            "  else if(h<90)base='Gelb';"
            "  else if(h<150)base='Grün';"
            "  else if(h<210)base='Türkis';"
            "  else if(h<260)base='Blau';"
            "  else if(h<300)base='Lila';"
            "  else base='Pink';"
            " }"
            " return 'Farbe '+slot+' – '+base;"
            "}"

            "function updateColorUi(){"
            " const cfg=["
            "  {key:'green',slot:1,labelId:'greenEndLabel',suffix:' End (% von Max RPM)',hiddenId:'greenLabelHidden',nameSpanId:'color1Name'},"
            "  {key:'yellow',slot:2,labelId:'yellowEndLabel',suffix:' End (% von Max RPM)',hiddenId:'yellowLabelHidden',nameSpanId:'color2Name'},"
            "  {key:'red',slot:3,labelId:'blinkStartLabel',suffix:' Start (% von Max RPM)',hiddenId:'redLabelHidden',nameSpanId:'color3Name'}"
            " ];"
            " cfg.forEach(c=>{"
            "  const colorInput=document.getElementById(c.key+'ColorInput');"
            "  if(!colorInput)return;"
            "  const name=classifyColor(c.slot,colorInput.value);"
            "  const lbl=document.getElementById(c.labelId);"
            "  if(lbl)lbl.innerText=name+c.suffix;"
            "  const span=document.getElementById(c.nameSpanId);"
            "  if(span)span.innerText=name;"
            "  const hidden=document.getElementById(c.hiddenId);"
            "  if(hidden)hidden.value=name;"
            " });"
            "}"

            "function updateAutoscaleUi(){"
            " var auto=document.getElementById('autoscaleToggle');"
            " var fixedContainer=document.getElementById('fixedMaxContainer');"
            " var fixedInput=document.getElementById('fixedMaxRpmInput');"
            " if(!auto||!fixedInput||!fixedContainer)return;"
            " var enabled=!auto.checked;"
            " fixedInput.disabled=!enabled;"
            " if(!enabled){"
            "  fixedContainer.classList.add('disabled-field');"
            " }else{"
            "  fixedContainer.classList.remove('disabled-field');"
            " }"
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
            "    if((e=document.getElementById('vehicleModel'))){"
            "      e.dataset.base=s.vehicleModel;"
            "      if(!s.vehicleInfoRequestRunning) e.innerText=s.vehicleModel;"
            "    }"
            "    if((e=document.getElementById('vehicleVin'))){"
            "      e.dataset.base=s.vehicleVin;"
            "      if(!s.vehicleInfoRequestRunning) e.innerText=s.vehicleVin;"
            "    }"
            "    if((e=document.getElementById('vehicleDiag'))){"
            "      e.dataset.base=s.vehicleDiag;"
            "      if(!s.vehicleInfoRequestRunning) e.innerText=s.vehicleDiag;"
            "    }"
            "    // Punkt-Animation für Fahrzeugdaten (VIN/Modell/Diagnose)"
            "    if(s.vehicleInfoRequestRunning){"
            "      if(!vehicleDotsActive){"
            "        vehicleDotsActive=true;"
            "        vehicleDotsStep=0;"
            "        if(vehicleDotsTimer){clearInterval(vehicleDotsTimer);vehicleDotsTimer=null;}"
            "        vehicleDotsTimer=setInterval(function(){"
            "          vehicleDotsStep=(vehicleDotsStep+1)%4;"
            "          ['vehicleModel','vehicleVin','vehicleDiag'].forEach(function(id){"
            "            var el=document.getElementById(id);"
            "            if(el && el.dataset && el.dataset.base!==undefined){"
            "              el.innerText=el.dataset.base + '.'.repeat(vehicleDotsStep);"
            "            }"
            "          });"
            "        },400);"
            "      }"
            "    }else{"
            "      if(vehicleDotsActive){"
            "        vehicleDotsActive=false;"
            "        if(vehicleDotsTimer){clearInterval(vehicleDotsTimer);vehicleDotsTimer=null;}"
            "        ['vehicleModel','vehicleVin','vehicleDiag'].forEach(function(id){"
            "          var el=document.getElementById(id);"
            "          if(el && el.dataset && el.dataset.base!==undefined){"
            "            el.innerText=el.dataset.base;"
            "          }"
            "        });"
            "      }"
            "    }"
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
            " var autoToggle=document.getElementById('autoscaleToggle');"
            " if(autoToggle){"
            "  autoToggle.addEventListener('change',()=>{markDirty();updateAutoscaleUi();});"
            " }"
            " updateAutoscaleUi();"
            " ['green','yellow','red'].forEach(name=>{"
            "  var color=document.getElementById(name+'ColorInput');"
            "  if(color){"
            "    color.addEventListener('input',()=>{updateColorUi();markDirty();});"
            "    color.addEventListener('change',()=>{updateColorUi();markDirty();});"
            "  }"
            " });"
            " ['greenEndSlider','yellowEndSlider','blinkStartSlider'].forEach(id=>{"
            "  var el=document.getElementById(id);"
            "  if(el){updateSliderDisplay(el);}"
            " });"
            " updateColorUi();"
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
            " setInterval(fetchStatus,1500);"
            " setInterval(()=>updateSpinnerVisibility(false),1000);"
            "}"

            "document.addEventListener('DOMContentLoaded',initUI);"
            "</script>");

        //
        // JavaScript – Teil 2: LED-Preview & Farblabels
        //
        page += F(
            "<script>"
            "var blinkPreviewEndTs=0;"
            "var blinkTimerId=null;"
            "var blinkPhase=false;"

            "function triggerBlinkPreview(){"
            "  blinkPreviewEndTs=Date.now()+2500;"
            "  if(blinkTimerId===null){"
            "    blinkTimerId=setInterval(function(){"
            "      blinkPhase=!blinkPhase;"
            "      if(Date.now()>=blinkPreviewEndTs){"
            "        clearInterval(blinkTimerId);"
            "        blinkTimerId=null;"
            "        blinkPhase=false;"
            "      }"
            "      updateLedPreview();"
            "    },200);"
            "  }"
            "}"

            "function updateLedPreview(){"
            "  var cont=document.getElementById('ledPreview');"
            "  if(!cont) return;"
            "  var count=parseInt(cont.dataset.ledCount||'0',10);"
            "  if(!count) return;"

            "  var gSlider=document.getElementById('greenEndSlider');"
            "  var ySlider=document.getElementById('yellowEndSlider');"
            "  var bSlider=document.getElementById('blinkStartSlider');"
            "  if(!gSlider||!ySlider||!bSlider) return;"

            "  var gv=parseInt(gSlider.value||'0',10);"
            "  var yv=parseInt(ySlider.value||'0',10);"
            "  var bv=parseInt(bSlider.value||'0',10);"

            "  var gColor=document.getElementById('greenColorInput');"
            "  var yColor=document.getElementById('yellowColorInput');"
            "  var rColor=document.getElementById('redColorInput');"
            "  if(!gColor||!yColor||!rColor) return;"

            "  var now=Date.now();"
            "  var blinkActive=(blinkTimerId!==null && now<blinkPreviewEndTs);"

            "  var dots=cont.querySelectorAll('.led-dot');"
            "  for(var i=0;i<dots.length;i++){"
            "    var frac=((i+0.5)/count)*100;"
            "    var col;"

            "    if(frac<=gv){"
            "      col=gColor.value;"
            "    }else if(frac<=yv){"
            "      col=yColor.value;"
            "    }else{"
            "      if(blinkActive && frac>=bv){"
            "        col=blinkPhase ? '#222222' : rColor.value;"
            "      }else{"
            "        col=rColor.value;"
            "      }"
            "    }"

            "    dots[i].style.backgroundColor=col;"
            "  }"
            "}"

            "function layoutLedDots(){"
            "  var cont=document.getElementById('ledPreview');"
            "  if(!cont) return;"
            "  var dots=cont.querySelectorAll('.led-dot');"
            "  var count=dots.length;"
            "  if(!count) return;"

            "  var width=cont.clientWidth;"
            "  if(width<=0) return;"

            "  var spacing=2;"
            "  var maxSize=Math.floor((width-spacing*(count-1))/count);"
            "  var size=Math.max(4,Math.min(14,maxSize));"

            "  for(var i=0;i<dots.length;i++){"
            "    dots[i].style.width=size+'px';"
            "    dots[i].style.height=size+'px';"
            "    dots[i].style.marginLeft=(spacing/2)+'px';"
            "    dots[i].style.marginRight=(spacing/2)+'px';"
            "  }"
            "}"

            "function initLedPreview(){"
            "  var cont=document.getElementById('ledPreview');"
            "  if(!cont) return;"
            "  var count=parseInt(cont.dataset.ledCount||'0',10);"
            "  cont.innerHTML='';"
            "  for(var i=0;i<count;i++){"
            "    var d=document.createElement('div');"
            "    d.className='led-dot';"
            "    cont.appendChild(d);"
            "  }"
            "  layoutLedDots();"
            "  updateLedPreview();"
            "  window.addEventListener('resize',layoutLedDots);"
            "}"

            "function updateColorUi(){"
            "  var cfg=["
            "    {key:'green',slot:1,labelId:'greenEndLabel',hiddenId:'greenLabelHidden',nameSpanId:'color1Name'},"
            "    {key:'yellow',slot:2,labelId:'yellowEndLabel',hiddenId:'yellowLabelHidden',nameSpanId:'color2Name'},"
            "    {key:'red',slot:3,labelId:'blinkStartLabel',hiddenId:'redLabelHidden',nameSpanId:'color3Name'}"
            "  ];"
            "  cfg.forEach(function(c){"
            "    var colorInput=document.getElementById(c.key+'ColorInput');"
            "    if(!colorInput) return;"
            "    var rawName=classifyColor(c.slot,colorInput.value);"
            "    var name=rawName;"
            "    var idx=rawName.indexOf('–');"
            "    if(idx>=0){ name=rawName.substring(idx+1).trim(); }"
            "    var span=document.getElementById(c.nameSpanId);"
            "    if(span) span.innerText=name;"
            "    var hidden=document.getElementById(c.hiddenId);"
            "    if(hidden) hidden.value=name;"
            "    var lbl=document.getElementById(c.labelId);"
            "    if(lbl) lbl.style.color=colorInput.value;"
            "  });"
            "  updateLedPreview();"
            "}"

            "document.addEventListener('DOMContentLoaded',function(){"
            "  initLedPreview();"
            "  var ids=['greenEndSlider','yellowEndSlider','blinkStartSlider'];"
            "  ids.forEach(function(id){"
            "    var el=document.getElementById(id);"
            "    if(!el) return;"
            "    el.addEventListener('input',function(){"
            "      updateLedPreview();"
            "      if(id==='blinkStartSlider'){triggerBlinkPreview();}"
            "    });"
            "    el.addEventListener('change',function(){"
            "      updateLedPreview();"
            "      if(id==='blinkStartSlider'){triggerBlinkPreview();}"
            "    });"
            "  });"
            "  updateColorUi();"
            "});"
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
        page += F(
            "<style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#111;color:#eee;padding:16px;margin:0;}"
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
            "button{margin-top:10px;width:100%;padding:10px;border:none;border-radius:6px;background:#0af;color:#000;font-weight:bold;font-size:14px;}"
            "button:disabled{background:#555;color:#888;}"
            ".status-message{font-size:12px;color:#aaa;margin-top:6px;}"
            ".status-message .highlight{color:#0af;}"
            ".error-text{font-size:12px;color:#ff6b6b;margin-top:6px;display:none;}"
            "</style></head><body>");

        page += "<h1><a href=\"/\">‹ Zurück</a><span>Einstellungen</span></h1>";

        page += F("<form id='settingsForm' method='POST' action='/settings'>");

        //
        // Modus / Entwicklermodus
        //
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

        //
        // Mein Fahrzeug
        //
        String safeVin = htmlEscape(readVehicleVin());
        String safeModel = htmlEscape(readVehicleModel());
        String safeDiag = htmlEscape(readVehicleDiagStatus());

        page += F("<div class='section'>");
        page += F("<div class='section-title'>Mein Fahrzeug</div>");
        page += "<div class='row small'>VIN: <strong id='vehicleVin'>" + safeVin + "</strong></div>";
        page += "<div class='row small'>Modell: <strong id='vehicleModel'>" + safeModel + "</strong></div>";
        page += "<div class='row small'>Diagnose: <strong id='vehicleDiag'>" + safeDiag + "</strong></div>";

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
        String safeStatus = htmlEscape(vehStatus);
        page += "<div class='row small'>Status: <span id='vehicleStatus' data-base='" + safeStatus + "'>" + safeStatus + "</span></div>";

        page += F("<button type='button' id='btnVehicleRefresh'>Fahrzeugdaten neu synchronisieren</button>");
        page += F("<div class='error-text' id='vehicleRefreshError'></div>");

        page += F("</div>"); // section Mein Fahrzeug

        page += F("<button type='submit' id='settingsSave' disabled>Speichern</button>");
        page += F("</form>");

        //
        // JS Settings
        //
        page += F(
            "<script>"
            "document.addEventListener('DOMContentLoaded',function(){"
            "  var form=document.getElementById('settingsForm');"
            "  var saveBtn=document.getElementById('settingsSave');"
            "  var dirty=false;"
            "  function markDirty(){"
            "    if(!saveBtn) return;"
            "    if(!dirty){"
            "      saveBtn.disabled=false;"
            "      dirty=true;"
            "    }"
            "  }"
            "  if(form && saveBtn){"
            "    form.querySelectorAll('input').forEach(function(el){"
            "      if(el.type==='button' || el.id==='settingsSave') return;"
            "      el.addEventListener('change',markDirty);"
            "      el.addEventListener('input',markDirty);"
            "    });"
            "    form.addEventListener('submit',function(){saveBtn.disabled=true;});"
            "  }"
            "  var statusEl=document.getElementById('vehicleStatus');"
            "  var errorEl=document.getElementById('vehicleRefreshError');"
            "  var refreshBtn=document.getElementById('btnVehicleRefresh');"
            "  var dotsTimer=null;"
            "  var currentStatusKey='';"
            "  var refreshActive=false;"
            "  var refreshStart=0;"
            "  function setStatus(text,loading){"
            "    if(!statusEl) return;"
            "    var key=text+'|'+(loading?'1':'0');"
            "    if(currentStatusKey===key) return;"
            "    currentStatusKey=key;"
            "    statusEl.dataset.base=text;"
            "    statusEl.innerText=text;"
            "    if(dotsTimer){clearInterval(dotsTimer);dotsTimer=null;}"
            "    if(loading){"
            "      var step=0;"
            "      dotsTimer=setInterval(function(){"
            "        step=(step+1)%4;"
            "        statusEl.innerText=statusEl.dataset.base + '.'.repeat(step);"
            "        ['vehicleVin','vehicleModel','vehicleDiag'].forEach(function(id){"
            "          var el=document.getElementById(id);"
            "          if(el && el.dataset && el.dataset.base!==undefined){"
            "            el.innerText=el.dataset.base + '.'.repeat(step);"
            "          }"
            "        });"
            "      },400);"
            "    }else{"
            "      ['vehicleVin','vehicleModel','vehicleDiag'].forEach(function(id){"
            "        var el=document.getElementById(id);"
            "        if(el && el.dataset && el.dataset.base!==undefined){"
            "          el.innerText=el.dataset.base;"
            "        }"
            "      });"
            "    }"
            "  }"
            "  function showError(msg){"
            "    if(!errorEl) return;"
            "    if(msg){"
            "      errorEl.innerText=msg;"
            "      errorEl.style.display='block';"
            "    }else{"
            "      errorEl.innerText='';"
            "      errorEl.style.display='none';"
            "    }"
            "  }"
            "  function setRefreshActive(active){"
            "    refreshActive=active;"
            "    if(refreshBtn) refreshBtn.disabled=active;"
            "    if(active){"
            "      refreshStart=Date.now();"
            "      showError('');"
            "    }"
            "  }"
            "  if(refreshBtn){"
            "    refreshBtn.addEventListener('click',function(){"
            "      if(refreshBtn.disabled) return;"
            "      setRefreshActive(true);"
            "      setStatus('Sync läuft',true);"
            "      fetch('/settings/vehicle-refresh',{method:'POST'})"
            "        .then(function(res){return res.json();})"
            "        .then(function(data){"
            "          if(!data||data.status!=='started'){"
            "            setRefreshActive(false);"
            "            if(data && data.reason==='no-connection'){"
            "              showError('Keine OBD-Verbindung vorhanden.');"
            "              setStatus('Sync nicht möglich',false);"
            "            }else{"
            "              showError('Sync konnte nicht gestartet werden.');"
            "            }"
            "          }"
            "        })"
            "        .catch(function(){"
            "          setRefreshActive(false);"
            "          showError('Sync fehlgeschlagen.');"
            "        });"
            "    });"
            "  }"
            "  function updateVehicleInfo(data){"
            "    var el;"
            "    if((el=document.getElementById('vehicleVin'))){"
            "      el.dataset.base=data.vehicleVin;"
            "      if(!data.vehicleInfoRequestRunning) el.innerText=data.vehicleVin;"
            "    }"
            "    if((el=document.getElementById('vehicleModel'))){"
            "      el.dataset.base=data.vehicleModel;"
            "      if(!data.vehicleInfoRequestRunning) el.innerText=data.vehicleModel;"
            "    }"
            "    if((el=document.getElementById('vehicleDiag'))){"
            "      el.dataset.base=data.vehicleDiag;"
            "      if(!data.vehicleInfoRequestRunning) el.innerText=data.vehicleDiag;"
            "    }"
            "    var loading=false;"
            "    var statusText='Noch keine Daten';"
            "    if(data.vehicleInfoRequestRunning){"
            "      statusText='Abruf läuft';"
            "      loading=true;"
            "    }else if(data.vehicleInfoReady){"
            "      if(data.vehicleInfoAge<=1){"
            "        statusText='Gerade aktualisiert (0s)';"
            "      }else{"
            "        statusText='Letztes Update vor '+data.vehicleInfoAge+'s';"
            "      }"
            "    }"
            "    setStatus(statusText,loading);"
            "    if(refreshActive && !data.vehicleInfoRequestRunning){"
            "      if(data.vehicleInfoReady && data.vehicleInfoAge<=2){"
            "        setRefreshActive(false);"
            "        setStatus('Gerade aktualisiert (0s)',false);"
            "        showError('');"
            "      }else if(Date.now()-refreshStart>7000){"
            "        setRefreshActive(false);"
            "        showError('Keine Antwort vom Fahrzeug.');"
            "      }"
            "    }"
            "  }"
            "  function pollStatus(){"
            "    fetch('/status')"
            "      .then(function(res){return res.json();})"
            "      .then(updateVehicleInfo)"
            "      .catch(function(){});"
            "  }"
            "  pollStatus();"
            "  setInterval(pollStatus,1500);"
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
            cfg.greenLabel = safeLabel(server.arg("greenLabel"), "Farbe 1");
        }
        if (server.hasArg("yellowLabel"))
        {
            cfg.yellowLabel = safeLabel(server.arg("yellowLabel"), "Farbe 2");
        }
        if (server.hasArg("redLabel"))
        {
            cfg.redLabel = safeLabel(server.arg("redLabel"), "Farbe 3");
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

        unsigned long now = millis();
        unsigned long vehicleAge = 0;
        if (g_vehicleInfoAvailable && g_vehicleInfoLastUpdate > 0 && now >= g_vehicleInfoLastUpdate)
        {
            vehicleAge = (now - g_vehicleInfoLastUpdate) / 1000;
        }

        String json;
        json.reserve(512);
        json = "{";
        json += "\"rpm\":" + String(g_currentRpm);
        json += ",\"maxRpm\":" + String(g_maxSeenRpm);
        json += ",\"speed\":" + String(g_vehicleSpeedKmh);
        json += ",\"gear\":" + String(g_estimatedGear);
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
        json += ",\"vehicleInfoRequestRunning\":" + String(g_vehicleInfoRequestRunning ? "true" : "false");
        json += ",\"vehicleInfoReady\":" + String(g_vehicleInfoAvailable ? "true" : "false");
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

        g_devMode = server.hasArg("devMode");
        if (!g_devMode)
        {
            g_autoReconnect = true;
        }

        server.sendHeader("Location", "/settings");
        server.send(303);
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

        if (!g_devMode)
        {
            server.send(403, "text/plain", "Forbidden");
            return;
        }

        displayShowTestLogo();
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
    server.on("/settings/vehicle-refresh", HTTP_POST, handleSettingsVehicleRefresh);
    server.on("/dev/display-logo", HTTP_POST, handleDevDisplayLogo);

    server.begin();
    Serial.println("Webserver gestartet (http://192.168.4.1/)");
}

void webUiLoop()
{
    server.handleClient();
}

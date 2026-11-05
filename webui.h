#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <panel.h>
#include <controls.h>
#include <power.h>
#include <effects.h>
#include <graphics.h>
#include <wifi_helpers.h>
#include <ota.h>

#include "secrets.h"

// The control page and its API.
//
// Reading state needs no credentials; anything that changes the light does.
// It is only a fish tank, but it is a listener on the LAN either side of a
// password, and the OTA endpoint next door deserves the same treatment.
//
// Handlers only ever set the values in controls.h and effects.h. The sketch
// reads those and decides what to do with them, so a request cannot leave the
// light in a state the normal path could not also reach.

// Owned by the sketch, which is where the schedule and the overrides are
// resolved. The page only reports them.
extern int hour;
extern int minute;
extern bool clockSynced;
extern bool testMode;
extern bool resyncRequested;

WebServer server(80);

// The panel state, as JSON. Everything the page needs in one request.
String statusJson() {
  String out;
  out.reserve(900);

  out += "{\"uptimeSec\":";
  out += millis() / 1000;
  out += ",\"heapFree\":";
  out += (uint32_t)ESP.getFreeHeap();
  out += ",\"rssi\":";
  out += WiFi.RSSI();
  out += ",\"ip\":\"";
  out += WiFi.localIP().toString();
  out += "\",\"dropouts\":";
  out += wifiDropoutCount();

  out += ",\"clockSynced\":";
  out += clockSynced ? "true" : "false";
  out += ",\"hour\":";
  out += hour;
  out += ",\"minute\":";
  out += minute;
  out += ",\"dayProgress\":";
  out += dayProgress(hour, minute);

  out += ",\"mode\":";
  out += graphics_current_mode();
  out += ",\"scheduledMode\":";
  out += timeMode(hour, minute);
  out += ",\"modeOverride\":";
  out += modeOverride;
  out += ",\"transitionPercent\":";
  out += graphics_transition_progress();
  out += ",\"transitionSec\":";
  out += graphics_transition_duration() / 1000;

  out += ",\"testMode\":";
  out += testMode ? "true" : "false";
  out += ",\"testSwitch\":";
  out += testSwitchOn() ? "true" : "false";
  out += ",\"testRequest\":";
  out += testModeRequest ? "true" : "false";
  out += ",\"brightness\":";
  out += masterBrightness;
  out += ",\"budgetMa\":";
  out += powerBudgetMa(testMode);

  out += ",\"pixelOverrides\":";
  out += panel_override_count();
  out += ",\"fps\":";
  out += panelFps;
  out += ",\"focusStop\":";
  out += effects_focus_stop();
  out += ",\"focusCommanded\":";
  out += effects_focus_commanded() ? "true" : "false";

  out += ",\"effects\":[";
  for (uint8_t i = 0; i < effects_count(); ++i) {
    if (i) out += ",";
    out += "{\"name\":\"";
    out += effects_name(i);
    out += "\",\"level\":";
    out += effects_strength(i);
    out += ",\"override\":";
    out += effects_override_of(i);
    out += ",\"enabled\":";
    out += effects_enabled(i) ? "true" : "false";
    out += ",\"cast\":";
    out += effect_in_cast(i) ? "true" : "false";

    out += ",\"params\":[";
    for (uint8_t n = 0; n < effects_param_count(i); ++n) {
      if (n) out += ",";
      out += "{\"name\":\"";
      out += effects_param_name(i, n);
      out += "\",\"value\":";
      out += effects_param(i, n);
      out += "}";
    }
    out += "]}";
  }
  out += "]}";
  return out;
}

// The live frame, as one hex string of 256 RGB triples. This is what the panel
// is actually showing, effects and power limiting included - the page draws it
// so you can see the light without standing over the tank.
String frameHex() {
  // Not named HEX: the core defines that as a macro for Print::print base 16.
  static const char* HEXDIGITS = "0123456789abcdef";
  String out;
  out.reserve(NUM_LEDS * 6 + 2);

  for (int row = 0; row < ROWS; ++row) {
    for (int col = 0; col < COLS; ++col) {
      CRGB c = leds[ledIndex(col, row)];
      uint8_t ch[3] = { c.r, c.g, c.b };
      for (uint8_t i = 0; i < 3; ++i) {
        out += HEXDIGITS[ch[i] >> 4];
        out += HEXDIGITS[ch[i] & 0x0F];
      }
    }
  }
  return out;
}

uint8_t hexByte(const String& s, int at) {
  auto nib = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  const char* p = s.c_str();
  return (uint8_t)((nib(p[at]) << 4) | nib(p[at + 1]));
}

// Guards everything that changes the light.
bool webAuthorized() {
  if (server.authenticate(WEB_USER, WEB_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

void webSendOk() {
  server.send(200, "application/json", "{\"ok\":true}");
}

const char CONTROL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aquarium light</title><style>
:root{color-scheme:dark;--bg:#11141a;--card:#1a1f28;--line:#2b323d;--dim:#8a94a6;--fg:#e8ecf3;--accent:#5fb0ff}
*{box-sizing:border-box}body{margin:0;padding:18px;background:var(--bg);color:var(--fg);
font:15px/1.5 ui-sans-serif,system-ui,sans-serif}h1{font-size:18px;margin:0 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:14px}
.card h2{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--dim);margin:0 0 10px}
canvas{width:100%;image-rendering:pixelated;border-radius:6px;background:#000;display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.stat{background:#141821;border-radius:6px;padding:8px 10px}
.stat b{display:block;font-size:11px;color:var(--dim);font-weight:500;text-transform:uppercase;letter-spacing:.05em}
.stat span{font-variant-numeric:tabular-nums}
button{background:#232a35;color:var(--fg);border:1px solid var(--line);border-radius:6px;
padding:8px 12px;font:inherit;cursor:pointer}button:hover{border-color:var(--accent)}
button.on{background:var(--accent);border-color:var(--accent);color:#08111d;font-weight:600}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
input[type=range]{flex:1;min-width:140px;accent-color:var(--accent)}
.fx{display:grid;grid-template-columns:70px 1fr 38px 58px 46px;gap:6px;align-items:center;margin-bottom:6px}
.bar{height:8px;background:#141821;border-radius:4px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--accent)}
.warn{color:#ffb454}
</style></head><body>
<h1>Aquarium light</h1>

<div class="card"><h2>Panel</h2><canvas id="panel" width="32" height="8"></canvas></div>

<div class="card"><h2>Mode</h2>
<div class="row" id="modes"></div>
<div style="margin-top:10px" class="row">
<label style="min-width:78px;color:var(--dim)">Brightness</label>
<input type="range" id="bright" min="0" max="255"><span id="brightv" style="width:34px;text-align:right"></span>
</div>
<div style="margin-top:10px" class="row">
<button id="test">Test mode</button><button id="resync">Re-sync clock</button>
<button id="clearpx">Clear pixels</button><button id="reboot">Reboot</button>
</div></div>

<div class="card"><h2>Focus</h2>
<div class="row" id="focus"></div>
<div style="margin-top:8px;color:var(--dim);font-size:13px" id="focusnote"></div></div>

<div class="card"><h2>Effects</h2><div id="fx"></div></div>

<div class="card"><h2>State</h2><div class="grid" id="stats"></div></div>

<script>
const MODES=['Auto','Morning','Mid-day','Afternoon','Evening','Night'];
let st={};
const ctx=document.getElementById('panel').getContext('2d');

function post(path){return fetch(path,{method:'POST'}).then(r=>{if(r.status===401)alert('Wrong credentials');return r})}

document.getElementById('modes').innerHTML=MODES.map((m,i)=>
  `<button data-mode="${i}">${m}</button>`).join('');
document.querySelectorAll('[data-mode]').forEach(b=>b.onclick=()=>
  post('/api/mode?set='+b.dataset.mode).then(refresh));

const STOPS=['Left','Middle','Right'];
document.getElementById('focus').innerHTML=STOPS.map((n,i)=>
  `<button data-stop="${i}">${n} for 1 min</button>`).join('');
document.querySelectorAll('[data-stop]').forEach(b=>b.onclick=()=>
  post('/api/focus?stop='+b.dataset.stop+'&hold=60').then(refresh));

const bright=document.getElementById('bright');
bright.oninput=()=>document.getElementById('brightv').textContent=bright.value;
bright.onchange=()=>post('/api/brightness?set='+bright.value);
document.getElementById('test').onclick=()=>post('/api/test?set='+(st.testRequest?0:1)).then(refresh);
document.getElementById('resync').onclick=()=>post('/api/resync').then(refresh);
document.getElementById('clearpx').onclick=()=>post('/api/pixels?clear=1').then(refresh);
document.getElementById('reboot').onclick=()=>{if(confirm('Reboot the light?'))post('/api/reboot')};

function drawFrame(hex){
  const img=ctx.createImageData(32,8);
  for(let i=0;i<32*8;i++){
    img.data[i*4]=parseInt(hex.substr(i*6,2),16);
    img.data[i*4+1]=parseInt(hex.substr(i*6+2,2),16);
    img.data[i*4+2]=parseInt(hex.substr(i*6+4,2),16);
    img.data[i*4+3]=255;
  }
  ctx.putImageData(img,0,0);
}

function pad(n){return String(n).padStart(2,'0')}

function render(){
  document.querySelectorAll('[data-mode]').forEach(b=>
    b.classList.toggle('on',+b.dataset.mode===st.modeOverride));
  document.getElementById('test').classList.toggle('on',st.testMode);
  document.querySelectorAll('[data-stop]').forEach(b=>
    b.classList.toggle('on',st.focusCommanded&&+b.dataset.stop===st.focusStop));
  document.getElementById('focusnote').textContent=st.focusCommanded
    ?'Held at '+STOPS[st.focusStop]+'; releases on its own.'
    :'Resting at '+STOPS[st.focusStop]+', moving on its own every 15-30 min.';
  if(document.activeElement!==bright){bright.value=st.brightness;
    document.getElementById('brightv').textContent=st.brightness}

  document.getElementById('fx').innerHTML=st.effects.map((e,i)=>
    `<div class="fx"><span style="color:var(--dim)">${e.name}</span>
     <span class="bar"><i style="width:${e.level*100/255}%"></i></span>
     <span style="text-align:right">${e.level}${e.cast?'*':''}</span>
     <button data-fx="${i}">${e.override<0?'auto':'pinned'}</button>
     <button data-en="${i}" class="${e.enabled?'on':''}">${e.enabled?'on':'off'}</button></div>`).join('');
  document.querySelectorAll('[data-en]').forEach(b=>b.onclick=()=>
    post('/api/effect?i='+b.dataset.en+'&enabled='+(st.effects[b.dataset.en].enabled?0:1)).then(refresh));
  document.querySelectorAll('[data-fx]').forEach(b=>b.onclick=()=>{
    const e=st.effects[b.dataset.fx];
    const v=e.override<0?prompt('Pin '+e.name+' to (0-255):',e.level):-1;
    if(v!==null)post('/api/effect?i='+b.dataset.fx+'&set='+v).then(refresh)});

  const t=st.clockSynced?pad(st.hour)+':'+pad(st.minute)
    :'<span class="warn">no clock</span>';
  const s=[['Time',t],['Mode',MODES[st.mode]||st.mode],
    ['Scheduled',MODES[st.scheduledMode]],
    ['Transition',st.transitionPercent<100?st.transitionPercent+'% of '+st.transitionSec+'s':'settled'],
    ['Pinned pixels',st.pixelOverrides],['Budget',st.budgetMa+' mA'],['Test switch',st.testSwitch?'closed':'open'],
    ['Uptime',Math.floor(st.uptimeSec/3600)+'h '+Math.floor(st.uptimeSec/60)%60+'m'],
    ['Frame rate',st.fps+' fps'],['Signal',st.rssi+' dBm'],['Free heap',(st.heapFree/1024).toFixed(0)+' KB'],
    ['Wi-Fi drops',st.dropouts],['Address',st.ip]];
  document.getElementById('stats').innerHTML=s.map(([k,v])=>
    `<div class="stat"><b>${k}</b><span>${v}</span></div>`).join('');
}

function refresh(){
  fetch('/api/status').then(r=>r.json()).then(j=>{st=j;render()}).catch(()=>{});
  fetch('/api/frame').then(r=>r.text()).then(drawFrame).catch(()=>{});
}
refresh();setInterval(refresh,1000);
</script></body></html>)HTML";

void webui_begin() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", CONTROL_PAGE);
  });

  server.on("/api/status", HTTP_GET, []() {
    server.send(200, "application/json", statusJson());
  });

  server.on("/api/frame", HTTP_GET, []() {
    server.send(200, "text/plain", frameHex());
  });

  server.on("/api/mode", HTTP_POST, []() {
    if (!webAuthorized()) return;
    setModeOverride(server.arg("set").toInt());
    webSendOk();
  });

  server.on("/api/brightness", HTTP_POST, []() {
    if (!webAuthorized()) return;
    long value = server.arg("set").toInt();
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    setMasterBrightness((uint8_t)value);
    webSendOk();
  });

  server.on("/api/test", HTTP_POST, []() {
    if (!webAuthorized()) return;
    setTestModeRequest(server.arg("set").toInt() != 0);
    webSendOk();
  });

  server.on("/api/effect", HTTP_POST, []() {
    if (!webAuthorized()) return;
    uint8_t i = (uint8_t)server.arg("i").toInt();
    if (server.hasArg("enabled")) {
      effects_set_enabled(i, server.arg("enabled").toInt() != 0);
    }
    if (server.hasArg("set")) {
      effects_override(i, (int16_t)server.arg("set").toInt());
    }
    webSendOk();
  });

  server.on("/api/focus", HTTP_POST, []() {
    if (!webAuthorized()) return;
    long stop = server.arg("stop").toInt();
    long hold = server.hasArg("hold") ? server.arg("hold").toInt() : 60;
    long level = server.hasArg("level") ? server.arg("level").toInt() : 200;
    if (hold < 1) hold = 1;
    if (hold > 3600) hold = 3600;
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    effects_focus_at((uint8_t)stop, (uint16_t)hold, (uint8_t)level);
    webSendOk();
  });

  // One pixel, held against the live frame until it is released. Everything
  // not set keeps animating underneath.
  server.on("/api/pixel", HTTP_POST, []() {
    if (!webAuthorized()) return;

    int col = server.arg("col").toInt();
    int row = server.arg("row").toInt();
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"out of range\"}");
      return;
    }

    if (server.hasArg("release")) {
      panel_release_pixel(col, row);
    } else {
      String rgb = server.arg("rgb");
      if (rgb.length() < 6) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"rgb=RRGGBB\"}");
        return;
      }
      panel_set_pixel(col, row, CRGB(hexByte(rgb, 0), hexByte(rgb, 2), hexByte(rgb, 4)));
    }
    webSendOk();
  });

  // The whole panel at once, as 256 RGB triples of hex - the same shape
  // /api/frame hands out, so a frame can be read back, edited and posted.
  server.on("/api/pixels", HTTP_POST, []() {
    if (!webAuthorized()) return;

    if (server.hasArg("clear")) {
      panel_clear_overrides();
      webSendOk();
      return;
    }

    String body = server.arg("plain");
    if (body.length() < (unsigned)(NUM_LEDS * 6)) {
      server.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"expected 1536 hex chars\"}");
      return;
    }
    for (int row = 0; row < ROWS; ++row) {
      for (int col = 0; col < COLS; ++col) {
        int at = (row * COLS + col) * 6;
        panel_set_pixel(col, row,
                        CRGB(hexByte(body, at), hexByte(body, at + 2), hexByte(body, at + 4)));
      }
    }
    webSendOk();
  });

  // One effect parameter. What each means is documented next to the table in
  // effects.h; they are plain 0-255 bytes here.
  server.on("/api/param", HTTP_POST, []() {
    if (!webAuthorized()) return;
    long fx = server.arg("fx").toInt();
    long n = server.arg("p").toInt();
    long v = server.arg("v").toInt();
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    if (!params_set((uint8_t)fx, (uint8_t)n, (uint8_t)v)) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"no such parameter\"}");
      return;
    }
    webSendOk();
  });

  server.on("/api/resync", HTTP_POST, []() {
    if (!webAuthorized()) return;
    resyncRequested = true;
    webSendOk();
  });

  server.on("/api/reboot", HTTP_POST, []() {
    if (!webAuthorized()) return;
    server.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "no such endpoint");
  });

  server.begin();

  Serial.print(F("Control page at http://"));
  Serial.println(WiFi.localIP().toString().c_str());
}

// Cheap. Called every frame, so a request waits at most one frame.
void webui_handle() {
  server.handleClient();
}

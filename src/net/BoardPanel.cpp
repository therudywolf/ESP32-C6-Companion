#include "net/BoardPanel.h"

#include <WebServer.h>
#include <WiFi.h>

#include "core/TextUtil.h"
#include "core/config.h"
#include "pet/Achievements.h"
#include "pet/PetKind.h"
#include "pet/WolfPet.h"
#include "ui/SceneIds.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"

/* Below this much free heap the panel stands aside: a request costs a few KB
 * and the TLS fetches, the LLM task and the Zigbee stack all need theirs. */
static const uint32_t kHeapFloor = 18 * 1024;

/* The page. One file, no framework, served from flash. It polls /api/state
 * every three seconds and posts each change as GET /api/set?key=value — the
 * same keys the PC panel's rc block uses, so the firmware side is one parser.
 *
 * Kept deliberately small: this is the panel for the PC-is-off case, read on
 * a phone across the room, and every kilobyte is served through a WiFi stack
 * that shares its radio with the coordinator. */
static const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang=ru><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Nocturne C6</title>
<style>
body{margin:0;background:#0d0a12;color:#eee;font:15px system-ui,sans-serif;padding:12px 12px 40px}
h1{font-size:18px;margin:0 0 4px}h2{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:#9a8fa8;margin:18px 0 6px}
.c{background:#181322;border:1px solid #2c2238;border-radius:10px;padding:10px;margin-bottom:8px}
.r{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
button,select{background:#241b30;color:#eee;border:1px solid #3a2c4a;border-radius:8px;padding:9px 12px;font-size:14px}
button.on{background:#ff3d5e;color:#120408;border-color:#ff3d5e;font-weight:700}
input[type=range]{width:100%}input[type=text]{flex:1;padding:9px;border-radius:8px;border:1px solid #3a2c4a;background:#241b30;color:#eee}
.t{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.t div{background:#241b30;border-radius:8px;padding:8px;text-align:center}
.t b{display:block;font-size:20px}.t span{font-size:11px;color:#9a8fa8}
.bar{height:6px;background:#2c2238;border-radius:3px;overflow:hidden}.bar i{display:block;height:100%;background:#00e0a0}
small{color:#9a8fa8}#st{font-size:12px;color:#00d0ff;min-height:16px}
</style></head><body>
<h1 id=ttl>Nocturne C6</h1><small id=sub>плата · без ПК</small>
<div id=st></div>
<h2>Сейчас</h2><div class=c>
<div class=t><div><b id=clk>--:--</b><span>часы</span></div><div><b id=room>—</b><span>комната</span></div><div><b id=temp>—</b><span>плата, C</span></div></div>
<div id=petbox style="margin-top:8px">
<div class=r><b id=pname>—</b> <small id=pstat></small></div>
<div class=r><small style=width:70px>сытость</small><div class=bar style=flex:1><i id=bh></i></div></div>
<div class=r><small style=width:70px>радость</small><div class=bar style=flex:1><i id=bj></i></div></div>
<div class=r><small style=width:70px>энергия</small><div class=bar style=flex:1><i id=be></i></div></div>
<div class=r><button onclick="set({action:'feed'})">Кормить</button><button onclick="set({action:'play'})">Играть</button><button onclick="set({action:'pet'})">Гладить</button><button onclick="set({action:'talk'})">Говорить</button></div>
<div class=r><input type=text id=say placeholder="сказать..."><button onclick="say()">Сказать</button></div>
</div></div>
<h2>Оформление</h2><div class=c>
<div class=r id=looks></div>
<div class=r><select id=theme onchange="set({theme:+this.value})"></select><select id=style onchange="set({style:+this.value})"></select></div>
<div class=r><select id=bgstyle onchange="set({bgstyle:+this.value})"><option value=0>фон: чистый</option><option value=1>фон: анимация</option><option value=2>фон: сетка</option></select>
<select id=dots onchange="set({dots:+this.value})"><option value=0>матрица: выкл</option><option value=1>матрица: точки</option><option value=2>матрица: редкие</option></select></div>
<div class=r>Светлый фон <span class=tg data-k=bglight></span></div>
</div>
<h2>Экран</h2><div class=c>
<div class=r>Яркость <b id=brv></b></div><input type=range id=br min=30 max=210 onchange="set({bright:+this.value})">
<div class=r><select id=carousel onchange="set({carousel:+this.value})"><option value=-1>карусель: выкл</option><option value=5>5 с</option><option value=10>10 с</option><option value=15>15 с</option><option value=30>30 с</option></select>
<select id=timeout onchange="set({timeout:+this.value})"><option value=0>гашение: выкл</option><option value=30>30 с</option><option value=60>60 с</option></select></div>
<div class=r>Ночной режим <span class=tg data-k=night></span> <select id=nightfrom onchange="set({nightfrom:+this.value})"></select><select id=nightto onchange="set({nightto:+this.value})"></select></div>
<div class=r>Переворот <span class=tg data-k=flip></span></div>
</div>
<h2>Питомец</h2><div class=c>
<div class=r>Питомец <span class=tg data-k=pet></span> Фурревость <span class=tg data-k=furry></span></div>
<div class=r id=kinds></div>
<div class=r>Болтливость <span class=seg data-k=wchat data-n="выкл,редко,норма,часто"></span></div>
<div class=r>Характер <span class=seg data-k=wtone data-n="обычный,добрый,ворчун,дерзкий"></span></div>
<div class=r>LLM <span class=tg data-k=petllm></span> <button onclick="set({game:1})">Игра: бег</button><button onclick="set({game:2})">Игра: реакция</button></div>
</div>
<h2>Сигналы</h2><div class=c>
<div class=r>Диод <span class=tg data-k=led></span> <select id=ledmode onchange="set({ledmode:+this.value})"><option value=0>настроение</option><option value=1>выкл</option><option value=2>радуга</option><option value=3>свеча</option></select></div>
<div class=r>Яркость диода <b id=lbv></b></div><input type=range id=ledbright min=10 max=100 step=10 onchange="set({ledbright:+this.value})">
<div class=r>Уведомления с ПК <span class=tg data-k=notif></span></div>
</div>
<h2>Система</h2><div class=c><div id=sys><small>—</small></div>
<div class=r style=margin-top:8px><select id=screen onchange="set({screen:+this.value})"></select></div></div>
<script>
const $=i=>document.getElementById(i);let mute=0,S=null;
function touch(){mute=Date.now()+4000}
async function set(o){touch();try{const q=Object.entries(o).map(([k,v])=>k+'='+encodeURIComponent(v)).join('&');
const r=await fetch('/api/set?'+q);$('st').textContent=r.ok?'✓ '+JSON.stringify(o):'✗ '+r.status}catch(e){$('st').textContent='✗ '+e}}
function say(){const t=$('say').value.trim();if(t){set({say:t});$('say').value=''}}
function tg(el,k,v){el.innerHTML='';[['вкл',1],['выкл',0]].forEach(([n,x])=>{const b=document.createElement('button');b.textContent=n;
if(v===x)b.className='on';b.onclick=()=>{const o={};o[k]=x;set(o);tg(el,k,x)};el.appendChild(b)})}
function seg(el,k,names,v){el.innerHTML='';names.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;
if(v===i)b.className='on';b.onclick=()=>{const o={};o[k]=i;set(o);seg(el,k,names,i)};el.appendChild(b)})}
function opts(sel,list,v){if(sel.options.length!==list.length){sel.innerHTML='';list.forEach((n,i)=>{const o=document.createElement('option');o.value=i;o.textContent=n;sel.appendChild(o)})}sel.value=v}
function hours(sel,v){if(!sel.options.length)for(let h=0;h<24;h++){const o=document.createElement('option');o.value=h;o.textContent=h+':00';sel.appendChild(o)}sel.value=v}
function draw(s){S=s;$('ttl').textContent=s.name+' · Nocturne C6';$('sub').textContent='v'+s.ver+' · '+s.ip+(s.pc?' · ПК на связи':' · ПК выключен, плата сама');
$('clk').textContent=s.clock||'--:--';$('room').textContent=s.room||'—';$('temp').textContent=s.temp;
$('pname').textContent=s.pet?s.name+' · '+s.stage+' · ур. '+s.lvl:'питомец выключен';$('pstat').textContent=s.pet?s.status:'';
$('bh').style.width=s.h+'%';$('bj').style.width=s.j+'%';$('be').style.width=s.e+'%';
if(Date.now()<mute)return;
const c=s.cfg;document.querySelectorAll('.tg').forEach(e=>tg(e,e.dataset.k,c[e.dataset.k]));
document.querySelectorAll('.seg').forEach(e=>seg(e,e.dataset.k,e.dataset.n.split(','),c[e.dataset.k]));
const lk=$('looks');if(!lk.children.length)s.looks.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;b.onclick=()=>set({look:i});lk.appendChild(b)});
const kd=$('kinds');kd.innerHTML='';s.kinds.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;if(c.kind===i)b.className='on';b.onclick=()=>set({kind:i});kd.appendChild(b)});
opts($('theme'),s.themes,c.theme<0?0:c.theme);opts($('style'),s.styles,c.style);opts($('screen'),s.scenes,s.scene);
$('bgstyle').value=c.bgstyle;$('dots').value=c.dots;$('br').value=c.bright;$('brv').textContent=Math.round(c.bright/2.1)+'%';
$('carousel').value=c.carousel;$('timeout').value=c.timeout;hours($('nightfrom'),c.nightfrom);hours($('nightto'),c.nightto);
$('ledmode').value=c.ledmode;$('ledbright').value=c.ledbright;$('lbv').textContent=c.ledbright+'%';
$('sys').innerHTML='<small>память '+s.heap+' КБ (мин '+s.heapmin+') · в работе '+s.up+' · wifi '+s.rssi+' dBm · SD '+(s.sd?'есть':'нет')+' · zigbee '+(s.zb?'да':'нет')+'</small>'}
async function poll(){try{const r=await fetch('/api/state');draw(await r.json())}catch(e){$('st').textContent='нет связи с платой'}setTimeout(poll,3000)}
poll();
</script></body></html>)HTML";

void BoardPanel::begin(AppState *st, WolfPet *pet, Achievements *ach) {
  st_ = st;
  pet_ = pet;
  ach_ = ach;
}

void BoardPanel::start() {
  if (running_) return;
  if (!srv_) {
    srv_ = new WebServer(80);
    srv_->on("/", HTTP_GET, [this] { handleRoot(); });
    srv_->on("/api/state", HTTP_GET, [this] { handleState(); });
    srv_->on("/api/set", HTTP_GET, [this] { handleSet(); });
    srv_->onNotFound([this] { handleNotFound(); });
  }
  srv_->begin();
  running_ = true;
  /* No mDNS. Measured on the board with the coordinator up: the free heap
   * after WiFi + Zigbee is ~28 KB, and the mDNS responder (its own task and
   * buffers) took that to 15 KB with a low-water mark of 1 KB - one TLS
   * fetch from an out-of-memory panic. The address is on ПЛАТА C6 and in
   * the boot log instead; a name is not worth the margin. */
  Serial.printf("[WEB] board panel at http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void BoardPanel::stop() {
  if (!running_) return;
  srv_->stop();
  running_ = false;
  Serial.println("[WEB] board panel stopped");
}

void BoardPanel::tick(bool wifiUp, bool enabled) {
  if (!enabled || !wifiUp) {
    if (running_) stop();
    return;
  }
  if (!running_) {
    start();
    return;
  }
  /* The least important thing on the board stands aside first. */
  if (ESP.getFreeHeap() < kHeapFloor) return;
  srv_->handleClient();
}

void BoardPanel::handleRoot() {
  srv_->sendHeader("Cache-Control", "max-age=600");
  srv_->send_P(200, "text/html; charset=utf-8", kPage);
}

void BoardPanel::handleNotFound() { srv_->send(404, "text/plain", "nope"); }

static void jsonStr(String &o, const char *s) {
  o += '"';
  for (const char *p = s; *p; p++) {
    if (*p == '"' || *p == '\\') o += '\\';
    if ((unsigned char)*p < 0x20) continue;
    o += *p;
  }
  o += '"';
}

String BoardPanel::stateJson() {
  const AppState &st = *st_;
  const Settings &s = st.settings;
  String o;
  o.reserve(1100);
  o += "{\"ver\":\"" NOCT_VERSION "\",\"ip\":\"";
  o += WiFi.localIP().toString();
  o += "\",\"name\":";
  jsonStr(o, petkind::nameTitle());
  o += ",\"pc\":";
  o += st.pcOffline ? "0" : "1";
  o += ",\"clock\":\"";
  o += st.pcClock;
  o += "\",\"temp\":";
  o += (int)st.boardTemp;
  o += ",\"room\":\"";
  if (st.zb.count > 0 && st.zb.list[0].temp10 != -32768) {
    o += st.zb.list[0].temp10 / 10;
    o += ',';
    o += abs(st.zb.list[0].temp10 % 10);
    o += " C";
    if (st.zb.list[0].humidity >= 0) {
      o += " / ";
      o += st.zb.list[0].humidity;
      o += '%';
    }
  }
  o += "\",\"pet\":";
  o += s.petEnabled ? "1" : "0";
  o += ",\"h\":";
  o += pet_->hunger();
  o += ",\"j\":";
  o += pet_->happy();
  o += ",\"e\":";
  o += pet_->energy();
  o += ",\"stage\":";
  jsonStr(o, pet_->stageName());
  o += ",\"status\":";
  jsonStr(o, pet_->statusText());
  o += ",\"lvl\":";
  o += ach_ ? ach_->petLevel() : 1;
  o += ",\"heap\":";
  o += st.heapFreeKb;
  o += ",\"heapmin\":";
  o += st.heapMinKb;
  o += ",\"up\":\"";
  o += (unsigned long)(st.uptimeSec / 3600UL);
  o += "ч ";
  o += (unsigned long)((st.uptimeSec % 3600UL) / 60UL);
  o += "м\",\"rssi\":";
  o += st.link.rssi;
  o += ",\"sd\":";
  o += st.link.sdOk ? "1" : "0";
  o += ",\"zb\":";
  o += st.link.zbUp ? "1" : "0";
  o += ",\"scene\":";
  o += st.uiScene;
  o += ",\"cfg\":{\"theme\":";
  o += s.customActive ? -1 : s.themePreset;
  o += ",\"style\":";
  o += s.uiStyle;
  o += ",\"bgstyle\":";
  o += s.bgStyle;
  o += ",\"dots\":";
  o += s.dotStyle;
  o += ",\"bglight\":";
  o += s.bgLight ? 1 : 0;
  o += ",\"bright\":";
  o += s.brightness;
  o += ",\"carousel\":";
  o += s.carouselEnabled ? s.carouselIntervalSec : -1;
  o += ",\"timeout\":";
  o += s.displayTimeoutSec;
  o += ",\"night\":";
  o += s.nightMode ? 1 : 0;
  o += ",\"nightfrom\":";
  o += s.nightFrom;
  o += ",\"nightto\":";
  o += s.nightTo;
  o += ",\"flip\":";
  o += s.flipped ? 1 : 0;
  o += ",\"pet\":";
  o += s.petEnabled ? 1 : 0;
  o += ",\"kind\":";
  o += s.petKind;
  o += ",\"furry\":";
  o += s.furry ? 1 : 0;
  o += ",\"wchat\":";
  o += s.wolfChatter;
  o += ",\"wtone\":";
  o += s.wolfTone;
  o += ",\"petllm\":";
  o += s.petLlm ? 1 : 0;
  o += ",\"led\":";
  o += s.ledEnabled ? 1 : 0;
  o += ",\"ledmode\":";
  o += s.ledMode;
  o += ",\"ledbright\":";
  o += s.ledBright;
  o += ",\"notif\":";
  o += s.notifShow ? 1 : 0;
  o += "},\"themes\":[";
  for (int i = 0; i < theme::presetTotal(); i++) {
    if (i) o += ',';
    jsonStr(o, theme::presetName(i));
  }
  o += "],\"styles\":[";
  for (int i = 0; i < theme::STYLE_COUNT; i++) {
    if (i) o += ',';
    jsonStr(o, theme::styleName(i));
  }
  o += "],\"looks\":[";
  for (int i = 0; i < theme::LOOKS; i++) {
    if (i) o += ',';
    jsonStr(o, theme::look(i).name);
  }
  o += "],\"kinds\":[";
  for (int i = 0; i < petkind::PK_COUNT; i++) {
    if (i) o += ',';
    jsonStr(o, petkind::speciesLabel(i));
  }
  o += "],\"scenes\":[";
  for (int i = 0; i < SCENE_COUNT; i++) {
    if (i) o += ',';
    jsonStr(o, scenes::title(i));
  }
  o += "]}";
  return o;
}

void BoardPanel::handleState() {
  srv_->send(200, "application/json; charset=utf-8", stateJson());
}

/* Every key lands in the same AppState::rc* field the PC's payload fills,
 * so the apply logic in main() stays the single owner of what a setting
 * means. rcClear() first: with the PC off nothing else resets these. */
void BoardPanel::handleSet() {
  AppState &st = *st_;
  st.rcClear();
  int n = 0;
  auto iv = [&](const char *k, int &dst) {
    if (!srv_->hasArg(k)) return;
    dst = srv_->arg(k).toInt();
    n++;
  };
  iv("screen", st.rcScreen);
  iv("theme", st.rcTheme);
  iv("bright", st.rcBright);
  iv("led", st.rcLed);
  iv("carousel", st.rcCarousel);
  iv("petllm", st.rcPetLlm);
  iv("flip", st.rcFlip);
  iv("timeout", st.rcTimeout);
  iv("bgstyle", st.rcBgStyle);
  iv("bglight", st.rcBgLight);
  iv("wchat", st.rcWolfChatter);
  iv("wtone", st.rcWolfTone);
  iv("notif", st.rcNotif);
  iv("ledmode", st.rcLedMode);
  iv("night", st.rcNight);
  iv("nightfrom", st.rcNightFrom);
  iv("nightto", st.rcNightTo);
  iv("pet", st.rcPet);
  iv("kind", st.rcPetKind);
  iv("furry", st.rcFurry);
  iv("style", st.rcStyle);
  iv("look", st.rcLook);
  iv("ledbright", st.rcLedBright);
  iv("web", st.rcWeb);
  iv("game", st.rcGame);
  iv("pin", st.rcPin);
  if (srv_->hasArg("dots")) {
    /* dots is applied on every tick from rcDots (not gated by seq), the
     * same way the PC panel drives it */
    st.rcDots = srv_->arg("dots").toInt();
    n++;
  }
  if (srv_->hasArg("action")) {
    String a = srv_->arg("action");
    if (a == "feed" || a == "play" || a == "pet" || a == "talk") {
      st.rcAction = a;
      n++;
    }
  }
  if (srv_->hasArg("say")) {
    st.rcSay = stripGlyphs(srv_->arg("say").substring(0, 160).c_str());
    n++;
  }
  if (n) st.rcNew = true;
  srv_->send(200, "application/json", n ? "{\"ok\":1}" : "{\"ok\":0}");
}

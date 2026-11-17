// Geraete-Webseite: http://timechain-terminal.local (bzw. IP)
// Status mit aktuellem Bild, Layout-Editor, Einstellungen, Datenquelle, Passwort,
// Firmware-Update, WLAN. Optional mit Passwort (HTTP-Digest-Anmeldung, Benutzer "admin").
#include "app.h"
#include <ESPmDNS.h>
#include <Update.h>
#include <MD5Builder.h>
#include <Preferences.h>

static WebServer server(80);
bool refreshRequested = false;
bool infoRequested = false;
bool nodeChangePending = false;
Node pendingNode;
static bool webStarted = false;

// Aenderungen, die das Bild betreffen, uebernimmt die Hauptschleife (nicht mitten im Zeichnen)
bool settingsPending = false;
bool fwInstallPending = false;
uint32_t fwInstallAt = 0;
String fwInstallError;
Lang pendingLang = LANG_DE;
Layout pendingLayout;

extern const char THEME_JS[];  // Farb-Editor, weiter unten
static String flashMsg;
static bool flashIsError = false;

static void flash(const String &m, bool err = false) {
  flashMsg = m;
  flashIsError = err;
}

// Anmeldung per HTTP-Digest (RFC 7616, MD5, qop=auth): Das Passwort geht nie im Klartext durchs Netz,
// Basic-Anmeldungen werden abgelehnt. Die Nonce liegt dauerhaft im NVS, damit der Browser nach
// einem Neustart des Geraets nicht erneut fragt.
static const char REALM[] = AUTH_REALM;
static String digestNonce, digestOpaque;

static String md5hex(const String &s) {
  MD5Builder m;
  m.begin();
  m.add(s);
  m.calculate();
  return m.toString();
}

static void loadDigestNonce() {
  if (digestNonce.length()) return;
  Preferences p;
  p.begin("btcauth", false);
  digestNonce = p.getString("nonce", "");
  digestOpaque = p.getString("opaque", "");
  if (digestNonce.length() != 32 || digestOpaque.length() != 32) {
    auto rnd = [] {
      char b[33];
      for (int i = 0; i < 4; ++i) snprintf(b + i * 8, 9, "%08lx", (unsigned long)esp_random());
      return String(b);
    };
    digestNonce = rnd(), digestOpaque = rnd();
    p.putString("nonce", digestNonce);
    p.putString("opaque", digestOpaque);
  }
  p.end();
}

// Wert eines Parameters im Authorization-Header (mit oder ohne Anfuehrungszeichen)
static String authParam(const String &h, const char *name) {
  String key = String(name) + "=";
  int i = 0;
  while ((i = h.indexOf(key, i)) >= 0) {
    if (i == 0 || h[i - 1] == ' ' || h[i - 1] == ',') break;
    i += key.length();
  }
  if (i < 0) return "";
  i += key.length();
  if (i < (int)h.length() && h[i] == '"') {
    int e = h.indexOf('"', i + 1);
    return e < 0 ? String("") : h.substring(i + 1, e);
  }
  int e = h.indexOf(',', i);
  String v = e < 0 ? h.substring(i) : h.substring(i, e);
  v.trim();
  return v;
}

static const char *methodName() {
  switch (server.method()) {
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_DELETE: return "DELETE";
    default: return "GET";
  }
}

static bool authorized() {
  if (!cfg.password.length()) return true;
  loadDigestNonce();
  const String h = server.header("Authorization");
  if (!h.startsWith("Digest ")) return false;
  if (authParam(h, "username") != "admin" || authParam(h, "realm") != REALM || authParam(h, "nonce") != digestNonce)
    return false;
  const String uri = authParam(h, "uri"), resp = authParam(h, "response"), qop = authParam(h, "qop");
  if (!uri.startsWith(server.uri())) return false;  // Antwort gehoert zu einer anderen Adresse
  const String &ha1 = cfg.password;  // gespeichert ist nur HA1, nie das Passwort
  const String ha2 = md5hex(String(methodName()) + ":" + uri);
  String expect;
  if (qop == "auth")
    expect = md5hex(ha1 + ":" + digestNonce + ":" + authParam(h, "nc") + ":" + authParam(h, "cnonce") + ":auth:" + ha2);
  else
    expect = md5hex(ha1 + ":" + digestNonce + ":" + ha2);
  return resp.length() == 32 && resp.equalsIgnoreCase(expect);
}

// Am Anfang jedes Handlers: false = Passwortabfrage wurde gesendet
static bool guard() {
  if (authorized()) return true;
  loadDigestNonce();
  server.sendHeader("WWW-Authenticate", String("Digest realm=\"") + REALM + "\", qop=\"auth\", algorithm=MD5, nonce=\"" +
                                            digestNonce + "\", opaque=\"" + digestOpaque + "\"");
  server.send(401, "text/plain; charset=utf-8", L("Passwort erforderlich", "Password required"));
  return false;
}

static String esc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String fmtUptime(uint32_t sec) {
  uint32_t d = sec / 86400, h = sec / 3600 % 24, m = sec / 60 % 60;
  if (d) return String(d) + L(" Tg. ", " d ") + String(h) + L(" Std.", " h");
  if (h) return String(h) + L(" Std. ", " h ") + String(m) + " min";
  return String(m) + " min";
}

// Offizielles Bitcoin-Logo (gemeinfrei, Wikimedia Commons "Bitcoin.svg")
static const char BTC_SVG[] PROGMEM = R"svg(<svg viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><g transform="translate(0.0063,-0.003)"><path fill="#f7931a" d="m63.033,39.744c-4.274,17.143-21.637,27.576-38.782,23.301-17.138-4.274-27.571-21.638-23.295-38.78,4.272-17.145,21.635-27.579,38.775-23.305,17.144,4.274,27.576,21.64,23.302,38.784z"/><path fill="#fff" d="m46.103,27.444c0.637-4.258-2.605-6.547-7.038-8.074l1.438-5.768-3.511-0.875-1.4,5.616c-0.923-0.23-1.871-0.447-2.813-0.662l1.41-5.653-3.509-0.875-1.439,5.766c-0.764-0.174-1.514-0.346-2.242-0.527l0.004-0.018-4.842-1.209-0.934,3.75s2.605,0.597,2.55,0.634c1.422,0.355,1.679,1.296,1.636,2.042l-1.638,6.571c0.098,0.025,0.225,0.061,0.365,0.117-0.117-0.029-0.242-0.061-0.371-0.092l-2.296,9.205c-0.174,0.432-0.615,1.08-1.609,0.834,0.035,0.051-2.552-0.637-2.552-0.637l-1.743,4.019,4.569,1.139c0.85,0.213,1.683,0.436,2.503,0.646l-1.453,5.834,3.507,0.875,1.439-5.772c0.958,0.26,1.888,0.5,2.798,0.726l-1.434,5.745,3.511,0.875,1.453-5.823c5.987,1.133,10.489,0.676,12.384-4.739,1.527-4.36-0.076-6.875-3.226-8.515,2.294-0.529,4.022-2.038,4.483-5.155zm-8.022,11.249c-1.085,4.36-8.426,2.003-10.806,1.412l1.928-7.729c2.38,0.594,10.012,1.77,8.878,6.317zm1.086-11.312c-0.99,3.966-7.1,1.951-9.082,1.457l1.748-7.01c1.982,0.494,8.365,1.416,7.334,5.553z"/></g></svg>)svg";

// WLAN-Wechsel: Formular mit Netzwerksuche
static const char WIFI_HTML[] PROGMEM = R"wf(<div id="wifi"><p class="hint" style="margin-top:0">%T_CONNECTED% %CUR%.</p>
<h3>%T_OTHER%</h3><form method="post" action="/wifi-connect" autocomplete="off">
<div class="row"><div><label for="wssid">%T_SSID%</label><input type="text" id="wssid" name="ssid" maxlength="32" required autocapitalize="off" spellcheck="false" list="wlist"><datalist id="wlist"></datalist></div>
<div><label for="wpass">%T_PASS%</label><input type="password" id="wpass" name="pass" maxlength="63" autocomplete="new-password"></div></div>
<label style="font-weight:400;display:flex;gap:6px;align-items:center;margin:6px 0"><input type="checkbox" style="width:auto;margin:0" onchange="document.getElementById('wpass').type=this.checked?'text':'password'">%T_SHOW%</label>
<p><button type="button" class="sec" id="wscan">%T_SCAN%</button> <span class="hint" id="wst"></span></p>
<div id="wnets" style="display:flex;flex-wrap:wrap;gap:6px;margin:6px 0"></div>
<p class="hint">%T_HINT%</p><button>%T_CONNECT%</button></form></div>
<script>(()=>{const b=document.getElementById('wscan'),st=document.getElementById('wst'),box=document.getElementById('wnets'),ss=document.getElementById('wssid');
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}
function poll(n){fetch('/api/wifi-scan').then(r=>r.json()).then(j=>{if(j.busy){if(n<20)setTimeout(()=>poll(n+1),700);else{st.textContent='';b.disabled=false}return}
b.disabled=false;st.textContent=j.n.length?'':'%T_NONE%';box.innerHTML='';j.n.forEach(w=>{const x=document.createElement('button');x.type='button';x.className='sec';
x.style.cssText='padding:4px 10px;font-size:13px';x.textContent=w.s+'  '+bars(w.r)+(w.o?'  (%T_OPEN%)':'  🔒');x.title=w.r+' dBm';
x.onclick=()=>{ss.value=w.s;const p=document.getElementById('wpass');p.value='';if(!w.o)p.focus()};box.append(x)})}).catch(()=>{b.disabled=false;st.textContent=''})}
b.onclick=()=>{b.disabled=true;st.textContent='%T_SCANNING%';poll(0)}})();</script>)wf";

// Warteseite nach "Verbinden": fragt den Zustand ab, bis das Geraet zurueck ist oder verschwunden
static const char WIFI_WAIT_HTML[] PROGMEM = R"ww(<section><h2>WLAN</h2><p id="wmsg">%T_TRY%</p><p class="hint" id="whint">%T_INFO%</p>
<p><a href="/" id="whome" hidden>%T_HOME%</a></p></section>
<script>(()=>{const m=document.getElementById('wmsg'),h=document.getElementById('whint'),home=document.getElementById('whome'),t0=Date.now();let seen=false;
function done(msg,hint,cls){m.textContent=msg;m.className=cls||'';h.innerHTML=hint||'';home.hidden=false}
function tick(){const c=new AbortController();setTimeout(()=>c.abort(),3000);
fetch('/api/wifi-state',{signal:c.signal,cache:'no-store'}).then(r=>r.json()).then(j=>{
if(j.st==2)return done(j.err,'%T_BACK%','bad');
if(j.st==0&&j.cur=='%SSIDJS%')return done('%T_OK%','');
setTimeout(tick,2000)}).catch(()=>{if(Date.now()-t0>60000)return done('','%T_GONE%');setTimeout(tick,2000)})}
setTimeout(tick,2500)})();</script>)ww";

// Pfeil nach oben; Vorschaubilder in voller Breite, Klick = Vergroesserung (mind. doppelt, sonst fensterfuellend)
static const char PAGE_FOOT_JS[] PROGMEM = R"js(<script>
(()=>{const t=document.getElementById('totop');const sh=()=>t.classList.toggle('show',scrollY>400);addEventListener('scroll',sh);sh();
t.onclick=e=>{e.preventDefault();scrollTo({top:0,behavior:'smooth'})};
// Vorschaubilder: bei ganzzahligem Massstab Pixel 1:1 vergroessert (pixelated); sonst erst ganzzahlig
// hochrechnen und dann weich verkleinern lassen - so bleiben die Kanten ohne ungleich breite Pixel
function crisp(i){if(!i.naturalWidth||!i.parentElement||i.src.startsWith('blob:'))return;
const c0=document.createElement('canvas');c0.width=i.naturalWidth;c0.height=i.naturalHeight;c0.getContext('2d').drawImage(i,0,0);i._o=c0;i._m=0;fit(i)}
function fit(i){const o=i._o;if(!o)return;const d=devicePixelRatio||1,pw=i.parentElement.clientWidth,nw=o.width,nh=o.height;
const w=nh>nw?Math.min(pw,Math.round(innerHeight*.85*nw/nh)):pw;i.style.width=w+'px';const k=w*d/nw;
const m=(Math.abs(k-Math.round(k))<.02&&k>=1)?1:Math.max(2,Math.ceil(k)),ir=m==1?'pixelated':'auto';
if(i._m===m){i.style.imageRendering=ir;return}i._m=m;
const c=document.createElement('canvas');c.width=nw*m;c.height=nh*m;const x=c.getContext('2d');x.imageSmoothingEnabled=false;x.drawImage(o,0,0,c.width,c.height);
c.toBlob(bl=>{if(!bl)return;const u=URL.createObjectURL(bl);if(i._u)URL.revokeObjectURL(i._u);i._u=u;i.style.imageRendering=ir;i.src=u},'image/png')}
window.crispAll=()=>document.querySelectorAll('img.shot').forEach(crisp);
document.addEventListener('load',e=>{const i=e.target;if(i.matches&&i.matches('img.shot')&&!i.src.startsWith('blob:'))crisp(i)},true);
let rt=0;addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(()=>document.querySelectorAll('img.shot').forEach(fit),150)});crispAll();
const z=document.getElementById('zoom'),zi=z.querySelector('img');
document.addEventListener('click',e=>{const i=e.target;if(!i.matches||!i.matches('img.shot')||!i._o)return;const o=i._o,d=devicePixelRatio||1;
const fitW=Math.min((innerWidth-24)/o.width,(innerHeight-24)/o.height)*o.width,target=Math.max(fitW,2*i.clientWidth);
const kd=Math.max(1,Math.round(target*d/o.width));
zi.onload=()=>z.scrollTo((z.scrollWidth-innerWidth)/2,(z.scrollHeight-innerHeight)/2);zi.src=o.toDataURL();zi.style.width=(o.width*kd/d)+'px';z.hidden=false});
z.onclick=()=>{z.hidden=true};
// Meldungen oben: verschwinden von selbst (Fehler spaeter), mit x schliessbar
const en=document.documentElement.lang=='en',tb=document.getElementById('toasts');
const T=en?{busy:'The display is being redrawn – this page responds again in about 30 seconds.',soon:'The display will be redrawn shortly …',done:'The display has been updated.'}
:{busy:'Das Display wird gerade neu gezeichnet – die Seite antwortet in etwa 30 Sekunden wieder.',soon:'Das Display wird gleich neu gezeichnet …',done:'Das Display ist aktualisiert.'};
function hide(e){if(!e||!e.parentNode)return;e.classList.add('out');setTimeout(()=>e.remove(),300)}
function arm(e,ms){if(!e.querySelector('.tx')){const x=document.createElement('button');x.type='button';x.className='tx';x.textContent='×';x.onclick=()=>hide(e);e.append(x)}
clearTimeout(e._t);if(ms)e._t=setTimeout(()=>hide(e),ms)}
function toast(msg,kind,ms,id){let e=id&&document.getElementById(id);if(!e){e=document.createElement('div');if(id)e.id=id;e.append(document.createElement('span'));tb.append(e)}
e.className='msg'+(kind?' '+kind:'');e.firstChild.textContent=msg;arm(e,ms);return e}
window.toast=toast;tb.querySelectorAll('.msg').forEach(e=>arm(e,e.classList.contains('err')?15000:7000));
// Status des Displays: neu gezeichnet wird gerade (keine Antwort), gleich (pending) oder ist fertig
// Eine einzelne verspaetete Antwort ist kein Bildwechsel (nach einem Bildwechsel arbeitet das Geraet
// erst den Rueckstau ab): "gerade neu gezeichnet" erst nach zwei Fehlschlaegen in Folge oder wenn
// der Bildwechsel angekuendigt war, und nicht in den ersten 20 s nach "aktualisiert"
let renders=null,busy=false,fails=0,quiet=0;
function poll(){const c=new AbortController(),t=setTimeout(()=>c.abort(),6000);
fetch('/api/status',{signal:c.signal,cache:'no-store'}).then(r=>r.json()).then(j=>{clearTimeout(t);fails=0;
if(renders!==null&&j.renders>renders){quiet=Date.now()+20000;toast(T.done,'',5000,'tdisp');document.querySelectorAll('img.shot[data-live]').forEach(i=>{i._o=null;i._m=0;i.src='/preview.bmp?t='+Date.now()})}
else if(j.pending)toast(T.soon,'busy',0,'tdisp');else if(busy)hide(document.getElementById('tdisp'));
busy=j.pending;renders=j.renders;
if(j.update&&!document.getElementById('fmsg')){let seen='';try{seen=sessionStorage.getItem('ttupd')}catch(e){}
if(seen!==j.latest){try{sessionStorage.setItem('ttupd',j.latest)}catch(e){}toast((en?'Update to version ':'Update auf Version ')+j.latest+(en?' available – install it under System.':' verfügbar – unter System installieren.'),'',12000,'tupd')}}}).catch(()=>{clearTimeout(t);fails++;
if(renders!==null&&Date.now()>quiet&&(busy||fails>=2)){toast(T.busy,'busy',0,'tdisp');busy=true}})
.finally(()=>setTimeout(poll,document.hidden?15000:3000))}
poll()})();
</script>)js";

static const char PAGE_HEAD[] PROGMEM = R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Timechain Terminal</title>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<style>
:root{--bg:#f6f6f4;--card:#fff;--fg:#1a1a1a;--mut:#666;--acc:#f7931a;--line:#ddd;--err:#c62828;--ok:#2e7d32}
@media(prefers-color-scheme:dark){:root{--bg:#121212;--card:#1e1e1e;--fg:#eee;--mut:#aaa;--line:#333}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,sans-serif}
main{max-width:900px;margin:0 auto;padding:16px}
.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;margin:4px 0 18px}
.brand{display:flex;align-items:center;gap:10px;font-size:21px;font-weight:700;letter-spacing:.2px}
.brand svg{width:36px;height:36px;flex:none}
.tabs{display:inline-flex;background:var(--card);border:1px solid var(--line);border-radius:999px;padding:4px;gap:4px}
.tabs a{padding:7px 18px;border-radius:999px;color:var(--fg);text-decoration:none;font-weight:600;font-size:15px;transition:background .15s}
.tabs a:hover{background:var(--bg)}.tabs a.on{background:var(--acc);color:#111}
h3{font-size:14px;margin:16px 0 6px;color:var(--mut);text-transform:uppercase;letter-spacing:.4px}h2+h3{margin-top:4px}
h2{font-size:16px;margin:0 0 10px}section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin:0 0 14px}
table{width:100%;border-collapse:collapse}td{padding:3px 0;vertical-align:top}td:first-child{color:var(--mut);width:40%}
input[type=text],input[type=password],select{width:100%;padding:9px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:14px}input[type=file]{width:100%;margin:4px 0 10px}
button{padding:9px 14px;border:0;border-radius:6px;background:var(--acc);color:#000;font:inherit;font-weight:600;cursor:pointer;margin-top:10px}
button.sec{background:transparent;border:1px solid var(--line);color:var(--fg)}button:disabled{opacity:.5;cursor:default}
.hint{color:var(--mut);font-size:14px;margin:6px 0 0}.hint a,section p a{color:var(--acc)}.msg{padding:10px 14px;border-radius:8px;margin:0 0 14px;color:#fff;background:var(--ok)}
.msg.err{background:var(--err)}.bad{color:var(--err)}.shot{width:100%;max-width:100%;height:auto;border:1px solid var(--line);border-radius:6px;display:block;margin:0 auto;cursor:zoom-in}
#toasts{position:fixed;top:12px;left:50%;transform:translateX(-50%);z-index:8;display:flex;flex-direction:column;gap:8px;width:min(560px,calc(100% - 24px));pointer-events:none}
#toasts .msg{margin:0;display:flex;gap:10px;align-items:center;box-shadow:0 4px 18px #0006;pointer-events:auto;transition:opacity .3s,transform .3s;animation:tin .25s}
#toasts .msg.out{opacity:0;transform:translateY(-10px)}.msg.busy{background:#3d4b55}
.msg.busy>span:first-child::before{content:"";display:inline-block;width:12px;height:12px;border:2px solid #fff6;border-top-color:#fff;border-radius:50%;margin-right:9px;vertical-align:-1px;animation:spin 1s linear infinite}
.tx{margin:0 0 0 auto;padding:0 4px;background:none;color:inherit;font-size:20px;line-height:1;font-weight:400}
@keyframes spin{to{transform:rotate(360deg)}}@keyframes tin{from{opacity:0;transform:translateY(-10px)}}
#totop{position:fixed;right:18px;bottom:18px;width:46px;height:46px;border-radius:50%;background:var(--acc);color:#111;display:flex;align-items:center;justify-content:center;font-size:24px;font-weight:700;text-decoration:none;box-shadow:0 2px 10px #0006;opacity:0;pointer-events:none;transition:opacity .2s;z-index:5}
#totop.show{opacity:1;pointer-events:auto}
#zoom{position:fixed;inset:0;background:#000e;display:flex;overflow:auto;padding:12px;box-sizing:border-box;z-index:9;cursor:zoom-out}
#zoom[hidden]{display:none}#zoom img{image-rendering:pixelated;max-width:none;flex:none;margin:auto}
.evbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}.evbar>*{flex:1 1 150px;width:auto;margin:0}
.evbar button{flex:0 0 auto}
.ev{display:grid;grid-template-columns:auto 96px 1fr auto;gap:10px;align-items:center;padding:7px 2px;border-top:1px solid var(--line);font-size:15px}
.ev.off .evt,.ev.off .evd{opacity:.45}.ev small{background:var(--line);border-radius:6px;padding:1px 6px;font-size:12px;margin-left:6px;white-space:nowrap}
.evd{color:var(--mut);font-variant-numeric:tabular-nums;font-size:14px}.eva{display:flex;gap:4px}
.ib{margin:0;padding:4px 9px;background:transparent;border:1px solid var(--line);color:var(--fg);font-weight:400}
#evform{border:2px solid var(--acc);border-radius:8px;padding:10px 12px;margin:10px 0}
@media(max-width:560px){.ev{grid-template-columns:auto 1fr auto}.evd{grid-column:2}.evt{grid-column:2}.eva{grid-row:1/3;grid-column:3}}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1 1 200px}code{font-size:14px}
</style></head><body><main>)";

static void sendPage(const String &body, int tab = -1) {
  String html = FPSTR(PAGE_HEAD);
  html.replace("<html>", lang == LANG_EN ? "<html lang=\"en\">" : "<html lang=\"de\">");
  html += "<header class=\"top\"><div class=\"brand\">";
  html += FPSTR(BTC_SVG);
  html += "<span>Timechain Terminal</span></div><nav class=\"tabs\"><a href=\"/\"";
  html += tab == 0 ? " class=\"on\"" : "";
  html += ">";
  html += L("Übersicht", "Overview");
  html += "</a><a href=\"/content\"";
  html += tab == 2 ? " class=\"on\"" : "";
  html += ">";
  html += L("Inhalte", "Content");
  html += "</a><a href=\"/layout\"";
  html += tab == 1 ? " class=\"on\"" : "";
  html += ">";
  html += L("Layout &amp; Farben", "Layout &amp; colors");
  html += "</a></nav></header>";
  // Meldungen oben eingeblendet; sie verschwinden nach einigen Sekunden (siehe PAGE_FOOT_JS)
  html += "<div id=\"toasts\">";
  if (flashMsg.length()) {
    html += String("<div class=\"msg") + (flashIsError ? " err" : "") + "\"><span>" + esc(flashMsg) + "</span></div>";
    flashMsg = "";
  }
  html += "</div>";
  html += body;
  html += "</main><a id=\"totop\" href=\"#\" title=\"";
  html += L("Nach oben", "Back to top");
  html += "\">&#8593;</a><div id=\"zoom\" hidden><img alt=\"\"></div>";
  html += FPSTR(PAGE_FOOT_JS);
  html += "</body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", html);
}

static void redirect(const char *to = "/") {
  server.sendHeader("Location", to);
  server.send(303);
}

static String opt(const String &value, const String &label, bool sel) {
  return "<option value=\"" + value + "\"" + (sel ? " selected" : "") + ">" + label + "</option>";
}

static const char MD5_JS[] PROGMEM = R"md(function md5(s){s=unescape(encodeURIComponent(s));const K=[],R=[7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21],w=[],n=s.length;
for(let i=0;i<64;i++)K[i]=Math.floor(Math.abs(Math.sin(i+1))*4294967296)|0;
for(let i=0;i<n;i++)w[i>>2]|=s.charCodeAt(i)<<(i%4*8);w[n>>2]|=128<<(n%4*8);const L=((n+8>>6)+1)*16;w[L-2]=n*8;
let a=0x67452301,b=0xefcdab89|0,c=0x98badcfe|0,d=0x10325476;
for(let i=0;i<L;i+=16){let A=a,B=b,C=c,D=d;for(let j=0;j<64;j++){let f,g;
if(j<16){f=B&C|~B&D;g=j}else if(j<32){f=D&B|~D&C;g=(5*j+1)%16}else if(j<48){f=B^C^D;g=(3*j+5)%16}else{f=C^(B|~D);g=7*j%16}
const t=D,x=(A+f+K[j]+(w[i+g]|0))|0,h=R[(j>>4)*4+j%4];D=C;C=B;B=(B+(x<<h|x>>>32-h))|0;A=t}
a=a+A|0;b=b+B|0;c=c+C|0;d=d+D|0}
return[a,b,c,d].map(v=>{let h='';for(let i=0;i<4;i++)h+=(v>>>i*8&255).toString(16).padStart(2,'0');return h}).join('')})md";

// Passwort-Formular: der Browser rechnet HA1 = md5("admin:realm:passwort") und schickt nur diesen Wert
static const char PW_JS[] PROGMEM = R"pw(<script>(()=>{
%MD5%
const f=document.getElementById('pwf'),a=document.getElementById('pw1'),b=document.getElementById('pw2'),h=document.getElementById('ha1');
f.onsubmit=e=>{if(a.value!==b.value){e.preventDefault();alert('%T_MISMATCH%');return}
if(a.value&&a.value.length<6){e.preventDefault();alert('%T_SHORT%');return}
h.value=a.value?md5('admin:%REALM%:'+a.value):'';a.value=b.value=''}})();</script>)pw";

static String pwScript() {
  String s = FPSTR(PW_JS);
  s.replace("%MD5%", FPSTR(MD5_JS));
  s.replace("%REALM%", AUTH_REALM);
  s.replace("%T_MISMATCH%", L("Die Passwörter stimmen nicht überein.", "The passwords do not match."));
  s.replace("%T_SHORT%", L("Bitte mindestens 6 Zeichen.", "Please use at least 6 characters."));
  return s;
}

static void handleRoot() {
  if (!guard()) return;
  String b;
  b += "<section><h2>";
  b += L("Aktuelles Bild", "Current screen");
  b += "</h2><img class=\"shot\" data-live src=\"/preview.bmp\" width=\"" + String(screenW()) + "\" height=\"" +
       String(screenH()) + "\" alt=\"\">"
       "<form method=\"post\" action=\"/refresh\"><button class=\"sec\">";
  b += L("Display jetzt neu zeichnen", "Redraw display now");
  b += "</button></form><p class=\"hint\">";
  b += L("Am Gerät: RESET-Taste drücken, etwa 1 Sekunde warten, nochmal drücken (innerhalb von 10 s) zeigt einen "
         "großen QR-Code zu dieser Seite. Schneller geht es nicht: Das Gerät braucht nach dem ersten Druck gut eine "
         "halbe Sekunde, bis es ihn bemerkt.",
         "On the device: press RESET, wait about 1 second, press again (within 10 s) to show a large QR code for this "
         "page. Faster does not work: after the first press the device needs about half a second to notice it.");
  b += "</p></section>";

  b += "<section><h2>Status</h2><table>";
  b += String("<tr><td>") + L("Letzter Block", "Latest block") + "</td><td>" +
       (data.nBlocks ? fmtInt(data.blocks[0].height) + " (" + fmtAge(data.blocks[0].ts) + ")" : String("–")) +
       "</td></tr>";
  b += String("<tr><td>") + L("Daten", "Data") + "</td><td>" +
       (data.online ? String("ok") : String("<span class=\"bad\">") + L("Fehler", "Error") + "</span>") +
       (data.updatedAt ? String(L(", Stand ", ", as of ")) + fmtTime(data.updatedAt, "%H:%M:%S") : String("")) +
       "</td></tr>";
  if (!data.online && lastHttpError.length())
    b += String("<tr><td>") + L("Letzter Fehler", "Last error") + "</td><td class=\"bad\">" + esc(lastHttpError) +
         "</td></tr>";
  b += String("<tr><td>") + L("Datenquelle", "Data source") + "</td><td>" + esc(node.url) + "</td></tr>";
  b += "<tr><td>WLAN</td><td>" + esc(WiFi.SSID()) + " (" + String(WiFi.RSSI()) + " dBm)</td></tr>";
  b += String("<tr><td>") + L("Adresse", "Address") + "</td><td>http://" WIFI_HOSTNAME ".local<br>http://" +
       WiFi.localIP().toString() + "</td></tr>";
  b += "<tr><td>Firmware</td><td>" FW_VERSION "</td></tr>";
  {
    esp_reset_reason_t r = esp_reset_reason();
    const char *why = r == ESP_RST_POWERON ? L("Einschalten oder RESET-Taste", "power on or RESET button")
                      : r == ESP_RST_SW    ? L("Software (Update, Einstellung)", "software (update, setting)")
                      : r == ESP_RST_BROWNOUT ? L("Spannungseinbruch", "brownout")
                      : (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT)
                          ? L("Absturz/Watchdog", "crash/watchdog")
                          : L("anderer Grund", "other");
    b += String("<tr><td>") + L("Letzter Neustart", "Last restart") + "</td><td>" + why + "</td></tr>";
  }
  b += String("<tr><td>") + L("Laufzeit", "Uptime") + "</td><td>" + fmtUptime(millis() / 1000) + "</td></tr>";
  b += String("<tr><td>") + L("Freier Speicher", "Free memory") + "</td><td>" + fmtInt(ESP.getFreeHeap() / 1024) +
       " KB</td></tr></table></section>";

  // Allgemein
  b += String("<section><h2>") + L("Allgemein", "General") +
       "</h2><form method=\"post\" action=\"/settings\"><label>Sprache / Language</label><select name=\"lang\">" +
       opt("de", "Deutsch", lang == LANG_DE) + opt("en", "English", lang == LANG_EN) + "</select><p class=\"hint\">" +
       L("Einstellungen zu einzelnen Inhalten (Währungen, Preis-Chart, Miner, Ereignisse) stehen unter „Inhalte“.",
         "Settings for individual widgets (currencies, price chart, miners, events) are under “Content”.") +
       "</p><button>" + L("Speichern", "Save") + "</button></form></section>";

  // Datenquelle
  b += String("<section><h2>") + L("Datenquelle", "Data source") + "</h2><form method=\"post\" action=\"/node\">" +
       "<label>" + L("mempool-Server", "mempool server") + "</label><input type=\"text\" name=\"url\" value=\"" +
       esc(node.url) + "\" placeholder=\"" DEFAULT_NODE_URL "\" autocapitalize=\"off\" spellcheck=\"false\"><p class=\"hint\">" +
       L("Leer lassen für mempool.space. Eigener Node im Heimnetz, z.&nbsp;B. <code>http://umbrel.local:3006</code> "
         "(mempool-App auf Umbrel) oder <code>http://192.168.1.20:3006</code>. Die Adresse wird vor dem Speichern geprüft.",
         "Leave empty for mempool.space. Your own node, e.g. <code>http://umbrel.local:3006</code> (mempool app on "
         "Umbrel) or <code>http://192.168.1.20:3006</code>. The address is tested before saving.") +
       "</p><button>" + L("Speichern", "Save") + "</button></form></section>";

  // Sicherheit
  b += String("<section><h2>") + L("Sicherheit", "Security") + "</h2><h3>" +
       L("Passwort für diese Seite", "Password for this page") +
       "</h3><form method=\"post\" action=\"/password\" id=\"pwf\"><input type=\"hidden\" name=\"ha1\" id=\"ha1\">"
       "<div class=\"row\"><div><label>" +
       L("Neues Passwort", "New password") +
       "</label><input type=\"password\" id=\"pw1\" autocomplete=\"new-password\"></div><div><label>" +
       L("Wiederholen", "Repeat") + "</label><input type=\"password\" id=\"pw2\" autocomplete=\"new-password\"></div></div>" +
       "<p class=\"hint\">" +
       (cfg.password.length() ? L("Passwort ist gesetzt (Benutzer <code>admin</code>). Beide Felder leer lassen und "
                                  "speichern entfernt es.",
                                  "A password is set (user <code>admin</code>). Leave both fields empty and save to "
                                  "remove it.")
                              : L("Kein Passwort gesetzt: Jeder im WLAN kann Einstellungen ändern. Benutzername ist "
                                  "<code>admin</code>.",
                                  "No password set: anyone on your Wi-Fi can change settings. User name is "
                                  "<code>admin</code>.")) +
       L(" Das Passwort verlässt den Browser nie: Er schickt nur einen daraus berechneten Wert, und "
         "das Gerät speichert nur diesen. Vergessen? Gerät per USB an einen PC anschließen und im seriellen "
         "Monitor <code>password-reset</code> senden.",
         " The password never leaves your browser: it only sends a value derived from it, and the device stores "
         "only that. Forgot it? Connect the device to a PC via USB and send <code>password-reset</code> in a "
         "serial monitor.") +
       "</p><button>" + L("Speichern", "Save") + "</button></form></section>" + pwScript();

  // System: Firmware und WLAN
  b += String("<section><h2>System</h2><h3 id=\"fw\">") + L("Firmware", "Firmware") + "</h3><p>" +
       L("Installiert: Version ", "Installed: version ") + FW_VERSION + ". ";
  const String latest = latestFirmware();
  const String relUrl = String("https://github.com/") + FW_REPO + "/releases";
  if (firmwareUpdateAvailable()) {
    b += String("<b>") + L("Neue Version ", "New version ") + latest + L(" verfügbar", " available") +
         "</b> (<a href=\"" + relUrl + "/tag/v" + latest + "\" target=\"_blank\" rel=\"noopener\">" +
         L("Änderungen", "changes") + "</a>).</p><form method=\"post\" action=\"/fw-install\" "
         "onsubmit=\"var b=this.querySelector('button');b.disabled=true;b.textContent='…'\"><button>" +
         L("Version ", "Install version ") + latest + L(" installieren", "") + "</button><p class=\"hint\">" +
         L("Das Gerät lädt die Firmware direkt von GitHub und startet danach neu (1–2 Minuten). Einstellungen und "
           "WLAN bleiben erhalten.",
           "The device downloads the firmware directly from GitHub and then restarts (1–2 minutes). Settings and "
           "Wi-Fi are kept.") +
         "</p></form>";
  } else {
    b += (latest.length() ? String(L("Das ist die neueste Version.", "This is the latest version."))
                          : String(L("Neueste Version noch nicht bekannt.", "Latest version not known yet."))) +
         "</p><form method=\"post\" action=\"/fw-check\"><button class=\"sec\">" +
         L("Nach Updates suchen", "Check for updates") + "</button><p class=\"hint\">" +
         L("Das Gerät fragt außerdem alle 6 Stunden selbst bei GitHub nach.",
           "The device also checks GitHub by itself every 6 hours.") +
         "</p></form>";
  }
  b += String("<details><summary>") + L("Firmware-Datei selbst hochladen", "Upload a firmware file yourself") +
       "</summary><form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\" "
       "onsubmit=\"var b=this.querySelector('button');b.disabled=true;b.textContent='…'\">"
       "<input type=\"file\" name=\"fw\" accept=\".bin\" required><button>" +
       L("Installieren", "Install") + "</button><p class=\"hint\">" +
       L("Datei <code>timechain-terminal-….bin</code> aus den <a href=\"", "File <code>timechain-terminal-….bin</code> from the <a href=\"") +
       relUrl + "\" target=\"_blank\" rel=\"noopener\">Releases</a>" +
       L("; Einstellungen und WLAN bleiben erhalten.", "; settings and Wi-Fi are kept.") +
       "</p></form></details><h3>WLAN</h3>";
  {
    String wf = FPSTR(WIFI_HTML);
    wf.replace("%CUR%", esc(WiFi.SSID()) + " (" + String(WiFi.RSSI()) + " dBm)");
    wf.replace("%OLD%", esc(WiFi.SSID()));
    wf.replace("%T_CONNECTED%", L("Verbunden mit", "Connected to"));
    wf.replace("%T_OTHER%", L("Mit anderem WLAN verbinden", "Connect to another Wi-Fi"));
    wf.replace("%T_SCAN%", L("Netzwerke suchen", "Search networks"));
    wf.replace("%T_SCANNING%", L("Suche läuft …", "Searching …"));
    wf.replace("%T_NONE%", L("Keine Netzwerke gefunden.", "No networks found."));
    wf.replace("%T_PICK%", L("– aus der Liste wählen –", "– pick from the list –"));
    wf.replace("%T_SSID%", L("Netzwerkname (SSID)", "Network name (SSID)"));
    wf.replace("%T_PASS%", L("Passwort", "Password"));
    wf.replace("%T_SHOW%", L("anzeigen", "show"));
    wf.replace("%T_CONNECT%", L("Verbinden", "Connect"));
    wf.replace("%T_OPEN%", L("offen", "open"));
    wf.replace("%T_HINT%",
               L("Das Gerät testet die neue Verbindung, bevor es sie übernimmt: Klappt sie nicht innerhalb von 20 "
                 "Sekunden, verbindet es sich wieder mit »%OLD%«. Danach startet es neu und ist im neuen WLAN unter "
                 "<a href=\"http://" WIFI_HOSTNAME ".local\">" WIFI_HOSTNAME ".local</a> erreichbar (oder unter "
                 "der neuen IP-Adresse, siehe Router). Nur 2,4-GHz-Netze.",
                 "The device tests the new connection before using it: if it does not work within 20 seconds, it "
                 "reconnects to »%OLD%«. It then restarts and can be reached on the new Wi-Fi at "
                 "<a href=\"http://" WIFI_HOSTNAME ".local\">" WIFI_HOSTNAME ".local</a> (or its new IP address, "
                 "see your router). 2.4 GHz networks only."));
    wf.replace("%OLD%", esc(WiFi.SSID()));
    b += wf;
  }
  b += String("<form method=\"post\" action=\"/wifi-reset\" onsubmit=\"return confirm('") +
       L("WLAN-Daten löschen und Einrichtungs-WLAN starten?", "Forget Wi-Fi and start the setup Wi-Fi?") +
       "')\" style=\"margin-top:14px\"><p class=\"hint\" style=\"margin-top:0\">" +
       L("Oder alle WLAN-Daten löschen: Das Gerät öffnet dann wieder das WLAN »" WIFI_AP_NAME "« zur Einrichtung.",
         "Or forget all Wi-Fi data: the device then opens the Wi-Fi »" WIFI_AP_NAME "« for setup again.") +
       "</p><button class=\"sec\">" + L("WLAN-Daten löschen", "Forget Wi-Fi") + "</button></form></section>";
  sendPage(b, 0);
}

// Ein-Klick-Update: Warteseite fragt den Status ab, bis die neue Version laeuft oder ein Fehler kommt
static const char FW_WAIT_HTML[] PROGMEM = R"fw(<section><h2>Firmware</h2><p id="fmsg">%T_RUN%</p><p class="hint" id="fhint">%T_INFO%</p>
<p><a href="/" id="fhome" hidden>%T_HOME%</a></p></section>
<script>(()=>{const m=document.getElementById('fmsg'),h=document.getElementById('fhint'),home=document.getElementById('fhome'),t0=Date.now();
function done(msg,cls,hint){m.textContent=msg;m.className=cls||'';h.textContent=hint||'';home.hidden=false}
function tick(){const c=new AbortController();setTimeout(()=>c.abort(),5000);
fetch('/api/status',{signal:c.signal,cache:'no-store'}).then(r=>r.json()).then(j=>{
if(j.version=='%VER%')return done('%T_OK%');
if(!j.fwBusy&&j.fwErr)return done(j.fwErr,'bad','%T_KEEP%');
setTimeout(tick,3000)}).catch(()=>{if(Date.now()-t0>240000)return done('%T_SLOW%','bad');setTimeout(tick,3000)})}
setTimeout(tick,4000)})();</script>)fw";

static void handleFwInstall() {
  if (!guard()) return;
  if (!firmwareUpdateAvailable()) {
    flash(L("Es gibt keine neuere Version.", "There is no newer version."), true);
    return redirect("/#fw");
  }
  fwInstallError = "";
  fwInstallPending = true;
  fwInstallAt = millis();
  String p = FPSTR(FW_WAIT_HTML);
  p.replace("%T_RUN%", String(L("Version ", "Version ")) + latestFirmware() +
                           L(" wird von GitHub geladen und installiert …", " is being downloaded from GitHub and installed …"));
  p.replace("%T_INFO%", L("Das dauert 1–2 Minuten; danach startet das Gerät neu. Diese Seite meldet sich, "
                          "sobald es wieder erreichbar ist.",
                          "This takes 1–2 minutes; then the device restarts. This page reports back as soon as "
                          "it can be reached again."));
  p.replace("%T_OK%", String(L("Version ", "Version ")) + latestFirmware() + L(" ist installiert.", " is installed."));
  p.replace("%T_KEEP%", String(L("Die bisherige Version ", "The previous version ")) + FW_VERSION +
                            L(" läuft unverändert weiter.", " keeps running unchanged."));
  p.replace("%T_SLOW%", L("Das Gerät antwortet nicht mehr. Bitte die Seite in ein paar Minuten neu laden.",
                          "The device does not respond. Please reload the page in a few minutes."));
  p.replace("%T_HOME%", L("Zur Übersicht", "To the overview"));
  p.replace("%VER%", latestFirmware());
  sendPage(p, 0);
}

// WLAN suchen (asynchron: erster Aufruf startet, folgende liefern das Ergebnis)
static void handleWifiScan() {
  if (!guard()) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, false);
    n = WIFI_SCAN_RUNNING;
  }
  if (n == WIFI_SCAN_RUNNING) return server.send(200, "application/json", "{\"busy\":true}");
  // nach Signalstaerke sortiert, jeder Name nur einmal
  int idx[40], m = 0;
  for (int i = 0; i < n && m < 40; ++i) {
    if (!WiFi.SSID(i).length()) continue;
    bool dup = false;
    for (int k = 0; k < m && !dup; ++k) dup = WiFi.SSID(idx[k]) == WiFi.SSID(i);
    if (!dup) idx[m++] = i;
  }
  for (int a = 0; a < m; ++a)
    for (int b = a + 1; b < m; ++b)
      if (WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[a])) std::swap(idx[a], idx[b]);
  JsonDocument d;
  JsonArray arr = d["n"].to<JsonArray>();
  for (int k = 0; k < m; ++k) {
    JsonObject o = arr.add<JsonObject>();
    o["s"] = WiFi.SSID(idx[k]);
    o["r"] = WiFi.RSSI(idx[k]);
    o["o"] = WiFi.encryptionType(idx[k]) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", out);
}

static void handleWifiConnect() {
  if (!guard()) return;
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (!ssid.length() || ssid.length() > 32 || pass.length() > 63 || (pass.length() && pass.length() < 8)) {
    flash(L("Bitte einen Netzwerknamen (höchstens 32 Zeichen) und ein Passwort mit 8 bis 63 Zeichen angeben "
            "(leer bei offenen Netzen).",
            "Please enter a network name (up to 32 characters) and a password of 8 to 63 characters (empty for "
            "open networks)."),
          true);
    return redirect("/#wifi");
  }
  if (ssid == WiFi.SSID() && pass == WiFi.psk()) {
    flash(String(L("Das Gerät ist bereits mit »", "The device is already connected to »")) + esc(ssid) + "«.");
    return redirect("/#wifi");
  }
  requestWifiSwitch(ssid, pass);
  String p = FPSTR(WIFI_WAIT_HTML);
  p.replace("%SSID%", esc(ssid));
  p.replace("%OLD%", esc(WiFi.SSID()));
  p.replace("%T_TRY%", L("Verbinde mit »%SSID%« …", "Connecting to »%SSID%« …"));
  p.replace("%T_INFO%",
            L("Das Gerät testet die Verbindung (bis zu 20 Sekunden) und startet bei Erfolg neu. Diese Seite "
              "prüft, ob es wieder erreichbar ist.",
              "The device tests the connection (up to 20 seconds) and restarts if it works. This page checks "
              "whether it can be reached again."));
  p.replace("%T_OK%", L("Verbunden mit »%SSID%«.", "Connected to »%SSID%«."));
  p.replace("%T_BACK%", L("Wieder mit »%OLD%« verbunden.", "Reconnected to »%OLD%«."));
  p.replace("%T_GONE%",
            L("Das Gerät antwortet hier nicht mehr – vermutlich ist es jetzt im WLAN »%SSID%«. Verbinde dein "
              "Handy oder deinen Rechner mit diesem WLAN und öffne <a href=\"http://" WIFI_HOSTNAME
              ".local\">" WIFI_HOSTNAME ".local</a> (oder die neue IP-Adresse aus dem Router). Klappt die "
              "Verbindung dort nicht, öffnet das Gerät nach einigen Minuten das Einrichtungs-WLAN »" WIFI_AP_NAME
              "«.",
              "The device no longer answers here – it is probably on the Wi-Fi »%SSID%« now. Connect your phone "
              "or computer to that Wi-Fi and open <a href=\"http://" WIFI_HOSTNAME ".local\">" WIFI_HOSTNAME
              ".local</a> (or the new IP address from your router). If it cannot connect there, the device "
              "opens the setup Wi-Fi »" WIFI_AP_NAME "« after a few minutes."));
  p.replace("%T_HOME%", L("Zur Übersicht", "To the overview"));
  String js = ssid;  // fuer den Vergleich im Skript
  js.replace("\\", "\\\\");
  js.replace("'", "\\'");
  js.replace("<", "\\x3c");
  p.replace("%SSIDJS%", js);
  p.replace("%SSID%", esc(ssid));
  p.replace("%OLD%", esc(WiFi.SSID()));
  sendPage(p, 0);
}

// Zustand des Wechsels fuer die Warteseite
static void handleWifiState() {
  if (!guard()) return;
  JsonDocument d;
  d["st"] = (int)wifiSwitchState();
  d["cur"] = WiFi.SSID();
  d["err"] = wifiSwitchError();
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", out);
}

// Speichert nur die Felder, die das jeweilige Formular mitschickt
static void handleSettings() {
  if (!guard()) return;
  pendingLang = lang;
  if (server.hasArg("lang")) pendingLang = server.arg("lang") == "en" ? LANG_EN : LANG_DE;
  if (server.hasArg("days")) {
    int days = server.arg("days").toInt();
    cfg.chartDays = chartPeriod(days) ? days : 7;
  }
  if (server.hasArg("cur1")) {
    int c1 = server.arg("cur1").toInt();
    cfg.cur1 = (c1 >= 0 && c1 < NUM_CUR) ? c1 : 0;
    String c2s = server.arg("cur2");
    int c2 = c2s.toInt();
    cfg.cur2 = (c2s == "none" || c2 < 0 || c2 >= NUM_CUR || c2 == cfg.cur1) ? CUR_NONE : c2;
  }
  if (server.hasArg("parts")) {
    parseParts(server.arg("parts"));
    cfg.parts = partsToString();
  }
  if (server.hasArg("miners")) {
    String m = server.arg("miners");
    m.trim();
    m.replace(" ", "");
    m.replace(";", ",");
    cfg.miners = m.substring(0, 120);
  }
  Lang old = lang;
  lang = pendingLang;  // fuer saveSettings und die Meldung
  saveSettings();
  lang = old;
  pendingLayout = layout;
  settingsPending = true;
  flash(pendingLang == LANG_EN ? "Saved. The display will be redrawn." : "Gespeichert. Das Display wird neu gezeichnet.");
  const String back = server.arg("back");
  redirect(back == "layout" ? "/layout" : back == "content" ? "/content" : "/");
}

static void handleTheme() {
  if (!guard()) return;
  String t = server.arg("t");
  if (!validTheme(t)) return server.send(400, "text/plain; charset=utf-8", L("Ungültiges Farbschema.", "Invalid color scheme."));
  cfg.theme = t;
  saveSettings();
  pendingLayout = layout;
  pendingLang = lang;
  settingsPending = true;
  server.send(200, "text/plain; charset=utf-8",
              L("Farben gespeichert. Das Display wird neu gezeichnet (ca. 30 s).", "Colors saved. The display will be redrawn (about 30 s)."));
}

// Ausrichtung und Kopfzeile (Seite "Layout bearbeiten")
static void handleDisplay() {
  if (!guard()) return;
  cfg.headerPrice = server.hasArg("hdr");
  cfg.headerQr = server.hasArg("hdrqr");
  int rot = server.arg("rot").toInt();
  cfg.rotation = (rot >= 0 && rot <= 3) ? rot : 0;
  if (!parseLayout(activeLayoutText(), pendingLayout)) {
    activeLayoutText() = defaultLayoutText();
    parseLayout(activeLayoutText(), pendingLayout);
  }
  pendingLang = lang;
  saveSettings();
  settingsPending = true;
  flash(L("Gespeichert. Das Display wird neu gezeichnet.", "Saved. The display will be redrawn."));
  redirect("/layout");
}

static void handleNode() {
  if (!guard()) return;
  Node n;
  if (!parseNodeUrl(server.arg("url"), n)) {
    flash(L("Ungültige Adresse. Beispiel: http://umbrel.local:3006", "Invalid address. Example: http://umbrel.local:3006"), true);
    return redirect();
  }
  uint32_t tip = testNode(n);
  if (!tip) {
    flash(String(L("Server ", "Server ")) + n.url + L(" antwortet nicht: ", " does not respond: ") + lastHttpError +
              L(". Nicht gespeichert.", ". Not saved."),
          true);
    return redirect();
  }
  cfg.nodeUrl = n.url;
  saveSettings();
  pendingNode = n;
  nodeChangePending = true;
  flash(String(L("Gespeichert. ", "Saved. ")) + n.url + L(" meldet Block ", " reports block ") + fmtInt(tip) + ".");
  Serial.printf("[cfg] neue Datenquelle %s (Block %u)\n", n.url.c_str(), tip);
  redirect();
}

static void handlePassword() {
  if (!guard()) return;
  // Normalfall: der Browser schickt nur HA1 (leer = Passwort entfernen)
  if (server.hasArg("ha1")) {
    String h = server.arg("ha1");
    h.toLowerCase();
    if (h.length() && !isHA1(h)) {
      flash(L("Ungültiger Wert.", "Invalid value."), true);
      return redirect();
    }
    cfg.password = h;
    saveSettings();
    loadDigestNonce();
    flash(h.length() ? L("Passwort gesetzt. Benutzername: admin", "Password set. User name: admin")
                     : L("Passwort entfernt.", "Password removed."));
    Serial.printf("[cfg] Passwort %s\n", h.length() ? "gesetzt" : "entfernt");
    return redirect();
  }
  // ohne JavaScript: Klartext-Felder (werden sofort in HA1 umgerechnet)
  String a = server.arg("pw"), b = server.arg("pw2");
  if (a != b) {
    flash(L("Die Passwörter stimmen nicht überein.", "The passwords do not match."), true);
    return redirect();
  }
  if (a.length() && a.length() < 6) {
    flash(L("Bitte mindestens 6 Zeichen.", "Please use at least 6 characters."), true);
    return redirect();
  }
  cfg.password = a.length() ? digestHA1(a.substring(0, 64)) : String("");
  saveSettings();
  flash(a.length() ? L("Passwort gesetzt. Benutzername: admin", "Password set. User name: admin")
                   : L("Passwort entfernt.", "Password removed."));
  Serial.printf("[cfg] Passwort %s\n", a.length() ? "gesetzt" : "entfernt");
  redirect();
}

// ---------------------------------------------------------------------------
// Firmware-Update. Waehrend eines Bildwechsels wird nicht geflasht: Panel und
// Flash-Schreiben zusammen belasten die Stromversorgung.
// ---------------------------------------------------------------------------
static bool updateRefused = false, updateDenied = false;

static void handleUpdateDone() {
  if (updateDenied) {
    updateDenied = false;
    server.requestAuthentication(BASIC_AUTH, PRODUCT_NAME);
    return;
  }
  bool ok = !updateRefused && !Update.hasError() && Update.isFinished();
  String b;
  if (ok) b = String("<div class=\"msg\">") + L("Update installiert. Das Gerät startet neu …", "Update installed. Restarting …") + "</div>";
  else if (updateRefused)
    b = String("<div class=\"msg err\">") +
        L("Das Display zeichnet gerade (ca. 30 s). Bitte danach noch einmal installieren.",
          "The display is redrawing (about 30 s). Please install again afterwards.") +
        "</div>";
  else b = String("<div class=\"msg err\">") + L("Update fehlgeschlagen: ", "Update failed: ") + esc(Update.errorString()) + "</div>";
  updateRefused = false;
  b += String("<script>setTimeout(()=>location.href='/',") + (ok ? "25000" : "4000") + ")</script>";
  server.sendHeader("Connection", "close");
  sendPage(b);
  if (ok) {
    Serial.println("[ota] fertig, Neustart");
    delay(800);
    ESP.restart();
  }
}

static void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    updateDenied = !authorized();
    updateRefused = !updateDenied && epdActive;
    if (updateDenied || updateRefused) {
      Serial.printf("[ota] abgelehnt: %s\n", updateDenied ? "kein Passwort" : "Bildwechsel laeuft");
      return;
    }
    Serial.printf("[ota] Start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() && Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.isRunning()) return;
    if (Update.end(true)) Serial.printf("[ota] %u Bytes geschrieben\n", up.totalSize);
    else Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    Serial.println("[ota] abgebrochen");
  }
}

// ---------------------------------------------------------------------------
// Vorschau und Layout-Editor
// ---------------------------------------------------------------------------
static void handlePreview() {
  if (!guard()) return;
  Layout tmp;
  const Layout *pl = nullptr;
  // ?rot=0..3: Vorschau in einer anderen Ausrichtung, ohne die Einstellung zu aendern
  const uint8_t rotSaved = cfg.rotation;
  if (server.hasArg("rot")) {
    cfg.rotation = server.arg("rot").toInt() & 3;
    if (!server.hasArg("l") && parseLayout(activeLayoutText(), tmp)) pl = &tmp;
  }
  struct RotRestore {
    uint8_t v;
    bool q;
    uint8_t c1, c2;
    ~RotRestore() { cfg.rotation = v, cfg.headerQr = q, cfg.cur1 = c1, cfg.cur2 = c2; }
  } rotRestore{rotSaved, cfg.headerQr, cfg.cur1, cfg.cur2};
  // ?cur1=/cur2=: andere Waehrungen nur fuer die Vorschau
  if (server.hasArg("cur1")) cfg.cur1 = server.arg("cur1").toInt() % NUM_CUR;
  if (server.hasArg("cur2")) cfg.cur2 = server.arg("cur2").toInt() % NUM_CUR;
  if (server.hasArg("hdrqr")) cfg.headerQr = server.arg("hdrqr") == "1";
  if (server.hasArg("l")) {
    String err;
    if (!parseLayout(server.arg("l"), tmp, &err)) return server.send(400, "text/plain; charset=utf-8", err);
    pl = &tmp;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(previewBmpSize());
  server.send(200, "image/bmp", "");
  WiFiClient c = server.client();
  uint32_t t0 = millis();
  // ?lang=en/de: Vorschau in einer anderen Sprache, ohne die Einstellung zu aendern
  Lang saved = lang;
  if (server.hasArg("lang")) lang = server.arg("lang") == "en" ? LANG_EN : LANG_DE;
  if (validTheme(server.arg("theme"))) themeOverride = server.arg("theme");
  void (*painter)() = nullptr;
  if (server.arg("screen") == "setup") painter = setupScreenPainter();
  if (server.arg("screen") == "info") painter = drawInfoScreen;
#ifdef TT_DEVTOOLS
  const bool extreme = server.arg("data") == "extrem";
  if (extreme) pushTestData();
#endif
  streamPreviewBmp(c, pl, painter);
#ifdef TT_DEVTOOLS
  if (extreme) popTestData();
#endif
  lang = saved;
  themeOverride = "";
  Serial.printf("[web] Vorschau in %lu ms\n", millis() - t0);
}

static void handleLayoutSave() {
  if (!guard()) return;
  Layout l;
  String err;
  if (!parseLayout(server.arg("l"), l, &err)) return server.send(400, "text/plain; charset=utf-8", err);
  activeLayoutText() = layoutToString(l);
  saveSettings();
  pendingLayout = l;
  pendingLang = lang;
  settingsPending = true;
  Serial.printf("[cfg] Layout: %s\n", activeLayoutText().c_str());
  server.send(200, "text/plain; charset=utf-8", L("Gespeichert. Das Display wird neu gezeichnet (ca. 30 s).",
                                                  "Saved. The display will be redrawn (about 30 s)."));
}

const char THEME_JS[] PROGMEM = R"(<script>
(()=>{const R=%ROLES%,P=%PRESETS%,D="%DEFAULT%";let t="%THEME%".split('');
const CC={k:'#1a1a1a',w:'#f4f2ec',y:'#e8c400',r:'#c8261e',o:'repeating-conic-gradient(#c8261e 0 25%,#e8c400 0 50%) 0 0/6px 6px'};
const K=['bg','text','title','bar','bartext','border','hl','hltext','headbg','headtext','price','logo','logosym','minedbg','minedtext','minedh','mpbg','mptext','chartfill','chartline','accent'];
const O=[{},{bg:'k',text:'w',title:'y',bar:'y',bartext:'k',border:'w',hl:'r',hltext:'w',headbg:'y',headtext:'k',price:'k',logo:'k',logosym:'y',minedbg:'w',minedtext:'k',minedh:'r',mpbg:'y',mptext:'k',chartfill:'r',chartline:'w',accent:'y'},
{hl:'r',hltext:'w',price:'r',logo:'r',minedh:'r',mpbg:'r',mptext:'w',chartfill:'r',bar:'k'},
{title:'k',bar:'k',bartext:'w',hl:'k',hltext:'w',price:'w',logo:'w',logosym:'k',minedh:'w',mpbg:'w',mptext:'k',chartfill:'k',accent:'k'},
{headbg:'w',headtext:'k',price:'r',logo:'y',logosym:'w'}];
const tb=document.getElementById('roles'),pr=document.getElementById('presets'),st=document.getElementById('thst');
window.TH=t.join('');
function changed(){window.TH=t.join('');st.textContent='';if(window.updPreview)updPreview()}
function draw(){tb.innerHTML='';R.forEach((n,i)=>{const d=document.createElement('div');d.className='rl';
d.innerHTML='<span>'+n+'</span><span class="sw">'+'kwyro'.split('').map(c=>'<label style="background:'+CC[c]+'"><input type="radio" name="c'+i+'" value="'+c+'"'+(t[i]==c?' checked':'')+'></label>').join('')+'</span>';
d.querySelectorAll('input').forEach(x=>x.onchange=()=>{t[i]=x.value;changed()});tb.appendChild(d)})}
P.forEach((n,i)=>{const b=document.createElement('button');b.type='button';b.className='sec';b.textContent=n;
b.onclick=()=>{t=D.split('');K.forEach((k,j)=>{if(O[i][k])t[j]=O[i][k]});draw();changed()};pr.appendChild(b)});
document.getElementById('thsave').onclick=async()=>{st.textContent='…';const r=await fetch('/theme',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'t='+t.join('')});st.textContent=await r.text()};
draw()})();
</script>)";

static const char EDITOR[] PROGMEM = R"(
<section><h2>%T_TITLE%</h2>
<p class="hint">%T_HELP%</p>
<div id="pal" class="pal"></div>
<div id="ed"><div class="hd"><span>TIMECHAIN TERMINAL</span><span>64.383 $</span></div><div id="grid"></div></div>
<div class="row" style="margin-top:4px">
<button type="button" class="sec" id="auto">%T_AUTO%</button>
<button type="button" class="sec" id="def">%T_DEF%</button>
<button type="button" class="sec" id="prev">%T_PREV%</button>
<button type="button" id="save">%T_SAVE%</button></div>
<p class="hint" id="st"></p>
</section>
<style>
.pal{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0}
.chk{display:flex;gap:8px;align-items:center;color:var(--fg)}
.pre{display:flex;flex-wrap:wrap;gap:6px}.pre button{margin:0;padding:6px 11px;font-size:14px;font-weight:500}
.thg{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1.4fr);gap:16px;align-items:start}
@media(max-width:720px){.thg{grid-template-columns:1fr}}
.pvbox{margin:0 0 6px}details{margin:10px 0}summary{cursor:pointer;color:var(--acc);font-weight:600}
#roles{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:6px 10px;margin-top:8px}
.rl{display:flex;justify-content:space-between;align-items:center;gap:10px;font-size:13px;background:var(--bg);border:1px solid var(--line);border-radius:7px;padding:6px 10px}
.rl:hover{border-color:var(--acc)}
.sw{display:inline-flex;gap:4px;flex:none}
.sw label{margin:0;display:inline-block;width:18px;height:18px;border-radius:50%;border:2px solid #888;cursor:pointer;position:relative}
.sw input{position:absolute;opacity:0}.sw label:has(input:checked){outline:2px solid var(--acc);outline-offset:1px}
.pal label{display:flex;align-items:center;gap:6px;margin:0;padding:5px 10px;border:1px solid var(--line);border-radius:20px;color:var(--fg);font-size:14px;cursor:pointer;user-select:none}
.pal input{margin:0}
#ed{position:relative;width:100%;margin:0 auto;background:#f4f2ec;border:1px solid var(--line);border-radius:6px;overflow:hidden;touch-action:none}
#ed .hd{position:absolute;left:0;top:0;right:0;background:#1a1a1a;color:#fff;display:flex;justify-content:space-between;align-items:center;padding:0 3%;font:700 clamp(9px,2.2vw,16px) system-ui}
#ed .hd span:last-child{color:#e8c400}
#grid{position:absolute;background-image:linear-gradient(#0000 calc(100% - 1px),#0001 0),linear-gradient(90deg,#0000 calc(100% - 1px),#0001 0);background-size:5% 8.333%}
.w{position:absolute;background:#fff;border:2px solid #1a1a1a;border-radius:3px;color:#1a1a1a;font:600 clamp(9px,1.6vw,14px) system-ui;padding:4px 6px;cursor:move;overflow:hidden;user-select:none}
.w.bad{border-color:#c62828;background:#fde8e8}.w small{display:block;font-weight:400;opacity:.6}
.w .rs{position:absolute;z-index:1;opacity:0;transition:opacity .15s}.w:hover .rs,.w .rs[data-e=se]{opacity:1}
.rs[data-e=n],.rs[data-e=s]{left:16px;right:16px;height:9px;cursor:ns-resize}.rs[data-e=n]{top:-2px}.rs[data-e=s]{bottom:-2px}
.rs[data-e=e],.rs[data-e=w]{top:16px;bottom:16px;width:9px;cursor:ew-resize}.rs[data-e=e]{right:-2px}.rs[data-e=w]{left:-2px}
.rs[data-e=n]:hover,.rs[data-e=s]:hover,.rs[data-e=e]:hover,.rs[data-e=w]:hover{background:#c8261e66}
.rs[data-e=nw],.rs[data-e=ne],.rs[data-e=sw],.rs[data-e=se]{width:16px;height:16px}
.rs[data-e=nw]{left:0;top:0;cursor:nwse-resize;background:linear-gradient(315deg,#0000 50%,#c8261e 50%)}
.rs[data-e=ne]{right:0;top:0;cursor:nesw-resize;background:linear-gradient(45deg,#0000 50%,#c8261e 50%)}
.rs[data-e=sw]{left:0;bottom:0;cursor:nesw-resize;background:linear-gradient(225deg,#0000 50%,#c8261e 50%)}
.rs[data-e=se]{right:0;bottom:0;cursor:nwse-resize;background:linear-gradient(135deg,#0000 50%,#c8261e 50%)}
.w{padding-left:10px}
</style>
<script>
const WI=%WIDGETS%,DEF="%DEFAULT%",T=%TEXTS%;let L=parse("%LAYOUT%");
let [C,R,SW,SH]=[%GRID%];const grid=document.getElementById('grid'),st=document.getElementById('st'),pv=document.getElementById('pv');
(()=>{const ed=document.getElementById('ed');ed.style.aspectRatio=SW+'/'+SH;if(SH>SW)ed.style.maxWidth='420px';
ed.querySelector('.hd').style.height=(62/SH*100)+'%';Object.assign(grid.style,{left:12/SW*100+'%',top:70/SH*100+'%',
width:(SW-24)/SW*100+'%',height:(SH-78)/SH*100+'%',backgroundSize:100/C+'% '+100/R+'%'});
WI.forEach(i=>{i.xw=Math.min(i.xw,C);i.xh=Math.min(i.xh,R)})})();
function pos(d,o){d.style.left=o.x*100/C+'%';d.style.top=o.y*100/R+'%';d.style.width='calc('+o.w*100/C+'% - '+800/(SW-16)+'%)';
d.style.height='calc('+o.h*100/R+'% - '+800/(SH-70)+'%)'}
function parse(s){return s.split(';').filter(x=>x).map(x=>{const[k,v]=x.split(':');const[a,b,c,d]=(v||'').split(',').map(Number);return{k,x:a,y:b,w:c,h:d}}).filter(o=>info(o.k)&&o.w>0&&o.h>0)}
function str(){return L.map(o=>o.k+':'+[o.x,o.y,o.w,o.h].join(',')).join(';')}
function info(k){return WI.find(w=>w.k==k)}
function overlap(a,b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h}
function bad(o){return L.some(p=>p!==o&&overlap(o,p))}
function render(){grid.innerHTML='';let ok=true;for(const o of L){const d=document.createElement('div'),i=info(o.k),b=bad(o);ok=ok&&!b;
d.className='w'+(b?' bad':'');pos(d,o);
d.innerHTML=i.n+'<small>'+o.w+' × '+o.h+'</small><small>'+T.min+' '+i.mw+'×'+i.mh+' · '+T.max+' '+i.xw+'×'+i.xh+'</small>'+['n','s','e','w','nw','ne','sw','se'].map(e=>'<div class="rs" data-e="'+e+'"></div>').join('');d.title=i.n+': '+T.min+' '+i.mw+'×'+i.mh+', '+T.max+' '+i.xw+'×'+i.xh;drag(d,o);grid.appendChild(d)}
document.getElementById('save').disabled=!ok;st.textContent=ok?'':T.overlap;pal();if(ok&&window.pvReady)updPreview()}
function pal(){const p=document.getElementById('pal');p.innerHTML='';for(const i of WI){const l=document.createElement('label'),c=document.createElement('input');c.type='checkbox';c.checked=L.some(o=>o.k==i.k);
c.onchange=()=>{if(c.checked)add(i);else L=L.filter(o=>o.k!=i.k);render()};l.title=T.min+' '+i.mw+'×'+i.mh+', '+T.max+' '+i.xw+'×'+i.xh;l.append(c,i.n);p.appendChild(l)}}
function free(o){return o.x>=0&&o.y>=0&&o.x+o.w<=C&&o.y+o.h<=R&&!L.some(p=>overlap(o,p))}
function add(i){for(const[w,h]of[[i.dw,i.dh],[i.mw,i.mh]])for(let y=0;y+h<=R;y++)for(let x=0;x+w<=C;x++){const o={k:i.k,x,y,w,h};if(free(o)){L.push(o);return}}
L.push({k:i.k,x:0,y:0,w:i.mw,h:i.mh});st.textContent=T.nospace}
function drag(d,o){d.onpointerdown=e=>{e.preventDefault();d.setPointerCapture(e.pointerId);const rs=e.target.classList.contains('rs')?e.target.dataset.e:'',r=grid.getBoundingClientRect(),
cw=r.width/C,ch=r.height/R,sx=e.clientX,sy=e.clientY,s={...o},i=info(o.k);
d.onpointermove=m=>{const dx=Math.round((m.clientX-sx)/cw),dy=Math.round((m.clientY-sy)/ch);
if(rs){// Ecke/Kante: die gegenueberliegende Seite bleibt stehen
if(rs.includes('e'))o.w=Math.max(i.mw,Math.min(C-s.x,i.xw,s.w+dx));if(rs.includes('s'))o.h=Math.max(i.mh,Math.min(R-s.y,i.xh,s.h+dy));
if(rs.includes('w')){const nw=Math.max(i.mw,Math.min(s.x+s.w,i.xw,s.w-dx));o.x=s.x+s.w-nw;o.w=nw}
if(rs.includes('n')){const nh=Math.max(i.mh,Math.min(s.y+s.h,i.xh,s.h-dy));o.y=s.y+s.h-nh;o.h=nh}}else{o.x=Math.max(0,Math.min(C-o.w,s.x+dx));o.y=Math.max(0,Math.min(R-o.h,s.y+dy))}
pos(d,o);d.querySelector('small').textContent=o.w+' × '+o.h;d.classList.toggle('bad',bad(o))};
d.onpointerup=()=>{d.onpointermove=null;render()}}}
// Breite w auf Widgets nach Gewicht verteilen, Mindestbreiten beachten
function split(keys,w,wt){const tot=keys.reduce((a,k)=>a+(wt[k]||1),0);let r=keys.map(k=>Math.max(info(k).mw,Math.floor(w*(wt[k]||1)/tot)));
let s=r.reduce((a,b)=>a+b,0),i=0;while(s<w){r[i++%r.length]++;s++}while(s>w){const j=r.findIndex((v,n)=>v>info(keys[n]).mw);if(j<0)break;r[j]--;s--}return r}
function stack(keys,x,y,w,h,out){const hs=keys.map(k=>info(k).mh);let r=h-hs.reduce((a,b)=>a+b,0),n=0;while(r>0){hs[n++%hs.length]++;r--}
let yy=y;keys.forEach((k,j)=>{out.push({k,x,y:yy,w,h:hs[j]});yy+=hs[j]})}
function row(keys,x,y,w,h,out,wt){let xx=x;split(keys,w,wt).forEach((ww,n)=>{out.push({k:keys[n],x:xx,y,w:ww,h});xx+=ww})}
function valid(out){return out.every(o=>{const i=info(o.k);return o.w>=i.mw&&o.h>=i.mh&&o.w<=i.xw&&o.h<=i.xh&&o.x>=0&&o.y>=0&&o.x+o.w<=C&&o.y+o.h<=R})&&
out.every((o,a)=>out.every((p,b)=>a==b||!overlap(o,p)))}
// Notloesung: kleinste Groessen der Reihe nach setzen, dann wachsen lassen, bis alles voll ist
function grow(keys){const out=[];const fits=o=>o.x+o.w<=C&&o.y+o.h<=R&&!out.some(p=>p!==o&&overlap(o,p));
for(const k of keys){const i=info(k);let done=false;for(let y=0;y+i.mh<=R&&!done;y++)for(let x=0;x+i.mw<=C&&!done;x++){const o={k,x,y,w:i.mw,h:i.mh};if(fits(o)){out.push(o);done=true}}}
let more=true;while(more){more=false;for(const o of out){const i=info(o.k);if(o.w<i.xw){o.w++;if(fits(o))more=true;else o.w--}if(o.h<i.xh){o.h++;if(fits(o))more=true;else o.h--}}}return out}
// Abschnitte von oben nach unten mit Wunschhoehe; Restplatz geht an grosse Inhalte, nicht an Kacheln
const TILES=['mempool','hashrate','difficulty','halving','lightning','miner1','miner2','miner3','miner4'];
function plan(keys,qrTile){const P=R>C,mh=k=>info(k).mh,mx=a=>Math.max(0,...a.map(mh)),pick=a=>a.filter(k=>keys.includes(k));
let big=pick(['block','chart','miner']),side=pick(['moscow','fees','history']),tiles=pick(TILES),chain=keys.includes('chain');
if(qrTile&&side.includes('history')){side=side.filter(k=>k!='history');tiles.push('history')}
if(!P&&big.length){if(big.length>=3&&side.length){side.push('miner');big=big.filter(k=>k!='miner')}
while(side.length>1&&side.reduce((a,k)=>a+mh(k),0)>8){const k=side.includes('history')?'history':side[side.length-1];side=side.filter(x=>x!=k);tiles.push(k)}}
const S=[];
if(!P&&big.length){S.push({t:'top',h:6,min:Math.max(mx(big),side.reduce((a,k)=>a+mh(k),0),2),grow:4})}
else{for(const k of ['block','chart','miner'])if(big.includes(k))S.push({t:'one',keys:[k],h:k=='block'?6:k=='chart'?5:4,min:Math.max(mh(k),k=='block'?4:3),grow:3});
if(side.length){if(big.length||chain||tiles.length)S.push({t:'row',keys:side,h:3,min:mx(side),grow:1});
else for(const k of side)S.push({t:'one',keys:[k],h:3,min:mh(k),grow:3})}}
if(chain)S.push({t:'one',keys:['chain'],h:P?5:4,min:3,grow:2});
if(tiles.length){const per0=P?3:6,only=!S.length,rows=only?Math.min(tiles.length,Math.max(1,Math.round(R/6))):Math.ceil(tiles.length/per0),per=Math.ceil(tiles.length/rows);
for(let r=0;r*per<tiles.length;r++){const part=tiles.slice(r*per,(r+1)*per),m=mx(part);S.push({t:'row',keys:part,h:Math.max(2,m),min:m,grow:only?0:1,tile:!only})}}
S.forEach(s=>s.h=Math.max(s.h,s.min));
let sum=S.reduce((a,s)=>a+s.h,0);
for(let i=S.length-1;i>=0&&sum>R;i--)while(sum>R&&S[i].h>S[i].min){S[i].h--;sum--}
if(sum>R)return qrTile?null:plan(keys,true);
let extra=R-sum;for(const s of S){const g=Math.min(extra,s.grow);s.h+=g;extra-=g}
const tgt=S.some(s=>!s.tile)?S.filter(s=>!s.tile):S;let n=0;while(extra>0&&tgt.length){tgt[n++%tgt.length].h++;extra--}
const out=[],wt={block:13,chart:10,miner:7,moscow:1,fees:1,history:1.4};let y=0;
for(const s of S){if(s.t=='top'){if(side.length){const sw=7;row(big,0,y,C-sw,s.h,out,wt);stack(side,C-sw,y,sw,s.h,out)}else row(big,0,y,C,s.h,out,wt)}
else if(s.t=='one')out.push({k:s.keys[0],x:0,y,w:C,h:s.h});else row(s.keys,0,y,C,s.h,out,wt);y+=s.h}
return valid(out)&&y==R&&out.length==keys.length?out:qrTile?null:plan(keys,true)}
// Notloesung: Reihen ("Regale") mit Mindestbreiten fuellen, Hoehe je Reihe = groesste Mindesthoehe
function shelves(keys){const order=['block','chart','miner','moscow','fees','history','chain',...TILES];keys=[...keys].sort((a,b)=>order.indexOf(a)-order.indexOf(b));
const rows=[];let cur=[],w=0;for(const k of keys){const i=info(k);if(w+i.mw>C&&cur.length){rows.push(cur);cur=[];w=0}cur.push(k);w+=i.mw}if(cur.length)rows.push(cur);
const hs=rows.map(r=>Math.max(...r.map(k=>info(k).mh)));let extra=R-hs.reduce((a,b)=>a+b,0);if(extra<0)return null;
const prefer=rows.map(r=>r.some(k=>!TILES.includes(k)));let n=0;const idx=prefer.some(x=>x)?rows.map((r,i)=>i).filter(i=>prefer[i]):rows.map((r,i)=>i);
while(extra>0){hs[idx[n++%idx.length]]++;extra--}
const out=[],wt={block:13,chart:10,miner:7};let y=0;rows.forEach((r,i)=>{row(r,0,y,C,hs[i],out,wt);y+=hs[i]});return valid(out)?out:null}
function auto(){const keys=L.map(o=>o.k);L=plan(keys)||shelves(keys)||grow(keys);render()}
document.getElementById('auto').onclick=auto;
document.getElementById('def').onclick=()=>{L=parse(DEF);render()};
let pvTimer=0;const pvst=document.getElementById('pvst');
function updPreview(){clearTimeout(pvTimer);pvTimer=setTimeout(()=>{pvst.textContent=T.loading;pv.onload=()=>pvst.textContent='';
pv.onerror=()=>pvst.textContent=T.err;pv.src='/preview.bmp?l='+encodeURIComponent(str())+'&theme='+(window.TH||'')+'&t='+Date.now()},400)}
document.getElementById('prev').onclick=()=>{updPreview();pv.scrollIntoView({behavior:'smooth',block:'center'})};
document.getElementById('save').onclick=async()=>{st.textContent='…';const r=await fetch('/layout',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'l='+encodeURIComponent(str())});st.textContent=await r.text()};
window.pvReady=true;render();
</script>)";

static void handleLayoutPage() {
  if (!guard()) return;
  String widgets = "[";
  for (int i = 0; i < W_COUNT; ++i) {
    const WidgetInfo &w = WIDGETS[i];
    if (i) widgets += ',';
    widgets += String("{\"k\":\"") + w.key + "\",\"n\":\"" + (lang == LANG_EN ? w.en : w.de) + "\",\"mw\":" + w.minW +
               ",\"mh\":" + w.minH + ",\"xw\":" + w.maxW + ",\"xh\":" + w.maxH + ",\"dw\":" + w.defW +
               ",\"dh\":" + w.defH + "}";
  }
  widgets += "]";
  String texts = String("{\"overlap\":\"") +
                 L("Rot markierte Inhalte überlappen – so kann nicht gespeichert werden.",
                   "Widgets marked red overlap – cannot save like this.") +
                 "\",\"nospace\":\"" + L("Kein freier Platz – bitte etwas verkleinern oder „Automatisch anordnen“.",
                                        "No free space – shrink something or use “Arrange automatically”.") +
                 "\",\"min\":\"" + L("min.", "min") + "\",\"max\":\"" + L("max.", "max") +
                 "\",\"loading\":\"" + L("Vorschau wird berechnet …", "Rendering preview …") + "\",\"err\":\"" +
                 L("Vorschau fehlgeschlagen", "Preview failed") + "\"}";
  String page = FPSTR(EDITOR);
  page.replace("%WIDGETS%", widgets);
  page.replace("%TEXTS%", texts);
  page.replace("%DEFAULT%", defaultLayoutText());
  // nur gueltige Inhalte an den Editor geben (z.B. entfallene wie das fruehere "qr")
  Layout clean;
  page.replace("%LAYOUT%", parseLayout(activeLayoutText(), clean) ? layoutToString(clean) : String(defaultLayoutText()));
  page.replace("%GRID%", String(gridCols()) + "," + String(gridRows()) + "," + String(screenW()) + "," +
                             String(screenH()));
  page.replace("%T_TITLE%", L("Layout", "Layout"));
  page.replace("%T_HELP%",
               L("Inhalte oben an- oder abwählen. Kästen verschieben, an den roten Ecken oder an den Kanten in der Größe ändern. "
                 "„Automatisch anordnen“ verteilt die gewählten Inhalte lückenlos. „Vorschau“ zeigt das echte Bild, "
                 "ohne zu speichern. Neu hinzugefügte Inhalte (z. B. Preis-Chart) bekommen ihre Daten erst nach dem Speichern.",
                 "Select widgets above. Drag boxes to move them, use the red corners or the edges to resize. “Arrange "
                 "automatically” fills the screen without gaps. “Preview” shows the real image without saving. Newly added "
                 "widgets (e.g. the price chart) get their data after saving."));
  page.replace("%T_AUTO%", L("Automatisch anordnen", "Arrange automatically"));
  page.replace("%T_DEF%", L("Standard", "Default"));
  page.replace("%T_PREV%", L("Vorschau", "Preview"));
  page.replace("%T_SAVE%", L("Speichern", "Save"));

  // Ausrichtung und Kopfzeile
  String disp = String("<section><h2>") + L("Ausrichtung und Kopfzeile", "Orientation and header") +
                "</h2><form method=\"post\" action=\"/display\"><select name=\"rot\">" +
                opt("0", L("Querformat", "Landscape"), cfg.rotation == 0) +
                opt("2", L("Querformat, um 180° gedreht", "Landscape, rotated 180°"), cfg.rotation == 2) +
                opt("1", L("Hochformat (Anschluss rechts)", "Portrait (connector right)"), cfg.rotation == 1) +
                opt("3", L("Hochformat (Anschluss links)", "Portrait (connector left)"), cfg.rotation == 3) +
                "</select><label class=\"chk\"><input type=\"checkbox\" name=\"hdr\" value=\"1\"" +
                (cfg.headerPrice ? " checked" : "") + ">" +
                L("Kurs in der Kopfzeile (Hauptwährung groß, Zweitwährung klein darunter)",
                  "Price in the header (main currency large, second currency small below)") +
                "</label><label class=\"chk\"><input type=\"checkbox\" name=\"hdrqr\" value=\"1\"" +
                (cfg.headerQr ? " checked" : "") + ">" +
                L("Kleiner QR-Code zur Webseite rechts in der Kopfzeile", "Small QR code for the web page at the right of the header") +
                "</label><p class=\"hint\">" +
                L("Das Gerät hat keinen Lagesensor. Hoch- und Querformat haben je ein eigenes Layout. Die Währungen "
                  "stehen unter Übersicht → Einstellungen.",
                  "The device has no orientation sensor. Portrait and landscape each have their own layout. The "
                  "currencies are under Overview → Settings.") +
                "</p><button>" + L("Speichern", "Save") + "</button></form></section>";

  // Farben mit gemeinsamer Vorschau
  String theme = String("<section><h2>") + L("Vorschau", "Preview") +
                 "</h2><div class=\"pvbox\"><img id=\"pv\" class=\"shot\" alt=\"\"><p class=\"hint\" id=\"pvst\"></p></div>"
                 "<p class=\"hint\">" +
                 L("Zeigt Layout und Farben, ohne zu speichern, und aktualisiert sich bei jeder Änderung. Klick auf das "
                   "Bild vergrößert es.",
                   "Shows layout and colors without saving and updates on every change. Click the image to enlarge it.") +
                 "</p></section><section><h2>" + L("Farben", "Colors") + "</h2><p class=\"hint\">" +
                 L("Vorlage wählen, bei Bedarf einzelne Farben anpassen.", "Choose a preset, adjust single colors if you like.") +
                 "</p><div class=\"pre\" id=\"presets\"></div><details><summary>" +
                 L("Einzelne Farben anpassen", "Adjust single colors") + "</summary><p class=\"hint\">" +
                 L("Orange wird aus Rot und Gelb gemischt und eignet sich nur für Flächen.",
                   "Orange is mixed from red and yellow and only suits areas.") +
                 "</p><div id=\"roles\"></div></details><button type=\"button\" id=\"thsave\">" +
                 L("Farben speichern", "Save colors") + "</button><p class=\"hint\" id=\"thst\"></p></section>";
  String roles = "[";
  for (int i = 0; i < C_ROLE_COUNT; ++i) roles += String(i ? "," : "") + "\"" + (lang == LANG_EN ? ROLES[i].en : ROLES[i].de) + "\"";
  roles += "]";
  String js = FPSTR(THEME_JS);
  js.replace("%ROLES%", roles);
  js.replace("%THEME%", cfg.theme);
  js.replace("%DEFAULT%", defaultTheme());
  js.replace("%PRESETS%", lang == LANG_EN ? "['Default','Dark','Red','Black & white','Light header']"
                                          : "['Standard','Dunkel','Rot','Schwarz-Weiß','Helle Kopfzeile']");
  // Farb-Skript zuerst (setzt window.TH), dann der Editor (ruft die Vorschau auf)
  int sp = page.indexOf("<script>");
  page = disp + page.substring(0, sp) + theme + js + page.substring(sp);
  sendPage(page, 1);
}

// ---------------------------------------------------------------------------
// Seite "Inhalte": Einstellungen einzelner Inhalte und Ereignis-Editor
// ---------------------------------------------------------------------------
static const char EVENTS_HTML[] PROGMEM = R"ev(<section id="events"><h2>%T_TITLE%</h2><p class="hint">%T_HELP%</p>
<div class="evbar"><input type="search" id="evq" placeholder="%T_SEARCH%"><select id="evf"><option value="all">%T_ALL%</option>
<option value="on">%T_ON%</option><option value="off">%T_OFF%</option><option value="own">%T_OWNS%</option><option value="edit">%T_EDITS%</option></select>
<select id="evs"><option value="chrono">%T_CHRONO%</option><option value="cal">%T_CAL%</option></select>
<button type="button" id="evadd">+ %T_ADD%</button></div>
<div id="evform" hidden><h3 id="evtitle" style="margin-top:2px"></h3><div class="row"><div><label>%T_DATE%</label><input type="date" id="evdate" min="1900-01-01" max="2100-12-31"></div></div>
<label>Deutsch</label><input type="text" id="evde" maxlength="%MAX%"><label>English</label><input type="text" id="even" maxlength="%MAX%">
<p class="hint">%T_FORMHINT%</p><div class="row"><button type="button" id="evok">%T_OK%</button><button type="button" class="sec" id="evcancel">%T_CANCEL%</button></div></div>
<p class="hint"><span id="evcount"></span> <span id="evst"></span></p><div id="evlist"></div></section>)ev";

static const char EVENTS_JS[] PROGMEM = R"js(<script>
(()=>{const T=%TEXTS%,LG='%LANG%',$=id=>document.getElementById(id);let S=null,ed=null;
const p2=n=>String(n).padStart(2,'0'),fmt=r=>LG=='en'?r.y+'-'+p2(r.m)+'-'+p2(r.d):p2(r.d)+'.'+p2(r.m)+'.'+r.y,txt=r=>LG=='en'?(r.en||r.de):(r.de||r.en);
async function load(){const r=await fetch('/api/events');S=await r.json();S.offSet=new Set(S.off);S.editMap={};S.edit.forEach(e=>S.editMap[e.i]=e);draw()}
function rows(){const o=[];S.builtin.forEach((b,i)=>{const e=S.editMap[i],v=e?{y:e.y,m:e.m,d:e.d,de:e.de,en:e.en}:{y:b[0],m:b[1],d:b[2],de:b[3],en:b[4]};
o.push({...v,kind:'b',i,on:!S.offSet.has(i),edited:!!e})});S.own.forEach((x,j)=>o.push({...x,kind:'o',j,on:x.on!==false}));return o}
function btn(t,title,f){const b=document.createElement('button');b.type='button';b.className='ib';b.textContent=t;b.title=title;b.onclick=f;return b}
function draw(){const q=$('evq').value.toLowerCase(),f=$('evf').value,all=rows();
let R=all.filter(r=>(!q||(r.de+' '+r.en+' '+fmt(r)).toLowerCase().includes(q))&&(f=='all'||(f=='on'&&r.on)||(f=='off'&&!r.on)||(f=='own'&&r.kind=='o')||(f=='edit'&&r.edited)));
R.sort($('evs').value=='cal'?(a,b)=>a.m-b.m||a.d-b.d||a.y-b.y:(a,b)=>a.y-b.y||a.m-b.m||a.d-b.d);
const box=$('evlist');box.innerHTML='';
for(const r of R){const d=document.createElement('div');d.className='ev'+(r.on?'':' off');
const c=document.createElement('input');c.type='checkbox';c.checked=r.on;c.title=T.active;
c.onchange=()=>{if(r.kind=='b'){c.checked?S.offSet.delete(r.i):S.offSet.add(r.i)}else S.own[r.j].on=c.checked;save()};
const dt=document.createElement('span');dt.className='evd';dt.textContent=fmt(r);
const tx=document.createElement('span');tx.className='evt';tx.textContent=txt(r);
if(r.kind=='o'||r.edited){const b=document.createElement('small');b.textContent=r.kind=='o'?T.own:T.edited;tx.append(b)}
const a=document.createElement('span');a.className='eva';a.append(btn('\u270E',T.edit,()=>openForm(r)));
if(r.kind=='o')a.append(btn('\u2715',T.del,()=>{if(confirm(T.delq)){S.own.splice(r.j,1);save()}}));
else if(r.edited)a.append(btn('\u21BA',T.reset,()=>{delete S.editMap[r.i];save()}));
d.append(c,dt,tx,a);box.append(d)}
$('evcount').textContent=T.count.replace('%1',R.length).replace('%2',all.length).replace('%3',all.filter(r=>r.on).length)}
function openForm(r){ed=r||{kind:'new'};$('evform').hidden=false;$('evtitle').textContent=r?T.edit:T.add;
$('evdate').value=r?r.y+'-'+p2(r.m)+'-'+p2(r.d):'';$('evde').value=r?r.de||'':'';$('even').value=r?r.en||'':'';
$('evform').scrollIntoView({behavior:'smooth',block:'center'});$('evde').focus()}
$('evadd').onclick=()=>openForm(null);$('evcancel').onclick=()=>{$('evform').hidden=true};
$('evok').onclick=()=>{const dv=$('evdate').value;if(!dv)return alert(T.nodate);const[y,m,d]=dv.split('-').map(Number);
const de=$('evde').value.trim(),en=$('even').value.trim();if(!de&&!en)return alert(T.notext);const v={y,m,d,de,en};
if(ed.kind=='new')S.own.push({...v,on:true});else if(ed.kind=='o')Object.assign(S.own[ed.j],v);else S.editMap[ed.i]={i:ed.i,...v};
$('evform').hidden=true;save()};
async function save(){$('evst').textContent='…';const r=await fetch('/api/events',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({off:[...S.offSet],edit:Object.values(S.editMap),own:S.own})});$('evst').textContent=await r.text();if(r.ok)draw();else load()}
['evq','evf','evs'].forEach(id=>$(id).oninput=draw);load()})();
</script>)js";

// Teile je Kachel: Reihenfolge = Prioritaet, einzeln abschaltbar (Daten als JSON, Liste per JS)
static const char PARTS_HTML[] PROGMEM = R"pt(<section id="parts"><h2>%T_TITLE%</h2><p class="hint">%T_HELP%</p>
<p class="hint" id="plegend"></p>
<style>.pg{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:12px;margin:10px 0}
.pgc{border:1px solid var(--line);border-radius:8px;padding:8px 10px}.pgc.out{opacity:.6}.pgc h3{margin:2px 0 2px;font-size:15px}
.pgi{font-size:12px;color:var(--mut);margin:0 0 6px}
.pr{display:flex;align-items:center;gap:6px;padding:4px 0;border-top:1px solid var(--line)}
.pr label{flex:1;margin:0;display:flex;gap:8px;align-items:center;font-weight:400;cursor:pointer}.pr.off label{opacity:.5}
.pr input{width:auto;margin:0}.pr button{padding:2px 9px;margin:0;min-width:0;font-size:13px;background:none;color:var(--acc);border:1px solid var(--line)}
.pr button:disabled{opacity:.25}.pn{color:var(--mut);font-size:12px;min-width:18px;text-align:right}
.pt{display:flex;flex-direction:column;line-height:1.25}.ps{font-size:12px;font-weight:600}
.ps.y{color:var(--ok)}.ps.p{color:var(--acc)}.ps.n{color:var(--err)}.ps.d,.ps.f{color:var(--mut)}
.pr.fix label{cursor:default}.pr.fix .pn{font-size:13px}</style>
<form method="post" action="/settings" id="pform"><input type="hidden" name="back" value="content"><input type="hidden" name="parts" id="pval">
<div class="pg" id="pgrid"></div><button>%T_SAVE%</button> <button type="button" id="pdef" class="sec">%T_DEF%</button></form></section>
<script>(()=>{const G=%GROUPS%,cur='%CUR%',T=%TEXTS%,st={};let M=null,tm=0;
G.forEach(g=>{st[g.k]=g.p.map(p=>({k:p.k,on:true}))});
cur.split(';').forEach(s=>{const i=s.indexOf(':');if(i<0)return;const k=s.slice(0,i),g=G.find(x=>x.k==k);if(!g)return;const l=[];
s.slice(i+1).split(',').forEach(t=>{const off=t[0]=='-',kk=off?t.slice(1):t;if(g.p.some(p=>p.k==kk)&&!l.some(x=>x.k==kk))l.push({k:kk,on:!off})});
g.p.forEach(p=>{if(!l.some(x=>x.k==p.k))l.push({k:p.k,on:true})});st[k]=l});
const val=()=>G.map(g=>g.k+':'+st[g.k].map(x=>(x.on?'':'-')+x.k).join(',')).join(';');
const grid=document.getElementById('pgrid');
document.getElementById('plegend').textContent=T.legend;
function measure(){clearTimeout(tm);tm=setTimeout(()=>{fetch('/api/parts?p='+encodeURIComponent(val())).then(r=>r.json()).then(j=>{M=j;draw(true)}).catch(()=>{})},250)}
// Die beiden Blockketten-Gruppen gehoeren zur selben Kachel; gezeigt wird je Gruppe laut M.cs
function wsOf(gi){const k=G[gi].k,gk=k=='chainmp'?G.findIndex(g=>g.k=='chain'):gi;return M.w.filter(w=>w.g==gk)}
function status(gi,pi){if(!M)return null;const ws=wsOf(gi);if(!ws.length)return null;const k=G[gi].k;
if((k=='chain'||k=='chainmp')&&M.cs)return M.cs[k=='chain'?0:1]>>pi&1?['y',T.yes]:['n',T.noroom];
const shown=ws.filter(w=>w.s>>pi&1),data=ws.filter(w=>w.d>>pi&1);
if(shown.length==ws.length)return['y',T.yes];
if(shown.length)return['p',T.part+': '+shown.map(w=>w.t).join(', ')];
if(!data.length)return['d',T.nodata];return['n',T.noroom]}
function draw(keep){grid.innerHTML='';G.forEach((g,gi)=>{const c=document.createElement('div');c.className='pgc';
const h=document.createElement('h3');h.textContent=g.t;const info=document.createElement('div');info.className='pgi';
if(M){const ws=wsOf(gi);if(ws.length)info.textContent=T.inl+' ('+M.o+'): '+ws.map(w=>w.t+' '+w.c).join(', ');
else{info.textContent=T.notin+' ('+M.o+')';c.classList.add('out')}}c.append(h,info);
if(g.k=='chainmp'){const hn=document.createElement('div');hn.className='pgi';hn.textContent=T.chainhint;c.append(hn)}
const fx=document.createElement('div');fx.className='pr fix';fx.innerHTML='<label><span class="pn">🔒</span><span class="pt"><span></span><span class="ps f"></span></span></label>';
fx.querySelector('.pt span').textContent=g.f;fx.querySelector('.ps').textContent=T.always;if(g.f)c.append(fx);
const l=st[g.k];let n=g.k=='chain'?1:0;  // Blockkette: die Blockhöhe ist Zeile 1
if(g.k=='chain')fx.querySelector('.pn').textContent='1.';l.forEach((it,i)=>{const r=document.createElement('div');r.className='pr'+(it.on?'':' off');
const lab=document.createElement('label'),cb=document.createElement('input'),tx=document.createElement('span'),sp=document.createElement('span'),ps=document.createElement('span'),num=document.createElement('span');
num.className='pn';num.textContent=it.on?++n+'.':'';cb.type='checkbox';cb.checked=it.on;cb.onchange=()=>{it.on=cb.checked;draw();measure()};
tx.className='pt';sp.textContent=g.p.find(p=>p.k==it.k).t;ps.className='ps';tx.append(sp,ps);
const pi=g.p.findIndex(p=>p.k==it.k),s=it.on?status(gi,pi):null;if(!it.on){ps.classList.add('f');ps.textContent=T.off}else if(s){ps.classList.add(s[0]);ps.textContent=s[1]}
lab.append(num,cb,tx);r.append(lab);
[['▲',-1],['▼',1]].forEach(([s,d])=>{const b=document.createElement('button');b.type='button';b.textContent=s;b.title=d<0?T.up:T.down;
const j=i+d;b.disabled=j<0||j>=l.length;b.onclick=()=>{[l[i],l[j]]=[l[j],l[i]];draw();measure()};r.append(b)});c.append(r)});grid.append(c)})}
draw();measure();
document.getElementById('pform').onsubmit=()=>{document.getElementById('pval').value=val()};
document.getElementById('pdef').onclick=()=>{if(!confirm(T.defq))return;G.forEach(g=>{st[g.k]=g.p.map(p=>({k:p.k,on:true}))});draw();measure()};})();</script>)pt";

// Messung fuer die Seite "Inhalte": welche Teile im aktuellen Layout Platz finden (p = ungespeicherte Auswahl)
static void handlePartsApi() {
  if (!guard()) return;
  const bool tmp = server.hasArg("p");
  if (tmp) parseParts(server.arg("p"));
  static uint16_t shown[W_COUNT], have[W_COUNT];
  uint16_t chainSplit[2];
  measureParts(layout, shown, have, chainSplit);
  if (tmp) parseParts(cfg.parts);
  String j = String("{\"o\":\"") + (portrait() ? L("Hochformat", "portrait") : L("Querformat", "landscape")) +
             "\",\"cs\":[" + String(chainSplit[0]) + "," + String(chainSplit[1]) + "],\"w\":[";
  bool first = true;
  for (int i = 0; i < layout.n; ++i) {
    const Slot &s = layout.s[i];
    const int g = partGroupOf((WidgetId)s.id);
    if (g < 0) continue;
    if (!first) j += ',';
    first = false;
    j += String("{\"k\":\"") + WIDGETS[s.id].key + "\",\"t\":\"" + L(WIDGETS[s.id].de, WIDGETS[s.id].en) + "\",\"g\":" +
         String(g) + ",\"s\":" + String(shown[s.id]) + ",\"d\":" + String(have[s.id]) + ",\"c\":\"" + String(s.w) + "×" +
         String(s.h) + "\"}";
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", j);
}


static String periodOptions() {
  String s;
  for (const ChartPeriod &cp : CHART_PERIODS) s += opt(String(cp.days), L(cp.de, cp.en), cfg.chartDays == cp.days);
  return s;
}

static void handleContentPage() {
  if (!guard()) return;
  String b;
  // Kurs, Waehrungen, Preis-Chart
  b += String("<section><h2>") + L("Kurs und Währungen", "Price and currencies") +
       "</h2><form method=\"post\" action=\"/settings\"><input type=\"hidden\" name=\"back\" value=\"content\"><div class=\"row\">";
  String c1 = "<div><label>" + String(L("Hauptwährung", "Main currency")) + "</label><select name=\"cur1\">";
  String c2 = "<div><label>" + String(L("Zweitwährung", "Second currency")) + "</label><select name=\"cur2\">" +
              opt("none", L("keine", "none"), cfg.cur2 >= NUM_CUR);
  for (int i = 0; i < NUM_CUR; ++i) {
    String sym = CURRENCIES[i].sym;
    sym.replace("\x02", "₣");  // Franken-Zeichen: auf dem Display gezeichnet, im Browser als Unicode
    String label = String(CURRENCIES[i].code) + " (" + sym + ")";
    c1 += opt(String(i), label, cfg.cur1 == i);
    c2 += opt(String(i), label, cfg.cur2 == i);
  }
  b += c1 + "</select></div>" + c2 + "</select></div><div><label>" + L("Preis-Chart: Zeitraum", "Price chart: period") +
       "</label><select name=\"days\">" + periodOptions() +
       "</select></div></div><p class=\"hint\">" +
       L("Die Hauptwährung steht groß in der Kopfzeile und im Preis-Chart, Moscow Time zeigt beide. Ob der Kurs in "
         "der Kopfzeile erscheint, steht unter „Layout &amp; Farben“.",
         "The main currency is shown large in the header and in the price chart, Moscow Time shows both. Whether "
         "the price appears in the header is set under “Layout &amp; colors”.") +
       "</p><button>" + L("Speichern", "Save") + "</button></form></section>";
  // Miner
  b += String("<section><h2>Miner</h2><form method=\"post\" action=\"/settings\"><input type=\"hidden\" name=\"back\" "
              "value=\"content\"><label>") +
       L("Adressen der Miner im Heimnetz", "Addresses of miners on your network") +
       "</label><input type=\"text\" name=\"miners\" value=\"" + esc(cfg.miners) +
       "\" placeholder=\"192.168.1.50, bitaxe.local\" autocapitalize=\"off\" spellcheck=\"false\"><p class=\"hint\">" +
       L("Geräte mit AxeOS (Bitaxe, NerdQaxe …): IP-Adressen oder Namen, mit Komma getrennt, höchstens 4. "
         "„Miner (alle)“ zeigt alle zusammen, „Miner 1“ bis „Miner 4“ je ein Gerät (Reihenfolge wie hier).",
         "Devices running AxeOS (Bitaxe, NerdQaxe …): IP addresses or names, comma-separated, up to 4. "
         "“Miners (all)” shows all together, “Miner 1” to “Miner 4” one device each (order as entered here).") +
       "</p><button>" + L("Speichern", "Save") + "</button></form></section>";
  // Teile der Kacheln
  {
    String pt = FPSTR(PARTS_HTML), gj = "[";
    for (int g = 0; g < N_PART_GROUPS; ++g) {
      const PartGroup &pg = PART_GROUPS[g];
      if (g) gj += ',';
      gj += String("{\"k\":\"") + pg.key + "\",\"t\":\"" + L(pg.de, pg.en) + "\",\"f\":\"" + L(pg.fixDe, pg.fixEn) +
            "\",\"p\":[";
      for (int i = 0; i < pg.n; ++i)
        gj += String(i ? "," : "") + "{\"k\":\"" + pg.parts[i].key + "\",\"t\":\"" + L(pg.parts[i].de, pg.parts[i].en) + "\"}";
      gj += "]}";
    }
    gj += "]";
    pt.replace("%T_TITLE%", L("Inhalte der Kacheln", "Tile contents"));
    pt.replace("%T_HELP%",
               L("Was jede Kachel unter dem Hauptwert zeigt. Die Reihenfolge ist zugleich die Priorität: Ist eine "
                 "Kachel zu klein für alles, fallen die unteren Einträge zuerst weg. Abgewählte Einträge werden nie "
                 "angezeigt; der freie Platz geht an die übrigen. Mit 🔒 markierte Werte zeigt die Kachel immer.",
                 "What each tile shows below its main value. The order is also the priority: if a tile is too small "
                 "for everything, the entries at the bottom are dropped first. Unchecked entries are never shown; "
                 "their space goes to the others. Values marked 🔒 are always shown."));
    pt.replace("%TEXTS%",
               L("{\"legend\":\"Neben jedem Eintrag steht, ob er im aktuellen Layout zu sehen ist: grün = sichtbar, "
                 "orange = nur in einem Teil der Kacheln, rot = kein Platz (Kachel vergrößern oder Eintrag nach oben "
                 "schieben), grau = keine Daten oder ausgeblendet. Die Anzeige rechnet mit der Auswahl hier, auch "
                 "bevor sie gespeichert ist.\",\"yes\":\"✓ sichtbar\",\"chainhint\":\"Gleiche Nummer wie bei den geminten Blöcken = gleiche Höhe im Display.\",\"part\":\"◐ nur in\",\"noroom\":\"✗ kein "
                 "Platz\",\"nodata\":\"keine Daten\",\"off\":\"ausgeblendet\",\"always\":\"immer sichtbar\","
                 "\"inl\":\"Im Layout\",\"notin\":\"Nicht im aktuellen Layout\",\"up\":\"nach oben (wichtiger)\","
                 "\"down\":\"nach unten (weniger wichtig)\",\"defq\":\"Alle Kacheln auf die Standard-Inhalte "
                 "zurücksetzen? (Erst mit „Speichern“ übernommen.)\"}",
                 "{\"legend\":\"Each entry shows whether it is visible in the current layout: green = visible, "
                 "orange = only in some of the tiles, red = no room (enlarge the tile or move the entry up), "
                 "grey = no data or hidden. The check uses the selection here, even before it is saved.\","
                 "\"yes\":\"✓ visible\",\"chainhint\":\"Same number as for the mined blocks = same height on the display.\",\"part\":\"◐ only in\",\"noroom\":\"✗ no room\",\"nodata\":\"no "
                 "data\",\"off\":\"hidden\",\"always\":\"always visible\",\"inl\":\"In layout\",\"notin\":\"Not "
                 "in the current layout\",\"up\":\"up (more important)\",\"down\":\"down (less important)\","
                 "\"defq\":\"Reset all tiles to the default contents? (Applied when you save.)\"}"));
    pt.replace("%T_SAVE%", L("Speichern", "Save"));
    pt.replace("%T_DEF%", L("Standard", "Defaults"));
    pt.replace("%GROUPS%", gj);
    pt.replace("%CUR%", partsToString());
    b += pt;
  }
  // Ereignisse
  String ev = FPSTR(EVENTS_HTML);
  ev.replace("%T_TITLE%", L("Ereignisse für „Heute vor X Jahren“", "Events for “On this day”"));
  ev.replace("%T_HELP%", L("Eingebaute Ereignisse lassen sich ausblenden oder bearbeiten, eigene Ereignisse "
                           "(z.&nbsp;B. der Tag, an dem du deine ersten Sats gekauft hast) kommen dazu. Änderungen "
                           "werden sofort gespeichert und erscheinen spätestens nach einer Minute auf dem Display.",
                           "Built-in events can be hidden or edited, and you can add your own (e.g. the day you "
                           "bought your first sats). Changes are saved immediately and appear on the display "
                           "within a minute."));
  ev.replace("%T_SEARCH%", L("Suchen …", "Search …"));
  ev.replace("%T_ALL%", L("Alle", "All"));
  ev.replace("%T_ON%", L("Aktiv", "Active"));
  ev.replace("%T_OFF%", L("Ausgeblendet", "Hidden"));
  ev.replace("%T_OWNS%", L("Eigene", "Own"));
  ev.replace("%T_EDITS%", L("Geänderte", "Edited"));
  ev.replace("%T_CHRONO%", L("Chronologisch", "Chronological"));
  ev.replace("%T_CAL%", L("Nach Kalendertag", "By calendar day"));
  ev.replace("%T_ADD%", L("Eigenes Ereignis", "Own event"));
  ev.replace("%T_DATE%", L("Datum", "Date"));
  ev.replace("%T_FORMHINT%", L("Mindestens ein Text; fehlt eine Sprache, wird die andere angezeigt. Höchstens 90 Zeichen.",
                               "At least one text; if one language is missing, the other is shown. At most 90 characters."));
  ev.replace("%T_OK%", L("Übernehmen", "Apply"));
  ev.replace("%T_CANCEL%", L("Abbrechen", "Cancel"));
  ev.replace("%MAX%", "90");
  String js = FPSTR(EVENTS_JS);
  js.replace("%LANG%", lang == LANG_EN ? "en" : "de");
  js.replace("%TEXTS%",
             String("{\"active\":\"") + L("Auf dem Display zeigen", "Show on the display") + "\",\"own\":\"" +
                 L("eigenes", "own") + "\",\"edited\":\"" + L("geändert", "edited") + "\",\"edit\":\"" +
                 L("Bearbeiten", "Edit") + "\",\"add\":\"" + L("Eigenes Ereignis", "Own event") + "\",\"del\":\"" +
                 L("Löschen", "Delete") + "\",\"delq\":\"" + L("Dieses Ereignis löschen?", "Delete this event?") +
                 "\",\"reset\":\"" + L("Original wiederherstellen", "Restore original") + "\",\"nodate\":\"" +
                 L("Bitte ein Datum wählen.", "Please choose a date.") + "\",\"notext\":\"" +
                 L("Bitte einen Text eingeben.", "Please enter a text.") + "\",\"count\":\"" +
                 L("%1 von %2 angezeigt, %3 aktiv.", "%1 of %2 shown, %3 active.") + "\"}");
  b += ev + js;
  sendPage(b, 2);
}

static uint32_t eventsChangedMs = 0;  // Display nach Aenderungen leicht verzoegert neu zeichnen

static void handleEventsGet() {
  if (!guard()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", eventsJson());
}

static void handleEventsPost() {
  if (!guard()) return;
  String err;
  if (!eventsSave(server.arg("plain"), err)) return server.send(400, "text/plain; charset=utf-8", err);
  if (layout.has(W_HISTORY)) eventsChangedMs = millis() | 1;
  server.send(200, "text/plain; charset=utf-8", L("Gespeichert.", "Saved."));
}

static void handleStatusJson() {
  JsonDocument d;
  d["version"] = FW_VERSION;
  d["online"] = data.online;
  d["height"] = data.nBlocks ? data.blocks[0].height : 0;
  d["updated"] = (uint32_t)data.updatedAt;
  d["node"] = node.url;
  d["layout"] = activeLayoutText();
  d["rotation"] = cfg.rotation;
  d["lang"] = lang == LANG_EN ? "en" : "de";
  d["error"] = data.online ? "" : lastHttpError;
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  d["uptime"] = millis() / 1000;
  d["renders"] = renderCount;
  d["latest"] = latestFirmware();
  d["update"] = firmwareUpdateAvailable();
  d["fwBusy"] = fwInstallPending;
  d["fwErr"] = fwInstallError;
  d["pending"] = redrawPending();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void startWeb() {
  if (MDNS.begin(WIFI_HOSTNAME)) MDNS.addService("http", "tcp", 80);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/favicon.svg", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "image/svg+xml", BTC_SVG);
  });
  server.on("/layout", HTTP_GET, handleLayoutPage);
  server.on("/layout", HTTP_POST, handleLayoutSave);
  server.on("/preview.bmp", HTTP_GET, handlePreview);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/content", HTTP_GET, handleContentPage);
  server.on("/api/parts", HTTP_GET, handlePartsApi);
  server.on("/api/events", HTTP_GET, handleEventsGet);
  server.on("/api/events", HTTP_POST, handleEventsPost);
  server.on("/theme", HTTP_POST, handleTheme);
  server.on("/display", HTTP_POST, handleDisplay);
#ifdef TT_DEVTOOLS
  server.on("/test/overflow", HTTP_GET, [] {
    if (guard()) streamOverflowReport(server);
  });
#endif
  server.on("/node", HTTP_POST, handleNode);
  server.on("/password", HTTP_POST, handlePassword);
  server.on("/refresh", HTTP_POST, [] {
    if (!guard()) return;
    refreshRequested = true;
    flash(L("Display wird neu gezeichnet (ca. 30 s).", "Redrawing the display (about 30 s)."));
    redirect();
  });
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/fw-check", HTTP_POST, [] {
    if (!guard()) return;
    if (!checkFirmwareNow()) flash(L("GitHub ist gerade nicht erreichbar.", "GitHub is not reachable right now."), true);
    else if (firmwareUpdateAvailable())
      flash(String(L("Neue Version ", "New version ")) + latestFirmware() + L(" verfügbar.", " available."));
    else flash(L("Die Firmware ist aktuell.", "The firmware is up to date."));
    redirect("/#fw");
  });
  server.on("/fw-install", HTTP_POST, handleFwInstall);
  server.on("/api/wifi-scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi-state", HTTP_GET, handleWifiState);
  server.on("/wifi-connect", HTTP_POST, handleWifiConnect);
  server.on("/wifi-reset", HTTP_POST, [] {
    if (!guard()) return;
    sendPage(String("<div class=\"msg\">") +
             L("WLAN-Daten gelöscht. Das Gerät startet das Einrichtungs-WLAN »" WIFI_AP_NAME "«.",
               "Wi-Fi forgotten. The device starts the setup Wi-Fi »" WIFI_AP_NAME "«.") +
             "</div>");
    delay(800);
    wifiResetAndRestart();
  });
  server.on("/api/status", HTTP_GET, [] {
    if (guard()) handleStatusJson();
  });
  server.onNotFound([] { server.send(404, "text/plain", "404"); });
  server.begin();
  webStarted = true;
  Serial.printf("[web] http://%s.local  bzw.  http://%s\n", WIFI_HOSTNAME, WiFi.localIP().toString().c_str());
}

void pumpWeb() {
  if (!webStarted) return;
  server.handleClient();
  // nach Aenderungen an den Ereignissen eine Minute Ruhe abwarten, dann neu zeichnen
  if (eventsChangedMs && millis() - eventsChangedMs > 60000) {
    eventsChangedMs = 0;
    refreshRequested = true;
  }
}

// Einstellungen (NVS), Layout-Text und Adresse des mempool-Servers
#include "app.h"
#include <Preferences.h>
#include <MD5Builder.h>

Settings cfg;
Layout layout;
Node node;

// Mindest-, Hoechst- und Standardgroessen in Rasterfeldern. Die Hoechstgroesse wird zusaetzlich
// durch das Raster begrenzt (Querformat 20 x 12, Hochformat 12 x 20).
// 1 Spalte = ca. 39 px, 1 Zeile = ca. 34 px (jeweils inkl. 8 px Abstand).
const WidgetInfo WIDGETS[W_COUNT] = {
    {"block", "Letzter Block", "Latest block", 5, 2, 20, 20, 13, 6},
    {"moscow", "Moscow Time", "Moscow Time", 4, 2, 20, 20, 7, 3},
    {"fees", "Gebühren", "Fees", 5, 2, 20, 20, 7, 3},
    {"chain", "Blockkette", "Block chain", 6, 3, 20, 20, 20, 4},
    {"mempool", "Mempool", "Mempool", 3, 2, 20, 20, 4, 2},
    {"hashrate", "Hashrate", "Hashrate", 3, 2, 20, 20, 4, 2},
    {"difficulty", "Difficulty", "Difficulty", 3, 2, 20, 20, 4, 2},
    {"halving", "Halving", "Halving", 3, 2, 20, 20, 4, 2},
    {"lightning", "Lightning", "Lightning", 3, 2, 20, 20, 4, 2},
    {"chart", "Preis-Chart", "Price chart", 5, 3, 20, 20, 10, 4},
    {"miner", "Miner (alle)", "Miners (all)", 4, 2, 20, 20, 7, 3},
    {"miner1", "Miner 1", "Miner 1", 3, 2, 20, 20, 5, 3},
    {"miner2", "Miner 2", "Miner 2", 3, 2, 20, 20, 5, 3},
    {"miner3", "Miner 3", "Miner 3", 3, 2, 20, 20, 5, 3},
    {"miner4", "Miner 4", "Miner 4", 3, 2, 20, 20, 5, 3},
    {"history", "Heute vor X Jahren", "On this day", 5, 3, 20, 20, 10, 3},
};

const RoleInfo ROLES[C_ROLE_COUNT] = {
    {"bg", "Hintergrund", "Background", 'w'},
    {"text", "Text", "Text", 'k'},
    {"title", "Überschriften", "Headings", 'r'},
    {"bar", "Titelbalken", "Title bars", 'r'},
    {"bartext", "Text im Titelbalken", "Title bar text", 'w'},
    {"border", "Rahmen und Linien", "Borders and lines", 'k'},
    {"hl", "Hervorhebung (Gebühr hoch)", "Highlight (high fee)", 'y'},
    {"hltext", "Text auf Hervorhebung", "Text on highlight", 'k'},
    {"headbg", "Kopfzeile: Hintergrund", "Header: background", 'k'},
    {"headtext", "Kopfzeile: Text", "Header: text", 'w'},
    {"price", "Kopfzeile: Kurs", "Header: price", 'y'},
    {"logo", "Logo: Kreis", "Logo: circle", 'y'},
    {"logosym", "Logo: Zeichen", "Logo: symbol", 'w'},
    {"minedbg", "Geminte Blöcke", "Mined blocks", 'k'},
    {"minedtext", "Geminte Blöcke: Text", "Mined blocks: text", 'w'},
    {"minedh", "Geminte Blöcke: Höhe", "Mined blocks: height", 'y'},
    {"mpbg", "Mempool-Blöcke", "Mempool blocks", 'y'},
    {"mptext", "Mempool-Blöcke: Text", "Mempool blocks: text", 'k'},
    {"chartfill", "Chart: Fläche", "Chart: area", 'y'},
    {"chartline", "Chart: Linie", "Chart: line", 'k'},
    {"accent", "Akzent (Fortschritt)", "Accent (progress)", 'r'},
};

const char *DEFAULT_LAYOUT =
    "block:0,0,13,6;moscow:13,0,7,3;fees:13,3,7,3;chain:0,6,20,4;"
    "mempool:0,10,4,2;hashrate:4,10,4,2;difficulty:8,10,4,2;halving:12,10,4,2;lightning:16,10,4,2";

// Hochformat: 12 x 20 Felder
const char *DEFAULT_LAYOUT_P =
    "block:0,0,12,6;moscow:0,6,6,3;fees:6,6,6,3;chain:0,9,12,5;"
    "mempool:0,14,4,3;hashrate:4,14,4,3;difficulty:8,14,4,3;halving:0,17,6,3;lightning:6,17,6,3";

bool portrait() { return cfg.rotation & 1; }
int screenW() { return portrait() ? 480 : 800; }
int screenH() { return portrait() ? 800 : 480; }
int gridCols() { return portrait() ? 12 : 20; }
int gridRows() { return portrait() ? 20 : 12; }
String &activeLayoutText() { return portrait() ? cfg.layoutP : cfg.layout; }
const char *defaultLayoutText() { return portrait() ? DEFAULT_LAYOUT_P : DEFAULT_LAYOUT; }

String defaultTheme() {
  String t;
  for (int i = 0; i < C_ROLE_COUNT; ++i) t += ROLES[i].def;
  return t;
}

bool validTheme(const String &t) {
  if (t.length() != C_ROLE_COUNT) return false;
  for (size_t i = 0; i < t.length(); ++i)
    if (!strchr("kwyro", t[i])) return false;
  return true;
}

bool Layout::hasMiner() const {
  return has(W_MINER) || has(W_MINER1) || has(W_MINER2) || has(W_MINER3) || has(W_MINER4);
}

bool Layout::has(WidgetId id) const {
  for (int i = 0; i < n; ++i)
    if (s[i].id == id) return true;
  return false;
}

static int widgetByKey(const String &k) {
  for (int i = 0; i < W_COUNT; ++i)
    if (k == WIDGETS[i].key) return i;
  return -1;
}

bool parseLayout(const String &text, Layout &out, String *err) {
  Layout l;
  const int COLS = gridCols(), ROWS = gridRows();
  uint8_t used[GRID_MAX][GRID_MAX] = {};
  int start = 0;
  auto fail = [&](const String &m) {
    if (err) *err = m;
    return false;
  };
  while (start < (int)text.length()) {
    int end = text.indexOf(';', start);
    if (end < 0) end = text.length();
    String item = text.substring(start, end);
    start = end + 1;
    item.trim();
    if (!item.length()) continue;
    int colon = item.indexOf(':');
    if (colon < 0) return fail("Format: " + item);
    if (item.startsWith("qr:")) continue;  // frueherer Inhalt "QR-Code", entfallen
    int id = widgetByKey(item.substring(0, colon));
    if (id < 0) return fail(String(L("Unbekannter Inhalt: ", "Unknown widget: ")) + item.substring(0, colon));
    if (l.has((WidgetId)id)) return fail(String(L("Doppelt: ", "Duplicate: ")) + WIDGETS[id].key);
    int v[4], vi = 0, p = colon + 1;
    while (vi < 4 && p <= (int)item.length()) {
      int c = item.indexOf(',', p);
      if (c < 0) c = item.length();
      v[vi++] = item.substring(p, c).toInt();
      p = c + 1;
    }
    if (vi != 4) return fail("Format: " + item);
    const WidgetInfo &wi = WIDGETS[id];
    if (v[0] < 0 || v[1] < 0 || v[2] < wi.minW || v[3] < wi.minH || v[2] > wi.maxW || v[3] > wi.maxH ||
        v[0] + v[2] > COLS || v[1] + v[3] > ROWS)
      return fail(String(L("Größe/Position ungültig: ", "Invalid size/position: ")) + wi.key);
    for (int y = v[1]; y < v[1] + v[3]; ++y)
      for (int x = v[0]; x < v[0] + v[2]; ++x) {
        if (used[y][x]) return fail(String(L("Überlappung: ", "Overlap: ")) + wi.key);
        used[y][x] = 1;
      }
    l.s[l.n++] = {(uint8_t)id, (uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]};
  }
  out = l;
  return true;
}

String layoutToString(const Layout &l) {
  String s;
  for (int i = 0; i < l.n; ++i) {
    const Slot &sl = l.s[i];
    if (s.length()) s += ';';
    s += String(WIDGETS[sl.id].key) + ':' + sl.x + ',' + sl.y + ',' + sl.w + ',' + sl.h;
  }
  return s;
}

static bool isIpOrLocal(const String &h) {
  if (h.endsWith(".local")) return true;
  IPAddress ip;
  return ip.fromString(h);
}

// Akzeptiert "https://mempool.space", "http://umbrel.local:3006", "umbrel.local:3006",
// "192.168.2.10:3006/" oder ".../api". Ohne Schema: http fuer IPs/.local, sonst https.
bool parseNodeUrl(String in, Node &out) {
  in.trim();
  if (!in.length()) in = DEFAULT_NODE_URL;
  bool https;
  int p = in.indexOf("://");
  String rest;
  if (p >= 0) {
    String scheme = in.substring(0, p);
    scheme.toLowerCase();
    if (scheme == "https") https = true;
    else if (scheme == "http") https = false;
    else return false;
    rest = in.substring(p + 3);
  } else {
    rest = in;
    int c = rest.indexOf(':'), s = rest.indexOf('/');
    String h = rest.substring(0, c >= 0 ? c : (s >= 0 ? s : rest.length()));
    https = !isIpOrLocal(h);
  }
  while (rest.endsWith("/")) rest.remove(rest.length() - 1);
  if (rest.endsWith("/api")) rest.remove(rest.length() - 4);
  while (rest.endsWith("/")) rest.remove(rest.length() - 1);
  int slash = rest.indexOf('/');
  String hostPort = slash >= 0 ? rest.substring(0, slash) : rest;
  String base = slash >= 0 ? rest.substring(slash) : String("");
  int colon = hostPort.indexOf(':');
  String host = colon >= 0 ? hostPort.substring(0, colon) : hostPort;
  long port = colon >= 0 ? hostPort.substring(colon + 1).toInt() : (https ? 443 : 80);
  host.toLowerCase();
  if (!host.length() || host.indexOf(' ') >= 0 || port <= 0 || port > 65535) return false;
  out.https = https;
  out.host = host;
  out.port = (uint16_t)port;
  out.base = base;
  out.url = String(https ? "https://" : "http://") + host;
  if (port != (https ? 443 : 80)) out.url += ":" + String(port);
  out.url += base;
  return true;
}

String digestHA1(const String &password) {
  MD5Builder m;
  m.begin();
  m.add(String("admin:") + AUTH_REALM + ":" + password);
  m.calculate();
  return m.toString();
}

bool isHA1(const String &s) {
  if (s.length() != 32) return false;
  for (char c : s)
    if (!isdigit(c) && (c < 'a' || c > 'f')) return false;
  return true;
}

void loadSettings() {
  Preferences p;
  p.begin("btcterm", true);
  cfg.nodeUrl = p.getString("node", DEFAULT_NODE_URL);
  cfg.password = p.getString("pw", "");
  // bis 1.0.1 stand das Passwort im Klartext im NVS: jetzt nur noch HA1 (siehe unten)
  const bool migratePw = cfg.password.length() && !isHA1(cfg.password);
  if (migratePw) cfg.password = digestHA1(cfg.password);
  cfg.layout = p.getString("layout", DEFAULT_LAYOUT);
  cfg.layoutP = p.getString("layoutP", DEFAULT_LAYOUT_P);
  cfg.rotation = p.getUChar("rot", 0) & 3;
  cfg.headerQr = p.getBool("hdrQr", false);
  cfg.miners = p.getString("miners", "");
  cfg.chartDays = p.getUShort("chartDays2", p.getUChar("chartDays", 7));
  cfg.theme = p.getString("theme", "");
  cfg.parts = p.getString("parts", "");
  cfg.cur1 = p.getUChar("cur1", 0);
  cfg.cur2 = p.getUChar("cur2", 1);
  cfg.headerPrice = p.getBool("hdrPrice", true);
  lang = (Lang)p.getUChar("lang", LANG_DE);
  p.end();
  if (migratePw) {
    Preferences w;
    w.begin("btcterm", false);
    w.putString("pw", cfg.password);
    w.end();
    Serial.println("[cfg] Passwort in HA1-Wert umgewandelt");
  }
  if (!parseNodeUrl(cfg.nodeUrl, node)) parseNodeUrl(DEFAULT_NODE_URL, node);
  if (!parseLayout(activeLayoutText(), layout)) {
    activeLayoutText() = defaultLayoutText();
    parseLayout(activeLayoutText(), layout);
  }
  activeLayoutText() = layoutToString(layout);  // entfallene Inhalte (z.B. "qr") aus dem Text entfernen
  if (!chartPeriod(cfg.chartDays)) cfg.chartDays = 7;
  if (!validTheme(cfg.theme)) cfg.theme = defaultTheme();
  parseParts(cfg.parts);
  cfg.parts = partsToString();
  if (cfg.cur1 >= NUM_CUR) cfg.cur1 = 0;
  if (cfg.cur2 >= NUM_CUR || cfg.cur2 == cfg.cur1) cfg.cur2 = CUR_NONE;
}

void saveSettings() {
  Preferences p;
  p.begin("btcterm", false);
  p.putString("node", cfg.nodeUrl);
  p.putString("pw", cfg.password);
  p.putString("layout", cfg.layout);
  p.putString("layoutP", cfg.layoutP);
  p.putUChar("rot", cfg.rotation);
  p.putBool("hdrQr", cfg.headerQr);
  p.putString("miners", cfg.miners);
  p.putUShort("chartDays2", cfg.chartDays);
  p.putString("theme", cfg.theme);
  p.putString("parts", cfg.parts);
  p.putUChar("cur1", cfg.cur1);
  p.putUChar("cur2", cfg.cur2);
  p.putBool("hdrPrice", cfg.headerPrice);
  p.putUChar("lang", lang);
  p.end();
}

void resetPasswordAndWifi() {
  Preferences p;
  p.begin("btcterm", false);
  p.remove("pw");
  p.end();
  cfg.password = "";
}

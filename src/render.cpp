// Zeichnen: Kopfzeile, Widgets im Raster, Info-Bildschirme, Vorschau als BMP, Pruefstand
#include "app.h"
#include <GxEPD2_4C.h>
#include <epd4c/GxEPD2_750c_GDEM075F52.h>
#include "qrcode.h"  // QR-Code-Erzeugung aus dem ESP-IDF (libqrcode)
#include "btc_logo.h"

static const uint16_t BLK = GxEPD_BLACK;
static const uint16_t WHT = GxEPD_WHITE;
static const uint16_t RED = GxEPD_RED;
static const uint16_t YEL = GxEPD_YELLOW;
// Das Panel kennt kein Orange. ORG wird beim Zeichnen pixelweise als Schachbrett
// aus Rot und Gelb gemischt (Dithering) - fuer Flaechen gedacht, nicht fuer Text.
static const uint16_t ORG = 0xFC00;

// 800 x 48 Pixel pro Seite = 9,6 KB Puffer (der ESP32 hat kein PSRAM).
using PanelBase = GxEPD2_4C<GxEPD2_750c_GDEM075F52, GxEPD2_750c_GDEM075F52::HEIGHT / 10>;
class Panel : public PanelBase {
 public:
  using PanelBase::PanelBase;
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (color == ORG) color = ((x + y) & 1) ? RED : YEL;
    PanelBase::drawPixel(x, y, color);
  }
};
static Panel display(GxEPD2_750c_GDEM075F52(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2;
// Bildschirmgroesse je nach Ausrichtung (Querformat 800 x 480, Hochformat 480 x 800)
#define W (screenW())
#define H (screenH())

// Vorschau: zeichnet in einen Streifen von 800 x 48 Pixeln (2 Bit pro Pixel),
// genau wie GxEPD2 seitenweise. Daraus wird ein BMP fuer den Browser.
class BandCanvas : public Adafruit_GFX {
 public:
  static const int BUF = 800 * 48 / 4;
  int BW = 800, BH = 48;  // Querformat 800 x 48, Hochformat 480 x 80 (mit grossem Puffer mehr)
  int16_t y0 = 0;
  uint8_t fixed[BUF];
  uint8_t *buf = fixed;   // fuer die Vorschau zeitweise ein groesserer Puffer
  size_t cap = BUF;
  BandCanvas() : Adafruit_GFX(800, 480) {}
  static uint8_t idx(uint16_t c, int16_t x, int16_t y) {
    if (c == ORG) c = ((x + y) & 1) ? RED : YEL;
    if (c == BLK) return 0;
    if (c == WHT) return 1;
    if (c == YEL) return 2;
    if (c == RED) return 3;
    return 1;
  }
  inline void put(int x, int y, uint8_t v) {
    int i = (y - y0) * BW + x;
    uint8_t sh = (3 - (i & 3)) * 2;
    buf[i >> 2] = (buf[i >> 2] & ~(3 << sh)) | (v << sh);
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || x >= BW || y < y0 || y >= y0 + BH) return;
    put(x, y, idx(c, x, y));
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int ya = max<int>(y, y0), yb = min<int>(y + h, y0 + BH);
    int xa = max<int>(x, 0), xb = min<int>(x + w, BW);
    if (c == ORG) {
      for (int yy = ya; yy < yb; ++yy)
        for (int xx = xa; xx < xb; ++xx) put(xx, yy, idx(c, xx, yy));
      return;
    }
    const uint8_t v = idx(c, 0, 0);
    for (int yy = ya; yy < yb; ++yy)
      for (int xx = xa; xx < xb; ++xx) put(xx, yy, v);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
  void fillScreen(uint16_t c) override {
    if (c == ORG) return fillRect(0, y0, BW, BH, c);
    memset(buf, idx(c, 0, 0) * 0x55, (size_t)BW * BH / 4);
  }
  void setWidth(int w) {
    BW = w;
    BH = cap * 4 / w;
  }
  uint8_t get(int x, int row) const {
    int i = row * BW + x;
    return (buf[i >> 2] >> ((3 - (i & 3)) * 2)) & 3;
  }
};
static BandCanvas canvas;

// Pruefstand: zeichnet nichts, zaehlt aber jeden Pixel ausserhalb des Widget-Rechtecks
static String lastText;  // zuletzt gezeichneter Text (fuer die Meldung)
class CheckCanvas : public Adafruit_GFX {
 public:
  int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
  uint32_t outside = 0;
  String sample;
  CheckCanvas() : Adafruit_GFX(800, 480) {}
  void hit(int x, int y) {
    ++outside;
    if (!sample.length()) sample = "(" + String(x) + "," + String(y) + ") '" + lastText + "'";
  }
  void drawPixel(int16_t x, int16_t y, uint16_t) override {
    if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) hit(x, y);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t) override {
    if (w <= 0 || h <= 0) return;
    if (x < cx0 || y < cy0 || x + w > cx1 || y + h > cy1) {
      int ix = max<int>(0, min<int>(x + w, cx1) - max<int>(x, cx0));
      int iy = max<int>(0, min<int>(y + h, cy1) - max<int>(y, cy0));
      outside += (uint32_t)w * h - (uint32_t)ix * iy;
      if (!sample.length()) sample = "rect(" + String(x) + "," + String(y) + "," + String(w) + "," + String(h) + ")";
    }
  }
  void fillScreen(uint16_t) override {}
};

static Adafruit_GFX *gfx = &display;  // aktuelles Ziel (Panel, Vorschau oder Pruefstand)

// Welche Teile (siehe parts.cpp) jedes Widget tatsaechlich gezeigt hat (fuer die Seite "Inhalte")
static int curWidget = -1;
static uint16_t shownMask[W_COUNT];
static uint16_t dataMask[W_COUNT];  // Teile, fuer die es ueberhaupt Daten gibt
static uint16_t chainShown[2];      // Blockkette getrennt: [0] geminte, [1] Mempool-Bloecke
static void markPart(int part) {
  if (curWidget >= 0 && part >= 0 && part < 16) shownMask[curWidget] |= 1 << part;
}
static void noData(int part = -1) {  // -1 = gar keine Daten
  if (curWidget < 0) return;
  if (part < 0) dataMask[curWidget] = 0;
  else if (part < 16) dataMask[curWidget] &= ~(1 << part);
}
bool epdActive = false;
void (*epdBusyHook)() = nullptr;
String themeOverride;

// Kuerzungen zaehlen (Pruefstand): "ok" = erlaubt, z.B. bei frei waehlbaren Namen
static uint16_t truncCount = 0, truncOkCount = 0;
static String truncSample;

static void onEpdBusy(const void *) {
  if (epdBusyHook) epdBusyHook();
  delay(2);
}

static void bindU8g2(Adafruit_GFX &g) {
  u8g2.begin(g);
  u8g2.setFontMode(1);  // 1 = transparent; u8g2_SetFont setzt das bei jedem Schriftwechsel zurueck!
  u8g2.setFontDirection(0);
}

void displayInit() {
  display.init(115200, true, 10, false);
  display.setRotation(0);
  display.epd2.setBusyCallback(onEpdBusy);
  bindU8g2(display);
}

// ---------------------------------------------------------------------------
// Farben aus dem Farbschema
// ---------------------------------------------------------------------------
static uint16_t panelColor(char c) {
  switch (c) {
    case 'k': return BLK;
    case 'y': return YEL;
    case 'r': return RED;
    case 'o': return ORG;
    default: return WHT;
  }
}
static uint16_t col(ColorRole r) {
  const String &t = themeOverride.length() ? themeOverride : cfg.theme;
  return panelColor(r < (int)t.length() ? t[r] : ROLES[r].def);
}

// ---------------------------------------------------------------------------
// Text mit Sats-Symbol: das Zeichen '\x01' im Text wird als Sats-Symbol gezeichnet
// ---------------------------------------------------------------------------
enum Align { LEFT, CENTER, RIGHT };
static const char SATS = '\x01';
#define F_(name) u8g2_font_##name

static const char FRANC = '\x02';  // Franken-Zeichen ₣ (fehlt in den Schriften)

static int satsAdvance(int a) { return max(5, (a * 6 + 5) / 10) + 4; }
static int francAdvance(int a) { return max(6, (a * 7 + 5) / 10) + 2; }
static int symAdvance(char c, int a) { return c == SATS ? satsAdvance(a) : francAdvance(a); }

// Sats-Symbol (drei waagerechte Balken, senkrechter Strich mittig), Hoehe = Grossbuchstabe
static void drawSats(int x, int y, int a, uint16_t c) {
  int w = max(5, (a * 6 + 5) / 10);
  int t = a >= 20 ? 2 : 1;
  x += 1;
  int top = y - a;
  gfx->fillRect(x + w / 2 - t / 2, top, t, a, c);
  for (int i = 0; i < 3; ++i) {
    int by = top + (a * (2 + 3 * i) + 5) / 10;
    gfx->fillRect(x, by - t / 2, w, t, c);
  }
}

// Franken-Zeichen ₣: ein F mit zusaetzlichem Querstrich durch den unteren Teil des Stamms
static void drawFranc(int x, int y, int a, uint16_t c) {
  int w = max(6, (a * 7 + 5) / 10);
  int t = max(1, (a + 6) / 9);
  int top = y - a;
  int sx = x + max(2, w / 4);               // Stamm, links Platz fuer den unteren Querstrich
  gfx->fillRect(sx, top, t, a, c);          // Stamm
  gfx->fillRect(sx, top, x + w - sx, t, c); // oberer Balken
  int my = top + (a * 45 + 50) / 100;
  gfx->fillRect(sx, my, (x + w - sx) * 3 / 4, t, c);  // mittlerer Balken
  int ly = top + (a * 75 + 50) / 100;
  gfx->fillRect(x, ly, (sx - x) * 2 + t, t, c);       // unterer Querstrich durch den Stamm
}

static void drawSym(char ch, int x, int y, int a, uint16_t c) {
  if (ch == SATS) drawSats(x, y, a, c);
  else drawFranc(x, y, a, c);
}

// naechstes Sonderzeichen (Sats oder Franken) ab start, -1 wenn keins
static int nextSym(const String &s, int start) {
  for (int i = start; i < (int)s.length(); ++i)
    if (s[i] == SATS || s[i] == FRANC) return i;
  return -1;
}

static int textWidth(const String &s, const uint8_t *font) {
  u8g2.setFont(font);
  int a = u8g2.getFontAscent();
  int w = 0, start = 0;
  for (;;) {
    int p = nextSym(s, start);
    String seg = s.substring(start, p < 0 ? s.length() : p);
    if (seg.length()) w += u8g2.getUTF8Width(seg.c_str());
    if (p < 0) break;
    w += symAdvance(s[p], a);
    start = p + 1;
  }
  return w;
}

static void text(int x, int y, const String &s, const uint8_t *font, uint16_t fg, Align al = LEFT) {
  lastText = s;
  int w = textWidth(s, font);  // setzt auch die Schrift
  if (al == CENTER) x -= w / 2;
  if (al == RIGHT) x -= w;
  // u8g2_SetFont schaltet bei jedem Schriftwechsel auf "Hintergrund zeichnen" zurueck,
  // deshalb hier jedes Mal wieder transparent setzen
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(fg);
  int a = u8g2.getFontAscent();
  int start = 0;
  for (;;) {
    int p = nextSym(s, start);
    String seg = s.substring(start, p < 0 ? s.length() : p);
    if (seg.length()) {
      u8g2.drawUTF8(x, y, seg.c_str());
      x += u8g2.getUTF8Width(seg.c_str());
    }
    if (p < 0) break;
    drawSym(s[p], x, y, a, fg);
    x += symAdvance(s[p], a);
    start = p + 1;
  }
}

// Kuerzt mit "..", bis der Text passt (UTF-8-sicher). allowed: bei frei waehlbaren Namen ok
static String truncate(String s, const uint8_t *font, int maxW, bool allowed = false) {
  if (textWidth(s, font) <= maxW) return s;
  if (allowed) ++truncOkCount;
  else {
    ++truncCount;
    if (!truncSample.length()) truncSample = s;
  }
  while (s.length() > 1) {
    int cut = s.length() - 1;
    while (cut > 0 && (s[cut] & 0xC0) == 0x80) --cut;
    s.remove(cut);
    if (textWidth(s + "..", font) <= maxW) return s + "..";
  }
  return s;
}

// Erste Variante, die passt; sonst die letzte gekuerzt
static String pick(std::initializer_list<String> variants, const uint8_t *font, int maxW, bool allowed = false) {
  String last;
  for (const String &v : variants) {
    last = v;
    if (textWidth(v, font) <= maxW) return v;
  }
  return truncate(last, font, maxW, allowed);
}

// Groesste Schrift, in die der Text passt (sonst die kleinste)
static const uint8_t *fitFont(const String &s, int maxW, std::initializer_list<const uint8_t *> fonts) {
  const uint8_t *last = nullptr;
  for (const uint8_t *f : fonts) {
    last = f;
    if (textWidth(s, f) <= maxW) return f;
  }
  return last;
}

static String feeSpan(float lo, float hi, bool sym) {
  return fmtFee(lo) + " - " + fmtFee(hi) + (sym ? String(" ") + SATS + "/vB" : String(" sat/vB"));
}

// ---------------------------------------------------------------------------
// QR-Code (ESP-IDF qrcode): wird einmal pro Text erzeugt und zwischengespeichert
// ---------------------------------------------------------------------------
static const int QR_MAX = 37;  // Version 5
static uint8_t qrMod[QR_MAX][QR_MAX];
static int qrSize = 0;
static String qrText;

static void qrCapture(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  if (n > QR_MAX) return;
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) qrMod[y][x] = esp_qrcode_get_module(q, x, y) ? 1 : 0;
  qrSize = n;
}

static bool ensureQr(const String &t) {
  if (qrSize && t == qrText) return true;
  qrSize = 0;
  esp_qrcode_config_t c = {qrCapture, 5, ESP_QRCODE_ECC_LOW};
  if (esp_qrcode_generate(&c, t.c_str()) != ESP_OK || !qrSize) return false;
  qrText = t;
  return true;
}

// Seitenlaenge in Modulen inkl. Ruhezone (2 Module je Seite)
static int qrModules() { return qrSize + 4; }

// Immer Schwarz auf Weiss, unabhaengig vom Farbschema: invertierte Codes lesen viele Handys nicht
static void drawQr(int x, int y, int scale) {
  const uint16_t dark = BLK, light = WHT;
  int n = qrModules();
  gfx->fillRect(x, y, n * scale, n * scale, light);
  for (int my = 0; my < qrSize; ++my)
    for (int mx = 0; mx < qrSize; ++mx)
      if (qrMod[my][mx]) gfx->fillRect(x + (mx + 2) * scale, y + (my + 2) * scale, scale, scale, dark);
}

String deviceUrl() { return "http://" + WiFi.localIP().toString(); }

// ---------------------------------------------------------------------------
// Grundelemente
// ---------------------------------------------------------------------------
static void drawBitcoinLogo(int cx, int cy) {
  const int x0 = cx - BTC_LOGO_W / 2, y0 = cy - BTC_LOGO_H / 2;
  const uint16_t sym = col(C_LOGO_SYM), circ = col(C_LOGO);
  for (int y = 0; y < BTC_LOGO_H; ++y)
    for (int x = 0; x < BTC_LOGO_W; ++x) {
      uint8_t c = BTC_LOGO[y * BTC_LOGO_W + x];
      if (c == 1) gfx->drawPixel(x0 + x, y0 + y, sym);
      else if (c == 2) gfx->drawPixel(x0 + x, y0 + y, circ);
    }
}

// Kasten mit Titelbalken und doppeltem Rahmen. Gibt die Hoehe des Balkens zurueck.
static int drawBox(int x, int y, int w, int h, const String &title, const String &unit,
                   bool titleIsName = false) {
  const bool big = h >= 80;
  const int bar = big ? 24 : 20;
  const uint8_t *tf = big ? F_(helvB12_te) : F_(helvB10_te);
  const int base = y + (big ? 17 : 15);
  gfx->fillRect(x, y, w, bar, col(C_BAR));
  String t = truncate(title, tf, w - 20, titleIsName);
  text(x + 10, base, t, tf, col(C_BAR_TEXT));
  if (unit.length() && textWidth(t, tf) + 24 + textWidth(unit, F_(helvR10_te)) <= w - 10)
    text(x + w - 10, base, unit, F_(helvR10_te), col(C_BAR_TEXT), RIGHT);
  gfx->drawRect(x, y, w, h, col(C_BORDER));
  gfx->drawRect(x + 1, y + 1, w - 2, h - 2, col(C_BORDER));
  return bar;
}

// Hinweis mittig in einem Bereich, bei Bedarf zweizeilig
static void note(int x, int y, int w, int h, const String &msg) {
  const uint8_t *f = fitFont(msg, w - 16, {F_(helvR12_te), F_(helvR10_te), F_(helvR08_te)});
  if (textWidth(msg, f) <= w - 16 || msg.indexOf(' ') < 0) {
    text(x + w / 2, y + h / 2 + 5, truncate(msg, f, w - 16), f, col(C_TEXT), CENTER);
    return;
  }
  int sp = msg.lastIndexOf(' ', msg.length() / 2 + 4);
  if (sp < 0) sp = msg.indexOf(' ');
  String a = msg.substring(0, sp), b = msg.substring(sp + 1);
  f = F_(helvR10_te);
  text(x + w / 2, y + h / 2 - 3, truncate(a, f, w - 16), f, col(C_TEXT), CENTER);
  text(x + w / 2, y + h / 2 + 12, truncate(b, f, w - 16), f, col(C_TEXT), CENTER);
}

// ---------------------------------------------------------------------------
// Kopfzeile (fest, 62 px)
// ---------------------------------------------------------------------------
static void drawHeader() {
  gfx->fillRect(0, 0, W, 62, col(C_HEAD_BG));
  drawBitcoinLogo(34, 31);
  // ganz rechts optional ein kleiner QR-Code zur Geraete-Webseite
  int right = W - 16;
  if (cfg.headerQr && WiFi.status() == WL_CONNECTED && ensureQr(deviceUrl())) {
    int s = max(1, 58 / qrModules()), q = qrModules() * s;
    int qx = W - 4 - q;
    drawQr(qx, (62 - q) / 2, s);
    right = qx - 12;
  }
  // Kurs rechts: Hauptwaehrung gross, Zweitwaehrung klein darunter
  int priceLeft = right;
  if (cfg.headerPrice && data.priceOk) {
    String p1 = fmtPrice(data.prices[cfg.cur1], cfg.cur1);
    bool two = cfg.cur2 < NUM_CUR && data.prices[cfg.cur2];
    String p2 = two ? fmtPrice(data.prices[cfg.cur2], cfg.cur2) : String("");
    const uint8_t *f1 = fitFont(p1, portrait() ? 170 : 300, {F_(helvB24_te), F_(helvB18_te), F_(helvB14_te)});
    int w1 = textWidth(p1, f1), w2 = two ? textWidth(p2, F_(helvB12_te)) : 0;
    text(right, two ? 34 : 43, p1, f1, col(C_PRICE), RIGHT);
    if (two) text(right, 54, p2, F_(helvB12_te), col(C_HEAD_TEXT), RIGHT);
    priceLeft = right - max(w1, w2);
  }
  const int room = priceLeft - 16 - 68;
  const String title = room >= 210 ? "TIMECHAIN TERMINAL" : "TIMECHAIN";
  text(68, 32, title, fitFont(title, room, {F_(helvB18_te), F_(helvB14_te), F_(helvB10_te)}), col(C_HEAD_TEXT));
  if (data.online && data.updatedAt) {
    String when = fmtDate(data.updatedAt, true) + ", " + fmtTime(data.updatedAt, "%H:%M");
    String sub = lang == LANG_EN ? "Updated " + when : "Stand " + when + " Uhr";
    String full = sub + "  ·  " + node.host;
    const uint8_t *sf = textWidth(full, F_(helvR10_te)) <= room ? F_(helvR10_te) : F_(helvR08_te);
    if (textWidth(full, sf) > room) full = fmtTime(data.updatedAt, "%H:%M") + "  ·  " + node.host;
    text(68, 53, truncate(full, sf, room, true), sf, col(C_HEAD_TEXT));
  } else {
    String s = data.updatedAt ? String(L("OFFLINE - letzte Daten von ", "OFFLINE - last data from ")) +
                                    fmtDate(data.updatedAt, false) + " " + fmtTime(data.updatedAt, "%H:%M")
                              : String(L("OFFLINE - noch keine Daten", "OFFLINE - no data yet"));
    text(68, 53, truncate(s, F_(helvB10_te), room), F_(helvB10_te), col(C_PRICE));
  }
}

// ---------------------------------------------------------------------------
// Widget: letzter Block
// ---------------------------------------------------------------------------
struct BlockPreset {
  const uint8_t *num;
  int numH;
  const uint8_t *line, *lineB;
  int pitch, gap;
};
static const BlockPreset BLOCK_PRESETS[] = {
    {F_(logisoso92_tn), 92, F_(helvR18_te), F_(helvB18_te), 31, 44},
    {F_(logisoso78_tn), 78, F_(helvR18_te), F_(helvB18_te), 30, 40},
    {F_(logisoso62_tn), 62, F_(helvR14_te), F_(helvB14_te), 25, 33},
    {F_(logisoso50_tn), 50, F_(helvR14_te), F_(helvB14_te), 24, 31},
    {F_(logisoso42_tn), 42, F_(helvR12_te), F_(helvB12_te), 21, 27},
    {F_(logisoso32_tn), 32, F_(helvR12_te), F_(helvB12_te), 20, 25},
    {F_(logisoso24_tn), 24, F_(helvR10_te), F_(helvB10_te), 17, 21},
};
static const int N_PRESETS = sizeof BLOCK_PRESETS / sizeof BLOCK_PRESETS[0];

static int linesFitting(const BlockPreset &p, int h) {
  if (21 + p.numH + 3 > h) return -1;
  int k = 0;
  while (k < 6 && 21 + p.numH + p.gap + k * p.pitch + 4 <= h) ++k;
  return k;
}

// Fuehrende Nullen des Hashs (werden als "19×0" zusammengefasst)
static int hashZeros(const char *h) {
  int z = 0;
  while (z < 64 && h[z] == '0') ++z;
  return z;
}

// Hash als Raster 4 x 16 Zeichen in Festbreitenschrift
static const int HASH_GAP = 22;
static int hashGridW(const BlockPreset &p) { return p.numH >= 78 ? 16 * 9 : p.numH >= 58 ? 16 * 7 : 16 * 6; }

// erste Variante aus "a|b|c", die in maxW passt ("" = keine)
static String firstFit(const String &variants, const uint8_t *f, int maxW) {
  int start = 0;
  while (start <= (int)variants.length()) {
    int bar = variants.indexOf('|', start);
    if (bar < 0) bar = variants.length();
    String v = variants.substring(start, bar);
    if (v.length() && textWidth(v, f) <= maxW) return v;
    start = bar + 1;
  }
  return "";
}

// Zeilen unter der Blockhoehe aus den eingestellten Teilen: Alter fett in eigener Zeile, Hash als
// eigene Zeile (oder im Raster daneben), die uebrigen Teile nach Moeglichkeit zusammengefasst ("a · b")
struct BLine { String s; uint8_t kind; };  // 0 normal, 1 fett, 2 Hash
static int blockLinesWith(const BlockPreset &p, int w, bool hashGrid, int maxLines, BLine *out, int *placed, bool eager,
                          uint16_t *mask) {
  const BlockInfo &b = data.blocks[0];
  uint8_t ord[8];
  const int np = enabledParts(W_BLOCK, ord, 8);
  int nl = 0, done = 0;
  uint16_t m = 0;
  const String sep = "  ·  ";
  for (int j = 0; j < np; ++j) {
    const int part = ord[j];
    if (part == 5) {  // Hash
      if (strlen(b.hash) != 64) {
        noData(5);
        continue;
      }
      if (hashGrid) {
        ++done, m |= 1 << part;
        continue;
      }
      if (nl >= maxLines) break;
      out[nl++] = {"", 2};
      ++done, m |= 1 << part;
      continue;
    }
    if (part == 0) {  // Alter
      if (nl >= maxLines) break;
      String when = fmtTime((time_t)b.ts, lang == LANG_EN ? "%H:%M" : "%H:%M Uhr");
      String age = fmtAge(b.ts);
      out[nl++] = {pick({age.length() ? age + "  (" + when + ")" : when, age, fmtAgeShort(b.ts)}, p.lineB, w), 1};
      ++done, m |= 1 << part;
      continue;
    }
    String v;
    switch (part) {
      case 1: v = String("Miner: ") + b.pool + "|" + b.pool; break;
      case 2: {
        String mb = fmtDec(b.size / 1e6, 2) + " MB", tx = fmtInt(b.txCount);
        v = tx + L(" Transaktionen", " transactions") + sep + mb + "|" + tx + " Tx" + sep + mb + "|" + tx + " Tx";
        break;
      }
      case 3: {
        String span = feeSpan(b.feeMin, b.feeMax, true);
        v = String(L("Gebühren ", "Fees ")) + span + "|" + span + "|" + fmtFee(b.feeMin) + "-" + fmtFee(b.feeMax) + " " +
            SATS + "/vB";
        break;
      }
      case 4: {
        String rw = fmtDec(b.reward / 1e8, 3) + " BTC";
        v = String(L("Belohnung ", "Reward ")) + rw + "|" + rw;
        break;
      }
    }
    // an die vorige Zeile anhaengen: in der langen Form, solange noch Zeilen frei sind, sonst auch kuerzer
    if (nl && out[nl - 1].kind == 0) {
      const int room = w - textWidth(out[nl - 1].s + sep, p.line);
      int bar = v.indexOf('|');
      String j2 = (nl < maxLines && !eager) ? firstFit(bar < 0 ? v : v.substring(0, bar), p.line, room) : firstFit(v, p.line, room);
      if (j2.length()) {
        out[nl - 1].s += sep + j2;
        ++done, m |= 1 << part;
        continue;
      }
    }
    if (nl >= maxLines) break;
    String s = firstFit(v, p.line, w);
    if (!s.length() && part == 1) s = truncate(b.pool, p.line, w, true);  // Namen duerfen gekuerzt werden
    if (!s.length()) continue;
    out[nl++] = {s, 0};
    ++done, m |= 1 << part;
  }
  if (placed) *placed = done;
  if (mask) *mask = m;
  return nl;
}

// beide Arten probieren: lange Formen auf eigenen Zeilen (schoener) oder dicht gepackt (mehr Inhalt)
static int blockLines(const BlockPreset &p, int w, bool hashGrid, int maxLines, BLine *out, int *placed,
                      uint16_t *mask) {
  BLine alt[6];
  int pa = 0, pb = 0;
  uint16_t ma = 0, mb = 0;
  int na = blockLinesWith(p, w, hashGrid, maxLines, out, &pa, false, &ma);
  int nb = blockLinesWith(p, w, hashGrid, maxLines, alt, &pb, true, &mb);
  if (pb > pa) {
    for (int i = 0; i < nb; ++i) out[i] = alt[i];
    na = nb, pa = pb, ma = mb;
  }
  if (placed) *placed = pa;
  if (mask) *mask = ma;
  return na;
}

static void wBlock(int x, int y, int w, int h) {
  text(x, y + 13, L("LETZTER BLOCK", "LATEST BLOCK"), F_(helvB12_te), col(C_TITLE));
  if (!data.nBlocks) {
    noData();
    text(x, y + 21 + 24, "-", F_(logisoso24_tn), col(C_TEXT));
    return;
  }
  const BlockInfo &b = data.blocks[0];
  const String num = fmtInt(b.height);
  const bool hashOn = strlen(b.hash) == 64 && partEnabled(W_BLOCK, 5);
  uint8_t ordTmp[8];
  const int total = enabledParts(W_BLOCK, ordTmp, 8) - (strlen(b.hash) == 64 ? 0 : (partEnabled(W_BLOCK, 5) ? 1 : 0));

  // Groesste Darstellung, in der alle Teile Platz finden: zuerst mit Hash-Raster neben der
  // Blockhoehe, dann mit Hash als Zeile; sonst die, die die meisten Teile zeigt
  BLine lines[6], tryL[6];
  int best = -1, nl = 0, bestPlaced = -1;
  uint16_t bestMask = 0;
  bool bestGrid = false;
  auto attempt = [&](int i, bool grid) {
    const BlockPreset &p = BLOCK_PRESETS[i];
    int k = linesFitting(p, h);
    if (k < 0) return -1;
    int placed = 0;
    uint16_t m = 0;
    int n = blockLines(p, w, grid, k, tryL, &placed, &m);
    if (placed > bestPlaced) {
      bestPlaced = placed, best = i, bestGrid = grid, nl = n, bestMask = m;
      for (int j = 0; j < n; ++j) lines[j] = tryL[j];
    }
    return placed;
  };
  bool found = false;
  for (int i = 0; hashOn && i < N_PRESETS && !found; ++i) {
    const BlockPreset &p = BLOCK_PRESETS[i];
    if (p.numH >= 50 && textWidth(num, p.num) + HASH_GAP + hashGridW(p) <= w && linesFitting(p, h) >= 0) {
      bestPlaced = -1;
      found = attempt(i, true) >= total;
    }
  }
  for (int i = 0; i < N_PRESETS && !found; ++i)
    if (textWidth(num, BLOCK_PRESETS[i].num) <= w && linesFitting(BLOCK_PRESETS[i], h) >= 0) {
      bestPlaced = -1;
      found = attempt(i, false) >= total;
    }
  if (!found) {  // nicht alles passt: die Groesse mit den meisten Teilen (bei Gleichstand die groessere)
    best = -1, bestPlaced = -1;
    for (int i = 0; i < N_PRESETS; ++i) {
      const BlockPreset &p = BLOCK_PRESETS[i];
      if (hashOn && p.numH >= 50 && textWidth(num, p.num) + HASH_GAP + hashGridW(p) <= w) attempt(i, true);
      if (textWidth(num, p.num) <= w) attempt(i, false);
    }
  }
  if (best < 0) {  // passt in keiner Groesse: so gross wie moeglich
    const uint8_t *f = fitFont(num, w, {F_(helvB18_te), F_(helvB14_te), F_(helvB10_te)});
    text(x, min(y + h - 4, y + 21 + 18), truncate(num, f, w), f, col(C_TEXT));
    return;
  }
  const BlockPreset &p = BLOCK_PRESETS[best];
  const int numTop = y + 21, numBase = numTop + p.numH;
  const int numW = textWidth(num, p.num);
  text(x, numBase, num, p.num, col(C_TEXT));
  for (int i = 0; i < 16; ++i)
    if (bestMask & (1 << i)) markPart(i);

  if (bestGrid) {
    const uint8_t *mono = p.numH >= 78 ? F_(9x15_mf) : p.numH >= 58 ? F_(7x13_mf) : F_(6x12_mf);
    int gx = x + numW + HASH_GAP;
    text(gx, y + 13, "HASH", F_(helvB08_te), col(C_TITLE));
    int pitch = (p.numH - 11) / 3;
    const int z = hashZeros(b.hash);
    if (z >= 16) {  // erste Zeile: Anzahl Nullen, darunter der Rest in drei Zeilen
      const String rest = String(b.hash).substring(z);
      const int per = (rest.length() + 2) / 3;
      text(gx, numTop + 11, String(z) + "×0", mono, col(C_TITLE));
      for (int i = 0; i < 3; ++i)
        text(gx, numTop + 11 + (i + 1) * pitch, rest.substring(i * per, min((int)rest.length(), (i + 1) * per)), mono,
             col(C_TEXT));
    } else {
      for (int i = 0; i < 4; ++i)
        text(gx, numTop + 11 + i * pitch, String(b.hash).substring(i * 16, i * 16 + 16), mono, col(C_TEXT));
    }
  } else if (nl == 0 && partEnabled(W_BLOCK, 0)) {
    // sehr flach: Alter rechts neben die Blockhoehe
    String age = fmtAgeShort(b.ts);
    if (numW + 12 + textWidth(age, F_(helvB10_te)) <= w) {
      text(x + numW + 12, numBase, age, F_(helvB10_te), col(C_TEXT));
      markPart(0);
    }
  }

  // uebrigen Platz auf die Abstaende verteilen (hoechstens ein Drittel mehr)
  int gapB = p.gap, pitchB = p.pitch;
  if (nl > 0) {
    int extra = h - (21 + p.numH + p.gap + (nl - 1) * p.pitch + 4);
    int add = min(extra / (nl + 1), p.pitch / 3);
    if (add > 0) gapB += add, pitchB += add;
  }
  int yy = numBase + gapB;
  for (int i = 0; i < nl; ++i, yy += pitchB) {
    if (lines[i].kind == 2) {  // Hash als Zeile; ganz, wenn es passt, sonst in der Mitte gekuerzt
      static const struct { const uint8_t *f; int cw; } MONO[] = {
          {F_(10x20_mf), 10}, {F_(9x15_mf), 9}, {F_(7x13_mf), 7}, {F_(6x12_mf), 6}};
      int lw = textWidth("Hash ", p.line);
      // fuehrende Nullen als "19×0", dann der Rest (ganz, wenn es passt, sonst in der Mitte gekuerzt)
      const int z = hashZeros(b.hash);
      const String prefix = z >= 8 ? String(z) + "×0 " : String("");
      String hs = String(b.hash).substring(z >= 8 ? z : 0);
      const int pl = prefix.length() - 1;  // "×" ist ein Zeichen, in UTF-8 aber zwei Bytes
      const int len = hs.length();
      int target = p.pitch >= 30 ? 1 : p.pitch >= 24 ? 2 : 3;
      int mi = target;
      for (int j = target; j < 4; ++j)
        if (lw + (max(0, pl) + len) * MONO[j].cw <= w) {
          mi = j;
          break;
        }
      int chars = (w - lw) / MONO[mi].cw - max(0, pl);
      if (chars < len) {
        int half = max(3, (chars - 2) / 2);
        hs = hs.substring(0, half) + ".." + hs.substring(len - half);
      }
      text(x, yy, "Hash ", p.line, col(C_TEXT));
      if (prefix.length()) text(x + lw, yy, prefix, MONO[mi].f, col(C_TITLE));
      text(x + lw + max(0, pl) * MONO[mi].cw, yy, hs, MONO[mi].f, col(C_TEXT));
      continue;
    }
    text(x, yy, lines[i].s, lines[i].kind == 1 ? p.lineB : p.line, col(C_TEXT));
  }
}

// ---------------------------------------------------------------------------
// Widget: Moscow Time (Haupt- und optional Zweitwaehrung)
// ---------------------------------------------------------------------------
static String perUnit(uint8_t c) {
  const Currency &cu = CURRENCIES[c];
  if (lang == LANG_EN) return cu.symFirstEn ? String(cu.sym) + "1" : String("1 ") + cu.sym;
  return String("1 ") + cu.sym;
}

static void wMoscow(int x, int y, int w, int h) {
  const uint8_t c1 = cfg.cur1, c2 = cfg.cur2;
  const bool two = c2 < NUM_CUR;
  String unit = String(L("sats pro ", "sats per ")) + perUnit(c1) + (two ? " / " + perUnit(c2) : String(""));
  int bar = drawBox(x, y, w, h, "MOSCOW TIME", unit);
  const int cy = y + bar, ch = h - bar - 2;
  const uint8_t cur[2] = {c1, two ? c2 : c1};
  String v[2];
  for (int i = 0; i < 2; ++i) v[i] = data.priceOk && data.prices[cur[i]] ? moscowTime(data.prices[cur[i]]) : String("-");
  String names[2] = {lang == LANG_EN ? CURRENCIES[cur[0]].en : CURRENCIES[cur[0]].de,
                     lang == LANG_EN ? CURRENCIES[cur[1]].en : CURRENCIES[cur[1]].de};
  struct FontH { const uint8_t *f; int h; };
  static const FontH fonts[] = {{F_(logisoso78_tn), 78}, {F_(logisoso62_tn), 62}, {F_(logisoso50_tn), 50},
                                {F_(logisoso38_tn), 38}, {F_(logisoso32_tn), 32}, {F_(logisoso24_tn), 24},
                                {F_(logisoso18_tn), 18}, {F_(helvB14_te), 14},     {F_(helvB10_te), 10}};
  const int nf = sizeof fonts / sizeof fonts[0];
  const int n = two ? 2 : 1;
  // Anordnung waehlen: nebeneinander oder untereinander, je nachdem was groesser wird
  int bestF = nf - 1, bestCols = n;
  bool bestLabel = false;
  for (int cols = n; cols >= 1; --cols) {
    int rows = (n + cols - 1) / cols;
    int cw = (w - 4) / cols, rh = ch / rows;
    for (int i = 0; i < nf; ++i) {
      bool lab = rh >= fonts[i].h + 26;
      int needW = 0;
      for (int j = 0; j < n; ++j)
        needW = max(needW, lab ? max(textWidth(v[j], fonts[i].f), textWidth(names[j], F_(helvB10_te)))
                               : textWidth(v[j], fonts[i].f) + 6 + textWidth(CURRENCIES[cur[j]].sym, F_(helvB12_te)));
      if (fonts[i].h + (lab ? 18 : 0) + 6 <= rh && needW <= cw - 10) {
        if (i < bestF) bestF = i, bestCols = cols, bestLabel = lab;
        break;
      }
    }
  }
  const int cols = bestCols, rows = (n + cols - 1) / cols;
  const int cw = (w - 4) / cols, rh = ch / rows;
  const int fh = fonts[bestF].h;
  const int blockH = fh + (bestLabel ? 18 : 0);
  for (int i = 0; i < n; ++i) {
    int c = i % cols, r = i / cols;
    int cx = x + 2 + cw * c + cw / 2;
    int top = cy + rh * r + (rh - blockH) / 2;
    if (bestLabel) {
      text(cx, top + fh, truncate(v[i], fonts[bestF].f, cw - 10), fonts[bestF].f, col(C_TEXT), CENTER);
      text(cx, top + fh + 17, names[i], F_(helvB10_te), col(C_TITLE), CENTER);
    } else {
      String sym = CURRENCIES[cur[i]].sym;
      int vw = textWidth(v[i], fonts[bestF].f), sw = textWidth(sym, F_(helvB12_te));
      if (vw + 6 + sw > cw - 10) {  // passt nur ohne Waehrungszeichen
        text(cx, top + fh, truncate(v[i], fonts[bestF].f, cw - 10), fonts[bestF].f, col(C_TEXT), CENTER);
        continue;
      }
      int sx = cx - (vw + 6 + sw) / 2;
      text(sx, top + fh, v[i], fonts[bestF].f, col(C_TEXT));
      text(sx + vw + 6, top + fh, sym, F_(helvB12_te), col(C_TITLE));
    }
  }
  if (n == 2) {
    if (cols == 2) gfx->drawFastVLine(x + 2 + cw, cy + 6, ch - 12, col(C_BORDER));
    else gfx->drawFastHLine(x + 10, cy + rh, w - 20, col(C_BORDER));
  }
}

// ---------------------------------------------------------------------------
// Widget: Gebuehren wie auf mempool.space (Niedrig | Mittel | Hoch)
// ---------------------------------------------------------------------------
static void wFees(int x, int y, int w, int h) {
  int bar = drawBox(x, y, w, h, L("GEBÜHREN", "FEES"), "sat/vB");
  const int cy = y + bar, ch = h - bar - 2;
  const int colW = (w - 4) / 3;
  static const int feeIdx[] = {2, 1, 0};  // hourFee, halfHourFee, fastestFee
  const char *labels[] = {L("Niedrig", "Low"), L("Mittel", "Medium"), L("Hoch", "High")};
  String vals[3];
  for (int i = 0; i < 3; ++i) vals[i] = data.feesOk ? fmtFee(data.fees[feeIdx[i]]) : String("-");
  // groesste Schrift, die in Hoehe und Spaltenbreite passt
  static const struct { const uint8_t *f; int h; } VF[] = {
      {F_(logisoso62_tn), 62}, {F_(logisoso50_tn), 50}, {F_(logisoso38_tn), 38}, {F_(logisoso32_tn), 32},
      {F_(logisoso24_tn), 24}, {F_(helvB18_te), 18},     {F_(helvB14_te), 14},     {F_(helvB10_te), 10}};
  const int maxH = ch >= 100 ? ch - 60 : ch >= 50 ? 24 : 18;
  int vi = 7;
  for (int j = 0; j < 7; ++j) {
    bool fits = VF[j].h <= maxH;
    for (int i = 0; fits && i < 3; ++i) fits = textWidth(vals[i], VF[j].f) <= colW - 8;
    if (fits) {
      vi = j;
      break;
    }
  }
  const uint8_t *vf = VF[vi].f;
  const int vh = VF[vi].h;
  bool labelsFit = true;
  for (int i = 0; i < 3; ++i) labelsFit = labelsFit && textWidth(labels[i], F_(helvB10_te)) <= colW - 6;
  const bool showLabels = labelsFit && ch >= vh + 24;
  String rest;
  if (data.feesOk && ch >= vh + 24 + 20) {
    String eco = fmtFee(data.fees[3]), mn = fmtFee(data.fees[4]);
    for (const String &r : {String(L("Ohne Priorität ", "No priority ")) + eco + "   ·   Minimum " + mn,
                            String(L("Ohne Prio. ", "No prio. ")) + eco + " · Min. " + mn, "Min. " + mn})
      if (textWidth(r, F_(helvR10_te)) <= w - 12) {
        rest = r;
        break;
      }
  }
  const bool showRest = rest.length() > 0;
  // Zeile "Ohne Prioritaet" sitzt unten; Label und Wert mittig im Bereich darueber
  const int lineY = showRest ? y + h - 2 - 22 : y + h - 2;
  const int areaH = lineY - cy;
  const int blockH = (showLabels ? 17 : 0) + vh;
  const int t = cy + (areaH - blockH) / 2;
  const int valBase = t + (showLabels ? 17 : 0) + vh;
  for (int i = 0; i < 3; ++i) {
    int cx0 = x + 2 + i * colW;
    int cx = cx0 + colW / 2;
    uint16_t fg = col(C_TEXT);
    if (i == 2) {
      gfx->fillRect(cx0, cy, w - 4 - 2 * colW, lineY - cy, col(C_HIGHLIGHT));
      fg = col(C_HL_TEXT);
    } else if (i > 0) {
      gfx->drawFastVLine(cx0, cy + 5, lineY - cy - 10, col(C_BORDER));
    }
    if (showLabels) text(cx, t + 11, labels[i], F_(helvB10_te), fg, CENTER);
    text(cx, valBase, truncate(vals[i], vf, colW - 4), vf, fg, CENTER);
  }
  if (showRest) {
    gfx->drawFastHLine(x + 2, lineY, w - 4, col(C_BORDER));
    text(x + w / 2, lineY + 16, rest, F_(helvR10_te), col(C_TEXT), CENTER);
  }
}

// ---------------------------------------------------------------------------
// Widget: Blockkette (gemint und Mempool, ein- oder zweireihig)
// ---------------------------------------------------------------------------
struct CubeFonts { const uint8_t *head, *line, *bold, *small; int pitch; };
static const CubeFonts CUBE_S = {F_(helvB12_te), F_(helvR10_te), F_(helvB10_te), F_(helvR08_te), 20};
static const CubeFonts CUBE_L = {F_(helvB18_te), F_(helvR14_te), F_(helvB14_te), F_(helvR12_te), 28};
static const CubeFonts *cubeFonts = &CUBE_S;

// Gebuehren in den Wuerfeln, fuer alle Wuerfel gleich geschrieben. Versucht wird der Reihe nach:
// Sats-Symbol, kompakt "1-5" mit Symbol; dasselbe in kleinerer Schrift;
// erst zuletzt ohne Einheit (die steht dann in der Ueberschrift).
struct UnitStyle { bool small; uint8_t unit; bool compact; };  // unit: 0 = sat/vB, 1 = Symbol, 2 = ohne
// Spanne immer mit Sats-Symbol (nicht "sat/vB"): normal, kompakt "1-5", kleiner, kleiner kompakt, ohne
static const UnitStyle RANGE_STYLES[] = {{false, 1, false}, {false, 1, true}, {true, 1, false}, {true, 1, true},
                                         {false, 2, true}};
static const UnitStyle MEDIAN_STYLES[] = {{false, 1, false}, {true, 1, false}, {false, 2, false}};
static const int N_RANGE = sizeof RANGE_STYLES / sizeof RANGE_STYLES[0];
static const int N_MEDIAN = sizeof MEDIAN_STYLES / sizeof MEDIAN_STYLES[0];
static int rangeStyle = 0, medianStyle = 0;

static const uint8_t *rangeFont(int i) { return RANGE_STYLES[i].small ? cubeFonts->small : cubeFonts->line; }
static const uint8_t *medianFont(int i) { return MEDIAN_STYLES[i].small ? cubeFonts->bold : cubeFonts->head; }
static String unitSuffix(uint8_t unit) {
  if (unit == 0) return " sat/vB";
  if (unit == 1) return String(" ") + SATS + "/vB";
  return "";
}
static String cubeRange(bool mined, int idx, int st) {
  float lo = mined ? data.blocks[idx].feeMin : data.mp[idx].feeLo;
  float hi = mined ? data.blocks[idx].feeMax : data.mp[idx].feeHi;
  return fmtFee(lo) + (RANGE_STYLES[st].compact ? "-" : " - ") + fmtFee(hi) + unitSuffix(RANGE_STYLES[st].unit);
}
static String cubeMedian(int idx, int st) { return "~" + fmtFee(data.mp[idx].medianFee) + unitSuffix(MEDIAN_STYLES[st].unit); }

static void chooseUnitStyles(int nMined, int nMp, int inner) {
  rangeStyle = N_RANGE - 1;
  for (int st = 0; st < N_RANGE; ++st) {
    bool ok = true;
    for (int i = 0; ok && i < nMined && i < data.nBlocks; ++i) ok = textWidth(cubeRange(true, i, st), rangeFont(st)) <= inner;
    for (int i = 0; ok && i < nMp && i < data.nMp; ++i) ok = textWidth(cubeRange(false, i, st), rangeFont(st)) <= inner;
    if (ok) {
      rangeStyle = st;
      break;
    }
  }
  medianStyle = N_MEDIAN - 1;
  for (int st = 0; st < N_MEDIAN; ++st) {
    bool ok = true;
    for (int i = 0; ok && i < nMp && i < data.nMp; ++i) ok = textWidth(cubeMedian(i, st), medianFont(st)) <= inner;
    if (ok) {
      medianStyle = st;
      break;
    }
  }
}
static bool unitInHeading() { return RANGE_STYLES[rangeStyle].unit == 2 || MEDIAN_STYLES[medianStyle].unit == 2; }

static void drawCube(int x, int y, int w, int h, bool mined, int idx) {
  const uint16_t bg = col(mined ? C_MINED_BG : C_MP_BG), fg = col(mined ? C_MINED_TEXT : C_MP_TEXT);
  gfx->fillRect(x, y, w, h, bg);
  if (bg == col(C_BG)) gfx->drawRect(x, y, w, h, col(C_BORDER));
  const CubeFonts &cf = *cubeFonts;
  const int cx = x + w / 2, inner = w - 4;
  String lines[6];
  const uint8_t *fonts[6];
  uint16_t colors[6];
  bool names[6] = {false, false, false, false, false, false};
  int8_t pids[6];
  int n = 0, curPid = -1;
  auto add = [&](const String &s, const uint8_t *f, uint16_t c, bool name = false) {
    lines[n] = s, fonts[n] = f, names[n] = name, pids[n] = curPid;
    colors[n++] = c;
  };
  // Geminte und naechste Bloecke haben getrennte Einstellungen. Die Zeilen beider Wuerfelarten stehen
  // nach ihrer Nummer auf derselben Hoehe (gemeinsamer oberer Rand), so dass gleiche Positionen in
  // beiden Listen fluchten.
  uint8_t ord[8];
  const int np = enabledPartsOfGroup(mined ? PG_CHAIN : PG_CHAINMP, ord, 8);
  if (mined) {
    const BlockInfo &b = data.blocks[idx];
    add(fmtInt(b.height), cf.head, col(C_MINED_HEIGHT));
    for (int j = 0; j < np; ++j) {
      curPid = ord[j];
      switch (ord[j]) {
        case 0: add(cubeRange(true, idx, rangeStyle), rangeFont(rangeStyle), fg); break;
        case 1: add(fmtInt(b.txCount) + " Tx", cf.line, fg); break;
        case 2: add(fmtAgeShort(b.ts), cf.bold, fg); break;
        case 3: add(b.pool, cf.small, fg, true); break;
      }
    }
  } else {
    const MempoolBlock &m = data.mp[idx];
    for (int j = 0; j < np; ++j) {
      curPid = ord[j];
      switch (ord[j]) {
        case 0: add(cubeMedian(idx, medianStyle), medianFont(medianStyle), fg); break;
        case 1: add(cubeRange(false, idx, rangeStyle), rangeFont(rangeStyle), fg); break;
        case 2: add(fmtInt(m.nTx) + " Tx", cf.line, fg); break;
        case 3: add(String(L("in ~", "in ~")) + String((idx + 1) * 10) + L(" Min.", " min"), cf.bold, fg); break;
      }
    }
  }
  uint8_t tmp[8];
  const int rows = max(1 + enabledPartsOfGroup(PG_CHAIN, tmp, 8), enabledPartsOfGroup(PG_CHAINMP, tmp, 8));
  if (!n) return;
  // so viele Zeilen wie passen, vertikal zentriert; fehlt Platz, rueckt der Zeilenabstand
  // erst etwas zusammen, bevor Zeilen wegfallen
  // Raster fuer beide Wuerfelarten gleich: Zeilenzahl = die laengere der beiden Listen
  const int minPitch = cf.pitch == 20 ? 17 : 23;
  int pitch = cf.pitch;
  if (rows * pitch + 6 > h) pitch = max(minPitch, (h - 6) / rows);
  int k = rows;
  while (k > 1 && k * pitch + 6 > h) --k;
  int top = y + (h - k * pitch) / 2;
  int asc = (cf.pitch == 20 ? 15 : 21) - (cf.pitch - pitch) / 2;
  for (int i = 0; i < min(n, k); ++i) {
    text(cx, top + i * pitch + asc, truncate(lines[i], fonts[i], inner, names[i]), fonts[i], colors[i], CENTER);
    if (curWidget == W_CHAIN && pids[i] >= 0) chainShown[mined ? 0 : 1] |= 1 << pids[i];
  }
}

// Eine Reihe von Wuerfeln; newestRight: geminte Bloecke (neuester rechts)
static void cubeRow(int x, int y, int cw, int ch, int gap, int count, bool mined, bool newestRight) {
  for (int i = 0; i < count; ++i) {
    int slot = newestRight ? count - 1 - i : i;
    int cx = x + slot * (cw + gap);
    bool have = mined ? i < data.nBlocks : i < data.nMp;
    if (have) drawCube(cx, y, cw, ch, mined, i);
    else gfx->drawRect(cx, y, cw, ch, col(C_BORDER));
  }
}

static void wChain(int x, int y, int w, int h) {
  const int gap = 8, divGap = 24, labelH = 16;
  const String lMined = L("ZULETZT GEMINT", "RECENTLY MINED");
  // einreihig: links gemint, rechts Mempool
  int N = constrain((w - divGap + gap + 54) / (101 + gap), 2, MAX_BLOCKS + MAX_MP);
  int nMp = constrain((int)lroundf(N * 3.0f / 7.0f), 1, MAX_MP);
  int nMined = constrain(N - nMp, 1, MAX_BLOCKS);
  N = nMined + nMp;
  const int cw1 = (w - divGap - (N - 2) * gap) / N;
  const int ch1 = min(h - labelH, cw1 * 13 / 10);
  // zweireihig (oben gemint, unten Mempool), wenn das deutlich groessere Wuerfel ergibt
  const int rowH = (h - 2 * labelH - gap) / 2;
  int n2 = constrain((w + gap) / (110 + gap), 1, MAX_MP);
  const int cw2 = (w - (n2 - 1) * gap) / n2;
  const int ch2 = min(rowH, cw2 * 13 / 10);
  const bool twoRows = rowH >= 80 && ch2 * cw2 > ch1 * cw1 * 13 / 10;

  if (twoRows) {
    cubeFonts = (cw2 >= 140 && ch2 >= 150) ? &CUBE_L : &CUBE_S;
    chooseUnitStyles(n2, n2, cw2 - 4);
    const String unitNote = unitInHeading() ? String("  (sat/vB)") : String("");
    int top = y + (h - 2 * (labelH + ch2) - gap) / 2;
    text(x + 1, top + 10, truncate(lMined + unitNote, F_(helvB08_te), w), F_(helvB08_te), col(C_TEXT));
    cubeRow(x, top + labelH, cw2, ch2, gap, n2, true, true);
    int y2 = top + labelH + ch2 + gap;
    text(x + 1, y2 + 10, truncate(L("NÄCHSTE BLÖCKE (MEMPOOL)", "NEXT BLOCKS (MEMPOOL)"), F_(helvB08_te), w),
         F_(helvB08_te), col(C_TEXT));
    cubeRow(x, y2 + labelH, cw2, ch2, gap, n2, false, false);
    return;
  }
  cubeFonts = (cw1 >= 140 && ch1 >= 150) ? &CUBE_L : &CUBE_S;
  chooseUnitStyles(nMined, nMp, cw1 - 4);
  const String unitNote = unitInHeading() ? String("  (sat/vB)") : String("");
  // Reihe vertikal mittig, falls die Wuerfel nicht die ganze Hoehe brauchen
  y += (h - labelH - ch1) / 2;
  h = ch1 + labelH;
  const int cubeY = y + labelH;
  const int minedW = nMined * cw1 + (nMined - 1) * gap;
  text(x + 1, y + 10, pick({lMined + unitNote, lMined, L("GEMINT", "MINED")}, F_(helvB08_te), minedW),
       F_(helvB08_te), col(C_TEXT));
  cubeRow(x, cubeY, cw1, ch1, gap, nMined, true, true);
  int divX = x + minedW + divGap / 2;
  for (int yy = cubeY - 4; yy < y + h - 1; yy += 8) gfx->drawFastVLine(divX, yy, min(4, y + h - yy), col(C_BORDER));
  int mx = divX + divGap / 2;
  int mw = x + w - mx;
  text(mx + 1, y + 10,
       pick({L("NÄCHSTE BLÖCKE (MEMPOOL)", "NEXT BLOCKS (MEMPOOL)"), L("NÄCHSTE BLÖCKE", "NEXT BLOCKS"), "MEMPOOL"},
            F_(helvB08_te), mw - 2),
       F_(helvB08_te), col(C_TEXT));
  cubeRow(mx, cubeY, cw1, ch1, gap, nMp, false, false);
}

// ---------------------------------------------------------------------------
// Kacheln (Mempool, Hashrate, Difficulty, Halving, Lightning)
// ---------------------------------------------------------------------------
// Teil-Text "lang|kurz" -> Variante, die in maxW passt ("" = passt gar nicht)
static String partVariant(const String &part, const uint8_t *f, int maxW) {
  int bar = part.indexOf('|');
  String a = bar < 0 ? part : part.substring(0, bar), b = bar < 0 ? String("") : part.substring(bar + 1);
  if (textWidth(a, f) <= maxW) return a;
  if (b.length() && textWidth(b, f) <= maxW) return b;
  return "";
}

// Teile zu Zeilen zusammensetzen ("a · b"), so viele wie in maxLines passen
static int flowPartsWith(const String *parts, int n, const uint8_t *f, int maxW, int maxLines, String *lines,
                         int *placed, bool eager, uint16_t *mask) {
  int nl = 0, done = 0;
  uint16_t m = 0;
  const String sep = "  ·  ";
  for (int i = 0; i < n; ++i) {
    const int bar = parts[i].indexOf('|');
    const String lng = bar < 0 ? parts[i] : parts[i].substring(0, bar);
    const String sht = bar < 0 ? String("") : parts[i].substring(bar + 1);
    // anhaengen in der langen Form; die kurze nur, wenn keine Zeile mehr frei ist
    if (nl && textWidth(lines[nl - 1] + sep + lng, f) <= maxW) {
      lines[nl - 1] += sep + lng;
      ++done, m |= 1 << i;
      continue;
    }
    if (eager && nl && sht.length() && textWidth(lines[nl - 1] + sep + sht, f) <= maxW) {
      lines[nl - 1] += sep + sht;
      ++done, m |= 1 << i;
      continue;
    }
    if (nl < maxLines) {
      String v = partVariant(parts[i], f, maxW);
      if (!v.length()) continue;
      lines[nl++] = v;
      ++done, m |= 1 << i;
      continue;
    }
    if (nl && sht.length() && textWidth(lines[nl - 1] + sep + sht, f) <= maxW) {
      lines[nl - 1] += sep + sht;
      ++done, m |= 1 << i;
      continue;
    }
    break;
  }
  if (placed) *placed = done;
  if (mask) *mask = m;
  return nl;
}

static int flowParts(const String *parts, int n, const uint8_t *f, int maxW, int maxLines, String *lines,
                     int *placed = nullptr, uint16_t *mask = nullptr) {
  String alt[6];
  int pa = 0, pb = 0;
  uint16_t ma = 0, mb = 0;
  int na = flowPartsWith(parts, n, f, maxW, maxLines, lines, &pa, false, &ma);
  int nb = flowPartsWith(parts, n, f, maxW, maxLines, alt, &pb, true, &mb);
  if (pb > pa) {
    for (int i = 0; i < nb; ++i) lines[i] = alt[i];
    na = nb, pa = pb, ma = mb;
  }
  if (placed) *placed = pa;
  if (mask) *mask = ma;
  return na;
}

// Angleichen der Kacheln einer Reihe: erst misst jede Kachel ihre eigene Wahl (tileRec), dann
// gilt fuer alle Kacheln derselben Reihe die kleinste Groesse/Schrift und die groesste Hoehe
struct TileSync { int8_t size, vf, sf; int16_t need; };
static TileSync tileSync[W_COUNT], tileRec[W_COUNT];
static bool tileSyncActive = false, tileRecord = false;

static uint8_t tileIds[16];  // Teil-Nummern zu den Texten, die drawTile bekommt (von orderedParts)

static void drawTile(int x, int y, int w, int h, std::initializer_list<String> labels,
                     std::initializer_list<String> values, const String *parts, int nParts, float progress = -1) {
  if (!parts) noData();
  gfx->drawRect(x, y, w, h, col(C_BORDER));
  // Kacheln einer Reihe gleichen sich an (Groesse, Schriften, Hoehe), siehe syncTiles()
  TileSync *ts = (tileSyncActive && curWidget >= 0) ? &tileSync[curWidget] : nullptr;
  // drei Groessen je nach Platz
  int size = (h >= 120 && w >= 260) ? 2 : (h >= 88 && w >= 180) ? 1 : 0;
  if (ts && ts->size >= 0) size = min(size, (int)ts->size);
  const int pad = size ? 12 : 8, inner = w - 2 * pad;
  const uint8_t *lf = size == 2 ? F_(helvB14_te) : size ? F_(helvB10_te) : F_(helvB08_te);
  // Wert: erste Variante, die wenigstens in helvB12 passt, dann so gross wie moeglich
  String value = *(values.end() - 1);
  for (const String &v : values)
    if (textWidth(v, F_(helvB12_te)) <= inner) {
      value = v;
      break;
    }
  static const uint8_t *const VF[] = {F_(helvB24_te), F_(helvB18_te), F_(helvB14_te), F_(helvB12_te), F_(helvB10_te)};
  int vfi = 4;
  for (int i = 0; i < 5; ++i)
    if (textWidth(value, VF[i]) <= inner) {
      vfi = i;
      break;
    }
  if (size == 0 && vfi == 0) vfi = 1;
  if (ts && ts->vf > vfi) vfi = ts->vf;
  const uint8_t *vf = VF[vfi];
  const int lbOff = size == 2 ? 22 : size ? 18 : 13;     // Grundlinie Titel
  const int vbOff = size == 2 ? 64 : size ? 52 : 35;     // Grundlinie Wert
  const int sbOff = size == 2 ? 92 : size ? 74 : 50;     // Grundlinie erste Unterzeile
  const int extra = progress >= 0 ? 6 : 0;
  // Unterzeilen: so viele Teile wie in Hoehe und Breite passen, in der eingestellten Reihenfolge.
  // Erst mit der normalen Schrift, sonst mit der kleinsten.
  String lines[6], tryLines[6];
  int nl = 0, best = -1;
  uint16_t bestMask = 0;
  const uint8_t *sf = F_(helvR08_te);
  const uint8_t *big = size == 2 ? F_(helvR14_te) : size ? F_(helvR12_te) : F_(helvR08_te);
  for (const uint8_t *f : {big, F_(helvR08_te)}) {
    if (ts && ts->sf == 1 && f == big && big != F_(helvR08_te)) continue;  // Reihe nutzt die kleine Schrift
    const int pitch = f == F_(helvR14_te) ? 19 : f == F_(helvR12_te) ? 16 : 11;
    const int maxLines = nParts ? constrain((h - extra - sbOff - 3) / pitch + 1, 0, 6) : 0;
    int placed = 0;
    uint16_t m = 0;
    int n = flowParts(parts, nParts, f, inner, maxLines, tryLines, &placed, &m);
    if (placed > best) {  // mehr Teile gewinnen; bei Gleichstand die groessere Schrift (zuerst probiert)
      best = placed, nl = n, sf = f, bestMask = m;
      for (int i = 0; i < n; ++i) lines[i] = tryLines[i];
    }
  }
  const int pitch = sf == F_(helvR14_te) ? 19 : sf == F_(helvR12_te) ? 16 : 11;
  const int need = (nl ? sbOff + (nl - 1) * pitch + 3 : vbOff + 4) + extra;
  if (tileRecord && curWidget >= 0)
    tileRec[curWidget] = {(int8_t)size, (int8_t)vfi, (int8_t)(sf == big && big != F_(helvR08_te) ? 0 : 1), (int16_t)need};
  const int t = y + max(0, (h - max(need, ts && ts->need >= 0 ? (int)ts->need : 0)) / 2);
  text(x + pad, t + lbOff, pick(labels, lf, inner), lf, col(C_TITLE));
  text(x + pad, t + vbOff, truncate(value, vf, inner), vf, col(C_TEXT));
  for (int i = 0; i < nl; ++i) text(x + pad, t + sbOff + i * pitch, lines[i], sf, col(C_TEXT));
  for (int i = 0; i < nParts; ++i)
    if (bestMask & (1 << i)) markPart(tileIds[i]);
  if (progress >= 0) {
    if (curWidget == W_DIFFICULTY) markPart(2);
    int bw = w - 2 * pad, by = y + h - 6;
    gfx->drawRect(x + pad, by, bw, 3, col(C_BORDER));
    gfx->fillRect(x + pad, by, (int)(bw * constrain(progress, 0.0f, 100.0f) / 100), 3, col(C_ACCENT));
  }
}

// Teile eines Inhalts in der eingestellten Reihenfolge einsammeln
static int orderedParts(WidgetId id, const String *all, String *out) {
  uint8_t idx[16];
  int n = enabledParts(id, idx, 16), m = 0;
  for (int i = 0; i < n; ++i)
    if (all[idx[i]].length()) tileIds[m] = idx[i], out[m++] = all[idx[i]];
    else if (!(id == W_DIFFICULTY && idx[i] == 2)) noData(idx[i]);  // Balken ist kein Text
  return m;
}

static void wMempool(int x, int y, int w, int h) {
  if (!data.mempoolOk) return drawTile(x, y, w, h, {"MEMPOOL"}, {"-"}, nullptr, 0);
  String all[2] = {fmtDec(data.mpVsize / 1e6, 1) + " MvB",
                   "~" + fmtInt((data.mpVsize + 999999) / 1000000) + L(" Blöcke", " blocks")};
  String p[2];
  int n = orderedParts(W_MEMPOOL, all, p);
  drawTile(x, y, w, h, {"MEMPOOL"}, {fmtInt(data.mpCount) + " Tx", fmtInt(data.mpCount)}, p, n);
}

static void wHashrate(int x, int y, int w, int h) {
  if (!data.hashOk) return drawTile(x, y, w, h, {"HASHRATE"}, {"-"}, nullptr, 0);
  double eh = data.hashrate / 1e18;
  String hr = eh >= 1000 ? fmtDec(eh / 1000, 2) + " ZH/s" : fmtDec(eh, 0) + " EH/s";
  String d = fmtDec(data.difficulty / 1e12, 2) + " T";
  String all[1] = {"Difficulty " + d + "|Diff. " + d};
  String p[1];
  int n = orderedParts(W_HASHRATE, all, p);
  drawTile(x, y, w, h, {"HASHRATE"}, {hr}, p, n);
}

static void wDifficulty(int x, int y, int w, int h) {
  if (!data.diffOk) return drawTile(x, y, w, h, {"DIFFICULTY"}, {"-"}, nullptr, 0);
  String dv = (data.diffChange >= 0 ? "+" : "") + fmtDec(data.diffChange, 2) + " %";
  String all[3] = {String(L("in ", "in ")) + fmtInt(data.diffRemaining) + L(" Blöcken", " blocks"),
                   data.retargetAt > 1700000000 ? "~" + fmtDate(data.retargetAt, false) : String(""), ""};
  String p[3];
  int n = orderedParts(W_DIFFICULTY, all, p);
  drawTile(x, y, w, h, {"DIFFICULTY", "DIFF."}, {dv}, p, n,
           partEnabled(W_DIFFICULTY, 2) ? data.diffProgress : -1.0f);
}

static void wHalving(int x, int y, int w, int h) {
  if (!data.nBlocks) return drawTile(x, y, w, h, {L("NÄCHSTES HALVING", "NEXT HALVING"), "HALVING"}, {"-"}, nullptr, 0);
  uint32_t height = data.blocks[0].height;
  uint32_t epoch = height / 210000 + 1;
  uint32_t remaining = epoch * 210000 - height;
  time_t est = time(nullptr) + (time_t)remaining * 600;
  struct tm tmv;
  localtime_r(&est, &tmv);
  String when = String(monthName(tmv.tm_mon)) + " " + String(tmv.tm_year + 1900);
  String whenShort = String(monthName(tmv.tm_mon)).substring(0, 3) + " " + String(tmv.tm_year + 1900);
  String all[2] = {lang == LANG_EN ? "Halving #" + String(epoch) : String(epoch) + ". Halving",
                   "~" + when + "|~" + whenShort};
  String p[2];
  int n = orderedParts(W_HALVING, all, p);
  drawTile(x, y, w, h, {L("NÄCHSTES HALVING", "NEXT HALVING"), "HALVING"},
           {fmtInt(remaining) + L(" Blöcke", " blocks"), fmtInt(remaining)}, p, n);
}

static void wLightning(int x, int y, int w, int h) {
  if (!data.lnOk) return drawTile(x, y, w, h, {"LIGHTNING"}, {"-"}, nullptr, 0);
  String nodes = fmtInt(data.lnNodes), ch = fmtInt(data.lnChannels);
  // lang|kurz, z.B. "16.244 Knoten|16.244 Kn."
  String all[2] = {nodes + L(" Knoten|", " nodes|") + nodes + L(" Kn.", " nod."),
                   ch + L(" Kanäle|", " channels|") + ch + L(" Kan.", " ch.")};
  String p[2];
  int n = orderedParts(W_LIGHTNING, all, p);
  drawTile(x, y, w, h, {"LIGHTNING"}, {fmtDec(data.lnCapacity / 1e8, 2) + " BTC", fmtDec(data.lnCapacity / 1e8, 0) + " BTC"},
           p, n);
}

// ---------------------------------------------------------------------------
// Widget: Preis-Chart (Hauptwaehrung)
// ---------------------------------------------------------------------------
static void wChart(int x, int y, int w, int h) {
  const ChartPeriod *cp = chartPeriod(cfg.chartDays);
  String title = String(L("KURS ", "PRICE ")) + (cp ? L(cp->titleDe, cp->titleEn) : "");
  if (cfg.chartDays == 0 && data.chartOk && data.chartDays == 0 && data.chartFrom) {  // "KURS SEIT 2013"
    struct tm tmv;
    localtime_r(&data.chartFrom, &tmv);
    title = String(L("KURS SEIT ", "PRICE SINCE ")) + String(tmv.tm_year + 1900);
  }
  const String sym = CURRENCIES[cfg.cur1].sym;
  if (!data.chartOk || data.chartN < 2 || data.chartCur != cfg.cur1 || data.chartDays != cfg.chartDays) {
    int bar = drawBox(x, y, w, h, title, sym);
    return note(x, y + bar, w, h - bar, L("Daten folgen", "Loading data"));
  }
  const float first = data.chart[0], last = data.chart[data.chartN - 1];
  float lo = first, hi = first;
  for (int i = 0; i < data.chartN; ++i) lo = min(lo, data.chart[i]), hi = max(hi, data.chart[i]);
  float chg = (last - first) / first * 100.0f;
  // ab dem Zehnfachen als Faktor ("×1.180.000"), sonst in Prozent
  String unit = sym + "  " +
                (chg >= 900 ? "×" + fmtInt((uint32_t)lroundf(last / first)) : (chg >= 0 ? "+" : "") + fmtDec(chg, 1) + " %");
  const bool logScale = cfg.chartDays == 0 && lo > 0;  // "seit 2010": logarithmisch
  int bar = drawBox(x, y, w, h, title, unit);

  const uint8_t *lf = F_(helvR08_te);
  auto priceLabel = [](float v) { return v < 10 ? fmtDec(v, 2) : v < 1000 ? fmtDec(v, 0) : fmtInt((uint32_t)lroundf(v)); };
  String sHi = priceLabel(hi), sLo = priceLabel(lo);
  int labelW = max(textWidth(sHi, lf), textWidth(sLo, lf)) + 10;
  if (w - 18 - labelW < 60) labelW = 0;  // zu schmal: ohne Preisbeschriftung
  const bool dates = h >= 120;
  const int px0 = x + 10, px1 = x + w - 8 - labelW;
  const int py0 = y + bar + 12, py1 = y + h - (dates ? 20 : 10);
  if (px1 - px0 < 30 || py1 - py0 < 16) return;
  const float range = max(hi - lo, 1.0f), lrange = logScale ? max(logf(hi / lo), 0.01f) : 1.0f;
  auto valY = [&](float v) {
    float f = logScale ? logf(max(v, lo) / lo) / lrange : (v - lo) / range;
    return py1 - (int)lroundf(f * (py1 - py0));
  };
  auto valueAt = [&](int px) {
    float pos = (float)(px - px0) / (px1 - px0) * (data.chartN - 1);
    int i = constrain((int)pos, 0, data.chartN - 2);
    float f = pos - i;
    return data.chart[i] * (1 - f) + data.chart[i + 1] * f;
  };
  for (int px = px0; px <= px1; ++px) {
    int yy = valY(valueAt(px));
    gfx->drawFastVLine(px, yy, py1 - yy + 1, col(C_CHART_FILL));
  }
  for (int yy : {py0, py1})
    for (int px = px0; px + 3 <= px1; px += 6) gfx->drawFastHLine(px, yy, 3, col(C_BORDER));
  int prevY = valY(valueAt(px0));
  for (int px = px0 + 1; px <= px1; ++px) {
    int yy = valY(valueAt(px));
    gfx->drawLine(px - 1, prevY, px, yy, col(C_CHART_LINE));
    gfx->drawLine(px - 1, max(py0, prevY - 1), px, max(py0, yy - 1), col(C_CHART_LINE));
    prevY = yy;
  }
  if (labelW) {
    text(px1 + 6, py0 + 4, sHi, lf, col(C_TEXT));
    text(px1 + 6, py1 + 4, sLo, lf, col(C_TEXT));
  }
  if (dates) {
    String from;
    if (cfg.chartDays == 1) from = fmtTime(data.chartFrom, "%H:%M");
    else if (cfg.chartDays && cfg.chartDays <= 90) from = fmtDate(data.chartFrom, false);
    else from = fmtTime(data.chartFrom, "%m/%Y");  // Monat und Jahr
    String now = L("jetzt", "now");
    if (textWidth(from, lf) + textWidth(now, lf) + 10 <= px1 - px0) {
      text(px0, y + h - 7, from, lf, col(C_TEXT));
      text(px1, y + h - 7, now, lf, col(C_TEXT), RIGHT);
    }
  }
}

// ---------------------------------------------------------------------------
// Widgets: Miner im Heimnetz (AxeOS). "miner" = alle, "miner1..4" = je ein Geraet
// ---------------------------------------------------------------------------
static String fmtHash(float ghs, int dec = 2) {
  if (ghs >= 1000) return fmtDec(ghs / 1000, dec) + " TH/s";
  return fmtDec(ghs, 0) + " GH/s";
}

// Hashrate gross: erst kleinere Schrift, dann weniger Nachkommastellen
struct HashFit { String s; const uint8_t *f; int h; };
static HashFit fitHash(float ghs, int maxW, int maxH) {
  static const struct { const uint8_t *f; int h; } FS[] = {
      {F_(helvB24_te), 24}, {F_(helvB18_te), 18}, {F_(helvB14_te), 14}, {F_(helvB12_te), 12}};
  for (int dec = 2; dec >= 0; --dec) {
    String v = fmtHash(ghs, dec);
    for (const auto &size : FS)
      if (size.h <= maxH && textWidth(v, size.f) <= maxW) return {v, size.f, size.h};
  }
  return {truncate(fmtHash(ghs, 0), FS[3].f, maxW), FS[3].f, 12};
}
static String diffText(const char *s, double num) { return num > 0 ? fmtSI(num) : String(s); }

static int configuredMiners() {
  int n = 0, start = 0;
  while (start < (int)cfg.miners.length()) {
    int end = cfg.miners.indexOf(',', start);
    if (end < 0) end = cfg.miners.length();
    if (end > start) ++n;
    start = end + 1;
  }
  return min(n, MAX_MINERS);
}

// Kennzahlen eines Miners in der eingestellten Reihenfolge (Teile der Gruppe "miner")
struct MField {
  const char *label;
  String value;
  uint8_t part;
};
static int minerFieldList(const MinerInfo &m, MField *out, int maxF, bool reportMissing = true) {
  uint8_t idx[16];
  const int n = enabledParts(W_MINER1, idx, 16);
  int k = 0;
  for (int i = 0; i < n && k < maxF; ++i) {
    const char *lab = "";
    String v;
    switch (idx[i]) {
      case 0: lab = L("Temperatur", "Temperature"), v = fmtDec(m.temp, 0) + " °C"; break;
      case 1: lab = L("Leistung", "Power"), v = fmtDec(m.power, 1) + " W"; break;
      case 2: if (m.fanPct) lab = L("Lüfter", "Fan"), v = String(m.fanPct) + " %"; break;
      case 3:
        if (m.hashrate > 0 && m.power > 0) lab = L("Effizienz", "Efficiency"), v = fmtDec(m.power / (m.hashrate / 1000), 1) + " J/TH";
        break;
      case 4: lab = "Best Diff", v = diffText(m.bestDiff, m.bestDiffNum); break;
      case 5: lab = "Shares", v = fmtInt(m.accepted); break;
      case 6: if (m.uptime) lab = L("Laufzeit", "Uptime"), v = fmtDuration(m.uptime); break;
      case 7:
        if (m.sessionDiffNum > 0 || m.sessionDiff[0]) lab = L("Best (Sitzung)", "Best (session)"), v = diffText(m.sessionDiff, m.sessionDiffNum);
        break;
      case 8: if (m.vrTemp > 0) lab = L("VR-Temp.", "VR temp."), v = fmtDec(m.vrTemp, 0) + " °C"; break;
      case 9: if (m.freq) lab = L("Frequenz", "Frequency"), v = String(m.freq) + " MHz"; break;
      case 10: if (m.rejected) lab = L("Abgelehnt", "Rejected"), v = fmtInt(m.rejected); break;
      case 11: if (m.fanRpm) lab = L("Lüfter U/min", "Fan rpm"), v = fmtInt(m.fanRpm); break;
      case 12: if (m.version[0]) lab = "Firmware", v = m.version; break;
    }
    if (v.length()) out[k].label = lab, out[k].value = v, out[k].part = idx[i], ++k;
    else if (reportMissing) noData(idx[i]);
  }
  return k;
}

// Raster aus Label/Wert-Zellen: Spaltenbreiten aus den tatsaechlichen Texten, so viele Spalten
// wie noetig, damit moeglichst viele Felder (in Prioritaets-Reihenfolge) hineinpassen
struct FieldGrid {
  bool big;
  int cols, rows, shown, cellH;
  int colW[16];
};
static FieldGrid packFields(const MField *f, int n, int w, int h) {
  FieldGrid g{};
  g.big = h >= 150 && w >= 360;
  g.cellH = g.big ? 44 : 30;
  const uint8_t *lf = g.big ? F_(helvR10_te) : F_(helvR08_te);
  const uint8_t *vf = g.big ? F_(helvB18_te) : F_(helvB12_te);
  const int gap = g.big ? 18 : 12;
  const int R = h / g.cellH;
  if (R <= 0 || w <= 0 || n <= 0) return g;
  int cw[16];
  for (int i = 0; i < n; ++i) cw[i] = max(textWidth(f[i].label, lf), textWidth(f[i].value, vf));
  for (int c = 1; c <= n; ++c) {
    // mit c Spalten so viele Felder wie moeglich (auch weniger als c * R, falls breite Felder stoeren)
    bool fit = false;
    for (int shown = min(n, c * R); shown > (c - 1) * R && !fit; --shown) {
      int total = (c - 1) * gap, colW[16] = {0};
      for (int i = 0; i < shown; ++i) colW[i % c] = max(colW[i % c], cw[i]);
      for (int j = 0; j < c; ++j) total += colW[j];
      if (total > w) continue;
      fit = true;
      if (shown > g.shown) {
        g.cols = c, g.shown = shown, g.rows = (shown + c - 1) / c;
        // Restplatz gleichmaessig auf die Spalten verteilen
        int extra = (w - total) / c;
        for (int j = 0; j < c; ++j) g.colW[j] = colW[j] + gap + extra;
      }
    }
    if (!fit) break;  // mehr Spalten passen nicht
    if (g.shown == n) break;  // alle passen: so wenige Spalten wie moeglich (mehr Zeilen)
  }
  return g;
}

static void drawFields(const MField *f, const FieldGrid &g, int x, int y, int h) {
  if (!g.shown) return;
  for (int i = 0; i < g.shown; ++i) markPart(f[i].part);
  const uint8_t *lf = g.big ? F_(helvR10_te) : F_(helvR08_te);
  const uint8_t *vf = g.big ? F_(helvB18_te) : F_(helvB12_te);
  int top = y + (h - g.rows * g.cellH) / 2;
  for (int i = 0; i < g.shown; ++i) {
    int c = i % g.cols, r = i / g.cols, cx = x;
    for (int j = 0; j < c; ++j) cx += g.colW[j];
    int cy = top + r * g.cellH;
    text(cx, cy + (g.big ? 14 : 10), f[i].label, lf, col(C_TITLE));
    text(cx, cy + (g.big ? 36 : 25), f[i].value, vf, col(C_TEXT));
  }
}

static void minerSingle(int x, int y, int w, int h, int idx) {
  const int configured = configuredMiners();
  const bool have = idx < data.nMiners;
  String title = have ? String("MINER · ") + data.miners[idx].name : "MINER " + String(idx + 1);
  const MinerInfo *m = have ? &data.miners[idx] : nullptr;
  // "Update verfuegbar" rechts im Titelbalken (kurz, wenn wenig Platz)
  String unit;
  if (m && minerUpdateAvailable(*m)) {
    String full = String(L("Update ", "Update ")) + latestMinerFirmware(m->repo);
    unit = textWidth(title, h >= 80 ? F_(helvB12_te) : F_(helvB10_te)) + 24 + textWidth(full, F_(helvR10_te)) <= w - 10
               ? full : String("Update");
    if (textWidth(unit, F_(helvR10_te)) + 90 > w) title = "MINER";
  }
  int bar = drawBox(x, y, w, h, title, unit, true);
  const int cy = y + bar, ch = h - bar - 2, ix = x + 10, iw = w - 20;
  if (idx >= configured || !have || !m->ok) noData();
  if (idx >= configured)
    return note(x, cy, w, ch, String(L("Kein Miner ", "No miner ")) + String(idx + 1) + L(" eingetragen", " configured"));
  if (!have) return note(x, cy, w, ch, L("Daten folgen", "Loading data"));
  if (!m->ok) return note(x, cy, w, ch, L("Miner nicht erreichbar", "Miner unreachable"));
  MField fields[16];
  const int nf = minerFieldList(*m, fields, 16);
  // Hashrate gross, dazu das Raster - darunter (hohe Kachel) oder rechts daneben (breite, flache
  // Kachel), je nachdem, wo mehr Werte hineinpassen
  HashFit hfit = fitHash(m->hashrate, iw, ch < 50 ? 18 : 24);
  const int hh = hfit.h, hw = textWidth(hfit.s, hfit.f);
  FieldGrid below = packFields(fields, nf, iw, ch - hh - 12);
  FieldGrid side = packFields(fields, nf, iw - hw - 18, ch - 2);
  if (side.shown > below.shown) {
    text(ix, cy + (ch + hh) / 2, hfit.s, hfit.f, col(C_TEXT));
    drawFields(fields, side, ix + hw + 18, cy + 1, ch - 2);
    return;
  }
  if (below.shown == 0) {  // kein Platz fuer das Raster: nur die Hashrate, mittig
    text(ix, cy + (ch + hh) / 2, hfit.s, hfit.f, col(C_TEXT));
    return;
  }
  // Hashrate und Raster zusammen vertikal mittig
  const int blockH = hh + 8 + below.rows * below.cellH;
  const int top = cy + max(0, (ch - blockH) / 2);
  text(ix, top + hh, hfit.s, hfit.f, col(C_TEXT));
  drawFields(fields, below, ix, top + hh + 8, below.rows * below.cellH);
}

// Alle Inhalte teilen sich Schriftgröße, Grundlinien und Zeilenabstand.
static constexpr int MINER_FIELDS = 16, MINER_GAP = 6;
struct MinerRow {
  String name, hash, value[MINER_FIELDS];
  bool online;
};
struct MinerType {
  const uint8_t *regular, *bold;
  int size, pitch;
};
struct MinerWidths {
  int name = 0, hash = 0, value[MINER_FIELDS] = {};
};
struct MinerCell {
  int part, line, right;
};
enum MinerSummary { SUM_SIDE, SUM_INLINE, SUM_ABOVE };
struct MinerTable {
  const MinerType *type = nullptr;
  MinerSummary summary = SUM_ABOVE;
  int cols = 0, shown = 0, rows = 0, width = 0, gap = 0, offset = 0;
  int nameWidth = 0, hashRight = 0, lines = 1, fields = 0, primary = 0;
  uint16_t priority = 0;
  MinerCell cells[MINER_FIELDS];
};

static bool betterMinerTable(const MinerTable &a, const MinerTable &b) {
  if (a.shown != b.shown) return a.shown > b.shown;
  if (a.primary != b.primary) return a.primary > b.primary;
  // Zusätzliche Werte sollen auf Kosten einer geringfügig kleineren, gut lesbaren Schrift passen.
  const int aq = min(a.type->size, 14) + 2 * a.fields, bq = min(b.type->size, 14) + 2 * b.fields;
  if (aq != bq) return aq > bq;
  if (a.type->size != b.type->size) return a.type->size > b.type->size;
  if (a.priority != b.priority) return a.priority > b.priority;
  if (a.lines != b.lines) return a.lines < b.lines;
  return a.cols < b.cols;
}

static void fitMinerFields(MinerTable &t, const MinerWidths &m, const uint8_t *order, int n, int maxLines) {
  const int fullName = m.name + MINER_GAP;
  const int minName = min(fullName, t.width < 100 ? max(20, t.type->size + MINER_GAP) : 48);
  t.hashRight = minName + m.hash;
  if (t.hashRight > t.width) return;
  int line = 0, used = t.hashRight, firstEnd = used;
  for (int i = 0; i < n; ++i) {
    const int part = order[i];
    if (!m.value[part]) continue;
    const int width = m.value[part];
    if (used + MINER_GAP + width > t.width) {
      if (line + 1 >= maxLines || width > t.width) continue;
      ++line;
      used = 0;
    }
    used += (used ? MINER_GAP : 0) + width;
    if (!line) firstEnd = used;
    t.cells[t.fields++] = {part, line, used};
    t.priority |= 1U << (n - i - 1);
    if (part < 2) ++t.primary;
  }
  const int moreName = min(fullName - minName, t.width - firstEnd);
  t.hashRight += moreName;
  t.nameWidth = t.hashRight - m.hash - MINER_GAP;
  for (int i = 0; i < t.fields && !t.cells[i].line; ++i) t.cells[i].right += moreName;
  t.lines = line + 1;
}

static MinerTable fitMinerTable(const MinerRow *miners, int count, const uint8_t *order, int n,
                               const String &total, const String &power, int w, int h) {
  static const MinerType fonts[] = {
      {F_(helvR24_te), F_(helvB24_te), 24, 36}, {F_(helvR18_te), F_(helvB18_te), 18, 27},
      {F_(helvR14_te), F_(helvB14_te), 14, 21}, {F_(helvR12_te), F_(helvB12_te), 12, 16},
      {F_(helvR10_te), F_(helvB10_te), 10, 14}, {F_(helvR08_te), F_(helvB08_te), 8, 12}};
  const int gap = w < 200 ? 8 : 16;
  MinerTable best;
  for (const auto &font : fonts) {
    const int lines = h / font.pitch;
    if (!lines) continue;
    const int totalW = textWidth(total, font.bold), powerW = textWidth(power, font.bold);
    const int sumW = totalW + (powerW ? MINER_GAP + powerW : 0);
    MinerWidths widths[MAX_MINERS];
    for (int i = 0; i < count; ++i) {
      const MinerRow &m = miners[i];
      if (i) widths[i] = widths[i - 1];
      MinerWidths &a = widths[i];
      a.name = max(a.name, textWidth(m.name, font.regular));
      a.hash = max(a.hash, textWidth(m.hash, font.regular));
      for (int j = 0; j < n; ++j) {
        const int part = order[j];
        a.value[part] = max(a.value[part], textWidth(m.value[part], font.regular));
      }
    }
    for (MinerSummary summary : {SUM_SIDE, SUM_INLINE, SUM_ABOVE}) {
      const bool above = summary == SUM_ABOVE;
      if (summary == SUM_SIDE && (!powerW || lines < 2)) continue;
      if (above && sumW > w) continue;
      const int offset = above ? 0 : (summary == SUM_SIDE ? max(totalW, powerW) : sumW) + gap;
      if (offset >= w) continue;
      const int tableLines = lines - above;
      for (int shown = count; shown > 0 && shown >= best.shown; --shown) {
        for (int cols = 1; cols <= shown; ++cols) {
          const int groups = (shown + cols - 1) / cols;
          const int maxLines = tableLines / groups;
          if (!maxLines) continue;
          MinerTable t;
          t.type = &font;
          t.summary = summary;
          t.shown = shown;
          t.cols = cols;
          t.width = (w - offset - (cols - 1) * gap) / cols;
          t.gap = gap;
          t.offset = offset;
          fitMinerFields(t, widths[shown - 1], order, n, maxLines);
          if (t.hashRight > t.width) continue;
          t.rows = groups * t.lines + above;
          if (summary == SUM_SIDE && t.rows < 2) continue;
          if (!best.shown || betterMinerTable(t, best)) best = t;
        }
      }
    }
  }
  return best;
}

static void drawMinerTable(const MinerRow *miners, const MinerTable &t, const String &hash, const String &power,
                           int x, int y, int h) {
  const MinerType &font = *t.type;
  u8g2.setFont(font.bold);
  const int boldAscent = u8g2.getFontAscent(), boldDescent = -u8g2.getFontDescent();
  u8g2.setFont(font.regular);
  const int ascent = max(boldAscent, (int)u8g2.getFontAscent());
  const int descent = max(boldDescent, -(int)u8g2.getFontDescent());
  const int pitch = min(font.pitch + 6, h / t.rows);
  const int base = y + (h - t.rows * pitch) / 2 + (pitch + ascent - descent) / 2;
  text(x, base, hash, font.bold, col(C_TEXT));
  if (power.length()) {
    if (t.summary == SUM_SIDE) text(x, base + pitch, power, font.bold, col(C_TEXT));
    else text(x + textWidth(hash, font.bold) + MINER_GAP, base, power, font.bold, col(C_TEXT));
    markPart(1);
  }
  const int tableBase = base + (t.summary == SUM_ABOVE ? pitch : 0);
  for (int i = 0; i < t.shown; ++i) {
    const MinerRow &m = miners[i];
    const int lx = x + t.offset + (i % t.cols) * (t.width + t.gap);
    const int yy = tableBase + (i / t.cols) * t.lines * pitch;
    const uint16_t ink = col(m.online ? C_TEXT : C_TITLE);
    text(lx, yy, truncate(m.name, font.regular, t.nameWidth, true), font.regular, ink);
    text(lx + t.hashRight, yy, m.hash, font.regular, ink, RIGHT);
    for (int j = 0; j < t.fields && m.online; ++j) {
      const MinerCell &cell = t.cells[j];
      if (!m.value[cell.part].length()) continue;
      text(lx + cell.right, yy + cell.line * pitch, m.value[cell.part], font.regular, ink, RIGHT);
      markPart(cell.part);
    }
  }
}

static void minerAll(int x, int y, int w, int h) {
  const int configured = configuredMiners();
  if (configured <= 1) return minerSingle(x, y, w, h, 0);
  MinerRow rows[MAX_MINERS];
  uint16_t available = 0;
  int online = 0;
  float total = 0, power = 0;
  bool upd = false;
  for (int i = 0; i < data.nMiners; ++i) {
    const MinerInfo &m = data.miners[i];
    MinerRow &row = rows[i];
    row.name = m.name;
    row.online = m.ok;
    row.hash = m.ok ? fmtHash(m.hashrate) : String(L("offline", "offline"));
    upd = upd || minerUpdateAvailable(m);
    if (!m.ok) continue;
    ++online;
    total += m.hashrate;
    power += m.power;
    MField fields[MINER_FIELDS];
    const int n = minerFieldList(m, fields, MINER_FIELDS, false);
    for (int j = 0; j < n; ++j) {
      const MField &f = fields[j];
      available |= 1U << f.part;
      switch (f.part) {
        case 0: case 3: case 9: row.value[f.part] = f.value; break;
        case 1: row.value[f.part] = fmtDec(m.power, 0) + " W"; break;
        default: row.value[f.part] = String(f.label) + " " + f.value; break;
      }
    }
  }
  String unit = String(online) + "/" + String(configured) + " online";
  if (upd && textWidth(unit + " · Update", F_(helvR10_te)) + 90 <= w) unit += " · Update";
  const int bar = drawBox(x, y, w, h, "MINER", unit);
  const int cy = y + bar + 4, ch = h - bar - 8, ix = x + 10, iw = w - 20;
  if (!data.nMiners) {
    noData();
    return note(x, cy, w, ch, L("Daten folgen", "Loading data"));
  }
  uint8_t order[MINER_FIELDS];
  const int n = enabledParts(W_MINER, order, MINER_FIELDS);
  if (!online) noData();
  for (int i = 0; i < n; ++i)
    if (!(available & (1U << order[i]))) noData(order[i]);

  const String hash = fmtHash(total);
  const String pw = online && partEnabled(W_MINER, 1) ? fmtDec(power, 0) + " W" : String();
  const MinerTable table = fitMinerTable(rows, data.nMiners, order, n, hash, pw, iw, ch);
  if (table.shown) return drawMinerTable(rows, table, hash, pw, ix, cy, ch);
  const auto *f = F_(helvB12_te);
  const int base = cy + (ch + 12) / 2, hw = textWidth(hash, f);
  text(ix, base, truncate(hash, f, iw), f, col(C_TEXT));
  if (pw.length() && hw + MINER_GAP + textWidth(pw, f) <= iw) {
    text(ix + hw + MINER_GAP, base, pw, f, col(C_TEXT));
    markPart(1);
  }
}

static void wMiner(int x, int y, int w, int h) { minerAll(x, y, w, h); }
static void wMiner1(int x, int y, int w, int h) { minerSingle(x, y, w, h, 0); }
static void wMiner2(int x, int y, int w, int h) { minerSingle(x, y, w, h, 1); }
static void wMiner3(int x, int y, int w, int h) { minerSingle(x, y, w, h, 2); }
static void wMiner4(int x, int y, int w, int h) { minerSingle(x, y, w, h, 3); }

// ---------------------------------------------------------------------------
// Widget: QR-Code zur Geraete-Webseite
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Widget: "Heute vor X Jahren" - Bitcoin-Ereignisse an diesem Kalendertag.
// Kuratierte Liste (Datum wie allgemein ueberliefert, meist UTC). Ist heute nichts dabei,
// wird das naechste bevorstehende Ereignis gezeigt.
// ---------------------------------------------------------------------------
static int dayOfYear(int m, int d) {
  static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  return cum[(m - 1) % 12] + d;
}

// Text an Leerzeichen in Zeilen teilen; -1, wenn ein einzelnes Wort zu breit ist
static int splitLines(String rest, const uint8_t *f, int maxW, String *out, int maxOut) {
  int n = 0;
  while (rest.length()) {
    String line = rest;
    if (textWidth(rest, f) > maxW) {
      int cut = -1;
      for (int i = rest.indexOf(' '); i > 0; i = rest.indexOf(' ', i + 1)) {
        if (textWidth(rest.substring(0, i), f) <= maxW) cut = i;
        else break;
      }
      if (cut < 0) return -1;
      line = rest.substring(0, cut);
      rest = rest.substring(cut + 1);
    } else {
      rest = "";
    }
    if (n < maxOut) out[n] = line;
    ++n;
  }
  return n;
}

static String histDate(const EventView &e) {
  char buf[24];
  if (lang == LANG_EN) snprintf(buf, sizeof buf, "%.3s %d, %d", monthName(e.m - 1), e.d, e.y);
  else snprintf(buf, sizeof buf, "%02d.%02d.%d", e.d, e.m, e.y);
  return buf;
}

static void wHistory(int x, int y, int w, int h) {
  int bar = drawBox(x, y, w, h, L("AN DIESEM TAG", "ON THIS DAY"), "");
  const int ix = x + 10, iw = w - 20, top = y + bar + 2, ih = h - bar - 6;
  time_t now = time(nullptr);
  if (now < 1700000000 && !historyTestText.length()) return note(x, y + bar, w, h - bar, L("Uhrzeit fehlt noch", "No time yet"));
  struct tm t;
  localtime_r(&now, &t);
  const int thisYear = t.tm_year + 1900;
  // Ereignisse heute; sonst das naechste bevorstehende
  EventView ev[4];
  int n = 0, ahead = 0;
  if (historyTestText.length()) {
    ev[n++] = {2009, 1, 3, historyTestText.c_str(), historyTestText.c_str()};
  } else {
    EventView v;
    for (int i = 0; i < eventTotal() && n < 4; ++i)
      if (eventAt(i, v) && v.m == t.tm_mon + 1 && v.d == t.tm_mday) ev[n++] = v;
    if (!n) {
      int today = dayOfYear(t.tm_mon + 1, t.tm_mday), bestD = 999;
      for (int i = 0; i < eventTotal(); ++i) {
        if (!eventAt(i, v)) continue;
        int d = (dayOfYear(v.m, v.d) - today + 365) % 365;
        if (d > 0 && d < bestD) bestD = d, ev[0] = v;
      }
      if (bestD == 999) return note(x, y + bar, w, h - bar, L("Keine Ereignisse aktiv", "No events enabled"));
      n = 1;
      ahead = bestD;
    }
  }
  // Ueberschrift je Ereignis (lange und kurze Form)
  auto heads = [&](const EventView &e, String &longH, String &shortH) {
    int yrs = thisYear - e.y + (ahead && dayOfYear(e.m, e.d) < dayOfYear(t.tm_mon + 1, t.tm_mday) ? 1 : 0);
    if (yrs <= 0) {  // eigenes Ereignis in diesem Jahr oder in der Zukunft
      longH = (ahead ? (ahead == 1 ? String(L("Morgen", "Tomorrow"))
                                   : (lang == LANG_EN ? "In " + String(ahead) + " days" : "In " + String(ahead) + " Tagen"))
                     : String(L("Heute", "Today"))) + ": " + histDate(e);
      shortH = histDate(e);
      return;
    }
    String ago = lang == LANG_EN ? (yrs == 1 ? String("1 year ago") : String(yrs) + " years ago")
                                 : (yrs == 1 ? String("vor 1 Jahr") : "vor " + String(yrs) + " Jahren");
    if (!ahead) {
      longH = lang == LANG_EN ? "Today, " + ago + " (" + String(e.y) + ")" : "Heute " + ago + " (" + String(e.y) + ")";
      shortH = (lang == LANG_EN ? ago : "V" + ago.substring(1)) + " (" + String(e.y) + ")";
    } else {
      String when = ahead == 1 ? String(L("Morgen", "Tomorrow"))
                               : (lang == LANG_EN ? "In " + String(ahead) + " days" : "In " + String(ahead) + " Tagen");
      longH = when + ": " + histDate(e);
      shortH = histDate(e);
    }
  };
  // groesste Schrift, bei der alles ohne Kuerzung passt
  struct FS { const uint8_t *body, *head; int pitch; };
  static const FS FONTS[] = {{F_(helvR18_te), F_(helvB18_te), 24}, {F_(helvR14_te), F_(helvB14_te), 19},
                             {F_(helvR12_te), F_(helvB12_te), 16}, {F_(helvR10_te), F_(helvB10_te), 13},
                             {F_(helvR08_te), F_(helvB08_te), 11}};
  String lines[4][8], head[4];
  int nl[4];
  int fi = -1, shown = n;
  for (int f = 0; f < 5 && fi < 0; ++f) {
    for (shown = n; shown >= 1 && fi < 0; --shown) {
      int total = 0;
      bool ok = true;
      for (int k = 0; k < shown && ok; ++k) {
        String lh, sh;
        heads(ev[k], lh, sh);
        head[k] = textWidth(lh, FONTS[f].head) <= iw ? lh : sh;
        ok = textWidth(head[k], FONTS[f].head) <= iw;
        nl[k] = splitLines(eventText(ev[k]), FONTS[f].body, iw, lines[k], 8);
        ok = ok && nl[k] > 0 && nl[k] <= 8;
        total += (1 + nl[k]) * FONTS[f].pitch + (k ? 8 : 0);
      }
      if (ok && total <= ih) fi = f;
    }
  }
  if (fi < 0) {  // passt in keiner Groesse: kleinste Schrift, gekuerzt
    fi = 4, shown = 1;
    String lh, sh;
    heads(ev[0], lh, sh);
    head[0] = truncate(sh, FONTS[4].head, iw);
    int rows = max(1, ih / FONTS[4].pitch - 1);
    nl[0] = splitLines(eventText(ev[0]), FONTS[4].body, iw, lines[0], 8);
    if (nl[0] < 0 || nl[0] > rows) {
      nl[0] = 1;
      lines[0][0] = truncate(eventText(ev[0]), FONTS[4].body, iw);
    }
  } else {
    ++shown;  // Schleife hat nach dem Treffer noch einmal heruntergezaehlt
  }
  const FS &f = FONTS[fi];
  int total = 0;
  for (int k = 0; k < shown; ++k) total += (1 + nl[k]) * f.pitch + (k ? 8 : 0);
  int yy = top + max(0, (ih - total) / 2) + f.pitch - 4;
  for (int k = 0; k < shown; ++k) {
    if (k) yy += 8;
    text(ix, yy, head[k], f.head, col(C_TITLE));
    yy += f.pitch;
    for (int i = 0; i < nl[k]; ++i, yy += f.pitch) text(ix, yy, lines[k][i], f.body, col(C_TEXT));
  }
}

// ---------------------------------------------------------------------------
// Raster und Dashboard
// ---------------------------------------------------------------------------
static const int GX0 = 12, GY0 = 70, GAP = 8;

static void slotRect(const Slot &s, int &x, int &y, int &w, int &h) {
  const int GW = W - 24, GH = H - 78;  // Rasterflaeche unter der Kopfzeile
  const float px = (GW + GAP) / (float)gridCols(), py = (GH + GAP) / (float)gridRows();
  x = GX0 + lroundf(s.x * px);
  y = GY0 + lroundf(s.y * py);
  w = GX0 + lroundf((s.x + s.w) * px) - GAP - x;
  h = GY0 + lroundf((s.y + s.h) * py) - GAP - y;
}

typedef void (*WidgetFn)(int, int, int, int);
static const WidgetFn WIDGET_FN[W_COUNT] = {wBlock,     wMoscow, wFees,  wChain,  wMempool, wHashrate,
                                            wDifficulty, wHalving, wLightning, wChart, wMiner,   wMiner1,
                                            wMiner2,   wMiner3, wMiner4, wHistory};

static bool isTile(int id) {
  return id == W_MEMPOOL || id == W_HASHRATE || id == W_DIFFICULTY || id == W_HALVING || id == W_LIGHTNING;
}

static void syncTiles(const Layout &l) {
  static CheckCanvas cc;
  cc.cx0 = cc.cy0 = -10000, cc.cx1 = cc.cy1 = 10000;
  Adafruit_GFX *saved = gfx;
  gfx = &cc;
  bindU8g2(cc);
  for (int i = 0; i < W_COUNT; ++i) tileSync[i] = {-1, -1, -1, -1};
  tileSyncActive = true;
  tileRecord = true;
  for (int pass = 0; pass < 2; ++pass) {
    // Pass 0: eigene Wahl jeder Kachel; Pass 1: mit angeglichenen Schriften die Hoehe messen
    for (int i = 0; i < l.n; ++i) {
      if (!isTile(l.s[i].id)) continue;
      int x, y, w, h;
      slotRect(l.s[i], x, y, w, h);
      curWidget = l.s[i].id;
      WIDGET_FN[l.s[i].id](x, y, w, h);
    }
    for (int i = 0; i < l.n; ++i) {
      const Slot &a = l.s[i];
      if (!isTile(a.id)) continue;
      TileSync g = tileRec[a.id];
      for (int j = 0; j < l.n; ++j) {
        const Slot &b = l.s[j];
        if (!isTile(b.id) || b.y != a.y || b.h != a.h) continue;
        const TileSync &o = tileRec[b.id];
        g.size = min(g.size, o.size), g.vf = max(g.vf, o.vf), g.sf = max(g.sf, o.sf), g.need = max(g.need, o.need);
      }
      if (pass == 0) tileSync[a.id] = {g.size, g.vf, g.sf, -1};
      else tileSync[a.id].need = g.need;
    }
  }
  tileRecord = false;
  curWidget = -1;
  gfx = saved;
  bindU8g2(*saved);
}

void drawDashboard() {
  // In der Vorschau wird streifenweise gerechnet: was den Streifen nicht beruehrt, wird
  // uebersprungen (die Widgets zeichnen nachweislich nicht ueber ihren Rand, siehe Pruefstand).
  const bool band = gfx == &canvas;
  const int b0 = canvas.y0, b1 = canvas.y0 + canvas.BH;
  syncTiles(layout);
  gfx->fillScreen(col(C_BG));
  if (!band || b0 < 62) drawHeader();
  for (int i = 0; i < layout.n; ++i) {
    int x, y, w, h;
    slotRect(layout.s[i], x, y, w, h);
    if (band && (y + h <= b0 || y >= b1)) continue;
    curWidget = layout.s[i].id;
    WIDGET_FN[layout.s[i].id](x, y, w, h);
  }
  curWidget = -1;
  tileSyncActive = false;
}

// Zeichnet das Layout unsichtbar und meldet je Widget, welche Teile Platz fanden
void measureParts(const Layout &l, uint16_t *masks, uint16_t *haveData, uint16_t *chainSplit) {
  syncTiles(l);
  static CheckCanvas cc;
  cc.cx0 = cc.cy0 = -10000, cc.cx1 = cc.cy1 = 10000;
  Adafruit_GFX *saved = gfx;
  gfx = &cc;
  bindU8g2(cc);
  memset(shownMask, 0, sizeof shownMask);
  memset(dataMask, 0xFF, sizeof dataMask);
  chainShown[0] = chainShown[1] = 0;
  for (int i = 0; i < l.n; ++i) {
    int x, y, w, h;
    slotRect(l.s[i], x, y, w, h);
    curWidget = l.s[i].id;
    WIDGET_FN[l.s[i].id](x, y, w, h);
  }
  curWidget = -1;
  tileSyncActive = false;
  gfx = saved;
  bindU8g2(*saved);
  memcpy(masks, shownMask, sizeof shownMask);
  memcpy(haveData, dataMask, sizeof dataMask);
  chainSplit[0] = chainShown[0], chainSplit[1] = chainShown[1];
}

uint32_t renderCount = 0;

void refreshDisplay(void (*painter)()) {
  uint32_t t0 = millis();
  epdActive = true;
  display.setRotation(cfg.rotation);
  display.setFullWindow();
  display.firstPage();
  do {
    painter();
  } while (display.nextPage());
  display.hibernate();
  epdActive = false;
  ++renderCount;
  Serial.printf("[epd] Bild aktualisiert in %lu ms\n", millis() - t0);
}

#ifdef TT_DEVTOOLS
// Pruefstand: Widget unsichtbar zeichnen und zaehlen, was ueber den Rand geht
RenderCheck checkWidget(WidgetId id, int wCells, int hCells) {
  static CheckCanvas cc;
  Slot s = {(uint8_t)id, 0, 0, (uint8_t)wCells, (uint8_t)hCells};
  int x, y, w, h;
  slotRect(s, x, y, w, h);
  cc.cx0 = x, cc.cy0 = y, cc.cx1 = x + w, cc.cy1 = y + h;
  cc.outside = 0;
  cc.sample = "";
  truncCount = truncOkCount = 0;
  truncSample = "";
  Adafruit_GFX *saved = gfx;
  gfx = &cc;
  bindU8g2(cc);
  WIDGET_FN[id](x, y, w, h);
  gfx = saved;
  bindU8g2(*saved);
  RenderCheck r;
  r.outside = cc.outside;
  r.truncs = truncCount;
  r.truncsOk = truncOkCount;
  r.sample = cc.outside ? cc.sample : truncSample;
  return r;
}
#endif

// ---------------------------------------------------------------------------
// Info-Bildschirme
// ---------------------------------------------------------------------------
static void drawFrame() {
  gfx->fillScreen(col(C_BG));
  gfx->fillRect(0, 0, W, 62, col(C_HEAD_BG));
  drawBitcoinLogo(34, 31);
  text(68, 40, "TIMECHAIN TERMINAL", F_(helvB18_te), col(C_HEAD_TEXT));
  gfx->fillRect(0, H - 24, W, 24, col(C_HIGHLIGHT));
  text(W / 2, H - 6, truncate(PRODUCT_NAME " " FW_VERSION "  ·  GDP075FW1  ·  " + node.host, F_(helvR10_te), W - 20, true),
       F_(helvR10_te), col(C_HL_TEXT), CENTER);
}

// Zeilenumbruch an Leerzeichen; gibt die Anzahl der Zeilen zurueck
static int wrapText(int x, int y, int maxW, int pitch, String rest, const uint8_t *f, uint16_t c, int maxLines = 4) {
  int n = 0;
  while (rest.length() && n < maxLines) {
    String line = rest;
    if (textWidth(rest, f) > maxW) {
      int cut = -1;
      for (int i = 1; i < (int)rest.length(); ++i)
        if (rest[i] == ' ' && textWidth(rest.substring(0, i), f) <= maxW) cut = i;
      if (cut < 0) {
        line = truncate(rest, f, maxW);
        rest = "";
      } else {
        line = rest.substring(0, cut);
        rest = rest.substring(cut + 1);
      }
    } else {
      rest = "";
    }
    text(x, y + n * pitch, line, f, c);
    ++n;
  }
  return n;
}

static String msgTitle;
static String msgLines[5];

static void drawMessage() {
  drawFrame();
  text(40, 140, truncate(msgTitle, F_(helvB24_te), W - 60), F_(helvB24_te), col(C_TITLE));
  int yy = 200;
  for (int i = 0; i < 5; ++i)
    if (msgLines[i].length()) yy += 34 * wrapText(40, yy, W - 60, 30, msgLines[i], F_(helvR18_te), col(C_TEXT), 3) + 8;
}

void showMessage(const String &title, const String &l1, const String &l2, const String &l3, const String &l4,
                 const String &l5) {
  msgTitle = title;
  msgLines[0] = l1;
  msgLines[1] = l2;
  msgLines[2] = l3;
  msgLines[3] = l4;
  msgLines[4] = l5;
  refreshDisplay(drawMessage);
}

// Einrichtung zweisprachig: die Sprache wird erst im Portal gewaehlt
static void drawSetup() {
  drawFrame();
  const String head = "WLAN einrichten  ·  Wi-Fi setup";
  int ty = 118;
  if (textWidth(head, F_(helvB24_te)) <= W - 60) text(40, ty, head, F_(helvB24_te), col(C_TITLE));
  else {
    text(40, ty, "WLAN einrichten", F_(helvB24_te), col(C_TITLE));
    text(40, ty += 34, "Wi-Fi setup", F_(helvB24_te), col(C_TITLE));
  }
  const char *de[] = {"Mit Handy oder PC das WLAN »" WIFI_AP_NAME "« wählen",
                      "Die Einrichtungsseite öffnet sich (sonst 192.168.4.1 aufrufen)",
                      "»Configure WiFi«: 2,4-GHz-WLAN wählen, Passwort eingeben"};
  const char *en[] = {"Connect your phone or PC to the Wi-Fi »" WIFI_AP_NAME "«",
                      "The setup page opens (otherwise go to 192.168.4.1)",
                      "»Configure WiFi«: choose your 2.4 GHz Wi-Fi, enter the password"};
  // eine Schriftgroesse fuer alle drei Zeilen (die laengste entscheidet)
  const uint8_t *fd = F_(helvB18_te), *fe = F_(helvR14_te);
  for (int i = 0; i < 3; ++i) {
    if (textWidth(de[i], fd) > W - 102) fd = F_(helvB14_te);
    if (textWidth(en[i], fe) > W - 102) fe = F_(helvR12_te);
  }
  int yy = ty + 64;
  for (int i = 0; i < 3; ++i) {
    gfx->fillCircle(56, yy - 14, 17, col(C_TEXT));
    text(56, yy - 6, String(i + 1), F_(helvB18_te), col(C_BG), CENTER);
    int n = wrapText(90, yy - 4, W - 102, 24, de[i], fd, col(C_TEXT), 3);
    int m = wrapText(90, yy - 4 + n * 24 + 2, W - 102, 22, en[i], fe, col(C_TEXT), 3);
    yy += n * 24 + m * 22 + (portrait() ? 34 : 18);
  }
}

void showSetupScreen() { refreshDisplay(drawSetup); }
void (*setupScreenPainter())() { return drawSetup; }

// Geraete-Webseite mit grossem QR-Code (BOOT-Taste, Webseite, nach der Einrichtung)
static String infoTitle;
void drawInfoScreen() {
  drawFrame();
  String url = deviceUrl();
  int textX = 40, top = 140;
  if (ensureQr(url)) {
    if (portrait()) {
      int s = min((W - 80) / qrModules(), 10), q = qrModules() * s;
      drawQr((W - q) / 2, 90, s);
      top = 90 + q + 60;
    } else {
      int s = min(360 / qrModules(), 10), q = qrModules() * s;
      drawQr(30, 62 + (394 - q) / 2, s);
      textX = 30 + q + 30;
    }
  }
  const int tw = W - textX - 20, dy = top - 140;
  String title = infoTitle.length() ? infoTitle : String(L("Geräte-Webseite", "Device web page"));
  const uint8_t *tf = fitFont(title, tw, {F_(helvB24_te), F_(helvB18_te)});
  text(textX, 140 + dy, truncate(title, tf, tw), tf, col(C_TITLE));
  text(textX, 192 + dy, url, fitFont(url, tw, {F_(helvB24_te), F_(helvB18_te), F_(helvB14_te)}), col(C_TEXT));
  String host = "http://" WIFI_HOSTNAME ".local";
  text(textX, 228 + dy, host, fitFont(host, tw, {F_(helvR18_te), F_(helvR14_te), F_(helvR12_te)}), col(C_TEXT));
  const char *hints[] = {L("Mit der Handy-Kamera scannen:", "Scan with your phone camera:"),
                         L("Einstellungen, Layout, Farben, Updates.", "Settings, layout, colors, updates."),
                         L("Zurück zur Anzeige nach 3 Minuten", "Back to the dashboard after 3 minutes")};
  for (int i = 0; i < 3; ++i)
    text(textX, 290 + dy + i * 28, hints[i], fitFont(hints[i], tw, {F_(helvR14_te), F_(helvR12_te), F_(helvR10_te)}),
         col(C_TEXT));
}

void showConnectedScreen() {
  infoTitle = L("WLAN verbunden", "Wi-Fi connected");
  refreshDisplay(drawInfoScreen);
  infoTitle = "";
}

// ---------------------------------------------------------------------------
// Vorschau als BMP (4 Bit, 800 x 480, von oben nach unten), streifenweise gerechnet
// ---------------------------------------------------------------------------
static void put16(uint8_t *p, uint16_t v) { p[0] = v, p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }

static const uint32_t BMP_HEADER = 14 + 40 + 16 * 4;
uint32_t previewBmpSize() { return BMP_HEADER + (uint32_t)(W / 2) * H; }

void streamPreviewBmp(WiFiClient &client, const Layout *preview, void (*painter)()) {
  if (!painter) painter = drawDashboard;
  uint8_t hdr[BMP_HEADER] = {0};
  hdr[0] = 'B', hdr[1] = 'M';
  const uint32_t BMP_ROW = W / 2;
  put32(hdr + 2, previewBmpSize());
  put32(hdr + 10, BMP_HEADER);
  put32(hdr + 14, 40);
  put32(hdr + 18, W);
  put32(hdr + 22, (uint32_t)-H);  // negativ = von oben nach unten
  put16(hdr + 26, 1);
  put16(hdr + 28, 4);
  put32(hdr + 34, BMP_ROW * H);
  put32(hdr + 46, 16);
  // Palette (BGRA) in Farbtoenen nahe am echten Panel
  const uint32_t pal[4] = {0x1a1a1a, 0xf4f2ec, 0xe8c400, 0xc8261e};
  for (int i = 0; i < 4; ++i) {
    uint8_t *p = hdr + 54 + i * 4;
    p[0] = pal[i] & 0xff, p[1] = (pal[i] >> 8) & 0xff, p[2] = pal[i] >> 16;
  }
  client.write(hdr, sizeof hdr);

  Layout saved = layout;
  if (preview) layout = *preview;
  gfx = &canvas;
  bindU8g2(canvas);
  // groesserer Streifen = weniger Durchgaenge (24 KB, nur waehrend der Vorschau belegt)
  const size_t bigCap = (size_t)W * 120 / 4 * (portrait() ? 5 : 3) / 3;  // 120 bzw. 200 Zeilen
  uint8_t *big = (uint8_t *)malloc(bigCap);
  if (big) canvas.buf = big, canvas.cap = bigCap;
  // Zeilen sammeln und in Bloecken von ca. 4 KB senden statt Zeile fuer Zeile
  static uint8_t out[4000];
  size_t used = 0;
  canvas.setWidth(W);
  for (int band = 0; band * canvas.BH < H; ++band) {
    canvas.y0 = band * canvas.BH;
    painter();
    for (int r = 0; r < canvas.BH && canvas.y0 + r < H; ++r) {
      if (used + BMP_ROW > sizeof out) client.write(out, used), used = 0;
      uint8_t *row = out + used;
      for (int x = 0; x < W; x += 2) row[x / 2] = (canvas.get(x, r) << 4) | canvas.get(x + 1, r);
      used += BMP_ROW;
    }
    if (!client.connected()) break;
  }
  if (used) client.write(out, used);
  canvas.buf = canvas.fixed, canvas.cap = BandCanvas::BUF;
  free(big);
  gfx = &display;
  bindU8g2(display);
  layout = saved;
}

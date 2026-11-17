// Gemeinsame Deklarationen aller Module des Timechain Terminals.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <time.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Sprache
// ---------------------------------------------------------------------------
enum Lang : uint8_t { LANG_DE = 0, LANG_EN = 1 };
extern Lang lang;
inline const char *L(const char *de, const char *en) { return lang == LANG_EN ? en : de; }

// ---------------------------------------------------------------------------
// Waehrungen (wie /api/v1/prices von mempool.space)
// ---------------------------------------------------------------------------
static const int NUM_CUR = 7;
static const uint8_t CUR_NONE = 255;
struct Currency {
  const char *code;          // "USD", JSON-Schluessel und API-Parameter
  const char *sym;           // "$"
  const char *de, *en;       // Name unter der Moscow Time
  bool symFirstEn;           // Englisch: Symbol vor der Zahl ($64,000) statt dahinter
};
extern const Currency CURRENCIES[NUM_CUR];

// ---------------------------------------------------------------------------
// Farben: jede Rolle bekommt eine der Panel-Farben
//   k = Schwarz, w = Weiss, y = Gelb, r = Rot, o = Orange (Rot/Gelb gemischt, fuer Flaechen)
// ---------------------------------------------------------------------------
enum ColorRole : uint8_t {
  C_BG, C_TEXT, C_TITLE, C_BAR, C_BAR_TEXT, C_BORDER, C_HIGHLIGHT, C_HL_TEXT,
  C_HEAD_BG, C_HEAD_TEXT, C_PRICE, C_LOGO, C_LOGO_SYM,
  C_MINED_BG, C_MINED_TEXT, C_MINED_HEIGHT, C_MP_BG, C_MP_TEXT,
  C_CHART_FILL, C_CHART_LINE, C_ACCENT, C_ROLE_COUNT
};
struct RoleInfo { const char *key, *de, *en; char def; };
extern const RoleInfo ROLES[C_ROLE_COUNT];
String defaultTheme();
bool validTheme(const String &t);

// ---------------------------------------------------------------------------
// Layout: Widgets auf einem Raster von 20 x 12 Feldern unter der Kopfzeile
// ---------------------------------------------------------------------------
enum WidgetId : uint8_t {
  W_BLOCK, W_MOSCOW, W_FEES, W_CHAIN, W_MEMPOOL, W_HASHRATE, W_DIFFICULTY,
  W_HALVING, W_LIGHTNING, W_CHART, W_MINER, W_MINER1, W_MINER2, W_MINER3, W_MINER4, W_HISTORY, W_COUNT
};
// Querformat: 20 x 12 Felder, Hochformat: 12 x 20 Felder (siehe gridCols()/gridRows())
static const int GRID_MAX = 20;

struct WidgetInfo {
  const char *key;          // im Layout-Text, z.B. "block"
  const char *de, *en;      // Anzeigename im Editor
  uint8_t minW, minH;       // Mindestgroesse in Rasterfeldern
  uint8_t maxW, maxH;       // Hoechstgroesse
  uint8_t defW, defH;       // Groesse beim Hinzufuegen
};
extern const WidgetInfo WIDGETS[W_COUNT];

struct Slot { uint8_t id, x, y, w, h; };
struct Layout {
  uint8_t n = 0;
  Slot s[W_COUNT];
  bool has(WidgetId id) const;
  bool hasMiner() const;
};
extern const char *DEFAULT_LAYOUT;     // Querformat
extern const char *DEFAULT_LAYOUT_P;   // Hochformat
bool parseLayout(const String &text, Layout &out, String *err = nullptr);
String layoutToString(const Layout &l);

// Teile einer Kachel (was sie zeigt, in welcher Prioritaet), siehe parts.cpp
struct PartInfo { const char *key, *de, *en; };
struct PartGroup { const char *key, *de, *en; const PartInfo *parts; uint8_t n; const char *fixDe, *fixEn; };
extern const PartGroup PART_GROUPS[];
extern const int N_PART_GROUPS;
int partGroupOf(WidgetId id);
bool parseParts(const String &text);
String partsToString();
int enabledParts(WidgetId id, uint8_t *out, int max);
bool partEnabled(WidgetId id, int part);

// ---------------------------------------------------------------------------
// Einstellungen (NVS)
// ---------------------------------------------------------------------------
struct Settings {
  String nodeUrl = DEFAULT_NODE_URL;
  String password;              // HA1 = md5("admin:" AUTH_REALM ":" Passwort), leer = Webseite ohne Passwort
  String layout;                // Layout-Text Querformat
  String layoutP;               // Layout-Text Hochformat
  uint8_t rotation = 0;         // 0 = quer, 1 = hoch (90°), 2 = quer gedreht (180°), 3 = hoch (270°)
  bool headerQr = false;        // QR-Code zur Webseite rechts in der Kopfzeile
  String miners;                // IP-Adressen/Namen, durch Komma getrennt
  String theme;                 // eine Farbe je Rolle, siehe ColorRole
  String parts;                 // Teile je Kachel, siehe parts.cpp
  uint16_t chartDays = 7;       // Zeitraum des Preis-Charts in Tagen, 0 = gesamter Verlauf (siehe CHART_PERIODS)
  uint8_t cur1 = 0;             // Hauptwaehrung (Index in CURRENCIES)
  uint8_t cur2 = 1;             // Zweitwaehrung oder CUR_NONE
  bool headerPrice = true;      // Kurs in der Kopfzeile zeigen
};
extern Settings cfg;
extern Layout layout;           // aktives Layout (passend zur Ausrichtung)
bool portrait();
int screenW();                  // 800 bzw. 480
int screenH();
int gridCols();
int gridRows();
String &activeLayoutText();     // cfg.layout oder cfg.layoutP
const char *defaultLayoutText();
void loadSettings();
void saveSettings();
void resetPasswordAndWifi();
// Digest-Anmeldung: Bereich (realm) und daraus der gespeicherte Wert HA1 = md5("admin:realm:passwort")
#define AUTH_REALM PRODUCT_NAME
String digestHA1(const String &password);
bool isHA1(const String &s);

// ---------------------------------------------------------------------------
// Datenquelle
// ---------------------------------------------------------------------------
struct Node {
  String url;
  bool https = true;
  String host;
  uint16_t port = 443;
  String base;
};
extern Node node;
bool parseNodeUrl(String in, Node &out);

// ---------------------------------------------------------------------------
// Daten
// ---------------------------------------------------------------------------
static const int MAX_BLOCKS = 8;
static const int MAX_MP = 8;
static const int MAX_MINERS = 4;
static const int CHART_POINTS = 180;
// Zeitraeume des Preis-Charts (mempool.space: stuendliche Kurse seit 2024, davor woechentlich; USD ab 2010,
// EUR ab 10/2013, GBP/JPY ab 11/2014, CHF ab 12/2019)
struct ChartPeriod { uint16_t days; const char *de, *en, *titleDe, *titleEn; };
static const ChartPeriod CHART_PERIODS[] = {
    {1, "24 Stunden", "24 hours", "24 STD.", "24H"},     {7, "7 Tage", "7 days", "7 TAGE", "7 DAYS"},
    {30, "30 Tage", "30 days", "30 TAGE", "30 DAYS"},    {90, "90 Tage", "90 days", "90 TAGE", "90 DAYS"},
    {365, "1 Jahr", "1 year", "1 JAHR", "1 YEAR"},       {730, "2 Jahre", "2 years", "2 JAHRE", "2 YEARS"},
    {1461, "4 Jahre", "4 years", "4 JAHRE", "4 YEARS"},
    {0, "Gesamter Verlauf (logarithmisch)", "Full history (logarithmic)", "GESAMT", "ALL"}};
static const int N_CHART_PERIODS = sizeof CHART_PERIODS / sizeof CHART_PERIODS[0];
inline const ChartPeriod *chartPeriod(uint16_t days) {
  for (const ChartPeriod &p : CHART_PERIODS)
    if (p.days == days) return &p;
  return nullptr;
}

struct BlockInfo {
  uint32_t height = 0, ts = 0, txCount = 0, size = 0;
  uint64_t reward = 0;
  float medianFee = 0, feeMin = 0, feeMax = 0;
  char pool[32] = "";
  char hash[65] = "";
};

struct MempoolBlock {
  uint32_t nTx = 0;
  float medianFee = 0, feeLo = 0, feeHi = 0;
};

struct MinerInfo {
  bool ok = false;
  char name[24] = "";
  float hashrate = 0;           // GH/s
  float temp = 0, vrTemp = 0, power = 0;
  char bestDiff[16] = "";       // als Text geliefert
  double bestDiffNum = 0;       // als Zahl geliefert
  char sessionDiff[16] = "";
  double sessionDiffNum = 0;
  uint32_t accepted = 0, rejected = 0, uptime = 0;
  uint16_t freq = 0, fanRpm = 0;
  uint8_t fanPct = 0;
  char version[16] = "";        // AxeOS-Version, z.B. "v2.15.3"
  uint8_t repo = 0;             // 0 = unbekannt, 1 = Bitaxe (bitaxeorg/ESP-Miner), 2 = NerdQAxe
};
// Neueste AxeOS-Version laut GitHub fuer die Firmware des Miners ("" = unbekannt)
const char *latestMinerFirmware(uint8_t repo);
bool minerUpdateAvailable(const MinerInfo &m);

struct Data {
  BlockInfo blocks[MAX_BLOCKS];
  int nBlocks = 0;
  MempoolBlock mp[MAX_MP];
  int nMp = 0;
  bool feesOk = false;
  float fees[5] = {0};  // fastest, halfHour, hour, economy, minimum
  bool priceOk = false;
  uint32_t prices[NUM_CUR] = {0};
  bool mempoolOk = false;
  uint32_t mpCount = 0;
  uint64_t mpVsize = 0, mpTotalFee = 0;
  bool diffOk = false;
  float diffProgress = 0, diffChange = 0;
  uint32_t diffRemaining = 0;
  time_t retargetAt = 0;
  bool hashOk = false;
  double hashrate = 0, difficulty = 0;
  bool lnOk = false;
  uint64_t lnCapacity = 0;
  uint32_t lnNodes = 0, lnChannels = 0;
  // Preis-Chart (aelteste zuerst)
  bool chartOk = false;
  uint16_t chartN = 0;
  float chart[CHART_POINTS];
  time_t chartFrom = 0, chartTo = 0;
  uint8_t chartCur = 0;
  uint16_t chartDays = 0xFFFF;
  uint32_t chartFetchedMs = 0;
  // Miner
  int nMiners = 0;
  MinerInfo miners[MAX_MINERS];
  time_t updatedAt = 0;
  bool online = false;
};
extern Data data;

// net.cpp
extern WiFiClientSecure tls;
extern String lastHttpError;
bool fetchAll();
bool fetchBlocks(uint32_t fromHeight = 0);
uint32_t fetchTipHeight();
uint32_t testNode(const Node &n);
void resetConnection();
void stopApiConnection();
// Firmware-Updates aus den GitHub-Releases (FW_REPO): neueste Version ("" = unbekannt)
const char *latestFirmware();
bool firmwareUpdateAvailable();
bool checkFirmwareNow();                 // sofort bei GitHub nachfragen
bool installFirmwareUpdate(String &err);  // laedt das Release und startet bei Erfolg neu

// format.cpp
String fmtInt(uint64_t v);
String fmtDec(double v, int decimals);
String fmtFee(float f);
String fmtAge(uint32_t ts);
String fmtAgeShort(uint32_t ts);
String fmtTime(time_t t, const char *fmt);
String fmtDate(time_t t, bool withYear);
String fmtPrice(uint32_t v, uint8_t cur);
String fmtSI(double v);
String fmtDuration(uint32_t sec);
String moscowTime(uint32_t price);
const char *monthName(int m);

// render.cpp
extern U8G2_FOR_ADAFRUIT_GFX u8g2;
void displayInit();
void refreshDisplay(void (*painter)());
void drawDashboard();
void drawInfoScreen();
void showMessage(const String &title, const String &l1 = "", const String &l2 = "",
                 const String &l3 = "", const String &l4 = "", const String &l5 = "");
void showSetupScreen();
void (*setupScreenPainter())();
void showConnectedScreen();
extern bool epdActive;
void streamPreviewBmp(WiFiClient &client, const Layout *preview, void (*painter)() = nullptr);
uint32_t previewBmpSize();
extern void (*epdBusyHook)();
extern String themeOverride;    // nur fuer Vorschauen, sonst leer
String deviceUrl();             // http://<IP>

// Pruefstand (render.cpp): zeichnet ein Widget unsichtbar und zaehlt, was ueber den Rand geht
struct RenderCheck { uint32_t outside = 0; uint16_t truncs = 0, truncsOk = 0; String sample; };
#ifdef TT_DEVTOOLS
RenderCheck checkWidget(WidgetId id, int w, int h);
#endif
// Zeichnet das Layout unsichtbar: je Widget die gezeigten Teile und die Teile, fuer die es Daten gibt
// chainSplit[2]: Teile der Blockkette, die in geminten [0] bzw. Mempool-Bloecken [1] Platz fanden
void measureParts(const Layout &l, uint16_t *shown, uint16_t *haveData, uint16_t *chainSplit);
// events.cpp: Ereignisse fuer "Heute vor X Jahren" (eingebaut + geaendert + eigene)
struct EventView { uint16_t y; uint8_t m, d; const char *de, *en; };
void eventsBegin();
bool eventsSave(const String &json, String &err);
String eventsJson();
int eventTotal();
bool eventAt(int i, EventView &v);   // false = ausgeblendet
const char *eventText(const EventView &v);
int eventLongestBuiltin();
extern String historyTestText;      // Pruefstand: fester Text statt "heute" (leer = aus)

// test.cpp (nur im Entwickler-Build)
#ifdef TT_DEVTOOLS
void streamOverflowReport(WebServer &server);
void pushTestData();
void popTestData();
#endif

// web.cpp
void startWeb();
void pumpWeb();
extern bool refreshRequested;
extern bool nodeChangePending;
extern Node pendingNode;
extern uint32_t renderCount;     // Anzahl Bildwechsel seit dem Start (fuer die Statusmeldungen der Webseite)
bool redrawPending();             // ein Bildwechsel steht an
extern bool fwInstallPending;     // Ein-Klick-Update angefordert (Hauptschleife installiert)
extern uint32_t fwInstallAt;
extern String fwInstallError;
extern bool settingsPending;     // Sprache/Layout/Farben geaendert -> in loop() uebernehmen
extern Lang pendingLang;
extern Layout pendingLayout;
extern bool infoRequested;       // QR-/Info-Bildschirm zeigen

// main.cpp
void wifiResetAndRestart();
// WLAN-Wechsel ueber die Webseite: neue Verbindung wird erst getestet; scheitert sie, geht es
// zurueck ins bisherige WLAN. Bei Erfolg startet das Geraet neu (mDNS, Uhrzeit, Verbindungen frisch).
enum WifiSwitchState { WS_IDLE, WS_PENDING, WS_FAILED };
void requestWifiSwitch(const String &ssid, const String &pass);
WifiSwitchState wifiSwitchState();
String wifiSwitchTarget();
String wifiSwitchError();

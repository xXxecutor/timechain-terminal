// ===========================================================================
//  Timechain Terminal fuer Good Display GDP075FW1
//  ESP32 + 7,5" 4-Farben-E-Paper (800x480, Schwarz/Weiss/Gelb/Rot)
//
//  Holt per WLAN von mempool.space (oder einem eigenen mempool-Server) Bloecke,
//  Gebuehren, Mempool, Hashrate, Difficulty, Lightning, Kurs und Kursverlauf,
//  optional den Status eigener Miner (AxeOS). Inhalte und Anordnung sind ueber
//  die Geraete-Webseite einstellbar.
//
//  Module: settings.cpp (NVS, Layout), net.cpp (Abruf), render.cpp (Zeichnen),
//          web.cpp (Webseite), format.cpp (Zahlen/Datum, Deutsch/Englisch)
// ===========================================================================
#include "app.h"
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include "certs.h"

// ---------------------------------------------------------------------------
// WLAN
// ---------------------------------------------------------------------------
static bool portalUsed = false;

static void onPortalStart(WiFiManager *) {
  portalUsed = true;
  Serial.println("[wifi] Einrichtungs-Portal gestartet: " WIFI_AP_NAME);
  showSetupScreen();
}

void wifiResetAndRestart() {
  Serial.println("[cmd] WLAN-Daten werden gelöscht, Neustart ...");
  WiFiManager wm;
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

// --- WLAN-Wechsel ueber die Webseite ---
static WifiSwitchState wsState = WS_IDLE;
static String wsSsid, wsPass, wsError;
static uint32_t wsAt = 0;

void requestWifiSwitch(const String &ssid, const String &pass) {
  wsSsid = ssid, wsPass = pass, wsError = "";
  wsState = WS_PENDING;
  wsAt = millis();
}
WifiSwitchState wifiSwitchState() { return wsState; }
String wifiSwitchTarget() { return wsSsid; }
String wifiSwitchError() { return wsError; }

static bool waitConnected(uint32_t ms) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

static void doWifiSwitch() {
  // gespeicherte Konfiguration komplett sichern, um sie bei einem Fehlschlag 1:1 zurueckzuschreiben
  wifi_config_t oldCfg;
  memset(&oldCfg, 0, sizeof oldCfg);
  esp_wifi_get_config(WIFI_IF_STA, &oldCfg);
  const String oldS = WiFi.SSID();
  Serial.printf("[wifi] Wechsel zu \"%s\" ...\n", wsSsid.c_str());
  WiFi.persistent(true);  // bei Erfolg bleiben die neuen Daten gespeichert
  WiFi.disconnect(false, false);
  delay(200);
  WiFi.begin(wsSsid.c_str(), wsPass.length() ? wsPass.c_str() : nullptr);
  if (waitConnected(20000)) {
    Serial.printf("[wifi] verbunden mit \"%s\", IP %s -> Neustart\n", wsSsid.c_str(), WiFi.localIP().toString().c_str());
    delay(300);
    ESP.restart();
  }
  const wl_status_t st = WiFi.status();
  Serial.printf("[wifi] Wechsel fehlgeschlagen (Status %d), zurueck zu \"%s\"\n", (int)st, oldS.c_str());
  wsError = st == WL_NO_SSID_AVAIL
                ? String(L("WLAN »", "Wi-Fi »")) + wsSsid + L("« nicht gefunden (nur 2,4 GHz wird unterstützt).",
                                                         "« not found (only 2.4 GHz is supported).")
                : String(L("Keine Verbindung mit »", "Could not connect to »")) + wsSsid +
                      L("« – Passwort prüfen.", "« – check the password.");
  wsPass = "";
  WiFi.disconnect(false, false);
  delay(200);
  esp_wifi_set_config(WIFI_IF_STA, &oldCfg);  // stellt auch die gespeicherten Daten wieder her
  WiFi.begin();
  if (!waitConnected(20000)) {
    Serial.println("[wifi] altes WLAN nicht erreichbar -> Neustart");
    ESP.restart();
  }
  Serial.printf("[wifi] wieder mit \"%s\" verbunden, IP %s\n", oldS.c_str(), WiFi.localIP().toString().c_str());
  wsState = WS_FAILED;
}

// Sprachauswahl im Portal: sichtbares <select>, Wert landet im versteckten Feld "lang"
static const char LANG_SELECT[] =
    "<label for='langsel'>Sprache / Language</label>"
    "<select id='langsel' onchange=\"document.getElementById('lang').value=this.value\" "
    "style='width:100%;padding:5px;margin:5px 0 10px'>"
    "<option value='de'>Deutsch</option><option value='en'>English</option></select>"
    "<script>document.getElementById('langsel').value=document.getElementById('lang')?"
    "document.getElementById('lang').value:'de'</script>";

static void setupWifi() {
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  WiFiManager wm;
  if (digitalRead(WIFI_RESET_PIN) == LOW) {
    Serial.println("[wifi] BOOT-Taste gedrückt -> WLAN-Daten und Passwort löschen");
    resetPasswordAndWifi();
    wm.resetSettings();
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  wm.setTitle(PRODUCT_NAME);
  wm.setHostname(WIFI_HOSTNAME);
  wm.setAPCallback(onPortalStart);
  wm.setConnectTimeout(30);
  wm.setConnectRetries(3);
  // Optional schon bei der Einrichtung: Sprache und eigener mempool-Server
  WiFiManagerParameter pLang("lang", "", lang == LANG_EN ? "en" : "de", 3, "type='hidden'");
  WiFiManagerParameter pLangSel(LANG_SELECT);
  WiFiManagerParameter pNode("node", "mempool-Server (leer = mempool.space, eigener Node z.B. "
                                     "http://umbrel.local:3006)",
                             node.url.c_str(), 100);
  wm.addParameter(&pLang);
  wm.addParameter(&pLangSel);
  wm.addParameter(&pNode);
  // Mit gespeicherten Daten nur 5 Min. Portal (z.B. Router kurz aus), dann Neustart.
  wm.setConfigPortalTimeout(wm.getWiFiIsSaved() ? 300 : 0);
  if (!wm.autoConnect(WIFI_AP_NAME)) {
    Serial.println("[wifi] Keine Verbindung -> Neustart");
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  Serial.printf("[wifi] verbunden: %s, IP %s, RSSI %d dBm\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (portalUsed) {
    lang = String(pLang.getValue()) == "en" ? LANG_EN : LANG_DE;
    Node n;
    if (parseNodeUrl(pNode.getValue(), n)) node = n;
    cfg.nodeUrl = node.url;
    saveSettings();
    Serial.printf("[cfg] Sprache %s, Datenquelle %s\n", lang == LANG_EN ? "en" : "de", node.url.c_str());
  }
}

static void syncTime() {
  configTzTime(TZ_INFO, "pool.ntp.org", "time.cloudflare.com", "ptbtime1.ptb.de");
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 15000) delay(200);
  Serial.printf("[time] %s\n", fmtTime(time(nullptr), "%d.%m.%Y %H:%M:%S").c_str());
}

// delay(), bei dem die Webseite weiter antwortet
static void waitMs(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    pumpWeb();
    delay(5);
  }
}

// Serielle Befehle (115200 Baud): "wifi-reset", "refresh", "info", "factory-reset"
static void handleSerial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line == "wifi-reset") {
        wifiResetAndRestart();
      } else if (line == "factory-reset") {
        Serial.println("[cmd] Alle Einstellungen werden gelöscht");
        cfg = Settings();
        cfg.layout = DEFAULT_LAYOUT;
        cfg.layoutP = DEFAULT_LAYOUT_P;
        cfg.theme = defaultTheme();
        lang = LANG_DE;
        saveSettings();
        wifiResetAndRestart();
      } else if (line == "password-reset") {
        cfg.password = "";
        saveSettings();
        Serial.println("[cmd] Passwort der Webseite entfernt");
      } else if (line == "refresh") {
        refreshRequested = true;
      } else if (line == "info") {
        Serial.printf("[info] Firmware %s, Quelle %s, IP %s, Heap %u, Passwort %s\n[info] Layout %s\n",
                      FW_VERSION, node.url.c_str(), WiFi.localIP().toString().c_str(), ESP.getFreeHeap(),
                      cfg.password.length() ? "ja" : "nein", activeLayoutText().c_str());
      } else if (line.length()) {
        Serial.println("[cmd] Befehle: refresh | info | password-reset | wifi-reset | factory-reset");
      }
      line = "";
    } else if (line.length() < 32) {
      line += c;
    }
  }
}

// ---------------------------------------------------------------------------
// Ablauf
// ---------------------------------------------------------------------------
static uint32_t lastRenderMs = 0;
static uint32_t lastTipPollMs = 0;
static bool renderedOffline = false;
static bool newBlockPending = false;
static uint32_t pendingTip = 0;  // zuletzt gemeldete Blockhoehe

// BOOT-Taste (GPIO0) im Betrieb: kurz druecken zeigt den QR-/Info-Bildschirm, nochmal druecken
// kehrt zurueck (sonst nach 3 Minuten). Beim Einschalten gedrueckt halten = WLAN/Passwort loeschen.
static bool infoShown = false;
static uint32_t infoSinceMs = 0;
static const uint32_t INFO_MS = 180000;

// Zweimal RESET innerhalb von 10 s: QR-Code zur Webseite zeigen. Beim Start wird ein Merker im
// NVS gesetzt und nach 10 s wieder geloescht; findet der naechste Start ihn noch, war es ein Doppel-Reset.
// (Die Taste am GDP075FW1 ist offenbar RESET/EN, nicht BOOT/GPIO0.)
bool redrawPending() {
  return refreshRequested || newBlockPending || settingsPending || nodeChangePending || infoRequested;
}

static bool checkDoubleReset() {
  esp_reset_reason_t r = esp_reset_reason();
  bool byButton = r == ESP_RST_POWERON || r == ESP_RST_EXT;
  Preferences p;
  p.begin("btcterm", false);
  bool armed = p.getBool("dr", false);
  p.putBool("dr", byButton);
  p.end();
  if (byButton)
    xTaskCreate(
        [](void *) {
          vTaskDelay(pdMS_TO_TICKS(10000));
          Preferences q;
          q.begin("btcterm", false);
          q.putBool("dr", false);
          q.end();
          vTaskDelete(nullptr);
        },
        "dr", 3072, nullptr, 1, nullptr);
  return byButton && armed;
}

static bool bootPressed() {
  static bool last = true;
  static uint32_t lastChange = 0;
  bool now = digitalRead(WIFI_RESET_PIN);  // HIGH = losgelassen
  if (now != last && millis() - lastChange > 60) {
    lastChange = millis();
    last = now;
    return !now;
  }
  return false;
}

static void updateAndRender() {
  bool ok = fetchAll();
  // Die Blockliste von mempool.space haengt der Tip-Abfrage manchmal ein paar
  // Sekunden hinterher. Dann kurz warten statt den alten Block neu zu zeichnen.
  for (int i = 0; ok && pendingTip && data.nBlocks && data.blocks[0].height < pendingTip && i < 10; ++i) {
    Serial.printf("[data] Blockliste noch bei %u, warte auf %u ...\n", data.blocks[0].height, pendingTip);
    waitMs(1500);
    stopApiConnection();  // neue Verbindung -> ggf. anderer, aktuellerer mempool-Server
    BlockInfo saved[MAX_BLOCKS];
    memcpy(saved, data.blocks, sizeof saved);
    int savedN = data.nBlocks;
    // Antwortet der Server mit aelteren Bloecken oder Fehler, alten Stand behalten
    if (!fetchBlocks(pendingTip) || data.blocks[0].height != pendingTip) {
      memcpy(data.blocks, saved, sizeof saved);
      data.nBlocks = savedN;
    }
  }
  if (pendingTip && data.nBlocks)
    Serial.printf("[data] zeichne Block %u (gemeldet: %u)\n", data.blocks[0].height, pendingTip);
  if (!ok && renderedOffline) return;  // Offline-Bild steht schon, nicht nochmal blinken
  refreshDisplay(drawDashboard);
  lastRenderMs = millis();
  renderedOffline = !ok;
  if (ok) newBlockPending = false;
}

// Neue Datenquelle uebernehmen: Verbindung neu aufbauen, alte Werte verwerfen
static void applyNodeChange() {
  nodeChangePending = false;
  resetConnection();
  node = pendingNode;
  data = Data();
  newBlockPending = false;
  pendingTip = 0;
  renderedOffline = false;
  refreshRequested = true;
}

// Sprache, Layout, Chart- und Miner-Einstellungen uebernehmen
static void applySettings() {
  settingsPending = false;
  lang = pendingLang;
  layout = pendingLayout;
  refreshRequested = true;
}

void setup() {
  // ganz zuerst: Merker fuer den Doppel-Reset setzen (je frueher, desto kuerzer darf der Abstand sein)
  const bool doubleReset = checkDoubleReset();
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== " PRODUCT_NAME " " FW_VERSION " (GDP075FW1) ===");
  if (doubleReset) Serial.println("[info] Doppel-Reset erkannt -> QR-Code");
  loadSettings();
  eventsBegin();  // eigene und geaenderte Ereignisse aus dem Dateisystem
  Serial.printf("[cfg] Datenquelle %s, Sprache %s, Passwort %s\n", node.url.c_str(), lang == LANG_EN ? "en" : "de",
                cfg.password.length() ? "ja" : "nein");
  // Waehrend des ca. 27 s langen Bildwechsels werden KEINE Web-Anfragen bearbeitet:
  // Panel-Spannung plus starker WLAN-Verkehr (Upload, Vorschau) fuehrten im Test an
  // einem PC-USB-Anschluss zu einem Spannungseinbruch und Neustart. Anfragen warten
  // in der Zwischenzeit und werden danach beantwortet.
  epdBusyHook = nullptr;
  displayInit();

  setupWifi();
  startWeb();
  syncTime();
  tls.setCACert(ROOT_CA_BUNDLE);
  if (portalUsed) {
    // Nach der Ersteinrichtung zeigen, wo die Einstellungen zu finden sind
    // mit QR-Code; eine Minute stehen lassen, dann kommen die Bitcoin-Daten
    showConnectedScreen();
    waitMs(60000);
  }
  lastTipPollMs = millis();
  if (doubleReset && !portalUsed) {
    // QR-Bildschirm; die Bitcoin-Daten kommen nach 3 Minuten (oder nach "neu zeichnen")
    infoShown = true;
    infoSinceMs = millis();
    refreshDisplay(drawInfoScreen);
    lastRenderMs = millis();
    return;
  }
  updateAndRender();
}

void loop() {
  handleSerial();
  pumpWeb();
  // kurz warten, damit die Antwortseite noch beim Browser ankommt
  if (wsState == WS_PENDING && millis() - wsAt > 1500) doWifiSwitch();
  if (nodeChangePending) applyNodeChange();
  if (settingsPending) applySettings();
  if (bootPressed()) infoRequested = true;
  if (infoShown && refreshRequested) infoShown = false;  // "neu zeichnen" beendet den Info-Bildschirm
  if (infoRequested) {
    infoRequested = false;
    if (infoShown) {
      infoShown = false;
      refreshRequested = true;
    } else {
      Serial.println("[info] QR-Bildschirm (BOOT-Taste)");
      infoShown = true;
      infoSinceMs = millis();
      refreshDisplay(drawInfoScreen);
      lastRenderMs = millis();
    }
  }
  if (infoShown && millis() - infoSinceMs >= INFO_MS) {
    infoShown = false;
    refreshRequested = true;
  }
  uint32_t now = millis();

  if (now - lastTipPollMs >= TIP_POLL_SECONDS * 1000UL) {
    lastTipPollMs = now;
    if (WiFi.status() == WL_CONNECTED) {
      uint32_t t0 = millis();
      uint32_t tip = fetchTipHeight();
      static int logged = 0;
      if (logged < 3) {  // die ersten Abfragen protokollieren (Keep-Alive-Kontrolle)
        ++logged;
        Serial.printf("[tip] %u in %lu ms\n", tip, millis() - t0);
      }
      if (tip && data.nBlocks && tip > data.blocks[0].height) {
        if (!newBlockPending) Serial.printf("[data] Neuer Block %u\n", tip);
        newBlockPending = true;
        pendingTip = tip;
      }
    }
  }

  uint32_t sinceRender = now - lastRenderMs;
  bool periodic = sinceRender >= PERIODIC_REFRESH_MIN * 60000UL;
  bool retry = renderedOffline && sinceRender >= RETRY_SECONDS * 1000UL;
  bool allowed = sinceRender >= MIN_REFRESH_SECONDS * 1000UL;
  // Neue Bloecke werden sofort gezeichnet; nur der periodische Refresh
  // (Gebuehren/Kurs) respektiert die Mindestpause.
  // Waehrend der Info-Bildschirm steht, wird nicht neu gezeichnet; neue Bloecke kommen danach.
  if (!infoShown && (refreshRequested || newBlockPending || (allowed && periodic) || retry)) {
    refreshRequested = false;
    updateAndRender();
    if (renderedOffline) lastRenderMs = millis();  // Retry-Takt
  }
  delay(20);
}

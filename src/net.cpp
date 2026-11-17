// Datenabruf: mempool-API (Keep-Alive), Preis-Chart (Stream), Miner im Heimnetz (AxeOS)
#include "app.h"
#include <HTTPUpdate.h>
#include <ESPmDNS.h>
#include "certs.h"

Data data;
WiFiClientSecure tls;   // HTTPS (mempool.space, oeffentliche Instanzen)
static WiFiClient plain;  // HTTP (eigener Node im Heimnetz)
String lastHttpError;

// Eine dauerhafte Verbindung (Keep-Alive) fuer alle Anfragen:
// der teure TLS-Handshake faellt nur einmal an, nicht bei jeder Abfrage.
static HTTPClient api;
static IPAddress localNodeIp;  // per mDNS aufgeloeste IP eines .local-Servers

static WiFiClient &apiClient() { return node.https ? (WiFiClient &)tls : plain; }

void stopApiConnection() { apiClient().stop(); }

void resetConnection() {
  api.end();
  tls.stop();
  plain.stop();
  localNodeIp = IPAddress();
}

// .local-Namen loest der normale DNS nicht auf, deshalb per mDNS nachschlagen.
static String resolveHost(const String &host, IPAddress &cachedIp) {
  if (!host.endsWith(".local")) return host;
  if (cachedIp == IPAddress()) {
    cachedIp = MDNS.queryHost(host.substring(0, host.length() - 6), 2000);
    if (cachedIp == IPAddress()) {
      lastHttpError = host + L(" nicht gefunden (mDNS)", " not found (mDNS)");
      Serial.printf("[http] %s\n", lastHttpError.c_str());
      return "";
    }
    Serial.printf("[http] %s -> %s\n", host.c_str(), cachedIp.toString().c_str());
  }
  return cachedIp.toString();
}

static String nodeUrl(const Node &n, IPAddress &cachedIp, const char *path) {
  String host = resolveHost(n.host, cachedIp);
  if (!host.length()) return "";
  String url = String(n.https ? "https://" : "http://") + host;
  if (n.port != (n.https ? 443 : 80)) url += ":" + String(n.port);
  return url + n.base + path;
}

static void setupHttp(HTTPClient &http, bool reuse, uint16_t timeoutMs = 15000) {
  http.setReuse(reuse);
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs < 10000 ? timeoutMs : 10000);
  http.setUserAgent("TimechainTerminal/" FW_VERSION " (ESP32 e-paper)");
}

static int httpError(HTTPClient &http, WiFiClient &client, const String &what, int code) {
  lastHttpError = what + " -> " + (code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code));
  Serial.printf("[http] %s\n", lastHttpError.c_str());
  http.end();
  if (code < 0) client.stop();
  return code;
}

static bool requestBody(HTTPClient &http, WiFiClient &client, const Node &n, IPAddress &cachedIp,
                        bool reuse, const char *path, String &body) {
  setupHttp(http, reuse);
  String url = nodeUrl(n, cachedIp, path);
  if (!url.length()) return false;
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    httpError(http, client, path, code);
    if (code < 0) cachedIp = IPAddress();
    return false;
  }
  body = http.getString();  // loest auch chunked encoding auf
  http.end();               // Verbindung bleibt offen (Keep-Alive)
  return body.length() > 0;
}

static bool httpGet(const char *path, String &body) {
  return requestBody(api, apiClient(), node, localNodeIp, true, path, body);
}

static bool getJson(const char *path, JsonDocument &doc, JsonDocument *filter = nullptr) {
  String body;
  if (!httpGet(path, body)) return false;
  DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                    : deserializeJson(doc, body);
  if (err) {
    Serial.printf("[json] %s -> %s\n", path, err.c_str());
    return false;
  }
  return true;
}

// Prueft einen (neuen) Server mit eigener Verbindung, ohne die laufende zu stoeren.
uint32_t testNode(const Node &n) {
  WiFiClientSecure t;
  WiFiClient p;
  HTTPClient h;
  IPAddress ip;
  if (n.https) t.setCACert(ROOT_CA_BUNDLE);
  String body;
  if (!requestBody(h, n.https ? (WiFiClient &)t : p, n, ip, false, "/api/blocks/tip/height", body))
    return 0;
  uint32_t tip = (uint32_t)body.toInt();
  if (!tip) lastHttpError = L("Antwort ist keine Blockhöhe – ist das ein mempool-Server?",
                              "Response is not a block height – is this a mempool server?");
  return tip;
}

uint32_t fetchTipHeight() {
  String body;
  if (!httpGet("/api/blocks/tip/height", body)) return 0;
  return (uint32_t)body.toInt();
}

// Spanne wie auf mempool.space (blockchain-blocks.component.ts, getMin/MaxBlockFee):
// ist feeRange schon auf effektive Raten korrigiert (median == feeRange[3]),
// gilt feeRange[1] als Minimum, sonst feeRange[0]; Maximum ist der letzte Wert.
static void blockFeeSpan(JsonArray fr, float median, float &lo, float &hi) {
  lo = hi = 0;
  if (!fr.size()) return;
  bool effective = fr.size() > 3 && fabsf(fr[3].as<float>() - median) < 1e-6f;
  lo = (effective && fr.size() > 1) ? fr[1].as<float>() : fr[0].as<float>();
  hi = fr[fr.size() - 1].as<float>();
}

bool fetchBlocks(uint32_t fromHeight) {
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["height"] = true;
  filter[0]["timestamp"] = true;
  filter[0]["tx_count"] = true;
  filter[0]["size"] = true;
  filter[0]["extras"]["medianFee"] = true;
  filter[0]["extras"]["feeRange"] = true;
  filter[0]["extras"]["reward"] = true;
  filter[0]["extras"]["pool"]["name"] = true;
  JsonDocument doc;
  String path = "/api/v1/blocks";
  if (fromHeight) path += "/" + String(fromHeight);
  if (!getJson(path.c_str(), doc, &filter)) return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return false;
  int n = 0;
  for (JsonObject b : arr) {
    if (n >= MAX_BLOCKS) break;
    BlockInfo &bi = data.blocks[n++];
    bi.height = b["height"] | 0;
    bi.ts = b["timestamp"] | 0;
    bi.txCount = b["tx_count"] | 0;
    bi.size = b["size"] | 0;
    strlcpy(bi.hash, b["id"] | "", sizeof bi.hash);
    JsonObject ex = b["extras"];
    bi.reward = ex["reward"].as<uint64_t>();
    bi.medianFee = ex["medianFee"] | 0.0f;
    blockFeeSpan(ex["feeRange"], bi.medianFee, bi.feeMin, bi.feeMax);
    strlcpy(bi.pool, ex["pool"]["name"] | "?", sizeof bi.pool);
  }
  data.nBlocks = n;
  return true;
}

static bool fetchFees() {
  JsonDocument doc;
  if (!getJson("/api/v1/fees/precise", doc)) return false;
  data.fees[0] = doc["fastestFee"] | 0.0f;
  data.fees[1] = doc["halfHourFee"] | 0.0f;
  data.fees[2] = doc["hourFee"] | 0.0f;
  data.fees[3] = doc["economyFee"] | 0.0f;
  data.fees[4] = doc["minimumFee"] | 0.0f;
  data.feesOk = true;
  return true;
}

static bool fetchMempoolBlocks() {
  JsonDocument filter;
  filter[0]["nTx"] = true;
  filter[0]["medianFee"] = true;
  filter[0]["feeRange"] = true;
  JsonDocument doc;
  if (!getJson("/api/v1/fees/mempool-blocks", doc, &filter)) return false;
  int n = 0;
  for (JsonObject b : doc.as<JsonArray>()) {
    if (n >= MAX_MP) break;
    MempoolBlock &m = data.mp[n++];
    m.nTx = b["nTx"] | 0;
    m.medianFee = b["medianFee"] | 0.0f;
    JsonArray fr = b["feeRange"];
    m.feeLo = fr.size() ? fr[0].as<float>() : 0;
    m.feeHi = fr.size() ? fr[fr.size() - 1].as<float>() : 0;
  }
  data.nMp = n;
  return true;
}

static bool fetchMempool() {
  JsonDocument filter;
  filter["count"] = true;
  filter["vsize"] = true;
  filter["total_fee"] = true;
  JsonDocument doc;
  if (!getJson("/api/mempool", doc, &filter)) return false;
  data.mpCount = doc["count"] | 0;
  data.mpVsize = doc["vsize"].as<uint64_t>();
  data.mpTotalFee = doc["total_fee"].as<uint64_t>();
  data.mempoolOk = true;
  return true;
}

static bool fetchPrices() {
  JsonDocument filter;
  for (const Currency &c : CURRENCIES) filter[c.code] = true;
  JsonDocument doc;
  if (!getJson("/api/v1/prices", doc, &filter)) return false;
  for (int i = 0; i < NUM_CUR; ++i) data.prices[i] = doc[CURRENCIES[i].code] | 0;
  data.priceOk = data.prices[cfg.cur1] > 0;
  return true;
}

static bool fetchDifficulty() {
  JsonDocument doc;
  if (!getJson("/api/v1/difficulty-adjustment", doc)) return false;
  data.diffProgress = doc["progressPercent"] | 0.0f;
  data.diffChange = doc["difficultyChange"] | 0.0f;
  data.diffRemaining = doc["remainingBlocks"] | 0;
  data.retargetAt = (time_t)(doc["estimatedRetargetDate"].as<double>() / 1000.0);
  data.diffOk = true;
  return true;
}

static bool fetchHashrate() {
  JsonDocument filter;
  filter["currentHashrate"] = true;
  filter["currentDifficulty"] = true;
  JsonDocument doc;
  if (!getJson("/api/v1/mining/hashrate/3d", doc, &filter)) return false;
  data.hashrate = doc["currentHashrate"] | 0.0;
  data.difficulty = doc["currentDifficulty"] | 0.0;
  data.hashOk = data.hashrate > 0;
  return true;
}

static bool fetchLightning() {
  JsonDocument filter;
  filter["latest"]["total_capacity"] = true;
  filter["latest"]["node_count"] = true;
  filter["latest"]["channel_count"] = true;
  JsonDocument doc;
  if (!getJson("/api/v1/lightning/statistics/latest", doc, &filter)) return false;
  JsonObject l = doc["latest"];
  data.lnCapacity = l["total_capacity"].as<uint64_t>();
  data.lnNodes = l["node_count"] | 0;
  data.lnChannels = l["channel_count"] | 0;
  data.lnOk = data.lnCapacity > 0;
  return true;
}

// ---------------------------------------------------------------------------
// Preis-Chart: /api/v1/historical-price liefert alle Kurse, neueste zuerst (stuendlich seit 2024,
// davor woechentlich bis 2010), insgesamt ueber 1,5 MB. Die Kurse werden beim Lesen nach Zeit
// in CHART_POINTS Abschnitte gemittelt; sobald der Zeitraum abgedeckt ist, wird die Verbindung
// beendet. HTTP/1.0, damit der Body nicht "chunked" kommt.
// ---------------------------------------------------------------------------
static const time_t CHART_EPOCH = 1279497600;  // 19.07.2010, erster Kurs bei mempool.space (USD)
static float binSum[CHART_POINTS];
static uint16_t binCnt[CHART_POINTS];

static bool fetchChart() {
  const uint8_t cur = cfg.cur1;
  const char *curCode = CURRENCIES[cur].code;
  const uint16_t days = cfg.chartDays;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // Uhrzeit noch nicht gestellt
  const time_t from = days ? now - (time_t)days * 86400 : CHART_EPOCH;
  WiFiClientSecure t;
  WiFiClient p;
  HTTPClient h;
  IPAddress ip;
  if (node.https) t.setCACert(ROOT_CA_BUNDLE);
  WiFiClient &client = node.https ? (WiFiClient &)t : p;
  setupHttp(h, false);
  h.useHTTP10(true);
  String path = String("/api/v1/historical-price?currency=") + curCode;
  String url = nodeUrl(node, ip, path.c_str());
  if (!url.length() || !h.begin(client, url)) return false;
  int code = h.GET();
  if (code != 200) return httpError(h, client, "historical-price", code), false;

  memset(binSum, 0, sizeof binSum);
  memset(binCnt, 0, sizeof binCnt);
  WiFiClient *s = h.getStreamPtr();
  String keyS = String("\"") + curCode + "\":";
  const char *key = keyS.c_str();
  const int keyLen = keyS.length();
  char obj[200];
  int objLen = -1;  // -1: ausserhalb eines Objekts
  int depth = 0, n = 0;
  time_t newest = 0, oldest = 0;
  bool done = false;
  uint32_t t0 = millis();
  while (!done && millis() - t0 < 60000 && (s->connected() || s->available())) {
    int c = s->read();
    if (c < 0) {
      delay(2);
      continue;
    }
    if (c == '{') {
      if (++depth == 2) objLen = 0;  // {"prices":[ {...}, ... ]}
      continue;
    }
    if (c == '}') {
      if (depth-- == 2 && objLen > 0) {
        obj[objLen] = 0;
        char *tp = strstr(obj, "\"time\":");
        char *vp = strstr(obj, key);
        if (tp && vp) {
          time_t tt = (time_t)atol(tp + 7);
          float v = atof(vp + keyLen);
          if (tt < from) {
            done = true;  // Zeitraum abgedeckt
          } else if (v > 0) {
            if (!newest) newest = tt;
            oldest = tt;
            long bi = (long)((double)(tt - from) * CHART_POINTS / (double)(now - from));
            bi = constrain(bi, 0L, (long)CHART_POINTS - 1);
            binSum[bi] += v;
            ++binCnt[bi];
            ++n;
          }
        }
      }
      objLen = -1;
      if (depth <= 0) break;
      continue;
    }
    if (objLen >= 0 && objLen < (int)sizeof obj - 1) obj[objLen++] = (char)c;
  }
  h.end();
  client.stop();
  // Abschnitte von den aeltesten mit Daten bis zum neuesten; Luecken (woechentliche Kurse)
  // werden aus den Nachbarn gefuellt
  int first = 0;
  while (first < CHART_POINTS && !binCnt[first]) ++first;
  int m = 0;
  for (int i = first; i < CHART_POINTS; ++i) {
    if (binCnt[i]) {
      data.chart[m++] = binSum[i] / binCnt[i];
      continue;
    }
    int j = i + 1;
    while (j < CHART_POINTS && !binCnt[j]) ++j;
    float prev = m ? data.chart[m - 1] : 0, next = j < CHART_POINTS ? binSum[j] / binCnt[j] : prev;
    data.chart[m++] = prev + (next - prev) / (j - i + 1);
  }
  if (n < 2 || m < 2) {
    lastHttpError = "historical-price: keine Daten";
    return false;
  }
  data.chartN = m;
  data.chartFrom = oldest;
  data.chartTo = newest;
  data.chartCur = cur;
  data.chartDays = days;
  data.chartOk = true;
  data.chartFetchedMs = millis();
  Serial.printf("[chart] %d Kurse (%u Tage, %s) -> %d Punkte in %lu ms\n", n, days, curCode, m, millis() - t0);
  return true;
}

// ---------------------------------------------------------------------------
// Miner mit AxeOS (Bitaxe, NerdQaxe ...): GET http://<ip>/api/system/info
// ---------------------------------------------------------------------------
static void fetchMiner(const String &addr, MinerInfo &mi) {
  mi = MinerInfo();
  strlcpy(mi.name, addr.c_str(), sizeof mi.name);
  IPAddress ip;
  String host = resolveHost(addr, ip);
  if (!host.length()) return;
  WiFiClient c;
  HTTPClient h;
  setupHttp(h, false, 4000);
  if (!h.begin(c, "http://" + host + "/api/system/info")) return;
  int code = h.GET();
  if (code != 200) {
    httpError(h, c, "Miner " + addr, code);
    return;
  }
  JsonDocument filter;
  for (const char *k : {"hostname", "hashRate", "temp", "vrTemp", "power", "bestDiff", "bestSessionDiff",
                        "sharesAccepted", "sharesRejected", "uptimeSeconds", "frequency", "fanrpm", "fanspeed",
                        "version", "deviceModel", "ASICModel"})
    filter[k] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, h.getStream(), DeserializationOption::Filter(filter));
  h.end();
  if (err) {
    Serial.printf("[miner] %s: %s\n", addr.c_str(), err.c_str());
    return;
  }
  if (doc["hostname"].is<const char *>() && strlen(doc["hostname"]) > 0)
    strlcpy(mi.name, doc["hostname"], sizeof mi.name);
  mi.hashrate = doc["hashRate"] | 0.0f;
  mi.temp = doc["temp"] | 0.0f;
  mi.power = doc["power"] | 0.0f;
  if (doc["bestDiff"].is<const char *>()) strlcpy(mi.bestDiff, doc["bestDiff"], sizeof mi.bestDiff);
  else mi.bestDiffNum = doc["bestDiff"] | 0.0;
  if (doc["bestSessionDiff"].is<const char *>())
    strlcpy(mi.sessionDiff, doc["bestSessionDiff"], sizeof mi.sessionDiff);
  else mi.sessionDiffNum = doc["bestSessionDiff"] | 0.0;
  mi.vrTemp = doc["vrTemp"] | 0.0f;
  mi.freq = doc["frequency"] | 0;
  mi.fanRpm = doc["fanrpm"] | 0;
  mi.fanPct = doc["fanspeed"] | 0;
  strlcpy(mi.version, doc["version"] | "", sizeof mi.version);
  String model = doc["deviceModel"] | "";
  mi.repo = model.indexOf("Nerd") >= 0 ? 2 : (doc["ASICModel"].is<const char *>() ? 1 : 0);
  mi.accepted = doc["sharesAccepted"] | 0;
  mi.rejected = doc["sharesRejected"] | 0;
  mi.uptime = doc["uptimeSeconds"] | 0;
  mi.ok = true;
}

// ---------------------------------------------------------------------------
// "Update verfuegbar": neueste Release-Version der Miner-Firmware von GitHub, alle 6 Stunden.
// Die Antwort ist ca. 50 KB gross; "tag_name" steht nach ca. 1,5 KB, danach wird abgebrochen.
// ---------------------------------------------------------------------------
// Index 3 = diese Firmware selbst
static const char *MINER_REPOS[] = {"", "bitaxeorg/ESP-Miner", "shufps/ESP-Miner-NerdQAxePlus", FW_REPO};
static const uint8_t OWN_REPO = 3;
static char latestTag[4][16];
static uint32_t latestCheckedMs[4] = {0, 0, 0, 0};

const char *latestMinerFirmware(uint8_t repo) { return repo < 3 ? latestTag[repo] : ""; }

static bool fetchLatestTag(uint8_t repo) {
  WiFiClientSecure t;
  t.setCACert(ROOT_CA_BUNDLE);
  HTTPClient h;
  setupHttp(h, false, 8000);
  h.useHTTP10(true);
  Serial.printf("[github] frage nach %s\n", MINER_REPOS[repo]);
  if (!h.begin(t, String("https://api.github.com/repos/") + MINER_REPOS[repo] + "/releases/latest")) {
    Serial.println("[github] begin fehlgeschlagen");
    return false;
  }
  h.addHeader("Accept", "application/vnd.github+json");
  int code = h.GET();
  if (code != 200) return httpError(h, t, "GitHub", code), false;
  WiFiClient *s = h.getStreamPtr();
  String buf;
  uint32_t t0 = millis();
  bool ok = false;
  while (!ok && buf.length() < 8000 && millis() - t0 < 8000 && (s->connected() || s->available())) {
    int c = s->read();
    if (c < 0) {
      delay(2);
      continue;
    }
    buf += (char)c;
    if (c != '"') continue;
    int p = buf.indexOf("\"tag_name\"");
    if (p < 0) continue;
    // erst auswerten, wenn Doppelpunkt und beide Anfuehrungszeichen des Werts angekommen sind
    // (frueher wurde sonst ein beliebiger frueherer Text wie "url" als Version gelesen)
    int colon = buf.indexOf(':', p + 10);
    if (colon < 0) continue;
    int q = buf.indexOf('"', colon + 1);
    if (q < 0) continue;
    int e = buf.indexOf('"', q + 1);
    if (e < 0) continue;
    bool hasDigit = false;  // eine Versionsangabe enthaelt Ziffern
    for (int k = q + 1; k < e; ++k) hasDigit = hasDigit || isdigit((unsigned char)buf[k]);
    if (hasDigit) {
      strlcpy(latestTag[repo], buf.substring(q + 1, e).c_str(), sizeof latestTag[repo]);
      ok = true;
    }
  }
  h.end();
  t.stop();
  Serial.printf("[github] neueste Version %s: %s\n", MINER_REPOS[repo], ok ? latestTag[repo] : "?");
  return ok;
}

// Versionsvergleich "v2.15.3" < "v2.15.4" (Zahlenfolgen, Rest wird ignoriert)
static int cmpVersion(const char *a, const char *b) {
  while (*a || *b) {
    while (*a && !isdigit((unsigned char)*a)) ++a;
    while (*b && !isdigit((unsigned char)*b)) ++b;
    long x = *a ? strtol(a, (char **)&a, 10) : -1, y = *b ? strtol(b, (char **)&b, 10) : -1;
    if (x != y) return x < y ? -1 : 1;
    if (x < 0) break;
  }
  return 0;
}

// --- eigene Firmware ---
const char *latestFirmware() {
  const char *t = latestTag[OWN_REPO];
  return *t == 'v' ? t + 1 : t;
}
bool firmwareUpdateAvailable() { return latestTag[OWN_REPO][0] && cmpVersion(FW_VERSION, latestFirmware()) < 0; }

bool checkFirmwareNow() {
  latestCheckedMs[OWN_REPO] = millis();
  return fetchLatestTag(OWN_REPO);
}

// Laedt timechain-terminal-<version>.bin aus dem Release (GitHub leitet auf einen Download-Server
// mit Let's-Encrypt-Zertifikat um, beide Zertifizierungsstellen stehen in certs.h) und schreibt sie
// in den freien App-Bereich. Bei Erfolg Neustart, sonst bleibt die laufende Firmware aktiv.
bool installFirmwareUpdate(String &err) {
  if (!latestTag[OWN_REPO][0] && !checkFirmwareNow()) {
    err = L("GitHub nicht erreichbar.", "GitHub not reachable.");
    return false;
  }
  if (!firmwareUpdateAvailable()) {
    err = L("Es gibt keine neuere Version.", "There is no newer version.");
    return false;
  }
  const String ver = latestFirmware();
  const String url = String("https://github.com/") + FW_REPO + "/releases/download/" + latestTag[OWN_REPO] +
                     "/timechain-terminal-" + ver + ".bin";
  Serial.printf("[ota] lade %s (frei %u Byte)\n", url.c_str(), ESP.getFreeHeap());
  stopApiConnection();  // Speicher fuer die TLS-Verbindung frei machen
  WiFiClientSecure c;
  c.setCACert(ROOT_CA_BUNDLE);
  c.setTimeout(20);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setLedPin(-1);
  t_httpUpdate_return res = httpUpdate.update(c, url);
  if (res == HTTP_UPDATE_OK) {
    Serial.printf("[ota] Version %s installiert -> Neustart\n", ver.c_str());
    delay(500);
    ESP.restart();
  }
  err = String(L("Update fehlgeschlagen: ", "Update failed: ")) + httpUpdate.getLastErrorString();
  Serial.printf("[ota] %s\n", err.c_str());
  return false;
}

bool minerUpdateAvailable(const MinerInfo &m) {
  const char *latest = latestMinerFirmware(m.repo);
  return m.ok && m.repo && m.version[0] && latest[0] && cmpVersion(m.version, latest) < 0;
}

static void fetchMiners() {
  int n = 0, start = 0;
  String list = cfg.miners;
  while (start < (int)list.length() && n < MAX_MINERS) {
    int end = list.indexOf(',', start);
    if (end < 0) end = list.length();
    String a = list.substring(start, end);
    a.trim();
    start = end + 1;
    if (!a.length()) continue;
    fetchMiner(a, data.miners[n]);
    Serial.printf("[miner] %s: %s %.0f GH/s, Firmware %s (Typ %u)\n", a.c_str(), data.miners[n].ok ? "ok" : "FEHLER",
                  data.miners[n].hashrate, data.miners[n].version, data.miners[n].repo);
    ++n;
  }
  data.nMiners = n;
  // neueste Firmware fuer die vorkommenden Miner-Typen pruefen (hoechstens alle 6 Stunden)
  for (uint8_t repo = 1; repo < 3; ++repo) {
    bool used = false;
    for (int i = 0; i < n; ++i) used = used || data.miners[i].repo == repo;
    // erfolgreich: alle 6 Stunden; nach einem Fehler (z.B. GitHub-Limit 60/Stunde) fruehestens nach 30 Minuten
    const uint32_t wait = latestTag[repo][0] ? 6UL * 3600 * 1000 : 30UL * 60 * 1000;
    if (used && (!latestCheckedMs[repo] || millis() - latestCheckedMs[repo] > wait)) {
      latestCheckedMs[repo] = millis();
      fetchLatestTag(repo);
    }
  }
}

bool fetchAll() {
  uint32_t t0 = millis();
  bool okBlocks = fetchBlocks();
  bool okFees = fetchFees();
  fetchPrices();
  if (layout.has(W_CHAIN)) fetchMempoolBlocks();
  if (layout.has(W_MEMPOOL)) fetchMempool();
  if (layout.has(W_DIFFICULTY)) fetchDifficulty();
  if (layout.has(W_HASHRATE)) fetchHashrate();
  if (layout.has(W_LIGHTNING)) fetchLightning();
  if (layout.has(W_CHART)) {
    // lange Zeitraeume seltener laden (der Abruf dauert dort einige Sekunden)
    const uint32_t maxAge = cfg.chartDays && cfg.chartDays <= 7 ? 30UL * 60000UL
                            : cfg.chartDays && cfg.chartDays <= 90 ? 2UL * 3600000UL : 6UL * 3600000UL;
    bool stale = !data.chartOk || data.chartCur != cfg.cur1 || data.chartDays != cfg.chartDays ||
                 millis() - data.chartFetchedMs > maxAge;
    if (stale) fetchChart();
  }
  if (layout.hasMiner()) fetchMiners();
  // neuere Version dieser Firmware? (alle 6 Stunden, nach einem Fehler fruehestens nach 30 Minuten)
  {
    const uint32_t wait = latestTag[OWN_REPO][0] ? 6UL * 3600 * 1000 : 30UL * 60 * 1000;
    if (!latestCheckedMs[OWN_REPO] || millis() - latestCheckedMs[OWN_REPO] > wait) checkFirmwareNow();
  }
  data.online = okBlocks && okFees;
  if (data.online) data.updatedAt = time(nullptr);
  Serial.printf("[data] Abruf %s in %lu ms, Block %u, Heap frei %u\n", data.online ? "ok" : "FEHLER",
                millis() - t0, data.nBlocks ? data.blocks[0].height : 0, ESP.getFreeHeap());
  return data.online;
}

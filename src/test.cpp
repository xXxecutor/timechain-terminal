// Pruefstand: jedes Widget in jeder erlaubten Groesse, mit Extremwerten, echten Daten und
// ohne Daten, auf Deutsch und Englisch. Gemeldet wird alles, was ueber den Rand des Widgets
// hinaus gezeichnet oder mit ".." gekuerzt wird.  Aufruf: GET /test/overflow[?w=block]
#include "app.h"

static Data backup;

static void fillExtreme() {
  Data d;
  const time_t now = time(nullptr);
  d.nBlocks = MAX_BLOCKS;
  for (int i = 0; i < MAX_BLOCKS; ++i) {
    BlockInfo &b = d.blocks[i];
    b.height = 9999999 - i;
    b.ts = now - (23 * 3600 + 59 * 60) - i * 600;
    b.txCount = 99999;
    b.size = 3990000;
    b.reward = 5012345678ULL;
    b.medianFee = 1234.5f;
    b.feeMin = 0.11f;
    b.feeMax = 9876.5f;
    strlcpy(b.pool, "Foundry USA Pool Extra", sizeof b.pool);
    strlcpy(b.hash, "00000000000000000001a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f7", sizeof b.hash);
  }
  d.nMp = MAX_MP;
  for (int i = 0; i < MAX_MP; ++i) {
    d.mp[i].nTx = 99999;
    d.mp[i].medianFee = 1234.5f;
    d.mp[i].feeLo = 999.5f;
    d.mp[i].feeHi = 12345.6f;
  }
  d.feesOk = true;
  const float fees[5] = {1234, 999, 888, 777, 666};
  memcpy(d.fees, fees, sizeof fees);
  d.priceOk = true;
  const uint32_t prices[NUM_CUR] = {1234567, 1134567, 987654, 1098765, 1690000, 1890000, 199999999};
  memcpy(d.prices, prices, sizeof prices);
  d.mempoolOk = true;
  d.mpCount = 9999999;
  d.mpVsize = 999000000ULL;
  d.diffOk = true;
  d.diffProgress = 99.9f;
  d.diffChange = -99.99f;
  d.diffRemaining = 2016;
  d.retargetAt = now + 14 * 86400;
  d.hashOk = true;
  d.hashrate = 9.99e21;
  d.difficulty = 1.23e15;
  d.lnOk = true;
  d.lnCapacity = 9999999999999ULL;
  d.lnNodes = 99999;
  d.lnChannels = 999999;
  d.chartOk = true;
  d.chartN = CHART_POINTS;
  for (int i = 0; i < CHART_POINTS; ++i) d.chart[i] = 1200000 + 150000 * sinf(i / 9.0f) + i * 300;
  d.chartFrom = now - 7 * 86400;
  d.chartTo = now;
  d.chartCur = cfg.cur1;
  d.chartDays = cfg.chartDays;
  d.nMiners = MAX_MINERS;
  for (int i = 0; i < MAX_MINERS; ++i) {
    MinerInfo &m = d.miners[i];
    m.ok = i != 2;
    strlcpy(m.name, "bitaxe-gamma-wohnzimme", sizeof m.name);
    m.hashrate = 99999;
    m.temp = 99.9f;
    m.vrTemp = 88;
    m.power = 999.9f;
    m.bestDiffNum = 999.99e15;
    m.sessionDiffNum = 99.9e12;
    m.accepted = 9999999;
    m.rejected = 99999;
    m.uptime = 999 * 86400 + 23 * 3600;
    m.freq = 1000;
    m.fanRpm = 9999;
  }
  d.online = true;
  d.updatedAt = now;
  data = d;
  // ungünstigster Fall im Widget "Heute vor X Jahren": eigenes Ereignis mit 90 Zeichen
  historyTestText = "Gründung unseres Bitcoin-Stammtischs in Musterstadt mit erster gemeinsamer Node-Installati";
  cfg.cur1 = 3;  // Franken (selbst gezeichnetes Zeichen) und Austral-Dollar als Extremfall
  cfg.cur2 = 5;
  cfg.miners = "a,b,c,d";
}

void streamOverflowReport(WebServer &server) {
  const String only = server.arg("w");
  backup = data;
  const Settings cfgBackup = cfg;
  const Lang langBackup = lang;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  server.sendContent("# Ausrichtung Sprache Daten Widget BxH: aussen=Pixel ausserhalb, kurz=gekuerzt, ok=erlaubt gekuerzt\n");
  uint32_t t0 = millis();
  uint32_t checked = 0, overflow = 0, truncated = 0, printed = 0;
  static const char *VARIANT[] = {"extrem", "echt", "leer"};
  for (int orient = 0; orient < 2; ++orient)
  for (int li = 0; li < 2; ++li) {
    for (int v = 0; v < 3; ++v) {
      lang = li ? LANG_EN : LANG_DE;
      cfg = cfgBackup;
      cfg.rotation = orient;  // nur die Geometrie: Querformat 20 x 12, Hochformat 12 x 20
      if (v == 0) fillExtreme();
      else if (v == 1) data = backup, historyTestText = "";
      else {
        data = Data();
        cfg.miners = "";
        historyTestText = "";
      }
      for (int id = 0; id < W_COUNT; ++id) {
        if (id == W_MINER2 || id == W_MINER3) continue;  // gleicher Code wie Miner 1/4
        const WidgetInfo &wi = WIDGETS[id];
        if (only.length() && only != wi.key) continue;
        for (int w = wi.minW; w <= min<int>(wi.maxW, gridCols()); ++w) {
          for (int h = wi.minH; h <= min<int>(wi.maxH, gridRows()); ++h) {
            RenderCheck r = checkWidget((WidgetId)id, w, h);
            ++checked;
            if (r.outside) ++overflow;
            if (r.truncs) ++truncated;
            if ((r.outside || r.truncs) && printed < 600) {
              ++printed;
              server.sendContent(String(orient ? "hoch " : "quer ") + (li ? "en " : "de ") + VARIANT[v] + " " + wi.key + " " + w + "x" + h +
                                 ": aussen=" + r.outside + " kurz=" + r.truncs + " ok=" + r.truncsOk + "  " +
                                 r.sample + "\n");
            }
          }
          delay(0);
        }
      }
    }
  }
  data = backup;
  cfg = cfgBackup;
  lang = langBackup;
  historyTestText = "";
  server.sendContent("# geprueft: " + String(checked) + ", ueber den Rand: " + String(overflow) +
                     ", gekuerzt: " + String(truncated) + ", Dauer " + String((millis() - t0) / 1000) + " s\n");
  server.sendContent("");
}

// Vorschau mit Extremwerten (GET /preview.bmp?data=extrem)
static Data pushedData;
static String pushedMiners;
void pushTestData() {
  pushedData = data;
  pushedMiners = cfg.miners;
  fillExtreme();
}
void popTestData() {
  historyTestText = "";
  data = pushedData;
  cfg.miners = pushedMiners;
}

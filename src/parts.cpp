// Was eine Kachel zeigt: Teile je Inhalt, Reihenfolge = Prioritaet (wird die Kachel kleiner,
// fallen die hinteren Teile zuerst weg), einzeln abschaltbar. Gespeichert als Text im NVS:
//   "block:age,pool,-tx,fees,reward,hash;miner:temp,power,..."   ("-" = ausgeblendet)
#include "app.h"

static const PartInfo P_BLOCK[] = {{"age", "Alter und Uhrzeit", "Age and time"},
                                   {"pool", "Miner (Pool)", "Miner (pool)"},
                                   {"tx", "Transaktionen und Größe", "Transactions and size"},
                                   {"fees", "Gebührenspanne", "Fee range"},
                                   {"reward", "Belohnung", "Reward"},
                                   {"hash", "Block-Hash", "Block hash"}};
static const PartInfo P_CHAIN[] = {{"median", "Median-Gebühr (Mempool-Blöcke, steht immer oben)", "Median fee (mempool blocks, always on top)"},
                                   {"range", "Gebührenspanne", "Fee range"},
                                   {"tx", "Transaktionen", "Transactions"},
                                   {"time", "Alter bzw. Zeit bis zum Block", "Age or time until the block"},
                                   {"pool", "Pool (geminte Blöcke)", "Pool (mined blocks)"}};
static const PartInfo P_MINER[] = {{"temp", "Temperatur", "Temperature"},   {"power", "Leistung", "Power"},
                                   {"fan", "Lüfter (%)", "Fan (%)"},          {"eff", "Effizienz", "Efficiency"},
                                   {"best", "Best Diff", "Best diff"},        {"shares", "Shares", "Shares"},
                                   {"uptime", "Laufzeit", "Uptime"},          {"sbest", "Best (Sitzung)", "Best (session)"},
                                   {"vr", "VR-Temperatur", "VR temperature"}, {"freq", "Frequenz", "Frequency"},
                                   {"rej", "Abgelehnte Shares", "Rejected shares"},
                                   {"rpm", "Lüfter-Drehzahl", "Fan speed (rpm)"},
                                   {"fw", "Firmware-Version", "Firmware version"}};
static const PartInfo P_MEMPOOL[] = {{"size", "Größe (MvB)", "Size (MvB)"}, {"blocks", "Anzahl Blöcke", "Number of blocks"}};
static const PartInfo P_HASHRATE[] = {{"diff", "Difficulty", "Difficulty"}};
static const PartInfo P_DIFF[] = {{"remain", "Blöcke bis zur Anpassung", "Blocks until adjustment"},
                                  {"date", "Datum der Anpassung", "Adjustment date"},
                                  {"bar", "Fortschrittsbalken", "Progress bar"}};
static const PartInfo P_HALVING[] = {{"nth", "Nummer des Halvings", "Halving number"},
                                     {"when", "Voraussichtlicher Monat", "Expected month"}};
static const PartInfo P_LN[] = {{"nodes", "Knoten", "Nodes"}, {"channels", "Kanäle", "Channels"}};

#define PG(key, de, en, arr, fde, fen) {key, de, en, arr, (uint8_t)(sizeof arr / sizeof arr[0]), fde, fen}
const PartGroup PART_GROUPS[] = {
    PG("block", "Letzter Block", "Latest block", P_BLOCK, "Blockhöhe", "Block height"),
    PG("chain", "Blockkette", "Block chain", P_CHAIN, "Blockhöhe (geminte Blöcke)", "Block height (mined blocks)"),
    PG("miner", "Miner (alle Miner-Kacheln)", "Miners (all miner tiles)", P_MINER, "Hashrate und Name",
       "Hashrate and name"),
    PG("mempool", "Mempool", "Mempool", P_MEMPOOL, "Anzahl Transaktionen", "Number of transactions"),
    PG("hashrate", "Hashrate", "Hashrate", P_HASHRATE, "Hashrate des Netzwerks", "Network hashrate"),
    PG("difficulty", "Difficulty", "Difficulty", P_DIFF, "Erwartete Änderung in %", "Expected change in %"),
    PG("halving", "Halving", "Halving", P_HALVING, "Blöcke bis zum Halving", "Blocks until the halving"),
    PG("lightning", "Lightning", "Lightning", P_LN, "Kapazität (BTC)", "Capacity (BTC)"),
};
const int N_PART_GROUPS = sizeof PART_GROUPS / sizeof PART_GROUPS[0];

struct GroupCfg {
  uint8_t order[16];
  uint16_t on;  // Bit je Teil (Index in der Tabelle)
};
static GroupCfg groups[sizeof PART_GROUPS / sizeof PART_GROUPS[0]];

int partGroupOf(WidgetId id) {
  switch (id) {
    case W_BLOCK: return 0;
    case W_CHAIN: return 1;
    case W_MINER: case W_MINER1: case W_MINER2: case W_MINER3: case W_MINER4: return 2;
    case W_MEMPOOL: return 3;
    case W_HASHRATE: return 4;
    case W_DIFFICULTY: return 5;
    case W_HALVING: return 6;
    case W_LIGHTNING: return 7;
    default: return -1;
  }
}

static void setDefault(int g) {
  for (int i = 0; i < PART_GROUPS[g].n; ++i) groups[g].order[i] = i;
  groups[g].on = 0xFFFF;
}

// Liest den Text; unbekannte Schluessel werden ignoriert, fehlende (neue) Teile hinten angehaengt
bool parseParts(const String &text) {
  for (int g = 0; g < N_PART_GROUPS; ++g) setDefault(g);
  int start = 0;
  while (start < (int)text.length()) {
    int end = text.indexOf(';', start);
    if (end < 0) end = text.length();
    String item = text.substring(start, end);
    start = end + 1;
    int colon = item.indexOf(':');
    if (colon < 0) continue;
    String key = item.substring(0, colon);
    int g = -1;
    for (int i = 0; i < N_PART_GROUPS; ++i)
      if (key == PART_GROUPS[i].key) g = i;
    if (g < 0) continue;
    const PartGroup &pg = PART_GROUPS[g];
    uint8_t order[16];
    int n = 0;
    uint16_t seen = 0, on = 0;
    int p = colon + 1;
    while (p <= (int)item.length()) {
      int c = item.indexOf(',', p);
      if (c < 0) c = item.length();
      String k = item.substring(p, c);
      p = c + 1;
      bool off = k.startsWith("-");
      if (off) k.remove(0, 1);
      for (int i = 0; i < pg.n; ++i)
        if (k == pg.parts[i].key && !(seen & (1 << i))) {
          seen |= 1 << i;
          order[n++] = i;
          if (!off) on |= 1 << i;
        }
    }
    for (int i = 0; i < pg.n; ++i)
      if (!(seen & (1 << i))) order[n++] = i, on |= 1 << i;
    memcpy(groups[g].order, order, sizeof order);
    groups[g].on = on;
  }
  return true;
}

String partsToString() {
  String s;
  for (int g = 0; g < N_PART_GROUPS; ++g) {
    if (s.length()) s += ';';
    s += String(PART_GROUPS[g].key) + ':';
    for (int i = 0; i < PART_GROUPS[g].n; ++i) {
      int k = groups[g].order[i];
      if (i) s += ',';
      if (!(groups[g].on & (1 << k))) s += '-';
      s += PART_GROUPS[g].parts[k].key;
    }
  }
  return s;
}

// Aktive Teile eines Inhalts in Prioritaets-Reihenfolge (Indizes in der Teile-Tabelle)
int enabledParts(WidgetId id, uint8_t *out, int max) {
  int g = partGroupOf(id);
  if (g < 0) return 0;
  int n = 0;
  for (int i = 0; i < PART_GROUPS[g].n && n < max; ++i) {
    int k = groups[g].order[i];
    if (groups[g].on & (1 << k)) out[n++] = k;
  }
  return n;
}

bool partEnabled(WidgetId id, int part) {
  int g = partGroupOf(id);
  return g >= 0 && (groups[g].on & (1 << part));
}

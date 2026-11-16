// Ereignisse fuer "Heute vor X Jahren": eingebaute Liste (Flash, history_events.h) plus
// Aenderungen des Nutzers in /events.json auf dem internen Dateisystem (LittleFS):
//   {"off":[3,17],                                  eingebaute Ereignisse, die ausgeblendet sind
//    "edit":[{"i":12,"y":2010,"m":5,"d":22,"de":"..","en":".."}],   geaenderte eingebaute
//    "own":[{"y":2020,"m":3,"d":1,"on":true,"de":"..","en":".."}]}  eigene Ereignisse
#include "app.h"
#include <LittleFS.h>
#include <vector>
#include "history_events.h"

struct EvEntry {
  int idx = -1;  // eingebautes Ereignis (bei Aenderungen), sonst -1
  uint16_t y = 0;
  uint8_t m = 0, d = 0;
  bool on = true;
  String de, en;
};

static std::vector<EvEntry> edits, own;
static uint8_t offBits[(N_EVENTS + 7) / 8];
static bool fsOk = false;
static const char *EV_FILE = "/events.json";
static const int MAX_OWN = 50;
static const int MAX_CHARS = 90;  // passt im Pruefstand auch in die kleinste Kachel
String historyTestText;

static bool isOff(int i) { return offBits[i >> 3] & (1 << (i & 7)); }

static int utf8Len(const String &s) {
  int n = 0;
  for (size_t i = 0; i < s.length(); ++i)
    if ((s[i] & 0xC0) != 0x80) ++n;
  return n;
}

static bool validDate(int y, int m, int d) {
  static const uint8_t dim[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return y >= 1900 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= dim[m - 1];
}

static bool readEntry(JsonObjectConst o, EvEntry &e, String &err) {
  e.y = o["y"] | 0, e.m = o["m"] | 0, e.d = o["d"] | 0;
  e.on = o["on"] | true;
  e.de = o["de"] | "";
  e.en = o["en"] | "";
  e.de.trim();
  e.en.trim();
  if (!validDate(e.y, e.m, e.d)) return err = L("Ungültiges Datum", "Invalid date"), false;
  if (!e.de.length() && !e.en.length()) return err = L("Text fehlt", "Text missing"), false;
  if (utf8Len(e.de) > MAX_CHARS || utf8Len(e.en) > MAX_CHARS)
    return err = String(L("Text zu lang (höchstens ", "Text too long (at most ")) + MAX_CHARS + L(" Zeichen)", " characters)"),
           false;
  return true;
}

// Prueft den kompletten Zustand und uebernimmt ihn nur, wenn alles gueltig ist
static bool applyState(JsonVariantConst root, String &err) {
  uint8_t off[sizeof offBits] = {0};
  std::vector<EvEntry> ed, ow;
  for (JsonVariantConst v : root["off"].as<JsonArrayConst>()) {
    int i = v | -1;
    if (i < 0 || i >= N_EVENTS) return err = "off", false;
    off[i >> 3] |= 1 << (i & 7);
  }
  for (JsonObjectConst o : root["edit"].as<JsonArrayConst>()) {
    EvEntry e;
    e.idx = o["i"] | -1;
    if (e.idx < 0 || e.idx >= N_EVENTS) return err = "edit", false;
    if (!readEntry(o, e, err)) return false;
    ed.push_back(e);
  }
  for (JsonObjectConst o : root["own"].as<JsonArrayConst>()) {
    EvEntry e;
    if (!readEntry(o, e, err)) return false;
    ow.push_back(e);
    if ((int)ow.size() > MAX_OWN)
      return err = String(L("Höchstens ", "At most ")) + MAX_OWN + L(" eigene Ereignisse", " own events"), false;
  }
  memcpy(offBits, off, sizeof offBits);
  edits.swap(ed);
  own.swap(ow);
  return true;
}

static void stateToJson(JsonDocument &doc) {
  JsonArray off = doc["off"].to<JsonArray>();
  for (int i = 0; i < N_EVENTS; ++i)
    if (isOff(i)) off.add(i);
  JsonArray ed = doc["edit"].to<JsonArray>();
  for (const EvEntry &e : edits) {
    JsonObject o = ed.add<JsonObject>();
    o["i"] = e.idx, o["y"] = e.y, o["m"] = e.m, o["d"] = e.d, o["de"] = e.de, o["en"] = e.en;
  }
  JsonArray ow = doc["own"].to<JsonArray>();
  for (const EvEntry &e : own) {
    JsonObject o = ow.add<JsonObject>();
    o["y"] = e.y, o["m"] = e.m, o["d"] = e.d, o["on"] = e.on, o["de"] = e.de, o["en"] = e.en;
  }
}

void eventsBegin() {
  fsOk = LittleFS.begin(true);  // beim allerersten Start wird formatiert (ca. 1 s)
  if (!fsOk) {
    Serial.println("[events] Dateisystem nicht verfuegbar");
    return;
  }
  File f = LittleFS.open(EV_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  String err;
  if (deserializeJson(doc, f) || !applyState(doc.as<JsonVariantConst>(), err))
    Serial.printf("[events] Datei ungueltig: %s\n", err.c_str());
  f.close();
  Serial.printf("[events] %d eingebaut, %d aus, %d geaendert, %d eigene\n", N_EVENTS,
                [] { int n = 0; for (int i = 0; i < N_EVENTS; ++i) n += isOff(i); return n; }(), (int)edits.size(),
                (int)own.size());
}

bool eventsSave(const String &json, String &err) {
  if (!fsOk) return err = L("Dateisystem nicht verfügbar", "File system not available"), false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return err = "JSON", false;
  if (!applyState(doc.as<JsonVariantConst>(), err)) return false;
  JsonDocument out;
  stateToJson(out);
  File f = LittleFS.open(EV_FILE, "w");
  if (!f) return err = L("Speichern fehlgeschlagen", "Saving failed"), false;
  serializeJson(out, f);
  f.close();
  return true;
}

// Fuer die Webseite: eingebaute Liste plus Aenderungen
String eventsJson() {
  JsonDocument doc;
  JsonArray b = doc["builtin"].to<JsonArray>();
  for (int i = 0; i < N_EVENTS; ++i) {
    JsonArray e = b.add<JsonArray>();
    e.add(EVENTS[i].y), e.add(EVENTS[i].m), e.add(EVENTS[i].d), e.add(EVENTS[i].de), e.add(EVENTS[i].en);
  }
  stateToJson(doc);
  doc["max"] = MAX_CHARS;
  doc["maxOwn"] = MAX_OWN;
  String out;
  serializeJson(doc, out);
  return out;
}

int eventTotal() { return N_EVENTS + own.size(); }

// false = ausgeblendet
bool eventAt(int i, EventView &v) {
  if (i < N_EVENTS) {
    if (isOff(i)) return false;
    for (const EvEntry &e : edits)
      if (e.idx == i) {
        v = {e.y, e.m, e.d, e.de.c_str(), e.en.c_str()};
        return true;
      }
    v = {EVENTS[i].y, EVENTS[i].m, EVENTS[i].d, EVENTS[i].de, EVENTS[i].en};
    return true;
  }
  i -= N_EVENTS;
  if (i >= (int)own.size() || !own[i].on) return false;
  v = {own[i].y, own[i].m, own[i].d, own[i].de.c_str(), own[i].en.c_str()};
  return true;
}

// Text in der aktuellen Sprache, sonst in der anderen
const char *eventText(const EventView &v) {
  const char *a = lang == LANG_EN ? v.en : v.de, *b = lang == LANG_EN ? v.de : v.en;
  return a && a[0] ? a : b;
}

int eventLongestBuiltin() {
  int best = 0;
  for (int i = 1; i < N_EVENTS; ++i)
    if (max(strlen(EVENTS[i].de), strlen(EVENTS[i].en)) > max(strlen(EVENTS[best].de), strlen(EVENTS[best].en))) best = i;
  return best;
}

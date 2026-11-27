// Zahlen-, Zeit- und Datumsformate (Deutsch: 1.234,56 / Englisch: 1,234.56)
#include "app.h"

Lang lang = LANG_DE;

static char thouSep() { return lang == LANG_EN ? ',' : '.'; }
static char decSep() { return lang == LANG_EN ? '.' : ','; }

String fmtInt(uint64_t v) {
  char raw[24];
  snprintf(raw, sizeof raw, "%llu", (unsigned long long)v);
  String out;
  int len = strlen(raw);
  for (int i = 0; i < len; ++i) {
    out += raw[i];
    int rest = len - 1 - i;
    if (rest > 0 && rest % 3 == 0) out += thouSep();
  }
  return out;
}

String fmtDec(double v, int decimals) {
  bool neg = v < 0;
  if (neg) v = -v;
  double scale = pow(10.0, decimals);
  uint64_t scaled = (uint64_t)llround(v * scale);
  uint64_t ip = scaled / (uint64_t)scale;
  String out = (neg ? "-" : "") + fmtInt(ip);
  if (decimals > 0) {
    char frac[12];
    snprintf(frac, sizeof frac, "%0*llu", decimals,
             (unsigned long long)(scaled % (uint64_t)scale));
    out += decSep();
    out += frac;
  }
  return out;
}

String fmtFee(float f) {
  if (f >= 10) return fmtDec(f, 0);
  if (f >= 1) return fmtDec(f, 1);
  return fmtDec(f, 2);
}

String fmtAge(uint32_t ts) {
  time_t now = time(nullptr);
  if (now < 1700000000 || ts == 0) return "";
  long sec = (long)(now - (time_t)ts);
  if (sec < 60) return L("gerade eben", "just now");
  long min = sec / 60;
  if (lang == LANG_EN) {
    if (min < 60) return String(min) + " min ago";
    return String(min / 60) + " h " + String(min % 60) + " min ago";
  }
  if (min < 60) return "vor " + String(min) + " Min.";
  return "vor " + String(min / 60) + " Std. " + String(min % 60) + " Min.";
}

// kurz fuer die Blockwuerfel: "5 Min." / "1h 5 Min." / "5 min" / "1h 5m"
String fmtAgeShort(uint32_t ts) {
  time_t now = time(nullptr);
  if (now < 1700000000 || ts == 0) return "";
  long min = (long)(now - (time_t)ts) / 60;
  if (min < 1) return L("gerade eben", "just now");
  if (min < 60) return String(min) + L(" Min.", " min");
  return String(min / 60) + "h " + String(min % 60) + L(" Min.", "m");
}

String fmtTime(time_t t, const char *fmt) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[40];
  strftime(buf, sizeof buf, fmt, &tmv);
  return String(buf);
}

static const char *MONATE[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli",
                               "August", "September", "Oktober", "November", "Dezember"};
static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};

const char *monthName(int m) { return lang == LANG_EN ? MONTHS[m % 12] : MONATE[m % 12]; }

// "16.11." / "16.11.2026" bzw. "16 Nov" / "16 Nov 2026"
String fmtDate(time_t t, bool withYear) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[24];
  if (lang == LANG_EN)
    snprintf(buf, sizeof buf, withYear ? "%d %.3s %d" : "%d %.3s", tmv.tm_mday, MONTHS[tmv.tm_mon],
             tmv.tm_year + 1900);
  else
    snprintf(buf, sizeof buf, withYear ? "%02d.%02d.%d" : "%02d.%02d.", tmv.tm_mday, tmv.tm_mon + 1,
             tmv.tm_year + 1900);
  return String(buf);
}

const Currency CURRENCIES[NUM_CUR] = {
    {"USD", "$", "Dollar", "Dollar", true},
    {"EUR", "€", "Euro", "Euro", true},
    {"GBP", "£", "Pfund", "Pound", true},
    {"CHF", "\x02", "Franken", "Franc", false},  // \x02 = Franken-Zeichen, wird auf dem Display gezeichnet
    {"CAD", "C$", "Kan. Dollar", "Can. dollar", true},
    {"AUD", "A$", "Austr. Dollar", "Aus. dollar", true},
    {"JPY", "¥", "Yen", "Yen", true},
};

// Deutsch "64.383 $", "53.690 CHF"; Englisch "$64,383", "CHF 53,690"
String fmtPrice(uint32_t v, uint8_t cur) {
  if (cur >= NUM_CUR) cur = 0;
  const Currency &c = CURRENCIES[cur];
  if (lang == LANG_EN) return String(c.sym) + (c.symFirstEn ? "" : " ") + fmtInt(v);
  return fmtInt(v) + " " + c.sym;
}

// 11,9G / 4,29T / 950k
String fmtSI(double v) {
  static const char *suf[] = {"", "k", "M", "G", "T", "P", "E"};
  int i = 0;
  while (v >= 1000 && i < 6) v /= 1000, ++i;
  return fmtDec(v, v < 10 ? 2 : (v < 100 ? 1 : 0)) + suf[i];
}

// 3 Tg. 4 Std. / 5 Std. 12 Min. / 12 Min. (Englisch 3d 4h / 5h 12m / 12 min)
String fmtDuration(uint32_t sec, bool compact) {
  uint32_t d = sec / 86400, h = sec / 3600 % 24, m = sec / 60 % 60;
  if (compact) {
    if (d >= 10) return String(d) + " d";
    if (d) return String(d) + " d " + String(h) + " h";
    if (h) return String(h) + " h " + String(m) + " min";
    return String(m) + " min";
  }
  if (d >= 10) return String(d) + (lang == LANG_EN ? " days" : " Tage");
  if (lang == LANG_EN) {
    if (d) return String(d) + "d " + String(h) + "h";
    if (h) return String(h) + "h " + String(m) + "m";
    return String(m) + " min";
  }
  if (d) return String(d) + " Tg. " + String(h) + " Std.";
  if (h) return String(h) + " Std. " + String(m) + " Min.";
  return String(m) + " Min.";
}

// Moscow Time: Satoshis pro Waehrungseinheit, als Uhrzeit geschrieben (1544 -> 15:44)
String moscowTime(uint32_t price) {
  if (!price) return "-";
  uint32_t sats = 100000000UL / price;
  if (sats >= 10000) return fmtInt(sats);
  char buf[8];
  snprintf(buf, sizeof buf, "%u:%02u", (unsigned)(sats / 100), (unsigned)(sats % 100));
  return String(buf);
}

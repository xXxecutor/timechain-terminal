# Timechain Terminal für das Good Display GDP075FW1

Eigene Firmware für das 7,5"-E-Paper-Schild **GDP075FW1** (Platine ESP32-M075 mit
ESP32-WROOM-32D, 4-Farben-Panel GDEM075F52, 800×480, Schwarz/Weiß/Gelb/Rot).
Das Gerät läuft eigenständig: Es holt die Daten per WLAN direkt von
[mempool.space](https://mempool.space) oder von einem eigenen mempool-Server (z. B. Umbrel).
Ein PC wird nur für die erste Installation gebraucht, danach reicht ein USB-Netzteil.
Updates kommen über WLAN.

**Installieren:** [Web-Flasher](https://xxxecutor.github.io/timechain-terminal/) in Chrome oder
Edge öffnen, Gerät per USB anschließen, „Firmware installieren“. **Updates** ab Version 1.1.0
mit einem Klick: Das Gerät fragt alle 6 Stunden bei GitHub nach neuen
[Releases](https://github.com/xXxecutor/timechain-terminal/releases); ist eines da, zeigt die
Geräte-Webseite einen Hinweis und unter System den Button „Version … installieren“.

## Was angezeigt wird

Oben steht immer die Kopfzeile (Logo, Zeitpunkt der Daten bzw. OFFLINE und optional der Kurs:
Hauptwährung groß, Zweitwährung klein darunter; USD, EUR, GBP, CHF, CAD, AUD oder JPY).
Darunter lassen sich diese Inhalte frei wählen, anordnen und in der Größe ändern
(Querformat 20 × 12 Felder, Hochformat 12 × 20 Felder, je mit eigenem Layout; Mindest- und
Höchstgröße stehen im Editor an jedem Kasten):

| Inhalt | Zeigt |
|---|---|
| Letzter Block | Blockhöhe, Block-Hash (neben der Höhe oder als Zeile; die führenden Nullen als „19×0“ zusammengefasst), Alter, Miner/Pool, Transaktionen, Größe, Gebührenspanne, Belohnung |
| Moscow Time | Satoshis pro Einheit der Haupt- und Zweitwährung als „Uhrzeit“ (z. B. 15:44) |
| Gebühren | Niedrig / Mittel / Hoch wie auf mempool.space, darunter „Ohne Priorität“ und Minimum |
| Blockkette | links die letzten geminten Blöcke (schwarz) mit Gebührenspanne, rechts die nächsten Mempool-Blöcke (gelb); Anzahl passt sich der Breite an. Was die Würfel zeigen, wird für geminte und nächste Blöcke getrennt eingestellt (Seite „Inhalte“); gleiche Position in beiden Listen = gleiche Höhe im Display (bei den geminten Blöcken ist die Blockhöhe Zeile 1) |
| Mempool, Hashrate, Difficulty, Halving, Lightning | Kacheln mit Wert und so vielen Zusatzangaben, wie Platz ist (z. B. „16.237 Knoten · 32.676 Kanäle“). Kacheln in derselben Reihe gleichen sich an: Überschriften, Werte (gleiche Schriftgröße) und Zusatzzeilen stehen auf einer Linie |
| Preis-Chart | Kursverlauf in der Hauptwährung über 24 Stunden, 7, 30 oder 90 Tage, 1, 2 oder 4 Jahre oder den gesamten Verlauf (logarithmisch; mempool.space hat USD ab 2010, EUR ab 2013, GBP/JPY ab 2014, CHF ab 2019) |
| Miner (alle) | Gesamt-Hashrate und auf Wunsch Gesamtleistung. Summen und Gerätewerte teilen sich das Zeilenraster. Gerätewerte stehen rechtsbündig mit gemeinsamen Spalten für Bezeichnungen, Zahlen und Hashrate-Einheiten. Je nach Platz passen mehrere Geräte nebeneinander oder auf mehrere Zeilen. Bei vielen Werten bekommt jeder Miner einen getrennten Bereich mit Kopfzeile und festem Raster für beschriftete Messwerte. Die Kachel zeigt aktivierte Messwerte in der Reihenfolge unter „Inhalte“. Fehlende Messdaten werden übersprungen. Sobald ein verfügbarer Wert aus Platzgründen entfällt, entfallen auch alle nachfolgenden Werte. Bei nur einem Miner wie „Miner 1“. „Inhalte“ zählt auch die Gesamtleistung als sichtbar. |
| Miner 1 – 4 | je ein Gerät mit AxeOS (Bitaxe, NerdQaxe …): Hashrate groß, dazu so viele Werte wie Platz ist (Spalten nach tatsächlicher Textbreite, rechts neben oder unter der Hashrate) – Temperatur, Leistung, Lüfter (%), Effizienz, Best Diff, Shares, Laufzeit, Best der Sitzung, VR-Temperatur, Frequenz, Abgelehnte, Lüfter-Drehzahl, Firmware-Version. „Update v…“ im Titelbalken, wenn auf GitHub eine neuere AxeOS-Version erschienen ist (Abfrage alle 6 Stunden) |
| An diesem Tag | Bitcoin-Ereignis „heute vor X Jahren“ aus einer geprüften Liste von 86 Ereignissen (2008 bis 2025, siehe unten); ist heute nichts dabei, das nächste bevorstehende |

Die Gebührenspanne der Blöcke wird wie auf mempool.space berechnet und mit dem Sats-Symbol
geschrieben (auch die Median-Gebühr der Mempool-Blöcke): normal, kompakt („1-90 /vB“ mit Symbol),
dann in kleinerer Schrift; nur wenn gar nichts passt, steht die Einheit einmal in der Überschrift. Die Blockreihe nutzt
dabei überall dieselbe Schreibweise.
Alle Inhalte passen Schrift und Umfang an ihre Größe an. Was eine Kachel zeigt und in welcher
Reihenfolge, lässt sich auf der Seite „Inhalte“ je Inhalt einstellen (Letzter Block, Blockkette,
Miner, Mempool, Hashrate, Difficulty, Halving, Lightning): Die Reihenfolge ist zugleich die
Priorität – wird die Kachel kleiner, fallen die unteren Angaben zuerst weg; abgewählte erscheinen
nie, ihr Platz geht an die übrigen.

Sprache: Deutsch oder Englisch (Display und Webseite, inkl. Zahlenformat).

**Farben:** Jede Rolle (Hintergrund, Text, Überschriften, Titelbalken, Kopfzeile, Kurs, Logo,
gemintete und Mempool-Blöcke, Chart, Hervorhebung …) bekommt eine der Panel-Farben Schwarz,
Weiß, Gelb, Rot oder Orange (aus Rot und Gelb gemischt, nur für Flächen). Vorlagen:
Standard, Dunkel, Rot, Schwarz-Weiß, Helle Kopfzeile.

**QR-Code zur Geräte-Webseite:**
* klein rechts in der Kopfzeile (Seite „Layout bearbeiten“ → „Kleiner QR-Code … in der Kopfzeile“)
* groß auf dem ganzen Display: die Taste am Gerät (RESET) drücken, **etwa 1 Sekunde warten**, nochmal
  drücken (innerhalb von 10 s). Nach 3 Minuten erscheint wieder die normale Anzeige. Schneller geht
  es nicht: Nach einem Reset braucht das Gerät ca. 0,6 s (davon 0,55 s Bootloader), bis die Firmware
  läuft und den ersten Druck bemerken kann.
* automatisch nach der WLAN-Einrichtung

Der QR-Code ist immer schwarz auf weiß, unabhängig vom Farbschema (invertierte Codes lesen viele
Handys nicht). Die Taste am GDP075FW1 ist offenbar RESET, nicht BOOT/GPIO0; ein einfacher Druck
startet das Gerät neu. Falls ein Gerät doch eine BOOT-Taste hat, zeigt ein kurzer Druck darauf
ebenfalls den QR-Code.

**Ausrichtung:** Quer, quer um 180° gedreht, hoch (Anschluss rechts) oder hoch (Anschluss links).
Das Gerät hat keinen Lagesensor, deshalb wird die Ausrichtung auf der Webseite eingestellt.

## Aktualisierung der Anzeige

* Alle **5 s** wird geprüft, ob es einen neuen Block gibt (ca. 55 ms über eine dauerhaft
  offene HTTPS-Verbindung, Keep-Alive). Alle Daten zu laden dauert ca. 2–3 s.
* Jeder neue Block wird **sofort** gezeichnet, auch direkt nach einem vorherigen Bildwechsel.
* Ohne neuen Block wird spätestens alle **10 Minuten** neu gezeichnet (Gebühren, Kurs,
  Blockalter), dieser periodische Refresh hält mindestens 3 Minuten Abstand.
* Ein Bildwechsel dauert beim 4-Farben-Panel ca. 27 s und das Bild „flackert“ dabei,
  das ist normal. Teilaktualisierung kann dieses Panel nicht. Vom Block bis zum Bild
  vergehen also typisch 0–5 s (Abfrage) + ca. 3 s (Daten laden) + ca. 27 s (Panel).

Die Werte stehen in [src/config.h](src/config.h) und lassen sich dort ändern.

## Installation ohne Programmierkenntnisse (Web-Flasher)

Die Installationsseite **https://xxxecutor.github.io/timechain-terminal/** (Ordner
[web-flasher/](web-flasher/), auf Basis von [ESP Web Tools](https://esphome.github.io/esp-web-tools/)):
Gerät per USB anschließen, Seite in Chrome oder Edge am Computer öffnen, „Firmware installieren“
klicken und bei der ersten Installation „Gerät löschen“ bestätigen. Fehlt unter Windows der
Treiber für „USB-SERIAL CH340“, den CH340-Treiber des Herstellers WCH installieren.
Zum lokalen Testen reicht `python -m http.server` im Ordner und `http://localhost:8000`.

## WLAN

Beim ersten Start ohne gespeichertes WLAN öffnet das Gerät ein eigenes WLAN
**„Timechain-Terminal“** und zeigt die Anleitung auf dem Display:

1. Mit Handy/PC das WLAN „Timechain-Terminal“ wählen
2. Die Einrichtungsseite öffnet sich automatisch (sonst `http://192.168.4.1`)
3. „Configure WiFi“, dein **2,4-GHz**-WLAN wählen, Passwort eingeben.
   Darunter lassen sich die Sprache und optional ein eigener mempool-Server wählen.
   Die Anleitung auf dem Display steht auf Deutsch und Englisch.

Der ESP32 kann kein 5-GHz-WLAN.

Nach der Einrichtung zeigt das Display kurz die Adresse der Geräte-Webseite.

**WLAN wechseln:** auf der Geräte-Webseite unter System „Mit anderem WLAN verbinden“ (siehe
unten). Alle WLAN-Daten löschen und das Einrichtungs-WLAN starten: auf der Webseite „WLAN-Daten
löschen“, beim Einschalten die BOOT-Taste der Platine gedrückt halten (falls vorhanden) oder per
USB den Befehl `wifi-reset` senden.

Fällt das WLAN aus, versucht das Gerät es weiter und zeigt in der Kopfzeile
„OFFLINE“ mit dem Zeitpunkt der letzten Daten.

## Geräte-Webseite

Im Heimnetz unter **http://timechain-terminal.local** (oder der IP-Adresse des Geräts):

Drei Reiter, auf jeder Seite ein Pfeil zurück nach oben. Vorschaubilder füllen die volle Breite
(Hochformat höchstens 85 % der Fensterhöhe). Passt der Maßstab nicht ganzzahlig zu den
Bildschirmpixeln (z. B. Windows-Skalierung 125 %), wird das Bild erst ganzzahlig hochgerechnet und
dann weich angepasst, damit keine ungleich breiten Pixel entstehen. Ein Klick zeigt es mindestens
doppelt so groß (verschiebbar), ein weiterer Klick schließt.

Meldungen erscheinen oben eingeblendet und verschwinden nach einigen Sekunden. Die Seite zeigt
außerdem, wenn das Display gleich oder gerade neu gezeichnet wird (dann antwortet das Gerät rund
30 Sekunden nicht) und wenn es fertig ist; das „Aktuelle Bild“ lädt sich dann neu.

**Übersicht**
* **Aktuelles Bild** des Displays (vom Gerät selbst gerechnet, ca. 0,8 s) und „Display jetzt neu zeichnen“
* **Status:** letzter Block, Daten, Datenquelle, WLAN, Adresse, Firmware, letzter Neustart, Laufzeit, Speicher
* **Allgemein:** Sprache (Deutsch/Englisch)
* **Datenquelle:** mempool-Server (leer = mempool.space, eigener Node z. B. `http://umbrel.local:3006`
  oder `http://192.168.1.20:3006`, wird vor dem Speichern geprüft)
* **Sicherheit:** Passwort für die Seite (Benutzer `admin`, mindestens 6 Zeichen). Vergessen:
  Gerät per USB anschließen und im seriellen Monitor `password-reset` senden.
* **System:** Firmware-Update über WLAN (Einstellungen und WLAN bleiben erhalten): Gibt es auf
  GitHub ein neueres Release, steht dort „Version … installieren“. Das Gerät lädt die Datei dann
  selbst von GitHub (HTTPS, Zertifikate geprüft), schreibt sie in den zweiten App-Bereich und
  startet neu (1–2 Minuten); schlägt etwas fehl, läuft die bisherige Version weiter. „Nach Updates
  suchen“ fragt sofort nach, sonst alle 6 Stunden. Eine eigene Datei lässt sich weiterhin von Hand
  hochladen;
  **anderes WLAN:** „Netzwerke suchen“ listet die 2,4-GHz-Netze in Reichweite (nach Signalstärke),
  Name antippen, Passwort eingeben, „Verbinden“. Das Gerät testet die neue Verbindung bis zu 20 s;
  klappt sie nicht, kehrt es mit der zuvor gespeicherten WLAN-Konfiguration ins alte WLAN zurück
  und meldet den Grund (nicht gefunden / Passwort prüfen). Klappt sie, startet es neu und ist im
  neuen WLAN unter timechain-terminal.local erreichbar. Außerdem: WLAN-Daten löschen (Einrichtungs-WLAN)

**Inhalte** (Einstellungen der einzelnen Inhalte)
* **Kurs und Währungen:** Haupt- und Zweitwährung (USD, EUR, GBP, CHF mit ₣, CAD, AUD, JPY), Zeitraum des Preis-Charts
* **Miner:** Adressen der AxeOS-Geräte (Reihenfolge = Miner 1 bis 4)
* **Inhalte der Kacheln:** je Inhalt alle Angaben mit Häkchen (an/aus) und Pfeilen (Reihenfolge =
  Priorität), „Standard“ setzt alles zurück. Oben steht mit 🔒 der Wert, den die Kachel immer zeigt
  (z. B. die Blockhöhe). Neben jeder Angabe steht, ob sie im aktuellen Layout zu sehen ist
  (sichtbar / nur in einem Teil der Kacheln bzw. bei der Blockkette nur bei geminten oder nur bei
  den nächsten Blöcken / kein Platz / keine Daten); das Gerät rechnet das
  Layout dafür unsichtbar durch, schon während man umsortiert (`/api/parts`)
* **Entfernen:** Neben jeder aktiven Kachel steht „Entfernen“. Die Kachel verschwindet direkt aus dem Layout der aktuellen Ausrichtung. Über „Layout & Farben“ lässt sie sich wieder hinzufügen.
* **Ereignisse für „Heute vor X Jahren“:** alle eingebauten Ereignisse mit Suche, Filter
  (aktiv, ausgeblendet, eigene, geänderte) und Sortierung (chronologisch oder nach Kalendertag).
  Jedes lässt sich ausblenden oder bearbeiten (Datum, Text Deutsch/Englisch) und wieder auf das
  Original zurücksetzen; eigene Ereignisse (bis 50, je höchstens 90 Zeichen) kommen dazu.
  Gespeichert wird sofort in `/events.json` auf dem internen Dateisystem (LittleFS, 128 KB);
  das Display zeigt Änderungen nach einer Minute.

**Layout &amp; Farben**
* **Ausrichtung und Kopfzeile:** Quer/hoch, Kurs und QR-Code in der Kopfzeile
* **Layout:** Inhalte an-/abwählen, Kästen auf dem Raster verschieben und an allen vier Ecken und
  Kanten in der Größe ändern (die gegenüberliegende Seite bleibt stehen); „Automatisch anordnen“ verteilt die gewählten Inhalte lückenlos
* **Vorschau** in voller Breite, aktualisiert sich bei jeder Änderung von Layout oder Farben
* **Farben:** Vorlagen, einzelne Farben aufklappbar

* Maschinenlesbar: `/api/status` (JSON), `/api/events` (Ereignisse, GET/POST), `/preview.bmp` (aktuelles Bild; Parameter
  `l=<layout>`, `lang=en`, `theme=<farben>`, `rot=0..3`, `hdrqr=1`, `cur1=`/`cur2=` (0–6),
  `screen=setup|info`)
* Nur frei wählbare Namen (Miner, Pool, Server) werden bei Platzmangel mit „..“ gekürzt.

Ohne Passwort kann jeder im eigenen WLAN Einstellungen ändern und Firmware aufspielen.
Die Anmeldung läuft per HTTP-Digest (Basic-Anmeldungen lehnt das Gerät ab). Das Passwort selbst
geht nie durchs Netz, auch nicht beim Setzen: Der Browser berechnet daraus
`HA1 = md5("admin:Timechain Terminal:<passwort>")` und schickt nur diesen Wert; das Gerät speichert
nur HA1 (ältere Firmware speicherte das Passwort, es wird beim ersten Start umgewandelt). Wer HA1
mitschneidet, könnte sich damit an diesem Gerät anmelden, erfährt aber nicht das Passwort.
Die Seite selbst ist unverschlüsselt (HTTP): Inhalte und andere Formulare, z. B. ein neu
eingegebenes WLAN-Passwort, sind im Heimnetz mitlesbar. Echtes HTTPS mit gültigem Zertifikat ist
für `.local`-Adressen nicht möglich.

Während eines Bildwechsels (ca. 30 s) beantwortet das Gerät keine Anfragen, sie werden
danach erledigt. Grund: Panel-Spannung und starker WLAN-Verkehr zusammen haben im Test
an einem PC-USB-Anschluss einen Spannungseinbruch mit Neustart ausgelöst.

## Bauen und flashen

Voraussetzung: Python mit PlatformIO (`pip install platformio`). Gerät per USB anschließen
(erscheint als „USB-SERIAL CH340“, in den Beispielen COM3).

```bash
python -m platformio run -t upload --upload-port COM3
```

Serieller Monitor (Log, Befehle `refresh`, `info`, `password-reset`, `wifi-reset` und `factory-reset`):

```bash
python -m platformio device monitor --port COM3
```

### Entwickler-Build mit Prüfstand

```bash
python -m platformio run -e dev
```

Der Build `dev` enthält zusätzlich den Prüfstand `/test/overflow`: Er zeichnet jedes Widget in
jeder erlaubten Größe mit Extremwerten (7-stellige Blockhöhe, Gebühren über 1.000 sat/vB,
4 Miner mit langen Namen …), echten Daten und ohne Daten, auf Deutsch und Englisch, im Quer-
und Hochformat (ca. 30.000 Fälle, 4 Minuten), und meldet alles, was über den Rand geht oder
gekürzt wird. Stand 1.2.0: 0 Überläufe, 0 Kürzungen. Außerdem zeigt `/preview.bmp?data=extrem`
die Vorschau mit diesen Extremwerten. In der Firmware für Nutzer (Release, Web-Flasher) fehlt
beides.

### Release erstellen

```bash
python -m platformio run
```

```bash
python scripts/make_release.py
```

Das legt die Dateien für den Web-Flasher in `web-flasher/firmware/` ab, schreibt die Version
aus `src/config.h` (`FW_VERSION`) in `web-flasher/manifest.json` und kopiert die Datei für
das WLAN-Update nach `release/timechain-terminal-<version>.bin`.

## Original-Firmware wiederherstellen

Wer die Firmware des Geräts behalten will, sichert vor dem ersten Flashen den kompletten
Flash (4 MB). Der Ordner `backup/` wird nie hochgeladen (`.gitignore`), denn die Sicherung kann
WLAN-Zugangsdaten enthalten.

```bash
python -m esptool --port COM3 --baud 460800 read-flash 0 0x400000 backup/original.bin
```

Zurückspielen:

```bash
python -m esptool --port COM3 --baud 460800 write-flash 0 backup/original.bin
```

Eine nachgebaute Werks-Firmware von Good Display gibt es im Projekt
[iaon/GDP075FW1](https://github.com/iaon/GDP075FW1/releases).

## Technische Details

* Gehäuse (Good Display EW075F12): 196 × 138 × 12 mm, ca. 204 g, Micro-USB 5 V (Hersteller empfiehlt
  ein Netzteil mit mindestens 1 A). Lieferumfang laut Datenblatt nur Gerät und USB-Kabel; eine
  Wand- oder Standhalterung ist nicht vorgesehen. Laut Datenblatt auf eine ebene Fläche stellen,
  damit es nicht umkippt, hinten Abstand zur Wand für die Belüftung lassen, keine Küche/Bad.

* Miner: `GET http://<adresse>/api/system/info` (AxeOS)
* Pins Panel: CS 27, DC 14, RST 12, BUSY 13, SCK 18, MOSI 23
  (aus der Analyse der Werks-Firmware in [iaon/GDP075FW1](https://github.com/iaon/GDP075FW1))
* Quellcode: `main.cpp` (Ablauf, WLAN, BOOT-Taste), `net.cpp` (Datenabruf), `render.cpp`
  (Zeichnen, Widgets, Vorschau, Prüf-Canvas, QR), `web.cpp` (Webseite, Editoren),
  `settings.cpp` (NVS, Layout, Farbrollen), `format.cpp` (Zahlen/Datum/Währungen),
  `test.cpp` (Prüfstand, nur im Build `dev`), gemeinsame Typen in `app.h`
* QR-Code: `libqrcode` aus dem ESP-IDF (im Arduino-Core enthalten)
* Treiber: GxEPD2 (`GxEPD2_750c_GDEM075F52`), Seitenweise gezeichnet (9,6 KB Puffer, da kein PSRAM).
  Die Vorschau zeichnet genauso streifenweise in einen eigenen Puffer und streamt ein BMP.
* Layout als Text im NVS, z. B. `block:0,0,13,6;moscow:13,0,7,3;…` (Inhalt:x,y,Breite,Höhe)
* Partitionen: `min_spiffs.csv`, zwei App-Bereiche à 1,875 MB für Updates über WLAN
  (Firmware ca. 1,3 MB). Einstellungen liegen im NVS (Namensraum `btcterm`), WLAN-Daten ebenfalls.
* Schriften: U8g2 (mit Umlauten), JSON: ArduinoJson mit Filtern, WLAN-Portal: WiFiManager
* HTTPS mit Zertifikatsprüfung (Root-CAs in [src/certs.h](src/certs.h): Sectigo/USERTrust,
  Let's Encrypt, Google, DigiCert, Amazon), eigener Node im Heimnetz per HTTP.
  `.local`-Namen werden per mDNS aufgelöst.
* Verwendete API-Endpunkte: `/api/v1/blocks`, `/api/v1/fees/precise`,
  `/api/v1/fees/mempool-blocks`, `/api/mempool`, `/api/v1/prices`,
  `/api/v1/difficulty-adjustment`, `/api/v1/mining/hashrate/3d`, `/api/blocks/tip/height`,
  `/api/v1/lightning/statistics/latest`, `/api/v1/historical-price` (nur der Anfang des
  Streams wird gelesen). Abgerufen wird nur, was im Layout vorkommt. Fehlt ein Endpunkt auf einem eigenen Node
  (z. B. Lightning oder Kurse), bleibt die Kachel leer, der Rest läuft weiter.

## Quellen der Ereignisse („An diesem Tag“)

Die Liste steht in [src/history_events.h](src/history_events.h), jedes Datum mit Quellenkürzel:

* **BLK:** Zeitstempel des Blocks laut mempool.space-API (Genesis, Block 1 und 170, Overflow-Block
  74.638, Halvings, Soft-Fork-Aktivierungen, jeder 100.000. Block)
* **SNI:** Original-E-Mails und Forenbeiträge von Satoshi Nakamoto beim Satoshi Nakamoto Institute
  (Whitepaper 31.10.2008, v0.1-Ankündigung 08.01.2009, letzter Forenbeitrag 12.12.2010 u. a.)
* **WP:** englische Wikipedia (History of bitcoin, Bitcoin, Satoshi Nakamoto, Mt. Gox, SegWit,
  Silk Road, Bitcoin in El Salvador, Legality of cryptocurrency, U.S. Strategic Bitcoin Reserve)
* **WEB:** mehrere übereinstimmende Berichte (z. B. Lightning Labs, CoinDesk, The Block, Nasdaq)

Daten sind Kalendertage in UTC bzw. wie in der Quelle angegeben. Wo Quellen sich widersprachen,
gilt die Primärquelle (z. B. v0.1: E-Mail vom 08.01.2009, nicht 09.01.) oder das Ereignis wurde
weggelassen (z. B. das Allzeithoch 2021, für das Wikipedia und Medien unterschiedliche Tage nennen).

## Veröffentlichen auf GitHub (Web-Flasher)

Der Web-Flasher braucht eine HTTPS-Adresse; GitHub Pages ist kostenlos und passt zum
offenzulegenden Quellcode. Vorbereitet sind `.gitignore` (u. a. **`backup/` wird nie
hochgeladen**, die Flash-Sicherung kann WLAN-Zugangsdaten enthalten), `LICENSE` (GPL-3.0) und
der Workflow [.github/workflows/release.yml](.github/workflows/release.yml).

Einmalig: Repository öffentlich, Settings → Pages → Source: „GitHub Actions“ (GitHub Pages
gibt es im kostenlosen Konto nur für öffentliche Repositories).

Für jede neue Version:
1. `FW_VERSION` in `src/config.h` erhöhen (Schema `MAJOR.MINOR.PATCH`), committen und pushen.
2. Einen passenden Tag pushen, z. B. `git tag v1.1.0` und `git push origin v1.1.0`.
3. Der Workflow prüft, dass Tag und `FW_VERSION` übereinstimmen, baut die Firmware, hängt
   `timechain-terminal-<version>.bin` an das Release (für das Update über die Geräte-Webseite)
   und veröffentlicht den Web-Flasher unter https://xxxecutor.github.io/timechain-terminal/.

## Lizenz

GPL-3.0 (siehe [LICENSE](LICENSE)), weil die Display-Bibliothek GxEPD2 unter der GPL-3.0 steht.
Wer die Firmware weitergibt (auch als Binärdatei im Web-Flasher), muss den Quellcode mitliefern.

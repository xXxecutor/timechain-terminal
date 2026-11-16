#pragma once

// ---------------------------------------------------------------------------
// Hardware: Good Display GDP075FW1 (Platine ESP32-M075)
// Pinbelegung des E-Paper-Panels, aus der Original-Firmware ermittelt.
// SPI: SCK = GPIO18, MOSI = GPIO23 (Standard-VSPI)
// ---------------------------------------------------------------------------
#define EPD_CS    27
#define EPD_DC    14
#define EPD_RST   12
#define EPD_BUSY  13

// BOOT-Taste auf der Platine. Beim Einschalten gedrueckt halten
// -> gespeicherte WLAN-Daten werden geloescht.
#define WIFI_RESET_PIN 0

// ---------------------------------------------------------------------------
// WLAN-Einrichtung (Captive Portal)
// ---------------------------------------------------------------------------
#define PRODUCT_NAME   "Timechain Terminal"
#define WIFI_AP_NAME   "Timechain-Terminal"
#define WIFI_HOSTNAME  "timechain-terminal"

// Firmware-Version (Web-Oberflaeche, Info-Bildschirm, Web-Flasher)
#define FW_VERSION "1.9.3"

// ---------------------------------------------------------------------------
// Datenquelle
// Standard ist die oeffentliche API von mempool.space. Im Einrichtungsportal
// und auf der Geraete-Webseite laesst sich ein eigener mempool-Server
// eintragen, z.B. http://umbrel.local:3006 (mempool-App auf Umbrel).
// ---------------------------------------------------------------------------
#define DEFAULT_NODE_URL "https://mempool.space"

// ---------------------------------------------------------------------------
// Aktualisierung
// Das 4-Farben-Panel braucht fuer jeden Bildwechsel ca. 27 s und
// unterstuetzt keine Teilaktualisierung. Neue Bloecke werden immer sofort
// gezeichnet; die Mindestpause gilt nur fuer den periodischen Refresh.
// ---------------------------------------------------------------------------
#define TIP_POLL_SECONDS       5    // so oft wird nach einem neuen Block gefragt
#define PERIODIC_REFRESH_MIN   10   // spaetestens dann neu zeichnen (Gebuehren, Kurs)
#define MIN_REFRESH_SECONDS    180  // Mindestpause fuer den periodischen Refresh
#define RETRY_SECONDS          60   // Wartezeit nach einem Fehler

// Zeitzone (Deutschland, mit Sommerzeit)
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

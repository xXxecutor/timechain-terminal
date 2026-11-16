// Bitcoin-Ereignisse fuer das Widget "Heute vor X Jahren" (Stand November 2026).
// Jedes Datum ist gegen mindestens eine Quelle geprueft; Kuerzel am Zeilenende:
//   BLK  = Zeitstempel des Blocks laut mempool.space-API (UTC)
//   SNI  = Satoshi Nakamoto Institute (Original-E-Mails und Forenbeitraege, UTC)
//   WP   = englische Wikipedia (History of bitcoin, Bitcoin, Satoshi Nakamoto, Mt. Gox, SegWit,
//          Silk Road, Bitcoin in El Salvador, Legality of cryptocurrency, U.S. Strategic Bitcoin Reserve u. a.)
//   WEB  = mehrere uebereinstimmende Berichte (z. B. Lightning Labs, CoinDesk, The Block, Nasdaq)
// Datum = Kalendertag in UTC bzw. wie in der Quelle angegeben.
#pragma once
#include <stdint.h>

struct HistEvent {
  uint16_t y;
  uint8_t m, d;
  const char *de, *en;
};

static const HistEvent EVENTS[] = {
    // ---- 2008 - 2009: Entstehung
    {2008, 8, 18, "Die Domain bitcoin.org wird registriert", "The domain bitcoin.org is registered"},  // WP
    {2008, 10, 31, "Satoshi Nakamoto stellt das Bitcoin-Whitepaper auf einer Kryptografie-Mailingliste vor",
     "Satoshi Nakamoto announces the Bitcoin whitepaper on a cryptography mailing list"},  // SNI, WP
    {2009, 1, 3, "Der Genesis-Block (Block 0) wird gemint", "The genesis block (block 0) is mined"},  // BLK
    {2009, 1, 8, "Satoshi kündigt per E-Mail die Veröffentlichung von Bitcoin v0.1 an",
     "Satoshi announces the release of Bitcoin v0.1 by email"},  // SNI
    {2009, 1, 9, "Block 1 wird gemint, sechs Tage nach dem Genesis-Block",
     "Block 1 is mined, six days after the genesis block"},  // BLK
    {2009, 1, 10, "Hal Finney twittert »Running bitcoin«", "Hal Finney tweets »Running bitcoin«"},  // WEB
    {2009, 1, 12, "Erste Transaktion: Satoshi sendet Hal Finney 10 BTC (Block 170)",
     "First transaction: Satoshi sends Hal Finney 10 BTC (block 170)"},  // BLK, WP
    {2009, 2, 11, "Satoshi stellt Bitcoin im Forum der P2P Foundation vor",
     "Satoshi presents Bitcoin on the P2P Foundation forum"},  // SNI
    {2009, 10, 5, "Erster Wechselkurs: 1 $ = 1.309,03 BTC (New Liberty Standard)",
     "First exchange rate: $1 = 1,309.03 BTC (New Liberty Standard)"},  // WEB
    {2009, 11, 22, "Satoshi eröffnet das neue Bitcoin-Forum, den Vorläufer von Bitcointalk",
     "Satoshi opens the new Bitcoin forum, the forerunner of Bitcointalk"},  // SNI
    {2009, 12, 17, "Bitcoin 0.2 wird veröffentlicht", "Bitcoin 0.2 is released"},  // SNI
    // ---- 2010 - 2011
    {2010, 5, 22, "Bitcoin Pizza Day: Laszlo Hanyecz kauft zwei Pizzen für 10.000 BTC",
     "Bitcoin Pizza Day: Laszlo Hanyecz buys two pizzas for 10,000 BTC"},  // WP
    {2010, 7, 6, "Bitcoin 0.3 wird veröffentlicht", "Bitcoin 0.3 is released"},  // SNI
    {2010, 7, 18, "Mt. Gox startet als Bitcoin-Börse", "Mt. Gox launches as a bitcoin exchange"},  // WEB, WP
    {2010, 8, 15, "Value-Overflow-Fehler: Block 74.638 erzeugt 184 Mrd. BTC, Satoshi schlägt Alarm",
     "Value overflow bug: block 74,638 creates 184 billion BTC, Satoshi sounds the alarm"},  // BLK, SNI
    {2010, 12, 12, "Satoshi Nakamoto schreibt seinen letzten Forumsbeitrag (Version 0.3.19)",
     "Satoshi Nakamoto writes his last forum post (version 0.3.19)"},  // SNI
    {2010, 12, 29, "Block 100.000 wird gemint", "Block 100,000 is mined"},  // BLK
    {2011, 2, 9, "Bitcoin erreicht auf Mt. Gox erstmals 1 US-Dollar", "Bitcoin reaches $1 on Mt. Gox for the first time"},  // WEB
    {2011, 4, 26, "Letztes bekanntes Lebenszeichen von Satoshi Nakamoto", "Last known sign of life from Satoshi Nakamoto"},  // WP
    {2011, 6, 19, "Mt. Gox wird gehackt", "Mt. Gox is hacked"},  // WP
    // ---- 2012 - 2014
    {2012, 4, 1, "Das Soft-Fork-Update P2SH (BIP 16) wird aktiv", "The P2SH soft fork (BIP 16) activates"},  // BLK
    {2012, 9, 22, "Block 200.000 wird gemint", "Block 200,000 is mined"},  // BLK
    {2012, 11, 28, "Erstes Halving bei Block 210.000: Belohnung sinkt auf 25 BTC",
     "First halving at block 210,000: the reward drops to 25 BTC"},  // BLK
    {2013, 3, 12, "Eine Abspaltung der Blockchain durch Version 0.8 wird nach Stunden behoben",
     "A chain split caused by version 0.8 is resolved within hours"},  // WP, BLK (Block 225.430)
    {2013, 3, 18, "Die US-Behörde FinCEN veröffentlicht Regeln für virtuelle Währungen",
     "US agency FinCEN issues guidance on virtual currencies"},  // WP
    {2013, 8, 6, "Ein US-Bundesrichter stuft Bitcoin als Geld bzw. Währung ein",
     "A US federal judge rules bitcoin is a currency or form of money"},  // WP
    {2013, 10, 1, "Das FBI verhaftet Ross Ulbricht, den Betreiber von Silk Road",
     "The FBI arrests Ross Ulbricht, operator of Silk Road"},  // WP
    {2013, 10, 29, "Der erste Bitcoin-Automat geht in Vancouver in Betrieb",
     "The first bitcoin ATM opens in Vancouver"},  // WP
    {2013, 11, 28, "Bitcoin übersteigt auf Mt. Gox erstmals 1.000 $", "Bitcoin passes $1,000 on Mt. Gox for the first time"},  // WP
    {2013, 12, 5, "Chinas Zentralbank verbietet Finanzinstituten den Umgang mit Bitcoin",
     "China's central bank bars financial institutions from handling bitcoin"},  // WP
    {2014, 2, 7, "Mt. Gox stoppt alle Bitcoin-Auszahlungen", "Mt. Gox halts all bitcoin withdrawals"},  // WP
    {2014, 2, 24, "Mt. Gox stellt den Handel ein, die Website geht offline",
     "Mt. Gox suspends trading, its website goes offline"},  // WP
    {2014, 2, 28, "Mt. Gox beantragt in Tokio Gläubigerschutz", "Mt. Gox files for bankruptcy protection in Tokyo"},  // WP
    {2014, 3, 19, "Bitcoin 0.9.0 erscheint, der Client heißt ab jetzt Bitcoin Core",
     "Bitcoin 0.9.0 is released, the client is renamed Bitcoin Core"},  // WEB
    {2014, 5, 10, "Block 300.000 wird gemint", "Block 300,000 is mined"},  // BLK
    {2014, 8, 28, "Bitcoin-Pionier Hal Finney stirbt", "Bitcoin pioneer Hal Finney dies"},  // WP
    // ---- 2015 - 2017
    {2015, 5, 29, "Ross Ulbricht wird zu lebenslanger Haft verurteilt", "Ross Ulbricht is sentenced to life in prison"},  // WP
    {2016, 2, 25, "Block 400.000 wird gemint", "Block 400,000 is mined"},  // BLK
    {2016, 7, 4, "Das Soft-Fork-Update CSV (BIP 68, 112, 113) wird aktiv",
     "The CSV soft fork (BIP 68, 112, 113) activates"},  // BLK (Block 419.328)
    {2016, 7, 9, "Zweites Halving bei Block 420.000: Belohnung sinkt auf 12,5 BTC",
     "Second halving at block 420,000: the reward drops to 12.5 BTC"},  // BLK
    {2016, 8, 2, "Bei der Börse Bitfinex werden 119.756 BTC gestohlen", "119,756 BTC are stolen from the Bitfinex exchange"},  // WEB
    {2017, 4, 1, "Japan erkennt Bitcoin per Gesetz als Zahlungsmittel an",
     "A Japanese law recognises bitcoin as a means of payment"},  // WEB
    {2017, 5, 20, "Bitcoin steigt erstmals über 2.000 $", "Bitcoin rises above $2,000 for the first time"},  // WP
    {2017, 7, 21, "Miner aktivieren BIP 91 und ebnen den Weg für SegWit",
     "Miners activate BIP 91, clearing the way for SegWit"},  // WP
    {2017, 8, 1, "Bitcoin Cash spaltet sich bei Block 478.559 ab", "Bitcoin Cash splits off at block 478,559"},  // WP
    {2017, 8, 9, "SegWit ist »locked in«: alle Miner signalisieren Zustimmung",
     "SegWit is locked in: all miners signal support"},  // WP
    {2017, 8, 24, "SegWit wird bei Block 481.824 aktiviert", "SegWit activates at block 481,824"},  // BLK, WP
    {2017, 11, 8, "Die geplante Abspaltung SegWit2x wird abgesagt", "The planned SegWit2x hard fork is cancelled"},  // WP
    {2017, 12, 10, "Die ersten Bitcoin-Futures starten an der Cboe", "The first bitcoin futures launch on Cboe"},  // WEB
    {2017, 12, 17, "Allzeithoch bei 19.783 $; am selben Tag starten Bitcoin-Futures an der CME",
     "All-time high at $19,783; bitcoin futures launch on CME the same day"},  // WP, WEB
    {2017, 12, 18, "Block 500.000 wird gemint", "Block 500,000 is mined"},  // BLK
    // ---- 2018 - 2020
    {2018, 3, 15, "Lightning Labs veröffentlicht lnd als erste Beta für das Mainnet",
     "Lightning Labs releases lnd as the first beta for mainnet"},  // WEB
    {2019, 1, 19, "Start der »Lightning Torch«: eine Zahlung wandert um die Welt",
     "The »Lightning Torch« starts its journey around the world"},  // WP
    {2019, 10, 19, "Block 600.000 wird gemint", "Block 600,000 is mined"},  // BLK
    {2020, 1, 1, "In Deutschland dürfen Banken ab heute Kryptowerte verkaufen und verwahren",
     "German banks may sell and hold crypto assets from today"},  // WP
    {2020, 5, 11, "Drittes Halving bei Block 630.000: Belohnung sinkt auf 6,25 BTC",
     "Third halving at block 630,000: the reward drops to 6.25 BTC"},  // BLK
    {2020, 7, 2, "Erste Bitcoin-ETPs werden auf Xetra gehandelt (21Shares)",
     "First bitcoin ETPs trade on Xetra (21Shares)"},  // WP
    {2020, 8, 11, "MicroStrategy kauft 21.454 BTC als Reserve", "MicroStrategy buys 21,454 BTC as a treasury reserve"},  // WEB
    {2020, 9, 3, "Die Börse Frankfurt lässt das erste Bitcoin-ETN im regulierten Markt zu",
     "Frankfurt Stock Exchange admits the first bitcoin ETN to its regulated market"},  // WP
    {2020, 11, 30, "Neues Allzeithoch bei 19.850 $", "New all-time high at $19,850"},  // WP
    // ---- 2021
    {2021, 2, 8, "Tesla gibt einen Bitcoin-Kauf für 1,5 Mrd. $ bekannt", "Tesla announces a $1.5 billion bitcoin purchase"},  // WP
    {2021, 2, 16, "Bitcoin erreicht erstmals 50.000 $", "Bitcoin reaches $50,000 for the first time"},  // WP
    {2021, 4, 14, "Coinbase geht an die Nasdaq, Bitcoin erreicht 64.800 $",
     "Coinbase lists on Nasdaq, bitcoin reaches $64,800"},  // WP
    {2021, 5, 12, "Tesla nimmt doch keine Bitcoin mehr an", "Tesla stops accepting bitcoin again"},  // WP
    {2021, 6, 5, "Präsident Bukele kündigt Bitcoin als Zahlungsmittel für El Salvador an",
     "President Bukele announces bitcoin as legal tender for El Salvador"},  // WP
    {2021, 6, 9, "El Salvadors Parlament beschließt das Bitcoin-Gesetz", "El Salvador's parliament passes the Bitcoin Law"},  // WP
    {2021, 6, 12, "Taproot ist »locked in«", "Taproot is locked in"},  // WEB
    {2021, 9, 7, "Bitcoin wird in El Salvador gesetzliches Zahlungsmittel", "Bitcoin becomes legal tender in El Salvador"},  // WP
    {2021, 9, 11, "Block 700.000 wird gemint", "Block 700,000 is mined"},  // BLK
    {2021, 9, 24, "China verbietet Handel und Mining von Kryptowährungen vollständig",
     "China fully bans cryptocurrency trading and mining"},  // WP
    {2021, 11, 14, "Taproot wird bei Block 709.632 aktiviert", "Taproot activates at block 709,632"},  // BLK
    // ---- 2022 - 2023
    {2022, 4, 27, "Bitcoin wird in der Zentralafrikanischen Republik gesetzliches Zahlungsmittel",
     "Bitcoin becomes legal tender in the Central African Republic"},  // WP
    {2022, 11, 11, "Die Börse FTX meldet Insolvenz an", "The FTX exchange files for bankruptcy"},  // WP
    {2023, 7, 24, "Block 800.000 wird gemint", "Block 800,000 is mined"},  // BLK
    // ---- 2024 - 2025
    {2024, 1, 10, "Die US-Börsenaufsicht SEC genehmigt elf Spot-Bitcoin-ETFs", "The US SEC approves eleven spot bitcoin ETFs"},  // WEB
    {2024, 1, 11, "Der Handel mit Spot-Bitcoin-ETFs in den USA beginnt", "Trading of US spot bitcoin ETFs begins"},  // WEB
    {2024, 3, 13, "Neues Allzeithoch bei 73.664 $", "New all-time high at $73,664"},  // WP
    {2024, 4, 20, "Viertes Halving bei Block 840.000: Belohnung sinkt auf 3,125 BTC",
     "Fourth halving at block 840,000: the reward drops to 3.125 BTC"},  // BLK
    {2024, 5, 20, "Britisches Urteil: Craig Wright ist nicht Satoshi, seine Belege sind gefälscht",
     "UK judgment: Craig Wright is not Satoshi, his evidence was forged"},  // WP
    {2024, 7, 27, "Donald Trump kündigt eine strategische Bitcoin-Reserve an",
     "Donald Trump announces a strategic bitcoin reserve"},  // WP
    {2024, 12, 5, "Bitcoin steigt erstmals über 100.000 $", "Bitcoin rises above $100,000 for the first time"},  // WP
    {2025, 1, 21, "Ross Ulbricht wird von Präsident Trump begnadigt", "Ross Ulbricht is pardoned by President Trump"},  // WP
    {2025, 3, 6, "Die USA richten per Dekret eine strategische Bitcoin-Reserve ein",
     "A US executive order creates a Strategic Bitcoin Reserve"},  // WP
    {2025, 6, 6, "Block 900.000 wird gemint", "Block 900,000 is mined"},  // BLK
    {2025, 6, 22, "Texas richtet per Gesetz eine eigene Bitcoin-Reserve ein", "Texas creates its own bitcoin reserve by law"},  // WP
    {2025, 10, 6, "Neues Allzeithoch bei 126.210 $", "New all-time high at $126,210"},  // WP
};
static const int N_EVENTS = sizeof EVENTS / sizeof EVENTS[0];

"""Erstellt aus dem letzten Build die Dateien fuer Web-Flasher und WLAN-Update.

Aufruf (nach `python -m platformio run`):
    python scripts/make_release.py

Ergebnis:
    web-flasher/firmware/*.bin      Teile fuer den Browser-Flasher (ESP Web Tools)
    web-flasher/manifest.json       Version aus src/config.h
    release/timechain-terminal-<version>.bin   Datei fuer das Update ueber die Geraete-Webseite
"""
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio" / "build" / "gdp075fw1"
BOOT_APP0 = (pathlib.Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32"
             / "tools" / "partitions" / "boot_app0.bin")

version = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"',
                    (ROOT / "src" / "config.h").read_text(encoding="utf-8")).group(1)

# Offsets wie beim Flashen per PlatformIO (ESP32, Partitionstabelle min_spiffs.csv)
parts = [
    ("bootloader.bin", BUILD / "bootloader.bin", 0x1000),
    ("partitions.bin", BUILD / "partitions.bin", 0x8000),
    ("boot_app0.bin", BOOT_APP0, 0xE000),
    ("firmware.bin", BUILD / "firmware.bin", 0x10000),
]
for name, src, _ in parts:
    if not src.exists():
        sys.exit(f"fehlt: {src} - erst `python -m platformio run` ausfuehren")

out = ROOT / "web-flasher" / "firmware"
out.mkdir(parents=True, exist_ok=True)
for name, src, _ in parts:
    shutil.copyfile(src, out / name)

manifest = {
    "name": "Timechain Terminal (GDP075FW1)",
    "version": version,
    "new_install_prompt_erase": True,
    "builds": [{
        "chipFamily": "ESP32",
        "parts": [{"path": f"firmware/{name}", "offset": off} for name, _, off in parts],
    }],
}
(ROOT / "web-flasher" / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                                    encoding="utf-8")

rel = ROOT / "release"
rel.mkdir(exist_ok=True)
shutil.copyfile(BUILD / "firmware.bin", rel / f"timechain-terminal-{version}.bin")

size = (BUILD / "firmware.bin").stat().st_size
print(f"Version {version}: firmware.bin {size / 1024:.0f} KB (Platz: 1920 KB)")
print(f"  {out}")
print(f"  {rel / f'timechain-terminal-{version}.bin'}")

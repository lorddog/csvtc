# CsvTC

Ein [Total Commander](https://www.ghisler.com/) Lister-Plugin (WLX) zum Anzeigen
und Bearbeiten von CSV-Dateien – direkt mit F3 oder in der Schnellansicht
(Strg+Q).

Die Tabelle wird selbst gezeichnet (GDI, nur sichtbare Zellen), das Plugin ist
natives C++ ohne Abhängigkeiten außer Windows-Systembibliotheken.

- [English version](README.en.md)

> **Status:** Version 0.1

## Funktionen

- **Trenner wird automatisch erkannt** (Komma, Semikolon, Tab, senkrechter
  Strich) – auch bei Dezimalkommas und Trennern in Anführungszeichen. Mit **S**
  lässt er sich umschalten, falls die Erkennung danebenlag.
- **Farbschema wie das TC-Hauptfenster**: Hell- und Darkmode werden von Total
  Commander übernommen (`lcp_darkmode`), die Farben aus `wincmd.ini`
  (`[Colors]` bzw. `[ColorsDark]`). Mit **T** kann man auf das Farbschema des
  Markdown-Readers (Catppuccin Mocha) umschalten; die Wahl wird gespeichert.
- **Deutsch oder Englisch**: Die Oberfläche folgt der Spracheinstellung von
  Total Commander (siehe [Sprache](#sprache)).
- **Felder bearbeiten** mit **E**, Zeilen einfügen, Zeilen und Spalten löschen,
  Rückgängig
- **Byte-genaues Speichern**: unveränderte Zeilen werden exakt so geschrieben,
  wie sie in der Datei standen (Anführungszeichen, Zeilenenden, Kodierung,
  BOM). Nur geänderte Zeilen werden neu erzeugt. Gespeichert wird über eine
  temporäre Datei, die das Original erst am Ende ersetzt.
- Kodierungen: UTF-8 (mit/ohne BOM), UTF-16 LE/BE, ANSI
- Mehrzeilige Felder (Zeilenumbrüche in Anführungszeichen) werden mit ↵
  angezeigt und lassen sich bearbeiten
- Erste Zeile als fixierte Kopfzeile (automatisch erkannt, umschaltbar mit **H**)
- Zahlen rechtsbündig, Spaltenbreiten automatisch, per Maus veränderbar
- Suche über die Lister-Suche von TC (F7 / Strg+F, weiter mit F3)
- Kopieren (Strg+C): Feld bzw. markierte Zeilen als CSV
- Prozentanzeige im Lister-Menü, DPI-bewusst, Zoom mit Strg+Mausrad

## Tastatur

| Taste | Wirkung |
|---|---|
| Pfeiltasten, Bild ↑/↓, Pos1/Ende | Zelle wechseln |
| Strg+Pos1 / Strg+Ende | Anfang / Ende der Tabelle |
| **Shift**+Pfeiltasten, Shift+Klick | Zeilen markieren |
| Klick auf Zeilennummer · Strg+A | Zeile · alle Zeilen markieren |
| **E**, F2, Doppelklick | Feld bearbeiten – Enter übernimmt, Esc verwirft, Tab springt weiter, Shift+Enter fügt einen Zeilenumbruch ein |
| **i i** | leere Zeile an der Cursorposition einfügen (die aktuelle Zeile rutscht nach unten; auf der Kopfzeile: direkt darunter) |
| **d d** | markierte Zeile(n) bzw. aktuelle Zeile löschen |
| **Shift+D Shift+D** | aktuelle Spalte löschen |
| Strg+Z | Rückgängig (Bearbeiten, Einfügen, Löschen) |
| Strg+S | Speichern |
| Strg+C | Feld bzw. markierte Zeilen kopieren |
| **S** / Shift+S | Trenner vor/zurück: `,` `;` Tab `\|` |
| **H** | erste Zeile als Kopfzeile an/aus |
| **T** | Farbschema Total Commander ↔ Markdown-Reader |
| Strg+Mausrad, Strg +/−/0 | Schriftgröße |
| F1 | Tastenübersicht |
| **Esc** | Lister schließen – bei Änderungen kommt vorher „Speichern? Ja/Nein/Abbrechen“ |
| N / P, 1–8, F3, F7 | gehen an Total Commander |

Rechtsklick öffnet ein Kontextmenü mit denselben Funktionen.

„i i“, „d d“ und „Shift+D Shift+D“ müssen innerhalb von 1,5 Sekunden gedrückt
werden; die Statuszeile zeigt nach dem ersten Druck an, was passieren würde.
Jede andere Taste bricht ab. Die eingefügte Zeile bekommt so viele leere Felder,
wie die Tabelle Spalten hat.

Wird der Lister anders geschlossen (X, Alt+F4, N/P, Tasten 1–8, Cursor in der
Schnellansicht), fragt das Plugin ebenfalls nach – dann nur mit Ja/Nein, weil
Total Commander das Schließen nicht abbrechen lässt.

### Kopfzeile

Beim Öffnen rät das Plugin, ob die erste Zeile eine Kopfzeile ist: Enthält sie
Text, aber keine Zahlen, wird sie als fixierte Kopfzeile angezeigt. Sonst stehen
oben Spaltennummern (1, 2, 3 …). Mit **H** lässt sich das umschalten; die Wahl
gilt für die geöffnete Datei.

### Trenner umschalten bei ungespeicherten Änderungen

Wechselt man mit **S** den Trenner, nachdem schon etwas geändert wurde, wird der
aktuelle Stand mit dem alten Trenner zusammengesetzt und mit dem neuen wieder
zerlegt – es geht nichts verloren, nur der Undo-Verlauf beginnt neu.

## Sprache

Menü, Statuszeile, Meldungen und Tastenübersicht gibt es auf Deutsch und
Englisch. Das Plugin richtet sich nach der Sprache von Total Commander
(**Konfiguration → Einstellungen → Sprache**, in `wincmd.ini` der Eintrag
`LanguageIni`):

- TC auf Deutsch (`WCMD_DEU.LNG`) → Deutsch
- jede andere Sprache, auch TC ohne Sprachdatei (Englisch) → Englisch

Die Sprache wird bei jedem Öffnen einer Datei neu gelesen. Wer unabhängig von TC
eine feste Sprache möchte, setzt `Language=de` bzw. `Language=en` (siehe
[Einstellungen](#einstellungen)).

Die Knöpfe der Rückfragen (Ja/Nein/Abbrechen) beschriftet Windows in seiner
eigenen Sprache.

## Installation

1. `csvtc.zip` in Total Commander öffnen (Enter) – TC bietet die Installation
   über `pluginst.inf` automatisch an.
2. Alternativ: unter **Konfiguration → Einstellungen → Plugins →
   Lister-Plugins** `csvtc.wlx64` bzw. `csvtc.wlx` hinzufügen.

Erkannt werden `*.csv`, `*.tsv` und `*.tab`. Dateien über 256 MB (32-Bit: 64 MB)
überlässt das Plugin dem internen Lister.

## Hinweise

- **Archive/FTP:** Dateien aus Archiven entpackt TC in einen temporären Ordner
  (`%TEMP%\_tc\`). Speichern ändert dann nur diese Kopie – die Statuszeile
  weist darauf hin.
- **ANSI-Dateien:** Enthält eine ANSI-Datei nach dem Bearbeiten Zeichen, die
  die Codepage nicht darstellen kann, fragt das Plugin, ob als UTF-8 (mit BOM)
  gespeichert werden soll.
- Dateien ohne BOM, die nur ASCII enthalten, gelten als UTF-8.

## Einstellungen

Im Abschnitt `[CsvTC]` der `lsplugin.ini` von Total Commander:

| Schlüssel | Bedeutung |
|---|---|
| `Theme=tc` / `markdown` | Farbschema (wird mit **T** gesetzt) |
| `FontSize=10` | Schriftgröße in Punkt (Strg+Mausrad) |
| `Language=auto` / `de` / `en` | Sprache; `auto` (Standard) folgt Total Commander |
| `Log=1` | Diagnose-Log nach `%TEMP%\csvtc.log` (ShowFlags, Sprache, Tasten) |

## Bauen

Benötigt Visual Studio 2022/2026 (MSVC) und CMake 3.25+.

```powershell
.\build.ps1                 # x64 + x86, Release, inkl. Tests
.\build.ps1 -Arch x64       # nur 64 Bit
.\build.ps1 -Zip            # zusätzlich csvtc.zip zur Installation
```

Ausgabe in `dist\`:

| Datei | Architektur |
|---|---|
| `dist\csvtc.wlx64` | 64 Bit |
| `dist\csvtc.wlx` | 32 Bit |

### Tests

- `build64\bin\csvtest.exe` – Tests für das Datenmodell (Byte-genauer
  Roundtrip über Kodierungen und Zeilenenden, Trenner-Erkennung, Bearbeiten,
  Löschen, Undo, Speichern, Sprachwahl). Läuft bei `build.ps1` automatisch mit.
- `harness\run.ps1` – lädt das Plugin wie Total Commander, ohne TC, und spielt
  Tasten, Klicks und Screenshots ab:

```powershell
.\harness\run.ps1 -File .\testdata\kunden.csv -Flags 384 -Actions @(
    "--at 500 key 0x28", "--at 600 key S+0x28", "--at 800 shot {out}\auswahl.png",
    "--at 900 key 0x1B", "--at 1300 answer yes", "--at 3000 exit")
```

`-Flags 384` = `lcp_darkmode | lcp_darkmodenative`. Die Datei wird vorher nach
`testdata\out\` kopiert, das Original bleibt unverändert. Die Sprache kommt wie
im echten TC aus der `wincmd.ini`, auf die `COMMANDER_INI` zeigt.

## Lizenz

Copyright (c) 2026 LordDog

Open source — feel free to use, modify, and distribute. Credits are appreciated but not required.

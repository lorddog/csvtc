# CsvTC

A [Total Commander](https://www.ghisler.com/) lister plugin (WLX) for viewing
and editing CSV files – directly with F3 or in Quick View (Ctrl+Q).

The table is drawn by the plugin itself (GDI, visible cells only). The plugin is
native C++ with no dependencies besides the Windows system libraries.

- [Deutsche Version](README.de.md)

> **Status:** version 0.1

## Features

- **Automatic separator detection** (comma, semicolon, tab, pipe) – also with
  decimal commas and separators inside quotes. Press **S** to switch if the
  detection got it wrong.
- **Same color scheme as the TC main window**: light and dark mode are taken
  from Total Commander (`lcp_darkmode`), the colors from `wincmd.ini`
  (`[Colors]` or `[ColorsDark]`). Press **T** to switch to the color scheme of
  the Markdown reader (Catppuccin Mocha); the choice is saved.
- **English or German**: the user interface follows the language setting of
  Total Commander (see [Language](#language)).
- **Edit fields** with **E**, insert rows, delete rows and columns, undo
- **Byte-exact saving**: unchanged rows are written exactly as they were in the
  file (quotes, line endings, encoding, BOM). Only changed rows are rebuilt.
  Saving goes through a temporary file that replaces the original only at the
  end.
- Encodings: UTF-8 (with/without BOM), UTF-16 LE/BE, ANSI
- Multi-line fields (line breaks inside quotes) are shown with ↵ and can be
  edited
- First row as a frozen header (detected automatically, toggle with **H**)
- Numbers right-aligned, column widths automatic, adjustable with the mouse
- Search via TC's lister search (F7 / Ctrl+F, continue with F3)
- Copy (Ctrl+C): field or selected rows as CSV
- Percentage display in the lister menu, DPI-aware, zoom with Ctrl+mouse wheel

## Keyboard

| Key | Action |
|---|---|
| Arrow keys, PgUp/PgDn, Home/End | Move between cells |
| Ctrl+Home / Ctrl+End | Start / end of the table |
| **Shift**+arrow keys, Shift+click | Select rows |
| Click on row number · Ctrl+A | Select row · all rows |
| **E**, F2, double-click | Edit field – Enter accepts, Esc discards, Tab moves to the next field, Shift+Enter adds a line break |
| **i i** | Insert an empty row at the cursor (the current row moves down; on the header: right below it) |
| **d d** | Delete the selected row(s) or the current row |
| **Shift+D Shift+D** | Delete the current column |
| Ctrl+Z | Undo (edit, insert, delete) |
| Ctrl+S | Save |
| Ctrl+C | Copy field or selected rows |
| **S** / Shift+S | Next/previous separator: `,` `;` Tab `\|` |
| **H** | First row as header on/off |
| **T** | Color scheme Total Commander ↔ Markdown reader |
| Ctrl+mouse wheel, Ctrl +/−/0 | Font size |
| F1 | Keyboard shortcuts |
| **Esc** | Close the lister – if there are changes, asks “Save? Yes/No/Cancel” first |
| N / P, 1–8, F3, F7 | Passed on to Total Commander |

Right-click opens a context menu with the same functions.

“i i”, “d d” and “Shift+D Shift+D” must be pressed within 1.5 seconds; after the
first key press the status bar shows what would happen. Any other key cancels.
The inserted row gets as many empty fields as the table has columns.

If the lister is closed any other way (X, Alt+F4, N/P, keys 1–8, cursor
movement in Quick View), the plugin asks as well – but only Yes/No, because
Total Commander does not allow cancelling the close.

### Header row

When a file is opened, the plugin guesses whether the first row is a header: if
it contains text but no numbers, it is shown as a frozen header. Otherwise the
top row shows column numbers (1, 2, 3 …). Press **H** to toggle; the choice
applies to the open file.

### Changing the separator with unsaved changes

If you switch the separator with **S** after making changes, the current state
is joined with the old separator and split again with the new one – nothing is
lost, only the undo history starts over.

## Language

Menu, status bar, messages and the keyboard overview are available in English
and German. The plugin follows the language of Total Commander
(**Configuration → Options → Language**, the `LanguageIni` entry in
`wincmd.ini`):

- TC in German (`WCMD_DEU.LNG`) → German
- any other language, including TC without a language file (English) → English

The language is read again every time a file is opened. To use a fixed language
regardless of TC, set `Language=de` or `Language=en` (see
[Settings](#settings)).

The buttons of the prompts (Yes/No/Cancel) are labeled by Windows in its own
language.

## Installation

1. Open `csvtc.zip` in Total Commander (Enter) – TC offers to install it via
   `pluginst.inf`.
2. Alternatively: add `csvtc.wlx64` or `csvtc.wlx` under **Configuration →
   Options → Plugins → Lister plugins**.

The plugin handles `*.csv`, `*.tsv` and `*.tab`. Files larger than 256 MB
(32-bit: 64 MB) are left to the internal lister.

## Notes

- **Archives/FTP:** TC extracts files from archives to a temporary folder
  (`%TEMP%\_tc\`). Saving then only changes that copy – the status bar points
  this out.
- **ANSI files:** if an ANSI file contains characters after editing that the
  code page cannot represent, the plugin asks whether to save as UTF-8 (with
  BOM) instead.
- Files without a BOM that contain only ASCII are treated as UTF-8.

## Settings

In the `[CsvTC]` section of Total Commander's `lsplugin.ini`:

| Key | Meaning |
|---|---|
| `Theme=tc` / `markdown` | Color scheme (set with **T**) |
| `FontSize=10` | Font size in points (Ctrl+mouse wheel) |
| `Language=auto` / `de` / `en` | Language; `auto` (default) follows Total Commander |
| `Log=1` | Diagnostic log to `%TEMP%\csvtc.log` (ShowFlags, language, keys) |

## Building

Requires Visual Studio 2022/2026 (MSVC) and CMake 3.25+.

```powershell
.\build.ps1                 # x64 + x86, Release, including tests
.\build.ps1 -Arch x64       # 64-bit only
.\build.ps1 -Zip            # also create csvtc.zip for installation
```

Output in `dist\`:

| File | Architecture |
|---|---|
| `dist\csvtc.wlx64` | 64-bit |
| `dist\csvtc.wlx` | 32-bit |

### Tests

- `build64\bin\csvtest.exe` – tests for the data model (byte-exact round trip
  across encodings and line endings, separator detection, editing, deleting,
  undo, saving, language selection). Runs automatically with `build.ps1`.
- `harness\run.ps1` – loads the plugin the way Total Commander does, without
  TC, and plays back keys, clicks and screenshots:

```powershell
.\harness\run.ps1 -File .\testdata\kunden.csv -Flags 384 -Actions @(
    "--at 500 key 0x28", "--at 600 key S+0x28", "--at 800 shot {out}\selection.png",
    "--at 900 key 0x1B", "--at 1300 answer yes", "--at 3000 exit")
```

`-Flags 384` = `lcp_darkmode | lcp_darkmodenative`. The file is copied to
`testdata\out\` first, so the original stays unchanged. As in real TC, the
language comes from the `wincmd.ini` that `COMMANDER_INI` points to.

## License

Copyright (c) 2026 LordDog

Open source — feel free to use, modify, and distribute. Credits are appreciated but not required.

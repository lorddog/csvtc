/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 *
 * Farbschemata: "Total Commander" übernimmt die Farben des Hauptfensters
 * (wincmd.ini [Colors] bzw. [ColorsDark] im Darkmode), "Markdown" ist das
 * Catppuccin-Mocha-Schema des MarkdownTC-Readers.
 */
#pragma once
#include <windows.h>

enum class ThemeMode { TotalCommander, Markdown };

struct Palette {
    bool     dark = false;
    COLORREF bg, bgAlt, fg;
    COLORREF grid;
    COLORREF headerBg, headerFg;
    COLORREF gutterBg, gutterFg;
    COLORREF cursorBg, cursorFg, cursorInactiveBg;
    COLORREF rowHighlight;
    COLORREF selBg, selFg, selMarker;
    COLORREF accent;
    COLORREF number;
    COLORREF statusBg, statusFg, statusWarn, statusOk;
    COLORREF editBg, editFg, editBorder;
};

namespace Theme {

// Ist Total Commander gerade im Darkmode? Bevorzugt aus den ShowFlags
// (lcp_darkmode), sonst aus wincmd.ini DarkMode=0/1/2.
bool    TcIsDark(int showFlags);
Palette Make(ThemeMode mode, bool tcDark);

COLORREF Blend(COLORREF a, COLORREF b, double t);

} // namespace Theme

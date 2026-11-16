/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 */
#include "Theme.h"
#include "Settings.h"
#include "listplug.h"
#include <optional>
#include <string>
#include <cwchar>
#include <cwctype>

namespace {

std::wstring IniString(const std::wstring& ini, const wchar_t* section, const wchar_t* key)
{
    wchar_t buf[512];
    GetPrivateProfileStringW(section, key, L"", buf, 512, ini.c_str());
    return buf;
}

// Werte: dezimal (R + 256*G + 65536*B) oder hex "$BBGGRR"; -1 = Standard.
std::optional<COLORREF> IniColor(const std::wstring& ini, const wchar_t* section, const wchar_t* key)
{
    if (ini.empty()) return std::nullopt;
    std::wstring v = IniString(ini, section, key);
    size_t a = 0;
    while (a < v.size() && iswspace(v[a])) ++a;
    v.erase(0, a);
    if (v.empty()) return std::nullopt;
    long long value = 0;
    if (v[0] == L'$') value = (long long)wcstoull(v.c_str() + 1, nullptr, 16);
    else value = wcstoll(v.c_str(), nullptr, 10);
    if (value < 0) return std::nullopt;
    return (COLORREF)(value & 0xFFFFFF);
}

bool WindowsAppsDark()
{
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return false;
    return value == 0;
}

} // namespace

namespace Theme {

COLORREF Blend(COLORREF a, COLORREF b, double t)
{
    auto mix = [t](int x, int y) { return (BYTE)(x + (y - x) * t + 0.5); };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

bool TcIsDark(int showFlags)
{
    if (showFlags & lcp_darkmode) return true;
    if (Settings::DarkFlagsReliable()) return false;
    // Ohne verlässliche Flags (alte TC-Version, Test-Host): wincmd.ini
    // [Configuration] DarkMode = 0 aus, 1 wie Apps, 2 ein.
    std::wstring ini = Settings::TcIniPath();
    if (ini.empty()) return false;
    int mode = (int)GetPrivateProfileIntW(L"Configuration", L"DarkMode", 0, ini.c_str());
    return mode == 2 || (mode == 1 && WindowsAppsDark());
}

Palette Make(ThemeMode mode, bool tcDark)
{
    Palette p{};
    if (mode == ThemeMode::Markdown) {
        // Catppuccin Mocha, wie im MarkdownTC-Reader
        const COLORREF base = RGB(0x1e, 0x1e, 0x2e), mantle = RGB(0x18, 0x18, 0x25),
                       crust = RGB(0x11, 0x11, 0x1b), surface0 = RGB(0x31, 0x32, 0x44),
                       surface1 = RGB(0x45, 0x47, 0x5a), overlay0 = RGB(0x6c, 0x70, 0x86),
                       text = RGB(0xcd, 0xd6, 0xf4), subtext0 = RGB(0xa6, 0xad, 0xc8),
                       mauve = RGB(0xcb, 0xa6, 0xf7), blue = RGB(0x89, 0xb4, 0xfa),
                       peach = RGB(0xfa, 0xb3, 0x87), red = RGB(0xf3, 0x8b, 0xa8),
                       green = RGB(0xa6, 0xe3, 0xa1);
        p.dark = true;
        p.bg = base;
        p.bgAlt = Blend(base, surface0, 0.30);
        p.fg = text;
        p.grid = surface0;
        p.headerBg = surface0;
        p.headerFg = mauve;
        p.gutterBg = mantle;
        p.gutterFg = overlay0;
        p.cursorBg = blue;
        p.cursorFg = base;
        p.cursorInactiveBg = surface1;
        p.rowHighlight = Blend(base, surface0, 0.75);
        p.selBg = Blend(base, mauve, 0.28);
        p.selFg = text;
        p.selMarker = mauve;
        p.accent = mauve;
        p.number = peach;
        p.statusBg = crust;
        p.statusFg = subtext0;
        p.statusWarn = red;
        p.statusOk = green;
        p.editBg = surface0;
        p.editFg = text;
        p.editBorder = blue;
        return p;
    }

    // Total Commander: Farben des Hauptfensters
    const std::wstring tcIni = Settings::TcIniPath();
    const wchar_t* section = tcDark ? L"ColorsDark" : L"Colors";
    const std::wstring ini = tcIni.empty() ? std::wstring() : Settings::TcSectionFile(tcIni, section);
    auto color = [&](const wchar_t* key, COLORREF def) { return IniColor(ini, section, key).value_or(def); };

    p.dark = tcDark;
    if (tcDark) {
        // Vorgaben = TC-Darkmode ohne eigene Farben (am Hauptfenster gemessen)
        p.bg = color(L"BackColor", RGB(0x20, 0x20, 0x20));
        p.fg = color(L"ForeColor", RGB(0xDC, 0xDC, 0xDC));
        p.grid = color(L"DarkBorder", RGB(0x40, 0x40, 0x40));
        p.cursorBg = color(L"CursorColor", RGB(0x1F, 0x5C, 0x99));
        p.cursorFg = color(L"CursorText", RGB(0xFF, 0xFF, 0xFF));
        p.selFg = color(L"MarkColor", RGB(0xFF, 0x6B, 0x6B));
        p.headerBg = Blend(p.bg, p.fg, 0.10);
    } else {
        p.bg = color(L"BackColor", GetSysColor(COLOR_WINDOW));
        p.fg = color(L"ForeColor", GetSysColor(COLOR_WINDOWTEXT));
        p.grid = Blend(p.bg, p.fg, 0.16);
        p.cursorBg = color(L"CursorColor", GetSysColor(COLOR_HIGHLIGHT));
        p.cursorFg = color(L"CursorText", GetSysColor(COLOR_HIGHLIGHTTEXT));
        p.selFg = color(L"MarkColor", RGB(0xFF, 0x00, 0x00));
        p.headerBg = Blend(p.bg, p.fg, 0.07);
    }
    auto bg2 = IniColor(ini, section, L"BackColor2");
    p.bgAlt = bg2 ? *bg2 : Blend(p.bg, p.fg, 0.035);
    p.headerFg = p.fg;
    p.gutterBg = p.headerBg;
    p.gutterFg = Blend(p.fg, p.bg, 0.40);
    p.cursorInactiveBg = Blend(p.bg, p.cursorBg, 0.45);
    p.rowHighlight = Blend(p.bg, p.cursorBg, 0.14);
    p.selBg = Blend(p.bg, p.cursorBg, 0.30);
    p.selMarker = p.selFg;
    p.accent = p.cursorBg;
    p.number = p.fg;
    p.statusBg = Blend(p.bg, p.fg, tcDark ? 0.06 : 0.10);
    p.statusFg = p.fg;
    p.statusWarn = tcDark ? RGB(0xFF, 0x8A, 0x65) : RGB(0xC0, 0x30, 0x00);
    p.statusOk = tcDark ? RGB(0x7C, 0xD6, 0x7C) : RGB(0x1B, 0x7F, 0x2A);
    p.editBg = tcDark ? Blend(p.bg, p.fg, 0.08) : p.bg;
    p.editFg = p.fg;
    p.editBorder = p.cursorBg;
    return p;
}

} // namespace Theme

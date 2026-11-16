/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 *
 * Plugin-Einstellungen im Abschnitt [CsvTC] der lsplugin.ini von TC.
 */
#pragma once
#include <windows.h>
#include <string>

namespace Settings {

void         SetPluginIni(const std::wstring& path);
std::wstring PluginIni();
void         SetInterfaceVersion(DWORD hi, DWORD lo);
// TC meldet den Darkmode ab Plugin-Interface 2.12 zuverlässig über ShowFlags.
bool         DarkFlagsReliable();

std::wstring GetString(const wchar_t* key, const wchar_t* def);
int          GetInt(const wchar_t* key, int def);
void         SetString(const wchar_t* key, const std::wstring& value);
void         SetInt(const wchar_t* key, int value);

// wincmd.ini von TC (COMMANDER_INI, sonst neben der lsplugin.ini); leer, wenn
// nicht gefunden.
std::wstring TcIniPath();
// Datei, in der ein Abschnitt der wincmd.ini wirklich steht (RedirectSection=).
std::wstring TcSectionFile(const std::wstring& ini, const wchar_t* section);
// Oberflächensprache aus [CsvTC] Language= und der Sprache von TC setzen.
void         ApplyLanguage();

// Diagnose-Log nach %TEMP%\csvtc.log, nur wenn [CsvTC] Log=1 gesetzt ist.
void         Log(const wchar_t* fmt, ...);

} // namespace Settings

/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 *
 * Oberflächensprache: Deutsch oder Englisch, je nach Sprache von Total
 * Commander. Rückfall ist Englisch.
 */
#pragma once
#include <initializer_list>
#include <string>
#include <string_view>

namespace Lang {

enum class Id { English, German };

// Gilt für alle Fenster des Plugins. Englisch, bis Set() aufgerufen wird.
void Set(Id id);
Id   Current();
inline bool IsGerman() { return Current() == Id::German; }

// Text in der aktuellen Sprache. Beide Fassungen stehen an der Aufrufstelle,
// so kann keine Übersetzung fehlen.
inline const wchar_t* Tr(const wchar_t* en, const wchar_t* de) { return IsGerman() ? de : en; }

// Ersetzt %1 … %9 durch args, %% durch %. Eingesetzter Text wird nicht erneut
// durchsucht. Ganze Sätze übersetzen, nicht Bruchstücke: die Wortstellung ist
// im Deutschen und Englischen verschieden.
std::wstring Fmt(std::wstring_view pattern, std::initializer_list<std::wstring_view> args);

// setting:       [CsvTC] Language= aus lsplugin.ini: auto (Standard), de, en
// tcLanguageIni: [Configuration] LanguageIni= aus wincmd.ini, z. B.
//                "WCMD_DEU.LNG" oder ein Pfad dorthin. Leer = TC läuft englisch.
// Deutsch nur bei der deutschen Sprachdatei von TC, sonst Englisch.
Id Resolve(std::wstring_view setting, std::wstring_view tcLanguageIni);

} // namespace Lang

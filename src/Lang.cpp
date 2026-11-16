/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 */
#include "Lang.h"
#include <cwctype>

namespace {

Lang::Id g_lang = Lang::Id::English;

std::wstring_view Trim(std::wstring_view s)
{
    while (!s.empty() && (iswspace(s.front()) || s.front() == L'"')) s.remove_prefix(1);
    while (!s.empty() && (iswspace(s.back()) || s.back() == L'"')) s.remove_suffix(1);
    return s;
}

bool StartsWithI(std::wstring_view s, std::wstring_view prefix)
{
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (towlower(s[i]) != towlower(prefix[i])) return false;
    return true;
}

bool EqualsI(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size() && StartsWithI(a, b);
}

} // namespace

namespace Lang {

void Set(Id id) { g_lang = id; }
Id   Current() { return g_lang; }

std::wstring Fmt(std::wstring_view pattern, std::initializer_list<std::wstring_view> args)
{
    std::wstring out;
    out.reserve(pattern.size() + 32);
    for (size_t i = 0; i < pattern.size(); ++i) {
        const wchar_t ch = pattern[i];
        if (ch == L'%' && i + 1 < pattern.size()) {
            const wchar_t next = pattern[i + 1];
            if (next == L'%') {
                out += L'%';
                ++i;
                continue;
            }
            if (next >= L'1' && next <= L'9') {
                const size_t idx = (size_t)(next - L'1');
                if (idx < args.size()) out += *(args.begin() + idx);
                ++i;
                continue;
            }
        }
        out += ch;
    }
    return out;
}

Id Resolve(std::wstring_view setting, std::wstring_view tcLanguageIni)
{
    setting = Trim(setting);
    if (EqualsI(setting, L"de") || EqualsI(setting, L"deu") || EqualsI(setting, L"german") ||
        EqualsI(setting, L"deutsch"))
        return Id::German;
    if (EqualsI(setting, L"en") || EqualsI(setting, L"eng") || EqualsI(setting, L"english"))
        return Id::English;

    // auto: Sprachdatei von TC. Die deutsche heißt WCMD_DEU.LNG.
    std::wstring_view lng = Trim(tcLanguageIni);
    const size_t slash = lng.find_last_of(L"\\/");
    if (slash != std::wstring_view::npos) lng.remove_prefix(slash + 1);
    return StartsWithI(lng, L"wcmd_deu") ? Id::German : Id::English;
}

} // namespace Lang

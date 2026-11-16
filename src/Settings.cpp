/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 */
#include "Settings.h"
#include "Lang.h"
#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace {
constexpr wchar_t kSection[] = L"CsvTC";
std::wstring g_ini;
DWORD g_verHi = 0, g_verLo = 0;
int   g_logEnabled = -1;   // -1 = noch nicht gelesen

std::wstring Expand(const std::wstring& s)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return (n > 0 && n <= sizeof(buf) / sizeof(buf[0])) ? std::wstring(buf) : s;
}

bool FileExists(const std::wstring& p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring IniString(const std::wstring& ini, const wchar_t* section, const wchar_t* key)
{
    wchar_t buf[512];
    GetPrivateProfileStringW(section, key, L"", buf, 512, ini.c_str());
    return buf;
}
} // namespace

namespace Settings {

void SetPluginIni(const std::wstring& path) { g_ini = path; }
std::wstring PluginIni() { return g_ini; }

void SetInterfaceVersion(DWORD hi, DWORD lo)
{
    g_verHi = hi;
    g_verLo = lo;
}

bool DarkFlagsReliable()
{
    return g_verHi > 2 || (g_verHi == 2 && g_verLo >= 12);
}

std::wstring GetString(const wchar_t* key, const wchar_t* def)
{
    if (g_ini.empty()) return def;
    wchar_t buf[512];
    GetPrivateProfileStringW(kSection, key, def, buf, 512, g_ini.c_str());
    return buf;
}

int GetInt(const wchar_t* key, int def)
{
    if (g_ini.empty()) return def;
    return (int)GetPrivateProfileIntW(kSection, key, def, g_ini.c_str());
}

void SetString(const wchar_t* key, const std::wstring& value)
{
    if (g_ini.empty()) return;
    WritePrivateProfileStringW(kSection, key, value.c_str(), g_ini.c_str());
}

void SetInt(const wchar_t* key, int value)
{
    SetString(key, std::to_wstring(value));
}

// TC setzt COMMANDER_INI in seiner eigenen Prozessumgebung.
std::wstring TcIniPath()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"COMMANDER_INI", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) {
        std::wstring p = Expand(buf);
        if (FileExists(p)) return p;
    }
    // Rückfall: wincmd.ini liegt meist neben der lsplugin.ini.
    size_t slash = g_ini.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        std::wstring p = g_ini.substr(0, slash + 1) + L"wincmd.ini";
        if (FileExists(p)) return p;
    }
    return {};
}

std::wstring TcSectionFile(const std::wstring& ini, const wchar_t* section)
{
    std::wstring redirect = IniString(ini, section, L"RedirectSection");
    if (redirect.empty()) return ini;
    std::wstring p = Expand(redirect);
    if (p.size() > 1 && p[1] != L':' && p[0] != L'\\') {
        size_t slash = ini.find_last_of(L"\\/");
        if (slash != std::wstring::npos) p = ini.substr(0, slash + 1) + p;
    }
    return FileExists(p) ? p : ini;
}

void ApplyLanguage()
{
    std::wstring lng;
    const std::wstring tcIni = TcIniPath();
    if (!tcIni.empty())
        lng = IniString(TcSectionFile(tcIni, L"Configuration"), L"Configuration", L"LanguageIni");
    const std::wstring setting = GetString(L"Language", L"auto");
    Lang::Set(Lang::Resolve(setting, lng));
    Log(L"language %ls (Language=%ls, LanguageIni=%ls)", Lang::IsGerman() ? L"de" : L"en", setting.c_str(),
        lng.c_str());
}

void Log(const wchar_t* fmt, ...)
{
    if (g_logEnabled < 0) g_logEnabled = GetInt(L"Log", 0) ? 1 : 0;
    if (!g_logEnabled) return;
    wchar_t dir[MAX_PATH + 1] = {};
    if (!GetTempPathW(MAX_PATH + 1, dir)) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (std::wstring(dir) + L"csvtc.log").c_str(), L"a, ccs=UTF-8") || !f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fputwc(L'\n', f);
    fclose(f);
}

} // namespace Settings

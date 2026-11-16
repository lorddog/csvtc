/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 */
#include <windows.h>
#include <memory>
#include <string>
#include <unordered_map>
#include "listplug.h"
#include "GridView.h"
#include "Settings.h"

HINSTANCE g_hInst = nullptr;

// Alle Lister-Aufrufe kommen aus dem UI-Thread von Total Commander.
static std::unordered_map<HWND, std::unique_ptr<GridView>> g_views;

static GridView* GetView(HWND hwnd)
{
    auto it = g_views.find(hwnd);
    return it != g_views.end() ? it->second.get() : nullptr;
}

static std::wstring AnsiToWide(const char* s)
{
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    w.resize((size_t)n - 1);
    return w;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hMod;
        DisableThreadLibraryCalls(hMod);
        GridView::RegisterWindowClass(hMod);
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        // Beim Entladen (FreeLibrary) die Fensterklasse freigeben, sonst zeigt
        // sie nach erneutem Laden auf die alte WndProc-Adresse.
        UnregisterClassW(L"CsvTCGridView", hMod);
    }
    return TRUE;
}

// ── WLX-Exporte ──────────────────────────────────────────────────────────────

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
{
    std::wstring w = AnsiToWide(FileToLoad);
    return w.empty() ? nullptr : ListLoadW(ParentWin, w.data(), ShowFlags);
}

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags)
{
    if (!ParentWin || !FileToLoad || !FileToLoad[0]) return nullptr;
    Settings::Log(L"ListLoadW flags=0x%X file=%ls", ShowFlags, FileToLoad);
    // Bei jedem Öffnen neu: die Sprache von TC kann sich geändert haben.
    Settings::ApplyLanguage();
    auto view = GridView::Create(ParentWin, FileToLoad, ShowFlags);
    if (!view) return nullptr;
    HWND hwnd = view->Hwnd();
    g_views[hwnd] = std::move(view);
    return hwnd;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND ListWin, char* FileToLoad, int ShowFlags)
{
    std::wstring w = AnsiToWide(FileToLoad);
    return w.empty() ? LISTPLUGIN_ERROR : ListLoadNextW(ParentWin, ListWin, w.data(), ShowFlags);
}

int __stdcall ListLoadNextW(HWND /*ParentWin*/, HWND ListWin, WCHAR* FileToLoad, int ShowFlags)
{
    if (!FileToLoad || !FileToLoad[0]) return LISTPLUGIN_ERROR;
    GridView* view = GetView(ListWin);
    if (!view) return LISTPLUGIN_ERROR;
    Settings::Log(L"ListLoadNextW flags=0x%X file=%ls", ShowFlags, FileToLoad);
    Settings::ApplyLanguage();
    return view->LoadNext(FileToLoad, ShowFlags) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

void __stdcall ListCloseWindow(HWND ListWin)
{
    auto it = g_views.find(ListWin);
    if (it == g_views.end()) {
        DestroyWindow(ListWin);
        return;
    }
    std::unique_ptr<GridView> view = std::move(it->second);
    g_views.erase(it);
    view->PromptSaveBeforeLeave();
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen)
{
    strncpy_s(DetectString, (size_t)maxlen, "EXT=\"CSV\" | EXT=\"TSV\" | EXT=\"TAB\"", _TRUNCATE);
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
    std::wstring w = AnsiToWide(SearchString);
    return w.empty() ? LISTPLUGIN_ERROR : ListSearchTextW(ListWin, w.data(), SearchParameter);
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter)
{
    if (!SearchString || !SearchString[0]) return LISTPLUGIN_ERROR;
    GridView* view = GetView(ListWin);
    if (!view) return LISTPLUGIN_ERROR;
    return view->Search(SearchString, SearchParameter) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    GridView* view = GetView(ListWin);
    if (!view) return LISTPLUGIN_ERROR;

    switch (Command) {
    case lc_newparams:
        // Kommt u. a. beim Umschalten zwischen Dark- und Normalmodus.
    {
        const bool stay = view->NewParams(Parameter);
        Settings::Log(L"lc_newparams 0x%X -> %ls", Parameter, stay ? L"OK" : L"ERROR");
        return stay ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
    }
    case lc_copy:
        view->CopyToClipboard();
        return LISTPLUGIN_OK;
    case lc_selectall:
        view->SelectAll();
        return LISTPLUGIN_OK;
    case lc_setpercent:
        view->SetPercent(Parameter);
        return LISTPLUGIN_OK;
    default:
        return LISTPLUGIN_ERROR;
    }
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps)
{
    if (!dps) return;
    Settings::SetPluginIni(AnsiToWide(dps->DefaultIniName));
    Settings::SetInterfaceVersion(dps->PluginInterfaceVersionHi, dps->PluginInterfaceVersionLow);
    Settings::Log(L"CsvTC loaded, interface %lu.%lu, ini %ls", dps->PluginInterfaceVersionHi,
                  dps->PluginInterfaceVersionLow, Settings::PluginIni().c_str());
}

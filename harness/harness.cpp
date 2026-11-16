/**
 * CsvTC — Test-Host: lädt das Lister-Plugin wie Total Commander, ohne TC.
 *
 *   harness.exe <plugin.wlx64> <datei> [--ini <pfad>] [--log <datei>] [--size WxH]
 *               [--flags <ShowFlags>] [--at <ms> <aktion> [arg]] ...
 *
 * Aktionen:  shot <png>       Screenshot des Fensters
 *            shotdlg <png>    Screenshot der offenen MessageBox
 *            key <taste>      Taste als Fensternachricht ans Plugin (bzw. an den
 *                             offenen Inline-Editor), z. B. 0x28, S+0x28 (Shift),
 *                             C+0x53 (Strg), A+0x46 (Alt), auch kombiniert S+C+0x23
 *            text <text>      Text als WM_CHAR tippen
 *            answer <knopf>   offene MessageBox beantworten: yes|no|cancel|ok
 *            menu             Kontextmenü öffnen (wie die Menütaste)
 *            endmenu          offenes Menü schließen
 *            next <datei>     ListLoadNextW
 *            search <text>    ListSearchTextW (findfirst)
 *            again <text>     ListSearchTextW (weitersuchen)
 *            percent <n>      ListSendCommand(lc_setpercent)
 *            params <flags>   ListSendCommand(lc_newparams)
 *            copy             ListSendCommand(lc_copy), Zwischenablage ins Log
 *            selectall        ListSendCommand(lc_selectall)
 *            click <x,y>      Mausklick (Koordinaten wie im Screenshot)
 *            dblclick <x,y>   Doppelklick
 *            close            ListCloseWindow
 *            exit             Programm beenden (ruft vorher ListCloseWindow)
 *
 * Wie Total Commander schließt der Host bei einem weitergeleiteten Esc den
 * "Lister" (ListCloseWindow + Ende). Alle Nachrichten, die das Plugin an das
 * Lister-Fenster schickt, landen im Log (--log).
 */
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include "listplug.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using PListLoadW            = HWND(__stdcall*)(HWND, WCHAR*, int);
using PListLoadNextW        = int(__stdcall*)(HWND, HWND, WCHAR*, int);
using PListCloseWindow      = void(__stdcall*)(HWND);
using PListSearchTextW      = int(__stdcall*)(HWND, WCHAR*, int);
using PListSendCommand      = int(__stdcall*)(HWND, int, int);
using PListSetDefaultParams = void(__stdcall*)(ListDefaultParamStruct*);

struct Action { UINT at; std::wstring name, arg; };

PListLoadW        pLoad;
PListLoadNextW    pNext;
PListCloseWindow  pClose;
PListSearchTextW  pSearch;
PListSendCommand  pCommand;

HWND  g_main, g_plugin;
FILE* g_log;
int   g_flags;
std::vector<Action> g_actions;

void Log(const wchar_t* fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(g_log, fmt, ap);
    va_end(ap);
    fputwc(L'\n', g_log);
    fflush(g_log);
}

bool SaveScreenshot(HWND hwnd, const std::wstring& path)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);

    CLSID png = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
    bool ok = false;
    {
        Gdiplus::Bitmap b(bmp, nullptr);
        ok = b.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

// Tasten und Mausklicks gehen als Fensternachrichten direkt an das Plugin —
// SendInput würde sie an das gerade aktive Fenster (z. B. ein Terminal)
// schicken, wenn der Host nicht im Vordergrund ist.

HWND KeyTarget()
{
    // Offener Inline-Editor hat in TC den Fokus
    if (HWND edit = FindWindowExW(g_plugin, nullptr, L"EDIT", nullptr)) return edit;
    return g_plugin;
}

void SetModifiers(bool shift, bool ctrl, bool alt)
{
    BYTE ks[256];
    GetKeyboardState(ks);
    ks[VK_SHIFT] = ks[VK_LSHIFT] = shift ? 0x80 : 0;
    ks[VK_CONTROL] = ks[VK_LCONTROL] = ctrl ? 0x80 : 0;
    ks[VK_MENU] = ks[VK_LMENU] = alt ? 0x80 : 0;
    SetKeyboardState(ks);
}

void PressKey(const std::wstring& spec)
{
    bool shift = false, ctrl = false, alt = false;
    size_t pos = 0;
    while (pos + 1 < spec.size() && spec[pos + 1] == L'+') {
        wchar_t m = spec[pos];
        if (m == L'S') shift = true;
        else if (m == L'C') ctrl = true;
        else if (m == L'A') alt = true;
        pos += 2;
    }
    const UINT vk = (UINT)wcstoul(spec.c_str() + pos, nullptr, 0);
    HWND target = KeyTarget();
    if (!target) return;
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM lp = 1 | ((LPARAM)scan << 16);
    if (vk >= VK_PRIOR && vk <= VK_DOWN) lp |= 1 << 24;
    if (alt) lp |= 1 << 29;
    SetModifiers(shift, ctrl, alt);
    SendMessageW(target, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, lp);
    SendMessageW(target, alt ? WM_SYSKEYUP : WM_KEYUP, vk, lp | 0xC0000000);
    SetModifiers(false, false, false);
}

void TypeText(const std::wstring& text)
{
    HWND target = KeyTarget();
    for (wchar_t ch : text) SendMessageW(target, WM_CHAR, ch, 1);
}

void ClickAt(const std::wstring& arg, bool dbl)
{
    // Koordinaten relativ zum Fensterrechteck (wie im Screenshot)
    int x = 0, y = 0;
    swscanf_s(arg.c_str(), L"%d,%d", &x, &y);
    RECT rc;
    GetWindowRect(g_main, &rc);
    POINT pt = { rc.left + x, rc.top + y };
    ScreenToClient(g_plugin, &pt);
    const LPARAM lp = MAKELPARAM(pt.x, pt.y);
    SendMessageW(g_plugin, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    SendMessageW(g_plugin, WM_LBUTTONUP, 0, lp);
    if (dbl) {
        SendMessageW(g_plugin, WM_LBUTTONDBLCLK, MK_LBUTTON, lp);
        SendMessageW(g_plugin, WM_LBUTTONUP, 0, lp);
    }
}

// Offene MessageBox des Plugins (sichtbarer Dialog dieses Threads)
HWND FindDialog()
{
    HWND dlg = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM p) -> BOOL {
        wchar_t cls[16] = {};
        GetClassNameW(h, cls, 16);
        if (wcscmp(cls, L"#32770") == 0 && IsWindowVisible(h)) {
            *reinterpret_cast<HWND*>(p) = h;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&dlg);
    return dlg;
}

// Offene MessageBox finden, Text loggen und Knopf drücken.
void AnswerDialog(const std::wstring& answer)
{
    HWND dlg = FindDialog();
    if (!dlg) { Log(L"answer: kein Dialog offen"); return; }
    wchar_t text[1024] = {};
    GetDlgItemTextW(dlg, 0xFFFF, text, 1024);
    for (wchar_t* p = text; *p; ++p) if (*p == L'\n' || *p == L'\r') *p = L' ';
    Log(L"dialog: [%ls]", text);
    int id = answer == L"yes" ? IDYES : answer == L"no" ? IDNO : answer == L"cancel" ? IDCANCEL : IDOK;
    PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)GetDlgItem(dlg, id));
}

std::wstring ClipboardText()
{
    std::wstring s;
    if (!OpenClipboard(g_main)) return L"<clipboard busy>";
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            s = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return s;
}

void ClosePlugin()
{
    if (!g_plugin) return;
    HWND p = g_plugin;
    g_plugin = nullptr;
    pClose(p);
    Log(L"ListCloseWindow done");
}

void RunAction(const Action& a)
{
    Log(L"action %ls %ls", a.name.c_str(), a.arg.c_str());
    std::wstring arg = a.arg;
    if (a.name == L"shot") {
        Log(L"shot %ls: %ls", arg.c_str(), SaveScreenshot(g_main, arg) ? L"ok" : L"FAILED");
    } else if (a.name == L"shotdlg") {
        // MessageBoxen zentriert Windows auf dem Monitor, nicht über dem Fenster.
        HWND dlg = FindDialog();
        Log(L"shotdlg %ls: %ls", arg.c_str(), dlg && SaveScreenshot(dlg, arg) ? L"ok" : L"FAILED");
    } else if (a.name == L"key" && g_plugin) {
        PressKey(arg);
    } else if (a.name == L"text" && g_plugin) {
        TypeText(arg);
    } else if (a.name == L"next" && g_plugin) {
        Log(L"ListLoadNextW -> %d", pNext(g_main, g_plugin, arg.data(), g_flags));
    } else if (a.name == L"search" && g_plugin) {
        Log(L"ListSearchTextW -> %d", pSearch(g_plugin, arg.data(), lcs_findfirst));
    } else if (a.name == L"again" && g_plugin) {
        Log(L"ListSearchTextW -> %d", pSearch(g_plugin, arg.data(), 0));
    } else if (a.name == L"percent" && g_plugin) {
        pCommand(g_plugin, lc_setpercent, _wtoi(arg.c_str()));
    } else if (a.name == L"params" && g_plugin) {
        g_flags = (int)wcstol(arg.c_str(), nullptr, 0);
        Log(L"lc_newparams -> %d", pCommand(g_plugin, lc_newparams, g_flags));
    } else if (a.name == L"copy" && g_plugin) {
        pCommand(g_plugin, lc_copy, 0);
        Log(L"clipboard: [%ls]", ClipboardText().c_str());
    } else if (a.name == L"selectall" && g_plugin) {
        pCommand(g_plugin, lc_selectall, 0);
    } else if ((a.name == L"click" || a.name == L"dblclick") && g_plugin) {
        ClickAt(arg, a.name == L"dblclick");
    } else if (a.name == L"menu" && g_plugin) {
        PostMessageW(g_plugin, WM_CONTEXTMENU, (WPARAM)g_plugin, MAKELPARAM(-1, -1));
    } else if (a.name == L"endmenu") {
        Log(L"endmenu -> %d", EndMenu());
    } else if (a.name == L"answer") {
        AnswerDialog(arg);
    } else if (a.name == L"close") {
        ClosePlugin();
    } else if (a.name == L"exit") {
        ClosePlugin();
        PostQuitMessage(0);
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_plugin) MoveWindow(g_plugin, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        Log(L"parent got %ls vk=0x%02X", msg == WM_KEYDOWN ? L"WM_KEYDOWN" : L"WM_SYSKEYDOWN", (unsigned)wp);
        if (wp == VK_ESCAPE) {
            // wie TC: Lister schließen
            Log(L"Esc -> Lister schliesst");
            ClosePlugin();
            PostQuitMessage(0);
        }
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == itm_percent) { Log(L"percent %u", (unsigned)LOWORD(wp)); return 0; }
        if (HIWORD(wp) == itm_focus) { Log(L"itm_focus"); return 0; }
        break;
    case WM_TIMER:
        KillTimer(hwnd, wp);
        if (wp >= 1 && wp <= g_actions.size()) RunAction(g_actions[wp - 1]);
        return 0;
    case WM_SETFOCUS:
        if (g_plugin) SetFocus(g_plugin);
        return 0;
    case WM_CLOSE:
        ClosePlugin();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ClosePlugin();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 3) {
        MessageBoxW(nullptr, L"harness.exe <plugin> <datei> [--ini p] [--log f] [--flags n] [--at ms aktion arg]",
                    L"harness", MB_OK);
        return 2;
    }
    std::wstring pluginPath = argv[1], file = argv[2], ini, logPath;
    int width = 1100, height = 700;
    for (int i = 3; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--ini" && i + 1 < argc) ini = argv[++i];
        else if (a == L"--log" && i + 1 < argc) logPath = argv[++i];
        else if (a == L"--size" && i + 1 < argc) swscanf_s(argv[++i], L"%dx%d", &width, &height);
        else if (a == L"--flags" && i + 1 < argc) g_flags = (int)wcstol(argv[++i], nullptr, 0);
        else if (a == L"--at" && i + 2 < argc) {
            Action act{ (UINT)_wtoi(argv[i + 1]), argv[i + 2], L"" };
            i += 2;
            bool needsArg = act.name != L"close" && act.name != L"exit" && act.name != L"copy" &&
                            act.name != L"selectall" && act.name != L"menu" && act.name != L"endmenu";
            // Nie die nächste Aktion als Argument verschlucken.
            if (needsArg && i + 1 < argc && wcscmp(argv[i + 1], L"--at") != 0) act.arg = argv[++i];
            g_actions.push_back(act);
        }
    }
    if (!logPath.empty()) _wfopen_s(&g_log, logPath.c_str(), L"w, ccs=UTF-8");

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gsi, nullptr);
    OleInitialize(nullptr);   // wie Total Commander (STA)

    HMODULE mod = LoadLibraryW(pluginPath.c_str());
    if (!mod) { Log(L"LoadLibrary failed: %lu", GetLastError()); return 3; }
    pLoad    = (PListLoadW)GetProcAddress(mod, "ListLoadW");
    pNext    = (PListLoadNextW)GetProcAddress(mod, "ListLoadNextW");
    pClose   = (PListCloseWindow)GetProcAddress(mod, "ListCloseWindow");
    pSearch  = (PListSearchTextW)GetProcAddress(mod, "ListSearchTextW");
    pCommand = (PListSendCommand)GetProcAddress(mod, "ListSendCommand");
    auto pParams = (PListSetDefaultParams)GetProcAddress(mod, "ListSetDefaultParams");
    if (!pLoad || !pNext || !pClose || !pSearch || !pCommand || !pParams) {
        Log(L"missing exports");
        return 4;
    }

    ListDefaultParamStruct dps{};
    dps.size = sizeof(dps);
    dps.PluginInterfaceVersionHi = 2;
    dps.PluginInterfaceVersionLow = 12;
    WideCharToMultiByte(CP_ACP, 0, ini.c_str(), -1, dps.DefaultIniName, MAX_PATH, nullptr, nullptr);
    pParams(&dps);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CsvTcHarness";
    RegisterClassW(&wc);
    g_main = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"CsvTC Harness",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             40, 40, width, height, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_main, SW_SHOW);
    SetForegroundWindow(g_main);

    g_plugin = pLoad(g_main, file.data(), g_flags);
    Log(L"ListLoadW -> %p", (void*)g_plugin);
    if (g_plugin) SetFocus(g_plugin);

    for (size_t i = 0; i < g_actions.size(); ++i) SetTimer(g_main, i + 1, g_actions[i].at, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    Log(L"exit");
    if (g_log) fclose(g_log);
    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}

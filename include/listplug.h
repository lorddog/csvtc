/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Lister plugin interface (subset of the official WLX SDK, version 2.12).
 * Werte geprüft gegen HISTORY.TXT von TC (lcp_darkmode=128,
 * lcp_darkmodenative=256) und die WLX-SDK-Doku.
 */
#pragma once
#include <windows.h>

#define LISTPLUGIN_OK    0
#define LISTPLUGIN_ERROR 1

// ShowFlags / lc_newparams parameter
#define lcp_wraptext         1
#define lcp_fittowindow      2
#define lcp_ansi             4
#define lcp_ascii            8
#define lcp_variable        12
#define lcp_forceshow       16
#define lcp_fitlargeronly   32
#define lcp_center          64
#define lcp_darkmode       128
#define lcp_darkmodenative 256

// ListSendCommand
#define lc_copy        1
#define lc_newparams   2
#define lc_selectall   3
#define lc_setpercent  4

// ListSearchText
#define lcs_findfirst  1
#define lcs_matchcase  2
#define lcs_wholewords 4
#define lcs_backwards  8

// WM_COMMAND an das Lister-Fenster: HIWORD(wParam) = itm_*, lParam = Plugin-Fenster
#define itm_percent   0xFFFE
#define itm_fontstyle 0xFFFD
#define itm_wrap      0xFFFC
#define itm_fit       0xFFFB
#define itm_next      0xFFFA
#define itm_center    0xFFF9
#define itm_focus     0xFFF8

#pragma pack(push, 4)
struct ListDefaultParamStruct {
    int   size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char  DefaultIniName[MAX_PATH];
};
#pragma pack(pop)

// Exporte stehen in src/csvtc.def (unverzierte Namen auf x86 und x64).
#ifdef __cplusplus
extern "C" {
#endif
HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags);
HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags);
int  __stdcall ListLoadNext(HWND ParentWin, HWND ListWin, char* FileToLoad, int ShowFlags);
int  __stdcall ListLoadNextW(HWND ParentWin, HWND ListWin, WCHAR* FileToLoad, int ShowFlags);
void __stdcall ListCloseWindow(HWND ListWin);
void __stdcall ListGetDetectString(char* DetectString, int maxlen);
int  __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter);
int  __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter);
int  __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter);
void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps);
#ifdef __cplusplus
}
#endif

/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 */
#include "GridView.h"
#include "Lang.h"
#include "Settings.h"
#include "listplug.h"
#include <algorithm>
#include <cwchar>
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

extern HINSTANCE g_hInst;

using Lang::Fmt;
using Lang::Tr;

namespace {

constexpr UINT     WM_APP_ENDEDIT = WM_APP + 1;   // wParam = Edit-Generation, lParam = EditAction
constexpr UINT_PTR kTimerPending = 1;
constexpr UINT_PTR kTimerMessage = 2;
constexpr DWORD    kPendingMs = 1500;             // Zeitfenster für "d d" bzw. "Shift+D Shift+D"
constexpr int      kDefaultFontPt = 10;
constexpr int      kMinFontPt = 6;
constexpr int      kMaxFontPt = 28;
constexpr size_t   kEditMaxLines = 8;

enum EditAction : LPARAM { kEditCancel = 0, kEditCommit = 1, kEditCommitRight = 2, kEditCommitLeft = 3 };

enum MenuId : UINT {
    ID_EDIT = 100, ID_HEADER, ID_AUTOSIZE, ID_INSROW, ID_DELROWS, ID_DELCOL, ID_UNDO, ID_COPY, ID_SAVE, ID_HELP,
    ID_THEME_TC, ID_THEME_MD,
    ID_SEP_FIRST = 200,
};

bool KeyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

void Fill(HDC dc, const RECT& rc, COLORREF c)
{
    SetDCBrushColor(dc, c);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void Line(HDC dc, int x1, int y1, int x2, int y2)
{
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
}

// Zahlen werden rechtsbündig (und im Markdown-Schema farbig) dargestellt.
bool IsNumeric(std::wstring_view v)
{
    size_t a = 0, b = v.size();
    while (a < b && v[a] == L' ') ++a;
    while (b > a && v[b - 1] == L' ') --b;
    if (a == b || b - a > 40) return false;
    bool digit = false;
    for (size_t i = a; i < b; ++i) {
        const wchar_t ch = v[i];
        if (ch >= L'0' && ch <= L'9') { digit = true; continue; }
        if (ch == L'.' || ch == L',' || ch == L'+' || ch == L'-' || ch == L' ' || ch == L'\'') continue;
        if ((ch == L'e' || ch == L'E') && digit) continue;
        if ((ch == L'%' || ch == L'€' || ch == L'$') && (i == a || i == b - 1)) continue;
        return false;
    }
    return digit;
}

// Einzeilige Darstellung: Zeilenumbrüche als ↵, Tabs als Leerzeichen.
void DisplayText(std::wstring_view v, std::wstring& out, size_t maxLen = 400)
{
    out.clear();
    const size_t n = std::min(v.size(), maxLen);
    for (size_t i = 0; i < n; ++i) {
        const wchar_t ch = v[i];
        if (ch == L'\r') {
            out += L'↵';
            if (i + 1 < v.size() && v[i + 1] == L'\n') ++i;
        } else if (ch == L'\n') {
            out += L'↵';
        } else if (ch == L'\t') {
            out += L' ';
        } else {
            out += ch;
        }
    }
    if (v.size() > maxLen) out += L'…';
}

// Tausendertrennzeichen: 400.000 (deutsch) bzw. 400,000 (englisch)
std::wstring FormatCount(size_t n)
{
    const wchar_t sep = Lang::IsGerman() ? L'.' : L',';
    std::wstring s = std::to_wstring(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, 1, sep);
    return s;
}

std::wstring RowsText(size_t n)
{
    return n == 1 ? std::wstring(Tr(L"1 row", L"1 Zeile")) : Fmt(Tr(L"%1 rows", L"%1 Zeilen"), { FormatCount(n) });
}

std::wstring ColumnsText(size_t n)
{
    return n == 1 ? std::wstring(Tr(L"1 column", L"1 Spalte"))
                  : Fmt(Tr(L"%1 columns", L"%1 Spalten"), { FormatCount(n) });
}

// Dateiname in Anführungszeichen der aktuellen Sprache
std::wstring Quoted(const std::wstring& s)
{
    return Fmt(Tr(L"“%1”", L"„%1“"), { s });
}

std::wstring FileName(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

bool HasExtension(const std::wstring& path, const wchar_t* ext)
{
    size_t dot = path.find_last_of(L'.');
    return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, ext) == 0;
}

wchar_t PreferredSeparator(const std::wstring& path)
{
    if (HasExtension(path, L".tsv") || HasExtension(path, L".tab")) return L'\t';
    wchar_t buf[8] = {};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SLIST, buf, 8) > 0 && buf[0]) return buf[0];
    return L',';
}

bool StartsWithI(const std::wstring& s, const std::wstring& prefix)
{
    return !prefix.empty() && s.size() >= prefix.size() &&
           CompareStringOrdinal(s.c_str(), (int)prefix.size(), prefix.c_str(), (int)prefix.size(), TRUE) == CSTR_EQUAL;
}

// Dateien aus Archiven/FTP entpackt TC nach %TEMP%\_tc\ — Speichern dort
// ändert nicht das Original.
bool IsTcTempPath(const std::wstring& path)
{
    wchar_t tmp[MAX_PATH + 1] = {};
    if (!GetTempPathW(MAX_PATH + 1, tmp)) return false;
    std::wstring t = tmp;
    if (StartsWithI(path, t + L"_tc\\")) return true;
    wchar_t longTmp[MAX_PATH * 2] = {};
    if (GetLongPathNameW(tmp, longTmp, MAX_PATH * 2)) {
        std::wstring lt = longTmp;
        if (!lt.empty() && lt.back() != L'\\') lt += L'\\';
        if (StartsWithI(path, lt + L"_tc\\")) return true;
    }
    return false;
}

// Erste Zeile ist vermutlich eine Kopfzeile, wenn sie Text, aber keine Zahlen enthält.
bool GuessHeader(const CsvDocument& doc)
{
    if (doc.RowCount() < 2) return false;
    bool any = false;
    for (size_t c = 0; c < doc.RowSize(0); ++c) {
        std::wstring_view v = doc.Cell(0, c);
        if (v.empty()) continue;
        if (IsNumeric(v)) return false;
        any = true;
    }
    return any;
}

bool MatchText(std::wstring_view hay, const std::wstring& needle, bool matchCase, bool wholeWords)
{
    if (hay.size() < needle.size()) return false;
    std::wstring h(hay);
    if (!matchCase) CharLowerBuffW(h.data(), (DWORD)h.size());
    for (size_t pos = h.find(needle); pos != std::wstring::npos; pos = h.find(needle, pos + 1)) {
        if (!wholeWords) return true;
        const size_t end = pos + needle.size();
        const bool leftOk = pos == 0 || !IsCharAlphaNumericW(h[pos - 1]);
        const bool rightOk = end >= h.size() || !IsCharAlphaNumericW(h[end]);
        if (leftOk && rightOk) return true;
    }
    return false;
}

// Ohne Tabs: die Tabstopps einer MessageBox sind fest, längere Tastennamen
// liefen sonst in die Beschreibung hinein.
const wchar_t kHelpTextEn[] =
    L"Navigation\n"
    L"   Arrow keys, PgUp/PgDn, Home/End – move between cells\n"
    L"   Ctrl+Home / Ctrl+End – start / end of the table\n"
    L"   Shift+arrow keys, Shift+click – select rows\n"
    L"   Click on row number, Ctrl+A – select row / all rows\n"
    L"\n"
    L"Editing\n"
    L"   E, F2, double-click – edit field\n"
    L"      (Enter accepts, Esc discards, Tab moves to the next field,\n"
    L"       Shift+Enter adds a line break)\n"
    L"   i i – insert an empty row at the cursor\n"
    L"   d d – delete row(s)\n"
    L"   Shift+D Shift+D – delete column\n"
    L"   Ctrl+Z – undo\n"
    L"   Ctrl+S – save\n"
    L"   Ctrl+C – copy field or selected rows\n"
    L"\n"
    L"View\n"
    L"   S / Shift+S – change separator (, ; Tab |)\n"
    L"   H – first row as header on/off\n"
    L"   T – color scheme: Total Commander ↔ Markdown reader\n"
    L"   Ctrl+mouse wheel, Ctrl +/−/0 – font size\n"
    L"   Drag / double-click header border – column width\n"
    L"\n"
    L"Esc closes (asks first if there are changes).\n"
    L"N/P, 1–8, F3, F7 go to Total Commander.";

const wchar_t kHelpTextDe[] =
    L"Navigation\n"
    L"   Pfeiltasten, Bild↑/↓, Pos1/Ende – Zelle wechseln\n"
    L"   Strg+Pos1 / Strg+Ende – Anfang / Ende der Tabelle\n"
    L"   Shift+Pfeiltasten, Shift+Klick – Zeilen markieren\n"
    L"   Klick auf Zeilennummer, Strg+A – Zeile / alle markieren\n"
    L"\n"
    L"Bearbeiten\n"
    L"   E, F2, Doppelklick – Feld bearbeiten\n"
    L"      (Enter übernimmt, Esc verwirft, Tab nächstes Feld,\n"
    L"       Shift+Enter Zeilenumbruch im Feld)\n"
    L"   i i – leere Zeile an der Cursorposition einfügen\n"
    L"   d d – Zeile(n) löschen\n"
    L"   Shift+D Shift+D – Spalte löschen\n"
    L"   Strg+Z – Rückgängig\n"
    L"   Strg+S – Speichern\n"
    L"   Strg+C – Feld bzw. markierte Zeilen kopieren\n"
    L"\n"
    L"Ansicht\n"
    L"   S / Shift+S – Trenner wechseln (, ; Tab |)\n"
    L"   H – erste Zeile als Kopfzeile an/aus\n"
    L"   T – Farbschema: Total Commander ↔ Markdown-Reader\n"
    L"   Strg+Mausrad, Strg +/−/0 – Schriftgröße\n"
    L"   Kopfzeilen-Rand ziehen / doppelklicken – Spaltenbreite\n"
    L"\n"
    L"Esc schließt (fragt bei Änderungen nach).\n"
    L"N/P, 1–8, F3, F7 gehen an Total Commander.";

} // namespace

// ── Klasse / Lebenszyklus ────────────────────────────────────────────────────

bool GridView::RegisterWindowClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    return RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

GridView::GridView(HWND parent) : m_parent(parent)
{
    m_themeMode = _wcsicmp(Settings::GetString(L"Theme", L"tc").c_str(), L"markdown") == 0
                      ? ThemeMode::Markdown : ThemeMode::TotalCommander;
    m_fontPt = std::clamp(Settings::GetInt(L"FontSize", kDefaultFontPt), kMinFontPt, kMaxFontPt);

    // In der Schnellansicht (Strg+Q) hängt das Plugin direkt im TC-Hauptfenster.
    wchar_t cls[64] = {};
    GetClassNameW(GetAncestor(parent, GA_ROOT), cls, 64);
    m_quickView = wcscmp(cls, L"TTOTAL_CMD") == 0;

    RECT rc;
    GetClientRect(parent, &rc);
    m_hwnd = CreateWindowExW(0, CLASS_NAME, nullptr,
                             WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
                             0, 0, rc.right, rc.bottom, parent, nullptr, g_hInst, this);
    if (!m_hwnd) return;
    m_dpi = GetDpiForWindow(m_hwnd);
    if (!m_dpi) m_dpi = 96;
    RecreateFonts();
}

std::unique_ptr<GridView> GridView::Create(HWND parent, const std::wstring& path, int showFlags)
{
    std::unique_ptr<GridView> v(new GridView(parent));
    if (!v->m_hwnd) return nullptr;
    v->m_showFlags = showFlags;
    v->m_tcDark = Theme::TcIsDark(showFlags);
    v->ApplyTheme();
    if (!v->LoadFile(path, 0)) return nullptr;
    ShowWindow(v->m_hwnd, SW_SHOW);
    return v;
}

GridView::~GridView()
{
    if (m_edit) RemoveWindowSubclass(m_edit, EditProc, 1);
    if (m_hwnd) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
    }
    if (m_font) DeleteObject(m_font);
    if (m_fontBold) DeleteObject(m_fontBold);
    if (m_editBrush) DeleteObject(m_editBrush);
}

// ── Laden / Speichern ────────────────────────────────────────────────────────

bool GridView::LoadFile(const std::wstring& path, wchar_t sep)
{
    CsvDocument doc;
    std::wstring err;
    if (!doc.LoadFile(path, err, sep, PreferredSeparator(path))) {
        Settings::Log(L"LoadFile failed: %ls (%ls)", path.c_str(), err.c_str());
        return false;
    }
    m_doc = std::move(doc);
    m_path = path;
    m_inTempCopy = IsTcTempPath(path);
    m_headerMode = GuessHeader(m_doc);
    ResetView();
    m_message.clear();
    if (m_inTempCopy)
        ShowMessage(Tr(L"Temporary copy (archive/FTP): saving does not change the original",
                       L"Temporäre Kopie (Archiv/FTP): Speichern ändert nicht das Original"), true, 10000);
    return true;
}

void GridView::ResetView()
{
    m_curRow = FirstDataRow();
    m_curCol = 0;
    m_top = FirstDataRow();
    m_scrollX = 0;
    m_selActive = false;
    m_pending = Pending::None;
    m_lastPercent = -1;
    ComputeColumnWidths();
    UpdateGutter();
    ClampState();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool GridView::Save()
{
    EndEdit(true);
    if (!m_doc.IsModified()) {
        ShowMessage(Tr(L"No changes to save", L"Keine Änderungen zu speichern"));
        return true;
    }
    std::string bytes;
    if (!m_doc.EncodeForSave(bytes)) {
        // Die Knöpfe beschriftet Windows in seiner Sprache, nicht in der von
        // TC — daher eine Ja/Nein-Frage ohne Knopfnamen.
        int r = Ask(Fmt(Tr(L"Some characters cannot be saved in the ANSI encoding (code page %1).\n\n"
                           L"Save as UTF-8 (with BOM) instead?\n"
                           L"Otherwise the file stays ANSI and these characters become “?”.",
                           L"Einige Zeichen lassen sich in der ANSI-Kodierung (Codepage %1) nicht speichern.\n\n"
                           L"Stattdessen als UTF-8 (mit BOM) speichern?\n"
                           L"Andernfalls bleibt die Datei ANSI und diese Zeichen werden zu „?“."),
                        { std::to_wstring(m_doc.AnsiCodePage()) }),
                    MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL) return false;
        if (r == IDYES) {
            m_doc.SetEncoding(TextEncoding::Utf8Bom);
            m_doc.EncodeForSave(bytes);
        }
    }
    std::wstring err;
    if (!CsvDocument::WriteFileAtomic(m_path, bytes, err)) {
        Ask(Fmt(Tr(L"Could not save %1:\n\n%2", L"%1 konnte nicht gespeichert werden:\n\n%2"),
                { Quoted(FileName(m_path)), err }),
            MB_OK | MB_ICONERROR);
        return false;
    }
    m_doc.MarkSaved();
    if (m_inTempCopy)
        ShowMessage(Tr(L"Saved – only to the temporary copy (archive/FTP)!",
                       L"Gespeichert – nur in der temporären Kopie (Archiv/FTP)!"), true, 8000);
    else
        ShowMessage(Tr(L"Saved", L"Gespeichert"));
    return true;
}

std::wstring GridView::SaveQuestion() const
{
    return Fmt(Tr(L"Save changes to %1?", L"Änderungen an %1 speichern?"), { Quoted(FileName(m_path)) });
}

void GridView::OnEscape()
{
    if (m_doc.IsModified()) {
        int r = Ask(SaveQuestion(), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) {
            if (!Save()) return;
        } else if (m_quickView) {
            // Schnellansicht bleibt offen: Änderungen wirklich verwerfen.
            LoadFile(m_path, 0);
            ShowMessage(Tr(L"Changes discarded", L"Änderungen verworfen"));
            return;
        } else {
            m_doc.MarkSaved();   // verwerfen, beim Schließen nicht erneut fragen
        }
    }
    if (!m_quickView) ForwardKey(VK_ESCAPE);
}

void GridView::PromptSaveBeforeLeave()
{
    EndEdit(true);
    if (!m_doc.IsModified()) return;
    int r = Ask(SaveQuestion(), MB_YESNO | MB_ICONQUESTION);
    if (r == IDYES) Save();
    m_doc.MarkSaved();
}

bool GridView::LoadNext(const std::wstring& path, int showFlags)
{
    PromptSaveBeforeLeave();
    m_showFlags = showFlags;
    m_tcDark = Theme::TcIsDark(showFlags);
    ApplyTheme();
    return LoadFile(path, 0);
}

int GridView::Ask(const std::wstring& text, UINT flags)
{
    const int r = MessageBoxW(GetAncestor(m_hwnd, GA_ROOT), text.c_str(), L"CsvTC", flags);
    Settings::Log(L"Ask -> %d (err %lu): %ls", r, r ? 0 : GetLastError(), text.c_str());
    return r;
}

// ── Darstellung ──────────────────────────────────────────────────────────────

void GridView::ApplyTheme()
{
    m_pal = Theme::Make(m_themeMode, m_tcDark);
    const wchar_t* theme = m_pal.dark ? L"DarkMode_Explorer" : L"Explorer";
    SetWindowTheme(m_hwnd, theme, nullptr);
    if (m_edit) SetWindowTheme(m_edit, theme, nullptr);
    if (m_editBrush) DeleteObject(m_editBrush);
    m_editBrush = CreateSolidBrush(m_pal.editBg);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void GridView::RecreateFonts()
{
    if (m_font) DeleteObject(m_font);
    if (m_fontBold) DeleteObject(m_fontBold);
    const int h = -MulDiv(m_fontPt, (int)m_dpi, 72);
    m_font = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    m_fontBold = CreateFontW(h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, m_font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    m_lineH = tm.tmHeight;
    m_charW = std::max<int>(1, tm.tmAveCharWidth);
    m_padX = MulDiv(6, (int)m_dpi, 96);
    m_padY = MulDiv(3, (int)m_dpi, 96);
    m_rowH = m_lineH + 2 * m_padY + 1;
    if (m_edit) SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_font, TRUE);
}

int GridView::MeasureText(std::wstring_view text, bool bold)
{
    std::wstring buf;
    DisplayText(text, buf, 200);
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, bold ? m_fontBold : m_font);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, buf.c_str(), (int)buf.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    return sz.cx;
}

static int ClampColWidth(int textW, int padX, int charW, UINT dpi)
{
    const int minW = charW * 4 + 2 * padX;
    const int maxW = MulDiv(420, (int)dpi, 96);
    return std::clamp(textW + 2 * padX + 1, minW, std::max(minW, maxW));
}

// Stichprobe für die Spaltenbreiten: Anfang, gleichmäßig verteilt, Ende
// (insgesamt höchstens ~1400 Zeilen bzw. ~80.000 Zellen).
static std::vector<size_t> SampleRows(size_t rows, size_t cols)
{
    const size_t budget = std::max<size_t>(60, 80000 / std::max<size_t>(cols, 1));
    const size_t head = std::min<size_t>({ rows, (size_t)1000, budget * 3 / 4 });
    std::vector<size_t> out;
    for (size_t r = 0; r < head; ++r) out.push_back(r);
    if (rows > head) {
        const size_t extra = std::min<size_t>(rows - head, std::max<size_t>(budget / 4, 20));
        const size_t spread = extra / 2, tail = extra - spread;
        for (size_t i = 1; i <= spread; ++i) out.push_back(head + (rows - head) * i / (spread + 1));
        for (size_t r = rows - tail; r < rows; ++r) out.push_back(r);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }
    return out;
}

void GridView::ComputeColumnWidths()
{
    const size_t cols = NavCols();
    std::vector<int> text(cols, 0);

    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, m_font);
    std::wstring buf;
    SIZE sz = {};
    for (size_t r : SampleRows(m_doc.RowCount(), cols)) {
        const bool bold = m_headerMode && r == 0;
        if (bold) SelectObject(dc, m_fontBold);
        const size_t n = m_doc.RowSize(r);
        for (size_t c = 0; c < n && c < cols; ++c) {
            std::wstring_view v = m_doc.Cell(r, c);
            if (v.empty()) continue;
            DisplayText(v, buf, 200);
            if (GetTextExtentPoint32W(dc, buf.c_str(), (int)buf.size(), &sz)) text[c] = std::max<int>(text[c], sz.cx);
        }
        if (bold) SelectObject(dc, m_font);
    }
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);

    m_colW.resize(cols);
    for (size_t c = 0; c < cols; ++c) m_colW[c] = ClampColWidth(text[c], m_padX, m_charW, m_dpi);
    UpdateColX();
}

int GridView::MeasureColumn(size_t c)
{
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, m_font);
    std::wstring buf;
    SIZE sz = {};
    int w = 0;
    for (size_t r : SampleRows(m_doc.RowCount(), 1)) {
        std::wstring_view v = m_doc.Cell(r, c);
        if (v.empty()) continue;
        const bool bold = m_headerMode && r == 0;
        if (bold) SelectObject(dc, m_fontBold);
        DisplayText(v, buf, 200);
        if (GetTextExtentPoint32W(dc, buf.c_str(), (int)buf.size(), &sz)) w = std::max<int>(w, sz.cx);
        if (bold) SelectObject(dc, m_font);
    }
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    return ClampColWidth(w, m_padX, m_charW, m_dpi);
}

void GridView::SyncColumns()
{
    const size_t cols = NavCols();
    if (m_colW.size() > cols) m_colW.resize(cols);
    while (m_colW.size() < cols) m_colW.push_back(MeasureColumn(m_colW.size()));
    UpdateColX();
}

void GridView::UpdateColX()
{
    m_colX.resize(m_colW.size() + 1);
    m_colX[0] = 0;
    for (size_t c = 0; c < m_colW.size(); ++c) m_colX[c + 1] = m_colX[c] + m_colW[c];
}

void GridView::UpdateGutter()
{
    size_t digits = std::to_wstring(std::max<size_t>(m_doc.RowCount(), 1)).size();
    digits = std::max<size_t>(digits, 3);
    m_gutterW = MeasureText(std::wstring(digits, L'0'), false) + 2 * m_padX + MulDiv(4, (int)m_dpi, 96);
}

void GridView::UpdateScrollBars()
{
    static thread_local bool inUpdate = false;   // Ein-/Ausblenden löst WM_SIZE aus
    if (inUpdate || !m_hwnd) return;
    inUpdate = true;
    const size_t fdr = FirstDataRow();
    const size_t data = m_doc.RowCount() - std::min(fdr, m_doc.RowCount());
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = data ? (int)data - 1 : 0;
    si.nPage = (UINT)VisibleRows();
    si.nPos = (int)(m_top - std::min(fdr, m_top));
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
    si.nMax = std::max(0, TotalWidth() - 1);
    si.nPage = (UINT)std::max(0, ViewWidth());
    si.nPos = m_scrollX;
    SetScrollInfo(m_hwnd, SB_HORZ, &si, TRUE);
    inUpdate = false;
}

void GridView::ShowMessage(const std::wstring& text, bool warn, UINT ms)
{
    m_message = text;
    m_messageWarn = warn;
    SetTimer(m_hwnd, kTimerMessage, ms, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

std::wstring GridView::PendingHint() const
{
    if (m_pending == Pending::Rows) {
        const size_t n = m_selActive ? m_selLast - m_selFirst + 1 : 1;
        if (n == 1) return Tr(L"d – press d again to delete the row", L"d – nochmal d löscht die Zeile");
        return Fmt(Tr(L"d – press d again to delete %1", L"d – nochmal d löscht %1"), { RowsText(n) });
    }
    if (m_pending == Pending::Column) {
        const std::wstring col = std::to_wstring(m_curCol + 1);
        if (m_headerMode && !m_doc.Cell(0, m_curCol).empty()) {
            std::wstring buf;
            DisplayText(m_doc.Cell(0, m_curCol), buf, 40);
            return Fmt(Tr(L"Shift+D – press Shift+D again to delete column %1 %2",
                          L"Shift+D – nochmal Shift+D löscht Spalte %1 %2"), { col, Quoted(buf) });
        }
        return Fmt(Tr(L"Shift+D – press Shift+D again to delete column %1",
                      L"Shift+D – nochmal Shift+D löscht Spalte %1"), { col });
    }
    if (m_pending == Pending::Insert) {
        const size_t at = InsertPosition(), fdr = FirstDataRow();
        if (at >= fdr && at < m_doc.RowCount())
            return Fmt(Tr(L"i – press i again to insert an empty row before row %1",
                          L"i – nochmal i fügt eine leere Zeile vor Zeile %1 ein"), { FormatCount(at - fdr + 1) });
        return Tr(L"i – press i again to insert an empty row", L"i – nochmal i fügt eine leere Zeile ein");
    }
    return {};
}

void GridView::ReportPercent()
{
    if (m_quickView) return;
    const size_t fdr = FirstDataRow();
    const size_t maxTop = MaxTop();
    const int percent = maxTop > fdr ? (int)((m_top - fdr) * 100 / (maxTop - fdr)) : 100;
    if (percent == m_lastPercent) return;
    m_lastPercent = percent;
    PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(percent, itm_percent), (LPARAM)m_hwnd);
}

// ── Geometrie ────────────────────────────────────────────────────────────────

RECT GridView::ClientRect() const
{
    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    return rc;
}

int GridView::DataBottom() const
{
    return std::max(DataTop(), (int)ClientRect().bottom - m_rowH);
}

int GridView::VisibleRows() const
{
    return std::max(1, (DataBottom() - DataTop()) / m_rowH);
}

int GridView::ViewWidth() const
{
    return std::max(0, (int)ClientRect().right - m_gutterW);
}

size_t GridView::MaxTop() const
{
    const size_t fdr = FirstDataRow();
    const size_t rows = m_doc.RowCount();
    const size_t data = rows > fdr ? rows - fdr : 0;
    const size_t vis = (size_t)VisibleRows();
    return fdr + (data > vis ? data - vis : 0);
}

bool GridView::CellRect(size_t row, size_t col, RECT& rc) const
{
    if (col >= m_colW.size()) return false;
    const int x = m_gutterW + m_colX[col] - m_scrollX;
    if (m_headerMode && row == 0) {
        rc = { x, 0, x + m_colW[col], m_rowH };
        return true;
    }
    if (row < m_top || row - m_top > (size_t)VisibleRows()) return false;
    const int y = DataTop() + (int)(row - m_top) * m_rowH;
    rc = { x, y, x + m_colW[col], y + m_rowH };
    return true;
}

GridView::Hit GridView::HitTest(int x, int y) const
{
    Hit h;
    if (y >= DataBottom()) { h.area = Area::Status; return h; }
    const int xx = x - m_gutterW + m_scrollX;
    if (x >= m_gutterW && xx >= 0 && xx < TotalWidth()) {
        auto it = std::upper_bound(m_colX.begin(), m_colX.end(), xx);
        h.col = (size_t)(it - m_colX.begin()) - 1;
        h.hasCol = true;
    }
    if (y < DataTop()) {
        if (x < m_gutterW) { h.area = Area::Corner; return h; }
        h.area = Area::Header;
        const int tol = MulDiv(4, (int)m_dpi, 96);
        for (size_t c = 0; c < m_colW.size(); ++c) {
            const int edge = m_gutterW + m_colX[c + 1] - m_scrollX;
            if (std::abs(x - edge) <= tol) { h.onBorder = true; h.borderCol = c; break; }
            if (edge > x + tol) break;
        }
        return h;
    }
    const size_t r = m_top + (size_t)((y - DataTop()) / m_rowH);
    if (r >= m_doc.RowCount()) return h;
    h.row = r;
    h.area = x < m_gutterW ? Area::Gutter : Area::Cell;
    return h;
}

// ── Bewegung / Auswahl ───────────────────────────────────────────────────────

void GridView::ClampState()
{
    const size_t rows = m_doc.RowCount();
    if (rows == 0) {
        m_curRow = m_top = 0;
        m_selActive = false;
    } else {
        if (m_curRow >= rows) m_curRow = rows - 1;
        const size_t fdr = FirstDataRow();
        m_top = std::clamp(m_top, fdr, std::max(fdr, MaxTop()));
        if (m_selActive) {
            if (m_selFirst >= rows) m_selActive = false;
            m_selLast = std::min(m_selLast, rows - 1);
            m_anchor = std::min(m_anchor, rows - 1);
        }
    }
    if (m_curCol >= NavCols()) m_curCol = NavCols() - 1;
    m_scrollX = std::clamp(m_scrollX, 0, std::max(0, TotalWidth() - ViewWidth()));
}

void GridView::EnsureVisible(bool headerToTop)
{
    const size_t fdr = FirstDataRow();
    const size_t vis = (size_t)VisibleRows();
    if (m_curRow >= fdr) {
        if (m_curRow < m_top) m_top = m_curRow;
        else if (m_curRow >= m_top + vis) m_top = m_curRow - vis + 1;
    } else if (headerToTop) {
        // Cursor auf der (fixierten) Kopfzeile: auch die Daten an den Anfang,
        // sonst steht man "oben", sieht aber die Mitte der Datei.
        m_top = fdr;
    }
    if (m_curCol < m_colW.size()) {
        const int left = m_colX[m_curCol], right = m_colX[m_curCol + 1], view = ViewWidth();
        if (left < m_scrollX) m_scrollX = left;
        else if (right > m_scrollX + view) m_scrollX = std::min(left, right - view);
    }
    ClampState();
}

void GridView::PageMove(int dir, bool extend)
{
    const size_t rows = m_doc.RowCount();
    if (!rows) return;
    const ptrdiff_t fdr = (ptrdiff_t)FirstDataRow();
    const ptrdiff_t last = (ptrdiff_t)rows - 1;
    const ptrdiff_t page = std::max(1, VisibleRows() - 1);
    if (fdr > last) return;                              // nur Kopfzeile, keine Daten
    if ((ptrdiff_t)m_curRow < fdr) {                     // Cursor auf der Kopfzeile
        if (dir > 0) MoveTo(std::min(fdr + page - 1, last), (ptrdiff_t)m_curCol, extend);
        return;
    }
    // Ansicht und Cursor um eine Seite verschieben; Bild↑ endet in der
    // ersten Datenzeile, nicht in der Kopfzeile.
    const ptrdiff_t maxTop = std::max(fdr, (ptrdiff_t)MaxTop());
    m_top = (size_t)std::clamp((ptrdiff_t)m_top + dir * page, fdr, maxTop);
    MoveTo(std::clamp((ptrdiff_t)m_curRow + dir * page, fdr, last), (ptrdiff_t)m_curCol, extend);
}

void GridView::MoveTo(ptrdiff_t row, ptrdiff_t col, bool extend, bool headerToTop)
{
    const size_t rows = m_doc.RowCount();
    if (!rows) return;
    const size_t r = (size_t)std::clamp<ptrdiff_t>(row, 0, (ptrdiff_t)rows - 1);
    const size_t c = (size_t)std::clamp<ptrdiff_t>(col, 0, (ptrdiff_t)NavCols() - 1);
    if (extend) {
        if (!m_selActive) {
            m_selActive = true;
            m_anchor = m_curRow;
        }
        m_curRow = r;
        m_curCol = c;
        m_selFirst = std::min(m_anchor, m_curRow);
        m_selLast = std::max(m_anchor, m_curRow);
    } else {
        if (r != m_curRow) m_selActive = false;
        m_curRow = r;
        m_curCol = c;
    }
    EnsureVisible(headerToTop);
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ReportPercent();
}

void GridView::ScrollToTop(ptrdiff_t top)
{
    EndEdit(true);
    const size_t fdr = FirstDataRow();
    m_top = (size_t)std::clamp<ptrdiff_t>(top, (ptrdiff_t)fdr, (ptrdiff_t)std::max(fdr, MaxTop()));
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ReportPercent();
}

void GridView::ScrollX(int x)
{
    EndEdit(true);
    m_scrollX = std::clamp(x, 0, std::max(0, TotalWidth() - ViewWidth()));
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void GridView::SelectAll()
{
    if (m_edit) {
        // Im Editor nur den Feldtext markieren — nicht alle Zeilen (ein "d d"
        // danach würde sonst alles löschen).
        SendMessageW(m_edit, EM_SETSEL, 0, -1);
        return;
    }
    const size_t rows = m_doc.RowCount();
    if (!rows) return;
    m_selActive = true;
    m_selFirst = FirstDataRow() < rows ? FirstDataRow() : 0;
    m_selLast = rows - 1;
    m_anchor = m_selFirst;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ShowMessage(Fmt(Tr(L"%1 selected", L"%1 markiert"), { RowsText(m_selLast - m_selFirst + 1) }));
}

void GridView::SetPercent(int percent)
{
    const size_t fdr = FirstDataRow();
    const size_t maxTop = MaxTop();
    ScrollToTop((ptrdiff_t)(fdr + (maxTop - fdr) * (size_t)std::clamp(percent, 0, 100) / 100));
}

bool GridView::NewParams(int showFlags)
{
    // Nur Darkmode umgeschaltet (oder gar nichts): neu einfärben und bleiben.
    // Andere Änderungen (Zeichensatz, Umbruch …) wie die übrigen Plugins:
    // LISTPLUGIN_ERROR lässt TC auf seinen eigenen Viewer wechseln.
    const int darkBits = lcp_darkmode | lcp_darkmodenative;
    const bool onlyDark = ((showFlags ^ m_showFlags) & ~darkBits) == 0;
    m_showFlags = showFlags;
    m_tcDark = Theme::TcIsDark(showFlags);
    ApplyTheme();
    return onlyDark || (showFlags & lcp_forceshow) != 0;
}

// ── Zeichnen ─────────────────────────────────────────────────────────────────

void GridView::Paint(HDC dc)
{
    const RECT rc = ClientRect();
    const int W = rc.right;
    Fill(dc, rc, m_pal.bg);
    SetBkMode(dc, TRANSPARENT);

    const size_t rows = m_doc.RowCount();
    const size_t fdr = FirstDataRow();
    const int dataTop = DataTop(), dataBottom = DataBottom();
    const int marker = std::max(2, MulDiv(3, (int)m_dpi, 96));
    const bool focus = m_hasFocus || m_edit;
    const COLORREF curBg = focus ? m_pal.cursorBg : m_pal.cursorInactiveBg;
    const COLORREF curFg = focus ? m_pal.cursorFg : m_pal.fg;

    // Sichtbarer Spaltenbereich [c0, c1)
    size_t c0 = 0, c1 = m_colW.size();
    if (!m_colW.empty()) {
        c0 = (size_t)(std::upper_bound(m_colX.begin(), m_colX.end(), m_scrollX) - m_colX.begin()) - 1;
        c0 = std::min(c0, m_colW.size() - 1);
        c1 = (size_t)(std::lower_bound(m_colX.begin(), m_colX.end(), m_scrollX + ViewWidth()) - m_colX.begin());
        c1 = std::min(c1, m_colW.size());
    }
    const int gridRight = std::min(W, m_gutterW + TotalWidth() - m_scrollX);

    HPEN pen = CreatePen(PS_SOLID, 1, m_pal.grid);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldFont = SelectObject(dc, m_font);
    std::wstring buf;

    // Datenzeilen
    int y = dataTop;
    for (size_t r = m_top; r < rows && y < dataBottom; ++r, y += m_rowH) {
        const bool sel = IsSelected(r);
        const COLORREF rowBg = sel ? m_pal.selBg
                             : r == m_curRow ? m_pal.rowHighlight
                             : ((r - fdr) & 1) ? m_pal.bgAlt : m_pal.bg;
        Fill(dc, RECT{ m_gutterW, y, gridRight, y + m_rowH }, rowBg);
        for (size_t c = c0; c < c1; ++c) {
            const int x = m_gutterW + m_colX[c] - m_scrollX;
            const RECT cell = { x, y, x + m_colW[c], y + m_rowH };
            const bool isCur = r == m_curRow && c == m_curCol;
            if (isCur) Fill(dc, RECT{ cell.left, cell.top, cell.right - 1, cell.bottom - 1 }, curBg);
            std::wstring_view v = m_doc.Cell(r, c);
            if (!v.empty()) {
                DisplayText(v, buf);
                const bool num = IsNumeric(v);
                SetTextColor(dc, isCur ? curFg : sel ? m_pal.selFg : num ? m_pal.number : m_pal.fg);
                RECT tr = { cell.left + m_padX, cell.top, cell.right - m_padX, cell.bottom - 1 };
                DrawTextW(dc, buf.c_str(), (int)buf.size(), &tr,
                          DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | (num ? DT_RIGHT : DT_LEFT));
            }
            Line(dc, cell.right - 1, y, cell.right - 1, y + m_rowH);
        }
        Line(dc, m_gutterW, y + m_rowH - 1, gridRight, y + m_rowH - 1);
    }

    if (rows == 0 || fdr >= rows) {
        SetTextColor(dc, m_pal.gutterFg);
        RECT tr = { m_gutterW, dataTop, W, dataBottom };
        const wchar_t* text = rows == 0 ? Tr(L"Empty file", L"Leere Datei") : Tr(L"No data rows", L"Keine Datenzeilen");
        DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }

    // Zeilennummern
    Fill(dc, RECT{ 0, dataTop, m_gutterW, dataBottom }, m_pal.gutterBg);
    y = dataTop;
    for (size_t r = m_top; r < rows && y < dataBottom; ++r, y += m_rowH) {
        if (IsSelected(r)) Fill(dc, RECT{ 0, y, marker, y + m_rowH }, m_pal.selMarker);
        else if (r == m_curRow) Fill(dc, RECT{ 0, y, marker, y + m_rowH }, m_pal.accent);
        const std::wstring num = std::to_wstring(r - fdr + 1);
        SetTextColor(dc, r == m_curRow ? m_pal.fg : IsSelected(r) ? m_pal.selFg : m_pal.gutterFg);
        RECT tr = { marker, y, m_gutterW - m_padX, y + m_rowH - 1 };
        DrawTextW(dc, num.c_str(), (int)num.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }
    Line(dc, m_gutterW - 1, 0, m_gutterW - 1, dataBottom);

    // Kopfzeile: erste CSV-Zeile oder Spaltennummern
    Fill(dc, RECT{ 0, 0, W, m_rowH }, m_pal.headerBg);
    SelectObject(dc, m_fontBold);
    for (size_t c = c0; c < c1; ++c) {
        const int x = m_gutterW + m_colX[c] - m_scrollX;
        const RECT cell = { x, 0, x + m_colW[c], m_rowH };
        const bool isCur = m_headerMode && m_curRow == 0 && c == m_curCol && rows;
        if (isCur) Fill(dc, RECT{ cell.left, cell.top, cell.right - 1, cell.bottom - 1 }, curBg);
        else if (c == m_curCol) Fill(dc, RECT{ cell.left, m_rowH - marker, cell.right - 1, m_rowH }, m_pal.accent);
        if (m_headerMode) DisplayText(m_doc.Cell(0, c), buf);
        else buf = std::to_wstring(c + 1);
        SetTextColor(dc, isCur ? curFg : m_headerMode ? m_pal.headerFg : m_pal.gutterFg);
        RECT tr = { cell.left + m_padX, 0, cell.right - m_padX, m_rowH - 1 };
        DrawTextW(dc, buf.c_str(), (int)buf.size(), &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | (m_headerMode ? DT_LEFT : DT_CENTER));
        Line(dc, cell.right - 1, 0, cell.right - 1, m_rowH);
    }
    Line(dc, 0, m_rowH - 1, W, m_rowH - 1);
    Fill(dc, RECT{ 0, 0, m_gutterW - 1, m_rowH - 1 }, m_pal.headerBg);
    SetTextColor(dc, m_pal.gutterFg);
    RECT corner = { 0, 0, m_gutterW - m_padX, m_rowH - 1 };
    DrawTextW(dc, L"#", 1, &corner, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SelectObject(dc, m_font);

    // Rahmen um den Inline-Editor
    if (m_edit) Fill(dc, m_editRect, m_pal.editBorder);

    PaintStatus(dc, RECT{ 0, dataBottom, W, rc.bottom });

    SelectObject(dc, oldFont);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void GridView::PaintStatus(HDC dc, const RECT& rc)
{
    Fill(dc, rc, m_pal.statusBg);
    SelectObject(dc, m_font);
    const int gap = m_padX * 3;

    // rechts: Hinweis / Meldung
    std::wstring right;
    COLORREF rightColor = m_pal.gutterFg;
    if (m_pending != Pending::None) {
        right = PendingHint();
        rightColor = m_pal.statusWarn;
    } else if (!m_message.empty()) {
        right = m_message;
        rightColor = m_messageWarn ? m_pal.statusWarn : m_pal.statusOk;
    } else {
        right = Tr(L"F1 Help", L"F1 Hilfe");
    }
    SIZE rs = {};
    GetTextExtentPoint32W(dc, right.c_str(), (int)right.size(), &rs);
    const int rightLeft = std::max<int>(rc.left + m_padX, rc.right - m_padX - rs.cx);
    RECT rr = { rightLeft, rc.top, rc.right - m_padX, rc.bottom };
    SetTextColor(dc, rightColor);
    DrawTextW(dc, right.c_str(), (int)right.size(), &rr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    int x = rc.left + m_padX;
    if (m_doc.IsModified()) {
        const wchar_t* mod = Tr(L"● Modified", L"● Geändert");
        SelectObject(dc, m_fontBold);
        SIZE ms = {};
        GetTextExtentPoint32W(dc, mod, (int)wcslen(mod), &ms);
        RECT mr = { x, rc.top, std::min<int>(x + ms.cx, rightLeft - gap), rc.bottom };
        SetTextColor(dc, m_pal.statusWarn);
        DrawTextW(dc, mod, -1, &mr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, m_font);
        x += ms.cx + gap;
    }

    const size_t rows = m_doc.RowCount(), fdr = FirstDataRow();
    std::wstring left = Fmt(m_doc.SeparatorDetected() ? Tr(L"Separator: %1 (detected)", L"Trenner: %1 (erkannt)")
                                                      : Tr(L"Separator: %1 (manual)", L"Trenner: %1 (manuell)"),
                            { CsvDocument::SeparatorName(m_doc.Separator()) });
    left += L"   ";
    left += CsvDocument::EncodingName(m_doc.Encoding());
    left += L" · ";
    left += CsvDocument::EolName(m_doc.LineEnding());
    left += L"   " + RowsText(rows > fdr ? rows - fdr : 0) + L" × " + ColumnsText(m_doc.ColCount());
    if (rows) {
        left += L"   ";
        if (m_headerMode && m_curRow == 0) left += Tr(L"Header", L"Kopfzeile");
        else left += Fmt(Tr(L"Row %1", L"Z. %1"), { FormatCount(m_curRow - fdr + 1) });
        left += L" · " + Fmt(Tr(L"Col %1", L"Sp. %1"), { std::to_wstring(m_curCol + 1) });
    }
    if (m_selActive)
        left += L"   " + Fmt(Tr(L"%1 selected", L"%1 markiert"), { FormatCount(m_selLast - m_selFirst + 1) });
    RECT lr = { x, rc.top, rightLeft - gap, rc.bottom };
    SetTextColor(dc, m_pal.statusFg);
    if (lr.right > lr.left)
        DrawTextW(dc, left.c_str(), (int)left.size(), &lr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// ── Tastatur ─────────────────────────────────────────────────────────────────

void GridView::ForwardKey(UINT vk)
{
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    PostMessageW(m_parent, WM_KEYDOWN, vk, 1 | ((LPARAM)scan << 16));
}

void GridView::OnKeyDown(UINT vk, LPARAM lp)
{
    switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_CAPITAL:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
    case VK_LWIN: case VK_RWIN:
        return;   // reine Modifikatoren unterbrechen kein "d d"
    }
    const bool ctrl = KeyDown(VK_CONTROL);
    const bool shift = KeyDown(VK_SHIFT);
    const bool repeat = (lp & (1 << 30)) != 0;
    Settings::Log(L"key vk=0x%02X ctrl=%d shift=%d repeat=%d", vk, ctrl, shift, repeat);

    // Doppeltasten: "d d" löscht Zeile(n), "Shift+D Shift+D" die Spalte,
    // "i i" fügt eine leere Zeile an der Cursorposition ein.
    Pending kind = Pending::None;
    if (!ctrl && vk == 'D') kind = shift ? Pending::Column : Pending::Rows;
    else if (!ctrl && !shift && vk == 'I') kind = Pending::Insert;
    if (kind != Pending::None) {
        if (repeat) return;   // gehaltene Taste löst nichts aus
        if (m_pending == kind && GetTickCount() - m_pendingTick <= kPendingMs) {
            m_pending = Pending::None;
            KillTimer(m_hwnd, kTimerPending);
            if (kind == Pending::Rows) DeleteRows();
            else if (kind == Pending::Column) DeleteColumn();
            else InsertRow();
        } else if (m_doc.RowCount() || kind == Pending::Insert) {
            m_pending = kind;
            m_pendingTick = GetTickCount();
            SetTimer(m_hwnd, kTimerPending, kPendingMs, nullptr);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }
    if (m_pending != Pending::None) {
        m_pending = Pending::None;
        KillTimer(m_hwnd, kTimerPending);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        if (vk == VK_ESCAPE) return;   // Esc bricht nur das Löschen ab
    }

    const ptrdiff_t row = (ptrdiff_t)m_curRow, col = (ptrdiff_t)m_curCol;

    if (ctrl) {
        switch (vk) {
        case 'S': Save(); return;
        case 'Z': Undo(); return;
        case 'C': case VK_INSERT: CopyToClipboard(); return;
        case 'A': SelectAll(); return;
        case VK_HOME: MoveTo(0, 0, shift); return;
        case VK_END: MoveTo((ptrdiff_t)m_doc.RowCount() - 1, (ptrdiff_t)NavCols() - 1, shift); return;
        case VK_OEM_PLUS: case VK_ADD: Zoom(+1); return;
        case VK_OEM_MINUS: case VK_SUBTRACT: Zoom(-1); return;
        case '0': case VK_NUMPAD0: Zoom(0); return;
        case 'F': case 'P': ForwardKey(vk); return;   // Suchen, Drucken (TC)
        }
        return;
    }

    switch (vk) {
    case VK_UP:    MoveTo(row - 1, col, shift); return;
    case VK_DOWN:  MoveTo(row + 1, col, shift); return;
    case VK_PRIOR: PageMove(-1, shift); return;
    case VK_NEXT:  PageMove(+1, shift); return;
    case VK_LEFT:  MoveTo(row, col - 1, shift); return;
    case VK_RIGHT: MoveTo(row, col + 1, shift); return;
    case VK_HOME:  MoveTo(row, 0, shift); return;
    case VK_END:   MoveTo(row, (ptrdiff_t)NavCols() - 1, shift); return;
    case VK_TAB:   MoveTo(row, col + (shift ? -1 : 1), false); return;
    case 'E':
    case VK_F2:    BeginEdit(); return;
    case 'S':      CycleSeparator(shift ? -1 : 1); return;
    case 'H':      ToggleHeader(); return;
    case 'T':
        SetThemeMode(m_themeMode == ThemeMode::Markdown ? ThemeMode::TotalCommander : ThemeMode::Markdown);
        return;
    case VK_F1:    ShowHelp(); return;
    case VK_ESCAPE: OnEscape(); return;
    case 'N': case 'P':
    case VK_F3: case VK_F7:
        ForwardKey(vk);
        return;
    }
    if (vk >= '1' && vk <= '8' && !shift) ForwardKey(vk);   // Anzeigemodus des Listers
}

// ── Inline-Editor ────────────────────────────────────────────────────────────

void GridView::BeginEdit()
{
    if (m_edit) return;
    if (!m_doc.RowCount()) {
        ShowMessage(Tr(L"Empty file – nothing to edit", L"Leere Datei – nichts zu bearbeiten"), true);
        return;
    }
    EnsureVisible(false);   // Kopfzeile ist fixiert, dafür nicht wegscrollen
    UpdateScrollBars();
    RECT cell;
    if (!CellRect(m_curRow, m_curCol, cell)) return;

    std::wstring_view v = m_doc.Cell(m_curRow, m_curCol);
    m_editNewline = v.find(L"\r\n") != std::wstring_view::npos ? Eol::CRLF
                  : v.find(L'\n') != std::wstring_view::npos ? Eol::LF
                  : v.find(L'\r') != std::wstring_view::npos ? Eol::CR : Eol::LF;
    // Für das EDIT-Control alle Umbrüche als CRLF
    std::wstring text;
    text.reserve(v.size() + 8);
    size_t lines = 1;
    int widest = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == L'\r' || v[i] == L'\n') {
            widest = std::max(widest, MeasureText(std::wstring_view(text).substr(lineStart), false));
            if (v[i] == L'\r' && i + 1 < v.size() && v[i + 1] == L'\n') ++i;
            text += L"\r\n";
            lineStart = text.size();
            ++lines;
        } else {
            text += v[i];
        }
    }
    widest = std::max(widest, MeasureText(std::wstring_view(text).substr(lineStart), false));

    const RECT rc = ClientRect();
    const int border = 2;
    const int cellW = cell.right - cell.left;
    int w = std::max(cellW, widest + 2 * m_padX + MulDiv(24, (int)m_dpi, 96) + 2 * border);
    w = std::min(w, std::max(cellW, (int)rc.right - m_gutterW));
    int left = cell.left;
    if (left + w > rc.right) left = std::max(m_gutterW, (int)rc.right - w);
    const int shown = (int)std::min(lines, kEditMaxLines);
    const int h = std::max(m_rowH, shown * m_lineH + 2 * m_padY + 2 * border);
    int top = cell.top;
    if (top + h > DataBottom()) top = std::max(0, DataBottom() - h);
    m_editRect = { left, top, left + w, top + h };

    DWORD style = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_NOHIDESEL;
    if (lines > kEditMaxLines) style |= WS_VSCROLL;
    m_edit = CreateWindowExW(0, L"EDIT", nullptr, style,
                             left + border, top + border, w - 2 * border, h - 2 * border,
                             m_hwnd, nullptr, g_hInst, nullptr);
    if (!m_edit) return;
    SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_font, FALSE);
    SendMessageW(m_edit, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    SetWindowTextW(m_edit, text.c_str());
    RECT er;
    GetClientRect(m_edit, &er);
    er.left += std::max(0, m_padX - border);
    er.right -= border;
    er.top += lines == 1 ? std::max(0, ((int)er.bottom - m_lineH) / 2) : std::max(0, m_padY - border);
    SendMessageW(m_edit, EM_SETRECTNP, 0, (LPARAM)&er);
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)text.size(), (LPARAM)text.size());
    SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
    if (m_pal.dark) SetWindowTheme(m_edit, L"DarkMode_Explorer", nullptr);
    SetWindowSubclass(m_edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));

    m_editRow = m_curRow;
    m_editCol = m_curCol;
    ++m_editGen;
    SetFocus(m_edit);
    ShowMessage(Tr(L"Enter accept · Esc discard · Shift+Enter line break",
                   L"Enter übernehmen · Esc verwerfen · Shift+Enter Zeilenumbruch"), false, 8000);
}

void GridView::EndEdit(bool commit, int moveCol)
{
    if (!m_edit) return;
    HWND e = m_edit;
    m_edit = nullptr;
    std::wstring text;
    if (commit) {
        const int n = GetWindowTextLengthW(e);
        text.resize((size_t)n + 1);
        GetWindowTextW(e, text.data(), n + 1);
        text.resize((size_t)n);
    }
    const bool hadFocus = GetFocus() == e;
    RemoveWindowSubclass(e, EditProc, 1);
    if (hadFocus) SetFocus(m_hwnd);
    DestroyWindow(e);
    m_message.clear();

    if (commit) {
        if (m_editNewline != Eol::CRLF) {
            const wchar_t nl = m_editNewline == Eol::CR ? L'\r' : L'\n';
            std::wstring out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') { out += nl; ++i; }
                else out += text[i];
            }
            text.swap(out);
        }
        if (m_doc.SetCell(m_editRow, m_editCol, text)) {
            SyncColumns();
            if (m_editCol < m_colW.size()) {
                const int w = ClampColWidth(MeasureText(text, m_headerMode && m_editRow == 0), m_padX, m_charW, m_dpi);
                if (w > m_colW[m_editCol]) {
                    m_colW[m_editCol] = w;
                    UpdateColX();
                }
            }
        }
    }
    if (moveCol) MoveTo((ptrdiff_t)m_curRow, (ptrdiff_t)m_curCol + moveCol, false);
    ClampState();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK GridView::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    auto* self = reinterpret_cast<GridView*>(data);
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL;
    case WM_KEYDOWN: {
        const bool ctrl = KeyDown(VK_CONTROL), shift = KeyDown(VK_SHIFT);
        switch (wp) {
        case VK_RETURN:
            if (ctrl || shift) {
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"\r\n");
                return 0;
            }
            PostMessageW(self->m_hwnd, WM_APP_ENDEDIT, self->m_editGen, kEditCommit);
            return 0;
        case VK_ESCAPE:
            PostMessageW(self->m_hwnd, WM_APP_ENDEDIT, self->m_editGen, kEditCancel);
            return 0;
        case VK_TAB:
            PostMessageW(self->m_hwnd, WM_APP_ENDEDIT, self->m_editGen, shift ? kEditCommitLeft : kEditCommitRight);
            return 0;
        case 'A':
            if (ctrl) {
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
            break;
        }
        break;
    }
    case WM_CHAR:
        // Enter/Esc/Tab/Strg+A haben wir schon behandelt — kein Piepen, kein Einfügen
        if (wp == L'\r' || wp == L'\n' || wp == 0x1B || wp == L'\t' || wp == 0x01) return 0;
        break;
    case WM_KILLFOCUS:
        PostMessageW(self->m_hwnd, WM_APP_ENDEDIT, self->m_editGen, kEditCommit);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, EditProc, 1);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ── Befehle ──────────────────────────────────────────────────────────────────

void GridView::DeleteRows()
{
    EndEdit(true);
    const size_t rows = m_doc.RowCount();
    if (!rows) return;
    const size_t first = m_selActive ? m_selFirst : m_curRow;
    const size_t last = m_selActive ? m_selLast : m_curRow;
    if (!m_doc.DeleteRows(first, last)) return;
    const size_t n = last - first + 1;
    m_selActive = false;
    m_curRow = std::min(first, m_doc.RowCount() ? m_doc.RowCount() - 1 : 0);
    SyncColumns();
    UpdateGutter();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    if (n == 1)
        ShowMessage(Tr(L"Row deleted – Ctrl+Z to undo", L"Zeile gelöscht – Strg+Z macht es rückgängig"));
    else
        ShowMessage(Fmt(Tr(L"%1 deleted – Ctrl+Z to undo", L"%1 gelöscht – Strg+Z macht es rückgängig"),
                        { RowsText(n) }));
}

// Neue Zeile kommt an die Cursorposition; steht der Cursor auf der
// Kopfzeile, direkt darunter (die Kopfzeile bleibt Kopfzeile).
size_t GridView::InsertPosition() const
{
    if (!m_doc.RowCount()) return 0;
    if (m_headerMode && m_curRow == 0) return 1;
    return m_curRow;
}

void GridView::InsertRow()
{
    EndEdit(true);
    const size_t at = InsertPosition();
    if (!m_doc.InsertRow(at)) return;
    m_curRow = at;
    m_selActive = false;
    SyncColumns();
    UpdateGutter();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ReportPercent();
    ShowMessage(Tr(L"Empty row inserted – E to edit, Ctrl+Z to undo",
                   L"Leere Zeile eingefügt – E bearbeitet, Strg+Z macht es rückgängig"));
}

void GridView::DeleteColumn()
{
    EndEdit(true);
    const size_t c = m_curCol;
    if (!m_doc.DeleteColumn(c)) {
        ShowMessage(Fmt(Tr(L"Column %1 does not exist in any row", L"Spalte %1 ist in keiner Zeile vorhanden"),
                        { std::to_wstring(c + 1) }), true);
        return;
    }
    if (c < m_colW.size() && m_colW.size() > 1) m_colW.erase(m_colW.begin() + (ptrdiff_t)c);
    SyncColumns();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    ShowMessage(Fmt(Tr(L"Column %1 deleted – Ctrl+Z to undo", L"Spalte %1 gelöscht – Strg+Z macht es rückgängig"),
                    { std::to_wstring(c + 1) }));
}

void GridView::Undo()
{
    EndEdit(true);
    const CsvDocument::UndoResult res = m_doc.Undo();
    switch (res.kind) {
    case CsvDocument::UndoResult::None:
        ShowMessage(Tr(L"Nothing to undo", L"Nichts rückgängig zu machen"));
        return;
    case CsvDocument::UndoResult::Edit:
        m_curRow = res.row;
        m_curCol = res.col;
        m_selActive = false;
        break;
    case CsvDocument::UndoResult::Rows:
        m_curRow = res.row;
        m_selActive = true;
        m_anchor = m_selFirst = res.row;
        m_selLast = res.row + res.count - 1;
        break;
    case CsvDocument::UndoResult::Column:
        if (m_colW.size() < NavCols())
            m_colW.insert(m_colW.begin() + (ptrdiff_t)std::min(res.col, m_colW.size()), MeasureColumn(res.col));
        m_curCol = res.col;
        break;
    case CsvDocument::UndoResult::Inserted:
        m_curRow = m_doc.RowCount() ? std::min(res.row, m_doc.RowCount() - 1) : 0;
        m_selActive = false;
        break;
    }
    SyncColumns();
    UpdateGutter();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ShowMessage(m_doc.IsModified() ? Tr(L"Undone", L"Rückgängig")
                                   : Tr(L"Undone – back to the saved state", L"Rückgängig – wieder im gespeicherten Zustand"));
}

void GridView::CycleSeparator(int dir)
{
    size_t idx = 0;
    for (size_t i = 0; i < kSeparatorCount; ++i)
        if (kSeparators[i] == m_doc.Separator()) idx = i;
    SetSeparator(kSeparators[(idx + kSeparatorCount + (size_t)(dir < 0 ? -1 : 1)) % kSeparatorCount]);
}

void GridView::SetSeparator(wchar_t sep)
{
    EndEdit(true);
    if (sep == m_doc.Separator()) return;
    const bool modified = m_doc.IsModified();
    m_doc.ChangeSeparator(sep);
    m_headerMode = GuessHeader(m_doc);
    m_selActive = false;
    ComputeColumnWidths();
    UpdateGutter();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    const std::wstring msg =
        modified ? Fmt(Tr(L"Separator: %1 – changes kept, undo history cleared",
                          L"Trenner: %1 – Änderungen bleiben erhalten, Undo-Verlauf zurückgesetzt"),
                       { CsvDocument::SeparatorName(sep) })
                 : Fmt(Tr(L"Separator: %1", L"Trenner: %1"), { CsvDocument::SeparatorName(sep) });
    ShowMessage(msg, false, modified ? 7000 : 4000);
}

void GridView::ToggleHeader()
{
    EndEdit(true);
    // Stand die Ansicht am Anfang, bleibt sie dort — sonst verschwände die
    // bisherige Kopfzeile beim Ausschalten oberhalb des sichtbaren Bereichs.
    const bool atStart = m_top <= FirstDataRow();
    m_headerMode = !m_headerMode;
    if (atStart) m_top = FirstDataRow();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ShowMessage(m_headerMode ? Tr(L"First row is the header", L"Erste Zeile ist Kopfzeile")
                             : Tr(L"First row is data", L"Erste Zeile ist Datenzeile"));
}

void GridView::SetThemeMode(ThemeMode mode)
{
    m_themeMode = mode;
    Settings::SetString(L"Theme", mode == ThemeMode::Markdown ? L"markdown" : L"tc");
    ApplyTheme();
    ShowMessage(mode == ThemeMode::Markdown ? Tr(L"Color scheme: Markdown reader", L"Farbschema: Markdown-Reader")
                                            : Tr(L"Color scheme: Total Commander", L"Farbschema: Total Commander"));
}

void GridView::Zoom(int delta)
{
    const int pt = delta == 0 ? kDefaultFontPt : std::clamp(m_fontPt + delta, kMinFontPt, kMaxFontPt);
    if (pt == m_fontPt) return;
    EndEdit(true);
    m_fontPt = pt;
    Settings::SetInt(L"FontSize", pt);
    RecreateFonts();
    ComputeColumnWidths();
    UpdateGutter();
    ClampState();
    EnsureVisible();
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    ShowMessage(Fmt(Tr(L"Font size %1 pt", L"Schriftgröße %1 pt"), { std::to_wstring(pt) }));
}

void GridView::ShowHelp()
{
    MessageBoxW(GetAncestor(m_hwnd, GA_ROOT), Tr(kHelpTextEn, kHelpTextDe),
                Tr(L"CsvTC – Keyboard shortcuts", L"CsvTC – Tastenbelegung"), MB_OK | MB_ICONINFORMATION);
}

void GridView::SetClipboardText(const std::wstring& text)
{
    if (!OpenClipboard(m_hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

void GridView::CopyToClipboard()
{
    if (m_edit) {
        SendMessageW(m_edit, WM_COPY, 0, 0);
        return;
    }
    if (!m_doc.RowCount()) return;
    if (m_selActive) {
        SetClipboardText(m_doc.SerializeRows(m_selFirst, m_selLast));
        ShowMessage(Fmt(Tr(L"%1 copied", L"%1 kopiert"), { RowsText(m_selLast - m_selFirst + 1) }));
    } else {
        SetClipboardText(std::wstring(m_doc.Cell(m_curRow, m_curCol)));
        ShowMessage(Tr(L"Field copied", L"Feld kopiert"));
    }
}

bool GridView::Search(const std::wstring& text, int params)
{
    EndEdit(true);
    const size_t rows = m_doc.RowCount();
    if (!rows || text.empty()) return false;
    const bool matchCase = (params & lcs_matchcase) != 0;
    const bool whole = (params & lcs_wholewords) != 0;
    const bool back = (params & lcs_backwards) != 0;
    std::wstring needle = text;
    if (!matchCase) CharLowerBuffW(needle.data(), (DWORD)needle.size());

    auto found = [&](size_t r, size_t c) {
        m_selActive = false;
        m_curRow = r;
        m_curCol = c;
        EnsureVisible(false);
        UpdateScrollBars();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        ReportPercent();
        return true;
    };

    if (!back) {
        size_t r = 0, c = 0;
        if (!(params & lcs_findfirst)) { r = m_curRow; c = m_curCol + 1; }
        for (; r < rows; ++r, c = 0)
            for (; c < m_doc.RowSize(r); ++c)
                if (MatchText(m_doc.Cell(r, c), needle, matchCase, whole)) return found(r, c);
    } else {
        ptrdiff_t r = (ptrdiff_t)rows - 1, c = PTRDIFF_MAX;
        if (!(params & lcs_findfirst)) { r = (ptrdiff_t)m_curRow; c = (ptrdiff_t)m_curCol - 1; }
        for (; r >= 0; --r, c = PTRDIFF_MAX) {
            c = std::min(c, (ptrdiff_t)m_doc.RowSize((size_t)r) - 1);
            for (; c >= 0; --c)
                if (MatchText(m_doc.Cell((size_t)r, (size_t)c), needle, matchCase, whole))
                    return found((size_t)r, (size_t)c);
        }
    }
    return false;
}

void GridView::ShowContextMenu(POINT pt)
{
    EndEdit(true);
    if (pt.x == -1 && pt.y == -1) {
        RECT c;
        if (CellRect(m_curRow, m_curCol, c)) pt = { c.left, c.bottom };
        else pt = { m_gutterW, DataTop() };
        ClientToScreen(m_hwnd, &pt);
    }
    const bool hasRows = m_doc.RowCount() > 0;
    const UINT rowsFlag = hasRows ? 0 : MF_GRAYED;

    HMENU menu = CreatePopupMenu();
    HMENU sepMenu = CreatePopupMenu();
    HMENU themeMenu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | rowsFlag, ID_EDIT, Tr(L"Edit field\tE", L"Feld bearbeiten\tE"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < kSeparatorCount; ++i)
        AppendMenuW(sepMenu, MF_STRING | (kSeparators[i] == m_doc.Separator() ? MF_CHECKED : 0),
                    ID_SEP_FIRST + i, CsvDocument::SeparatorName(kSeparators[i]));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sepMenu, Tr(L"Separator\tS", L"Trenner\tS"));
    AppendMenuW(menu, MF_STRING | (m_headerMode ? MF_CHECKED : 0), ID_HEADER,
                Tr(L"First row is header\tH", L"Erste Zeile ist Kopfzeile\tH"));
    AppendMenuW(themeMenu, MF_STRING | (m_themeMode == ThemeMode::TotalCommander ? MF_CHECKED : 0), ID_THEME_TC,
                Tr(L"Total Commander (main window)", L"Total Commander (Hauptfenster)"));
    AppendMenuW(themeMenu, MF_STRING | (m_themeMode == ThemeMode::Markdown ? MF_CHECKED : 0), ID_THEME_MD,
                Tr(L"Markdown reader (Catppuccin)", L"Markdown-Reader (Catppuccin)"));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)themeMenu, Tr(L"Color scheme\tT", L"Farbschema\tT"));
    AppendMenuW(menu, MF_STRING, ID_AUTOSIZE, Tr(L"Fit column widths", L"Spaltenbreiten anpassen"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_INSROW, Tr(L"Insert empty row\ti i", L"Leere Zeile einfügen\ti i"));
    AppendMenuW(menu, MF_STRING | rowsFlag, ID_DELROWS,
                m_selActive ? Tr(L"Delete selected rows\td d", L"Markierte Zeilen löschen\td d")
                            : Tr(L"Delete row\td d", L"Zeile löschen\td d"));
    AppendMenuW(menu, MF_STRING | (m_doc.ColCount() ? 0 : MF_GRAYED), ID_DELCOL,
                Tr(L"Delete column\tShift+D D", L"Spalte löschen\tShift+D D"));
    AppendMenuW(menu, MF_STRING | (m_doc.CanUndo() ? 0 : MF_GRAYED), ID_UNDO, Tr(L"Undo\tCtrl+Z", L"Rückgängig\tStrg+Z"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | rowsFlag, ID_COPY,
                m_selActive ? Tr(L"Copy selected rows\tCtrl+C", L"Markierte Zeilen kopieren\tStrg+C")
                            : Tr(L"Copy field\tCtrl+C", L"Feld kopieren\tStrg+C"));
    AppendMenuW(menu, MF_STRING | (m_doc.IsModified() ? 0 : MF_GRAYED), ID_SAVE, Tr(L"Save\tCtrl+S", L"Speichern\tStrg+S"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_HELP, Tr(L"Keyboard shortcuts\tF1", L"Tastenbelegung\tF1"));

    const UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd >= ID_SEP_FIRST && cmd < ID_SEP_FIRST + kSeparatorCount) {
        SetSeparator(kSeparators[cmd - ID_SEP_FIRST]);
        return;
    }
    switch (cmd) {
    case ID_EDIT:     BeginEdit(); break;
    case ID_HEADER:   ToggleHeader(); break;
    case ID_THEME_TC: SetThemeMode(ThemeMode::TotalCommander); break;
    case ID_THEME_MD: SetThemeMode(ThemeMode::Markdown); break;
    case ID_AUTOSIZE:
        ComputeColumnWidths();
        ClampState();
        UpdateScrollBars();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        break;
    case ID_INSROW:   InsertRow(); break;
    case ID_DELROWS:  DeleteRows(); break;
    case ID_DELCOL:   DeleteColumn(); break;
    case ID_UNDO:     Undo(); break;
    case ID_COPY:     CopyToClipboard(); break;
    case ID_SAVE:     Save(); break;
    case ID_HELP:     ShowHelp(); break;
    }
}

// ── Fenster-Prozedur ─────────────────────────────────────────────────────────

LRESULT CALLBACK GridView::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = reinterpret_cast<GridView*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<GridView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT GridView::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m_hwnd, &ps);
        const RECT rc = ClientRect();
        if (rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        EndEdit(true);
        ClampState();
        UpdateScrollBars();
        ReportPercent();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_SETFOCUS:
        m_hasFocus = true;
        if (m_edit) SetFocus(m_edit);
        if (m_quickView) PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(0, itm_focus), (LPARAM)m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        m_hasFocus = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        OnKeyDown((UINT)wp, lp);
        return 0;
    case WM_CHAR:
        return 0;
    case WM_SYSKEYDOWN:
        if (m_pending != Pending::None) {
            m_pending = Pending::None;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        break;   // Alt+… (Lister-Menü, Alt+F4) übernimmt DefWindowProc

    case WM_TIMER:
        KillTimer(m_hwnd, wp);
        if (wp == kTimerPending) m_pending = Pending::None;
        else if (wp == kTimerMessage) m_message.clear();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_APP_ENDEDIT:
        if (m_edit && (UINT)wp == m_editGen) {
            const int move = lp == kEditCommitRight ? 1 : lp == kEditCommitLeft ? -1 : 0;
            EndEdit(lp != kEditCancel, move);
        }
        return 0;

    case WM_CTLCOLOREDIT:
        if ((HWND)lp == m_edit) {
            SetTextColor((HDC)wp, m_pal.editFg);
            SetBkColor((HDC)wp, m_pal.editBg);
            return (LRESULT)m_editBrush;
        }
        break;

    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(m_hwnd, SB_VERT, &si);
        const ptrdiff_t fdr = (ptrdiff_t)FirstDataRow();
        ptrdiff_t top = (ptrdiff_t)m_top;
        switch (LOWORD(wp)) {
        case SB_LINEUP:        --top; break;
        case SB_LINEDOWN:      ++top; break;
        case SB_PAGEUP:        top -= VisibleRows(); break;
        case SB_PAGEDOWN:      top += VisibleRows(); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: top = fdr + si.nTrackPos; break;
        case SB_TOP:           top = 0; break;
        case SB_BOTTOM:        top = (ptrdiff_t)m_doc.RowCount(); break;
        default:               return 0;
        }
        ScrollToTop(top);
        return 0;
    }
    case WM_HSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(m_hwnd, SB_HORZ, &si);
        const int line = MulDiv(40, (int)m_dpi, 96);
        int x = m_scrollX;
        switch (LOWORD(wp)) {
        case SB_LINELEFT:      x -= line; break;
        case SB_LINERIGHT:     x += line; break;
        case SB_PAGELEFT:      x -= ViewWidth(); break;
        case SB_PAGERIGHT:     x += ViewWidth(); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: x = si.nTrackPos; break;
        case SB_LEFT:          x = 0; break;
        case SB_RIGHT:         x = TotalWidth(); break;
        default:               return 0;
        }
        ScrollX(x);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const UINT keys = GET_KEYSTATE_WPARAM(wp);
        if (keys & MK_CONTROL) {
            m_zoomAccum += delta;
            while (m_zoomAccum >= WHEEL_DELTA) { Zoom(+1); m_zoomAccum -= WHEEL_DELTA; }
            while (m_zoomAccum <= -WHEEL_DELTA) { Zoom(-1); m_zoomAccum += WHEEL_DELTA; }
            return 0;
        }
        if (keys & MK_SHIFT) {
            ScrollX(m_scrollX - MulDiv(delta, MulDiv(60, (int)m_dpi, 96), WHEEL_DELTA));
            return 0;
        }
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) lines = (UINT)VisibleRows();
        m_wheelAccum += delta;
        const int notches = m_wheelAccum / WHEEL_DELTA;
        m_wheelAccum %= WHEEL_DELTA;
        if (notches) ScrollToTop((ptrdiff_t)m_top - notches * (ptrdiff_t)lines);
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        m_hwheelAccum += GET_WHEEL_DELTA_WPARAM(wp);
        const int notches = m_hwheelAccum / WHEEL_DELTA;
        m_hwheelAccum %= WHEEL_DELTA;
        if (notches) ScrollX(m_scrollX + notches * MulDiv(60, (int)m_dpi, 96));
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        EndEdit(true);
        SetFocus(m_hwnd);
        m_pending = Pending::None;
        const Hit h = HitTest(x, y);
        const bool shift = (wp & MK_SHIFT) != 0;
        switch (h.area) {
        case Area::Header:
            if (h.onBorder) {
                m_resizing = true;
                m_resizeCol = h.borderCol;
                m_resizeStartX = x;
                m_resizeStartW = m_colW[h.borderCol];
                SetCapture(m_hwnd);
            } else if (h.hasCol) {
                if (m_headerMode && m_doc.RowCount()) MoveTo(0, (ptrdiff_t)h.col, shift, false);
                else MoveTo((ptrdiff_t)m_curRow, (ptrdiff_t)h.col, false);
            }
            break;
        case Area::Gutter:
            if (shift) {
                MoveTo((ptrdiff_t)h.row, (ptrdiff_t)m_curCol, true);
            } else {
                m_curRow = h.row;
                m_selActive = true;
                m_anchor = m_selFirst = m_selLast = h.row;
                EnsureVisible();
                UpdateScrollBars();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            break;
        case Area::Cell:
            if (!shift) m_selActive = false;
            MoveTo((ptrdiff_t)h.row, h.hasCol ? (ptrdiff_t)h.col : (ptrdiff_t)m_curCol, shift);
            break;
        case Area::Corner:
            SelectAll();
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        const Hit h = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h.area == Area::Header && h.onBorder) {
            m_colW[h.borderCol] = MeasureColumn(h.borderCol);
            UpdateColX();
            ClampState();
            UpdateScrollBars();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (h.hasCol && (h.area == Area::Cell || (h.area == Area::Header && m_headerMode))) {
            MoveTo(h.area == Area::Header ? 0 : (ptrdiff_t)h.row, (ptrdiff_t)h.col, false, false);
            BeginEdit();
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (m_resizing) {
            const int minW = m_charW * 2 + 2 * m_padX;
            m_colW[m_resizeCol] = std::max(minW, m_resizeStartW + (GET_X_LPARAM(lp) - m_resizeStartX));
            UpdateColX();
            ClampState();
            UpdateScrollBars();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (m_resizing) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        m_resizing = false;
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(m_hwnd, &p);
            const Hit h = HitTest(p.x, p.y);
            if (m_resizing || (h.area == Area::Header && h.onBorder)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_RBUTTONDOWN: {
        EndEdit(true);
        SetFocus(m_hwnd);
        const Hit h = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if ((h.area == Area::Cell || h.area == Area::Gutter) && !IsSelected(h.row))
            MoveTo((ptrdiff_t)h.row, h.hasCol ? (ptrdiff_t)h.col : (ptrdiff_t)m_curCol, false);
        else if (h.area == Area::Cell && h.hasCol) {
            m_curCol = h.col;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(m_hwnd, &p);
        ShowContextMenu(p);
        return 0;
    }
    case WM_CONTEXTMENU:
        ShowContextMenu(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        return 0;

    case WM_DPICHANGED_AFTERPARENT:
        m_dpi = GetDpiForWindow(m_hwnd);
        if (!m_dpi) m_dpi = 96;
        EndEdit(true);
        RecreateFonts();
        ComputeColumnWidths();
        UpdateGutter();
        ClampState();
        UpdateScrollBars();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_SYSCOLORCHANGE:
        m_pal = Theme::Make(m_themeMode, m_tcDark);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        break;

    case WM_NCDESTROY: {
        // Fenster wurde von außen zerstört (z. B. mit dem Lister) — nicht
        // später im Destruktor ein zweites Mal zerstören.
        HWND h = m_hwnd;
        m_hwnd = nullptr;
        m_edit = nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return DefWindowProcW(h, msg, wp, lp);
    }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

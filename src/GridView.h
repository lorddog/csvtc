/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 *
 * Selbst gezeichnete Tabellenansicht (doppelt gepuffertes GDI, es werden nur
 * sichtbare Zellen gezeichnet) mit Zellcursor, Zeilenmarkierung und
 * Inline-Editor.
 */
#pragma once
#include "CsvDocument.h"
#include "Theme.h"
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

class GridView {
public:
    static bool RegisterWindowClass(HINSTANCE hInst);
    // nullptr, wenn die Datei nicht geladen werden kann (TC nimmt dann den
    // internen Lister).
    static std::unique_ptr<GridView> Create(HWND parent, const std::wstring& path, int showFlags);
    ~GridView();

    GridView(const GridView&) = delete;
    GridView& operator=(const GridView&) = delete;

    HWND Hwnd() const { return m_hwnd; }

    // ── Aufrufe von Total Commander ────────────────────────────────────────
    bool LoadNext(const std::wstring& path, int showFlags);
    // Vor dem Schließen bzw. Dateiwechsel: kann nicht abgebrochen werden,
    // daher nur Ja/Nein.
    void PromptSaveBeforeLeave();
    bool Search(const std::wstring& text, int params);
    void CopyToClipboard();
    void SelectAll();
    void SetPercent(int percent);
    bool NewParams(int showFlags);   // false = TC soll auf eigenen Viewer wechseln

private:
    enum class Pending { None, Rows, Column, Insert };
    enum class Area { None, Corner, Header, Gutter, Cell, Status };

    struct Hit {
        Area   area = Area::None;
        size_t row = 0, col = 0;
        bool   hasCol = false;
        bool   onBorder = false;   // Spaltengrenze im Kopf (Breite ziehen)
        size_t borderCol = 0;
    };

    explicit GridView(HWND parent);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Laden / Speichern
    bool LoadFile(const std::wstring& path, wchar_t sep);
    bool Save();
    std::wstring SaveQuestion() const;
    void OnEscape();
    void ResetView();

    // Darstellung
    void ApplyTheme();
    void RecreateFonts();
    void ComputeColumnWidths();
    int  MeasureColumn(size_t c);
    int  MeasureText(std::wstring_view text, bool bold);
    void SyncColumns();
    void UpdateColX();
    void UpdateGutter();
    void UpdateScrollBars();
    void Paint(HDC dc);
    void PaintStatus(HDC dc, const RECT& rc);
    void ShowMessage(const std::wstring& text, bool warn = false, UINT ms = 4000);
    std::wstring PendingHint() const;
    void ReportPercent();

    // Geometrie
    RECT   ClientRect() const;
    int    DataTop() const { return m_rowH; }
    int    DataBottom() const;
    int    VisibleRows() const;
    int    ViewWidth() const;
    int    TotalWidth() const { return m_colX.empty() ? 0 : m_colX.back(); }
    size_t FirstDataRow() const { return (m_headerMode && m_doc.RowCount() > 0) ? 1 : 0; }
    size_t NavCols() const { return m_doc.ColCount() ? m_doc.ColCount() : 1; }
    size_t MaxTop() const;
    bool   CellRect(size_t row, size_t col, RECT& rc) const;
    Hit    HitTest(int x, int y) const;
    bool   IsSelected(size_t row) const
        { return m_selActive && row >= m_selFirst && row <= m_selLast; }

    // Bewegung / Auswahl
    // headerToTop: Steht der Cursor danach auf der Kopfzeile, an den
    // Dateianfang scrollen (Tastatur). Mausklicks lassen die Ansicht stehen.
    void MoveTo(ptrdiff_t row, ptrdiff_t col, bool extend, bool headerToTop = true);
    void PageMove(int dir, bool extend);
    void EnsureVisible(bool headerToTop = true);
    void ScrollToTop(ptrdiff_t top);
    void ScrollX(int x);
    void ClampState();

    // Befehle
    void OnKeyDown(UINT vk, LPARAM lp);
    void ForwardKey(UINT vk);
    void BeginEdit();
    void EndEdit(bool commit, int moveCol = 0);
    void DeleteRows();
    void DeleteColumn();
    void InsertRow();
    size_t InsertPosition() const;
    void Undo();
    void CycleSeparator(int dir);
    void SetSeparator(wchar_t sep);
    void ToggleHeader();
    void SetThemeMode(ThemeMode mode);
    void Zoom(int delta);
    void ShowContextMenu(POINT screenPt);
    void ShowHelp();
    void SetClipboardText(const std::wstring& text);
    int  Ask(const std::wstring& text, UINT flags);

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    HWND m_edit = nullptr;
    bool m_quickView = false;
    bool m_inTempCopy = false;
    bool m_hasFocus = false;

    std::wstring m_path;
    CsvDocument  m_doc;

    ThemeMode m_themeMode = ThemeMode::TotalCommander;
    int       m_showFlags = 0;
    bool      m_tcDark = false;
    Palette   m_pal{};
    HBRUSH    m_editBrush = nullptr;

    HFONT m_font = nullptr, m_fontBold = nullptr;
    int   m_fontPt = 10;
    UINT  m_dpi = 96;
    int   m_rowH = 20, m_padX = 6, m_padY = 3, m_charW = 7, m_lineH = 16;
    int   m_gutterW = 40;
    std::vector<int> m_colW, m_colX;

    bool   m_headerMode = true;
    size_t m_curRow = 0, m_curCol = 0;
    size_t m_top = 0;
    int    m_scrollX = 0;
    bool   m_selActive = false;
    size_t m_anchor = 0, m_selFirst = 0, m_selLast = 0;

    Pending m_pending = Pending::None;
    DWORD   m_pendingTick = 0;

    std::wstring m_message;
    bool         m_messageWarn = false;

    size_t m_editRow = 0, m_editCol = 0;
    UINT   m_editGen = 0;
    Eol    m_editNewline = Eol::LF;
    RECT   m_editRect{};

    bool   m_resizing = false;
    size_t m_resizeCol = 0;
    int    m_resizeStartX = 0, m_resizeStartW = 0;

    int m_wheelAccum = 0, m_hwheelAccum = 0, m_zoomAccum = 0;
    int m_lastPercent = -1;

    static constexpr wchar_t CLASS_NAME[] = L"CsvTCGridView";
};

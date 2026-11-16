/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 *
 * Datenmodell ohne UI: Laden (Kodierung, Trenner), Bearbeiten mit Undo,
 * Speichern. Unveränderte Zeilen werden beim Speichern byte-genau aus dem
 * Originaltext übernommen; nur geänderte Zeilen werden neu serialisiert.
 */
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class TextEncoding : uint8_t { Utf8, Utf8Bom, Utf16LE, Utf16BE, Ansi };
enum class Eol : uint8_t { None, LF, CRLF, CR };

// Größere Dateien überlassen wir dem internen Lister von TC.
#ifdef _WIN64
constexpr unsigned long long kMaxFileBytes = 256ull * 1024 * 1024;
#else
constexpr unsigned long long kMaxFileBytes = 64ull * 1024 * 1024;
#endif

// Reihenfolge für das Durchschalten mit S / Shift+S.
constexpr wchar_t kSeparators[] = { L',', L';', L'\t', L'|' };
constexpr size_t  kSeparatorCount = sizeof(kSeparators) / sizeof(kSeparators[0]);

class CsvDocument {
public:
    // Was ein Undo-Schritt wiederhergestellt hat (für die Cursorposition).
    struct UndoResult {
        enum Kind { None, Edit, Rows, Column, Inserted } kind = None;
        size_t row = 0, col = 0, count = 0;
    };

    // ── Laden ──────────────────────────────────────────────────────────────
    // sep = 0: automatisch erkennen; preferred = bevorzugter Trenner bei Gleichstand.
    bool LoadFile(const std::wstring& path, std::wstring& error,
                  wchar_t sep = 0, wchar_t preferred = L',');
    void LoadBytes(const std::string& bytes, wchar_t sep = 0, wchar_t preferred = L',');
    void LoadText(std::wstring text, wchar_t sep = 0, wchar_t preferred = L',');

    static void    Decode(const std::string& bytes, std::wstring& text,
                          TextEncoding& enc, UINT& ansiCp);
    // false = Text ließ sich nicht verlustfrei kodieren (nur ANSI).
    static bool    Encode(const std::wstring& text, TextEncoding enc, UINT ansiCp,
                          std::string& out);
    static wchar_t DetectSeparator(std::wstring_view text, wchar_t preferred);

    // ── Speichern ──────────────────────────────────────────────────────────
    std::wstring Serialize() const;
    // Zeilen first..last (inklusive) für die Zwischenablage, getrennt mit CRLF.
    std::wstring SerializeRows(size_t first, size_t last) const;
    bool EncodeForSave(std::string& out) const;   // false = verlustbehaftet
    static bool WriteFileAtomic(const std::wstring& path, const std::string& bytes,
                                std::wstring& error);
    bool SaveFile(const std::wstring& path, std::wstring& error); // Encode + Write + MarkSaved
    void MarkSaved();

    // ── Lesen ──────────────────────────────────────────────────────────────
    size_t           RowCount() const { return m_rows.size(); }
    size_t           ColCount() const { return m_colCount; }
    size_t           RowSize(size_t r) const { return r < m_rows.size() ? m_rows[r].cells.size() : 0; }
    std::wstring_view Cell(size_t r, size_t c) const;
    wchar_t          Separator() const { return m_sep; }
    bool             SeparatorDetected() const { return m_sepDetected; }
    TextEncoding     Encoding() const { return m_enc; }
    void             SetEncoding(TextEncoding enc) { m_enc = enc; }
    UINT             AnsiCodePage() const { return m_ansiCp; }
    Eol              LineEnding() const { return m_eol; }
    bool             IsModified() const { return m_forceModified || m_undo.size() != m_savedDepth; }
    bool             CanUndo() const { return !m_undo.empty(); }

    // ── Ändern (jeweils mit Undo) ──────────────────────────────────────────
    bool SetCell(size_t r, size_t c, std::wstring_view text);
    bool DeleteRows(size_t first, size_t last);   // inklusive
    // Leere Zeile vor Index `at` einfügen (at = RowCount() hängt an). Sie bekommt
    // so viele leere Felder, wie die Tabelle Spalten hat.
    bool InsertRow(size_t at);
    bool DeleteColumn(size_t c);
    UndoResult Undo();
    // Neu einlesen mit anderem Trenner. Bei ungespeicherten Änderungen wird der
    // aktuelle Stand (mit altem Trenner) serialisiert und neu zerlegt — es geht
    // nichts verloren. Der Undo-Verlauf wird dabei verworfen.
    void ChangeSeparator(wchar_t sep);

    static const wchar_t* SeparatorName(wchar_t sep);
    static const wchar_t* EncodingName(TextEncoding enc);
    static const wchar_t* EolName(Eol eol);

private:
    enum : uint8_t { kPool = 1, kQuoted = 2, kEdited = 4 };

    struct CellRef {
        uint32_t off = 0;
        uint32_t len = 0;
        uint8_t  flags = 0;
    };

    struct Row {
        uint32_t rawOff = 0;     // Originaltext der Zeile (ohne Zeilenende) in m_raw
        uint32_t rawLen = 0;
        Eol      eol = Eol::None;
        bool     dirty = false;  // true: beim Speichern neu serialisieren
        std::vector<CellRef> cells;
    };

    struct ColEntry { size_t row; CellRef cell; bool oldDirty; };

    struct UndoOp {
        enum Kind : uint8_t { Edit, DelRows, DelCol, InsRow } kind = Edit;
        size_t row = 0, col = 0;
        bool   oldTrailing = false;   // InsRow: Zeilenende am Dateiende vorher
        // Edit
        CellRef oldCell{};
        size_t  oldSize = 0;
        bool    oldDirty = false;
        // DelRows
        std::vector<Row> rows;
        // DelCol
        std::vector<ColEntry> colCells;
    };

    void Parse();
    void RecomputeColCount();
    void PushUndo(UndoOp&& op);
    std::wstring_view View(const CellRef& c) const;
    void AppendRow(std::wstring& out, const Row& row) const;
    static void AppendEol(std::wstring& out, Eol eol);

    std::wstring m_raw;    // dekodierter Dateiinhalt
    std::wstring m_pool;   // entschärfte bzw. bearbeitete Feldinhalte
    std::vector<Row> m_rows;
    size_t       m_colCount = 0;
    wchar_t      m_sep = L',';
    bool         m_sepDetected = false;
    TextEncoding m_enc = TextEncoding::Utf8;
    UINT         m_ansiCp = 1252;
    Eol          m_eol = Eol::CRLF;   // vorherrschendes Zeilenende
    bool         m_trailingEol = false;

    std::vector<UndoOp> m_undo;
    size_t m_savedDepth = 0;
    bool   m_forceModified = false;
};

/**
 * CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
 *
 * Copyright (c) 2026 LordDog
 * Open source — feel free to use, modify, and distribute.
 * Credits are appreciated but not required.
 */
#include "CsvDocument.h"
#include "Lang.h"
#include <algorithm>

using Lang::Tr;

namespace {

std::wstring SystemErrorText(DWORD code)
{
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring s = n && buf ? std::wstring(buf, n) : Tr(L"Error ", L"Fehler ") + std::to_wstring(code);
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s;
}

std::wstring MultiToWide(UINT cp, const char* data, size_t len, DWORD flags, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (len == 0) return {};
    int n = MultiByteToWideChar(cp, flags, data, (int)len, nullptr, 0);
    if (n <= 0) {
        if (ok) *ok = false;
        return {};
    }
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, flags, data, (int)len, w.data(), n);
    return w;
}

// Feld muss in Anführungszeichen, weil es sonst falsch zerlegt würde.
bool NeedsQuoting(std::wstring_view t, wchar_t sep, bool strict)
{
    for (wchar_t ch : t) {
        if (ch == sep || ch == L'\r' || ch == L'\n') return true;
        if (strict && ch == L'"') return true;
    }
    return false;
}

} // namespace

// ── Kodierung ────────────────────────────────────────────────────────────────

void CsvDocument::Decode(const std::string& bytes, std::wstring& text, TextEncoding& enc, UINT& ansiCp)
{
    const auto* b = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();
    ansiCp = GetACP();
    if (ansiCp == CP_UTF8) ansiCp = 1252;

    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
        enc = TextEncoding::Utf8Bom;
        text = MultiToWide(CP_UTF8, bytes.data() + 3, n - 3, 0);
        return;
    }
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) {
        const bool be = b[0] == 0xFE;
        enc = be ? TextEncoding::Utf16BE : TextEncoding::Utf16LE;
        const size_t count = (n - 2) / 2;
        text.resize(count);
        for (size_t i = 0; i < count; ++i) {
            unsigned char lo = b[2 + 2 * i], hi = b[3 + 2 * i];
            text[i] = be ? (wchar_t)((lo << 8) | hi) : (wchar_t)((hi << 8) | lo);
        }
        return;
    }
    bool ok = true;
    text = MultiToWide(CP_UTF8, bytes.data(), n, MB_ERR_INVALID_CHARS, &ok);
    if (ok) {
        enc = TextEncoding::Utf8;
        return;
    }
    enc = TextEncoding::Ansi;
    text = MultiToWide(ansiCp, bytes.data(), n, 0);
}

bool CsvDocument::Encode(const std::wstring& text, TextEncoding enc, UINT ansiCp, std::string& out)
{
    out.clear();
    switch (enc) {
    case TextEncoding::Utf16LE:
    case TextEncoding::Utf16BE: {
        const bool be = enc == TextEncoding::Utf16BE;
        out.resize(2 + text.size() * 2);
        out[0] = be ? '\xFE' : '\xFF';
        out[1] = be ? '\xFF' : '\xFE';
        for (size_t i = 0; i < text.size(); ++i) {
            const unsigned v = text[i];
            out[2 + 2 * i] = (char)(be ? (v >> 8) : (v & 0xFF));
            out[3 + 2 * i] = (char)(be ? (v & 0xFF) : (v >> 8));
        }
        return true;
    }
    case TextEncoding::Utf8:
    case TextEncoding::Utf8Bom: {
        if (enc == TextEncoding::Utf8Bom) out = "\xEF\xBB\xBF";
        if (text.empty()) return true;
        int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        size_t start = out.size();
        out.resize(start + (size_t)std::max(n, 0));
        if (n > 0)
            WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data() + start, n, nullptr, nullptr);
        return true;
    }
    case TextEncoding::Ansi: {
        if (text.empty()) return true;
        BOOL usedDefault = FALSE;
        int n = WideCharToMultiByte(ansiCp, WC_NO_BEST_FIT_CHARS, text.data(), (int)text.size(),
                                    nullptr, 0, nullptr, &usedDefault);
        if (n <= 0) return false;
        out.resize((size_t)n);
        usedDefault = FALSE;
        WideCharToMultiByte(ansiCp, WC_NO_BEST_FIT_CHARS, text.data(), (int)text.size(),
                            out.data(), n, nullptr, &usedDefault);
        return !usedDefault;
    }
    }
    return false;
}

// ── Trenner-Erkennung ────────────────────────────────────────────────────────

wchar_t CsvDocument::DetectSeparator(std::wstring_view text, wchar_t preferred)
{
    // Prioritätsreihenfolge für Gleichstände: Tab ist fast nie zufällig
    // konsistent, danach der bevorzugte (Gebietsschema-)Trenner.
    std::vector<wchar_t> cands = { L'\t' };
    if (preferred && preferred != L'\t' &&
        std::find(std::begin(kSeparators), std::end(kSeparators), preferred) != std::end(kSeparators))
        cands.push_back(preferred);
    for (wchar_t s : { L',', L';', L'|' })
        if (std::find(cands.begin(), cands.end(), s) == cands.end()) cands.push_back(s);

    const size_t limit = std::min<size_t>(text.size(), 256 * 1024);
    const bool   whole = limit == text.size();

    wchar_t best = 0;
    double  bestScore = 0.0;
    for (wchar_t sep : cands) {
        std::vector<size_t> counts;
        size_t count = 0;
        bool inQuotes = false, content = false, stoppedEarly = false;
        for (size_t i = 0; i < limit; ++i) {
            const wchar_t ch = text[i];
            if (ch == L'"') {
                inQuotes = !inQuotes;
                content = true;
            } else if (!inQuotes && (ch == L'\n' || ch == L'\r')) {
                if (ch == L'\r' && i + 1 < limit && text[i + 1] == L'\n') ++i;
                if (content) counts.push_back(count + 1);
                count = 0;
                content = false;
                if (counts.size() >= 200) { stoppedEarly = true; break; }
            } else {
                if (!inQuotes && ch == sep) ++count;
                content = true;
            }
        }
        // Letzten Datensatz nur werten, wenn er nicht abgeschnitten ist.
        if (!stoppedEarly && content && whole) counts.push_back(count + 1);
        if (counts.empty()) continue;

        // Häufigste Feldanzahl und wie viele Datensätze sie haben.
        std::vector<size_t> sorted = counts;
        std::sort(sorted.begin(), sorted.end());
        size_t mode = 0, modeFreq = 0;
        for (size_t i = 0; i < sorted.size();) {
            size_t j = i;
            while (j < sorted.size() && sorted[j] == sorted[i]) ++j;
            if (j - i > modeFreq) { modeFreq = j - i; mode = sorted[i]; }
            i = j;
        }
        if (mode < 2) continue;
        const double score = (double)modeFreq / (double)counts.size();
        if (score > bestScore + 1e-9) {
            bestScore = score;
            best = sep;
        }
    }
    if (best) return best;
    if (std::find(std::begin(kSeparators), std::end(kSeparators), preferred) != std::end(kSeparators))
        return preferred;
    return L',';
}

// ── Laden ────────────────────────────────────────────────────────────────────

bool CsvDocument::LoadFile(const std::wstring& path, std::wstring& error, wchar_t sep, wchar_t preferred)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = SystemErrorText(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        error = SystemErrorText(GetLastError());
        CloseHandle(h);
        return false;
    }
    if ((unsigned long long)size.QuadPart > kMaxFileBytes) {
        error = Tr(L"The file is too large.", L"Die Datei ist zu groß.");
        CloseHandle(h);
        return false;
    }
    std::string bytes((size_t)size.QuadPart, '\0');
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(bytes.size() - done, 16u * 1024 * 1024);
        DWORD got = 0;
        if (!ReadFile(h, bytes.data() + done, chunk, &got, nullptr)) {
            error = SystemErrorText(GetLastError());
            CloseHandle(h);
            return false;
        }
        if (got == 0) break;
        done += got;
    }
    CloseHandle(h);
    bytes.resize(done);
    LoadBytes(bytes, sep, preferred);
    return true;
}

void CsvDocument::LoadBytes(const std::string& bytes, wchar_t sep, wchar_t preferred)
{
    std::wstring text;
    TextEncoding enc = TextEncoding::Utf8;
    UINT cp = 1252;
    Decode(bytes, text, enc, cp);
    LoadText(std::move(text), sep, preferred);
    m_enc = enc;
    m_ansiCp = cp;
}

void CsvDocument::LoadText(std::wstring text, wchar_t sep, wchar_t preferred)
{
    m_raw = std::move(text);
    m_sepDetected = sep == 0;
    m_sep = sep ? sep : DetectSeparator(m_raw, preferred);
    m_enc = TextEncoding::Utf8;
    m_undo.clear();
    m_savedDepth = 0;
    m_forceModified = false;
    Parse();
}

void CsvDocument::Parse()
{
    m_rows.clear();
    m_pool.clear();
    const std::wstring& t = m_raw;
    const size_t n = t.size();
    const wchar_t sep = m_sep;
    size_t pos = 0;
    size_t eolCount[4] = {};

    while (pos < n) {
        Row row;
        row.rawOff = (uint32_t)pos;
        for (;;) {
            CellRef cell;
            if (pos < n && t[pos] == L'"') {
                cell.flags = kQuoted;
                const size_t start = ++pos;
                bool escaped = false;
                size_t close = std::wstring::npos;
                while (pos < n) {
                    size_t q = t.find(L'"', pos);
                    if (q == std::wstring::npos) { pos = n; break; }
                    if (q + 1 < n && t[q + 1] == L'"') { escaped = true; pos = q + 2; continue; }
                    close = q;
                    pos = q + 1;
                    break;
                }
                const size_t contentEnd = close == std::wstring::npos ? n : close;
                // Großzügig: Zeichen nach dem schließenden Anführungszeichen
                // bis zum nächsten Trenner gehören noch zum Feld.
                const size_t junkStart = pos;
                while (pos < n && t[pos] != sep && t[pos] != L'\r' && t[pos] != L'\n') ++pos;
                const bool junk = close != std::wstring::npos && pos > junkStart;
                if (escaped || junk) {
                    cell.flags |= kPool;
                    cell.off = (uint32_t)m_pool.size();
                    for (size_t i = start; i < contentEnd; ++i) {
                        m_pool.push_back(t[i]);
                        if (t[i] == L'"' && i + 1 < contentEnd && t[i + 1] == L'"') ++i;
                    }
                    if (junk) m_pool.append(t, junkStart, pos - junkStart);
                    cell.len = (uint32_t)(m_pool.size() - cell.off);
                } else {
                    cell.off = (uint32_t)start;
                    cell.len = (uint32_t)(contentEnd - start);
                }
            } else {
                const size_t start = pos;
                while (pos < n && t[pos] != sep && t[pos] != L'\r' && t[pos] != L'\n') ++pos;
                cell.off = (uint32_t)start;
                cell.len = (uint32_t)(pos - start);
            }
            row.cells.push_back(cell);
            if (pos < n && t[pos] == sep) {
                ++pos;
                continue;
            }
            break;
        }
        row.rawLen = (uint32_t)(pos - row.rawOff);
        if (pos < n) {
            if (t[pos] == L'\r' && pos + 1 < n && t[pos + 1] == L'\n') { row.eol = Eol::CRLF; pos += 2; }
            else if (t[pos] == L'\r') { row.eol = Eol::CR; ++pos; }
            else if (t[pos] == L'\n') { row.eol = Eol::LF; ++pos; }
        }
        ++eolCount[(int)row.eol];
        m_rows.push_back(std::move(row));
    }

    m_trailingEol = !m_rows.empty() && m_rows.back().eol != Eol::None;
    m_eol = Eol::CRLF;
    size_t bestCount = 0;
    for (Eol e : { Eol::CRLF, Eol::LF, Eol::CR }) {
        if (eolCount[(int)e] > bestCount) { bestCount = eolCount[(int)e]; m_eol = e; }
    }
    RecomputeColCount();
}

void CsvDocument::RecomputeColCount()
{
    size_t c = 0;
    for (const Row& r : m_rows) c = std::max(c, r.cells.size());
    m_colCount = c;
}

// ── Lesen ────────────────────────────────────────────────────────────────────

std::wstring_view CsvDocument::View(const CellRef& c) const
{
    const std::wstring& src = (c.flags & kPool) ? m_pool : m_raw;
    return std::wstring_view(src.data() + c.off, c.len);
}

std::wstring_view CsvDocument::Cell(size_t r, size_t c) const
{
    if (r >= m_rows.size() || c >= m_rows[r].cells.size()) return {};
    return View(m_rows[r].cells[c]);
}

// ── Serialisieren ────────────────────────────────────────────────────────────

void CsvDocument::AppendEol(std::wstring& out, Eol eol)
{
    switch (eol) {
    case Eol::LF:   out += L'\n'; break;
    case Eol::CR:   out += L'\r'; break;
    case Eol::CRLF:
    case Eol::None: out += L"\r\n"; break;
    }
}

void CsvDocument::AppendRow(std::wstring& out, const Row& row) const
{
    if (!row.dirty) {
        out.append(m_raw, row.rawOff, row.rawLen);
        return;
    }
    for (size_t i = 0; i < row.cells.size(); ++i) {
        if (i) out += m_sep;
        const CellRef& c = row.cells[i];
        const std::wstring_view v = View(c);
        const bool quote = (c.flags & kQuoted) || NeedsQuoting(v, m_sep, (c.flags & kEdited) != 0);
        if (!quote) {
            out.append(v);
            continue;
        }
        out += L'"';
        for (wchar_t ch : v) {
            if (ch == L'"') out += L'"';
            out += ch;
        }
        out += L'"';
    }
}

std::wstring CsvDocument::Serialize() const
{
    std::wstring out;
    out.reserve(m_raw.size() + m_raw.size() / 16 + 16);
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        AppendRow(out, row);
        const bool last = i + 1 == m_rows.size();
        if (!last || m_trailingEol) AppendEol(out, row.eol != Eol::None ? row.eol : m_eol);
    }
    return out;
}

std::wstring CsvDocument::SerializeRows(size_t first, size_t last) const
{
    std::wstring out;
    for (size_t i = first; i <= last && i < m_rows.size(); ++i) {
        AppendRow(out, m_rows[i]);
        out += L"\r\n";
    }
    return out;
}

bool CsvDocument::EncodeForSave(std::string& out) const
{
    return Encode(Serialize(), m_enc, m_ansiCp, out);
}

bool CsvDocument::WriteFileAtomic(const std::wstring& path, const std::string& bytes, std::wstring& error)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        error = Tr(L"The file is read-only.", L"Die Datei ist schreibgeschützt.");
        return false;
    }
    const std::wstring tmp = path + L".csvtc~";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = Tr(L"Could not create the temporary file: ", L"Temporäre Datei konnte nicht angelegt werden: ") +
                SystemErrorText(GetLastError());
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < bytes.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(bytes.size() - done, 16u * 1024 * 1024);
        DWORD written = 0;
        if (!WriteFile(h, bytes.data() + done, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        done += written;
    }
    DWORD writeErr = ok ? 0 : GetLastError();
    if (ok && !FlushFileBuffers(h)) { ok = false; writeErr = GetLastError(); }
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        error = Tr(L"Writing failed: ", L"Schreiben fehlgeschlagen: ") + SystemErrorText(writeErr);
        return false;
    }

    BOOL replaced = FALSE;
    if (attrs != INVALID_FILE_ATTRIBUTES)
        replaced = ReplaceFileW(path.c_str(), tmp.c_str(), nullptr,
                                REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
    if (!replaced &&
        !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        error = SystemErrorText(e);
        return false;
    }
    return true;
}

bool CsvDocument::SaveFile(const std::wstring& path, std::wstring& error)
{
    std::string bytes;
    EncodeForSave(bytes);
    if (!WriteFileAtomic(path, bytes, error)) return false;
    MarkSaved();
    return true;
}

void CsvDocument::MarkSaved()
{
    m_savedDepth = m_undo.size();
    m_forceModified = false;
}

// ── Ändern ───────────────────────────────────────────────────────────────────

void CsvDocument::PushUndo(UndoOp&& op)
{
    // Wurde unter den gespeicherten Stand zurückgenommen und dann neu
    // geändert, ist der gespeicherte Stand nicht mehr erreichbar.
    if (m_undo.size() < m_savedDepth) m_savedDepth = (size_t)-1;
    m_undo.push_back(std::move(op));
}

bool CsvDocument::SetCell(size_t r, size_t c, std::wstring_view text)
{
    if (r >= m_rows.size()) return false;
    Row& row = m_rows[r];
    if (c < row.cells.size()) {
        if (View(row.cells[c]) == text) return false;
    } else if (text.empty()) {
        return false;
    }
    if (m_pool.size() + text.size() > 0xFFFFFFF0u) return false;

    UndoOp op;
    op.kind = UndoOp::Edit;
    op.row = r;
    op.col = c;
    op.oldSize = row.cells.size();
    op.oldDirty = row.dirty;
    op.oldCell = c < row.cells.size() ? row.cells[c] : CellRef{};

    if (c >= row.cells.size()) row.cells.resize(c + 1);
    CellRef& cell = row.cells[c];
    const uint8_t quoted = cell.flags & kQuoted;
    cell.off = (uint32_t)m_pool.size();
    cell.len = (uint32_t)text.size();
    cell.flags = (uint8_t)(kPool | kEdited | quoted);
    m_pool.append(text);
    row.dirty = true;
    m_colCount = std::max(m_colCount, row.cells.size());
    PushUndo(std::move(op));
    return true;
}

bool CsvDocument::DeleteRows(size_t first, size_t last)
{
    if (first >= m_rows.size()) return false;
    last = std::min(last, m_rows.size() - 1);
    if (last < first) return false;

    UndoOp op;
    op.kind = UndoOp::DelRows;
    op.row = first;
    op.rows.assign(std::make_move_iterator(m_rows.begin() + first),
                   std::make_move_iterator(m_rows.begin() + last + 1));
    m_rows.erase(m_rows.begin() + first, m_rows.begin() + last + 1);
    RecomputeColCount();
    PushUndo(std::move(op));
    return true;
}

bool CsvDocument::InsertRow(size_t at)
{
    at = std::min(at, m_rows.size());
    UndoOp op;
    op.kind = UndoOp::InsRow;
    op.row = at;
    op.oldTrailing = m_trailingEol;

    Row row;
    row.dirty = true;               // kein Originaltext → immer serialisieren
    row.eol = Eol::None;            // vorherrschendes Zeilenende der Datei
    row.cells.resize(std::max<size_t>(m_colCount, 1));
    // Eine bisher leere Datei bekommt ein Zeilenende am Schluss.
    if (m_rows.empty()) m_trailingEol = true;
    m_rows.insert(m_rows.begin() + at, std::move(row));
    RecomputeColCount();
    PushUndo(std::move(op));
    return true;
}

bool CsvDocument::DeleteColumn(size_t c)
{
    UndoOp op;
    op.kind = UndoOp::DelCol;
    op.col = c;
    for (size_t r = 0; r < m_rows.size(); ++r) {
        Row& row = m_rows[r];
        if (c >= row.cells.size()) continue;
        op.colCells.push_back({ r, row.cells[c], row.dirty });
        row.cells.erase(row.cells.begin() + c);
        row.dirty = true;
    }
    if (op.colCells.empty()) return false;
    RecomputeColCount();
    PushUndo(std::move(op));
    return true;
}

CsvDocument::UndoResult CsvDocument::Undo()
{
    UndoResult res;
    if (m_undo.empty()) return res;
    UndoOp op = std::move(m_undo.back());
    m_undo.pop_back();

    switch (op.kind) {
    case UndoOp::Edit: {
        Row& row = m_rows[op.row];
        row.cells[op.col] = op.oldCell;
        row.cells.resize(op.oldSize);
        row.dirty = op.oldDirty;
        res = { UndoResult::Edit, op.row, op.col, 1 };
        break;
    }
    case UndoOp::DelRows: {
        const size_t count = op.rows.size();
        m_rows.insert(m_rows.begin() + op.row,
                      std::make_move_iterator(op.rows.begin()),
                      std::make_move_iterator(op.rows.end()));
        res = { UndoResult::Rows, op.row, 0, count };
        break;
    }
    case UndoOp::DelCol:
        for (const ColEntry& e : op.colCells) {
            Row& row = m_rows[e.row];
            row.cells.insert(row.cells.begin() + op.col, e.cell);
            row.dirty = e.oldDirty;
        }
        res = { UndoResult::Column, 0, op.col, 1 };
        break;
    case UndoOp::InsRow:
        m_rows.erase(m_rows.begin() + op.row);
        m_trailingEol = op.oldTrailing;
        res = { UndoResult::Inserted, op.row, 0, 1 };
        break;
    }
    RecomputeColCount();
    return res;
}

void CsvDocument::ChangeSeparator(wchar_t sep)
{
    if (sep == m_sep) return;
    const bool modified = IsModified();
    const TextEncoding enc = m_enc;
    const UINT cp = m_ansiCp;
    std::wstring text = modified ? Serialize() : std::move(m_raw);
    LoadText(std::move(text), sep);
    m_enc = enc;
    m_ansiCp = cp;
    m_forceModified = modified;
}

// ── Namen für die Statuszeile ────────────────────────────────────────────────

const wchar_t* CsvDocument::SeparatorName(wchar_t sep)
{
    switch (sep) {
    case L',':  return Tr(L"Comma ,", L"Komma ,");
    case L';':  return Tr(L"Semicolon ;", L"Semikolon ;");
    case L'\t': return Tr(L"Tab", L"Tabulator");
    case L'|':  return Tr(L"Pipe |", L"Strich |");
    default:    return L"?";
    }
}

const wchar_t* CsvDocument::EncodingName(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:    return L"UTF-8";
    case TextEncoding::Utf8Bom: return L"UTF-8 BOM";
    case TextEncoding::Utf16LE: return L"UTF-16 LE";
    case TextEncoding::Utf16BE: return L"UTF-16 BE";
    case TextEncoding::Ansi:    return L"ANSI";
    }
    return L"?";
}

const wchar_t* CsvDocument::EolName(Eol eol)
{
    switch (eol) {
    case Eol::LF:   return L"LF";
    case Eol::CR:   return L"CR";
    case Eol::CRLF:
    case Eol::None: return L"CRLF";
    }
    return L"?";
}

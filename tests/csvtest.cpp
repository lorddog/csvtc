/**
 * CsvTC — Tests für das Datenmodell (ohne UI).
 *
 *   csvtest.exe        → alle Tests, Rückgabe 0 = alles grün
 */
#include "CsvDocument.h"
#include "Lang.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0, g_passed = 0;

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)
void check(bool ok, const char* expr, const char* file, int line)
{
    if (ok) { ++g_passed; return; }
    ++g_failed;
    std::printf("  FEHLER %s:%d: %s\n", file, line, expr);
}

std::string Bytes(const CsvDocument& d)
{
    std::string out;
    d.EncodeForSave(out);
    return out;
}

std::string Utf8(const std::wstring& w)
{
    std::string out;
    CsvDocument::Encode(w, TextEncoding::Utf8, 1252, out);
    return out;
}

// Laden und unverändert wieder kodieren muss byte-identisch sein.
void RoundTrip(const char* name, const std::string& bytes)
{
    CsvDocument d;
    d.LoadBytes(bytes);
    const bool ok = Bytes(d) == bytes;
    if (!ok) std::printf("  Roundtrip '%s' nicht identisch\n", name);
    CHECK(ok);
    CHECK(!d.IsModified());
}

void TestRoundTrips()
{
    std::puts("Roundtrip");
    RoundTrip("crlf", "a,b,c\r\n1,2,3\r\n");
    RoundTrip("lf", "a,b,c\n1,2,3\n");
    RoundTrip("cr", "a,b\r1,2\r");
    RoundTrip("ohne Zeilenende am Schluss", "a;b\n1;2");
    RoundTrip("gemischt", "a,b\r\n1,2\n3,4\r5,6");
    RoundTrip("quotes", "\"a,1\",\"b\"\"x\"\"\",c\r\n\"mehr\r\nzeilig\",2,3\r\n");
    RoundTrip("leere Zeilen", "a,b\n\n1,2\n\n");
    RoundTrip("Trenner am Ende", "a,b,\n1,,\n");
    RoundTrip("unregelmäßig", "a,b,c,d\n1\n1,2\n");
    RoundTrip("unvollständiges Quote", "a,\"offen\nbis zum Ende");
    RoundTrip("Müll nach Quote", "\"ab\"cd,e\n");
    RoundTrip("leer", "");
    RoundTrip("nur Zeilenende", "\r\n");
    RoundTrip("utf8 bom", "\xEF\xBB\xBFName;Stra\xC3\x9F" "e\r\nM\xC3\xBCller;Hauptstr. 1\r\n");
    RoundTrip("utf8", "Name;Ort\nJos\xC3\xA9;K\xC3\xB6ln\n");
    RoundTrip("ansi 1252", "Name;Ort\nM\xFCller;K\xF6ln \x80\n");
    // alle Bytes 0x80–0xFF in ANSI
    std::string ansi = "x;";
    for (int b = 0x80; b <= 0xFF; ++b) ansi += (char)b;
    ansi += "\r\n";
    RoundTrip("ansi alle hohen Bytes", ansi);
    // UTF-16 LE/BE mit BOM
    std::string le = "\xFF\xFE", be = "\xFE\xFF";
    for (char ch : std::string("a;b\r\n\xE4;2\r\n")) {
        le += ch; le += '\0';
        be += '\0'; be += ch;
    }
    RoundTrip("utf16le", le);
    RoundTrip("utf16be", be);
}

void TestDetection()
{
    std::puts("Trenner-Erkennung");
    CHECK(CsvDocument::DetectSeparator(L"a,b,c\n1,2,3\n", L',') == L',');
    CHECK(CsvDocument::DetectSeparator(L"a;b;c\n1;2;3\n", L',') == L';');
    CHECK(CsvDocument::DetectSeparator(L"a\tb\tc\n1\t2\t3\n", L',') == L'\t');
    CHECK(CsvDocument::DetectSeparator(L"a|b|c\n1|2|3\n", L',') == L'|');
    // Dezimalkomma bei Semikolon-Trenner
    CHECK(CsvDocument::DetectSeparator(L"Name;Preis;Menge\nA;1,50;2\nB;2,00;3\n", L',') == L';');
    // Kommas innerhalb von Anführungszeichen zählen nicht
    CHECK(CsvDocument::DetectSeparator(L"\"a,x\";\"b,y\"\n\"1,2\";\"3,4,5\"\n", L',') == L';');
    // Gleichstand: bevorzugter Trenner gewinnt
    CHECK(CsvDocument::DetectSeparator(L"1,5;2,5\n3,5;4,5\n", L';') == L';');
    // Nur eine Spalte: bevorzugter Trenner
    CHECK(CsvDocument::DetectSeparator(L"eins\nzwei\n", L';') == L';');
    CHECK(CsvDocument::DetectSeparator(L"eins\nzwei\n", L'x') == L',');
}

void TestParse()
{
    std::puts("Zerlegen");
    CsvDocument d;
    d.LoadText(L"a,\"b,1\",\"c\"\"q\"\"\"\n\"x\r\ny\",2\n", L',');
    CHECK(d.RowCount() == 2);
    CHECK(d.ColCount() == 3);
    CHECK(d.Cell(0, 0) == L"a");
    CHECK(d.Cell(0, 1) == L"b,1");
    CHECK(d.Cell(0, 2) == L"c\"q\"");
    CHECK(d.Cell(1, 0) == L"x\r\ny");
    CHECK(d.Cell(1, 1) == L"2");
    CHECK(d.Cell(1, 2).empty());
    CHECK(d.RowSize(1) == 2);
    CHECK(d.LineEnding() == Eol::LF);
    CHECK(!d.SeparatorDetected());
}

void TestEdit()
{
    std::puts("Bearbeiten");
    CsvDocument d;
    d.LoadBytes("Name;Wert\r\nA;1\r\n\"B\";2\r\nC;3");
    CHECK(d.Separator() == L';');
    CHECK(!d.SetCell(1, 1, L"1"));                 // gleicher Inhalt = keine Änderung
    CHECK(!d.IsModified());
    CHECK(d.SetCell(1, 1, L"1;5"));                // braucht Quotes
    CHECK(d.SetCell(2, 0, L"Bee"));                // war gequotet, bleibt gequotet
    CHECK(d.SetCell(3, 1, L"sagt \"hi\""));
    CHECK(d.IsModified());
    CHECK(Bytes(d) == "Name;Wert\r\nA;\"1;5\"\r\n\"Bee\";2\r\nC;\"sagt \"\"hi\"\"\"");
    // Undo bis zum Original
    CHECK(d.Undo().kind == CsvDocument::UndoResult::Edit);
    CHECK(d.Undo().kind == CsvDocument::UndoResult::Edit);
    CHECK(d.Undo().kind == CsvDocument::UndoResult::Edit);
    CHECK(!d.IsModified());
    CHECK(Bytes(d) == "Name;Wert\r\nA;1\r\n\"B\";2\r\nC;3");
    CHECK(d.Undo().kind == CsvDocument::UndoResult::None);

    // Zelle hinter dem Zeilenende verlängert nur diese Zeile
    CsvDocument r;
    r.LoadBytes("a,b,c\n1\n");
    CHECK(r.SetCell(1, 2, L"x"));
    CHECK(Bytes(r) == "a,b,c\n1,,x\n");
    r.Undo();
    CHECK(Bytes(r) == "a,b,c\n1\n");

    // Mehrzeiliger Inhalt wird gequotet
    CsvDocument m;
    m.LoadBytes("a,b\n1,2\n");
    m.SetCell(1, 0, L"zwei\nZeilen");
    CHECK(Bytes(m) == "a,b\n\"zwei\nZeilen\",2\n");
}

void TestDelete()
{
    std::puts("Löschen");
    const std::string src = "h1,h2,h3\r\n1,2,3\r\n4,5\r\n6\r\n7,8,9\r\n";
    CsvDocument d;
    d.LoadBytes(src);
    CHECK(d.DeleteRows(1, 2));
    CHECK(d.RowCount() == 3);
    CHECK(Bytes(d) == "h1,h2,h3\r\n6\r\n7,8,9\r\n");
    auto u = d.Undo();
    CHECK(u.kind == CsvDocument::UndoResult::Rows && u.row == 1 && u.count == 2);
    CHECK(Bytes(d) == src);

    // Spalte 2 (Index 1): kurze Zeilen bleiben unangetastet
    CHECK(d.DeleteColumn(1));
    CHECK(d.ColCount() == 2);
    CHECK(Bytes(d) == "h1,h3\r\n1,3\r\n4\r\n6\r\n7,9\r\n");
    u = d.Undo();
    CHECK(u.kind == CsvDocument::UndoResult::Column && u.col == 1);
    CHECK(Bytes(d) == src);
    CHECK(!d.IsModified());
    CHECK(!d.DeleteColumn(7));                     // gibt es nirgends

    // Letzte Zeile ohne Zeilenende löschen
    CsvDocument e;
    e.LoadBytes("a\nb\nc");
    e.DeleteRows(2, 2);
    CHECK(Bytes(e) == "a\nb");
    e.Undo();
    e.DeleteRows(0, 0);
    CHECK(Bytes(e) == "b\nc");
    // alles löschen
    CsvDocument all;
    all.LoadBytes("a\nb\n");
    all.DeleteRows(0, 5);
    CHECK(all.RowCount() == 0);
    CHECK(Bytes(all).empty());
}

void TestInsert()
{
    std::puts("Einfügen");
    const std::string src = "a;b;c\r\n1;2;3\r\n4;5\r\n";
    CsvDocument d;
    d.LoadBytes(src);
    CHECK(d.InsertRow(1));
    CHECK(d.RowCount() == 4);
    CHECK(d.RowSize(1) == 3 && d.Cell(1, 0).empty());
    CHECK(d.IsModified());
    CHECK(Bytes(d) == "a;b;c\r\n;;\r\n1;2;3\r\n4;5\r\n");
    CHECK(d.SetCell(1, 1, L"neu"));
    CHECK(Bytes(d) == "a;b;c\r\n;neu;\r\n1;2;3\r\n4;5\r\n");
    d.Undo();
    auto u = d.Undo();
    CHECK(u.kind == CsvDocument::UndoResult::Inserted && u.row == 1);
    CHECK(Bytes(d) == src);
    CHECK(!d.IsModified());

    // LF-Datei ohne Zeilenende am Schluss: neue Zeile übernimmt LF
    CsvDocument e;
    e.LoadBytes("x,y\nz,w");
    e.InsertRow(1);
    CHECK(Bytes(e) == "x,y\n,\nz,w");
    e.InsertRow(0);
    CHECK(Bytes(e) == ",\nx,y\n,\nz,w");

    // leere Datei
    CsvDocument f;
    f.LoadBytes("");
    CHECK(f.InsertRow(0));
    CHECK(f.RowCount() == 1 && f.ColCount() == 1);
    f.SetCell(0, 0, L"a");
    CHECK(Bytes(f) == "a\r\n");
    f.Undo();
    f.Undo();
    CHECK(f.RowCount() == 0 && Bytes(f).empty() && !f.IsModified());

    // Einfügen, dann Trenner wechseln: Zeile bleibt erhalten
    CsvDocument g;
    g.LoadBytes("a;b\n1;2\n", L',');
    g.InsertRow(1);
    g.ChangeSeparator(L';');
    CHECK(g.RowCount() == 3 && g.ColCount() == 2 && g.Cell(1, 0).empty());
}

void TestModifiedTracking()
{
    std::puts("Änderungsstatus");
    CsvDocument d;
    d.LoadBytes("a,b\n1,2\n");
    d.SetCell(1, 0, L"x");
    d.MarkSaved();
    CHECK(!d.IsModified());
    d.Undo();
    CHECK(d.IsModified());          // unter den gespeicherten Stand zurück
    d.SetCell(1, 1, L"y");          // neuer Zweig: gespeicherter Stand unerreichbar
    CHECK(d.IsModified());
    d.Undo();
    CHECK(d.IsModified());
}

void TestSeparatorChange()
{
    std::puts("Trenner wechseln");
    // falsch erkannt (Komma), richtig wäre Semikolon
    CsvDocument d;
    d.LoadText(L"a;b,x\n1;2,y\n", L',');
    CHECK(d.ColCount() == 2);
    d.ChangeSeparator(L';');
    CHECK(d.ColCount() == 2 && d.Cell(0, 1) == L"b,x");
    CHECK(!d.IsModified());

    // mit ungespeicherter Änderung: bleibt erhalten
    CsvDocument e;
    e.LoadBytes("a;b\n1;2\n", L',');
    CHECK(e.ColCount() == 1);
    e.SetCell(1, 0, L"9;2");
    e.ChangeSeparator(L';');
    CHECK(e.IsModified());
    CHECK(!e.CanUndo());
    CHECK(e.Cell(1, 0) == L"9" && e.Cell(1, 1) == L"2");
    CHECK(Bytes(e) == "a;b\n9;2\n");
}

void TestEncodingLoss()
{
    std::puts("Kodierung");
    CsvDocument d;
    d.LoadBytes("a;b\n\xE4;2\n");                  // ungültiges UTF-8 → ANSI
    CHECK(d.Encoding() == TextEncoding::Ansi);
    CHECK(d.Cell(1, 0) == L"ä");
    d.SetCell(1, 1, L"中");                     // nicht in 1252
    std::string out;
    CHECK(!d.EncodeForSave(out));
    d.SetEncoding(TextEncoding::Utf8Bom);
    CHECK(d.EncodeForSave(out));
    CHECK(out.rfind("\xEF\xBB\xBF", 0) == 0);

    CsvDocument ascii;
    ascii.LoadBytes("a,b\n");
    CHECK(ascii.Encoding() == TextEncoding::Utf8);
}

void TestFileSave()
{
    std::puts("Datei speichern");
    wchar_t dir[MAX_PATH + 1];
    GetTempPathW(MAX_PATH + 1, dir);
    const std::wstring path = std::wstring(dir) + L"csvtc_test_" + std::to_wstring(GetCurrentProcessId()) + L".csv";
    const std::string src = "\xEF\xBB\xBFName;Ort\r\nM\xC3\xBCller;K\xC3\xB6ln\r\n";
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD w = 0;
        WriteFile(h, src.data(), (DWORD)src.size(), &w, nullptr);
        CloseHandle(h);
    }
    CsvDocument d;
    std::wstring err;
    CHECK(d.LoadFile(path, err));
    CHECK(d.Encoding() == TextEncoding::Utf8Bom);
    CHECK(d.SetCell(1, 1, L"Düsseldorf"));
    CHECK(d.SaveFile(path, err));
    CHECK(!d.IsModified());

    CsvDocument back;
    CHECK(back.LoadFile(path, err));
    CHECK(back.Cell(1, 1) == L"Düsseldorf");
    CHECK(Bytes(back) == "\xEF\xBB\xBFName;Ort\r\nM\xC3\xBCller;D\xC3\xBCsseldorf\r\n");
    CHECK(GetFileAttributesW((path + L".csvtc~").c_str()) == INVALID_FILE_ATTRIBUTES);

    // schreibgeschützt → Fehler, Datei unverändert
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
    back.SetCell(1, 0, L"X");
    CHECK(!back.SaveFile(path, err));
    CHECK(!err.empty());
    CHECK(back.IsModified());
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path.c_str());
}

void TestLanguage()
{
    std::puts("Sprache");
    using Lang::Id;
    // auto: Sprachdatei von TC
    CHECK(Lang::Resolve(L"auto", L"wcmd_deu.lng") == Id::German);
    CHECK(Lang::Resolve(L"", L"WCMD_DEU.LNG") == Id::German);
    CHECK(Lang::Resolve(L"auto", L"%COMMANDER_PATH%\\Language\\WCMD_DEU.LNG") == Id::German);
    CHECK(Lang::Resolve(L"auto", L"C:/tc/language/Wcmd_Deu.lng") == Id::German);
    CHECK(Lang::Resolve(L"auto", L"  \"wcmd_deu.lng\" ") == Id::German);
    CHECK(Lang::Resolve(L"auto", L"") == Id::English);            // TC ohne Sprachdatei = englisch
    CHECK(Lang::Resolve(L"auto", L"wcmd_fra.lng") == Id::English);
    CHECK(Lang::Resolve(L"auto", L"wcmd_eng.lng") == Id::English);
    CHECK(Lang::Resolve(L"auto", L"deu.lng") == Id::English);
    CHECK(Lang::Resolve(L"unbekannt", L"wcmd_deu.lng") == Id::German);
    // feste Einstellung schlägt TC
    CHECK(Lang::Resolve(L"en", L"wcmd_deu.lng") == Id::English);
    CHECK(Lang::Resolve(L"EN", L"wcmd_deu.lng") == Id::English);
    CHECK(Lang::Resolve(L"de", L"") == Id::German);
    CHECK(Lang::Resolve(L" De ", L"wcmd_fra.lng") == Id::German);

    // Platzhalter
    CHECK(Lang::Fmt(L"%1 × %2", { L"3", L"4" }) == L"3 × 4");
    CHECK(Lang::Fmt(L"%2 vor %1", { L"a", L"b" }) == L"b vor a");
    CHECK(Lang::Fmt(L"100%% %1", { L"x" }) == L"100% x");
    CHECK(Lang::Fmt(L"%1", { L"%2" }) == L"%2");                 // eingesetzter Text bleibt unverändert
    CHECK(Lang::Fmt(L"%3!", { L"a" }) == L"!");                  // fehlendes Argument = leer
    CHECK(Lang::Fmt(L"Ende %", {}) == L"Ende %");

    // Umschalten wirkt auf Texte des Datenmodells, Standard ist Englisch
    CHECK(Lang::Current() == Id::English);
    CHECK(std::wstring(CsvDocument::SeparatorName(L';')) == L"Semicolon ;");
    Lang::Set(Id::German);
    CHECK(std::wstring(CsvDocument::SeparatorName(L';')) == L"Semikolon ;");
    CHECK(std::wstring(Lang::Tr(L"Save", L"Speichern")) == L"Speichern");
    Lang::Set(Id::English);
    CHECK(std::wstring(Lang::Tr(L"Save", L"Speichern")) == L"Save");
}

} // namespace

int main()
{
    TestRoundTrips();
    TestDetection();
    TestParse();
    TestEdit();
    TestDelete();
    TestInsert();
    TestModifiedTracking();
    TestSeparatorChange();
    TestEncodingLoss();
    TestFileSave();
    TestLanguage();
    std::printf("\n%d bestanden, %d fehlgeschlagen\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}

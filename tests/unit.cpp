// Unit tests for the JSON parser and the i18n formatter.
//
// Run:  g++ -std=c++17 -I<src> -I<generated> tests\unit.cpp src\util\Json.cpp
//       src\util\Str.cpp src\i18n\I18n.cpp src\i18n\Strings.cpp
//       -lsetupapi -lcfgmgr32 ...  (see tools/run-tests.ps1)
#include <windows.h>
#include <stdio.h>

#include <string>

#include "app/AppInfo.h"
#include "util/Json.h"
#include "util/Str.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "core/Device.h"
#include "core/SmartInfo.h"
#include "ui/ConnectionFrame.h"

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const char* what) {
    ++g_checks;
    if (condition) {
        printf("  PASS  %s\n", what);
    } else {
        ++g_failures;
        printf("  FAIL  %s\n", what);
    }
}

static void WriteOut(const char* text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(out, text, (DWORD)strlen(text), &written, nullptr);
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
static void TestJson() {
    WriteOut("JSON parser\n");

    using qe::json::Value;

    // 1. valid document round-trips
    {
        Value root;
        std::wstring error;
        const wchar_t* text =
            L"{\"language\":\"en\",\"runAtStartup\":true,\"count\":3,\"nested\":{\"a\":[1,2]}}";
        const bool ok = Value::Parse(text, root, &error);
        Check(ok, "valid document parses");
        Check(root.GetString(L"language", L"") == L"en", "string field read back");
        Check(root.GetBool(L"runAtStartup", false), "bool field read back");
        Check(root.GetNumber(L"count", 0) == 3, "number field read back");
        Check(root.Find(L"nested") != nullptr, "nested object present");
        Check(root.Dump().find(L"\"language\": \"en\"") != std::wstring::npos,
              "dump contains the field");
    }

    // 2. deeply nested arrays must fail gracefully, not crash the process
    {
        Value root;
        std::wstring error;
        std::wstring deep(200000, L'[');
        const bool ok = Value::Parse(deep, root, &error);
        Check(!ok, "200k-deep array is rejected (no stack overflow)");
    }

    // 3. deeply nested objects
    {
        Value root;
        std::wstring error;
        std::wstring deep;
        for (int i = 0; i < 50000; ++i) deep += L"{\"a\":";
        deep += L"1";
        for (int i = 0; i < 50000; ++i) deep += L"}";
        const bool ok = Value::Parse(deep, root, &error);
        Check(!ok, "50k-deep object is rejected (no stack overflow)");
    }

    // 4. nesting just under the limit still works
    {
        Value root;
        std::wstring error;
        std::wstring deep;
        for (int i = 0; i < 60; ++i) deep += L"[";
        deep += L"1";
        for (int i = 0; i < 60; ++i) deep += L"]";
        const bool ok = Value::Parse(deep, root, &error);
        Check(ok, "60-deep nesting (under the limit) still parses");
    }

    // 5. malformed documents are rejected, not crashed on - and every one of
    //    them says where it failed and why
    {
        const wchar_t* bad[] = {L"", L"{", L"[1,2", L"{\"a\"}", L"tru", L"\"unterminated",
                                L"{\"a\":1,}", L"nul"};
        const char* what[] = {"empty input", "an unterminated object", "an unterminated array",
                              "an object key without a colon", "a truncated literal",
                              "an unterminated string", "a trailing comma", "a truncated null"};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            Value root;
            std::wstring error;
            const bool ok = Value::Parse(bad[i], root, &error);
            Check(!ok, "malformed input rejected");
            Check(!error.empty(),
                  (std::string("a rejected document reports an error: ") + what[i]).c_str());
            Check(error.rfind(L"JSON error at offset ", 0) == 0,
                  (std::string("the error names the offset it stopped at: ") + what[i]).c_str());
        }
    }

    // 6. empty containers
    {
        Value root;
        std::wstring error;
        Check(Value::Parse(L"{}", root, &error) && root.Size() == 0, "empty object");
        Check(Value::Parse(L"[]", root, &error) && root.Size() == 0, "empty array");
    }

    // 7. escapes and unicode
    {
        Value root;
        std::wstring error;
        Check(Value::Parse(L"\"a\\\"b\\\\c\\nd\"", root, &error), "escapes parse");
        Check(root.AsString() == L"a\"b\\c\nd", "escapes decode correctly");
        Check(Value::Parse(L"\"\\u4e2d\\u6587\"", root, &error), "unicode escapes parse");
        Check(root.AsString() == L"中文", "unicode escapes decode correctly");
        // Surrogate pair: U+1F600 (emoji)
        Check(Value::Parse(L"\"\\uD83D\\uDE00\"", root, &error), "surrogate pair parses");
        Check(root.AsString().size() == 2, "surrogate pair yields two UTF-16 units");
        Check(root.AsString() == L"\U0001F600", "surrogate pair decodes to U+1F600");
    }

    // 8. trailing content is an error
    {
        Value root;
        std::wstring error;
        Check(!Value::Parse(L"{} extra", root, &error), "trailing characters rejected");
    }

    // 9. number syntax
    {
        Value root;
        std::wstring error;
        Check(!Value::Parse(L"-", root, &error), "bare minus is rejected");
        Check(!Value::Parse(L"1.", root, &error), "trailing decimal point is rejected");
        Check(!Value::Parse(L"1e", root, &error), "exponent without digits is rejected");
        Check(!Value::Parse(L"1e+", root, &error), "exponent sign without digits is rejected");
        Check(!Value::Parse(L"1e400", root, &error), "exponent overflowing to infinity is rejected");
        Check(Value::Parse(L"0", root, &error) && root.AsNumber(-1) == 0, "zero parses");
        Check(Value::Parse(L"-0", root, &error) && root.AsNumber(1) == 0, "negative zero parses");
        Check(Value::Parse(L"1e10", root, &error) && root.AsNumber(0) == 1e10, "exponent form parses");
        Check(!Value::Parse(L"01", root, &error), "leading zero integer is rejected");
        Check(!Value::Parse(L"-01", root, &error), "negative leading zero integer is rejected");
        Check(!Value::Parse(L"00.5", root, &error), "double leading zero fraction is rejected");
        Check(Value::Parse(L"0.5", root, &error) && root.AsNumber(-1) == 0.5, "fraction after lone zero parses");
        Check(Value::Parse(L"0e1", root, &error) && root.AsNumber(-1) == 0, "exponent after lone zero parses");
    }

    // 10. lone surrogates are rejected, pairs still work
    {
        Value root;
        std::wstring error;
        Check(!Value::Parse(L"\"\\uDC00\"", root, &error), "lone low surrogate is rejected");
        Check(!Value::Parse(L"\"\\uDC00\\uD83D\"", root, &error), "low surrogate followed by high is rejected");
        Check(!Value::Parse(L"\"\\uD800\"", root, &error), "high surrogate without a pair is rejected");
    }

    // 11. raw control characters and malformed \u escapes
    {
        Value root;
        std::wstring error;
        const std::wstring rawCtrl = L"\"a" + std::wstring(1, L'\x01') + L"b\"";
        Check(!Value::Parse(rawCtrl, root, &error), "raw 0x01 byte in a string is rejected");
        Check(!Value::Parse(L"\"a\nb\"", root, &error), "raw newline in a string is rejected");
        Check(Value::Parse(L"\"a\\nb\"", root, &error) && root.AsString() == L"a\nb",
              "escaped newline is still accepted");
        Check(!Value::Parse(L"\"\\u12G4\"", root, &error), "bad hex digit in \\u escape is rejected");
    }

    // 12. the error message surface: every failure names the offset, what was
    //     expected and what was found, and the first failure is the one that
    //     reaches the caller
    {
        Value root;
        std::wstring error;

        // The two-argument form: "expected X, found Y".
        Check(!Value::Parse(L"{\"a\"}", root, &error) &&
                  error == L"JSON error at offset 4: expected ':' after the object key, found '}'",
              "a missing colon names the offset, the expectation and the token");
        Check(!Value::Parse(L"tru", root, &error) &&
                  error == L"JSON error at offset 0: expected 'true' or 'false', found 'tru'",
              "a truncated literal reports the token it stopped at");
        Check(!Value::Parse(L"[1,2", root, &error) &&
                  error == L"JSON error at offset 4: expected ',' or ']', found end of input",
              "an unterminated array reports the end of input as what it found");

        // The single-argument form has no "found" part.
        Check(!Value::Parse(L"\"unterminated", root, &error) &&
                  error == L"JSON error at offset 13: a closing '\"'",
              "an unterminated string reports only what it expected");
        Check(error.find(L"expected") == std::wstring::npos,
              "the single-argument failure does not claim anything was found");

        // The offset is where the parser stopped, and the preview is the token
        // sitting there.
        Check(!Value::Parse(L"{} extra", root, &error) &&
                  error == L"JSON error at offset 3: expected end of input, found 'extra'",
              "trailing content names the token that follows");
        Check(!Value::Parse(L"", root, &error) &&
                  error == L"JSON error at offset 0: expected a JSON value, found end of input",
              "empty input reports the end of input at offset 0");
        Check(!Value::Parse(L"\"\\u12G4\"", root, &error) &&
                  error == L"JSON error at offset 5: expected a hex digit, found 'G'",
              "a bad hex digit names the character it choked on");

        // The nesting guard keeps its own message.
        const std::wstring deep(200, L'[');
        Check(!Value::Parse(deep, root, &error) &&
                  error.find(L"nested at most 64 levels deep") != std::wstring::npos,
              "the nesting guard reports the depth limit it enforced");
        Check(error.rfind(L"JSON error at offset 64: ", 0) == 0,
              "the depth error names the offset of the bracket that was too deep");

        // The first failure is the one reported: a nested failure must not be
        // overwritten by the outer "expected a JSON value".
        Check(!Value::Parse(L"{\"a\": tru}", root, &error) &&
                  error.find(L"'true' or 'false'") != std::wstring::npos &&
                  error.find(L"a JSON value") == std::wstring::npos,
              "the innermost failure is the one that reaches the caller");

        // A successful parse leaves the caller's string alone.
        error = L"stale message from an earlier failure";
        Check(Value::Parse(L"{\"a\":1}", root, &error) &&
                  error == L"stale message from an earlier failure",
              "a successful parse does not touch the error string");

        // A caller that passes no error out-param must not crash.
        Check(Value::Parse(L"{\"a\":1}", root, nullptr), "a null error out-param is accepted");
        Check(!Value::Parse(L"{", root, nullptr), "a null error out-param still reports failure");
    }

    // 13. Size() counts the children of a container and Length() counts the
    //     characters of a string. Mixing the two used to let a non-empty JSON
    //     string answer "is this array non-empty?" with yes.
    {
        Value root;
        std::wstring error;
        Check(Value::Parse(L"\"abcd\"", root, &error) && root.IsString() && root.Size() == 0 &&
                  root.Length() == 4,
              "a string reports Size() 0 and Length() = number of characters");
        Check(Value::Parse(L"\"\"", root, &error) && root.Size() == 0 && root.Length() == 0,
              "an empty string reports Size() 0 and Length() 0");
        Check(Value::Parse(L"[1,2,3]", root, &error) && root.IsArray() && root.Size() == 3 &&
                  root.Length() == 0,
              "an array reports Size() = element count and Length() 0");
        Check(Value::Parse(L"{\"a\":1,\"b\":2}", root, &error) && root.IsObject() && root.Size() == 2 &&
                  root.Length() == 0,
              "an object reports Size() = key count and Length() 0");
        Check(Value::Parse(L"42", root, &error) && root.Size() == 0 && root.Length() == 0,
              "a number reports Size() 0 and Length() 0");
    }
}

// ---------------------------------------------------------------------------
// i18n formatter
// ---------------------------------------------------------------------------
static void TestI18n() {
    WriteOut("i18n formatter\n");

    qe::i18n::I18n::Instance().Init(L"en");

    // The bug this guards: an argument containing a later placeholder used to
    // be substituted a second time, because each placeholder was replaced in a
    // separate pass over the whole string.
    const std::wstring nasty = L"disk%2 and %3";
    const std::wstring result = qe::i18n::I18n::Instance().Format(
        qe::i18n::StrId::ConfirmEjectText, nasty, L"SECOND", L"THIRD");

    Check(result.find(nasty) != std::wstring::npos,
          "argument text survives verbatim (no re-substitution)");
    Check(result.find(L"SECOND") == std::wstring::npos,
          "second argument is NOT injected into the first argument's text");

    // Placeholders that do not exist in the template stay untouched.
    const std::wstring plain =
        qe::i18n::I18n::Instance().Format(qe::i18n::StrId::CliVersion, L"X", L"Y", L"Z");
    Check(plain.find(L"usb-switch " QUICKEJECT_VERSION_STRING_W) != std::wstring::npos, "template without placeholders");
    Check(plain.find(L'X') == std::wstring::npos, "unused argument is not appended");

    // A lone % or an unknown placeholder is left alone.
    const std::wstring lone =
        qe::i18n::I18n::Instance().Format(qe::i18n::StrId::CliUnknownDevice, L"100%", L"", L"");
    Check(lone.find(L"100%") != std::wstring::npos, "lone percent sign is preserved");

    // Chinese path
    qe::i18n::I18n::Instance().Init(L"zh");
    const std::wstring zh = qe::i18n::I18n::Instance().Format(
        qe::i18n::StrId::ConfirmEjectText, L"我的U盘", L"", L"");
    Check(zh.find(L"我的U盘") != std::wstring::npos, "Chinese argument substituted");
    qe::i18n::I18n::Instance().Init(L"en");
}

// ---------------------------------------------------------------------------
// JSON file I/O: atomic save path and the load failure surface
// ---------------------------------------------------------------------------
static void DeleteQuiet(const std::wstring& path) { DeleteFileW(path.c_str()); }

// Every non-directory entry of the directory that holds `path`, narrowed by
// `pattern` ("*" for all of them). Used to prove a failed save left nothing
// behind besides the file that was already there.
static std::vector<std::wstring> EntriesBeside(const std::wstring& path, const wchar_t* pattern) {
    std::vector<std::wstring> names;
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return names;
    const std::wstring search = path.substr(0, slash + 1) + pattern;
    WIN32_FIND_DATAW found{};
    HANDLE handle = FindFirstFileW(search.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) return names;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        names.push_back(found.cFileName);
    } while (FindNextFileW(handle, &found) != FALSE);
    FindClose(handle);
    return names;
}

static std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// The temporary names this process could own: <file>.<pid>.<tid>.<tick>.<n>.tmp.
// A live process can never share our PID and no other thread of ours can share
// our thread id, so anything with our pair was written by this process or by a
// predecessor of it. Another process's temporaries are deliberately left alone
// by RemoveStaleTemporaries (src/util/Json.cpp), so they are not residue.
static std::vector<std::wstring> OwnTemporaries(const std::wstring& path) {
    const std::wstring prefix = FileNameOf(path) + L"." + std::to_wstring(GetCurrentProcessId()) +
                                L"." + std::to_wstring(GetCurrentThreadId()) + L".";
    std::vector<std::wstring> mine;
    for (const std::wstring& name : EntriesBeside(path, L"*.tmp")) {
        if (name.rfind(prefix, 0) == 0) mine.push_back(name);
    }
    return mine;
}

static void TestJsonFile() {
    WriteOut("JSON file I/O\n");

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring base = std::wstring(tmp) + L"qe-unit-" + std::to_wstring(GetTickCount64()) + L"-";

    // A nested path exercises parent-directory creation, the UTF-8 BOM and the
    // temp-file-then-rename write path.
    const std::wstring nested = base + L"a\\b\\settings.json";
    qe::json::Value root = qe::json::Value::Object();
    root.Set(L"language", qe::json::Value::String(L"zh"));
    root.Set(L"runAtStartup", qe::json::Value::Bool(true));
    root.Set(L"count", qe::json::Value::Number(7));
    std::wstring error;
    Check(qe::json::SaveFile(nested, root, &error), "SaveFile creates parent dirs and writes the file");
    qe::json::Value back;
    Check(qe::json::LoadFile(nested, back, &error), "LoadFile reads the saved file back");
    Check(back.GetString(L"language", L"") == L"zh", "saved string round-trips");
    Check(back.GetBool(L"runAtStartup", false), "saved bool round-trips");
    Check(back.GetNumber(L"count", 0) == 7, "saved number round-trips");

    // CreateOnly must refuse to clobber an existing file, but write a new one.
    Check(!qe::json::SaveFile(nested, root, &error, qe::json::SaveMode::CreateOnly),
          "SaveFile(CreateOnly) refuses to overwrite an existing file");
    const std::wstring fresh = base + L"fresh.json";
    Check(qe::json::SaveFile(fresh, root, &error, qe::json::SaveMode::CreateOnly),
          "SaveFile(CreateOnly) writes a brand-new file");

    // Failure surface: missing file, non-JSON content and an empty file.
    Check(!qe::json::LoadFile(base + L"missing.json", back, &error),
          "LoadFile reports a missing file as an error");
    const std::wstring garbage = base + L"garbage.json";
    {
        HANDLE h = CreateFileW(garbage.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char text[] = "this is not json";
            DWORD written = 0;
            WriteFile(h, text, sizeof(text) - 1, &written, nullptr);
            CloseHandle(h);
        }
    }
    Check(!qe::json::LoadFile(garbage, back, &error), "LoadFile rejects non-JSON content");
    const std::wstring emptyFile = base + L"empty.json";
    {
        HANDLE h = CreateFileW(emptyFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    Check(!qe::json::LoadFile(emptyFile, back, &error), "LoadFile rejects an empty file");

    // Each scenario below gets a directory of its own, so "what is left in the
    // directory" has an exact answer.
    auto freshDir = [](const wchar_t* tag) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        return std::wstring(tmp) + L"qe-unit-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(GetTickCount64()) + L"-" + tag + L"\\";
    };

    {   // Two saves from one thread inside a single tick. The temporary name is
        // built from the tick, and GetTickCount64 only advances every ~16 ms, so
        // every save in that window competes for the same base name; the second
        // one must still succeed, whatever the first one left behind.
        const std::wstring dir = freshDir(L"twice");
        const std::wstring target = dir + L"settings.json";
        bool bothSavesOk = true;
        bool insideOneTick = false;
        for (int attempt = 0; attempt < 128 && !insideOneTick; ++attempt) {
            qe::json::Value first = qe::json::Value::Object();
            first.Set(L"count", qe::json::Value::Number(7));
            qe::json::Value second = qe::json::Value::Object();
            second.Set(L"count", qe::json::Value::Number(8));
            const DWORD before = GetTickCount64();
            bothSavesOk = bothSavesOk && qe::json::SaveFile(target, first, &error) &&
                          qe::json::SaveFile(target, second, &error);
            insideOneTick = (GetTickCount64() == before);
        }
        Check(bothSavesOk, "two consecutive saves from one thread both succeed");
        Check(insideOneTick, "both saves were observed inside a single GetTickCount64 tick");
        Check(OwnTemporaries(target).empty(), "neither of the two saves left a temporary behind");
        qe::json::Value twice;
        Check(qe::json::LoadFile(target, twice, &error) && twice.GetNumber(L"count", 0) == 8,
              "the second save's contents are the ones that survive");
        DeleteQuiet(target);
        RemoveDirectoryW(dir.c_str());
    }

    {   // CreateOnly on an existing target: the write is abandoned before the
        // rename, and the temporary it used must go with it - one leftover per
        // refused launch would otherwise pile up in the settings directory.
        const std::wstring dir = freshDir(L"createonly");
        const std::wstring target = dir + L"settings.json";
        Check(qe::json::SaveFile(target, root, &error), "the CreateOnly target is written first");
        Check(!qe::json::SaveFile(target, root, &error, qe::json::SaveMode::CreateOnly),
              "SaveFile(CreateOnly) refuses an existing target");
        Check(OwnTemporaries(target).empty(), "a refused CreateOnly save leaves no temporary behind");
        qe::json::Value untouched;
        Check(qe::json::LoadFile(target, untouched, &error) &&
                  untouched.GetNumber(L"count", 0) == 7,
              "a refused CreateOnly save leaves the existing file untouched");
        DeleteQuiet(target);
        RemoveDirectoryW(dir.c_str());
    }

    {   // A predecessor that died between creating its temporary and renaming it
        // leaves a file whose name the next save picks first. Plant one for every
        // tick this save can land on, so the retry path runs whatever
        // GetTickCount64 says when SaveFile is called.
        const std::wstring dir = freshDir(L"stale");
        const std::wstring target = dir + L"settings.json";
        Check(qe::json::SaveFile(target, root, &error), "the target exists before the crash is simulated");
        const DWORD now = GetTickCount64();
        int planted = 0;
        for (DWORD tick = now; tick <= now + 32; ++tick) {
            const std::wstring stale = qe::str::Format(L"%ls.%lu.%lu.%lu.0.tmp", target.c_str(),
                                                      GetCurrentProcessId(), GetCurrentThreadId(), tick);
            HANDLE handle = CreateFileW(stale.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
                ++planted;
            }
        }
        Check(planted == 33, "a crashed predecessor's temporaries were planted");
        Check(qe::json::SaveFile(target, root, &error),
              "a save succeeds despite a crashed predecessor's temporary");
        Check(OwnTemporaries(target).empty(), "the crashed predecessor's temporaries are cleaned up");
        DeleteQuiet(target);
        RemoveDirectoryW(dir.c_str());
    }

    {   // A save that cannot finish - the target is held open by another
        // process, so the rename is refused - must fail without destroying the
        // file already there and without leaving its temporary behind.
        const std::wstring dir = freshDir(L"locked");
        const std::wstring target = dir + L"settings.json";
        qe::json::Value original = qe::json::Value::Object();
        original.Set(L"language", qe::json::Value::String(L"en"));
        original.Set(L"count", qe::json::Value::Number(1));
        Check(qe::json::SaveFile(target, original, &error), "the locked target is written first");
        HANDLE locked = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(locked != INVALID_HANDLE_VALUE, "the target is held open exclusively");
        Check(!qe::json::SaveFile(target, root, &error), "a save fails when the target cannot be replaced");
        Check(OwnTemporaries(target).empty(), "the failed save leaves no temporary behind");
        const std::vector<std::wstring> left = EntriesBeside(target, L"*");
        Check(left.size() == 1 && left[0] == FileNameOf(target),
              "the failed save leaves only the original file in the directory");
        CloseHandle(locked);
        qe::json::Value survived;
        Check(qe::json::LoadFile(target, survived, &error) &&
                  survived.GetString(L"language", L"") == L"en" && survived.GetNumber(L"count", 0) == 1,
              "the failed save left the original contents intact");
        DeleteQuiet(target);
        RemoveDirectoryW(dir.c_str());
    }

    DeleteQuiet(nested);
    RemoveDirectoryW((base + L"a\\b").c_str());
    RemoveDirectoryW((base + L"a").c_str());
    DeleteQuiet(fresh);
    DeleteQuiet(garbage);
    DeleteQuiet(emptyFile);
    RemoveDirectoryW(base.c_str());
}

// ---------------------------------------------------------------------------
// str helpers: UTF-8/UTF-16 conversion and Win32 error rendering
// ---------------------------------------------------------------------------
static void TestStrHelpers() {
    WriteOut("str helpers: UTF conversion and Win32 messages\n");

    Check(qe::str::FromUtf8("hello") == L"hello", "ASCII UTF-8 decodes to UTF-16");
    Check(qe::str::FromUtf8("\xE4\xB8\xAD\xE6\x96\x87") == L"\u4e2d\u6587", "multi-byte UTF-8 decodes");
    Check(qe::str::ToUtf8(L"\u4e2d\u6587") == "\xE4\xB8\xAD\xE6\x96\x87", "UTF-16 encodes to multi-byte UTF-8");
    Check(qe::str::ToUtf8(L"hello") == "hello", "ASCII UTF-16 encodes to ASCII UTF-8");

    const std::wstring invalid = qe::str::FromUtf8("\xFF\xFE");
    Check(!invalid.empty(), "invalid UTF-8 still yields text (lossy fallback)");
    Check(invalid.find(L'\uFFFD') != std::wstring::npos, "invalid UTF-8 becomes U+FFFD");
    Check(!qe::str::FromUtf8("\xE4\xB8").empty(), "a truncated UTF-8 sequence is not emptied");

    Check(qe::str::Win32ErrorMessage(0).empty(), "Win32ErrorMessage(0) is empty");
    Check(!qe::str::Win32ErrorMessage(ERROR_FILE_NOT_FOUND).empty(),
          "Win32ErrorMessage renders a known system error");
}

// ---------------------------------------------------------------------------
// str::Format / FormatV: the 8 KiB truncation guard
// ---------------------------------------------------------------------------
static void TestStrFormat() {
    WriteOut("str::Format truncation\n");

    Check(qe::str::Format(L"%d and %ls", 42, L"text") == L"42 and text", "Format substitutes arguments");
    Check(qe::str::Format(L"plain").empty() == false, "Format returns plain text unchanged");
    Check(qe::str::Format(L"").empty(), "Format of an empty format string is empty");

    const std::wstring under(8000, L'A');
    Check(qe::str::Format(L"%ls", under.c_str()) == under, "output just under the cap is intact");

    const std::wstring over(20000, L'B');
    const std::wstring capped = qe::str::Format(L"%ls", over.c_str());
    Check(capped.size() <= 8 * 1024, "oversized output is capped at 8 KiB");
    Check(capped.size() < over.size(), "oversized output is actually truncated, not fatal");
}

// ---------------------------------------------------------------------------
// ConnectionPixels: the header-only tray animation rasteriser
// ---------------------------------------------------------------------------
static void TestConnectionPixels() {
    WriteOut("ConnectionPixels (tray animation)\n");

    const int size = 16;
    const auto insert = qe::ConnectionPixels(size, true, 1.0);
    const auto withdraw = qe::ConnectionPixels(size, false, 1.0);

    Check(insert.size() == static_cast<size_t>(size) * size, "pixel buffer is size*size");
    Check((insert.front() >> 24) == 0, "the far corner is fully transparent");
    Check((insert[(size / 2) * size + size / 2] >> 24) == 255, "the centre pixel is fully opaque");

    // Alpha encodes supersampled coverage only, so it must not depend on the
    // direction or the animation progress - only the colours may.
    bool alphaMatches = insert.size() == withdraw.size();
    for (size_t i = 0; alphaMatches && i < insert.size(); ++i)
        alphaMatches = (insert[i] >> 24) == (withdraw[i] >> 24);
    Check(alphaMatches, "the alpha channel is independent of insert/withdraw direction");
    Check(insert != withdraw, "inserting and withdrawing frames differ in colour");
    Check(qe::ConnectionPixels(size, true, 1.0) == insert, "rendering is deterministic");
}

// ---------------------------------------------------------------------------
// SmartInfo: attribute decoding boundaries
// ---------------------------------------------------------------------------
static void TestSmartBoundaries() {
    WriteOut("SMART attribute decoding\n");

    // Lay one 12-byte ATA attribute at `index` in a 512-byte payload.
    auto setAttr = [](uint8_t* sector, int index, uint8_t id, uint8_t current, uint64_t raw) {
        uint8_t* a = sector + 2 + index * 12;
        a[0] = id;
        a[3] = current;
        for (int b = 0; b < 6; ++b) a[5 + b] = static_cast<uint8_t>(raw >> (8 * b));
    };

    {   // Raw low byte out of range -> the "current" value is used instead.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 194, 30, 200);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(snap.hasTemperature && snap.temperatureC == 30,
              "temperature falls back to the current value when the raw byte is implausible");
    }
    {   // Neither raw nor current plausible -> the field stays unset.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 194, 255, 255);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(!snap.hasTemperature, "temperature with no plausible value stays unset");
    }
    {   // 190 is the other temperature attribute, and its raw low byte is just
        // as untrustworthy, so the same fallback and the same refusal apply.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 190, 38, 200);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(snap.hasTemperature && snap.temperatureC == 38,
              "attribute 190 falls back to the current value when the raw byte is implausible");
        setAttr(sector, 0, 190, 255, 200);
        qe::SmartSnapshot neither;
        qe::ParseSmartAttributes(sector, sizeof(sector), neither);
        Check(!neither.hasTemperature, "attribute 190 with no plausible value stays unset");
    }
    {   // The ATA attribute table holds 30 entries; everything past them is
        // vendor data. An attribute at offset 2 + 30*12 still sits inside a
        // 512-byte payload, so only the 30-entry cap keeps it from being decoded.
        uint8_t sector[512] = {};
        setAttr(sector, 30, 194, 0, 41);
        qe::SmartSnapshot past;
        qe::ParseSmartAttributes(sector, sizeof(sector), past);
        Check(!past.hasTemperature, "an attribute past the 30-entry cap is not decoded");
        setAttr(sector, 29, 194, 0, 41);
        qe::SmartSnapshot last;
        qe::ParseSmartAttributes(sector, sizeof(sector), last);
        Check(last.hasTemperature && last.temperatureC == 41,
              "the last slot inside the cap (index 29) still decodes");
    }
    {   // Attribute id 0 is padding and must be skipped.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 0, 30, 41);
        setAttr(sector, 1, 194, 0, 41);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(snap.hasTemperature && snap.temperatureC == 41,
              "attribute id 0 is skipped and the following attribute still decodes");
    }
    {   // The full 6-byte raw counter is decoded.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 9, 0, 0x0102030405ull);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(snap.hasPowerOnHours && snap.powerOnHours == 0x0102030405ull,
              "power-on hours decode the full 6-byte raw counter");
    }
    {   // The first attribute of a kind wins; later duplicates are ignored.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 9, 0, 100);
        setAttr(sector, 1, 9, 0, 200);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(snap.powerOnHours == 100, "the first power-on-hours attribute wins");
    }
    {   // All supported ids decode; 190 is a temperature attribute too.
        uint8_t sector[512] = {};
        setAttr(sector, 0, 190, 0, 45);
        setAttr(sector, 1, 9, 0, 1000);
        setAttr(sector, 2, 12, 0, 50);
        setAttr(sector, 3, 5, 0, 3);
        setAttr(sector, 4, 197, 0, 7);
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(sector, sizeof(sector), snap);
        Check(snap.hasTemperature && snap.temperatureC == 45, "attribute 190 decodes temperature");
        Check(snap.hasPowerOnHours && snap.powerOnHours == 1000, "power-on hours decode");
        Check(snap.hasPowerCycles && snap.powerCycles == 50, "power cycles decode");
        Check(snap.hasReallocated && snap.reallocated == 3, "reallocated sectors decode");
        Check(snap.hasPending && snap.pending == 7, "pending sectors decode");
    }
    {   // A buffer too small to hold even one attribute is ignored.
        uint8_t tiny[10] = {};
        qe::SmartSnapshot snap;
        qe::ParseSmartAttributes(tiny, sizeof(tiny), snap);
        Check(!snap.hasTemperature && !snap.hasPowerOnHours && !snap.hasPending,
              "a buffer smaller than one attribute is ignored");
    }
}

int main() {
    TestJson();
    TestI18n();
    TestJsonFile();
    TestStrHelpers();
    TestStrFormat();
    TestConnectionPixels();
    TestSmartBoundaries();
    Check(qe::str::IsDriveLetterMount(L"E:\\"), "drive root is a drive-letter mount");
    Check(qe::str::IsDriveLetterMount(L"e:\\"), "lowercase drive root is accepted");
    Check(!qe::str::IsDriveLetterMount(L"C:\\mnt\\usb\\"), "directory mount is not its host drive");
    Check(!qe::str::IsDriveLetterMount(L"\\\\?\\Volume{123}\\"), "volume GUID is not a drive letter");
    Check(!qe::str::IsDriveLetterMount(L"1:\\"), "invalid drive letter is rejected");
    qe::DeviceList grouped(3);
    grouped[0].readerId = L"reader";
    grouped[0].readerName = L"Reader";
    grouped[1].instanceId = L"stick";
    grouped[2].readerId = L"reader";
    const auto groups = qe::GroupReaders(grouped);
    Check(groups.size() == 2 && groups[0].id == L"reader" && groups[0].indexes.size() == 2 &&
              groups[0].indexes[0] == 0 && groups[0].indexes[1] == 2 && groups[1].indexes.size() == 1 &&
              groups[1].indexes[0] == 1,
          "card-reader slots group together and a single disk stays on its own");
    qe::Device alone;
    alone.readerId = L"only-slot";
    const auto single = qe::GroupReaders(qe::DeviceList{alone});
    Check(single.size() == 1 && single[0].id.empty(), "one slot is not wrapped in a reader submenu");
    uint8_t sector[512] = {};
    sector[2] = 194;
    sector[7] = 41;
    sector[14] = 9;
    sector[19] = 10;
    sector[20] = 1;
    qe::SmartSnapshot snap;
    qe::ParseSmartAttributes(sector, sizeof(sector), snap);
    Check(snap.hasTemperature && snap.temperatureC == 41, "SMART temperature attribute is decoded");
    Check(snap.hasPowerOnHours && snap.powerOnHours == 266, "SMART power-on hours are decoded");

    Check(qe::str::FileSizeString(0) == L"0 B", "zero bytes shows as 0 B");
    Check(qe::str::FileSizeString(512) == L"512 B", "sub-kilobyte size stays in bytes");
    Check(qe::str::FileSizeString(1048064) == L"1023.5 KB", "kilobytes keep their decimal below the carry");
    Check(qe::str::FileSizeString(1048575) == L"1 MB", "a size rounding up to the unit boundary carries over");
    Check(qe::str::FileSizeString(1048576) == L"1 MB", "exact unit boundary shows without decimals");
    Check(qe::str::FileSizeString(512ull * 1024 * 1024) == L"512 MB", "megabytes drop the trailing .0");
    Check(qe::str::FileSizeString(18446744073709551615ull) == L"16384 PB", "the largest size caps at the last unit");
    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

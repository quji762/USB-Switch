// Unit tests for the JSON parser and the i18n formatter.
//
// Run:  g++ -std=c++17 -I<src> -I<generated> tests\unit.cpp src\util\Json.cpp
//       src\util\Str.cpp src\i18n\I18n.cpp src\i18n\Strings.cpp
//       -lsetupapi -lcfgmgr32 ...  (see tools/run-tests.ps1)
#include <windows.h>
#include <stdio.h>

#include "app/AppInfo.h"
#include "util/Json.h"
#include "util/Str.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"

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

    // 5. malformed documents are rejected, not crashed on
    {
        const wchar_t* bad[] = {L"", L"{", L"[1,2", L"{\"a\"}", L"tru", L"\"unterminated",
                                L"{\"a\":1,}", L"nul"};
        for (const wchar_t* text : bad) {
            Value root;
            std::wstring error;
            const bool ok = Value::Parse(text, root, &error);
            Check(!ok, "malformed input rejected");
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
    }

    // 8. trailing content is an error
    {
        Value root;
        std::wstring error;
        Check(!Value::Parse(L"{} extra", root, &error), "trailing characters rejected");
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
        qe::i18n::I18n::Instance().Format(qe::i18n::StrId::ErrorGeneric, L"100%", L"", L"");
    Check(lone.find(L"100%") != std::wstring::npos, "lone percent sign is preserved");

    // Chinese path
    qe::i18n::I18n::Instance().Init(L"zh");
    const std::wstring zh = qe::i18n::I18n::Instance().Format(
        qe::i18n::StrId::ConfirmEjectText, L"我的U盘", L"", L"");
    Check(zh.find(L"我的U盘") != std::wstring::npos, "Chinese argument substituted");
    qe::i18n::I18n::Instance().Init(L"en");
}

int main() {
    TestJson();
    TestI18n();
    Check(qe::str::IsDriveLetterMount(L"E:\\"), "drive root is a drive-letter mount");
    Check(qe::str::IsDriveLetterMount(L"e:\\"), "lowercase drive root is accepted");
    Check(!qe::str::IsDriveLetterMount(L"C:\\mnt\\usb\\"), "directory mount is not its host drive");
    Check(!qe::str::IsDriveLetterMount(L"\\\\?\\Volume{123}\\"), "volume GUID is not a drive letter");
    Check(!qe::str::IsDriveLetterMount(L"1:\\"), "invalid drive letter is rejected");
    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

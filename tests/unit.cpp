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
#include "core/Device.h"
#include "core/SmartInfo.h"

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

int main() {
    TestJson();
    TestI18n();
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

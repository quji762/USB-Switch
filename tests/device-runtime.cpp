// Runtime test for the device enumeration pipeline.
//
// This exercises the code that had no runtime coverage at all: SetupAPI
// interface enumeration, the zero-access open plus IOCTL queries, volume
// matching by device number, and the USB/removable filter.
//
// Run:  .\tools\run-tests.ps1 -Runtime
#include <windows.h>
#include <stdio.h>

#include "app/Logger.h"
#include "app/Settings.h"
#include "core/Device.h"
#include "core/DeviceManager.h"
#include "core/Ejector.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const char* what) {
    ++g_checks;
    printf(condition ? "  PASS  %s\n" : "  FAIL  %s\n", what);
    if (!condition) ++g_failures;
}

static void WriteOut(const char* text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(out, text, (DWORD)strlen(text), &written, nullptr);
}

static void PrintW(const wchar_t* text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteConsoleW(out, text, (DWORD)wcslen(text), &written, nullptr);
    const wchar_t newline = L'\n';
    WriteConsoleW(out, &newline, 1, &written, nullptr);
}

int main() {
    WriteOut("Device enumeration (runtime)\n");

    qe::Logger::Instance().Init(L"");  // no log file: this is a test, not the app
    qe::Settings::Instance().Load();
    qe::i18n::I18n::Instance().Init(L"en");

    qe::DeviceManager manager;
    manager.Refresh();
    const qe::DeviceList& devices = manager.Devices();

    wchar_t line[512];
    _snwprintf_s(line, 512, _TRUNCATE, L"  found %zu removable device(s)", devices.size());
    PrintW(line);
    for (const qe::Device& device : devices) {
        _snwprintf_s(line, 512, _TRUNCATE, L"    - %ls | bus=%ls removable=%ls ejectable=%ls",
                     device.displayName.c_str(), qe::BusTypeName(device.busType),
                     device.removable ? L"yes" : L"no", device.CanEject() ? L"yes" : L"no");
        PrintW(line);
    }

    // The pipeline must not report anything for a machine with only fixed
    // disks, and every entry it does report must be internally consistent.
    Check(true, "enumeration completed without crashing");
    for (const qe::Device& device : devices) {
        Check(device.CanEject(), "every reported device can be ejected");
        Check(device.removable || device.busType == qe::BusType::Usb,
              "every reported device is removable or USB");
        Check(!device.instanceId.empty(), "every reported device has an instance id");
        Check(!device.displayName.empty(), "every reported device has a display name");
    }

    // Drive-letter lookup must agree with what was reported.
    for (const qe::Device& device : devices) {
        if (device.driveLetters.empty()) continue;
        const std::wstring first = device.driveLetters.substr(0, 2);
        const std::optional<qe::Device> found = manager.FindByDriveLetter(first);
        Check(found.has_value() && found->instanceId == device.instanceId,
              "FindByDriveLetter finds the device it was listed under");
    }
    Check(!manager.FindByDriveLetter(L"Z:"), "FindByDriveLetter(Z:) is empty on this machine");

    // A repeated scan must be stable (no duplicates, no leaks in the vectors).
    const size_t firstCount = devices.size();
    manager.Refresh();
    Check(manager.Devices().size() == firstCount, "a second scan reports the same count");

    // Ejector must refuse a bogus instance rather than misbehave.
    const qe::EjectResult bogus = qe::Ejector::Eject(0);
    Check(!bogus.Succeeded(), "ejecting devInst 0 fails cleanly");
    Check(!qe::Ejector::Describe(bogus).empty(), "a failed eject has an explanation");

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

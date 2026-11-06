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
#include "app/AutoStart.h"
#include "app/Settings.h"
#include "core/Device.h"
#include "core/DeviceManager.h"
#include "core/Ejector.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"

static int g_failures = 0;
static int g_checks = 0;

enum class Fault { None, ClassDevs, FirstVolume, NextVolume };
static Fault fault = Fault::None;

static HDEVINFO WINAPI TestGetClassDevs(const GUID* guid, PCWSTR enumerator, HWND parent, DWORD flags) {
    if (fault == Fault::ClassDevs) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return SetupDiGetClassDevsW(guid, enumerator, parent, flags);
}
static HANDLE WINAPI TestFindFirstVolume(LPWSTR volume, DWORD length) {
    if (fault == Fault::FirstVolume) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return FindFirstVolumeW(volume, length);
}
static BOOL WINAPI TestFindNextVolume(HANDLE handle, LPWSTR volume, DWORD length) {
    if (fault == Fault::NextVolume) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return FindNextVolumeW(handle, volume, length);
}

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
    Check(manager.Refresh(), "device enumeration succeeds");
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
    Check(!manager.FindByDriveLetter(L"?:"), "an invalid drive letter does not match a device");

    // A repeated scan must be stable (no duplicates, no leaks in the vectors).
    const size_t firstCount = devices.size();
    Check(manager.Refresh(), "second enumeration succeeds");
    Check(manager.Devices().size() == firstCount, "a second scan reports the same count");

    qe::DeviceManager::EnumerationApi api;
    api.getClassDevs = TestGetClassDevs;
    api.findFirstVolume = TestFindFirstVolume;
    api.findNextVolume = TestFindNextVolume;
    qe::DeviceManager faultManager(api);
    int callbacks = 0;
    faultManager.SetChangeCallback([&callbacks]() { ++callbacks; });
    Check(faultManager.Refresh(), "baseline enumeration for fault tests succeeds");
    const auto baseline = faultManager.Devices();
    for (const auto mode : {Fault::ClassDevs, Fault::FirstVolume, Fault::NextVolume}) {
        fault = mode;
        Check(!faultManager.Refresh(), "system enumeration error is reported");
        const auto after = faultManager.Devices();
        bool same = after.size() == baseline.size();
        for (size_t i = 0; same && i < baseline.size(); ++i) {
            same = after[i].instanceId == baseline[i].instanceId &&
                   after[i].displayName == baseline[i].displayName &&
                   after[i].driveLetters == baseline[i].driveLetters;
        }
        Check(same, "failed enumeration retains previous snapshot");
        Check(callbacks == 1, "failed enumeration does not send a change callback");
    }
    fault = Fault::None;
    Check(faultManager.Refresh() && callbacks == 2, "enumeration recovers after a transient error");

    // Ejector must refuse a bogus instance rather than misbehave.
    const qe::EjectResult bogus = qe::Ejector::Eject(L"USB-Switch\\NONEXISTENT\\TEST");
    Check(!bogus.Succeeded(), "ejecting a nonexistent instance fails cleanly");
    Check(!qe::Ejector::Describe(bogus).empty(), "a failed eject has an explanation");

    const std::wstring testRunName = L"USB-Switch-test-" + std::to_wstring(GetCurrentProcessId());
    using Startup = qe::AutoStart;
    const std::wstring owned = L"\"C:\\test-owned.exe\"";
    const std::wstring other = L"\"C:\\different.exe\"";
    if (Startup::Inspect(testRunName, owned) != Startup::Entry::Missing) {
        Check(false, "temporary startup entry name is unused");
    } else if (Startup::SetEnabled(testRunName, owned)) {
        Check(Startup::Inspect(testRunName, owned) == Startup::Entry::Owned &&
              Startup::Inspect(testRunName, other) == Startup::Entry::Other, "ownership reflects actual command");
        Check(!Startup::SetEnabled(testRunName, other), "foreign startup command cannot be overwritten");
        Check(Startup::ClearIfMatches(testRunName, other) &&
              Startup::Inspect(testRunName, owned) == Startup::Entry::Owned, "foreign startup command cannot be removed");
        Check(Startup::ClearIfMatches(testRunName, L"\"C:\\TEST-OWNED.exe\"") &&
              Startup::Inspect(testRunName, owned) == Startup::Entry::Missing, "matching startup entry is removed");
    } else {
        Check(false, "temporary startup entry created");
    }

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

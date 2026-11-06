// Runtime test for the device enumeration pipeline.
//
// This exercises the code that had no runtime coverage at all: SetupAPI
// interface enumeration, the zero-access open plus IOCTL queries, volume
// matching by device number, and the USB/removable filter.
//
// Run:  .\tools\run-tests.ps1 -Runtime
#include <windows.h>
#include <stdio.h>
#include <filesystem>
#include <fstream>

#include "app/Logger.h"
#include "app/AutoStart.h"
#include "app/Settings.h"
#include "core/Device.h"
#include "core/DeviceJson.h"
#include "core/DeviceManager.h"
#include "core/Ejector.h"
#include "core/EjectJson.h"
#include "core/RemovalTarget.h"
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

namespace topology {
constexpr DEVINST disk = 10, transport = 20;
DEVINST firstChild = disk;
CONFIGRET siblingStatus = CR_NO_SUCH_DEVNODE;
std::wstring service = L"USBSTOR";
DWORD caps = CM_DEVCAP_REMOVABLE;

CONFIGRET WINAPI Parent(PDEVINST node, DEVINST device, ULONG) {
    *node = transport;
    return device == disk ? CR_SUCCESS : CR_FAILURE;
}
CONFIGRET WINAPI Child(PDEVINST node, DEVINST device, ULONG) {
    *node = firstChild;
    return device == transport ? CR_SUCCESS : CR_FAILURE;
}
CONFIGRET WINAPI Sibling(PDEVINST node, DEVINST device, ULONG) {
    *node = 11;
    return device == disk ? siblingStatus : CR_FAILURE;
}
CONFIGRET WINAPI Property(DEVINST device, ULONG prop, PULONG type, PVOID buffer, PULONG bytes, ULONG) {
    if (device != transport) return CR_FAILURE;
    if (prop == CM_DRP_SERVICE) {
        ULONG size = static_cast<ULONG>((service.size() + 1) * sizeof(wchar_t));
        if (*bytes < size) return CR_BUFFER_SMALL;
        memcpy(buffer, service.c_str(), size);
        *type = REG_SZ; *bytes = size;
    } else if (prop == CM_DRP_CAPABILITIES && *bytes >= sizeof(caps)) {
        memcpy(buffer, &caps, sizeof(caps));
        *type = REG_DWORD; *bytes = sizeof(caps);
    } else return CR_FAILURE;
    return CR_SUCCESS;
}
void Verify() {
    const qe::detail::RemovalTopologyApi api{Parent, Child, Sibling, Property};
    auto target = [&] { return qe::detail::RemovalTarget(disk, api); };
    Check(target() == transport, "single USBSTOR disk selects removable transport");
    service = L"UASPStor";
    Check(target() == transport, "single UASP disk selects removable transport");
    siblingStatus = CR_SUCCESS;
    Check(target() == disk, "second slot prevents transport ejection");
    siblingStatus = CR_ACCESS_DENIED;
    Check(target() == disk, "failed sibling lookup cannot promote request");
    siblingStatus = CR_NO_SUCH_DEVNODE;
    firstChild = 11;
    Check(target() == disk, "changed first child prevents transport ejection");
    firstChild = disk;
    caps = 0;
    Check(target() == disk, "nonremovable transport is never selected");
    caps = CM_DEVCAP_REMOVABLE;
    service = L"USBHUB3";
    Check(target() == disk, "USB hub is never selected as storage transport");
    service.assign(L"USBSTOR\0bad", 11);
    Check(target() == disk, "malformed service identity cannot promote request");
}
}  // namespace topology

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
    topology::Verify();
    WriteOut("Device enumeration (runtime)\n");

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        (L"USB-Switch-log-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(root)) return 1;
    const auto log = root / L"rotation.log";
    { std::ofstream seed(log, std::ios::binary); seed.seekp((1 << 20) - 2); seed.put('x'); }
    qe::Logger::Instance().Init(root.wstring(), L"rotation.log");
    QE_LOG_INFO(L"cross rotation threshold");
    QE_LOG_INFO(L"first line after rotation");
    qe::Logger::Instance().Shutdown();
    const fs::path backup = root / L"rotation.log.old";
    Check(fs::exists(backup) && fs::file_size(backup) >= (1 << 20) &&
          fs::file_size(log) > 0 && fs::file_size(log) < 1024, "log rotates before restart and continues writing");
    fs::remove(backup); fs::remove(log);

    const auto settingsPath = (root / L"settings.json").wstring();
    const auto original = qe::json::Value::String(L"original");
    const auto changed = qe::json::Value::String(L"changed");
    qe::json::Value loaded;
    auto isValue = [&](const wchar_t* expected) {
        return qe::json::LoadFile(settingsPath, loaded, nullptr) && loaded.AsString() == expected;
    };
    Check(qe::json::SaveFile(settingsPath, original, nullptr, qe::json::SaveMode::CreateOnly),
          "migration can create absent settings");
    Check(!qe::json::SaveFile(settingsPath, changed, nullptr, qe::json::SaveMode::CreateOnly) &&
          isValue(L"original"), "late migration cannot replace settings saved by another process");
    HANDLE held = CreateFileW(settingsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    Check(held != INVALID_HANDLE_VALUE && !qe::json::SaveFile(settingsPath, changed) &&
          isValue(L"original"), "failed replacement leaves the previous settings readable");
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    Check(qe::json::SaveFile(settingsPath, changed) && isValue(L"changed"),
          "settings save succeeds after file lock is released");
    fs::remove(settingsPath);
    Check(fs::is_empty(root), "failed saves clean up their own temporary files");
    fs::remove(root);
    qe::Logger::Instance().Init(L"");  // no log file: this is a test, not the app
    qe::Settings::Instance().Load();
    qe::i18n::I18n::Instance().Init(L"en");

    qe::Device folderOnly;
    folderOnly.mountPaths = {L"C:\\Mounts\\USB\\"};
    folderOnly.instanceId = L"test-folder";
    qe::Device host;
    host.instanceId = L"test-host";
    host.mountPaths = {L"C:\\"};
    qe::DeviceList snapshot{host, folderOnly};
    const auto matched = qe::FindByMountPath(snapshot, L"c:/Mounts/USB");
    Check(matched && matched->instanceId == folderOnly.instanceId,
          "exact folder mount selects its device instead of the host");
    Check(!qe::FindByMountPath(snapshot, L"C:\\Mounts\\USB\\file.txt") &&
          !qe::FindByMountPath(snapshot, L"C:\\Mounts") &&
          !qe::FindByMountPath(snapshot, L"Mounts\\USB"),
          "unlisted paths do not fall back to a host disk");
    snapshot[0].mountPaths.push_back(folderOnly.mountPaths[0]);
    Check(!qe::FindByMountPath(snapshot, folderOnly.mountPaths[0]),
          "conflicting mount identities cannot select a device");
    const auto folderJson = qe::DeviceJson(folderOnly);
    const auto* folderPaths = folderJson.Find(L"mountPaths");
    Check(folderPaths && folderPaths->Size() == 1 &&
          folderPaths->Items()[0].AsString() == folderOnly.mountPaths[0] &&
          folderJson.Find(L"driveLetters")->Size() == 0,
          "folder-mounted device exports its mount path without a host drive letter");

    const auto windowsDisks = qe::WindowsVolumeDisks();
    Check(!windowsDisks.empty(), "live Windows volume resolves to backing disk numbers");
    qe::DeviceManager::EnumerationApi missingSystem;
    missingSystem.windowsDisks = [] { return std::vector<DWORD>{}; };
    qe::DeviceManager unverifiable(missingSystem);
    Check(!unverifiable.Refresh(), "scan refuses to publish targets when Windows disk identity is unavailable");
    qe::DeviceManager manager;
    // Exercise populated output even on a runner with no USB drives attached.
    qe::Device sample;
    sample.volumeLabel = L"资料 \"A&B\"";
    sample.driveLetters = L"E:, F:";
    sample.sizeBytes = UINT64_MAX;
    qe::json::Value parsed;
    Check(qe::json::Value::Parse(qe::DeviceJson(sample).Dump(), parsed) &&
          parsed.GetString(L"volumeLabel", L"") == sample.volumeLabel &&
          parsed.GetString(L"mountedVolumeBytes", L"") == L"18446744073709551615",
          "JSON inventory preserves quoted Unicode labels and exact byte counts");
    const auto* drives = parsed.Find(L"driveLetters");
    Check(drives && drives->Size() == 2 && drives->Items()[0].AsString() == L"E:" &&
          drives->Items()[1].AsString() == L"F:", "JSON inventory splits multiple drive letters");
    const auto unmounted = qe::DeviceJson(qe::Device{});
    Check(unmounted.Find(L"driveLetters")->Size() == 0 && unmounted.Find(L"mountedVolumeBytes")->IsNull(),
          "JSON inventory represents absent mounts and unknown capacity");
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

    qe::EjectResult refusal;
    refusal.status = qe::EjectStatus::Vetoed;
    refusal.vetoType = PNP_VetoDevice;
    Check(refusal.CanRetry(), "device veto after closing files offers a manual retry");
    refusal.vetoApplication = L"STORAGE\\Volume";
    const auto veto = qe::EjectJson(refusal, L"test-id");
    Check(veto.GetString(L"status", L"") == L"vetoed" && veto.GetBool(L"retryable", false) &&
          veto.GetString(L"vetoName", L"") == refusal.vetoApplication && veto.GetNumber(L"vetoType", -1) == PNP_VetoDevice,
          "structured eject response preserves device veto and retry eligibility");
    refusal.vetoType = static_cast<PNP_VETO_TYPE>(12); // PNP_VetoInsufficientRights
    Check(!refusal.CanRetry(), "insufficient rights do not suggest a transient busy device");
    refusal.status = qe::EjectStatus::Failed;
    refusal.vetoType = PNP_VetoTypeUnknown;
    Check(!refusal.CanRetry(), "failed device lookup does not offer retry");
    // Ejector must refuse a bogus instance rather than misbehave.
    const qe::EjectResult bogus = qe::Ejector::Eject(L"USB-Switch\\NONEXISTENT\\TEST");
    Check(bogus.status == qe::EjectStatus::Ineligible, "unlisted instance is rejected before the PnP eject request");
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

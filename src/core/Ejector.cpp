#include "core/Ejector.h"
#include "core/DeviceManager.h"
#include "core/RemovalTarget.h"
#include "core/VetoEvents.h"

#include <iterator>
#include <setupapi.h>
#include <devguid.h>
#include <devpkey.h>
#include <cfg.h>
#include "util/Win32.h"

#include "app/Logger.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "util/Str.h"

// GUID_DEVCLASS_DISKDRIVE {4D36E967-E325-11CE-BFC1-08002BE10318}. devguid.h
// only declares it and expects the toolchain's uuid library to supply the
// symbol: MinGW happens to get that from libuuid.a, MSVC would need uuid.lib,
// which this project does not link. Defining it once here keeps the class check
// in RestartStopped independent of which library a toolchain drags in.
EXTERN_C const GUID GUID_DEVCLASS_DISKDRIVE =
    {0x4D36E967, 0xE325, 0x11CE, {0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18}};

namespace qe {
namespace {

// PNP_VETO_TYPE values 8..13, spelled out so the veto switch below does not
// depend on how complete a given cfg.h / cfgmgr32.h revision is. They are plain
// enum constants (not macros), so an #ifdef guard would never work - they are
// defined unconditionally with the values from cfg.h:
//   8  PNP_VetoIllegalDeviceRequest   9  PNP_VetoInsufficientPower
//   10 PNP_VetoNonDisableable         11 PNP_VetoLegacyDriver
//   12 PNP_VetoInsufficientRights     13 PNP_VetoAlreadyRemoved
// When a future SDK exports them these definitions simply collide harmlessly
// with the same values, so no guard is needed either way.
constexpr PNP_VETO_TYPE kVetoIllegalDeviceRequest = static_cast<PNP_VETO_TYPE>(8);
constexpr PNP_VETO_TYPE kVetoInsufficientPower = static_cast<PNP_VETO_TYPE>(9);
constexpr PNP_VETO_TYPE kVetoNonDisableable = static_cast<PNP_VETO_TYPE>(10);
constexpr PNP_VETO_TYPE kVetoLegacyDriver = static_cast<PNP_VETO_TYPE>(11);
constexpr PNP_VETO_TYPE kVetoInsufficientRights = static_cast<PNP_VETO_TYPE>(12);
constexpr PNP_VETO_TYPE kVetoAlreadyRemoved = static_cast<PNP_VETO_TYPE>(13);

unsigned long ConfigRetToWin32Error(CONFIGRET configRet) {
    switch (configRet) {
        case CR_SUCCESS: return ERROR_SUCCESS;
        case CR_INVALID_POINTER: return ERROR_INVALID_PARAMETER;
        case CR_INVALID_DATA:
        case CR_INVALID_DEVNODE: return ERROR_INVALID_DATA;
        case CR_NO_SUCH_DEVNODE:
        case CR_NO_SUCH_VALUE: return ERROR_NOT_FOUND;
        case CR_ACCESS_DENIED: return ERROR_ACCESS_DENIED;
        default: return ERROR_GEN_FAILURE;
    }
}

}  // namespace

EjectResult Ejector::Eject(const std::wstring& instanceId) {
    EjectResult result;
    if (instanceId.empty()) {
        result.status = EjectStatus::Failed;
        result.win32Error = ERROR_INVALID_PARAMETER;
        return result;
    }

    // Confirmation and retry dialogs can outlive the original scan. Recheck
    // eligibility before every request, even for callers holding an instance ID.
    // A failed rescan is not the same as a gone device: one may clear on its
    // own, the other makes the target genuinely un-ejectable.
    DeviceManager current;
    if (!current.Refresh()) {
        result.status = EjectStatus::ScanUnavailable;
        QE_LOG_WARN(L"eject aborted: the device list is temporarily unavailable");
        return result;
    }
    if (!current.FindByInstanceId(instanceId)) {
        result.status = EjectStatus::Ineligible;
        return result;
    }
    DEVINST devInst = 0;
    std::wstring mutableId = instanceId;  // CM_Locate_DevNodeW takes DEVINSTID_W.
    const CONFIGRET locateRet = CM_Locate_DevNodeW(&devInst, mutableId.data(), CM_LOCATE_DEVNODE_NORMAL);
    if (locateRet != CR_SUCCESS || devInst == 0) {
        result.configRet = locateRet;
        result.win32Error = locateRet == CR_SUCCESS ? ERROR_NOT_FOUND : ConfigRetToWin32Error(locateRet);
        QE_LOG_WARN(L"device is no longer present (%ls, CONFIGRET 0x%02lX)", instanceId.c_str(), locateRet);
        return result;
    }

    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    WCHAR vetoName[MAX_PATH] = L"";
    const DEVINST target = detail::RemovalTarget(devInst);
    WCHAR targetId[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(target, targetId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) targetId[0] = 0;
    FILETIME start{};
    GetSystemTimePreciseAsFileTime(&start);
    result.requestedAt = start;
    const CONFIGRET configRet = CM_Request_Device_EjectW(
        target, &vetoType, vetoName, static_cast<ULONG>(std::size(vetoName)), 0);

    result.configRet = configRet;
    result.vetoType = vetoType;
    result.vetoApplication = vetoName;

    if (configRet == CR_SUCCESS) {
        result.status = EjectStatus::Success;
        result.stoppedInstanceId = targetId;
        return result;
    }
    if (configRet == CR_REMOVE_VETOED) {
        result.status = EjectStatus::Vetoed;
        FILETIME end{};
        GetSystemTimePreciseAsFileTime(&end);
        result.reportedProcesses = ReadVetoEvents(instanceId, targetId, start, end);
        QE_LOG_WARN(L"eject vetoed (type %d, app \"%ls\")", static_cast<int>(vetoType), vetoName);
        return result;
    }

    result.status = EjectStatus::Failed;
    result.win32Error = ConfigRetToWin32Error(configRet);
    QE_LOG_ERROR(L"CM_Request_Device_Eject failed: CONFIGRET 0x%02lX (Win32 %lu)", configRet,
                 result.win32Error);
    return result;
}

bool Ejector::HeldForEject(const std::wstring& targetId) {
    auto id = targetId;
    DEVINST node{};
    ULONG status{}, problem{};
    return !id.empty() && CM_Locate_DevNodeW(&node, id.data(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
           CM_Get_DevNode_Status(&status, &problem, node, 0) == CR_SUCCESS && (status & DN_HAS_PROBLEM) &&
           problem == CM_PROB_HELD_FOR_EJECT;
}

DWORD Ejector::RestartStopped(const std::wstring& diskId, const std::wstring& targetId) {
    if (diskId.empty() || targetId.empty()) return ERROR_INVALID_PARAMETER;
    DeviceManager live;
    if (!live.Refresh()) return ERROR_GEN_FAILURE;
    if (live.FindByInstanceId(diskId)) return ERROR_ALREADY_EXISTS;
    DevInfoSet info(SetupDiCreateDeviceInfoList(nullptr, nullptr));
    if (!info) return GetLastError();
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    if (!SetupDiOpenDeviceInfoW(info.Get(), targetId.c_str(), nullptr, 0, &data)) return GetLastError();
    // Arguments cross a process boundary on elevation. Verify the recorded disk
    // and the only other target we can have ejected: its USB storage parent.
    SP_DEVINFO_DATA disk{};
    disk.cbSize = sizeof(disk);
    if (!SetupDiOpenDeviceInfoW(info.Get(), diskId.c_str(), nullptr, 0, &disk)) return GetLastError();
    if (!IsEqualGUID(disk.ClassGuid, GUID_DEVCLASS_DISKDRIVE)) return ERROR_INVALID_DATA;
    if (disk.DevInst != data.DevInst) {
        WCHAR parent[MAX_DEVICE_ID_LEN]{}, service[64]{};
        DEVPROPTYPE type{};
        DWORD bytes{}, regType{};
        if (!SetupDiGetDevicePropertyW(info.Get(), &disk, &DEVPKEY_Device_Parent, &type,
                reinterpret_cast<BYTE*>(parent), sizeof(parent), &bytes, 0) ||
            type != DEVPROP_TYPE_STRING || bytes < sizeof(WCHAR) || bytes > sizeof(parent) ||
            bytes % sizeof(WCHAR) || parent[bytes / sizeof(WCHAR) - 1] ||
            !str::EqualsI(parent, targetId)) return ERROR_DEVICE_NOT_CONNECTED;
        if (!SetupDiGetDeviceRegistryPropertyW(info.Get(), &data, SPDRP_SERVICE, &regType,
                reinterpret_cast<BYTE*>(service), sizeof(service), &bytes) ||
            regType != REG_SZ || bytes < sizeof(WCHAR) || bytes > sizeof(service) ||
            bytes % sizeof(WCHAR) || service[bytes / sizeof(WCHAR) - 1] ||
            (!str::EqualsI(service, L"USBSTOR") && !str::EqualsI(service, L"UASPStor")))
            return ERROR_NOT_SUPPORTED;
    }
    SP_PROPCHANGE_PARAMS change{};
    change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    change.StateChange = DICS_PROPCHANGE;
    change.Scope = DICS_FLAG_CONFIGSPECIFIC;
    if (!SetupDiSetClassInstallParamsW(info.Get(), &data, &change.ClassInstallHeader, sizeof(change))) return GetLastError();
    ULONG status{}, problem{};
    if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) != CR_SUCCESS ||
        !(status & DN_HAS_PROBLEM) || problem != CM_PROB_HELD_FOR_EJECT) return ERROR_DEVICE_NOT_CONNECTED;
    if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, info.Get(), &data)) return GetLastError();
    SP_DEVINSTALL_PARAMS_W install{};
    install.cbSize = sizeof(install);
    if (!SetupDiGetDeviceInstallParamsW(info.Get(), &data, &install)) return GetLastError();
    return install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART) ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}

std::wstring Ejector::Describe(const EjectResult& result) {
    const auto& strings = i18n::I18n::Instance();

    if (result.status == EjectStatus::ScanUnavailable)
        return strings.Get(i18n::StrId::ErrorEnumFailed);
    if (result.status == EjectStatus::Ineligible) return strings.Get(i18n::StrId::EjectIneligible);
    if (result.status == EjectStatus::Vetoed) {
        std::wstring reason;
        i18n::StrId suffixId = i18n::StrId::VetoUnknown;
        bool hasSuffix = false;

        switch (result.vetoType) {
            case PNP_VetoLegacyDevice:
                reason = strings.Get(i18n::StrId::VetoLegacyDevice);
                suffixId = i18n::StrId::VetoDevicePathSuffix;
                hasSuffix = true;
                break;
            case PNP_VetoPendingClose:
                reason = strings.Get(i18n::StrId::VetoPendingClose);
                suffixId = i18n::StrId::VetoDevicePathSuffix;
                hasSuffix = true;
                break;
            case PNP_VetoWindowsApp:
                reason = strings.Get(i18n::StrId::VetoWindowsApp);
                suffixId = i18n::StrId::VetoAppSuffix;
                hasSuffix = true;
                break;
            case PNP_VetoWindowsService:
                reason = strings.Get(i18n::StrId::VetoWindowsService);
                suffixId = i18n::StrId::VetoAppSuffix;
                hasSuffix = true;
                break;
            case PNP_VetoOutstandingOpen:
                reason = strings.Get(i18n::StrId::VetoOutstandingOpen);
                suffixId = i18n::StrId::VetoDevicePathSuffix;
                hasSuffix = true;
                break;
            case PNP_VetoDevice:
                reason = strings.Get(i18n::StrId::VetoDevice);
                suffixId = i18n::StrId::VetoDevicePathSuffix;
                hasSuffix = true;
                break;
            case PNP_VetoDriver:
                reason = strings.Get(i18n::StrId::VetoDriver);
                suffixId = i18n::StrId::VetoDriverSuffix;
                hasSuffix = true;
                break;
            case kVetoIllegalDeviceRequest:
                reason = strings.Get(i18n::StrId::VetoIllegalDeviceRequest);
                suffixId = i18n::StrId::VetoDevicePathSuffix;
                hasSuffix = true;
                break;
            case kVetoInsufficientPower:
                reason = strings.Get(i18n::StrId::VetoInsufficientPower);
                break;  // no accompanying text for this type
            case kVetoNonDisableable:
                reason = strings.Get(i18n::StrId::VetoNonDisableable);
                suffixId = i18n::StrId::VetoDevicePathSuffix;
                hasSuffix = true;
                break;
            case kVetoLegacyDriver:
                reason = strings.Get(i18n::StrId::VetoLegacyDriver);
                suffixId = i18n::StrId::VetoDriverSuffix;
                hasSuffix = true;
                break;
            case kVetoInsufficientRights:
                reason = strings.Get(i18n::StrId::VetoInsufficientRights);
                break;  // no accompanying text for this type
            case kVetoAlreadyRemoved:
                reason = strings.Get(i18n::StrId::VetoAlreadyRemoved);
                break;
            case PNP_VetoTypeUnknown:
            default:
                reason = strings.Get(i18n::StrId::VetoUnknown);
                break;
        }

        // What PnP puts in the name buffer depends on the veto type: only
        // PNP_VetoWindowsApp / PNP_VetoWindowsService / PNP_VetoDriver /
        // PNP_VetoLegacyDriver report an application, service or driver name
        // (PNP_VetoDriver is the plain one, hence the driver suffix above).
        // The other types with a suffix report a device instance path, and the
        // ones without a suffix report nothing at all - labelling all of them
        // "used by" would misdescribe a device path.
        if (hasSuffix && !result.vetoApplication.empty()) {
            reason += L"\n\n";
            reason += strings.Format(suffixId, result.vetoApplication);
        }
        if (result.reportedProcesses.empty()) {
            reason += L"\n\n";
            reason += strings.Get(i18n::StrId::NoEventProcess);
        } else {
            reason += L"\n\n";
            reason += strings.Get(i18n::StrId::EventProcesses);
            for (const auto& process : result.reportedProcesses)
                reason += str::Format(L"\n%ls (PID %lu)", process.name.c_str(), process.pid);
        }
        return reason;
    }

    if (result.status == EjectStatus::Failed) {
        std::wstring message = str::Win32ErrorMessage(result.win32Error);
        if (message.empty()) {
            message = str::Format(L"CONFIGRET 0x%02lX", result.configRet);
        } else {
            message += str::Format(L" (CONFIGRET 0x%02lX)", result.configRet);
        }
        return message;
    }

    return {};
}

}  // namespace qe

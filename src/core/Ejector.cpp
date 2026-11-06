#include "core/Ejector.h"

#include <iterator>

#include "app/Logger.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "util/Str.h"

namespace qe {
namespace {

// MinGW's cfgmgr32.h stops at PNP_VetoDriver and does not export
// CM_MapCrToWin32Error, so both are provided here. Values follow the Windows
// SDK definitions (PNP_VetoParentDevice == 8).
#ifndef PNP_VetoParentDevice
#define PNP_VetoParentDevice ((PNP_VETO_TYPE)8)
#endif

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

EjectResult Ejector::Eject(DEVINST devInst) {
    EjectResult result;
    if (devInst == 0) {
        result.status = EjectStatus::Failed;
        result.win32Error = ERROR_INVALID_PARAMETER;
        return result;
    }

    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    WCHAR vetoName[MAX_PATH] = L"";
    const CONFIGRET configRet = CM_Request_Device_EjectW(
        devInst, &vetoType, vetoName, static_cast<ULONG>(std::size(vetoName)), 0);

    result.configRet = configRet;
    result.vetoType = vetoType;
    result.vetoApplication = vetoName;

    if (configRet == CR_SUCCESS) {
        result.status = EjectStatus::Success;
        return result;
    }
    if (configRet == CR_REMOVE_VETOED) {
        result.status = EjectStatus::Vetoed;
        QE_LOG_WARN(L"eject vetoed (type %d, app \"%ls\")", static_cast<int>(vetoType), vetoName);
        return result;
    }

    result.status = EjectStatus::Failed;
    result.win32Error = ConfigRetToWin32Error(configRet);
    QE_LOG_ERROR(L"CM_Request_Device_Eject failed: CONFIGRET 0x%02lX (Win32 %lu)", configRet,
                 result.win32Error);
    return result;
}

std::wstring Ejector::Describe(const EjectResult& result) {
    const auto& strings = i18n::I18n::Instance();

    if (result.status == EjectStatus::Vetoed) {
        std::wstring reason;
        switch (result.vetoType) {
            case PNP_VetoLegacyDevice:
                reason = strings.Get(i18n::StrId::VetoLegacyDevice);
                break;
            case PNP_VetoPendingClose:
                reason = strings.Get(i18n::StrId::VetoPendingClose);
                break;
            case PNP_VetoWindowsApp:
                reason = strings.Get(i18n::StrId::VetoWindowsApp);
                break;
            case PNP_VetoWindowsService:
                reason = strings.Get(i18n::StrId::VetoWindowsService);
                break;
            case PNP_VetoOutstandingOpen:
                reason = strings.Get(i18n::StrId::VetoOutstandingOpen);
                break;
            case PNP_VetoDevice:
                reason = strings.Get(i18n::StrId::VetoDevice);
                break;
            case PNP_VetoDriver:
                reason = strings.Get(i18n::StrId::VetoDriver);
                break;
            case PNP_VetoParentDevice:
                reason = strings.Get(i18n::StrId::VetoParentDevice);
                break;
            case PNP_VetoTypeUnknown:
            default:
                reason = strings.Get(i18n::StrId::VetoUnknown);
                break;
        }
        if (!result.vetoApplication.empty()) {
            reason += L"\n\n";
            reason += strings.Format(i18n::StrId::VetoAppSuffix, result.vetoApplication);
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

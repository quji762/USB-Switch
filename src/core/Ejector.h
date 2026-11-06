#pragma once
// Safe removal through the PnP manager.
//
// CM_Request_Device_Eject is the documented, supported way to stop and remove a
// device: it runs the full stop/query-remove sequence and reports *why* it
// refused (the PNP_VETO_TYPE). That veto information is the difference between
// " ejection failed" and "Word is holding a file open on E:".

#include <string>

#include <windows.h>
#include <cfgmgr32.h>

namespace qe {

enum class EjectStatus {
    Success,
    Vetoed,   // refused by the PnP manager, vetoType says why
    Failed,   // the call itself failed
};

struct EjectResult {
    EjectStatus status = EjectStatus::Failed;
    CONFIGRET configRet = CR_SUCCESS;
    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    std::wstring vetoApplication;  // name of the vetoing application, if reported
    unsigned long win32Error = 0;  // valid when status == Failed

    bool Succeeded() const noexcept { return status == EjectStatus::Success; }
};

class Ejector {
public:
    // Sends the eject request for the device instance behind `devInst`.
    static EjectResult Eject(DEVINST devInst);

    // Localized explanation of a non-successful result.
    static std::wstring Describe(const EjectResult& result);
};

}  // namespace qe

#pragma once
// Safe removal through the PnP manager.
//
// CM_Request_Device_Eject is the documented, supported way to stop and remove a
// device: it runs the full stop/query-remove sequence and reports *why* it
// refused (the PNP_VETO_TYPE). That veto information is the difference between
// " ejection failed" and "Word is holding a file open on E:".

#include <string>
#include <vector>

#include <windows.h>
#include <cfgmgr32.h>

namespace qe {

enum class EjectStatus {
    Success,
    Ineligible, // unavailable, system disk, or could not verify current inventory
    Vetoed,   // refused by the PnP manager, vetoType says why
    Failed,   // the call itself failed
};

struct ReportedProcess { DWORD pid; std::wstring name; };

struct EjectResult {
    FILETIME requestedAt{};
    std::wstring stoppedInstanceId; // Actual removal target, only after success.
    std::vector<ReportedProcess> reportedProcesses;
    EjectStatus status = EjectStatus::Failed;
    CONFIGRET configRet = CR_SUCCESS;
    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    std::wstring vetoApplication;  // name of the vetoing application, if reported
    unsigned long win32Error = 0;  // valid when status == Failed

    // These refusals can clear while the user waits or closes files. Never
    // resubmit automatically: the tray shows Retry with Cancel selected.
    bool CanRetry() const noexcept {
        if (status != EjectStatus::Vetoed) return false;
        switch (vetoType) {
            case PNP_VetoDevice:
            case PNP_VetoPendingClose:
            case PNP_VetoOutstandingOpen:
            case PNP_VetoWindowsApp:
            case PNP_VetoWindowsService:
            case PNP_VetoTypeUnknown: return true;
            default: return false;
        }
    }

    bool Succeeded() const noexcept { return status == EjectStatus::Success; }
};

class Ejector {
public:
    // Resolve the stable instance ID immediately before the request. A cached
    // DEVINST can go stale while a confirmation dialog is open.
    static EjectResult Eject(const std::wstring& instanceId);
    // Restart only the exact node previously stopped by this tray session.
    static DWORD RestartStopped(const std::wstring& diskId, const std::wstring& targetId);
    static bool HeldForEject(const std::wstring& targetId);

    // Localized explanation of a non-successful result.
    static std::wstring Describe(const EjectResult& result);
};

}  // namespace qe

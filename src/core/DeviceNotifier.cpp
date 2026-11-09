#include "core/DeviceNotifier.h"

#include <initguid.h>

#include <windows.h>
#include <winioctl.h>  // GUID_DEVINTERFACE_DISK
#include <dbt.h>

#include "app/Logger.h"
#include "util/Str.h"

namespace qe {
namespace {

std::wstring DriveLettersFromMask(DWORD unitMask) {
    std::wstring letters;
    for (int i = 0; i < 26; ++i) {
        if ((unitMask & (1u << i)) == 0) continue;
        if (!letters.empty()) letters += L", ";
        letters += static_cast<wchar_t>(L'A' + i);
        letters += L':';
    }
    return letters;
}

}  // namespace

bool DeviceNotifier::Register(HWND hwnd) {
    Unregister();
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_DISK;

    notify_ = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notify_ == nullptr) {
        QE_LOG_WARN(L"RegisterDeviceNotification failed: %lu", GetLastError());
        return false;
    }
    hwnd_ = hwnd;
    QE_LOG_INFO(L"device notifications registered");
    return true;
}

void DeviceNotifier::Unregister() noexcept {
    if (notify_ != nullptr) {
        UnregisterDeviceNotification(notify_);
        notify_ = nullptr;
    }
    hwnd_ = nullptr;
}

bool DeviceNotifier::IsRelevantChange(UINT event, LPARAM data) {
    switch (event) {
        case DBT_DEVNODES_CHANGED:
            return true;
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
            break;
        default:
            return false;
    }

    if (data == 0) return true;
    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if (header == nullptr || header->dbch_size < sizeof(DEV_BROADCAST_HDR)) return false;

    // DBT_DEVTYP_HANDLE broadcasts are delivered only to windows that registered
    // a DEV_BROADCAST_HANDLE filter. Register() registers a device-interface
    // filter, so such a message can never arrive here and is not accepted.
    switch (header->dbch_devicetype) {
        case DBT_DEVTYP_VOLUME:
        case DBT_DEVTYP_DEVICEINTERFACE:
            return true;
        default:
            return false;
    }
}

std::wstring DeviceNotifier::DescribeChange(UINT event, LPARAM data) {
    const wchar_t* action = L"change";
    if (event == DBT_DEVICEARRIVAL) action = L"arrival";
    if (event == DBT_DEVICEREMOVECOMPLETE) action = L"removal";
    if (event == DBT_DEVNODES_CHANGED) return L"device tree change";

    if (data == 0) return str::Format(L"%ls (no payload)", action);
    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if (header->dbch_size < sizeof(DEV_BROADCAST_HDR)) {
        return str::Format(L"%ls (truncated payload)", action);
    }
    if (header->dbch_devicetype == DBT_DEVTYP_VOLUME) {
        if (header->dbch_size < sizeof(DEV_BROADCAST_VOLUME)) {
            return str::Format(L"%ls (truncated volume payload)", action);
        }
        const auto* volume = reinterpret_cast<const DEV_BROADCAST_VOLUME*>(data);
        return str::Format(L"%ls of volume %ls", action,
                           DriveLettersFromMask(volume->dbcv_unitmask).c_str());
    }
    if (header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
        if (header->dbch_size < sizeof(DEV_BROADCAST_DEVICEINTERFACE_W)) {
            return str::Format(L"%ls (truncated interface payload)", action);
        }
        const auto* interfaceChange = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(data);
        // dbcc_name is only as long as the payload says; cap the scan so a
        // truncated broadcast cannot read past it.
        const size_t available =
            (header->dbch_size - offsetof(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name)) / sizeof(WCHAR);
        std::wstring name(interfaceChange->dbcc_name, wcsnlen(interfaceChange->dbcc_name, available));
        return str::Format(L"%ls of interface %ls", action, name.c_str());
    }
    return str::Format(L"%ls (type %lu)", action, header->dbch_devicetype);
}

}  // namespace qe

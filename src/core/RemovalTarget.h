#pragma once
#include <windows.h>
#include <cfgmgr32.h>
#include <iterator>

#include "util/Str.h"

namespace qe::detail {

struct RemovalTopologyApi {
    decltype(&CM_Get_Parent) parent = CM_Get_Parent;
    decltype(&CM_Get_Child) child = CM_Get_Child;
    decltype(&CM_Get_Sibling) sibling = CM_Get_Sibling;
    decltype(&CM_Get_DevNode_Registry_PropertyW) property = CM_Get_DevNode_Registry_PropertyW;
    decltype(&CM_Get_Device_IDW) id = CM_Get_Device_IDW;
};

// Some USBSTOR disks expose safe removal on their immediate transport node.
inline DEVINST RemovalTarget(DEVINST disk, const RemovalTopologyApi& api = {}) {
    DEVINST parent{}, child{}, sibling{};
    if (api.parent(&parent, disk, 0) != CR_SUCCESS || !parent || parent == disk) return disk;
    WCHAR service[64]{};
    ULONG type{}, bytes = sizeof(service);
    if (api.property(parent, CM_DRP_SERVICE, &type, service, &bytes, 0) != CR_SUCCESS ||
        type != REG_SZ || bytes < sizeof(WCHAR) || bytes > sizeof(service) || bytes % sizeof(WCHAR) ||
        service[bytes / sizeof(WCHAR) - 1] != 0) return disk;
    const std::wstring_view name(service, bytes / sizeof(WCHAR) - 1);
    if (!str::EqualsI(name, L"USBSTOR") && !str::EqualsI(name, L"UASPStor")) return disk;
    DWORD caps{};
    bytes = sizeof(caps);
    if (api.property(parent, CM_DRP_CAPABILITIES, &type, &caps, &bytes, 0) != CR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(caps) || !(caps & CM_DEVCAP_REMOVABLE)) return disk;
    // Inspect the children last, as close as possible to the eject request.
    if (api.child(&child, parent, 0) != CR_SUCCESS || child != disk) return disk;
    const CONFIGRET next = api.sibling(&sibling, child, 0);
    if (next == CR_NO_SUCH_DEVNODE) return parent;
    if (next != CR_SUCCESS || !sibling) return disk;
    // WD My Passport exposes an auxiliary SES management LUN alongside the
    // disk. Allow only that identifiable leaf, never another disk or slot.
    WCHAR id[MAX_DEVICE_ID_LEN]{};
    constexpr wchar_t prefix[] = L"USBSTOR\\Other&Ven_WD&Prod_SES_Device&";
    if (api.id(sibling, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS ||
        !str::EqualsI(std::wstring_view(id).substr(0, std::size(prefix) - 1), prefix) ||
        api.child(&child, sibling, 0) != CR_NO_SUCH_DEVNODE ||
        api.sibling(&child, sibling, 0) != CR_NO_SUCH_DEVNODE) return disk;
    return parent;
}

}  // namespace qe::detail

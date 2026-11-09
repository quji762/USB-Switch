#include "core/UsbPort.h"
#include "app/Logger.h"
#include "util/Str.h"
#include "util/Win32.h"
#include <cfgmgr32.h>
#include <cfg.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <vector>

namespace qe {
namespace {
constexpr GUID hubClass{0xf18a0e88,0xc30c,0x11d0,{0x88,0x15,0x00,0xa0,0xc9,0x06,0xbe,0xd8}};
std::wstring Property(DEVINST n, ULONG key) {
    WCHAR text[512]{};
    ULONG type{}, bytes=sizeof(text);
    if (CM_Get_DevNode_Registry_PropertyW(n,key,&type,text,&bytes,0)!=CR_SUCCESS ||
        type!=REG_SZ || bytes<2 || bytes>sizeof(text) || bytes%2 || text[bytes/2-1]) return {};
    return text;
}
}
DWORD ReconnectUsbPort(const std::wstring& target, bool probeOnly) {
    if (!probeOnly) {
        HANDLE raw{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return GetLastError();
        Handle token(raw);
        TOKEN_ELEVATION elevation{};
        DWORD size{};
        if (!GetTokenInformation(token.Get(), TokenElevation, &elevation, sizeof(elevation), &size))
            return GetLastError();
        if (!elevation.TokenIsElevated) return ERROR_ACCESS_DENIED;
    }
    auto id=target;
    DEVINST node{},parent{};
    if (id.rfind(L"USB\\",0)!=0 || CM_Locate_DevNodeW(&node,id.data(),CM_LOCATE_DEVNODE_NORMAL)!=CR_SUCCESS)
        return ERROR_DEVICE_NOT_CONNECTED;
    const auto service=Property(node,CM_DRP_SERVICE), key=Property(node,CM_DRP_DRIVER);
    if ((_wcsicmp(service.c_str(),L"USBSTOR") && _wcsicmp(service.c_str(),L"UASPStor")) || key.empty())
        return ERROR_NOT_SUPPORTED;
    ULONG type{},port{},bytes=sizeof(port);
    WCHAR parentId[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_DevNode_Registry_PropertyW(node,CM_DRP_ADDRESS,&type,&port,&bytes,0)!=CR_SUCCESS ||
        type!=REG_DWORD || bytes!=sizeof(port) || !port || port>255 ||
        CM_Get_Parent(&parent,node,0)!=CR_SUCCESS || CM_Get_Device_IDW(parent,parentId,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS)
        return ERROR_NOT_FOUND;
    ULONG chars{};
    GUID cls=hubClass;
    if (CM_Get_Device_Interface_List_SizeW(&chars,&cls,parentId,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS ||
        chars<3 || chars>32768) return ERROR_NOT_FOUND;
    std::vector<WCHAR> path(chars);
    if (CM_Get_Device_Interface_ListW(&cls,parentId,path.data(),chars,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS ||
        !path[0] || wcsnlen(path.data(),chars)+2!=chars) return ERROR_NOT_FOUND;
    Handle hub(CreateFileW(path.data(),probeOnly ? 0 : GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           nullptr,OPEN_EXISTING,0,nullptr));
    if (!hub) return GetLastError();
    std::vector<BYTE> buf(4096);
    auto* info=reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(buf.data());
    info->ConnectionIndex=port;
    DWORD got{};
    if (!DeviceIoControl(hub.Get(),IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,info,sizeof(*info),
                         info,static_cast<DWORD>(buf.size()),&got,nullptr)) return GetLastError();
    constexpr auto offset=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName);
    if (got>buf.size() || info->ActualLength>got || info->ActualLength<offset+sizeof(WCHAR)) return ERROR_INVALID_DATA;
    auto len=(info->ActualLength-offset)/sizeof(WCHAR);
    if (wcsnlen(info->DriverKeyName,len)>=len || _wcsicmp(info->DriverKeyName,key.c_str())) return ERROR_DEVICE_NOT_CONNECTED;
    if (probeOnly) return ERROR_SUCCESS;
    ULONG status{},problem{};
    if (CM_Get_DevNode_Status(&status,&problem,node,0)!=CR_SUCCESS || !(status & DN_HAS_PROBLEM) ||
        problem!=CM_PROB_HELD_FOR_EJECT) return ERROR_DEVICE_NOT_CONNECTED;
    USB_CYCLE_PORT_PARAMS req{};
    req.ConnectionIndex=port;
    if (!DeviceIoControl(hub.Get(),IOCTL_USB_HUB_CYCLE_PORT,&req,sizeof(req),&req,sizeof(req),&got,nullptr))
        return GetLastError();
    // StatusReturned is a USBD status from the hub driver, not a Win32 error,
    // so folding it into ERROR_GEN_FAILURE throws away the only clue about
    // which hub refused the cycle.
    if (req.StatusReturned) {
        const std::wstring detail=str::Format(L"port cycle on %ls (connection %lu) reported hub status 0x%08lX",
                                              target.c_str(),port,req.StatusReturned);
        QE_LOG_WARN(L"%ls",detail.c_str());
        // The elevated helper that usually performs this cycle does not open a
        // log file (see --restart-held-usb), so leave the same trace for a
        // debugger rather than dropping it.
        if (Logger::Instance().Path().empty()) OutputDebugStringW(detail.c_str());
        return ERROR_GEN_FAILURE;
    }
    return ERROR_SUCCESS;
}
}

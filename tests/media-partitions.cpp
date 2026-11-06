#include "core/MediaEject.h"
#include <winioctl.h>
#include <cstdio>
#include <string_view>

namespace {
HANDLE opened[3]{};
int locks, ejections;
bool busy;
HANDLE WINAPI Open(LPCWSTR path, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    int i = path[0] - L'0';
    if (i < 0 || i > 2) return INVALID_HANDLE_VALUE;
    return opened[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}
BOOL WINAPI Control(HANDLE h, DWORD code, LPVOID, DWORD, LPVOID out, DWORD size, LPDWORD bytes, LPOVERLAPPED) {
    *bytes = size;
    if (code == IOCTL_STORAGE_GET_DEVICE_NUMBER && h == opened[0]) {
        *static_cast<STORAGE_DEVICE_NUMBER*>(out) = {FILE_DEVICE_DISK, 7, 0}; return TRUE;
    }
    if (code == IOCTL_DISK_GET_DRIVE_LAYOUT_EX && h == opened[0]) {
        auto* layout = static_cast<DRIVE_LAYOUT_INFORMATION_EX*>(out);
        layout->PartitionStyle = PARTITION_STYLE_GPT; layout->PartitionCount = 2;
        for (int i = 0; i < 2; ++i) {
            auto& part = layout->PartitionEntry[i];
            part.PartitionStyle = PARTITION_STYLE_GPT;
            part.StartingOffset.QuadPart = (i+1)*4096; part.PartitionLength.QuadPart = 4096;
        }
        return TRUE;
    }
    if (code == IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS && (h == opened[1] || h == opened[2])) {
        auto* ext = static_cast<VOLUME_DISK_EXTENTS*>(out);
        ext->NumberOfDiskExtents = 1; ext->Extents[0].DiskNumber = 7;
        ext->Extents[0].StartingOffset.QuadPart = h == opened[1] ? 4096 : 8192;
        ext->Extents[0].ExtentLength.QuadPart = 4096; return TRUE;
    }
    if (code == FSCTL_LOCK_VOLUME) {
        if (busy && h == opened[2]) { SetLastError(ERROR_SHARING_VIOLATION); return FALSE; }
        ++locks; return TRUE;
    }
    if (code == IOCTL_STORAGE_EJECT_MEDIA) { ++ejections; return TRUE; }
    SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
}
}

int main() {
    qe::Device d;
    d.diskInterface = L"0"; d.deviceNumber = 7; d.driverMediaRemovable = true;
    for (int scenario = 0; scenario < 3; ++scenario) {
        d.volumeInterfaces = {L"1"};
        if (scenario != 0) d.volumeInterfaces.push_back(L"2");
        locks = ejections = 0; busy = scenario == 1;
        auto r = qe::EjectMedia(d, {Open, Control});
        if (scenario == 0 && (std::wstring_view(r.stage) != L"incomplete_volume_set" || locks || ejections)) return 1;
        if (scenario == 1 && (r.error != ERROR_SHARING_VIOLATION || locks != 1 || ejections)) return 2;
        if (scenario == 2 && (r.error || locks != 2 || ejections != 1)) return 3;
        for (HANDLE h : opened) {
            DWORD flags;
            if (h && (GetHandleInformation(h, &flags) || GetLastError() != ERROR_INVALID_HANDLE)) return 4;
        }
    }
    puts("Multi-partition media eject: incomplete layout, busy second volume, success and handle cleanup passed");
}

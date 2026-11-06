#include "core/MediaEject.h"
#include "util/Win32.h"
#include <winioctl.h>
#include <algorithm>
#include <set>

namespace qe {
namespace {
using Ranges = std::set<std::pair<LONGLONG, LONGLONG>>;

DWORD Layout(HANDLE disk, Ranges& ranges) {
    for (size_t size = 4096; size <= 1024 * 1024; size *= 2) {
        std::vector<BYTE> data(size);
        DWORD bytes = 0;
        if (!DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, data.data(),
                             static_cast<DWORD>(size), &bytes, nullptr)) {
            DWORD error = GetLastError();
            if (error == ERROR_INSUFFICIENT_BUFFER || error == ERROR_MORE_DATA) continue;
            return error;
        }
        const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(data.data());
        constexpr size_t prefix = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
        if (bytes < prefix || bytes > size || layout->PartitionCount > (bytes-prefix)/sizeof(PARTITION_INFORMATION_EX))
            return ERROR_INVALID_DATA;
        if (layout->PartitionStyle != PARTITION_STYLE_MBR && layout->PartitionStyle != PARTITION_STYLE_GPT)
            return ERROR_NOT_SUPPORTED;
        for (DWORD i = 0; i < layout->PartitionCount; ++i) {
            const auto& part = layout->PartitionEntry[i];
            if (part.PartitionStyle != layout->PartitionStyle) return ERROR_INVALID_DATA;
            if (part.PartitionStyle == PARTITION_STYLE_MBR && part.Mbr.PartitionType == PARTITION_ENTRY_UNUSED) continue;
            if (part.StartingOffset.QuadPart < 0 || part.PartitionLength.QuadPart <= 0 ||
                !ranges.emplace(part.StartingOffset.QuadPart, part.PartitionLength.QuadPart).second)
                return ERROR_INVALID_DATA;
        }
        return ranges.empty() ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
    }
    return ERROR_INSUFFICIENT_BUFFER;
}
}

MediaResult EjectMedia(const Device& device) {
    if (device.driverMediaRemovable != true || device.diskInterface.empty() || device.volumeInterfaces.empty())
        return {L"unsupported_media", ERROR_NOT_SUPPORTED};
    constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    Handle disk(CreateFileW(device.diskInterface.c_str(), 0, share, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!disk) return {L"open_disk", GetLastError()};
    DWORD bytes = 0;
    STORAGE_DEVICE_NUMBER id{};
    if (!DeviceIoControl(disk.Get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &id, sizeof(id), &bytes, nullptr))
        return {L"disk_identity", GetLastError()};
    if (bytes < sizeof(id) || id.DeviceType != FILE_DEVICE_DISK || id.DeviceNumber != device.deviceNumber)
        return {L"disk_changed", ERROR_INVALID_DATA};
    Ranges expected, observed;
    if (DWORD err = Layout(disk.Get(), expected)) return {L"disk_layout", err};
    std::vector<Handle> volumes;
    for (const auto& path : device.volumeInterfaces) {
        Handle volume(CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!volume) return {L"open_volume", GetLastError()};
        VOLUME_DISK_EXTENTS ext{};
        if (!DeviceIoControl(volume.Get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &ext, sizeof(ext), &bytes, nullptr))
            return {L"volume_extent", GetLastError()};
        if (bytes < sizeof(ext) || ext.NumberOfDiskExtents != 1 || ext.Extents[0].DiskNumber != id.DeviceNumber)
            return {L"volume_changed", ERROR_INVALID_DATA};
        auto range = std::make_pair(ext.Extents[0].StartingOffset.QuadPart, ext.Extents[0].ExtentLength.QuadPart);
        if (!expected.count(range) || !observed.insert(range).second) return {L"volume_changed", ERROR_INVALID_DATA};
        volumes.push_back(std::move(volume));
    }
    if (expected != observed) return {L"incomplete_volume_set", ERROR_NOT_SUPPORTED};
    for (const auto& volume : volumes)
        if (!DeviceIoControl(volume.Get(), FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytes, nullptr))
            return {L"lock_volume", GetLastError()};
    Ranges current;
    if (DWORD err = Layout(disk.Get(), current)) return {L"disk_layout", err};
    if (current != expected) return {L"disk_changed", ERROR_INVALID_DATA};
    if (!DeviceIoControl(volumes.front().Get(), IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &bytes, nullptr))
        return {L"eject_media", GetLastError()};
    return {L"media_ejected", ERROR_SUCCESS};
}
}

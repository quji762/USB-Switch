#include "core/MediaEject.h"
#include "util/Win32.h"
#include <objbase.h>  // IsEqualGUID for the partition-type checks below
#include <winioctl.h>
#include <algorithm>
#include <set>

namespace qe {
namespace {
using Ranges = std::set<std::pair<LONGLONG, LONGLONG>>;

// Windows puts a Microsoft reserved partition on every GPT disk it formats. It
// carries no filesystem and no volume is ever created for it, so counting it as
// a partition that must map to a volume fails every default GPT card.
constexpr GUID kMicrosoftReserved = {0xE3C9E316, 0x0B5C, 0x4DB8,
                                     {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}};

// The EFI system partition Windows creates on a GPT disk. It only holds the boot
// loader and by default gets no drive letter and stays hidden from Explorer, so
// no volume is reported for it - but a user can give it one, and the volume that
// appears then has to match this partition instead of failing as a change.
constexpr GUID kEfiSystemPartition = {0xC12A7328, 0xF81F, 0x11D2,
                                      {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};

// The metadata partition of a Windows dynamic (LDM) disk. It stores the dynamic
// disk database and is never mounted, so it has no volume either. The LDM data
// partition (AF9B60A0-...) is deliberately not listed here: the dynamic volumes
// inside it do get drive letters and must still map to a volume.
constexpr GUID kLdmMetadata = {0x5808C8AA, 0x7E8F, 0x42E0,
                               {0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3}};

// MBR extended-partition containers. The logical drives inside them are
// reported as partition entries of their own; the container never carries a
// volume, so requiring one would fail every MBR card that uses them.
bool ExtendedContainer(BYTE type) {
    return type == 0x05 || type == 0x0F || type == 0x85;
}

// Every partition that must map to a volume goes into `ranges`. The partitions
// below carry no volume by default, so they go into `optional` instead: one of
// them (an EFI system partition the user gave a drive letter, say) can still
// produce a volume, and that volume's extent is a partition range of this disk,
// not a change.
DWORD Layout(const MediaIo& io, HANDLE disk, Ranges& ranges, Ranges& optional) {
    for (size_t size = 4096; size <= 1024 * 1024; size *= 2) {
        std::vector<BYTE> data(size);
        DWORD bytes = 0;
        if (!io.control(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, data.data(),
                             static_cast<DWORD>(size), &bytes, nullptr)) {
            DWORD error = GetLastError();
            if (error == ERROR_INSUFFICIENT_BUFFER || error == ERROR_MORE_DATA) continue;
            return error;
        }
        const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(data.data());
        constexpr size_t prefix = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
        if (bytes < prefix || bytes > size || layout->PartitionCount > (bytes-prefix)/sizeof(PARTITION_INFORMATION_EX))
            return ERROR_INVALID_DATA;
        // DRIVE_LAYOUT_INFORMATION_EX::PartitionStyle is a raw DWORD in the SDK,
        // while every entry carries the PARTITION_STYLE enum. Hoisting the cast
        // keeps the comparison below between two values of the same type.
        const PARTITION_STYLE style = static_cast<PARTITION_STYLE>(layout->PartitionStyle);
        if (style != PARTITION_STYLE_MBR && style != PARTITION_STYLE_GPT)
            return ERROR_NOT_SUPPORTED;
        for (DWORD i = 0; i < layout->PartitionCount; ++i) {
            const auto& part = layout->PartitionEntry[i];
            if (part.PartitionStyle != style) return ERROR_INVALID_DATA;
            if (part.PartitionStyle == PARTITION_STYLE_MBR) {
                if (part.Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
                    ExtendedContainer(part.Mbr.PartitionType)) continue;
            } else if (IsEqualGUID(part.Gpt.PartitionType, kMicrosoftReserved) ||
                       IsEqualGUID(part.Gpt.PartitionType, kEfiSystemPartition) ||
                       IsEqualGUID(part.Gpt.PartitionType, kLdmMetadata)) {
                optional.emplace(part.StartingOffset.QuadPart, part.PartitionLength.QuadPart);
                continue;
            }
            if (part.StartingOffset.QuadPart < 0 || part.PartitionLength.QuadPart <= 0 ||
                !ranges.emplace(part.StartingOffset.QuadPart, part.PartitionLength.QuadPart).second)
                return ERROR_INVALID_DATA;
        }
        return ranges.empty() ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
    }
    return ERROR_INSUFFICIENT_BUFFER;
}
}

MediaResult EjectMedia(const Device& device, const MediaIo& io) {
    if (device.MediaUnavailable()) return device.mediaState == MediaState::Empty ?
        MediaResult{L"no_media", ERROR_NO_MEDIA_IN_DRIVE} : MediaResult{L"not_ready", ERROR_NOT_READY};
    if (device.driverMediaRemovable != true || device.diskInterface.empty() || device.volumeInterfaces.empty())
        return {L"unsupported_media", ERROR_NOT_SUPPORTED};
    constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    Handle disk(io.open(device.diskInterface.c_str(), 0, share, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!disk) return {L"open_disk", GetLastError()};
    DWORD bytes = 0;
    STORAGE_DEVICE_NUMBER id{};
    if (!io.control(disk.Get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &id, sizeof(id), &bytes, nullptr))
        return {L"disk_identity", GetLastError()};
    if (bytes < sizeof(id) || id.DeviceType != FILE_DEVICE_DISK || id.DeviceNumber != device.deviceNumber)
        return {L"disk_changed", ERROR_INVALID_DATA};
    Ranges expected, observed, optional;
    if (DWORD err = Layout(io, disk.Get(), expected, optional)) return {L"disk_layout", err};
    std::vector<Handle> volumes;
    for (const auto& path : device.volumeInterfaces) {
        Handle volume(io.open(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!volume) return {L"open_volume", GetLastError()};
        // A volume can span several disks (spanned, striped, mirrored) or several
        // extents of one disk. That is a static property of the partition table,
        // not something that changed while the eject ran, so it is reported on
        // its own stage instead of as a change. The buffer holds a few extents so
        // the count is read from the layout rather than from "the IOCTL said
        // something vague about the buffer size".
        std::vector<BYTE> buffer(offsetof(VOLUME_DISK_EXTENTS, Extents) + 4 * sizeof(DISK_EXTENT));
        auto* ext = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buffer.data());
        if (!io.control(volume.Get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_MORE_DATA || error == ERROR_INSUFFICIENT_BUFFER)
                return {L"volume_multi_extent", ERROR_NOT_SUPPORTED};
            return {L"volume_extent", error};
        }
        if (bytes < offsetof(VOLUME_DISK_EXTENTS, Extents) + sizeof(DISK_EXTENT) || !ext->NumberOfDiskExtents)
            return {L"volume_extent", ERROR_INVALID_DATA};
        if (ext->NumberOfDiskExtents != 1) return {L"volume_multi_extent", ERROR_NOT_SUPPORTED};
        if (ext->Extents[0].DiskNumber != id.DeviceNumber) return {L"volume_changed", ERROR_INVALID_DATA};
        auto range = std::make_pair(ext->Extents[0].StartingOffset.QuadPart, ext->Extents[0].ExtentLength.QuadPart);
        // An extent on a partition Layout put in `optional` is legitimate: that
        // partition carries no volume by default, but mounting one (a drive
        // letter on the EFI system partition) is exactly this case. Anything
        // outside both sets is a volume of another disk or a repartition.
        if (!expected.count(range) && !optional.count(range)) return {L"volume_changed", ERROR_INVALID_DATA};
        if (!observed.insert(range).second) return {L"volume_changed", ERROR_INVALID_DATA};
        volumes.push_back(std::move(volume));
    }
    // Every partition that must have a volume has one. A mounted `optional`
    // partition adds a range to `observed` beyond `expected`, which is fine;
    // a missing one is not.
    if (!std::includes(observed.begin(), observed.end(), expected.begin(), expected.end()))
        return {L"incomplete_volume_set", ERROR_NOT_SUPPORTED};
    for (const auto& volume : volumes)
        if (!io.control(volume.Get(), FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytes, nullptr))
            return {L"lock_volume", GetLastError()};
    Ranges current, currentOptional;
    if (DWORD err = Layout(io, disk.Get(), current, currentOptional)) return {L"disk_layout", err};
    if (current != expected || currentOptional != optional) return {L"disk_changed", ERROR_INVALID_DATA};
    if (!io.control(volumes.front().Get(), IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &bytes, nullptr))
        return {L"eject_media", GetLastError()};
    return {L"media_ejected", ERROR_SUCCESS};
}
}

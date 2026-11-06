#pragma once
// Removable device enumeration.
//
// Strategy (all of it documented in README):
//   1. Enumerate disk device interfaces (GUID_DEVINTERFACE_DISK) through
//      SetupAPI. Each interface gives a device path, a DEVINST and registry
//      properties.
//   2. Open each disk with zero access rights and query
//      IOCTL_STORAGE_GET_DEVICE_NUMBER + IOCTL_STORAGE_QUERY_PROPERTY for the
//      bus type, removable flag, vendor/product strings and device number.
//   3. Enumerate volumes (FindFirstVolume) and match them to disks through the
//      same device number, which is how drive letters, volume labels and sizes
//      end up on the right device.
//
// Only devices that are USB or report removable media are exposed - a fixed
// internal disk is never something this tool should touch.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <setupapi.h>

#include "core/Device.h"

namespace qe {

// Resolve an exact enumerated mount point, refusing ambiguous snapshots.
std::optional<Device> FindByMountPath(const DeviceList& devices, std::wstring_view input);

std::vector<DWORD> WindowsVolumeDisks();

class DeviceManager {
public:
    using ChangeCallback = std::function<void()>;

    // Boundary around the system-wide enumeration calls. Supplying alternate
    // implementations permits testing errors without altering real devices.
    struct EnumerationApi {
        decltype(&WindowsVolumeDisks) windowsDisks = WindowsVolumeDisks;
        decltype(&::SetupDiGetClassDevsW) getClassDevs = ::SetupDiGetClassDevsW;
        decltype(&::FindFirstVolumeW) findFirstVolume = ::FindFirstVolumeW;
        decltype(&::FindNextVolumeW) findNextVolume = ::FindNextVolumeW;
    };

    DeviceManager() = default;
    explicit DeviceManager(EnumerationApi api) : api_(api) {}

    void SetChangeCallback(ChangeCallback callback) { callback_ = std::move(callback); }

    // Retain the last good snapshot on a system enumeration failure.
    // The callback is invoked only after a successful refresh.
    bool Refresh();

    // A snapshot copy, not a reference into devices_: Refresh() replaces the
    // whole vector, so a reference would dangle across a rescan.
    std::vector<Device> Devices() const;

    // Lookups return a copy for the same reason - a pointer into devices_
    // becomes invalid the moment Refresh() runs.
    std::optional<Device> FindByDriveLetter(std::wstring_view driveLetter) const;
    std::optional<Device> FindByInstanceId(std::wstring_view instanceId) const;

    static BusType ToBusType(ULONG storageBusType);

private:
    // Raw data collected for one disk interface before volumes are attached.
    struct DiskCandidate {
        DEVINST devInst = 0;
        ULONG deviceNumber = 0xFFFFFFFF;
        ULONG storageBusType = 0xFFFFFFFF;  // STORAGE_BUS_TYPE value
        bool removableMedia = false;
        std::wstring instanceId;
        std::wstring friendlyName;
        std::wstring deviceDescription;
        std::wstring vendor;
        std::wstring product;
        std::wstring volumeLabel;
        std::wstring driveLetters;
        std::vector<std::wstring> mountPaths;
        // True when a mounted volume on this disk reports DRIVE_REMOVABLE. Used
        // as a fallback for devices whose storage descriptor IOCTL failed.
        bool volumeIsRemovable = false;
        uint64_t sizeBytes = 0;
    };

    bool CollectDiskInterfaces(std::vector<DiskCandidate>& disks);
    bool AttachVolumes(std::vector<DiskCandidate>& disks);
    static std::wstring BuildDisplayName(const DiskCandidate& disk);

    DeviceList devices_;
    ChangeCallback callback_;
    EnumerationApi api_;
};

}  // namespace qe

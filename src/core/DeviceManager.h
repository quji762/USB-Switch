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
#include <string>
#include <string_view>
#include <vector>

#include "core/Device.h"

namespace qe {

class DeviceManager {
public:
    using ChangeCallback = std::function<void()>;

    void SetChangeCallback(ChangeCallback callback) { callback_ = std::move(callback); }

    // Re-enumerates everything, then invokes the change callback (if any).
    void Refresh();

    const DeviceList& Devices() const noexcept { return devices_; }

    const Device* FindByDriveLetter(std::wstring_view driveLetter) const;
    const Device* FindByInstanceId(std::wstring_view instanceId) const;

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
        uint64_t sizeBytes = 0;
    };

    void CollectDiskInterfaces(std::vector<DiskCandidate>& disks);
    void AttachVolumes(std::vector<DiskCandidate>& disks);
    static std::wstring BuildDisplayName(const DiskCandidate& disk);

    DeviceList devices_;
    ChangeCallback callback_;
};

}  // namespace qe

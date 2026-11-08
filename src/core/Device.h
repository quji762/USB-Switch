#pragma once
// Device model shared by the enumerator, the tray UI and the CLI.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <cfgmgr32.h>  // DEVINST

namespace qe {

enum class BusType {
    Unknown,
    Usb,
    FireWire,
    Sd,
    Scsi,
    Ata,
    Sata,
    Nvme,
    Other,
};

// Short technical label for logs and CLI output ("USB", "SD", ...).
const wchar_t* BusTypeName(BusType busType);

enum class MediaState { Unknown, Present, Empty, NotReady };
struct Device {
    // Stable PnP instance id, e.g.
    // "USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0".
    std::wstring instanceId;
    std::wstring diskInterface;
    std::vector<std::wstring> volumeInterfaces;

    // Text shown in the tray menu, e.g. "CRUZER (E:) — 14.6 GB".
    std::wstring displayName;

    std::wstring volumeLabel;    // "MY USB" or empty
    std::wstring driveLetters;   // "E:" / "E:, F:" or empty (no mount point)
    std::vector<std::wstring> mountPaths;  // Drive roots and mounted folders.
    std::wstring vendor;         // from STORAGE_DEVICE_DESCRIPTOR
    std::wstring product;

    BusType busType = BusType::Unknown;
    bool removable = false;
    std::optional<bool> driverMediaRemovable;
    MediaState mediaState = MediaState::Unknown;
    bool MediaUnavailable() const { return mediaState == MediaState::Empty || mediaState == MediaState::NotReady; }
    uint64_t sizeBytes = 0;

    // PnP instance handle used for the eject request. 0 means "cannot eject".
    DEVINST devInst = 0;
    ULONG deviceNumber = 0xFFFFFFFF;

    bool CanEject() const noexcept { return devInst != 0; }

    // USB device that owns this disk when it is a card-reader slot. Empty for a
    // normal removable disk. Slots that share a readerId belong to one reader.
    std::wstring readerId;
    std::wstring readerName;
};

using DeviceList = std::vector<Device>;

struct DeviceGroup {
    std::wstring id;
    std::wstring name;
    std::vector<size_t> indexes;
};

// A reader submenu is produced only when at least two devices share readerId.
// Anything else stays a single top-level device, in the original order.
inline std::vector<DeviceGroup> GroupReaders(const DeviceList& devices) {
    std::unordered_map<std::wstring, size_t> counts;
    for (const auto& device : devices)
        if (!device.readerId.empty()) ++counts[device.readerId];

    std::vector<DeviceGroup> groups;
    std::unordered_map<std::wstring, size_t> position;
    for (size_t index = 0; index < devices.size(); ++index) {
        const auto& device = devices[index];
        const bool reader = !device.readerId.empty() && counts[device.readerId] >= 2;
        if (!reader) {
            groups.push_back(DeviceGroup{{}, {}, {index}});
            continue;
        }
        const auto found = position.find(device.readerId);
        if (found == position.end()) {
            position.emplace(device.readerId, groups.size());
            groups.push_back(DeviceGroup{device.readerId, device.readerName, {index}});
        } else {
            auto& group = groups[found->second];
            group.indexes.push_back(index);
            if (group.name.empty()) group.name = device.readerName;
        }
    }
    return groups;
}

}  // namespace qe

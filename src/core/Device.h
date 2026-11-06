#pragma once
// Device model shared by the enumerator, the tray UI and the CLI.

#include <cstdint>
#include <optional>
#include <string>
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

struct Device {
    // Stable PnP instance id, e.g.
    // "USBSTOR\DISK&VEN_SANDISK&PROD_CRUZER&REV_1.26\4C530001250606112345&0".
    std::wstring instanceId;

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
    uint64_t sizeBytes = 0;

    // PnP instance handle used for the eject request. 0 means "cannot eject".
    DEVINST devInst = 0;
    ULONG deviceNumber = 0xFFFFFFFF;

    bool CanEject() const noexcept { return devInst != 0; }
};

using DeviceList = std::vector<Device>;

}  // namespace qe

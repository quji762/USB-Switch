#include "core/DeviceManager.h"

// initguid.h must come first: it turns DEFINE_GUID into definitions so the
// GUIDs we reference (GUID_DEVINTERFACE_DISK) are real symbols, not externs.
#include <initguid.h>

#include <windows.h>
#include <winioctl.h>   // IOCTL_STORAGE_*, STORAGE_DEVICE_NUMBER, GUID_DEVINTERFACE_DISK
#include <setupapi.h>
#include <devpkey.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <iterator>

#include "app/Logger.h"
#include "util/Str.h"
#include "util/Win32.h"

namespace qe {
namespace {

constexpr size_t kDescriptorBufferSize = 4096;

// STORAGE_DEVICE_DESCRIPTOR stores its strings as raw ANSI bytes at offsets
// from the start of the descriptor. Offsets are only valid inside the bytes
// the IOCTL actually returned, so every read is bounds checked.
std::string DescriptorStringAt(const BYTE* base, DWORD validBytes, DWORD offset) {
    if (offset == 0 || offset >= validBytes) return {};
    const char* text = reinterpret_cast<const char*>(base) + offset;
    const size_t maxLength = static_cast<size_t>(validBytes - offset);
    const size_t length = strnlen(text, maxLength);
    return std::string(text, length);
}

std::wstring QueryDevicePropertyString(HDEVINFO info, const SP_DEVINFO_DATA& data,
                                       const DEVPROPKEY& key) {
    WCHAR buffer[512];
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    DWORD size = sizeof(buffer);
    if (SetupDiGetDevicePropertyW(info, const_cast<PSP_DEVINFO_DATA>(&data), &key, &type,
                                  reinterpret_cast<PBYTE>(buffer), size, &size, 0)) {
        return type == DEVPROP_TYPE_STRING ? std::wstring(buffer) : std::wstring();
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return {};
    std::vector<WCHAR> large(static_cast<size_t>(size) / sizeof(WCHAR) + 2, L'\0');
    size = static_cast<DWORD>(large.size() * sizeof(WCHAR));
    if (!SetupDiGetDevicePropertyW(info, const_cast<PSP_DEVINFO_DATA>(&data), &key, &type,
                                   reinterpret_cast<PBYTE>(large.data()), size, &size, 0)) {
        return {};
    }
    return type == DEVPROP_TYPE_STRING ? std::wstring(large.data()) : std::wstring();
}

struct DiskQuery {
    ULONG deviceNumber = 0xFFFFFFFF;
    ULONG storageBusType = 0xFFFFFFFF;
    bool removableMedia = false;
    std::string vendor;
    std::string product;
};

// Zero-access open + IOCTLs. Devices that refuse to open cannot be identified
// as removable, so the caller treats failure as "skip this interface".
bool QueryDisk(HANDLE handle, DiskQuery& out) {
    STORAGE_DEVICE_NUMBER number{};
    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number),
                         &returned, nullptr)) {
        QE_LOG_DEBUG(L"IOCTL_STORAGE_GET_DEVICE_NUMBER failed: %lu", GetLastError());
        return false;
    }
    if (returned < sizeof(number) || number.DeviceType != FILE_DEVICE_DISK) return false;
    out.deviceNumber = number.DeviceNumber;

    std::vector<BYTE> buffer(kDescriptorBufferSize);
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                        static_cast<DWORD>(buffer.size()), &returned, nullptr) &&
        returned >= offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) && returned <= buffer.size()) {
        const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        // The driver reports the descriptor size; trust the smaller of the two.
        const DWORD validBytes = (descriptor->Size != 0 && descriptor->Size < returned)
                                     ? descriptor->Size
                                     : returned;
        if (validBytes < offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties)) return true;
        out.storageBusType = descriptor->BusType;
        out.removableMedia = descriptor->RemovableMedia != FALSE;
        out.vendor = DescriptorStringAt(buffer.data(), validBytes, descriptor->VendorIdOffset);
        out.product = DescriptorStringAt(buffer.data(), validBytes, descriptor->ProductIdOffset);
    }
    return true;
}

}  // namespace

std::wstring DeviceManager::BuildDisplayName(const DiskCandidate& disk) {
    std::wstring name;
    if (!disk.volumeLabel.empty()) {
        name = disk.volumeLabel;
    } else if (!disk.friendlyName.empty()) {
        name = disk.friendlyName;
    } else if (!disk.product.empty()) {
        name = disk.product;  // already UTF-16
    } else if (!disk.deviceDescription.empty()) {
        name = disk.deviceDescription;
    } else {
        name = L"USB";
    }

    if (!disk.driveLetters.empty()) {
        name += L" (";
        name += disk.driveLetters;
        name += L")";
    }
    if (disk.driveLetters.empty() && !disk.mountPaths.empty()) {
        name += L" (" + disk.mountPaths.front() + L")";
    }
    if (disk.sizeBytes > 0) {
        name += L" — ";
        name += str::FileSizeString(disk.sizeBytes);
    }
    return name;
}

BusType DeviceManager::ToBusType(ULONG storageBusType) {
    switch (static_cast<STORAGE_BUS_TYPE>(storageBusType)) {
        case BusTypeUsb: return BusType::Usb;
        case BusType1394: return BusType::FireWire;
        case BusTypeSd: return BusType::Sd;
        case BusTypeScsi: return BusType::Scsi;
        case BusTypeAta: return BusType::Ata;
        case BusTypeSata: return BusType::Sata;
        case BusTypeNvme: return BusType::Nvme;
        default: return BusType::Unknown;
    }
}

const wchar_t* BusTypeName(BusType busType) {
    switch (busType) {
        case BusType::Usb: return L"USB";
        case BusType::FireWire: return L"1394";
        case BusType::Sd: return L"SD";
        case BusType::Scsi: return L"SCSI";
        case BusType::Ata: return L"ATA";
        case BusType::Sata: return L"SATA";
        case BusType::Nvme: return L"NVMe";
        case BusType::Unknown: return L"?";
        case BusType::Other: return L"other";
    }
    return L"?";
}

bool DeviceManager::CollectDiskInterfaces(std::vector<DiskCandidate>& disks) {
    DevInfoSet info(api_.getClassDevs(&GUID_DEVINTERFACE_DISK, nullptr, nullptr,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (!info) {
        QE_LOG_ERROR(L"SetupDiGetClassDevs(GUID_DEVINTERFACE_DISK) failed: %lu", GetLastError());
        return false;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        if (!SetupDiEnumDeviceInterfaces(info.Get(), nullptr, &GUID_DEVINTERFACE_DISK, index,
                                         &interfaceData)) {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS) break;
            QE_LOG_WARN(L"SetupDiEnumDeviceInterfaces failed: %lu", error);
            return false;
        }

        DWORD detailSize = 0;
        SetupDiGetDeviceInterfaceDetailW(info.Get(), &interfaceData, nullptr, 0, &detailSize, nullptr);
        if (detailSize == 0) continue;
        std::vector<BYTE> detailBuffer(detailSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA deviceData{};
        deviceData.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiGetDeviceInterfaceDetailW(info.Get(), &interfaceData, detail, detailSize,
                                              &detailSize, &deviceData)) {
            QE_LOG_DEBUG(L"SetupDiGetDeviceInterfaceDetail failed: %lu", GetLastError());
            continue;
        }

        DiskCandidate disk;
        disk.diskInterface = detail->DevicePath;
        disk.devInst = deviceData.DevInst;

        WCHAR instanceId[512];
        if (SetupDiGetDeviceInstanceIdW(info.Get(), &deviceData, instanceId,
                                        static_cast<DWORD>(std::size(instanceId)), nullptr)) {
            disk.instanceId = instanceId;
        }
        disk.friendlyName = QueryDevicePropertyString(info.Get(), deviceData, DEVPKEY_Device_FriendlyName);
        disk.deviceDescription =
            QueryDevicePropertyString(info.Get(), deviceData, DEVPKEY_Device_DeviceDesc);

        // Zero access rights: enough for IOCTLs, never blocks the device.
        Handle device(CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, 0, nullptr));
        if (!device) {
            QE_LOG_DEBUG(L"cannot open %ls (%lu); interface skipped", detail->DevicePath,
                         GetLastError());
            continue;
        }
        DiskQuery query;
        if (!QueryDisk(device.Get(), query)) continue;
        disk.deviceNumber = query.deviceNumber;
        disk.storageBusType = query.storageBusType;
        disk.removableMedia = query.removableMedia;
        STORAGE_HOTPLUG_INFO hotplug{};
        hotplug.Size = sizeof(hotplug);
        DWORD bytes = 0;
        if (DeviceIoControl(device.Get(), IOCTL_STORAGE_GET_HOTPLUG_INFO, nullptr, 0,
                            &hotplug, sizeof(hotplug), &bytes, nullptr) &&
            bytes >= sizeof(hotplug) && hotplug.Size == sizeof(hotplug))
            disk.driverMediaRemovable = hotplug.MediaRemovable != FALSE;
        if (disk.driverMediaRemovable == true || disk.removableMedia) {
            if (DeviceIoControl(device.Get(), IOCTL_STORAGE_CHECK_VERIFY2, nullptr, 0, nullptr, 0, &bytes, nullptr))
                disk.mediaState = MediaState::Present;
            else {
                const DWORD error = GetLastError();
                if (error == ERROR_NO_MEDIA_IN_DRIVE) disk.mediaState = MediaState::Empty;
                else if (error == ERROR_NOT_READY) disk.mediaState = MediaState::NotReady;
            }
        }
        if (disk.storageBusType == 0xFFFFFFFF) {
            // The descriptor IOCTL failed. Some real removable devices do not
            // implement it, so the bus type stays unknown; the device is still
            // kept when a mounted volume reports DRIVE_REMOVABLE (see
            // AttachVolumes) rather than being dropped from the list.
            QE_LOG_WARN(L"no storage descriptor for %ls; bus type unknown",
                        detail->DevicePath);
        }
        disk.vendor = str::FromUtf8(query.vendor);
        disk.product = str::FromUtf8(query.product);

        disks.push_back(std::move(disk));
    }
    return true;
}

bool DeviceManager::AttachVolumes(std::vector<DiskCandidate>& disks) {
    WCHAR volumeName[MAX_PATH];
    // FindFirstVolume's handle must be closed with FindVolumeClose, so it is
    // deliberately not wrapped in the kernel-handle RAII type.
    HANDLE find = api_.findFirstVolume(volumeName, static_cast<DWORD>(std::size(volumeName)));
    if (find == INVALID_HANDLE_VALUE) {
        QE_LOG_WARN(L"FindFirstVolume failed: %lu", GetLastError());
        return false;
    }

    do {
        DWORD needed = 0;
        GetVolumePathNamesForVolumeNameW(volumeName, nullptr, 0, &needed);
        if (needed == 0) continue;
        std::vector<WCHAR> mountPoints(static_cast<size_t>(needed) + 2, L'\0');
        if (!GetVolumePathNamesForVolumeNameW(volumeName, mountPoints.data(), needed, &needed)) {
            continue;
        }

        if (!mountPoints[0]) continue;
        const UINT driveType = GetDriveTypeW(volumeName);
        if (driveType != DRIVE_REMOVABLE && driveType != DRIVE_FIXED) continue;

        // Without the trailing slash CreateFile opens the volume, not its root directory.
        std::wstring devicePath(volumeName);
        if (devicePath.empty() || devicePath.back() != L'\\') continue;
        devicePath.pop_back();
        Handle volume(CreateFileW(devicePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, 0, nullptr));
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        if (!volume || !DeviceIoControl(volume.Get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr,
                0, &number, sizeof(number), &returned, nullptr) ||
            returned < sizeof(number) || number.DeviceType != FILE_DEVICE_DISK) continue;

        for (auto& disk : disks) {
            if (disk.deviceNumber == 0xFFFFFFFF || disk.deviceNumber != number.DeviceNumber) continue;
            disk.volumeInterfaces.push_back(devicePath);
            if (driveType == DRIVE_REMOVABLE) disk.volumeIsRemovable = true;
            for (const WCHAR* mount = mountPoints.data(); *mount; mount += std::wcslen(mount) + 1) {
                disk.mountPaths.emplace_back(mount);
                if (!str::IsDriveLetterMount(mount)) continue;
                if (!disk.driveLetters.empty()) disk.driveLetters += L", ";
                disk.driveLetters.append(mount, 2);
            }
            WCHAR label[MAX_PATH + 1]{};
            if (disk.volumeLabel.empty() && GetVolumeInformationW(volumeName, label, MAX_PATH + 1,
                    nullptr, nullptr, nullptr, nullptr, 0)) disk.volumeLabel = label;
            ULARGE_INTEGER total{};
            if (GetDiskFreeSpaceExW(volumeName, nullptr, &total, nullptr)) disk.sizeBytes += total.QuadPart;
            break;
        }
    } while (api_.findNextVolume(find, volumeName, static_cast<DWORD>(std::size(volumeName))));
    const DWORD lastError = GetLastError();
    FindVolumeClose(find);
    if (lastError != ERROR_NO_MORE_FILES) {
        QE_LOG_WARN(L"FindNextVolume failed: %lu", lastError);
        return false;
    }
    return true;
}

// Ask the running Windows volume for every physical disk extent. A drive
// letter alone would miss another partition on the same USB disk.
std::vector<DWORD> WindowsVolumeDisks() {
    WCHAR windows[MAX_PATH]{}, root[MAX_PATH]{}, guid[MAX_PATH]{};
    const UINT length = GetSystemWindowsDirectoryW(windows, MAX_PATH);
    if (!length || length >= MAX_PATH || !GetVolumePathNameW(windows, root, MAX_PATH) ||
        !GetVolumeNameForVolumeMountPointW(root, guid, MAX_PATH)) return {};
    std::wstring path(guid);
    if (path.empty() || path.back() != L'\\') return {};
    path.pop_back();
    Handle volume(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr));
    if (!volume) return {};
    for (size_t count = 4; count <= 4096; count *= 2) {
        const auto size = offsetof(VOLUME_DISK_EXTENTS, Extents) + count * sizeof(DISK_EXTENT);
        std::vector<BYTE> buffer(size);
        DWORD returned = 0;
        if (!DeviceIoControl(volume.Get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                             buffer.data(), static_cast<DWORD>(size), &returned, nullptr)) {
            if (GetLastError() == ERROR_MORE_DATA) continue;
            return {};
        }
        const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
        if (returned < sizeof(VOLUME_DISK_EXTENTS) || !extents->NumberOfDiskExtents ||
            extents->NumberOfDiskExtents > (returned - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT))
            return {};
        std::vector<DWORD> disks;
        for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i)
            disks.push_back(extents->Extents[i].DiskNumber);
        return disks;
    }
    return {};
}

bool DeviceManager::Refresh() {
    std::vector<DiskCandidate> disks;
    if (!CollectDiskInterfaces(disks) || !AttachVolumes(disks)) return false;

    const auto windowsDisks = api_.windowsDisks();
    if (windowsDisks.empty()) {
        QE_LOG_WARN(L"cannot identify disks backing the running Windows volume");
        return false;
    }
    DeviceList next;
    next.reserve(disks.size());
    for (auto& disk : disks) {
        // A disk that reports no instance id cannot be matched against the
        // previous scan, so FindByInstanceId(L"") would collide with it and the
        // hot-plug balloons would be wrong. Skip it.
        if (disk.instanceId.empty()) {
            QE_LOG_DEBUG(L"skipping a disk interface without an instance id");
            continue;
        }
        if (disk.deviceNumber == 0xFFFFFFFF) continue;  // could not be identified

        if (std::find(windowsDisks.begin(), windowsDisks.end(), disk.deviceNumber) != windowsDisks.end()) continue;

        const BusType busType = ToBusType(disk.storageBusType);
        const bool isUsb = busType == BusType::Usb;
        // Unknown bus type is not a reason to hide the device: a disk whose
        // descriptor IOCTL failed is still kept when a mounted volume says it
        // is removable. Fixed internal disks (SATA/NVMe, non-removable) stay
        // out of the list.
        if (!isUsb && !disk.removableMedia && !disk.volumeIsRemovable) continue;

        Device device;
        device.instanceId = disk.instanceId;
        device.diskInterface = disk.diskInterface;
        device.volumeInterfaces = disk.volumeInterfaces;
        device.devInst = disk.devInst;
        device.deviceNumber = disk.deviceNumber;
        device.busType = busType;
        device.driverMediaRemovable = disk.driverMediaRemovable;
        device.mediaState = disk.mediaState;
        device.removable = disk.removableMedia || disk.volumeIsRemovable || isUsb;
        device.volumeLabel = disk.volumeLabel;
        device.driveLetters = disk.driveLetters;
        device.mountPaths = disk.mountPaths;
        device.sizeBytes = disk.sizeBytes;
        device.vendor = disk.vendor;
        device.product = disk.product;
        device.displayName = BuildDisplayName(disk);
        next.push_back(std::move(device));
    }

    devices_ = std::move(next);
    QE_LOG_DEBUG(L"device scan complete: %zu removable device(s)", devices_.size());
    if (callback_) callback_();
    return true;
}

std::vector<Device> DeviceManager::Devices() const { return devices_; }

std::optional<Device> DeviceManager::FindByDriveLetter(std::wstring_view driveLetter) const {
    if (driveLetter.size() != 2 || driveLetter[1] != L':') return std::nullopt;
    return FindByMountPath(devices_, driveLetter);
}

std::optional<Device> FindByMountPath(const DeviceList& devices, std::wstring_view input) {
    // Accept only a listed absolute mount point. Do not resolve ancestors, junctions
    // or relative paths: they can identify a different device than the user intended.
    auto key = [](std::wstring_view path) {
        std::wstring result(path);
        for (auto& ch : result) if (ch == L'/') ch = L'\\';
        if (result.size() < 3 || result[1] != L':' || result[2] != L'\\') return std::wstring{};
        if (result[0] >= L'a' && result[0] <= L'z') result[0] -= L'a' - L'A';
        if (result[0] < L'A' || result[0] > L'Z') return std::wstring{};
        if (result.size() > 3 && result.back() == L'\\') result.pop_back();
        return result;
    };
    const auto wanted = input.size() == 2 && input[1] == L':'
                            ? key(std::wstring(input) + L"\\") : key(input);
    if (wanted.empty()) return std::nullopt;
    const Device* found = nullptr;
    for (const auto& device : devices) {
        for (const auto& path : device.mountPaths) {
            if (key(path) != wanted) continue;
            if (found && found->instanceId != device.instanceId) return std::nullopt;
            found = &device;
        }
    }
    if (found) return *found;
    return std::nullopt;
}

std::optional<Device> DeviceManager::FindByInstanceId(std::wstring_view instanceId) const {
    for (const auto& device : devices_) {
        if (!instanceId.empty() && str::EqualsI(device.instanceId, instanceId)) return device;
    }
    return std::nullopt;
}

}  // namespace qe

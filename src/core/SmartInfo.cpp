#include "core/SmartInfo.h"

#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <vector>

#include "util/Win32.h"

namespace qe {
namespace {

uint64_t RawAttribute(const uint8_t* attribute) {
    uint64_t raw = 0;
    for (int byte = 0; byte < 6; ++byte) raw |= static_cast<uint64_t>(attribute[5 + byte]) << (8 * byte);
    return raw;
}

bool PlausibleTemperature(int value) { return value >= 0 && value <= 125; }

void Remember(SmartSnapshot& out, uint8_t id, uint64_t raw, uint8_t current) {
    switch (id) {
        case 194:
        case 190: {
            if (out.hasTemperature) return;
            int temperature = static_cast<int>(raw & 0xFF);
            if (!PlausibleTemperature(temperature)) temperature = current;
            if (!PlausibleTemperature(temperature)) return;
            out.hasTemperature = true;
            out.temperatureC = temperature;
            break;
        }
        case 9:
            if (!out.hasPowerOnHours) {
                out.hasPowerOnHours = true;
                out.powerOnHours = raw;
            }
            break;
        case 12:
            if (!out.hasPowerCycles) {
                out.hasPowerCycles = true;
                out.powerCycles = raw;
            }
            break;
        case 5:
            if (!out.hasReallocated) {
                out.hasReallocated = true;
                out.reallocated = raw;
            }
            break;
        case 197:
            if (!out.hasPending) {
                out.hasPending = true;
                out.pending = raw;
            }
            break;
        default:
            break;
    }
}

bool ReadTemperature(HANDLE disk, SmartSnapshot& out) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceTemperatureProperty;
    query.QueryType = PropertyStandardQuery;
    std::vector<uint8_t> buffer(sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR) + sizeof(STORAGE_TEMPERATURE_INFO) * 4);
    DWORD returned = 0;
    if (!DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                         static_cast<DWORD>(buffer.size()), &returned, nullptr) ||
        returned < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR))
        return false;
    const auto* descriptor = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer.data());
    if (descriptor->InfoCount == 0) return false;
    const auto temperature = descriptor->TemperatureInfo[0].Temperature;
    if (temperature == static_cast<SHORT>(STORAGE_TEMPERATURE_VALUE_NOT_REPORTED) ||
        !PlausibleTemperature(temperature))
        return false;
    out.hasTemperature = true;
    out.temperatureC = temperature;
    return true;
}

bool AtaSmartRead(HANDLE disk, uint8_t* data) {
    struct Request {
        SCSI_PASS_THROUGH_DIRECT pass;
        UCHAR sense[32];
    } request{};
    request.pass.Length = sizeof(request.pass);
    request.pass.CdbLength = 16;
    request.pass.SenseInfoLength = sizeof(request.sense);
    request.pass.DataIn = SCSI_IOCTL_DATA_IN;
    request.pass.DataTransferLength = 512;
    request.pass.TimeOutValue = 2;
    request.pass.DataBuffer = data;
    request.pass.SenseInfoOffset = offsetof(Request, sense);
    // ATA PASS-THROUGH (16), PIO data-in, one sector. SMART READ DATA.
    UCHAR* cdb = request.pass.Cdb;
    cdb[0] = 0x85;
    cdb[1] = 0x08;
    cdb[2] = 0x0E;
    cdb[4] = 0xD0;
    cdb[6] = 0x01;
    cdb[10] = 0x4F;
    cdb[12] = 0xC2;
    cdb[13] = 0xA0;
    cdb[14] = 0xB0;
    DWORD returned = 0;
    if (DeviceIoControl(disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, &request, sizeof(request), &request, sizeof(request),
                        &returned, nullptr) &&
        request.pass.ScsiStatus == 0)
        return true;

    // Some USB bridges only implement the 12-byte pass-through command.
    request = {};
    request.pass.Length = sizeof(request.pass);
    request.pass.CdbLength = 12;
    request.pass.SenseInfoLength = sizeof(request.sense);
    request.pass.DataIn = SCSI_IOCTL_DATA_IN;
    request.pass.DataTransferLength = 512;
    request.pass.TimeOutValue = 2;
    request.pass.DataBuffer = data;
    request.pass.SenseInfoOffset = offsetof(Request, sense);
    cdb = request.pass.Cdb;
    cdb[0] = 0xA1;
    cdb[1] = 0x08;
    cdb[2] = 0x0E;
    cdb[3] = 0xD0;
    cdb[4] = 0x01;
    cdb[6] = 0x4F;
    cdb[7] = 0xC2;
    cdb[8] = 0xA0;
    cdb[9] = 0xB0;
    returned = 0;
    return DeviceIoControl(disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, &request, sizeof(request), &request, sizeof(request),
                           &returned, nullptr) &&
           request.pass.ScsiStatus == 0;
}

}  // namespace

void ParseSmartAttributes(const uint8_t* data, size_t size, SmartSnapshot& out) {
    if (!data || size < 2 + 12) return;
    const int available = static_cast<int>((size - 2) / 12);
    const int count = available < 30 ? available : 30;
    for (int index = 0; index < count; ++index) {
        const uint8_t* attribute = data + 2 + index * 12;
        if (attribute[0] == 0) continue;
        Remember(out, attribute[0], RawAttribute(attribute), attribute[3]);
    }
}


SmartSnapshot ReadPath(const std::wstring& diskInterface) {
    SmartSnapshot snapshot;
    {
        Handle probe(CreateFileW(diskInterface.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr));
        if (probe) ReadTemperature(probe.Get(), snapshot);
    }
    // Read access works without elevation; a few USB bridges only answer a
    // pass-through on a writable handle, so retry that before giving up.
    Handle disk(CreateFileW(diskInterface.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr));
    if (!disk) {
        return snapshot;
    }
    if (!snapshot.hasTemperature) ReadTemperature(disk.Get(), snapshot);
    uint8_t data[512]{};
    if (AtaSmartRead(disk.Get(), data)) {
        ParseSmartAttributes(data, sizeof(data), snapshot);
        return snapshot;
    }
    {
        Handle writable(CreateFileW(diskInterface.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
        if (writable && AtaSmartRead(writable.Get(), data)) ParseSmartAttributes(data, sizeof(data), snapshot);
    }
    return snapshot;
}

void Merge(SmartSnapshot& into, const SmartSnapshot& from) {
    if (!into.hasTemperature && from.hasTemperature) {
        into.hasTemperature = true;
        into.temperatureC = from.temperatureC;
    }
    if (!into.hasPowerOnHours && from.hasPowerOnHours) {
        into.hasPowerOnHours = true;
        into.powerOnHours = from.powerOnHours;
    }
    if (!into.hasPowerCycles && from.hasPowerCycles) {
        into.hasPowerCycles = true;
        into.powerCycles = from.powerCycles;
    }
    if (!into.hasReallocated && from.hasReallocated) {
        into.hasReallocated = true;
        into.reallocated = from.reallocated;
    }
    if (!into.hasPending && from.hasPending) {
        into.hasPending = true;
        into.pending = from.pending;
    }
}

bool Useful(const SmartSnapshot& snapshot) {
    return snapshot.hasTemperature && snapshot.hasPowerOnHours;
}

SmartSnapshot ReadSmart(const std::wstring& diskInterface, const std::vector<std::wstring>& mountPaths,
                        unsigned long deviceNumber) {
    SmartSnapshot snapshot = ReadPath(diskInterface);
    if (Useful(snapshot)) return snapshot;
    for (const auto& mountPath : mountPaths) {
        // A folder mount under another drive resolves to that host volume; the
        // device-number check below rejects it. Only this disk's own drive
        // roots can answer SMART through \\.\X:.
        if (mountPath.size() < 2 || mountPath[1] != L':') continue;
        const wchar_t letter = mountPath[0];
        if (!((letter >= L'A' && letter <= L'Z') || (letter >= L'a' && letter <= L'z'))) continue;
        std::wstring path = L"\\\\.\\";
        path.push_back(letter);
        path.push_back(L':');
        Handle volume(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        if (volume && deviceNumber != 0xFFFFFFFF &&
            DeviceIoControl(volume.Get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number), &returned, nullptr) &&
            returned >= sizeof(number) && number.DeviceNumber == deviceNumber)
            Merge(snapshot, ReadPath(path));
        if (Useful(snapshot)) return snapshot;
    }
    return snapshot;
}

}  // namespace qe

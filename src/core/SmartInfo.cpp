#include "core/SmartInfo.h"

#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <cstddef>
#include <vector>

#include "app/Logger.h"
#include "util/Str.h"
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

bool ReadTemperature(HANDLE disk, SmartSnapshot& out, const SmartIo& io) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceTemperatureProperty;
    query.QueryType = PropertyStandardQuery;
    std::vector<uint8_t> buffer(sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR) + sizeof(STORAGE_TEMPERATURE_INFO) * 4);
    DWORD returned = 0;
    if (!io.control(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                    static_cast<DWORD>(buffer.size()), &returned, nullptr) ||
        returned < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR))
        return false;
    const auto* descriptor = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer.data());
    if (descriptor->InfoCount == 0) return false;
    // The descriptor describes itself: Version holds the header size and Size the
    // total payload. Only TemperatureInfo[0] is read, but the header is checked
    // and the entry is required to fit inside the bytes the IOCTL actually
    // returned, so a driver that reports a shorter descriptor than it fills
    // cannot make a later multi-sensor read walk off the payload.
    const size_t headerSize = sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR);
    const size_t firstEntry =
        offsetof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo) + sizeof(STORAGE_TEMPERATURE_INFO);
    if (descriptor->Version != 0 && descriptor->Version < headerSize) return false;
    if (descriptor->Size != 0 && descriptor->Size < headerSize) return false;
    const DWORD validBytes = descriptor->Size < returned ? descriptor->Size : returned;
    if (validBytes < firstEntry) return false;
    const auto temperature = descriptor->TemperatureInfo[0].Temperature;
    if (temperature == static_cast<SHORT>(STORAGE_TEMPERATURE_VALUE_NOT_REPORTED) ||
        !PlausibleTemperature(temperature))
        return false;
    out.hasTemperature = true;
    out.temperatureC = temperature;
    return true;
}

struct PassThroughRequest {
    SCSI_PASS_THROUGH_DIRECT pass;
    UCHAR sense[32];
};

// A failed pass-through leaves the reason in the sense buffer, and the driver
// only writes it when the offset and length it reports point inside the
// request, so both are checked before anything is decoded. A SATL that reports
// ATA PASS-THROUGH INFORMATION AVAILABLE (ASC 00h / ASCQ 1Dh) returns the ATA
// task file in the fixed-format sense information field [SAT-2 12.2.2.7]; every
// other shape, descriptor-format sense included, is dumped raw so a field
// report still shows what the device answered.
void LogSenseFailure(const PassThroughRequest& request, const wchar_t* command) {
    const size_t offset = request.pass.SenseInfoOffset;
    const size_t length = request.pass.SenseInfoLength;
    if (offset < sizeof(request.pass) || offset > sizeof(PassThroughRequest)) return;
    if (length == 0 || length > sizeof(PassThroughRequest) - offset) return;
    const UCHAR* sense = reinterpret_cast<const UCHAR*>(&request) + offset;
    if (length < 14) {
        QE_LOG_DEBUG(L"%ls failed: scsi status %u, %zu sense byte(s)", command,
                     static_cast<unsigned>(request.pass.ScsiStatus), length);
        return;
    }
    QE_LOG_DEBUG(L"%ls failed: scsi status %u, sense key %02X, asc %02X, ascq %02X", command,
                 static_cast<unsigned>(request.pass.ScsiStatus), sense[1] & 0x0F, sense[12], sense[13]);
    if (sense[12] == 0x00 && sense[13] == 0x1D) {
        QE_LOG_DEBUG(L"ATA error register %02X, status %02X, device %02X, count %02X", sense[3],
                     sense[4], sense[5], sense[6]);
        return;
    }
    std::wstring hex;
    for (size_t index = 0; index < length && index < 18; ++index)
        hex += str::Format(L"%02X ", sense[index]);
    QE_LOG_DEBUG(L"sense bytes: %ls", hex.c_str());
}

bool AtaSmartRead(HANDLE disk, uint8_t* data, const SmartIo& io) {
    PassThroughRequest request{};
    request.pass.Length = sizeof(request.pass);
    request.pass.CdbLength = 16;
    request.pass.SenseInfoLength = sizeof(request.sense);
    request.pass.DataIn = SCSI_IOCTL_DATA_IN;
    request.pass.DataTransferLength = 512;
    request.pass.TimeOutValue = 2;
    request.pass.DataBuffer = data;
    request.pass.SenseInfoOffset = offsetof(PassThroughRequest, sense);
    // ATA PASS-THROUGH (16), PIO data-in, one sector. SMART READ DATA:
    // features 0xD0, sector count 1, LBA low/mid/high 00/4F/C2, device 0xA0,
    // command 0xB0. The SMART magic is the cylinder 0xC24F in LBA mid/high plus
    // the LBA-mode device byte - shift it one register over and the disk sees a
    // plain read request and never answers.
    UCHAR* cdb = request.pass.Cdb;
    cdb[0] = 0x85;
    cdb[1] = 0x08;  // protocol 4 (PIO data-in), multiple count 0
    cdb[2] = 0x0E;  // length in sector count, 512-byte blocks, device to host
    cdb[4] = 0xD0;  // features
    cdb[6] = 0x01;  // sector count
    cdb[10] = 0x4F; // LBA mid (SMART cylinder low)
    cdb[12] = 0xC2; // LBA high (SMART cylinder high)
    cdb[13] = 0xA0; // device: LBA mode, head 0
    cdb[14] = 0xB0; // command
    DWORD returned = 0;
    const BOOL sent = io.control(disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, &request, sizeof(request),
                                 &request, sizeof(request), &returned, nullptr);
    if (sent && request.pass.ScsiStatus == 0) return true;
    if (sent)
        LogSenseFailure(request, L"ATA pass-through (16)");
    else
        QE_LOG_DEBUG(L"ATA pass-through (16) IOCTL failed: %lu", GetLastError());

    // Some USB bridges only implement the 12-byte pass-through command.
    request = {};
    request.pass.Length = sizeof(request.pass);
    request.pass.CdbLength = 12;
    request.pass.SenseInfoLength = sizeof(request.sense);
    request.pass.DataIn = SCSI_IOCTL_DATA_IN;
    request.pass.DataTransferLength = 512;
    request.pass.TimeOutValue = 2;
    request.pass.DataBuffer = data;
    request.pass.SenseInfoOffset = offsetof(PassThroughRequest, sense);
    cdb = request.pass.Cdb;
    cdb[0] = 0xA1;
    cdb[1] = 0x08;
    cdb[2] = 0x0E;
    cdb[3] = 0xD0;  // features
    cdb[4] = 0x01;  // sector count
    cdb[6] = 0x4F;  // LBA mid (SMART cylinder low)
    cdb[7] = 0xC2;  // LBA high (SMART cylinder high)
    cdb[8] = 0xA0;  // device: LBA mode, head 0
    cdb[9] = 0xB0;  // command
    returned = 0;
    const BOOL sent12 = io.control(disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, &request, sizeof(request),
                                   &request, sizeof(request), &returned, nullptr);
    if (sent12 && request.pass.ScsiStatus == 0) return true;
    if (sent12)
        LogSenseFailure(request, L"ATA pass-through (12)");
    else
        QE_LOG_DEBUG(L"ATA pass-through (12) IOCTL failed: %lu", GetLastError());
    return false;
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


SmartSnapshot ReadPath(const std::wstring& diskInterface, const SmartIo& io) {
    SmartSnapshot snapshot;
    {
        Handle probe(io.open(diskInterface.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr));
        if (probe) ReadTemperature(probe.Get(), snapshot, io);
    }
    // Read access works without elevation; a few USB bridges only answer a
    // pass-through on a writable handle, so retry that before giving up.
    Handle disk(io.open(diskInterface.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr));
    if (!disk) {
        return snapshot;
    }
    if (!snapshot.hasTemperature) ReadTemperature(disk.Get(), snapshot, io);
    uint8_t data[512]{};
    if (AtaSmartRead(disk.Get(), data, io)) {
        ParseSmartAttributes(data, sizeof(data), snapshot);
        return snapshot;
    }
    {
        Handle writable(io.open(diskInterface.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr));
        if (writable && AtaSmartRead(writable.Get(), data, io)) ParseSmartAttributes(data, sizeof(data), snapshot);
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
                        unsigned long deviceNumber, const SmartIo& io) {
    SmartSnapshot snapshot = ReadPath(diskInterface, io);
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
        Handle volume(io.open(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        if (volume && deviceNumber != 0xFFFFFFFF &&
            io.control(volume.Get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number), &returned,
                       nullptr) &&
            returned >= sizeof(number) && number.DeviceNumber == deviceNumber)
            Merge(snapshot, ReadPath(path, io));
        if (Useful(snapshot)) return snapshot;
    }
    return snapshot;
}

}  // namespace qe

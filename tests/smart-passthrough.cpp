// ATA pass-through tests: the CDB SmartInfo builds for SMART READ DATA.
//
// A configurable mock stands in for CreateFileW/DeviceIoControl - the same seam
// tests/media-partitions.cpp uses for the eject state machine - so the register
// placement of both ATA PASS-THROUGH commands can be asserted without a disk
// that answers SMART. The registers these guard carry the SMART magic: LBA mid
// 0x4F, LBA high 0xC2 and device 0xA0, at the offsets each command's own task
// file defines (16-byte: 10/12/13; 12-byte: 6/7/8, LBA low left zero). A
// misplaced register does not fail: the device simply never answers, so only
// the bytes themselves can catch a regression.
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/SmartInfo.h"

static int g_failures = 0;
static int g_checks = 0;
static void Check(bool condition, const char* what) {
    ++g_checks;
    if (condition) {
        printf("  PASS  %s\n", what);
    } else {
        ++g_failures;
        printf("  FAIL  %s\n", what);
    }
}

namespace {

constexpr size_t kSectorSize = 512;
constexpr unsigned kRootDisk = 9;  // what the mock reports for \\.\E:

// Cast first: GENERIC_READ/WRITE are 0x80000000L/0x40000000L, so OR-ing them
// yields a negative long that would not compare equal to a DWORD.
constexpr DWORD kReadOnly = static_cast<DWORD>(GENERIC_READ);
constexpr DWORD kReadWrite = static_cast<DWORD>(GENERIC_READ) | static_cast<DWORD>(GENERIC_WRITE);

// ATA PASS-THROUGH (16), PIO data-in, one sector, SMART READ DATA: opcode 0x85,
// protocol 4 (PIO data-in) and multiple count 0 in byte 1, the length/type flags
// in byte 2, then the ATA task file at the 16-byte command's own register
// offsets - features 4, sector count 6, LBA low/mid/high 8/10/12, device 13,
// command 14. LBA low stays zero; the SMART magic is the cylinder 0xC24F in LBA
// mid/high plus the LBA-mode device byte.
const std::array<uint8_t, 16> kCdb16 = {
    0x85, 0x08, 0x0E, 0x00, 0xD0, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x4F, 0x00, 0xC2, 0xA0, 0xB0, 0x00};

// ATA PASS-THROUGH (12) is the same request one register earlier: features 3,
// sector count 4, LBA low/mid/high 5/6/7, device 8, command 9.
const std::array<uint8_t, 16> kCdb12 = {
    0xA1, 0x08, 0x0E, 0xD0, 0x01, 0x00, 0x4F, 0xC2, 0xA0, 0xB0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// One 12-byte ATA attribute at `index` of a SMART READ DATA payload.
void SetAttribute(uint8_t* sector, size_t index, uint8_t id, uint8_t current, uint64_t raw) {
    uint8_t* attribute = sector + 2 + index * 12;
    attribute[0] = id;
    attribute[3] = current;
    for (int byte = 0; byte < 6; ++byte)
        attribute[5 + byte] = static_cast<uint8_t>(raw >> (8 * byte));
}

void FillSector(uint8_t* sector) {
    memset(sector, 0, kSectorSize);
    SetAttribute(sector, 0, 194, 0, 41);    // temperature
    SetAttribute(sector, 1, 9, 0, 1234);    // power-on hours
    SetAttribute(sector, 2, 12, 0, 56);     // power cycles
}

// Everything a pass-through request carried, so a failing check can name what
// was actually sent instead of only that something differed.
struct Sent {
    int handle = -1;      // which io.open call produced the handle it was sent on
    int cdbLength = 0;
    unsigned dataIn = 0;
    unsigned transferLength = 0;
    unsigned timeOut = 0;
    unsigned senseInfoLength = 0;
    unsigned senseInfoOffset = 0;
    std::array<uint8_t, 16> cdb{};
};

struct Config {
    bool temperature = false;      // IOCTL_STORAGE_QUERY_PROPERTY answers
    int temperatureC = 0;
    bool fail16 = false;           // the 16-byte command reports an IOCTL error
    bool fail12 = false;           // the 12-byte command reports an IOCTL error
    unsigned scsiStatus = 0;       // a status the IOCTL itself reports as sent
    bool rejectWritable = false;   // CreateFileW with write access fails
    bool writableOnly = false;     // only a writable handle answers a pass-through
    std::wstring openPrefix;       // only paths with this prefix open (empty = all)
};

Config g;
std::vector<Sent> g_sent;
std::vector<HANDLE> g_handles;   // the handles io.open handed out, in order
std::vector<DWORD> g_access;     // the access mask of each of those handles
uint8_t g_sector[kSectorSize];

void Reset(Config config = {}) {
    g = config;
    g_sent.clear();
    g_handles.clear();
    g_access.clear();
    FillSector(g_sector);
}

const Sent* At(size_t index) { return index < g_sent.size() ? &g_sent[index] : nullptr; }

// Which io.open call produced this handle, so a control call can see the access
// mask its own handle was opened with.
int WhichHandle(HANDLE handle) {
    for (size_t i = 0; i < g_handles.size(); ++i)
        if (g_handles[i] == handle) return static_cast<int>(i);
    return -1;
}

HANDLE WINAPI Open(LPCWSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    if (!g.openPrefix.empty() && std::wstring(path).rfind(g.openPrefix, 0) != 0)
        return INVALID_HANDLE_VALUE;
    if ((access & GENERIC_WRITE) != 0 && g.rejectWritable) return INVALID_HANDLE_VALUE;
    // A real event keeps qe::Handle's CloseHandle honest; the mock only uses
    // the value as a token.
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (handle == nullptr) return INVALID_HANDLE_VALUE;
    g_handles.push_back(handle);
    g_access.push_back(access);
    return handle;
}

BOOL WINAPI Control(HANDLE handle, DWORD code, LPVOID in, DWORD, LPVOID out, DWORD size, LPDWORD bytes,
                    LPOVERLAPPED) {
    *bytes = size;
    if (code == IOCTL_STORAGE_QUERY_PROPERTY) {
        if (!g.temperature) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
        auto* descriptor = static_cast<STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(out);
        memset(descriptor, 0, size);
        // A descriptor that describes itself: Version is the header size and
        // Size the whole payload, the shape ReadTemperature requires.
        descriptor->Version = static_cast<DWORD>(sizeof(*descriptor));
        descriptor->Size = static_cast<DWORD>(sizeof(*descriptor) + sizeof(STORAGE_TEMPERATURE_INFO));
        descriptor->InfoCount = 1;
        descriptor->TemperatureInfo[0].Temperature = static_cast<SHORT>(g.temperatureC);
        return TRUE;
    }
    if (code == IOCTL_SCSI_PASS_THROUGH_DIRECT) {
        auto* pass = static_cast<SCSI_PASS_THROUGH_DIRECT*>(in);
        const int index = WhichHandle(handle);
        Sent sent;
        sent.handle = index;
        sent.cdbLength = pass->CdbLength;
        sent.dataIn = pass->DataIn;
        sent.transferLength = pass->DataTransferLength;
        sent.timeOut = pass->TimeOutValue;
        sent.senseInfoLength = pass->SenseInfoLength;
        sent.senseInfoOffset = pass->SenseInfoOffset;
        for (int i = 0; i < pass->CdbLength && i < 16; ++i) sent.cdb[i] = pass->Cdb[i];
        g_sent.push_back(sent);
        const bool twelve = pass->CdbLength == 12;
        if (twelve ? g.fail12 : g.fail16) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
        if (g.scsiStatus != 0) {
            // The IOCTL was delivered; the device reported the status and left
            // the reason in the sense buffer.
            pass->ScsiStatus = static_cast<UCHAR>(g.scsiStatus);
            return TRUE;
        }
        if (index >= 0 && g.writableOnly && (g_access[index] & GENERIC_WRITE) == 0) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        // The driver fills the caller's buffer with the sector it read.
        memcpy(pass->DataBuffer, g_sector, kSectorSize);
        pass->ScsiStatus = 0;
        return TRUE;
    }
    if (code == IOCTL_STORAGE_GET_DEVICE_NUMBER) {
        auto* number = static_cast<STORAGE_DEVICE_NUMBER*>(out);
        number->DeviceType = FILE_DEVICE_DISK;
        number->DeviceNumber = kRootDisk;
        number->PartitionNumber = 0xFFFFFFFFu;
        return TRUE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

std::string Describe(const Sent& sent) {
    static const char digits[] = "0123456789ABCDEF";
    std::string text = "cdb(" + std::to_string(sent.cdbLength) + ") ";
    for (int i = 0; i < sent.cdbLength && i < 16; ++i) {
        text += digits[sent.cdb[i] >> 4];
        text += digits[sent.cdb[i] & 0x0F];
        text += ' ';
    }
    return text;
}

// The message keeps the bytes that were actually sent, so a broken layout shows
// which register moved instead of a bare "expected != actual".
void CheckCdb(size_t index, const std::array<uint8_t, 16>& expected, int length, const char* what) {
    static std::string detail;
    const Sent* sent = At(index);
    const bool ok = sent != nullptr && sent->cdbLength == length && sent->cdb == expected;
    detail = what;
    if (sent != nullptr) detail += " [sent " + Describe(*sent) + "]";
    else detail += " [no such request]";
    Check(ok, detail.c_str());
}

}  // namespace

int main() {
    const qe::SmartIo io{Open, Control};
    const std::wstring kInterface = L"\\\\.\\PhysicalDrive1";

    printf("ATA pass-through CDB\n");

    // 1. The 16-byte command and the request around it.
    Reset();
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(g_sent.size() == 1, "the disk interface is asked for SMART exactly once");
        const Sent* sent = At(0);
        Check(sent != nullptr, "the pass-through request was recorded");
        CheckCdb(0, kCdb16, 16, "the 16-byte CDB carries the ATA PASS-THROUGH (16) register layout");
        if (sent != nullptr) {
            Check(sent->dataIn == static_cast<unsigned>(SCSI_IOCTL_DATA_IN) && sent->transferLength == kSectorSize,
                  "the request is PIO data-in of one 512-byte sector");
            Check(sent->senseInfoOffset == sizeof(SCSI_PASS_THROUGH_DIRECT) && sent->senseInfoLength == 32u,
                  "the sense buffer sits directly after the request header");
            Check(sent->timeOut == 2u, "the pass-through times out after two seconds");
        }
        Check(snap.hasTemperature && snap.temperatureC == 41,
              "the sector the device returned decodes its temperature");
        Check(snap.hasPowerOnHours && snap.powerOnHours == 1234,
              "power-on hours decode from the pass-through sector");
        Check(snap.hasPowerCycles && snap.powerCycles == 56,
              "power cycles decode from the pass-through sector");
    }

    // 2. A 16-byte command the device refuses is retried as the 12-byte one.
    Reset();
    g.fail16 = true;
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(g_sent.size() == 2, "a refused 16-byte command is retried as the 12-byte one");
        CheckCdb(1, kCdb12, 12, "the 12-byte CDB carries the ATA PASS-THROUGH (12) register layout");
        Check(At(1) != nullptr && At(1)->handle == At(0)->handle,
              "the 12-byte retry uses the same handle");
        Check(snap.hasTemperature && snap.temperatureC == 41,
              "the 12-byte fallback produces the same reading as the 16-byte one");
        Check(snap.hasPowerOnHours && snap.powerOnHours == 1234,
              "power-on hours decode through the 12-byte command");
    }

    // 3. A status the IOCTL reports as sent takes the same fallback, and the
    //    sense decoding it triggers must survive a zeroed sense buffer. Nothing
    //    was read, so ReadPath goes on to retry on a writable handle - the path
    //    case 4 asserts - which is why only the first two commands are pinned
    //    here instead of the total count.
    Reset();
    g.scsiStatus = 0x02;  // CHECK CONDITION
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(At(0) != nullptr && At(1) != nullptr && At(0)->handle == At(1)->handle &&
                  At(0)->cdbLength == 16 && At(1)->cdbLength == 12,
              "a CHECK CONDITION status also falls back to the 12-byte command");
        Check(!snap.hasPowerOnHours, "a device reporting CHECK CONDITION yields no SMART data");
    }

    // 4. Both commands failing on the read-only handle is retried on a writable
    //    one, because a few USB bridges only answer there.
    Reset();
    g.fail16 = true;
    g.fail12 = true;
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(g_sent.size() == 4, "both commands are retried on a writable handle before giving up");
        Check(g_access.size() == 3 && g_access[0] == 0 && g_access[1] == kReadOnly &&
                  g_access[2] == kReadWrite,
              "the interface is opened for probing, reading and then writing");
        Check(!snap.hasPowerOnHours, "a device answering no command at all yields an empty snapshot");
    }

    // 5. A writable handle that will not open ends the retry there.
    Reset();
    g.fail16 = true;
    g.fail12 = true;
    g.rejectWritable = true;
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(g_sent.size() == 2, "a refused writable open is not retried");
        Check(g_access.size() == 2, "only the probe and read-only handles were opened");
        Check(!snap.hasPowerOnHours, "a refused writable handle still yields an empty snapshot");
    }

    // 6. A bridge that answers only on a writable handle still produces a
    //    reading, and repeats the same 16-byte command on that handle.
    Reset();
    g.writableOnly = true;
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(g_sent.size() == 3, "a writable-only bridge is tried on the read-only handle first");
        Check(At(2) != nullptr && At(2)->handle == 2,
              "the third attempt is the one on the writable handle");
        CheckCdb(2, kCdb16, 16, "the writable retry repeats the 16-byte command unchanged");
        Check(snap.hasTemperature && snap.temperatureC == 41,
              "the writable handle's sector decodes its temperature");
        Check(snap.hasPowerOnHours && snap.powerOnHours == 1234,
              "power-on hours decode from the writable handle's sector");
    }

    // 7. When the SetupAPI interface path does not open, the drive root is
    //    tried instead - several USB bridges only answer SMART there.
    Reset();
    g.openPrefix = L"\\\\.\\E:";
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {L"E:\\"}, kRootDisk, io);
        Check(g_sent.size() == 1, "the drive root is asked for SMART when the interface does not open");
        CheckCdb(0, kCdb16, 16, "the drive root gets the same 16-byte command");
        Check(snap.hasTemperature && snap.temperatureC == 41, "the drive root's sector decodes");
        Check(snap.hasPowerOnHours && snap.powerOnHours == 1234,
              "power-on hours survive the drive-root path");
    }

    // 8. A drive root that turns out to belong to another disk is not read.
    Reset();
    g.openPrefix = L"\\\\.\\E:";
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {L"E:\\"}, 4, io);
        Check(g_sent.empty(), "a drive root belonging to another disk is not asked for SMART");
        Check(!snap.hasPowerOnHours, "a mismatched device number yields an empty snapshot");
    }

    // 9. The temperature IOCTL and the pass-through are independent: a device
    //    that answers only the temperature still gets the SMART command.
    Reset();
    g.temperature = true;
    g.temperatureC = 38;
    {
        const qe::SmartSnapshot snap = qe::ReadSmart(kInterface, {}, 0xFFFFFFFF, io);
        Check(snap.hasTemperature && snap.temperatureC == 38, "the temperature IOCTL is decoded");
        Check(g_sent.size() == 1, "a reported temperature does not replace the SMART read");
        Check(snap.hasPowerOnHours && snap.powerOnHours == 1234, "both readings end up in one snapshot");
    }

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

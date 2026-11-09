// Multi-partition media-eject tests: the partition/volume matching rules.
//
// A configurable mock stands in for CreateFileW/DeviceIoControl so the eject
// state machine can be driven through layouts that are impractical to build on
// real hardware (incomplete volume sets, partitions that never carry a volume,
// spanned volumes, a mid-eject disk-number change). The default configuration
// reproduces a plain two-partition GPT card; each scenario tweaks only what it
// exercises.
#include "core/MediaEject.h"
#include <winioctl.h>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

HANDLE opened[4]{};
int locks = 0;
int ejections = 0;

struct Partition {
    LONGLONG offset = 0;
    LONGLONG length = 0;
    BYTE mbrType = 0x07;
    // On a GPT card this is the partition-type GUID the mock reports; nullptr
    // means an ordinary data partition. Anything else is a specific type the
    // eject logic treats as needing no volume (Microsoft reserved, EFI system,
    // ...) - though a volume may still appear on one, which is scenario 16.
    const GUID* gptType = nullptr;
};
struct Volume {
    bool present = true;
    int diskNumber = 7;
    LONGLONG offset = 4096;
    LONGLONG length = 4096;
    int extentCount = 1;
};
struct Config {
    int diskDeviceNumber = 7;
    PARTITION_STYLE partitionStyle = PARTITION_STYLE_GPT;
    Partition parts[4] = {{4096, 4096}, {8192, 4096}, {0, 0}, {0, 0}};
    int partCount = 2;
    Volume vols[3];              // opened[1], opened[2], opened[3]
    int lockFailIndex = -1;      // volume index whose FSCTL_LOCK_VOLUME fails
    bool ejectFail = false;
};
Config g;

// The partition-type GUIDs MediaEject.cpp skips, mirrored here so the mock can
// build cards that carry them. The LDM data GUID is deliberately not one of
// them: dynamic volumes have drive letters and must still be matched.
const GUID kMicrosoftReserved = {0xE3C9E316, 0x0B5C, 0x4DB8,
                                 {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}};
const GUID kEfiSystemPartition = {0xC12A7328, 0xF81F, 0x11D2,
                                  {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
const GUID kLdmMetadata = {0x5808C8AA, 0x7E8F, 0x42E0,
                           {0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3}};
const GUID kLdmData = {0xAF9B60A0, 0x1431, 0x4F62,
                       {0xB7, 0xB6, 0x70, 0xD0, 0xA1, 0xF7, 0xE5, 0xD1}};

void ResetConfig() {
    g = Config{};
    g.vols[1].offset = 8192;     // volume "2" sits at the second partition
}

int WhichHandle(HANDLE h) {
    for (int i = 0; i < 4; ++i)
        if (opened[i] == h) return i;
    return -1;
}

HANDLE WINAPI Open(LPCWSTR path, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    int i = path[0] - L'0';
    if (i < 0 || i > 3) return INVALID_HANDLE_VALUE;
    return opened[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

BOOL WINAPI Control(HANDLE h, DWORD code, LPVOID, DWORD, LPVOID out, DWORD size, LPDWORD bytes, LPOVERLAPPED) {
    *bytes = size;
    if (code == IOCTL_STORAGE_GET_DEVICE_NUMBER) {
        *static_cast<STORAGE_DEVICE_NUMBER*>(out) = {FILE_DEVICE_DISK, (DWORD)g.diskDeviceNumber, 0};
        return TRUE;
    }
    if (code == IOCTL_DISK_GET_DRIVE_LAYOUT_EX) {
        auto* layout = static_cast<DRIVE_LAYOUT_INFORMATION_EX*>(out);
        layout->PartitionStyle = g.partitionStyle;
        layout->PartitionCount = g.partCount;
        for (int i = 0; i < g.partCount && i < 4; ++i) {
            auto& p = layout->PartitionEntry[i];
            p.PartitionStyle = g.partitionStyle;
            p.StartingOffset.QuadPart = g.parts[i].offset;
            p.PartitionLength.QuadPart = g.parts[i].length;
            if (g.partitionStyle == PARTITION_STYLE_MBR) p.Mbr.PartitionType = g.parts[i].mbrType;
            else if (g.parts[i].gptType) p.Gpt.PartitionType = *g.parts[i].gptType;
        }
        return TRUE;
    }
    if (code == IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS) {
        int vi = WhichHandle(h) - 1;
        if (vi < 0 || vi > 2 || !g.vols[vi].present) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
        auto* ext = static_cast<VOLUME_DISK_EXTENTS*>(out);
        ext->NumberOfDiskExtents = g.vols[vi].extentCount;
        ext->Extents[0].DiskNumber = g.vols[vi].diskNumber;
        ext->Extents[0].StartingOffset.QuadPart = g.vols[vi].offset;
        ext->Extents[0].ExtentLength.QuadPart = g.vols[vi].length;
        return TRUE;
    }
    if (code == FSCTL_LOCK_VOLUME) {
        if (g.lockFailIndex == WhichHandle(h) - 1) { SetLastError(ERROR_SHARING_VIOLATION); return FALSE; }
        ++locks;
        return TRUE;
    }
    if (code == IOCTL_STORAGE_EJECT_MEDIA) {
        if (g.ejectFail) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
        ++ejections;
        return TRUE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

}  // namespace

static int g_failures = 0;
static int g_checks = 0;
static void Check(bool condition, const char* what) {
    ++g_checks;
    if (condition) printf("  PASS  %s\n", what);
    else { ++g_failures; printf("  FAIL  %s\n", what); }
}

// EjectMedia closes every handle it opens (RAII), so after it returns the
// recorded handle values must all be invalid. Anything still open is a leak.
static bool AnyHandleOpen() {
    for (HANDLE h : opened) {
        DWORD flags;
        if (h && (GetHandleInformation(h, &flags) || GetLastError() != ERROR_INVALID_HANDLE)) return true;
    }
    return false;
}
static void ResetHandles() { for (HANDLE& h : opened) h = nullptr; }

// A removable card whose volumes are the interfaces "1".."N" the mock indexes.
static qe::Device Card(int volumeCount) {
    qe::Device d;
    d.diskInterface = L"0";
    d.deviceNumber = 7;
    d.driverMediaRemovable = true;
    for (int i = 1; i <= volumeCount; ++i) d.volumeInterfaces.push_back(std::to_wstring(i));
    return d;
}

int main() {
    // 1. A single volume against a two-partition card cannot cover every
    //    partition, so the set is reported incomplete before any lock.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(std::wstring_view(r.stage) == L"incomplete_volume_set" && locks == 0 && ejections == 0,
              "a single volume on a two-partition card reports an incomplete set");
        Check(!AnyHandleOpen(), "no handle leaks after an incomplete set");
    }

    // 2. The second volume is busy, so the second lock fails.
    ResetHandles(); ResetConfig(); g.lockFailIndex = 1; locks = ejections = 0;
    {
        auto r = qe::EjectMedia(Card(2), {Open, Control});
        Check(r.error == ERROR_SHARING_VIOLATION && std::wstring_view(r.stage) == L"lock_volume" &&
              locks == 1 && ejections == 0,
              "a busy second volume fails the lock with ERROR_SHARING_VIOLATION");
        Check(!AnyHandleOpen(), "no handle leaks after a failed lock");
    }

    // 3. A complete, quiet card locks both volumes and ejects once.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        auto r = qe::EjectMedia(Card(2), {Open, Control});
        Check(r.error == ERROR_SUCCESS && std::wstring_view(r.stage) == L"media_ejected" &&
              locks == 2 && ejections == 1,
              "a complete two-volume card locks both, ejects once and succeeds");
        Check(!AnyHandleOpen(), "no handle leaks after a successful eject");
    }

    // 4. Non-removable media is refused before any I/O.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        qe::Device d = Card(1);
        d.driverMediaRemovable = false;
        auto r = qe::EjectMedia(d, {Open, Control});
        Check(std::wstring_view(r.stage) == L"unsupported_media" && r.error == ERROR_NOT_SUPPORTED,
              "non-removable media is refused before any I/O");
        Check(!AnyHandleOpen(), "a refused device opens no handles");
    }

    // 5. A device with no volumes is refused.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        auto r = qe::EjectMedia(Card(0), {Open, Control});
        Check(std::wstring_view(r.stage) == L"unsupported_media",
              "a device with no volumes is refused");
    }

    // 6. An empty slot and a not-ready slot are distinguished.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        qe::Device d = Card(1);
        d.mediaState = qe::MediaState::Empty;
        auto r = qe::EjectMedia(d, {Open, Control});
        Check(std::wstring_view(r.stage) == L"no_media" && r.error == ERROR_NO_MEDIA_IN_DRIVE,
              "an empty slot reports no_media");
    }
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        qe::Device d = Card(1);
        d.mediaState = qe::MediaState::NotReady;
        auto r = qe::EjectMedia(d, {Open, Control});
        Check(std::wstring_view(r.stage) == L"not_ready" && r.error == ERROR_NOT_READY,
              "a not-ready slot reports not_ready");
    }

    // 7. The disk number changed under us -> disk_changed.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        qe::Device d = Card(1);
        d.deviceNumber = 3;   // the mock still reports disk 7
        auto r = qe::EjectMedia(d, {Open, Control});
        Check(std::wstring_view(r.stage) == L"disk_changed" && r.error == ERROR_INVALID_DATA,
              "a disk-number mismatch reports disk_changed");
        Check(!AnyHandleOpen(), "no handle leaks after a disk-number mismatch");
    }

    // 8. A Microsoft-reserved GPT partition needs no volume; the card still ejects.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partCount = 2;
        g.parts[0] = {2048, 2048, 0x07, &kMicrosoftReserved};    // reserved -> skipped
        g.parts[1] = {4096, 4096, 0x07, nullptr};                // the only real partition
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(r.error == ERROR_SUCCESS && ejections == 1,
              "a Microsoft-reserved GPT partition needs no volume and the card still ejects");
        Check(!AnyHandleOpen(), "no handle leaks after a reserved-partition eject");
    }

    // 9. A volume extent that matches no partition -> volume_changed.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.vols[0].offset = 9999;
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(std::wstring_view(r.stage) == L"volume_changed" && r.error == ERROR_INVALID_DATA,
              "a volume extent matching no partition reports volume_changed");
    }

    // 10. A volume on another disk -> volume_changed.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.vols[0].diskNumber = 99;
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(std::wstring_view(r.stage) == L"volume_changed",
              "a volume on another disk reports volume_changed");
    }

    // 11. A volume spanning multiple extents -> volume_multi_extent.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.vols[0].extentCount = 2;
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(std::wstring_view(r.stage) == L"volume_multi_extent" && r.error == ERROR_NOT_SUPPORTED,
              "a volume spanning multiple extents reports volume_multi_extent");
    }

    // 12. MBR: unused and extended-container entries need no volume.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partitionStyle = PARTITION_STYLE_MBR;
        g.partCount = 3;
        g.parts[0] = {0, 0, PARTITION_ENTRY_UNUSED, nullptr};    // unused -> skipped
        g.parts[1] = {2048, 2048, 0x0F, nullptr};                // extended container -> skipped
        g.parts[2] = {4096, 4096, 0x07, nullptr};                // the real partition
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(r.error == ERROR_SUCCESS && ejections == 1,
              "MBR unused and extended-container entries need no volume and the card still ejects");
    }

    // 13. An EFI system partition holds the boot loader only: by default it gets
    //     no drive letter and stays hidden from Explorer, so no volume is
    //     reported for it.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partCount = 2;
        g.parts[0] = {2048, 2048, 0x07, &kEfiSystemPartition};   // boot partition -> skipped
        g.parts[1] = {4096, 4096, 0x07, nullptr};                // the only mounted partition
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(r.error == ERROR_SUCCESS && std::wstring_view(r.stage) == L"media_ejected" && ejections == 1,
              "an EFI system partition needs no volume and the card still ejects");
        Check(!AnyHandleOpen(), "no handle leaks after an EFI-partition eject");
    }

    // 14. An LDM metadata partition stores the dynamic-disk database and is
    //     never mounted, so it needs no volume either.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partCount = 2;
        g.parts[0] = {2048, 2048, 0x07, &kLdmMetadata};          // metadata -> skipped
        g.parts[1] = {4096, 4096, 0x07, nullptr};                // the only mounted partition
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(r.error == ERROR_SUCCESS && std::wstring_view(r.stage) == L"media_ejected" && ejections == 1,
              "an LDM metadata partition needs no volume and the card still ejects");
        Check(!AnyHandleOpen(), "no handle leaks after an LDM-metadata eject");
    }

    // 15. The LDM *data* partition is not skipped: the dynamic volumes inside it
    //     do have drive letters, so an uncovered one still fails the card.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partCount = 3;
        g.parts[0] = {2048, 2048, 0x07, &kLdmMetadata};          // metadata -> skipped
        g.parts[1] = {4096, 4096, 0x07, &kLdmData};              // dynamic volume -> needs one
        g.parts[2] = {8192, 4096, 0x07, nullptr};                // the mounted partition
        g.vols[0].offset = 8192;                                // the only volume sits on part 2
        auto r = qe::EjectMedia(Card(1), {Open, Control});
        Check(std::wstring_view(r.stage) == L"incomplete_volume_set" && r.error == ERROR_NOT_SUPPORTED &&
              locks == 0 && ejections == 0,
              "an uncovered LDM data partition still reports an incomplete set");
    }

    // 16. A user can give the EFI system partition a drive letter or a folder
    //     mount point. A volume is then reported for it, and its extent has to
    //     match the ESP partition instead of failing the card as a change.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partCount = 2;
        g.parts[0] = {2048, 2048, 0x07, &kEfiSystemPartition};   // ESP, given a drive letter
        g.parts[1] = {4096, 4096, 0x07, nullptr};                // the data partition
        g.vols[0].offset = 2048;                                 // volume "1" is the ESP's
        g.vols[0].length = 2048;
        g.vols[1].offset = 4096;                                 // volume "2" is the data partition's
        g.vols[1].length = 4096;
        auto r = qe::EjectMedia(Card(2), {Open, Control});
        Check(r.error == ERROR_SUCCESS && std::wstring_view(r.stage) == L"media_ejected" &&
              locks == 2 && ejections == 1,
              "a mounted EFI system partition maps to its volume and the card still ejects");
        Check(!AnyHandleOpen(), "no handle leaks after a mounted-EFI eject");
    }

    // 17. ...but a volume on a range that is neither a data partition nor a
    //     skipped one is still a change, mounted ESP or not.
    ResetHandles(); ResetConfig(); locks = ejections = 0;
    {
        g.partCount = 2;
        g.parts[0] = {2048, 2048, 0x07, &kEfiSystemPartition};   // ESP with a volume
        g.parts[1] = {4096, 4096, 0x07, nullptr};                // the data partition
        g.vols[0].offset = 2048;
        g.vols[0].length = 2048;
        g.vols[1].offset = 4096;
        g.vols[1].length = 4096;
        g.vols[2].offset = 9999;                                 // matches no partition at all
        auto r = qe::EjectMedia(Card(3), {Open, Control});
        Check(std::wstring_view(r.stage) == L"volume_changed" && r.error == ERROR_INVALID_DATA &&
              locks == 0 && ejections == 0,
              "a volume matching neither a data nor an EFI partition still reports volume_changed");
    }

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

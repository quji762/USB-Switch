#pragma once
// SMART readings for a removable disk. Missing fields stay unset; many USB
// flash drives and memory cards do not report any of them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qe {

struct SmartSnapshot {
    bool hasTemperature = false;
    int temperatureC = 0;
    bool hasPowerOnHours = false;
    uint64_t powerOnHours = 0;
    bool hasPowerCycles = false;
    uint64_t powerCycles = 0;
    bool hasReallocated = false;
    uint64_t reallocated = 0;
    bool hasPending = false;
    uint64_t pending = 0;
};

// Interprets a 512-byte ATA SMART READ DATA payload. Values already present in
// out are kept when the attribute table does not contain that field.
void ParseSmartAttributes(const uint8_t* data, size_t size, SmartSnapshot& out);

// Opens the disk interface only long enough to ask for temperature and ATA
// SMART data. Failure leaves the snapshot empty.
// diskInterface is the SetupAPI path. Drive roots inside mountPaths are tried
// as \\.\X: because several USB bridges only answer SMART there.
SmartSnapshot ReadSmart(const std::wstring& diskInterface, const std::vector<std::wstring>& mountPaths,
                        unsigned long deviceNumber);

}  // namespace qe

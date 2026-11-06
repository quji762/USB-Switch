#pragma once

#include "core/Device.h"
#include "util/Json.h"
#include "util/Str.h"

namespace qe {

inline json::Value DeviceJson(const Device& device) {
    using json::Value;
    Value row = Value::Object();
    row.Set(L"instanceId", Value::String(device.instanceId));
    row.Set(L"displayName", Value::String(device.displayName));
    row.Set(L"volumeLabel", Value::String(device.volumeLabel));
    row.Set(L"bus", Value::String(BusTypeName(device.busType)));
    row.Set(L"removable", Value::Bool(device.removable));
    row.Set(L"ejectable", Value::Bool(device.CanEject()));
    // A decimal string retains the full uint64_t range in scripting runtimes.
    row.Set(L"mountedVolumeBytes", device.sizeBytes ? Value::String(std::to_wstring(device.sizeBytes))
                                                   : Value::Null());
    Value drives = Value::Array();
    std::wstring_view remaining = device.driveLetters;
    while (!remaining.empty()) {
        const auto comma = remaining.find(L',');
        drives.Push(Value::String(str::Trim(remaining.substr(0, comma))));
        if (comma == remaining.npos) break;
        remaining.remove_prefix(comma + 1);
    }
    row.Set(L"driveLetters", std::move(drives));
    return row;
}

}  // namespace qe

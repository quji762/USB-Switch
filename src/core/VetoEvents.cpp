#include "core/VetoEvents.h"
#include <winevt.h>
#include <algorithm>
#include <memory>
#include "util/Str.h"

namespace qe {
namespace {
struct CloseEvent { void operator()(void* h) const { EvtClose(h); } };
using Event = std::unique_ptr<void, CloseEvent>;
ULONGLONG Ticks(FILETIME t) {
    return (static_cast<ULONGLONG>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
}

std::vector<ReportedProcess> ReadVetoEvents(const std::wstring& disk, const std::wstring& target, FILETIME start, FILETIME end) {
    std::vector<ReportedProcess> result;
    LPCWSTR fields[]{L"Event/System/TimeCreated/@SystemTime", L"Event/EventData/Data[@Name='DeviceInstance']",
                     L"Event/EventData/Data[@Name='ProcessId']", L"Event/EventData/Data[@Name='ProcessName']"};
    Event ctx(EvtCreateRenderContext(4, fields, EvtRenderContextValues));
    if (!ctx) return result;
    // The Event Log service may publish slightly after CM returns. Never use an
    // older same-device event just because this request has no recorded process.
    for (int pass = 0; pass < 8 && result.empty(); ++pass) {
        if (pass) Sleep(200);
        Event query(EvtQuery(nullptr, L"System",
            L"*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP'] and EventID=225 and TimeCreated[timediff(@SystemTime) <= 60000]]]",
            EvtQueryChannelPath | EvtQueryReverseDirection));
        if (!query) break;
        for (int i = 0; i < 64 && result.size() < 8; ++i) {
            EVT_HANDLE raw{}; DWORD count{};
            if (!EvtNext(query.get(), 1, &raw, 100, 0, &count)) break;
            Event event(raw);
            DWORD size{}, n{};
            EvtRender(ctx.get(), event.get(), EvtRenderEventValues, 0, nullptr, &size, &n);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 65536) continue;
            std::vector<BYTE> data(size);
            if (!EvtRender(ctx.get(), event.get(), EvtRenderEventValues, size, data.data(), &size, &n) || n != 4) continue;
            const auto* v = reinterpret_cast<const EVT_VARIANT*>(data.data());
            if (v[0].Type != EvtVarTypeFileTime || v[0].FileTimeVal < Ticks(start) || v[0].FileTimeVal > Ticks(end) ||
                v[1].Type != EvtVarTypeString || v[2].Type != EvtVarTypeUInt32 ||
                !v[2].UInt32Val || v[3].Type != EvtVarTypeString) continue;
            if (!str::EqualsI(v[1].StringVal, disk) && (target.empty() || !str::EqualsI(v[1].StringVal, target))) continue;
            std::wstring name(v[3].StringVal);
            name.erase(0, name.find_last_of(L"\\/") + 1);
            if (!name.empty() && std::none_of(result.begin(), result.end(), [&](const ReportedProcess& p) {
                    return p.pid == v[2].UInt32Val && p.name == name;
                })) result.push_back({v[2].UInt32Val, std::move(name)});
        }
    }
    return result;
}
}

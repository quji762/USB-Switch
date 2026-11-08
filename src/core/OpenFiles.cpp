#include "core/OpenFiles.h"
#include "core/DeviceManager.h"
#include "util/Str.h"
#include "util/Win32.h"
#include <objbase.h>
#include <processsnapshot.h>
#include <algorithm>
#include <set>

namespace qe {
namespace {
using json::Value;
constexpr size_t chars = 65536;
constexpr wchar_t prefix[] = L"Local\\USB-Switch-files-";
struct View {
    wchar_t* ptr;
    ~View() { if (ptr) UnmapViewOfFile(ptr); }
};
struct Snapshot {
    HPSS snap{};
    HPSSWALK walk{};
    ~Snapshot() { if (walk) PssWalkMarkerFree(walk); if (snap) PssFreeSnapshot(GetCurrentProcess(), snap); }
};
Value Report(const wchar_t* status) {
    auto r = Value::Object();
    r.Set(L"status", Value::String(status));
    r.Set(L"paths", Value::Array());
    return r;
}
Value Scan(const std::wstring& id, DWORD pid, FILETIME before) {
    Handle proc(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE | PROCESS_VM_READ, FALSE, pid));
    if (!proc) return Report(GetLastError()==ERROR_ACCESS_DENIED ? L"access_denied" : L"unavailable");
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(proc.Get(), &created, &ended, &kernel, &user) || CompareFileTime(&created,&before)>0)
        return Report(L"process_changed");
    DeviceManager manager;
    if (!manager.Refresh()) return Report(L"device_unavailable");
    auto device = manager.FindByInstanceId(id);
    if (!device || device->volumeInterfaces.empty()) return Report(L"device_unavailable");
    std::vector<std::pair<std::wstring,std::wstring>> roots;
    for (auto vol : device->volumeInterfaces) {
        vol += L'\\';
        DWORD needed{};
        GetVolumePathNamesForVolumeNameW(vol.c_str(), nullptr, 0, &needed);
        std::vector<WCHAR> mount(needed < 65536 ? needed : 0);
        std::wstring display = vol;
        if (!mount.empty() && GetVolumePathNamesForVolumeNameW(vol.c_str(),mount.data(),needed,&needed) && mount[0])
            display = mount.data();
        roots.emplace_back(vol, display);
    }
    Snapshot s;
    DWORD err = PssCaptureSnapshot(proc.Get(), static_cast<PSS_CAPTURE_FLAGS>(PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_NAME_INFORMATION), 0, &s.snap);
    if (err) return Report(err==ERROR_ACCESS_DENIED ? L"access_denied" : L"unavailable");
    if (PssWalkMarkerCreate(nullptr,&s.walk)) return Report(L"unavailable");
    std::set<std::wstring> paths;
    bool partial=false;
    size_t length=0;
    PSS_HANDLE_ENTRY entry{};
    while ((err=PssWalkSnapshot(s.snap,PSS_WALK_HANDLES,s.walk,&entry,sizeof(entry)))==ERROR_SUCCESS) {
        // PSS may count the trailing NUL in TypeNameLength. File handles do
        // not necessarily have an ObjectName in the snapshot. The last clause
        // rejects a hypothetical five-character type that merely starts with
        // "File" and stores no terminator.
        if (!entry.TypeName || entry.TypeNameLength < 4*sizeof(WCHAR) || entry.TypeNameLength > 5*sizeof(WCHAR) ||
            wcsncmp(entry.TypeName,L"File",4) ||
            (entry.TypeNameLength == 5*sizeof(WCHAR) && entry.TypeName[4] != L'\0')) continue;
        HANDLE raw{};
        if (!DuplicateHandle(proc.Get(),entry.Handle,GetCurrentProcess(),&raw,0,FALSE,0)) { partial=true; continue; }
        Handle file(raw);
        if (GetFileType(raw)!=FILE_TYPE_DISK) continue;
        std::wstring path(512,L'\0');
        constexpr DWORD flags=VOLUME_NAME_GUID | FILE_NAME_OPENED;
        DWORD size=GetFinalPathNameByHandleW(raw,path.data(),static_cast<DWORD>(path.size()),flags);
        if (size>=path.size() && size<32768) {
            path.resize(size+1);
            size=GetFinalPathNameByHandleW(raw,path.data(),static_cast<DWORD>(path.size()),flags);
        }
        if (!size || size>=path.size()) {partial=true;continue;}
        path.resize(size);
        for (const auto& root: roots) {
            if (path.size()<root.first.size() || _wcsnicmp(path.c_str(),root.first.c_str(),root.first.size())) continue;
            auto name=root.second+path.substr(root.first.size());
            if (paths.insert(name).second) length+=name.size();
            break;
        }
        if (paths.size()>=32 || length>20000) {partial=true;break;}
    }
    auto r=Report(partial || err!=ERROR_NO_MORE_ITEMS ? L"partial" : L"inspected");
    for (auto& path:paths) r.At(L"paths").Push(Value::String(path));
    return r;
}
Value TimedScan(const std::wstring& id,DWORD pid,FILETIME before, ULONGLONG deadline) {
    if (GetTickCount64()>=deadline) return Report(L"timeout");
    GUID guid{};
    WCHAR suffix[40]{};
    if (CoCreateGuid(&guid)!=S_OK || !StringFromGUID2(guid,suffix,40)) return Report(L"unavailable");
    const std::wstring name=std::wstring(prefix)+suffix;
    Handle map(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,chars*sizeof(WCHAR),name.c_str()));
    if (!map || GetLastError()==ERROR_ALREADY_EXISTS) return Report(L"unavailable");
    View out{static_cast<wchar_t*>(MapViewOfFile(map.Get(),FILE_MAP_READ,0,0,0))};
    std::wstring exe(32768,L'\0');
    DWORD n=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));
    if (!out.ptr || !n || n>=exe.size() || id.empty() || id.back()==L'\\' || id.find(L'"')!=id.npos) return Report(L"unavailable");
    exe.resize(n);
    ULARGE_INTEGER ticks{}; ticks.LowPart=before.dwLowDateTime; ticks.HighPart=before.dwHighDateTime;
    auto cmd=L"\""+exe+L"\" --file-worker "+name+L" \""+id+L"\" "+std::to_wstring(pid)+L" "+std::to_wstring(ticks.QuadPart);
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info))
        return Report(L"unavailable");
    Handle process(info.hProcess), thread(info.hThread);
    auto now=GetTickCount64();
    if (WaitForSingleObject(process.Get(),now>=deadline?0:static_cast<DWORD>(deadline-now))!=WAIT_OBJECT_0) {
        TerminateProcess(process.Get(),ERROR_TIMEOUT); return Report(L"timeout");
    }
    DWORD code{};
    Value r;
    size_t len=wcsnlen(out.ptr,chars);
    if (!GetExitCodeProcess(process.Get(),&code) || code || len==chars ||
        !Value::Parse(std::wstring_view(out.ptr,len),r) || !r.IsObject()) return Report(L"unavailable");
    // The mapping name travels on the child's command line, so any same-session
    // process could write to it during the wait. Accept only exactly the schema
    // Scan() produces, with the caps Scan() itself enforces.
    static constexpr const wchar_t* kStatuses[] = {L"inspected", L"partial", L"access_denied",
        L"timeout", L"process_changed", L"device_unavailable", L"unavailable"};
    const Value* status = r.Find(L"status");
    bool known = false;
    if (status && status->IsString())
        for (const wchar_t* candidate : kStatuses)
            if ((known = status->AsString() == candidate)) break;
    const Value* paths = r.Find(L"paths");
    if (!known || !paths || !paths->IsArray() || paths->Size() > 32) return Report(L"unavailable");
    for (const auto& path : paths->Items()) {
        if (!path.IsString() || path.AsString().empty() || path.AsString().size() > 2048)
            return Report(L"unavailable");
        for (wchar_t c : path.AsString())
            if (c < 0x20) return Report(L"unavailable");
    }
    return r;
}
}
int FileWorker(const wchar_t* map,const wchar_t* device,const wchar_t* processId,const wchar_t* timestamp) {
    if (std::wstring_view(map).rfind(prefix,0)!=0 || wcslen(map)!=wcslen(prefix)+38) return 2;
    wchar_t* end{};
    auto pid=wcstoull(processId,&end,10); if (!pid || pid>MAXDWORD || *end) return 2;
    ULARGE_INTEGER ticks{}; ticks.QuadPart=wcstoull(timestamp,&end,10); if (!ticks.QuadPart || *end) return 2;
    Handle mapping(OpenFileMappingW(FILE_MAP_WRITE,FALSE,map));
    if (!mapping) return 1;
    View out{static_cast<wchar_t*>(MapViewOfFile(mapping.Get(),FILE_MAP_WRITE,0,0,chars*sizeof(WCHAR)))};
    if (!out.ptr) return 1;
    auto data=Scan(device,static_cast<DWORD>(pid),{ticks.LowPart,ticks.HighPart}).Dump();
    if (data.size()>=chars) return 1;
    std::copy(data.begin(),data.end(),out.ptr); out.ptr[data.size()]=0; return 0;
}
Value InspectOpenFiles(const std::wstring& id,const EjectResult& veto) {
    auto list=Value::Array();
    const auto deadline=GetTickCount64()+3500;
    for (const auto& p:veto.reportedProcesses) {
        auto r=TimedScan(id,p.pid,veto.requestedAt,deadline);
        r.Set(L"pid",Value::Number(p.pid)); r.Set(L"name",Value::String(p.name));
        list.Push(std::move(r));
    }
    return list;
}
}

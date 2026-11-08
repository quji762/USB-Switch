#pragma once

#include "core/Ejector.h"
#include "util/Json.h"

namespace qe {
inline json::Value EjectJson(const EjectResult& result, const std::wstring& instanceId) {
    using json::Value;
    Value out = Value::Object();
    const wchar_t* status = L"failed";
    if (result.Succeeded()) status = L"ejected";
    else if (result.status == EjectStatus::Vetoed) status = L"vetoed";
    else if (result.status == EjectStatus::Ineligible) status = L"ineligible";
    else if (result.status == EjectStatus::ScanUnavailable) status = L"enumeration_failed";
    out.Set(L"status", Value::String(status));
    out.Set(L"instanceId", Value::String(instanceId));
    out.Set(L"retryable", Value::Bool(result.CanRetry()));
    if (result.status == EjectStatus::Vetoed) {
        Value processes = Value::Array();
        for (const auto& process : result.reportedProcesses) {
            Value item = Value::Object();
            item.Set(L"pid", Value::Number(process.pid));
            item.Set(L"name", Value::String(process.name));
            processes.Push(std::move(item));
        }
        out.Set(L"reportedProcesses", std::move(processes));
        out.Set(L"vetoType", Value::Number(result.vetoType));
        out.Set(L"vetoName", Value::String(result.vetoApplication));
    } else if (result.status == EjectStatus::Failed) {
        out.Set(L"configRet", Value::Number(result.configRet));
        out.Set(L"win32Error", Value::Number(result.win32Error));
    }
    return out;
}
}  // namespace qe

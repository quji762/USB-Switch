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
    out.Set(L"status", Value::String(status));
    out.Set(L"instanceId", Value::String(instanceId));
    out.Set(L"retryable", Value::Bool(result.CanRetry()));
    if (result.status == EjectStatus::Vetoed) {
        out.Set(L"vetoType", Value::Number(result.vetoType));
        out.Set(L"vetoName", Value::String(result.vetoApplication));
    } else if (result.status == EjectStatus::Failed) {
        out.Set(L"configRet", Value::Number(result.configRet));
        out.Set(L"win32Error", Value::Number(result.win32Error));
    }
    return out;
}
}  // namespace qe

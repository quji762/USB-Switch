#pragma once
#include "core/Ejector.h"
#include "util/Json.h"

namespace qe {
json::Value InspectOpenFiles(const std::wstring& deviceId, const EjectResult& veto);
int FileWorker(const wchar_t* map, const wchar_t* device, const wchar_t* pid, const wchar_t* before);
}

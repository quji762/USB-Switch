#pragma once
#include "core/Ejector.h"

namespace qe {
std::vector<ReportedProcess> ReadVetoEvents(const std::wstring& disk, const std::wstring& target, FILETIME start, FILETIME end);
}

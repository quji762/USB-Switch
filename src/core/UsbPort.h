#pragma once
#include <windows.h>
#include <string>

namespace qe {
// Never cycle a hub or an arbitrary user-supplied port number.
DWORD ReconnectUsbPort(const std::wstring& target, bool probeOnly);
}

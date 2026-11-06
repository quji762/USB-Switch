#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/TrayIcon.h"

struct Call { DWORD operation; DWORD flags; std::wstring title; std::wstring text; };
static std::vector<Call> calls;
static bool failAdd = false;

static BOOL WINAPI FakeNotify(DWORD op, PNOTIFYICONDATAW data) {
    calls.push_back({op, data->uFlags, data->szInfoTitle, data->szInfo});
    return !(failAdd && op == NIM_ADD);
}

int main() {
    qe::TrayIcon icon(FakeNotify);
    failAdd = true;
    if (icon.Create(reinterpret_cast<HWND>(1), WM_APP + 2, nullptr, L"test") || icon.IsCreated()) return 1;
    failAdd = false;
    calls.clear();
    if (!icon.Create(reinterpret_cast<HWND>(1), WM_APP + 2, nullptr, L"test")) return 2;
    calls.clear();
    icon.ShowBalloon(L"Connected", L"Device ready");
    if (calls.size() != 1 || calls[0].operation != NIM_MODIFY || calls[0].flags != NIF_INFO ||
        calls[0].title != L"Connected" || calls[0].text != L"Device ready") return 3;
    calls.clear();
    icon.ShowBalloon(L"Removed", L"Safe to unplug");
    if (calls.size() != 1 || calls[0].text != L"Safe to unplug") return 4;
    icon.Remove();
    std::puts("Tray creation failure and notification delivery checks passed");
    return 0;
}

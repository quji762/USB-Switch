#pragma once
// "Run at sign-in" support.
//
// Registry Run key (HKCU) is the simplest mechanism and needs no elevation.
// For a shipped product a scheduled task is friendlier (it can start the app
// minimized without a console flash and is visible in Task Scheduler) - see
// README "Productization notes".

#include <string>
#include <string_view>

namespace qe {

class AutoStart {
public:
    // Reads/writes HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
    static bool IsEnabled(std::wstring_view valueName);
    static bool SetEnabled(std::wstring_view valueName, const std::wstring& commandLine);
    static bool Clear(std::wstring_view valueName);

    // Full path of the running executable, quoted command line ready.
    static std::wstring CommandLine();
};

}  // namespace qe

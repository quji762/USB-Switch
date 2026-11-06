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
    enum class Entry { Missing, Owned, Other, Error };
    static Entry Inspect(std::wstring_view valueName, std::wstring_view expected);
    static bool SetEnabled(std::wstring_view valueName, const std::wstring& commandLine);
    // Remove an entry only if it still has the expected command line.
    static bool ClearIfMatches(std::wstring_view valueName, std::wstring_view expected);

    // Full path of the running executable, quoted command line ready.
    static std::wstring CommandLine();
};

}  // namespace qe

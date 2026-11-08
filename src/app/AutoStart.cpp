#include "app/AutoStart.h"

#include <windows.h>

#include <vector>


namespace qe {
namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

using Entry = AutoStart::Entry;
Entry InspectValue(HKEY key, const wchar_t* name, std::wstring_view expected) {
    DWORD type = 0, bytes = 0;
    LONG rc = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (rc == ERROR_FILE_NOT_FOUND) return Entry::Missing;
    if (rc != ERROR_SUCCESS) return Entry::Error;
    if (type != REG_SZ || bytes != (expected.size() + 1) * sizeof(wchar_t)) return Entry::Other;
    std::vector<wchar_t> value(expected.size() + 1);
    rc = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    if (rc != ERROR_SUCCESS) return Entry::Error;
    if (type != REG_SZ || bytes != value.size() * sizeof(wchar_t) || value.back() != 0)
        return Entry::Other;
    return CompareStringOrdinal(value.data(), static_cast<int>(expected.size()),
                                expected.data(), static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL
        ? Entry::Owned : Entry::Other;
}

// The Run command may be quoted or plain and may carry arguments; everything
// up to the first unquoted space is the executable path.
std::wstring_view ExecutableName(std::wstring_view commandLine) {
    if (!commandLine.empty() && commandLine.front() == L'"') {
        const size_t end = commandLine.find(L'"', 1);
        commandLine = commandLine.substr(1, end == std::wstring_view::npos ? end : end - 1);
    } else {
        const size_t space = commandLine.find(L' ');
        if (space != std::wstring_view::npos) commandLine = commandLine.substr(0, space);
    }
    const size_t slash = commandLine.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? commandLine : commandLine.substr(slash + 1);
}

// Reads a REG_SZ value of any length, tolerating a writer that stored it
// without the terminating NUL. Empty on any failure.
std::wstring ReadRunValue(HKEY key, const wchar_t* name) {
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        type != REG_SZ || bytes == 0 || bytes % sizeof(wchar_t) || bytes > 32768 * sizeof(wchar_t))
        return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS)
        return {};
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

}  // namespace

std::wstring AutoStart::CommandLine() {
    std::wstring path(MAX_PATH, L'\0');
    while (path.size() <= 32768) {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!n) break;
        if (n < path.size()) {
            path.resize(n);
            return L"\"" + path + L"\"";
        }
        if (path.size() == 32768) break;
        path.resize(path.size() > 16384 ? 32768 : path.size() * 2);
    }
    return {};
}

AutoStart::Entry AutoStart::Inspect(std::wstring_view valueName, std::wstring_view expected) {
    if (expected.empty()) return Entry::Error;
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key);
    if (rc == ERROR_FILE_NOT_FOUND) return Entry::Missing;
    if (rc != ERROR_SUCCESS) return Entry::Error;
    Entry state = InspectValue(key, std::wstring(valueName).c_str(), expected);
    RegCloseKey(key);
    return state;
}

bool AutoStart::SetEnabled(std::wstring_view valueName, const std::wstring& commandLine) {
    if (commandLine.empty()) return false;
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0,
                              KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return false;
    const std::wstring name(valueName);
    Entry state = InspectValue(key, name.c_str(), commandLine);
    // An entry pointing at another program is never touched. A stale entry
    // from an older copy of this executable - the installation moved - would
    // otherwise be unfixable from within the app: SetEnabled refuses it and
    // ClearIfMatches never matches it. Its executable name identifies it as
    // ours, so adopt it by overwriting.
    bool adoptable = false;
    if (state == Entry::Other) {
        const std::wstring stored = ReadRunValue(key, name.c_str());
        const std::wstring_view existing = ExecutableName(stored);
        const std::wstring_view mine = ExecutableName(commandLine);
        adoptable = !existing.empty() && !mine.empty() &&
                    CompareStringOrdinal(existing.data(), static_cast<int>(existing.size()),
                                         mine.data(), static_cast<int>(mine.size()), TRUE) == CSTR_EQUAL;
    }
    if (state == Entry::Missing || (state == Entry::Other && adoptable)) {
        rc = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(commandLine.c_str()),
                            static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return state == Entry::Owned || (state == Entry::Missing && rc == ERROR_SUCCESS) ||
           (state == Entry::Other && adoptable && rc == ERROR_SUCCESS);
}

bool AutoStart::ClearIfMatches(std::wstring_view valueName, std::wstring_view expected) {
    if (expected.empty()) return false;
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    if (rc == ERROR_FILE_NOT_FOUND) return true;
    if (rc != ERROR_SUCCESS) return false;
    const std::wstring name(valueName);
    Entry state = InspectValue(key, name.c_str(), expected);
    if (state == Entry::Owned) rc = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    return state != Entry::Error && (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
}

}  // namespace qe

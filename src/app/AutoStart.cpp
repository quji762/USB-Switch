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
    if (state == Entry::Missing) {
        rc = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(commandLine.c_str()),
                            static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return state == Entry::Owned || (state == Entry::Missing && rc == ERROR_SUCCESS);
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

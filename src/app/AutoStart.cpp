#include "app/AutoStart.h"

#include <windows.h>

#include <iterator>

#include "app/Logger.h"
#include "util/Str.h"

namespace qe {
namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

HKEY OpenRunKey(REGSAM access) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  access, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        QE_LOG_WARN(L"RegCreateKeyEx(%ls) failed: %ld", kRunKeyPath, result);
        return nullptr;
    }
    return key;
}

}  // namespace

std::wstring AutoStart::CommandLine() {
    WCHAR path[MAX_PATH * 2] = L"";
    DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) return {};
    return L"\"" + std::wstring(path) + L"\"";
}

bool AutoStart::IsEnabled(std::wstring_view valueName) {
    HKEY key = OpenRunKey(KEY_READ);
    if (key == nullptr) return false;
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, std::wstring(valueName).c_str(), nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && size > 0;
}

bool AutoStart::SetEnabled(std::wstring_view valueName, const std::wstring& commandLine) {
    HKEY key = OpenRunKey(KEY_WRITE);
    if (key == nullptr) return false;
    const std::wstring name(valueName);
    DWORD bytes = static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t));
    LONG result = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(commandLine.c_str()), bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        QE_LOG_ERROR(L"RegSetValueEx(%ls) failed: %ld", name.c_str(), result);
        return false;
    }
    return true;
}

bool AutoStart::Clear(std::wstring_view valueName) {
    HKEY key = OpenRunKey(KEY_WRITE);
    if (key == nullptr) return false;
    const std::wstring name(valueName);
    LONG result = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        QE_LOG_ERROR(L"RegDeleteValue(%ls) failed: %ld", name.c_str(), result);
        return false;
    }
    return true;
}

}  // namespace qe

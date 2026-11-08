#include "app/Logger.h"

#include <windows.h>

#include <cstdarg>

#include "util/Str.h"
#include "util/Win32.h"

namespace qe {
namespace {

constexpr DWORD kMaxFileBytes = 1u << 20;  // 1 MiB before rotation

const wchar_t* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return L"DEBUG";
        case LogLevel::Info: return L"INFO ";
        case LogLevel::Warning: return L"WARN ";
        case LogLevel::Error: return L"ERROR";
    }
    return L"?????";
}

std::wstring Timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return str::Format(L"%04u-%02u-%02u %02u:%02u:%02u.%03u", time.wYear, time.wMonth, time.wDay,
                       time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}

// Device names and volume labels are external data; a label containing a raw
// CR/LF would forge extra log lines. Escape control characters below 0x20
// (keeping '\t' for readability) so every record stays exactly one line.
std::wstring SanitizeMessage(const std::wstring& text) {
    bool plain = true;
    for (wchar_t ch : text) {
        if (ch < 0x20 && ch != L'\t') { plain = false; break; }
    }
    if (plain) return text;
    static const wchar_t* kHexDigits = L"0123456789ABCDEF";
    std::wstring clean;
    clean.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch < 0x20 && ch != L'\t') {
            clean.push_back(L'\\');
            clean.push_back(L'x');
            clean.push_back(kHexDigits[(ch >> 4) & 0xF]);
            clean.push_back(kHexDigits[ch & 0xF]);
        } else {
            clean.push_back(ch);
        }
    }
    return clean;
}

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::wstring& directory, std::wstring_view baseName) {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseFileLocked();
    path_.clear();
    retryRotationAfter_ = 0;
    if (directory.empty()) return;
    CreateDirectories(directory);
    path_ = directory;
    if (!path_.empty() && path_.back() != L'\\') path_.push_back(L'\\');
    path_.append(baseName);

    // The log is opened with FILE_SHARE_READ only, so a second process racing
    // on the same file (two CLI runs) fails its first open. Retry briefly
    // before giving up and silently logging nowhere.
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) Sleep(50);
        file = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        path_.clear();
        return;
    }
    file_ = file;
    // Append to whatever is already there.
    SetFilePointer(file, 0, nullptr, FILE_END);
    RotateIfNeededLocked();
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseFileLocked();
}

void Logger::CloseFileLocked() {
    if (file_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(file_));
        file_ = nullptr;
    }
}

void Logger::Log(LogLevel level, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    std::wstring message = str::FormatV(format, args);
    va_end(args);

    // Format outside the lock: only the write needs to be serialized.
    const std::wstring line =
        str::Format(L"%ls [%ls] %ls", Timestamp().c_str(), LevelTag(level),
                    SanitizeMessage(message).c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    RotateIfNeededLocked();
    WriteLineLocked(line);
}

void Logger::RotateIfNeededLocked() {
    if (file_ == nullptr) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(static_cast<HANDLE>(file_), &size)) return;
    if (GetTickCount64() < retryRotationAfter_) return;
    if (size.QuadPart < static_cast<LONGLONG>(kMaxFileBytes)) return;

    CloseHandle(static_cast<HANDLE>(file_));
    file_ = nullptr;

    // If the move fails, keep the old log rather than recreating the file:
    // CREATE_ALWAYS below would truncate it and destroy the previous contents.
    const std::wstring backup = path_ + L".old";
    if (MoveFileExW(path_.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        // No QE_LOG_* here: Log() takes the same mutex this function is called
        // under, and std::mutex is not recursive. OutputDebugString reaches a
        // debugger without touching the log file.
        OutputDebugStringW(str::Format(L"USB-Switch: could not rotate %ls to %ls (%lu)",
                                       path_.c_str(), backup.c_str(), GetLastError())
                               .c_str());
        HANDLE append = CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (append != INVALID_HANDLE_VALUE) file_ = append;
        else path_.clear();
        retryRotationAfter_ = GetTickCount64() + 60000;
        return;
    }

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        path_.clear();
        return;
    }
    file_ = file;
}

void Logger::WriteLineLocked(const std::wstring& line) {
    if (file_ == nullptr) return;
    std::string utf8 = str::ToUtf8(line);
    utf8.append("\r\n");
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(file_), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

}  // namespace qe

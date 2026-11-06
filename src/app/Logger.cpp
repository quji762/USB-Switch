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

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::wstring& directory, std::wstring_view baseName) {
    Shutdown();
    if (directory.empty()) return;
    CreateDirectories(directory);
    path_ = directory;
    if (!path_.empty() && path_.back() != L'\\') path_.push_back(L'\\');
    path_.append(baseName);

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        path_.clear();
        return;
    }
    file_ = file;
    // Append to whatever is already there.
    SetFilePointer(file, 0, nullptr, FILE_END);
    RotateIfNeeded();
}

void Logger::Shutdown() {
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
    WriteLine(str::Format(L"%ls [%ls] %ls", Timestamp().c_str(), LevelTag(level), message.c_str()));
}

void Logger::RotateIfNeeded() {
    if (file_ == nullptr) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(static_cast<HANDLE>(file_), &size)) return;
    if (size.QuadPart < static_cast<LONGLONG>(kMaxFileBytes)) return;

    CloseHandle(static_cast<HANDLE>(file_));
    file_ = nullptr;

    std::wstring backup = path_ + L".old";
    MoveFileExW(path_.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        path_.clear();
        return;
    }
    file_ = file;
}

void Logger::WriteLine(const std::wstring& line) {
    if (file_ == nullptr) return;
    std::string utf8 = str::ToUtf8(line);
    utf8.append("\r\n");
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(file_), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

}  // namespace qe

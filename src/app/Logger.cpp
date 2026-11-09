#include "app/Logger.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <vector>

#include "util/Str.h"
#include "util/Win32.h"

namespace qe {
namespace {

constexpr DWORD kMaxFileBytes = 1u << 20;  // 1 MiB before rotation

// How many per-process log files to keep in the log directory.
//
// Every launch that loses the race for the shared name leaves one behind (see
// Init), so without a bound the directory grows by a file per launch: clicking
// the Settings shortcut a hundred times while investigating a device would
// leave a hundred files nobody ever looks at again.
//
// Eight is a deliberate middle ground. Each of these files holds a handful of
// lines from one short-lived process - a few KiB at most - so eight of them
// cost nothing on disk, and eight is still more sessions than a user re-opens
// while chasing one problem, which is the only case where the older ones are
// worth keeping. Anything beyond that is stale evidence about a run the user
// has already forgotten, and the shared log itself is separately capped by the
// 1 MiB rotation above.
constexpr size_t kKeepPerProcessLogs = 8;

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

std::wstring JoinPath(const std::wstring& directory, std::wstring_view baseName) {
    std::wstring path = directory;
    if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
    path.append(baseName);
    return path;
}

// "usb-switch.log" becomes "usb-switch.1234.log": the extension is kept so the
// per-process file still reads as a log next to its siblings.
std::wstring TaggedWithProcessId(std::wstring_view baseName) {
    const size_t dot = baseName.find_last_of(L'.');
    std::wstring tagged(dot == std::wstring_view::npos ? baseName : baseName.substr(0, dot));
    tagged.push_back(L'.');
    tagged += std::to_wstring(GetCurrentProcessId());
    if (dot != std::wstring_view::npos) {
        tagged.push_back(L'.');
        tagged.append(baseName.substr(dot + 1));
    }
    return tagged;
}

// "usb-switch.log" splits into the two halves the PID is written between, so
// the pattern below is derived rather than hard-coded: the CLI's
// "usb-switch-cli.log" prunes "usb-switch-cli.<pid>.log" and the tray's
// "usb-switch.log" prunes "usb-switch.<pid>.log", each in its own family.
//
// A base name without an extension is rejected: TaggedWithProcessId would
// produce a name ending in a bare dot, which is not a log file this code ever
// writes, so there is nothing safe to match.
bool SplitBaseName(std::wstring_view baseName, std::wstring* prefix, std::wstring* extension) {
    const size_t dot = baseName.find_last_of(L'.');
    if (dot == std::wstring_view::npos || dot == 0 || dot + 1 == baseName.size()) return false;
    *prefix = std::wstring(baseName.substr(0, dot));
    *extension = std::wstring(baseName.substr(dot + 1));
    return true;
}

// True only for a name of the exact shape "<prefix>.<digits>.<extension>", and
// only for digits: ASCII 0-9 between the two dots.
//
// This is the whole safety argument for pruning, so it is intentionally
// literal. The three checks each reject a class of file that must survive:
//   * the shared name itself ("usb-switch.log") - the digits segment is
//     missing, so the name is shorter than prefix + '.' + ext allows;
//   * the other family's files ("usb-switch-cli.log", and its tagged
//     "usb-switch-cli.1234.log") - neither starts with "usb-switch." followed
//     by a digit;
//   * anything a human or another tool dropped in ("usb-switch.log.old",
//     "usb-switch.12ab.log", "usb-switch..log") - the middle segment is not
//     all digits, or is empty.
// Comparison is case-sensitive on purpose: the names this code writes are
// derived from its own base name, and deleting strictly less is the safe
// direction.
bool IsPerProcessLogName(const std::wstring& name, const std::wstring& prefix,
                         const std::wstring& extension) {
    const std::wstring head = prefix + L".";
    const std::wstring tail = L"." + extension;
    // Room for at least one digit between the two fixed parts.
    if (name.size() < head.size() + 1 + tail.size()) return false;
    if (name.compare(0, head.size(), head) != 0) return false;
    if (name.compare(name.size() - tail.size(), tail.size(), tail) != 0) return false;
    for (size_t i = head.size(), end = name.size() - tail.size(); i < end; ++i) {
        if (name[i] < L'0' || name[i] > L'9') return false;
    }
    return true;
}

struct PerProcessLog {
    std::wstring name;
    FILETIME written{};
};

// Deletes all but the newest kKeepPerProcessLogs per-process logs in
// `directory`, leaving the total stable across launches instead of growing
// without bound.
//
// `keepName` is the name this process is about to write to (the tagged name
// Init falls back to); it is excluded from the candidates so a live log is
// never its own victim, and a stale file left behind by a reused PID survives
// long enough to be appended to rather than silently replaced.
//
// Failures are skipped, not reported. The expected cause is a file another
// process still holds open: OpenLogForAppend opens without FILE_SHARE_DELETE,
// so a writer that is still alive makes DeleteFileW fail with
// ERROR_SHARING_VIOLATION, and that file is exactly the one that must not go.
// The next start prunes it again. Nothing here may cost the log a line or
// divert Init from opening the log it was asked to open, so there is no
// QE_LOG_* and no failure path out of this function.
void PrunePerProcessLogs(const std::wstring& directory, std::wstring_view baseName,
                         const std::wstring& keepName) {
    std::wstring prefix;
    std::wstring extension;
    if (!SplitBaseName(baseName, &prefix, &extension)) return;

    std::wstring pattern = directory;
    if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');

    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;  // nothing to enumerate: nothing to prune

    std::vector<PerProcessLog> candidates;
    do {
        // A directory or reparse point that happens to carry the name is not
        // this code's log, and deleting it could remove something else's link.
        if (found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const std::wstring name = found.cFileName;
        if (!IsPerProcessLogName(name, prefix, extension)) continue;
        if (name == keepName) continue;
        candidates.push_back(PerProcessLog{name, found.ftLastWriteTime});
    } while (FindNextFileW(search, &found) != FALSE);
    FindClose(search);

    if (candidates.size() <= kKeepPerProcessLogs) return;
    std::sort(candidates.begin(), candidates.end(),
              [](const PerProcessLog& a, const PerProcessLog& b) {
                  return CompareFileTime(&a.written, &b.written) > 0;  // newest first
              });
    // Indices below the bound are the survivors; everything after them is
    // older and goes. A delete that fails leaves the file in place, which only
    // means one extra file until the next start.
    for (size_t i = kKeepPerProcessLogs; i < candidates.size(); ++i) {
        DeleteFileW(JoinPath(directory, candidates[i].name).c_str());
    }
}

// Append mode: the log is opened with FILE_SHARE_READ only, so a second
// process racing on the same file (two CLI runs, or a CLI run while the tray
// is resident) fails its first open. Retry briefly, then report the failure to
// the caller, which decides between a per-process file and no log at all.
HANDLE OpenLogForAppend(const std::wstring& path) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt) Sleep(50);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) return file;
    }
    return INVALID_HANDLE_VALUE;
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
    writeErrorReported_ = false;
    if (directory.empty()) return;
    CreateDirectories(directory);

    // Housekeeping first, before anything is opened: the per-process files
    // left by earlier launches are what accumulate, and pruning here - rather
    // than after a successful open - keeps the directory bounded even on a
    // start that ends up logging nowhere.
    PrunePerProcessLogs(directory, baseName, TaggedWithProcessId(baseName));

    path_ = JoinPath(directory, baseName);
    HANDLE file = OpenLogForAppend(path_);
    if (file == INVALID_HANDLE_VALUE) {
        // The running tray owns usb-switch.log with FILE_SHARE_READ only, so a
        // second instance - a --settings launch, or a CLI run that races it -
        // cannot write it. Fall back to a per-process name instead of dropping
        // every line: "another instance is already running" is exactly what a
        // second launch must leave behind. No QE_LOG_* here: Log() takes the
        // mutex this function holds and std::mutex is not recursive.
        // OutputDebugStringW reaches a debugger without touching the log file,
        // the same reasoning as the rotation failure below.
        OutputDebugStringW(str::Format(L"USB-Switch: could not open %ls (%lu); using a per-process log",
                                       path_.c_str(), GetLastError())
                               .c_str());
        path_ = JoinPath(directory, TaggedWithProcessId(baseName));
        file = OpenLogForAppend(path_);
        if (file == INVALID_HANDLE_VALUE) {
            OutputDebugStringW(str::Format(L"USB-Switch: could not open %ls either (%lu); logging is disabled",
                                           path_.c_str(), GetLastError())
                                   .c_str());
            path_.clear();
            return;
        }
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
        else {
            OutputDebugStringW(str::Format(L"USB-Switch: could not reopen %ls after a failed rotation (%lu)",
                                           path_.c_str(), GetLastError())
                                   .c_str());
            path_.clear();
        }
        retryRotationAfter_ = GetTickCount64() + 60000;
        return;
    }

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        // The previous contents survived as .old, so nothing is lost - but the
        // new log cannot be opened, and that must not pass unnoticed.
        OutputDebugStringW(str::Format(L"USB-Switch: could not recreate %ls after rotation (%lu)",
                                       path_.c_str(), GetLastError())
                               .c_str());
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
    if (WriteFile(static_cast<HANDLE>(file_), utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                  nullptr) == FALSE ||
        written != utf8.size()) {
        // A failed or short write truncates the record, so the file no longer
        // matches what was logged. Reported the same way as a rotation
        // failure: a full or dead disk is exactly when the log cannot be
        // trusted, and writing more would only repeat the failure. Once per
        // failure episode, so a full disk does not flood a debugger.
        if (!writeErrorReported_) {
            writeErrorReported_ = true;
            OutputDebugStringW(str::Format(L"USB-Switch: log write to %ls failed (%lu of %lu bytes, %lu)",
                                           path_.c_str(), written,
                                           static_cast<unsigned long>(utf8.size()), GetLastError())
                                   .c_str());
        }
        return;
    }
    writeErrorReported_ = false;
}

}  // namespace qe

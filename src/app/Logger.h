#pragma once
// Rotating single-file logger.
//
// The log lives in %LOCALAPPDATA%\USB-Switch\logs\usb-switch.log and is written
// as UTF-8. It is a diagnostic aid, not an audit trail: the previous file is
// kept as .old once the current one passes 1 MiB, and nothing is flushed
// synchronously (buffering is left to the CRT/OS).
//
// All entry points take a mutex. Updater::CheckAsync documents that its
// callback may run on any thread, and a real implementation (WinHTTP on a
// worker thread) will log from there - without the lock two writers would race
// on file_ and path_.

#include <mutex>
#include <string>
#include <string_view>

namespace qe {

enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    static Logger& Instance();

    // Opens (creating if needed) <directory>\<baseName>.
    void Init(const std::wstring& directory, std::wstring_view baseName = L"usb-switch.log");
    void Shutdown();

    // Exactly one overload: two would be ambiguous for a plain literal
    // (array-to-pointer conversion makes both candidates equally good). Every
    // call passes at least the format string, and a narrow literal is a
    // compile error by design - all log formats are wide.
    void Log(LogLevel level, const wchar_t* format, ...);

    const std::wstring& Path() const noexcept { return path_; }
    bool IsOpen() const noexcept { return file_ != nullptr; }

private:
    Logger() = default;
    // The *Locked variants require mutex_ to be held by the caller. They are
    // separate from the public entry points because std::mutex is not
    // recursive: a locked public method must not call another public one.
    void WriteLineLocked(const std::wstring& line);
    void RotateIfNeededLocked();
    void CloseFileLocked();

    std::wstring path_;
    void* file_ = nullptr;  // HANDLE, kept opaque so this header stays light
    std::mutex mutex_;
};

}  // namespace qe

#define QE_LOG_DEBUG(...) ::qe::Logger::Instance().Log(::qe::LogLevel::Debug, __VA_ARGS__)
#define QE_LOG_INFO(...)  ::qe::Logger::Instance().Log(::qe::LogLevel::Info, __VA_ARGS__)
#define QE_LOG_WARN(...)  ::qe::Logger::Instance().Log(::qe::LogLevel::Warning, __VA_ARGS__)
#define QE_LOG_ERROR(...) ::qe::Logger::Instance().Log(::qe::LogLevel::Error, __VA_ARGS__)

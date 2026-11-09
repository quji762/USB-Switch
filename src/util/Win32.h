#pragma once
// Minimal RAII wrappers for the Win32 handles this project uses. The project
// deliberately avoids smart-pointer gymnastics: two small types cover every
// handle we own.

#include <windows.h>
#include <setupapi.h>  // HDEVINFO, SetupDiDestroyDeviceInfoList

#include <string>
#include <string_view>

namespace qe {

// Creates every missing directory in `path`, including the last component.
// CreateDirectoryW only creates one level and fails harmlessly for existing
// directories, so the components are created one by one.
inline void CreateDirectories(const std::wstring& path) {
    if (path.empty()) return;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] != L'\\' && path[i] != L'/') continue;
        if (i == 0) continue;                        // leading separator
        if (i == 2 && path[1] == L':') continue;     // drive root, e.g. "C:\"
        CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    }
    CreateDirectoryW(path.c_str(), nullptr);
}

// Returns the value of the environment variable `name`, or `fallback` when it
// is unset or empty. GetEnvironmentVariableW's two-call protocol counts the
// terminating null in the length, so that handling lives here instead of being
// repeated at every call site.
inline std::wstring EnvironmentOr(std::wstring_view name, std::wstring_view fallback) {
    DWORD length = GetEnvironmentVariableW(std::wstring(name).c_str(), nullptr, 0);
    if (length == 0) return std::wstring(fallback);
    std::wstring value(static_cast<size_t>(length), L'\0');
    const DWORD copied =
        GetEnvironmentVariableW(std::wstring(name).c_str(), value.data(), length);
    if (copied == 0 || copied >= length) return std::wstring(fallback);
    value.resize(copied);
    return value;
}

// Closes a kernel HANDLE (no-op for nullptr / INVALID_HANDLE_VALUE).
class Handle {
public:
    Handle() noexcept = default;
    explicit Handle(HANDLE h) noexcept : handle_(h) {}
    ~Handle() { Reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HANDLE Get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    void Reset(HANDLE h = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = h;
    }

private:
    HANDLE handle_ = nullptr;
};

// Owns a block allocated by the Win32 API itself (FormatMessageW with
// FORMAT_MESSAGE_ALLOCATE_BUFFER hands out a LocalAlloc'ed buffer that the
// caller must LocalFree). Wrapping it keeps the release off the happy path,
// where a throwing std::wstring construction between the call and the free
// would otherwise leak the block.
class LocalBlock {
public:
    LocalBlock() noexcept = default;
    ~LocalBlock() { Reset(); }

    LocalBlock(const LocalBlock&) = delete;
    LocalBlock& operator=(const LocalBlock&) = delete;

    LocalBlock(LocalBlock&& other) noexcept : block_(other.block_) { other.block_ = nullptr; }
    LocalBlock& operator=(LocalBlock&& other) noexcept {
        if (this != &other) {
            Reset();
            block_ = other.block_;
            other.block_ = nullptr;
        }
        return *this;
    }

    LPWSTR Get() const noexcept { return block_; }
    explicit operator bool() const noexcept { return block_ != nullptr; }

    void Reset(LPWSTR block = nullptr) noexcept {
        if (block_ != nullptr) LocalFree(block_);
        block_ = block;
    }

    // Out-parameter for the ALLOCATE_BUFFER protocol: the API writes the
    // pointer here and leaves it alone on failure.
    LPWSTR* Receive() noexcept { return &block_; }

private:
    LPWSTR block_ = nullptr;
};

// Destroys a SetupAPI device information set (HDEVINFO).
class DevInfoSet {
public:
    DevInfoSet() noexcept = default;
    explicit DevInfoSet(HDEVINFO h) noexcept : info_(h) {}
    ~DevInfoSet() { Reset(); }

    DevInfoSet(const DevInfoSet&) = delete;
    DevInfoSet& operator=(const DevInfoSet&) = delete;

    DevInfoSet(DevInfoSet&& other) noexcept : info_(other.info_) { other.info_ = INVALID_HANDLE_VALUE; }
    DevInfoSet& operator=(DevInfoSet&& other) noexcept {
        if (this != &other) {
            Reset();
            info_ = other.info_;
            other.info_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    HDEVINFO Get() const noexcept { return info_; }
    explicit operator bool() const noexcept { return info_ != INVALID_HANDLE_VALUE && info_ != nullptr; }

    void Reset(HDEVINFO h = INVALID_HANDLE_VALUE) noexcept {
        if (info_ != INVALID_HANDLE_VALUE && info_ != nullptr) SetupDiDestroyDeviceInfoList(info_);
        info_ = h;
    }

private:
    HDEVINFO info_ = INVALID_HANDLE_VALUE;
};

}  // namespace qe

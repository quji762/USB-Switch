#pragma once
// Minimal RAII wrappers for the Win32 handles this project uses. The project
// deliberately avoids smart-pointer gymnastics: two small types cover every
// handle we own.

#include <windows.h>
#include <setupapi.h>  // HDEVINFO, SetupDiDestroyDeviceInfoList

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

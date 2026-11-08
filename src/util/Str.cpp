#include "util/Str.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace qe::str {

bool IsDriveLetterMount(std::wstring_view path) {
    return path.size() == 3 &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && path[2] == L'\\';
}
namespace {

constexpr size_t kFormatLimit = 8 * 1024;

}  // namespace
std::wstring FromUtf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                   static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        // Fall back to a lossy conversion so diagnostics never disappear.
        size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (size <= 0) return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    int written = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      out.data(), size);
    if (written <= 0) return {};
    out.resize(static_cast<size_t>(written));
    return out;
}

std::string ToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    int written = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                      out.data(), size, nullptr, nullptr);
    if (written <= 0) return {};
    out.resize(static_cast<size_t>(written));
    return out;
}

std::wstring FormatV(const wchar_t* fmt, va_list args) {
    if (fmt == nullptr) return {};
    // vswprintf() counts the terminator inside `size`, so the buffer size is
    // exactly what must be passed - no +1 here.
    std::wstring out(kFormatLimit, L'\0');
    int written = std::vswprintf(out.data(), out.size(), fmt, args);
    if (written < 0) {
        // vswprintf failed. Truncate at the first NUL, but guard against
        // find() returning npos (nothing was written at all): resize(npos)
        // would throw std::length_error.
        const size_t terminator = out.find(L'\0');
        out.resize(terminator == std::wstring::npos ? 0 : terminator);
        return out;
    }
    out.resize(static_cast<size_t>(written));
    return out;
}

std::wstring Format(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::wstring out = FormatV(fmt, args);
    va_end(args);
    return out;
}

std::wstring Trim(std::wstring_view s) {
    size_t begin = 0;
    size_t end = s.size();
    auto isSpace = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\f' || c == L'\v';
    };
    while (begin < end && isSpace(s[begin])) ++begin;
    while (end > begin && isSpace(s[end - 1])) --end;
    return std::wstring(s.substr(begin, end - begin));
}

std::wstring Lower(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return out;
}

bool EqualsI(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb - L'A' + L'a');
        if (ca != cb) return false;
    }
    return true;
}

bool EndsWithI(std::wstring_view s, std::wstring_view suffix) {
    if (s.size() < suffix.size()) return false;
    return EqualsI(s.substr(s.size() - suffix.size()), suffix);
}

std::wstring FileSizeString(unsigned long long bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    constexpr int kMaxUnit = 5;
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < kMaxUnit) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        // %ls, not %s: in the wprintf family %s already means a wide string,
        // and %ls states it explicitly.
        return str::Format(L"%llu %ls", bytes, units[0]);
    }
    // One decimal rounds up at .95: without a carry, 1048575 bytes would show
    // as "1024 KB" while 1048576 shows as "1 MB".
    if (value >= 1023.95 && unit < kMaxUnit) {
        value /= 1024.0;
        ++unit;
    }
    // One decimal, dropping a trailing ".0" so "512.0 MB" prints as "512 MB".
    std::wstring text = str::Format(L"%.1f", value);
    if (text.size() > 2 && str::EndsWithI(text, L".0")) {
        text.erase(text.size() - 2, 2);
    }
    return str::Format(L"%ls %ls", text.c_str(), units[unit]);
}

std::wstring Win32ErrorMessage(unsigned long error) {
    if (error == 0) return {};
    LPWSTR buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (len == 0 || buffer == nullptr) {
        return str::Format(L"Win32 error %lu", error);
    }
    std::wstring message(buffer, len);
    LocalFree(buffer);
    return str::Trim(message);
}

}  // namespace qe::str

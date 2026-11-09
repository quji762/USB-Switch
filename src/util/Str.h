#pragma once
// String helpers: UTF-8/UTF-16 conversion, printf-style formatting and small
// path utilities. Everything here is locale-independent (C locale semantics).

#include <cstdarg>
#include <string>
#include <string_view>

namespace qe::str {

// Converts UTF-8 bytes to UTF-16. Invalid sequences become U+FFFD (one per
// offending byte), so diagnostics never disappear and the result is never empty
// for non-empty input.
std::wstring FromUtf8(std::string_view utf8);

// Converts UTF-16 text to UTF-8 bytes (no BOM is added). The conversion never
// fails: an unpaired surrogate becomes U+FFFD rather than an error.
std::string ToUtf8(std::wstring_view wide);

// printf-style formatting into a std::wstring. Output longer than 8 KiB is cut
// at 8191 characters - the terminator needs the last slot - and is truncated
// rather than fatal. Measured on the UCRT this project links: vswprintf()
// returns -1 for an over-long output but has already written the truncated
// prefix, so the caller sees the text that was produced. A conversion that
// fails outright (nothing written) yields an empty string instead.
std::wstring Format(const wchar_t* fmt, ...);

// va_list variant, used by the logging macros.
std::wstring FormatV(const wchar_t* fmt, va_list args);

std::wstring Trim(std::wstring_view s);
std::wstring Lower(std::wstring_view s);

bool EqualsI(std::wstring_view a, std::wstring_view b);
bool EndsWithI(std::wstring_view s, std::wstring_view suffix);

// A volume mount point is a drive letter only when it is exactly "X:\\".
// Directory mount points such as "C:\\mnt\\usb\\" refer to a different
// volume and must never be resolved by opening the host drive C:.
bool IsDriveLetterMount(std::wstring_view path);

// Base-1024 human readable size, e.g. "14.6 GB" / "512 MB" / "0 B".
std::wstring FileSizeString(unsigned long long bytes);

// FormatMessage-based rendering of a Win32 error code.
std::wstring Win32ErrorMessage(unsigned long error);

}  // namespace qe::str

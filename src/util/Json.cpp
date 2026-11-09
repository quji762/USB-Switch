#include "util/Json.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cwchar>

#include "util/Str.h"
#include "util/Win32.h"

namespace qe::json {
namespace {

const std::wstring kEmptyString;

// Objects and arrays recurse into ParseValue. A hand-edited or corrupted
// settings file full of "[[[[[[..." would otherwise exhaust the stack, so the
// nesting depth is bounded. Real settings never come close to this.
constexpr int kMaxDepth = 64;

// How much of the input an error message shows at the failure point.
constexpr size_t kTokenPreview = 16;

// Temporary names tried before SaveFile gives up. The sequence number only has
// to outrun the files a predecessor of this same process left behind, so a
// handful is plenty.
constexpr unsigned kMaxSaveAttempts = 64;

// Deletes the ".tmp" files this same process (same PID and thread id) left in
// the directory of `path`. A live process can never share a PID with us and no
// other thread of ours can share our thread id, so anything matching was
// written by a predecessor that died before it could rename or delete it.
// Temporaries belonging to other processes are deliberately left alone: their
// owners may still be writing, and a name we cannot prove stale is not ours to
// delete. `keep` names the file we are still holding, if any.
void RemoveStaleTemporaries(const std::wstring& path, const std::wstring& keep) {
    const std::wstring pattern =
        path + str::Format(L".%lu.%lu.*.tmp", GetCurrentProcessId(), GetCurrentThreadId());
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    const size_t lastSlash = path.find_last_of(L"\\/");
    const std::wstring directory =
        (lastSlash == std::wstring::npos) ? std::wstring() : path.substr(0, lastSlash + 1);
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::wstring name = directory + found.cFileName;
        if (name != keep) DeleteFileW(name.c_str());
    } while (FindNextFileW(search, &found) != FALSE);
    FindClose(search);
}

void AppendEscaped(std::wstring& out, std::wstring_view text) {
    out.push_back(L'"');
    for (wchar_t c : text) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    out += str::Format(L"\\u%04x", static_cast<unsigned>(c));
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back(L'"');
}

void AppendNumber(std::wstring& out, double number) {
    // NaN and the infinities are not valid JSON. Writing them would produce a
    // file no parser accepts - including this one - so the next load would fail
    // and the settings would silently fall back to defaults.
    if (!std::isfinite(number)) {
        out += L"null";
        return;
    }
    if (number == std::floor(number) && std::fabs(number) < 1e15) {
        out += str::Format(L"%lld", static_cast<long long>(number));
        return;
    }
    out += str::Format(L"%.10g", number);
}

class Parser {
public:
    Parser(std::wstring_view text, std::wstring* error) : text_(text), error_(error) {}

    bool Run(Value& out) {
        SkipWhitespace();
        // Every failure below reports what it expected and what it found; the
        // guard only catches a future path that forgets to.
        if (!ParseValue(out)) {
            if (!reported_) Fail(L"a JSON value", DescribePosition());
            return false;
        }
        SkipWhitespace();
        if (pos_ != text_.size()) return Fail(L"end of input", DescribePosition());
        return true;
    }

private:
    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            wchar_t c = text_[pos_];
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\uFEFF') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    // A short window of the input at the failure point, so a hand-edited or
    // truncated settings file can be found by eye. Control characters are shown
    // as \uXXXX rather than being dropped into a message.
    std::wstring DescribePosition() const {
        if (pos_ >= text_.size()) return L"end of input";
        size_t end = pos_;
        while (end < text_.size() && end - pos_ < kTokenPreview && text_[end] != L' ' &&
               text_[end] != L'\t' && text_[end] != L'\n' && text_[end] != L'\r') {
            ++end;
        }
        std::wstring preview;
        for (size_t i = pos_; i < end; ++i) {
            const wchar_t c = text_[i];
            if (c >= 0x20 && c != 0x7F) {
                preview.push_back(c);
            } else {
                preview += str::Format(L"\\u%04X", static_cast<unsigned>(c));
            }
        }
        if (preview.empty()) preview = str::Format(L"\\u%04X", static_cast<unsigned>(text_[pos_]));
        return L"'" + preview + L"'";
    }

    bool Fail(std::wstring_view message) {
        reported_ = true;
        if (error_ != nullptr) {
            *error_ = str::Format(L"JSON error at offset %zu: %ls", pos_,
                                  std::wstring(message).c_str());
        }
        return false;
    }

    bool Fail(std::wstring_view expected, std::wstring_view found) {
        reported_ = true;
        if (error_ != nullptr) {
            *error_ = str::Format(L"JSON error at offset %zu: expected %ls, found %ls", pos_,
                                  std::wstring(expected).c_str(), std::wstring(found).c_str());
        }
        return false;
    }

    bool ParseValue(Value& out) {
        if (pos_ >= text_.size()) return Fail(L"a JSON value", L"end of input");
        const wchar_t c = text_[pos_];
        if (c == L'{' || c == L'[') {
            if (depth_ >= kMaxDepth) {
                // stack-overflow guard
                return Fail(str::Format(L"a value nested at most %d levels deep", kMaxDepth),
                            DescribePosition());
            }
            ++depth_;
            const bool ok = (c == L'{') ? ParseObject(out) : ParseArray(out);
            --depth_;
            return ok;
        }
        if (c == L'"') {
            std::wstring text;
            if (!ParseString(text)) return false;
            out = Value::String(std::move(text));
            return true;
        }
        if (c == L't' || c == L'f') return ParseBoolean(out);
        if (c == L'n') return ParseNull(out);
        if (c == L'-' || (c >= L'0' && c <= L'9')) return ParseNumber(out);
        return Fail(L"a JSON value", DescribePosition());
    }

    bool ParseObject(Value& out) {
        ++pos_;  // '{'
        out = Value::Object();
        SkipWhitespace();
        if (Peek() == L'}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipWhitespace();
            std::wstring key;
            if (!ParseString(key)) return false;  // ParseString reported the reason
            SkipWhitespace();
            if (Peek() != L':') return Fail(L"':' after the object key", DescribePosition());
            ++pos_;  // ':'
            SkipWhitespace();
            Value value;
            if (!ParseValue(value)) return false;
            out.Set(key, std::move(value));
            SkipWhitespace();
            wchar_t c = Peek();
            if (c == L',') {
                ++pos_;
                continue;
            }
            if (c == L'}') {
                ++pos_;
                return true;
            }
            return Fail(L"',' or '}'", DescribePosition());
        }
    }

    bool ParseArray(Value& out) {
        ++pos_;  // '['
        out = Value::Array();
        SkipWhitespace();
        if (Peek() == L']') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipWhitespace();
            Value value;
            if (!ParseValue(value)) return false;
            out.Push(std::move(value));
            SkipWhitespace();
            wchar_t c = Peek();
            if (c == L',') {
                ++pos_;
                continue;
            }
            if (c == L']') {
                ++pos_;
                return true;
            }
            return Fail(L"',' or ']'", DescribePosition());
        }
    }

    bool ParseBoolean(Value& out) {
        if (text_.substr(pos_, 4) == L"true") {
            pos_ += 4;
            out = Value::Bool(true);
            return true;
        }
        if (text_.substr(pos_, 5) == L"false") {
            pos_ += 5;
            out = Value::Bool(false);
            return true;
        }
        return Fail(L"'true' or 'false'", DescribePosition());
    }

    bool ParseNull(Value& out) {
        if (text_.substr(pos_, 4) == L"null") {
            pos_ += 4;
            out = Value::Null();
            return true;
        }
        return Fail(L"'null'", DescribePosition());
    }

    // Manual scan over the JSON number grammar: '-'? int frac? exp?
    // std::wcstod is not usable here - it also accepts "0x1F", "+5", "inf" and
    // "nan", none of which are valid JSON, and an overflowing exponent yields
    // an infinity that Dump() could not write back out.
    bool ParseNumber(Value& out) {
        const size_t start = pos_;
        if (Peek() == L'-') ++pos_;
        if (pos_ >= text_.size()) return Fail(L"a digit after '-'", L"end of input");

        // RFC 8259 integer part: int = "0" / digit1-9 *DIGIT, so a leading zero
        // may only stand alone ("0", "0.5" and "0e1" are fine; "01" is not).
        if (text_[pos_] < L'0' || text_[pos_] > L'9') {
            return Fail(L"a digit", DescribePosition());  // no integer part
        }
        if (text_[pos_] == L'0') {
            ++pos_;
            if (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                return Fail(L"no digit after a leading zero", DescribePosition());
            }
        } else {
            while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
            }
        }

        if (pos_ < text_.size() && text_[pos_] == L'.') {
            ++pos_;
            size_t fracDigits = 0;
            while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
                ++fracDigits;
            }
            if (fracDigits == 0) {
                return Fail(L"a digit after the decimal point", DescribePosition());
            }
        }

        if (pos_ < text_.size() && (text_[pos_] == L'e' || text_[pos_] == L'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == L'+' || text_[pos_] == L'-')) ++pos_;
            size_t expDigits = 0;
            while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
                ++expDigits;
            }
            if (expDigits == 0) return Fail(L"a digit in the exponent", DescribePosition());
        }

        // str::Format cannot be used on a string_view, so copy the token.
        const std::wstring token(text_.substr(start, pos_ - start));
        wchar_t* end = nullptr;
        const double number = std::wcstod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) return Fail(L"a number this parser accepts");
        if (!std::isfinite(number)) {
            return Fail(L"a number inside the double range");  // reject overflow to infinity
        }
        out = Value::Number(number);
        return true;
    }

    bool ParseString(std::wstring& out) {
        if (Peek() != L'"') return Fail(L"a string", DescribePosition());
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < text_.size()) {
            wchar_t c = text_[pos_++];
            if (c == L'"') return true;
            if (c != L'\\') {
                // RFC 8259: control characters must be escaped
                if (c < 0x20) {
                    return Fail(str::Format(L"an escaped control character, not U+%04X",
                                            static_cast<unsigned>(c)));
                }
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) return Fail(L"an escape character after '\\'");
            wchar_t escape = text_[pos_++];
            switch (escape) {
                case L'"': out.push_back(L'"'); break;
                case L'\\': out.push_back(L'\\'); break;
                case L'/': out.push_back(L'/'); break;
                case L'b': out.push_back(L'\b'); break;
                case L'f': out.push_back(L'\f'); break;
                case L'n': out.push_back(L'\n'); break;
                case L'r': out.push_back(L'\r'); break;
                case L't': out.push_back(L'\t'); break;
                case L'u': {
                    unsigned code = 0;
                    if (!ReadHex4(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        // High surrogate: expect a following \uXXXX low surrogate.
                        if (pos_ + 1 < text_.size() && text_[pos_] == L'\\' && text_[pos_ + 1] == L'u') {
                            pos_ += 2;
                            unsigned low = 0;
                            if (!ReadHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                return Fail(L"a low surrogate (\\uDC00-\\uDFFF) after a high one",
                                            DescribePosition());
                            }
                        } else {
                            return Fail(L"a low surrogate after a high one", DescribePosition());
                        }
                    }
                    // A code still in the low-surrogate range was not consumed
                    // as part of a pair above, so it would be a lone surrogate.
                    if (code >= 0xDC00 && code <= 0xDFFF) {
                        return Fail(L"a high surrogate before a low one", DescribePosition());
                    }
                    AppendUtf32(out, code);
                    break;
                }
                default:
                    return Fail(str::Format(L"a known escape, not '\\%lc'", escape));
            }
        }
        return Fail(L"a closing '\"'");  // unterminated string
    }

    bool ReadHex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) return Fail(L"four hex digits after '\\u'", L"end of input");
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const wchar_t c = text_[pos_];
            unsigned digit = 0;
            if (c >= L'0' && c <= L'9') {
                digit = static_cast<unsigned>(c - L'0');
            } else if (c >= L'a' && c <= L'f') {
                digit = static_cast<unsigned>(c - L'a' + 10);
            } else if (c >= L'A' && c <= L'F') {
                digit = static_cast<unsigned>(c - L'A' + 10);
            } else {
                // pos_ is only advanced past a digit once it has been accepted,
                // so a bad one leaves it on the character the message names:
                // the offset and the quoted character are the same position,
                // as they are for every other failure.
                return Fail(L"a hex digit", str::Format(L"'%lc'", c));
            }
            ++pos_;
            code = (code << 4) | digit;
        }
        return true;
    }

    static void AppendUtf32(std::wstring& out, unsigned code) {
        if (code > 0xFFFF) {
            // Encode as a surrogate pair so the result is valid UTF-16.
            unsigned value = code - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(code));
        }
    }

    wchar_t Peek() const { return pos_ < text_.size() ? text_[pos_] : L'\0'; }

    std::wstring_view text_;
    std::wstring* error_ = nullptr;
    size_t pos_ = 0;
    int depth_ = 0;
    bool reported_ = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value Value::Bool(bool b) {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = b;
    return v;
}

Value Value::Number(double number) {
    Value v;
    v.type_ = Type::Number;
    v.number_ = number;
    return v;
}

Value Value::String(std::wstring text) {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(text);
    return v;
}

Value Value::Array() {
    Value v;
    v.type_ = Type::Array;
    return v;
}

Value Value::Object() {
    Value v;
    v.type_ = Type::Object;
    return v;
}

const Value* Value::Find(std::wstring_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& entry : object_) {
        if (entry.first == key) return &entry.second;
    }
    return nullptr;
}

Value& Value::At(std::wstring_view key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
        array_.clear();
    }
    for (auto& entry : object_) {
        if (entry.first == key) return entry.second;
    }
    object_.emplace_back(std::wstring(key), Value());
    return object_.back().second;
}

void Value::Set(std::wstring_view key, Value value) {
    At(key) = std::move(value);
}

std::vector<std::wstring> Value::Keys() const {
    std::vector<std::wstring> keys;
    if (type_ != Type::Object) return keys;
    keys.reserve(object_.size());
    for (const auto& entry : object_) keys.push_back(entry.first);
    return keys;
}

bool Value::GetBool(std::wstring_view key, bool fallback) const {
    const Value* found = Find(key);
    return found != nullptr ? found->AsBool(fallback) : fallback;
}

double Value::GetNumber(std::wstring_view key, double fallback) const {
    const Value* found = Find(key);
    return found != nullptr ? found->AsNumber(fallback) : fallback;
}

std::wstring Value::GetString(std::wstring_view key, std::wstring_view fallback) const {
    const Value* found = Find(key);
    if (found == nullptr || !found->IsString()) return std::wstring(fallback);
    return found->string_;
}

void Value::Push(Value value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
        object_.clear();
    }
    array_.push_back(std::move(value));
}

// Containers only - see Json.h. Mixing the string length in here used to let a
// non-empty JSON string answer "is this array non-empty?" with yes.
size_t Value::Size() const noexcept {
    switch (type_) {
        case Type::Array: return array_.size();
        case Type::Object: return object_.size();
        default: return 0;
    }
}

size_t Value::Length() const noexcept {
    return type_ == Type::String ? string_.size() : 0;
}

bool Value::AsBool(bool fallback) const {
    switch (type_) {
        case Type::Bool: return bool_;
        case Type::Number: return number_ != 0.0;
        default: return fallback;
    }
}

double Value::AsNumber(double fallback) const {
    switch (type_) {
        case Type::Number: return number_;
        case Type::Bool: return bool_ ? 1.0 : 0.0;
        default: return fallback;
    }
}

const std::wstring& Value::AsString() const {
    return type_ == Type::String ? string_ : kEmptyString;
}

void Value::DumpTo(std::wstring& out, int indent) const {
    const std::wstring pad(static_cast<size_t>(indent) * 2, L' ');
    const std::wstring childPad(static_cast<size_t>(indent + 1) * 2, L' ');
    switch (type_) {
        case Type::Null: out += L"null"; break;
        case Type::Bool: out += bool_ ? L"true" : L"false"; break;
        case Type::Number: AppendNumber(out, number_); break;
        case Type::String: AppendEscaped(out, string_); break;
        case Type::Array: {
            if (array_.empty()) {
                out += L"[]";
                break;
            }
            out += L"[\n";
            for (size_t i = 0; i < array_.size(); ++i) {
                out += childPad;
                array_[i].DumpTo(out, indent + 1);
                out += (i + 1 < array_.size()) ? L",\n" : L"\n";
            }
            out += pad;
            out += L"]";
            break;
        }
        case Type::Object: {
            if (object_.empty()) {
                out += L"{}";
                break;
            }
            out += L"{\n";
            for (size_t i = 0; i < object_.size(); ++i) {
                out += childPad;
                AppendEscaped(out, object_[i].first);
                out += L": ";
                object_[i].second.DumpTo(out, indent + 1);
                out += (i + 1 < object_.size()) ? L",\n" : L"\n";
            }
            out += pad;
            out += L"}";
            break;
        }
    }
}

std::wstring Value::Dump() const {
    std::wstring out;
    DumpTo(out, 0);
    return out;
}

bool Value::Parse(std::wstring_view text, Value& out, std::wstring* error) {
    Parser parser(text, error);
    return parser.Run(out);
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

bool LoadFile(const std::wstring& path, Value& out, std::wstring* error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = str::Format(L"cannot open %ls: %ls", path.c_str(),
                                 str::Win32ErrorMessage(GetLastError()).c_str());
        }
        return false;
    }
    Handle handle(file);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024) {
        if (error != nullptr) *error = str::Format(L"unexpected size of %ls", path.c_str());
        return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        read != bytes.size()) {
        if (error != nullptr) *error = str::Format(L"cannot read %ls", path.c_str());
        return false;
    }
    std::wstring text = str::FromUtf8(bytes);
    if (!text.empty() && text.front() == L'\uFEFF') text.erase(0, 1);  // strip BOM
    return Value::Parse(text, out, error);
}

bool SaveFile(const std::wstring& path, const Value& root, std::wstring* error, SaveMode mode) {
    // qe::CreateDirectories (util/Win32.h) creates the parent chain.
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0) {
        CreateDirectories(path.substr(0, slash));
    }

    const std::string bytes = str::ToUtf8(root.Dump());
    const char bom[] = "\xEF\xBB\xBF";

    // Write to a temporary file and rename it into place. Writing straight to
    // the target with CREATE_ALWAYS would leave a truncated settings.json if
    // the disk fills up or the machine loses power mid-write, and the next
    // start would silently fall back to defaults.
    //
    // The name has to be unique per attempt: PID + thread id + tick alone
    // repeats when one thread saves twice inside a single tick (GetTickCount64
    // has a ~16 ms resolution), and CREATE_NEW would then fail on a file nobody
    // owns. The sequence number is appended until a free name turns up.
    // CreateFileW reports the collision as ERROR_FILE_EXISTS (80);
    // ERROR_ALREADY_EXISTS (183) is accepted too so the retry also survives a
    // rename or directory-creation path that reports it that way.
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < kMaxSaveAttempts; ++attempt) {
        temporary = str::Format(L"%ls.%lu.%lu.%llu.%u.tmp", path.c_str(), GetCurrentProcessId(),
                                GetCurrentThreadId(),
                                static_cast<unsigned long long>(GetTickCount64()), attempt);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD createError = GetLastError();
        if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS) {
            if (error != nullptr) {
                *error = str::Format(L"cannot write %ls: %ls", temporary.c_str(),
                                     str::Win32ErrorMessage(createError).c_str());
            }
            return false;
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = str::Format(L"cannot write %ls: no free temporary name", path.c_str());
        }
        return false;
    }
    Handle handle(file);
    DWORD written = 0;
    BOOL ok = WriteFile(file, bom, 3, &written, nullptr) && written == 3;
    ok = ok && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
         written == bytes.size();
    ok = ok && FlushFileBuffers(file);
    handle.Reset();  // close before the rename
    if (ok == FALSE) {
        DeleteFileW(temporary.c_str());
        RemoveStaleTemporaries(path, temporary);
        if (error != nullptr) *error = str::Format(L"cannot write %ls", temporary.c_str());
        return false;
    }
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (mode == SaveMode::Replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), flags)) {
        const DWORD moveError = GetLastError();
        DeleteFileW(temporary.c_str());
        RemoveStaleTemporaries(path, temporary);
        if (error != nullptr) {
            *error = str::Format(L"cannot replace %ls: %ls", path.c_str(),
                                 str::Win32ErrorMessage(moveError).c_str());
        }
        return false;
    }
    RemoveStaleTemporaries(path, temporary);
    return true;
}

}  // namespace qe::json

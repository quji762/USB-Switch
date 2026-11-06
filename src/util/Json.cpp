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
    if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < 1e15) {
        out += str::Format(L"%lld", static_cast<long long>(number));
        return;
    }
    out += str::Format(L"%.10g", number);
}

class Parser {
public:
    explicit Parser(std::wstring_view text) : text_(text) {}

    bool Run(Value& out, std::wstring* error) {
        SkipWhitespace();
        if (!ParseValue(out)) return Fail(error, L"unexpected token");
        SkipWhitespace();
        if (pos_ != text_.size()) return Fail(error, L"trailing characters after JSON value");
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

    bool Fail(std::wstring* error, const wchar_t* message) const {
        if (error != nullptr) {
            *error = str::Format(L"JSON error at offset %zu: %ls", pos_, message);
        }
        return false;
    }

    bool ParseValue(Value& out) {
        if (pos_ >= text_.size()) return false;
        wchar_t c = text_[pos_];
        if (c == L'{') return ParseObject(out);
        if (c == L'[') return ParseArray(out);
        if (c == L'"') {
            std::wstring text;
            if (!ParseString(text)) return false;
            out = Value::String(std::move(text));
            return true;
        }
        if (c == L't' || c == L'f') return ParseBoolean(out);
        if (c == L'n') return ParseNull(out);
        return ParseNumber(out);
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
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (Peek() != L':') return false;
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
            return false;
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
            return false;
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
        return false;
    }

    bool ParseNull(Value& out) {
        if (text_.substr(pos_, 4) == L"null") {
            pos_ += 4;
            out = Value::Null();
            return true;
        }
        return false;
    }

    bool ParseNumber(Value& out) {
        const wchar_t* begin = text_.data() + pos_;
        wchar_t* end = nullptr;
        double number = std::wcstod(begin, &end);
        if (end == begin) return false;
        pos_ += static_cast<size_t>(end - begin);
        out = Value::Number(number);
        return true;
    }

    bool ParseString(std::wstring& out) {
        if (Peek() != L'"') return false;
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < text_.size()) {
            wchar_t c = text_[pos_++];
            if (c == L'"') return true;
            if (c != L'\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) return false;
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
                                return false;
                            }
                        } else {
                            return false;
                        }
                    }
                    AppendUtf32(out, code);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;  // unterminated string
    }

    bool ReadHex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            wchar_t c = text_[pos_++];
            unsigned digit = 0;
            if (c >= L'0' && c <= L'9') {
                digit = static_cast<unsigned>(c - L'0');
            } else if (c >= L'a' && c <= L'f') {
                digit = static_cast<unsigned>(c - L'a' + 10);
            } else if (c >= L'A' && c <= L'F') {
                digit = static_cast<unsigned>(c - L'A' + 10);
            } else {
                return false;
            }
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
    size_t pos_ = 0;
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

void Value::Erase(std::wstring_view key) {
    if (type_ != Type::Object) return;
    for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it->first == key) {
            object_.erase(it);
            return;
        }
    }
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

size_t Value::Size() const noexcept {
    switch (type_) {
        case Type::Array: return array_.size();
        case Type::Object: return object_.size();
        case Type::String: return string_.size();
        default: return 0;
    }
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
    Parser parser(text);
    return parser.Run(out, error);
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

bool LoadFile(const std::wstring& path, Value& out, std::wstring* error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
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

bool SaveFile(const std::wstring& path, const Value& root, std::wstring* error) {
    // qe::CreateDirectories (util/Win32.h) creates the parent chain.
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0) {
        CreateDirectories(path.substr(0, slash));
    }

    std::string bytes = str::ToUtf8(root.Dump());
    const char bom[] = "\xEF\xBB\xBF";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = str::Format(L"cannot write %ls: %ls", path.c_str(),
                                 str::Win32ErrorMessage(GetLastError()).c_str());
        }
        return false;
    }
    Handle handle(file);
    DWORD written = 0;
    BOOL ok = WriteFile(file, bom, 3, &written, nullptr);
    ok = ok && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    if (!ok && error != nullptr) {
        *error = str::Format(L"cannot write %ls", path.c_str());
    }
    return ok != FALSE;
}

}  // namespace qe::json

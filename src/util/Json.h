#pragma once
// Minimal JSON reader/writer.
//
// Scope: exactly what this application needs - objects, arrays, strings,
// numbers, booleans and null - with UTF-8 files on disk. It is not a general
// purpose library: no duplicate-key detection, no float precision guarantees
// beyond what %.10g gives, and no streaming API.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qe::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    static Value Null() { return Value(); }
    static Value Bool(bool b);
    static Value Number(double number);
    static Value String(std::wstring text);
    static Value Array();
    static Value Object();

    Type type() const noexcept { return type_; }
    bool IsNull() const noexcept { return type_ == Type::Null; }
    bool IsObject() const noexcept { return type_ == Type::Object; }
    bool IsArray() const noexcept { return type_ == Type::Array; }
    bool IsString() const noexcept { return type_ == Type::String; }

    // ---------------- Object API ----------------
    // Returns nullptr when the key is missing or the value is not an object.
    const Value* Find(std::wstring_view key) const;

    // Returns (creating if needed) the child stored under `key`.
    Value& At(std::wstring_view key);

    void Set(std::wstring_view key, Value value);
    std::vector<std::wstring> Keys() const;

    bool GetBool(std::wstring_view key, bool fallback) const;
    double GetNumber(std::wstring_view key, double fallback) const;
    std::wstring GetString(std::wstring_view key, std::wstring_view fallback) const;

    // ---------------- Array API ----------------
    void Push(Value value);
    size_t Size() const noexcept;
    const std::vector<Value>& Items() const noexcept { return array_; }
    std::vector<Value>& Items() noexcept { return array_; }

    // ---------------- Scalars ----------------
    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0.0) const;
    // Reference stays valid while this Value lives; non-strings yield the
    // shared empty string.
    const std::wstring& AsString() const;

    // ---------------- Serialization ----------------
    // Pretty printed, 2-space indentation, LF line endings.
    std::wstring Dump() const;

    static bool Parse(std::wstring_view text, Value& out, std::wstring* error = nullptr);

private:
    void DumpTo(std::wstring& out, int indent) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::wstring string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::wstring, Value>> object_;
};

// Reads a UTF-8 (BOM optional) JSON file. Returns false and fills `error`.
bool LoadFile(const std::wstring& path, Value& out, std::wstring* error = nullptr);

// Writes a UTF-8 file (with BOM) creating parent directories as needed.
enum class SaveMode { Replace, CreateOnly };
bool SaveFile(const std::wstring& path, const Value& root, std::wstring* error = nullptr,
              SaveMode mode = SaveMode::Replace);

}  // namespace qe::json

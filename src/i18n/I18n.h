#pragma once
// Language selection and string formatting.
//
// "auto" resolves from the Windows UI language; "zh"/"en" force a language.
// Placeholders in the string table are %1, %2, %3.

#include <string>
#include <string_view>

#include "i18n/Strings.h"

namespace qe::i18n {

// Implementation detail of the string table (Strings.cpp).
const wchar_t* Lookup(StrId id, bool chinese);

class I18n {
public:
    static I18n& Instance();

    // `preference` is "auto", "zh" or "en"; anything else falls back to auto.
    void Init(std::wstring_view preference);
    void SetLanguage(std::wstring_view preference);

    const wchar_t* Get(StrId id) const;

    // Substitutes %1/%2/%3 left to right; extra arguments are ignored.
    std::wstring Format(StrId id, std::wstring_view a1 = {}, std::wstring_view a2 = {},
                        std::wstring_view a3 = {}) const;

    bool IsChinese() const noexcept { return chinese_; }
    const std::wstring& Language() const noexcept { return language_; }

    // UI language of the OS: "zh" for Chinese, otherwise "en".
    static std::wstring DetectSystemLanguage();

private:
    std::wstring language_ = L"auto";
    bool chinese_ = false;
};

}  // namespace qe::i18n

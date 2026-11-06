#include "i18n/I18n.h"

#include <windows.h>

#include "util/Str.h"

namespace qe::i18n {
namespace {

constexpr wchar_t kAuto[] = L"auto";
constexpr wchar_t kChinese[] = L"zh";
constexpr wchar_t kEnglish[] = L"en";

}  // namespace

I18n& I18n::Instance() {
    static I18n instance;
    return instance;
}

std::wstring I18n::DetectSystemLanguage() {
    LANGID language = GetUserDefaultUILanguage();
    return PRIMARYLANGID(language) == LANG_CHINESE ? kChinese : kEnglish;
}

void I18n::Init(std::wstring_view preference) {
    SetLanguage(preference);
}

void I18n::SetLanguage(std::wstring_view preference) {
    std::wstring resolved = preference.empty() ? kAuto : std::wstring(preference);
    if (resolved == kAuto) {
        resolved = DetectSystemLanguage();
    } else if (resolved != kChinese && resolved != kEnglish) {
        resolved = DetectSystemLanguage();
    }
    language_ = resolved;
    chinese_ = (resolved == kChinese);
}

const wchar_t* I18n::Get(StrId id) const {
    return Lookup(id, chinese_);
}

std::wstring I18n::Format(StrId id, std::wstring_view a1, std::wstring_view a2,
                          std::wstring_view a3) const {
    std::wstring text = Get(id);
    const wchar_t* placeholders[] = {L"%1", L"%2", L"%3"};
    const std::wstring_view values[] = {a1, a2, a3};
    for (int i = 0; i < 3; ++i) {
        if (text.find(placeholders[i]) == std::wstring::npos) continue;
        text = str::ReplaceAll(text, placeholders[i], values[i]);
    }
    return text;
}

}  // namespace qe::i18n

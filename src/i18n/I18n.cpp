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
    const std::wstring_view text = Get(id);
    const std::wstring_view values[] = {a1, a2, a3};

    // Single pass: a sequential ReplaceAll per placeholder would re-substitute
    // inside an argument's own text (a volume label such as "disk%2" would be
    // mangled), because the injected content is scanned again by later passes.
    std::wstring out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'%' && i + 1 < text.size()) {
            const wchar_t digit = text[i + 1];
            if (digit >= L'1' && digit <= L'3') {
                out.append(values[static_cast<size_t>(digit - L'1')]);
                ++i;  // consume the digit
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

}  // namespace qe::i18n

#include "app/Settings.h"

#include <windows.h>

#include "app/Logger.h"
#include "app/AppInfo.h"
#include "util/Str.h"

namespace qe {
namespace {

std::wstring EnvironmentOr(std::wstring_view name, std::wstring_view fallback) {
    DWORD length = GetEnvironmentVariableW(std::wstring(name).c_str(), nullptr, 0);
    if (length == 0) return std::wstring(fallback);
    std::wstring value(static_cast<size_t>(length), L'\0');
    DWORD copied = GetEnvironmentVariableW(std::wstring(name).c_str(), value.data(), length);
    if (copied == 0 || copied >= length) return std::wstring(fallback);
    value.resize(copied);
    return value;
}

}  // namespace

AppSettings AppSettings::FromJson(const json::Value& root) {
    AppSettings settings;
    if (!root.IsObject()) return settings;

    std::wstring language = root.GetString(L"language", L"auto");
    if (!language.empty()) settings.language = language;

    settings.runAtStartup = root.GetBool(L"runAtStartup", settings.runAtStartup);
    settings.showNotifications = root.GetBool(L"showNotifications", settings.showNotifications);
    settings.confirmBeforeEject = root.GetBool(L"confirmBeforeEject", settings.confirmBeforeEject);
    return settings;
}

json::Value AppSettings::ToJson() const {
    json::Value root = json::Value::Object();
    root.Set(L"language", json::Value::String(language));
    root.Set(L"runAtStartup", json::Value::Bool(runAtStartup));
    root.Set(L"showNotifications", json::Value::Bool(showNotifications));
    root.Set(L"confirmBeforeEject", json::Value::Bool(confirmBeforeEject));
    return root;
}

Settings& Settings::Instance() {
    static Settings instance;
    return instance;
}

std::wstring Settings::AppDataDirectory() {
    std::wstring roaming = EnvironmentOr(L"APPDATA", L"");
    if (roaming.empty()) {
        // Last-resort fallback: the user profile directory.
        roaming = EnvironmentOr(L"USERPROFILE", L"C:\\");
    }
    if (!roaming.empty() && roaming.back() == L'\\') roaming.pop_back();
    return roaming + L"\\" + QUICKEJECT_APP_NAME_W;
}

std::wstring Settings::FilePath() {
    return AppDataDirectory() + L"\\settings.json";
}

void Settings::Load() {
    const std::wstring path = FilePath();
    json::Value root;
    std::wstring error;
    if (json::LoadFile(path, root, &error)) {
        data_ = AppSettings::FromJson(root);
        QE_LOG_INFO(L"settings loaded from %ls", path.c_str());
        return;
    }
    QE_LOG_INFO(L"using default settings (%ls)", error.c_str());
    data_ = AppSettings{};
}

bool Settings::Save() const {
    std::wstring error;
    if (json::SaveFile(FilePath(), data_.ToJson(), &error)) {
        QE_LOG_DEBUG(L"settings saved to %ls", FilePath().c_str());
        return true;
    }
    QE_LOG_ERROR(L"could not save settings: %ls", error.c_str());
    return false;
}

}  // namespace qe

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

    auto mode = root.GetString(L"appearance", L"auto");
    if (mode == L"light" || mode == L"dark") settings.appearance = mode;
    auto key = root.GetString(L"menuHotkey", L"U");
    if (key.empty() || (key.size() == 1 && key[0] >= L'A' && key[0] <= L'Z')) settings.menuHotkey = key;
    if (const auto* aliases = root.Find(L"deviceAliases"); aliases && aliases->IsObject()) {
        for (const auto& id : aliases->Keys()) {
            auto name = str::Trim(aliases->GetString(id, L""));
            if (!id.empty() && !name.empty() && name.size() <= 64 && name.find_first_of(L"\r\n\t") == name.npos)
                settings.aliases[id] = name;
        }
    }
    if (const auto* hidden = root.Find(L"hiddenDevices"); hidden && hidden->IsObject()) {
        for (const auto& id : hidden->Keys()) {
            auto name = hidden->GetString(id, L"");
            if (!id.empty() && !name.empty()) settings.hiddenDevices[str::Lower(id)] = std::move(name);
        }
    }
    settings.runAtStartup = root.GetBool(L"runAtStartup", settings.runAtStartup);
    settings.showNotifications = root.GetBool(L"showNotifications", settings.showNotifications);
    settings.confirmBeforeEject = root.GetBool(L"confirmBeforeEject", settings.confirmBeforeEject);
    return settings;
}

json::Value AppSettings::ToJson() const {
    json::Value root = json::Value::Object();
    root.Set(L"menuHotkey", json::Value::String(menuHotkey));
    auto names = json::Value::Object();
    for (const auto& item : aliases) names.Set(item.first, json::Value::String(item.second));
    root.Set(L"deviceAliases", std::move(names));
    auto hidden = json::Value::Object();
    for (const auto& item : hiddenDevices) hidden.Set(item.first, json::Value::String(item.second));
    root.Set(L"hiddenDevices", std::move(hidden));
    root.Set(L"appearance", json::Value::String(appearance));
    root.Set(L"language", json::Value::String(language));
    root.Set(L"runAtStartup", json::Value::Bool(runAtStartup));
    root.Set(L"showNotifications", json::Value::Bool(showNotifications));
    root.Set(L"confirmBeforeEject", json::Value::Bool(confirmBeforeEject));
    return root;
}

std::wstring AppSettings::DeviceName(const std::wstring& id, const std::wstring& original) const {
    auto it = aliases.find(id);
    return it == aliases.end() ? original : it->second + L" — " + original;
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
    // Only fall back when the new file is absent. A damaged or inaccessible
    // new file must never be silently replaced by stale settings.
    const DWORD attributes = GetFileAttributesW(path.c_str());
    const DWORD pathError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (pathError == ERROR_FILE_NOT_FOUND || pathError == ERROR_PATH_NOT_FOUND) {
        const std::wstring directory = AppDataDirectory();
        const size_t separator = directory.find_last_of(L"\\/");
        const std::wstring legacy = directory.substr(0, separator) + L"\\QuickEject\\settings.json";
        if (json::LoadFile(legacy, root, nullptr) && root.IsObject()) {
            data_ = AppSettings::FromJson(root);
            if (json::SaveFile(path, data_.ToJson(), &error, json::SaveMode::CreateOnly)) {
                QE_LOG_INFO(L"migrated settings from %ls", legacy.c_str());
            } else if (json::LoadFile(path, root, nullptr)) {
                // Another process created current settings after our initial read.
                data_ = AppSettings::FromJson(root);
            } else {
                QE_LOG_WARN(L"could not persist migrated settings: %ls", error.c_str());
            }
            return;
        }
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

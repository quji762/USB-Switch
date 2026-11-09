#include "app/Settings.h"

#include <windows.h>

#include "app/Logger.h"
#include "app/AppInfo.h"
#include "util/Str.h"
#include "util/Win32.h"

namespace qe {
namespace {

// Top-level keys this build reads, plus the schema marker it writes. Any other
// member of a settings file is kept verbatim in AppSettings::extra so a newer
// version's keys survive when this older build saves its defaults over them.
constexpr const wchar_t* kKnownKeys[] = {
    L"language", L"appearance", L"menuHotkey", L"deviceAliases", L"hiddenDevices",
    L"runAtStartup", L"showNotifications", L"confirmBeforeEject", L"smartDisplay",
    L"version",
};

bool IsKnownKey(std::wstring_view key) {
    for (const wchar_t* candidate : kKnownKeys) {
        if (key == candidate) return true;
    }
    return false;
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
    if (const auto* smart = root.Find(L"smartDisplay"); smart && smart->IsObject()) {
        settings.showTemperature = smart->GetBool(L"temperature", settings.showTemperature);
        settings.showPowerOnHours = smart->GetBool(L"powerOnHours", settings.showPowerOnHours);
        settings.showPowerCycles = smart->GetBool(L"powerCycles", settings.showPowerCycles);
        settings.showReallocated = smart->GetBool(L"reallocated", settings.showReallocated);
        settings.showPendingSectors = smart->GetBool(L"pending", settings.showPendingSectors);
        // Same rule as top-level extra, one level down: members a newer version
        // added inside smartDisplay survive this build's saves.
        for (const auto& sub : smart->Keys()) {
            if (sub == L"temperature" || sub == L"powerOnHours" || sub == L"powerCycles" ||
                sub == L"reallocated" || sub == L"pending")
                continue;
            if (const json::Value* value = smart->Find(sub)) settings.smartExtra[sub] = *value;
        }
    }
    // Members this build does not understand are stored unchanged so the next
    // Save() writes them back instead of silently dropping data a newer
    // version had put into the file.
    for (const auto& extraKey : root.Keys()) {
        if (!IsKnownKey(extraKey)) {
            if (const json::Value* value = root.Find(extraKey)) settings.extra[extraKey] = *value;
        }
    }
    return settings;
}

json::Value AppSettings::ToJson() const {
    json::Value root = json::Value::Object();
    root.Set(L"version", json::Value::Number(1));  // schema version; readers ignore it for now
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
    auto smart = json::Value::Object();
    smart.Set(L"temperature", json::Value::Bool(showTemperature));
    smart.Set(L"powerOnHours", json::Value::Bool(showPowerOnHours));
    smart.Set(L"powerCycles", json::Value::Bool(showPowerCycles));
    smart.Set(L"reallocated", json::Value::Bool(showReallocated));
    smart.Set(L"pending", json::Value::Bool(showPendingSectors));
    for (const auto& item : smartExtra) smart.Set(item.first, item.second);
    root.Set(L"smartDisplay", std::move(smart));
    // Unknown keys read from a newer version are written back after the known
    // ones. `extra` never holds a known key, so this cannot shadow one; the
    // ordered map merely keeps the output deterministic.
    for (const auto& item : extra) root.Set(item.first, item.second);
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
    // The file exists but could not be read as JSON. Preserve its bytes before
    // the next Save() replaces the file, so a half-readable settings file can
    // still be recovered by hand instead of being silently lost to defaults.
    if (pathError != ERROR_FILE_NOT_FOUND && pathError != ERROR_PATH_NOT_FOUND) {
        const std::wstring corruptBackup = path + L".corrupt";
        if (CopyFileW(path.c_str(), corruptBackup.c_str(), FALSE)) {
            QE_LOG_WARN(L"unreadable settings preserved as %ls", corruptBackup.c_str());
        } else {
            QE_LOG_WARN(L"could not preserve unreadable settings: %ls",
                        str::Win32ErrorMessage(GetLastError()).c_str());
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

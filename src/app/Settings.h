#pragma once
// Persistent user settings, stored as JSON in %APPDATA%\USB-Switch\settings.json.
//
// Every field has a default so a missing or corrupt file simply yields
// defaults (and the file is rewritten on the next change) - the app never
// refuses to start because of bad settings.

#include <map>
#include <string>

#include "util/Json.h"

namespace qe {

struct AppSettings {
    // "auto" (follow the UI language), "zh" or "en".
    std::wstring language = L"auto";
    std::wstring menuHotkey = L"U"; // Ctrl+Alt plus this letter; empty disables.
    std::map<std::wstring, std::wstring> aliases; // PnP instance ID -> local display name
    std::map<std::wstring, std::wstring> hiddenDevices; // lowercase instance ID -> last displayed name
    std::wstring DeviceName(const std::wstring& id, const std::wstring& original) const;
    bool runAtStartup = false;
    bool showNotifications = true;
    bool confirmBeforeEject = true;

    static AppSettings FromJson(const json::Value& root);
    json::Value ToJson() const;
};

class Settings {
public:
    static Settings& Instance();

    void Load();
    bool Save() const;

    AppSettings& Data() noexcept { return data_; }
    const AppSettings& Data() const noexcept { return data_; }

    // %APPDATA%\USB-Switch (created on demand).
    static std::wstring AppDataDirectory();
    static std::wstring FilePath();

private:
    AppSettings data_;
};

}  // namespace qe

#include "app/App.h"

#include <string>

#include "app/AppInfo.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "app/Updater.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "util/Str.h"

namespace qe {
namespace {

std::wstring EnvironmentOr(std::wstring_view name, std::wstring_view fallback) {
    DWORD length = GetEnvironmentVariableW(std::wstring(name).c_str(), nullptr, 0);
    if (length == 0) return std::wstring(fallback);
    std::wstring value(static_cast<size_t>(length), L'\0');
    const DWORD copied =
        GetEnvironmentVariableW(std::wstring(name).c_str(), value.data(), length);
    if (copied == 0 || copied >= length) return std::wstring(fallback);
    value.resize(copied);
    return value;
}

}  // namespace

std::wstring LogDirectory() {
    std::wstring local = EnvironmentOr(L"LOCALAPPDATA", L"");
    if (local.empty()) local = EnvironmentOr(L"TEMP", L"C:\\Windows\\Temp");
    if (!local.empty() && local.back() == L'\\') local.pop_back();
    return local + L"\\" + QUICKEJECT_APP_NAME_W + L"\\logs";
}

App::App(HINSTANCE instance) : instance_(instance) {}

App::~App() { Logger::Instance().Shutdown(); }

InitStatus App::Init() {
    Logger::Instance().Init(LogDirectory());

    if (!singleInstance_.Acquire(L"QuickEject")) {
        SingleInstance::SignalExisting(MainWindow::kClassName);
        return InitStatus::AlreadyRunning;
    }

    Settings::Instance().Load();
    i18n::I18n::Instance().Init(Settings::Instance().Data().language);

    window_ = std::make_unique<MainWindow>(instance_);
    if (!window_->Create()) {
        const auto& strings = i18n::I18n::Instance();
        MessageBoxW(nullptr, strings.Get(i18n::StrId::ErrorWindowCreation), QUICKEJECT_APP_NAME_W,
                    MB_OK | MB_ICONERROR);
        QE_LOG_ERROR(L"startup failed: %ls", strings.Get(i18n::StrId::ErrorWindowCreation));
        return InitStatus::FatalError;
    }

    // Keep the Run entry pointing at the current executable (the installer
    // moves the binary on upgrade).
    if (Settings::Instance().Data().runAtStartup) {
        window_->ApplyRunAtStartup(true);
    }

    // Extension point: a real build queries the update channel here.
    GetUpdater().CheckAsync(nullptr);
    return InitStatus::Ok;
}

int App::Run() {
    QE_LOG_INFO(L"entering message loop");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

}  // namespace qe

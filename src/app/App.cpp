#include "app/App.h"

#include <string>

#include "app/AppInfo.h"
#include "app/AutoStart.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "app/Updater.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "util/Str.h"
#include "util/Win32.h"

namespace qe {

std::wstring LogDirectory() {
    std::wstring local = EnvironmentOr(L"LOCALAPPDATA", L"");
    if (local.empty()) local = EnvironmentOr(L"TEMP", L"C:\\Windows\\Temp");
    if (!local.empty() && local.back() == L'\\') local.pop_back();
    return local + L"\\" + QUICKEJECT_APP_NAME_W + L"\\logs";
}

App::App(HINSTANCE instance) : instance_(instance) {}

App::~App() { Logger::Instance().Shutdown(); }

InitStatus App::Init(bool openSettings) {
    // Logging, settings and localization are initialized by main() before the
    // App is constructed (the command line paths need them too), so they are
    // deliberately not repeated here: doing so would reopen the log file and
    // re-read the settings, and the "settings loaded" line would appear twice
    // in every GUI run.

    if (!singleInstance_.Acquire(L"USB-Switch")) {
        if (!SingleInstance::SignalExisting(MainWindow::kClassName, openSettings) && openSettings)
            return InitStatus::FatalError;
        return InitStatus::AlreadyRunning;
    }

    window_ = std::make_unique<MainWindow>(instance_);
    if (!window_->Create()) {
        const auto& strings = i18n::I18n::Instance();
        const auto error = window_->TrayCreationFailed() ? i18n::StrId::ErrorTrayCreation : i18n::StrId::ErrorWindowCreation;
        MessageBoxW(nullptr, strings.Get(error), QUICKEJECT_APP_NAME_W,
                    MB_OK | MB_ICONERROR);
        QE_LOG_ERROR(L"startup failed: %ls", strings.Get(error));
        return InitStatus::FatalError;
    }

    // Migrate only an owned legacy entry. Normal launches respect external removal.
    if (Settings::Instance().Data().runAtStartup) {
        const auto command = AutoStart::CommandLine();
        const auto slash = command.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            const auto legacy = command.substr(0, slash + 1) + L"quickeject.exe\"";
            if (AutoStart::Inspect(L"QuickEject", legacy) == AutoStart::Entry::Owned) {
                // A refused write leaves the legacy entry in place while the
                // setting claims startup is on; say so instead of letting the
                // next launch rediscover it with no trace.
                if (AutoStart::SetEnabled(QUICKEJECT_APP_NAME_W, command))
                    AutoStart::ClearIfMatches(L"QuickEject", legacy);
                else
                    QE_LOG_WARN(L"could not migrate the QuickEject startup entry to %ls",
                                command.c_str());
            }
        }
    }

    if (openSettings) PostMessageW(window_->Handle(), kWakeMessage, 1, 0);

    // Extension point: a real build queries the update channel here.
    GetUpdater().CheckAsync(nullptr);
    return InitStatus::Ok;
}

int App::Run() {
    QE_LOG_INFO(L"entering message loop");
    MSG message{};
    for (;;) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }
        if (result == 0) break;  // WM_QUIT
        // result < 0 is an error (for example an invalid window handle); log it
        // instead of exiting silently.
        QE_LOG_ERROR(L"GetMessage failed: %lu", GetLastError());
        // GetMessageW does not fill the MSG on failure, so message still holds
        // whatever the last dispatch left there (zero on a fresh loop):
        // returning message.wParam would report a clean exit for a loop that
        // never received WM_QUIT. A failure exit code is the honest answer.
        return 1;
    }
    return static_cast<int>(message.wParam);
}

}  // namespace qe

#pragma once
// Application shell: startup order, single-instance handling and the message
// loop. Everything user visible lives in MainWindow; this class only wires the
// pieces together in a deterministic order.

#include <memory>

#include <windows.h>

#include "app/SingleInstance.h"
#include "ui/MainWindow.h"

namespace qe {

// %LOCALAPPDATA%\USB-Switch\logs (falls back to the temp directory).
std::wstring LogDirectory();

enum class InitStatus {
    Ok,
    AlreadyRunning,  // another instance owns the tray; this one must exit
    FatalError,      // startup failed
};

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    InitStatus Init(bool openSettings);
    int Run();

private:
    HINSTANCE instance_;
    SingleInstance singleInstance_;
    std::unique_ptr<MainWindow> window_;
};

}  // namespace qe

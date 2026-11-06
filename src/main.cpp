// Entry point.
//
// The binary is a GUI subsystem application (no console window flashes on
// start) that still supports command line use: --list, --eject, --version and
// --help attach to the parent process' console when one exists.

#include <windows.h>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/AppInfo.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "core/Device.h"
#include "core/DeviceManager.h"
#include "core/Ejector.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "util/Str.h"

// Helpers live in qe so they can use qe::str, qe::i18n and friends directly.
namespace qe {
namespace {

struct Options {
    bool listDevices = false;
    bool showHelp = false;
    bool showVersion = false;
    bool usageError = false;  // unknown switch or missing argument
    bool minimized = false;   // accepted (Run key), no effect: always tray resident
    std::wstring ejectTarget;  // drive letter ("E:") or PnP instance id
};

Options ParseOptions(int argc, const wchar_t* const* argv) {
    Options options;
    // Each invocation selects exactly one action (or starts the tray).
    for (int i = 1; i < argc; ++i) {
        if (i != 1) options.usageError = true;
        const std::wstring argument = argv[i];
        // Both "--eject E:" and "--eject=E:" are accepted.
        const bool isEject = argument == L"--eject" || argument == L"-e" ||
                             argument.rfind(L"--eject=", 0) == 0;
        if (argument == L"--list" || argument == L"-l") {
            options.listDevices = true;
        } else if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            options.showHelp = true;
        } else if (argument == L"--version" || argument == L"-v") {
            options.showVersion = true;
        } else if (argument == L"--minimized") {
            options.minimized = true;
        } else if (isEject) {
            if (argument.rfind(L"--eject=", 0) == 0) {
                options.ejectTarget = argument.substr(8);
            } else if (i + 1 < argc && argv[i + 1][0] != L'-') {
                options.ejectTarget = argv[++i];
            } else {
                options.usageError = true;  // missing argument
            }
            if (str::Trim(options.ejectTarget).empty()) options.usageError = true;
        } else {
            options.usageError = true;  // unknown switch
        }
    }
    return options;
}

// A GUI subsystem binary has no console of its own. Three situations:
//   1. stdout redirected to a pipe or file -> write UTF-8 bytes to that handle
//      (checked first: borrowing the parent console would bypass redirection)
//   2. stdout is a console handle          -> WriteConsoleW
//   3. no inherited handle at all          -> attach to the parent console
//   4. nothing available (double clicked)  -> stay silent instead of crashing
enum class OutputTarget { None, File, ConsoleHandle, ConsoleStream };

OutputTarget g_outputTarget = OutputTarget::None;

bool AttachOutput() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        g_outputTarget = (GetConsoleMode(out, &mode) != FALSE) ? OutputTarget::ConsoleHandle
                                                              : OutputTarget::File;
        return true;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) != FALSE && freopen("CONOUT$", "w", stdout) != nullptr) {
        g_outputTarget = OutputTarget::ConsoleStream;
        return true;
    }
    return false;
}

void PrintLine(const std::wstring& text) {
    switch (g_outputTarget) {
        case OutputTarget::File: {
            const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (out == nullptr || out == INVALID_HANDLE_VALUE) break;
            std::string utf8 = str::ToUtf8(text);
            utf8 += "\r\n";
            DWORD written = 0;
            WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            break;
        }
        case OutputTarget::ConsoleHandle: {
            const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (out == nullptr || out == INVALID_HANDLE_VALUE) break;
            DWORD written = 0;
            WriteConsoleW(out, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
            const wchar_t newline = L'\n';
            WriteConsoleW(out, &newline, 1, &written, nullptr);
            break;
        }
        case OutputTarget::ConsoleStream:
            _putws(text.c_str());
            fflush(stdout);
            break;
        case OutputTarget::None:
        default:
            break;
    }
}

std::wstring NormalizeDriveLetter(std::wstring_view input) {
    std::wstring text = str::Trim(input);
    if (text.size() == 1) text += L':';
    if (text.size() != 2 || text[1] != L':') return {};
    if (text[0] >= L'a' && text[0] <= L'z') text[0] = static_cast<wchar_t>(text[0] - L'a' + L'A');
    if (!((text[0] >= L'A' && text[0] <= L'Z'))) return {};
    return text;
}

int RunListDevices() {
    const auto& strings = i18n::I18n::Instance();
    DeviceManager manager;
    if (!manager.Refresh()) {
        PrintLine(strings.Get(i18n::StrId::ErrorEnumFailed));
        return 1;
    }
    const std::vector<Device> devices = manager.Devices();

    PrintLine(strings.Get(i18n::StrId::CliListTitle));
    if (devices.empty()) {
        PrintLine(strings.Get(i18n::StrId::CliNoDevices));
        return 0;
    }

    for (size_t i = 0; i < devices.size(); ++i) {
        const Device& device = devices[i];
        std::wstring details = str::Format(L"bus=%ls, removable=%ls", BusTypeName(device.busType),
                                           device.removable ? L"yes" : L"no");
        if (device.sizeBytes > 0) {
            details += L", " + str::FileSizeString(device.sizeBytes);
        }
        details += device.CanEject() ? L", ejectable=yes" : L", ejectable=no";

        PrintLine(str::Format(L"[%zu] %ls  [%ls]", i + 1, device.displayName.c_str(),
                              details.c_str()));
        if (!device.instanceId.empty()) {
            PrintLine(L"    " + device.instanceId);
        }
    }
    return 0;
}

int RunEject(const std::wstring& target) {
    const auto& strings = i18n::I18n::Instance();
    DeviceManager manager;
    if (!manager.Refresh()) {
        PrintLine(strings.Get(i18n::StrId::ErrorEnumFailed));
        return 1;
    }

    std::optional<Device> device;
    const std::wstring drive = NormalizeDriveLetter(target);
    if (!drive.empty()) {
        device = manager.FindByDriveLetter(drive);
    } else {
        device = manager.FindByInstanceId(target);
    }
    if (!device) {
        PrintLine(strings.Format(i18n::StrId::CliUnknownDevice, target));
        return 2;
    }

    const EjectResult result = Ejector::Eject(device->instanceId);
    if (result.Succeeded()) {
        PrintLine(strings.Format(i18n::StrId::CliEjectOk, device->displayName));
        return 0;
    }
    PrintLine(strings.Format(i18n::StrId::CliEjectFailed, device->displayName,
                             Ejector::Describe(result)));
    return 1;
}

}  // namespace
}  // namespace qe

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        MessageBoxW(nullptr, L"CommandLineToArgvW failed", QUICKEJECT_APP_NAME_W, MB_OK | MB_ICONERROR);
        return 1;
    }
    const qe::Options options = qe::ParseOptions(argc, argv);

    // Log and settings come first: even a CLI run should leave a trace and use
    // the configured language.
    qe::Logger::Instance().Init(qe::LogDirectory());
    QE_LOG_INFO(L"%ls starting, command line: %ls", QUICKEJECT_NAME_AND_VERSION_W,
                GetCommandLineW());
    qe::Settings::Instance().Load();
    qe::i18n::I18n::Instance().Init(qe::Settings::Instance().Data().language);

    const bool commandLineAction =
        options.listDevices || options.showHelp || options.showVersion ||
        options.usageError || !options.ejectTarget.empty();
    if (commandLineAction && !qe::AttachOutput()) {
        QE_LOG_WARN(L"no output handle; command line output is unavailable");
    }

    const auto& strings = qe::i18n::I18n::Instance();
    int exitCode = 0;
    if (options.showHelp || options.usageError) {
        qe::PrintLine(strings.Get(qe::i18n::StrId::CliUsage));
        // A usage error is not a successful run: --help is, a bad switch is not.
        exitCode = options.usageError ? 2 : 0;
    } else if (options.showVersion) {
        qe::PrintLine(strings.Get(qe::i18n::StrId::CliVersion));
        exitCode = 0;
    } else if (options.listDevices) {
        exitCode = qe::RunListDevices();
    } else if (!options.ejectTarget.empty()) {
        exitCode = qe::RunEject(options.ejectTarget);
    } else {
        qe::App app(instance);
        const qe::InitStatus status = app.Init();
        if (status == qe::InitStatus::Ok) {
            exitCode = app.Run();
        } else {
            exitCode = (status == qe::InitStatus::FatalError) ? 1 : 0;
        }
    }

    qe::Logger::Instance().Shutdown();
    LocalFree(argv);
    return exitCode;
}

// Entry point.
//
// The binary is a GUI subsystem application (no console window flashes on
// start) that still supports command line use: --list, --eject, --version and
// --help attach to the parent process' console when one exists.

#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/AppInfo.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "core/Device.h"
#include "core/DeviceJson.h"
#include "core/DeviceManager.h"
#include "core/Ejector.h"
#include "core/MediaEject.h"
#include "core/OpenFiles.h"
#include "core/UsbPort.h"
#include "core/EjectJson.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "util/Str.h"
#include "util/Win32.h"

// Helpers live in qe so they can use qe::str, qe::i18n and friends directly.
namespace qe {
namespace {

struct Options {
    bool listDevices = false;
    bool mediaOnly = false;
    bool inspectFiles = false;
    bool jsonOutput = false;
    bool showHelp = false;
    bool showVersion = false;
    bool usageError = false;  // unknown switch or missing argument
    bool openSettings = false;
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
            if (i + 1 < argc && std::wstring_view(argv[i + 1]) == L"--json") {
                options.jsonOutput = true;
                ++i;
            }
        } else if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            options.showHelp = true;
        } else if (argument == L"--version" || argument == L"-v") {
            options.showVersion = true;
        } else if (argument == L"--settings") {
            options.openSettings = true;
        } else if (argument == L"--minimized") {
            options.minimized = true;
        } else if (isEject || argument == L"--eject-media") {
            options.mediaOnly = argument == L"--eject-media";
            if (argument.rfind(L"--eject=", 0) == 0) {
                options.ejectTarget = argument.substr(8);
            } else if (i + 1 < argc && argv[i + 1][0] != L'-') {
                options.ejectTarget = argv[++i];
            } else {
                options.usageError = true;  // missing argument
            }
            if (str::Trim(options.ejectTarget).empty()) options.usageError = true;
            if (!options.mediaOnly && i+1<argc && std::wstring_view(argv[i+1])==L"--files") {
                options.inspectFiles=true;
                ++i;
                if (i+1>=argc || std::wstring_view(argv[i+1])!=L"--json") options.usageError=true;
            }
            if (i + 1 < argc && std::wstring_view(argv[i + 1]) == L"--json") {
                options.jsonOutput = true;
                ++i;
            }
        } else {
            options.usageError = true;  // unknown switch
        }
    }
    return options;
}

// Prefer the inherited handle so a file or pipe redirection stays intact.
HANDLE g_output = nullptr;
Handle g_attachedOutput;
bool g_console = false;
bool g_writeFailed = false;

bool AttachOutput() {
    g_output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!g_output || g_output == INVALID_HANDLE_VALUE) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
        g_attachedOutput.Reset(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!g_attachedOutput) return false;
        g_output = g_attachedOutput.Get();
    }
    DWORD mode = 0;
    g_console = GetConsoleMode(g_output, &mode) != FALSE;
    return true;
}

void PrintLine(const std::wstring& text) {
    if (!g_output || g_output == INVALID_HANDLE_VALUE || g_writeFailed) return;
    const std::wstring line = text + L"\r\n";
    std::string bytes;
    if (!g_console) bytes = str::ToUtf8(line);
    const size_t count = g_console ? line.size() : bytes.size();
    for (size_t pos = 0; pos < count;) {
        DWORD chunk = static_cast<DWORD>((std::min)(count - pos, size_t{8192}));
        if (g_console && pos + chunk < count && IS_HIGH_SURROGATE(line[pos + chunk - 1])) --chunk;
        DWORD written = 0;
        const BOOL ok = g_console ? WriteConsoleW(g_output, line.data() + pos, chunk, &written, nullptr)
                                  : WriteFile(g_output, bytes.data() + pos, chunk, &written, nullptr);
        if (!ok || !written) {
            g_writeFailed = true;
            QE_LOG_WARN(L"command output write failed (%lu)", GetLastError());
            return;
        }
        pos += written;
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

int RunListDevices(bool jsonOutput) {
    const auto& strings = i18n::I18n::Instance();
    DeviceManager manager;
    if (!manager.Refresh()) {
        PrintLine(jsonOutput ? L"{\"error\":\"enumeration_failed\"}" : strings.Get(i18n::StrId::ErrorEnumFailed));
        return 1;
    }
    const std::vector<Device> devices = manager.Devices();
    if (jsonOutput) {
        auto rows = json::Value::Array();
        for (const auto& device : devices) rows.Push(DeviceJson(device));
        PrintLine(rows.Dump());
        return 0;
    }

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

int RunEject(const std::wstring& target, bool jsonOutput, bool mediaOnly, bool files) {
    const auto& strings = i18n::I18n::Instance();
    DeviceManager manager;
    if (!manager.Refresh()) {
        PrintLine(jsonOutput ? LR"({"status":"enumeration_failed","retryable":false})" : strings.Get(i18n::StrId::ErrorEnumFailed));
        return 1;
    }

    std::optional<Device> device;
    const std::wstring drive = NormalizeDriveLetter(target);
    if (!drive.empty()) {
        device = manager.FindByDriveLetter(drive);
    } else {
        device = manager.FindByInstanceId(target);
        if (!device) device = FindByMountPath(manager.Devices(), target);
    }
    if (!device) {
        PrintLine(jsonOutput ? LR"({"status":"not_found","retryable":false})" : strings.Format(i18n::StrId::CliUnknownDevice, target));
        return 2;
    }

    if (mediaOnly) {
        const auto result = EjectMedia(*device);
        if (jsonOutput) {
            auto row = json::Value::Object();
            row.Set(L"status", json::Value::String(result.stage));
            row.Set(L"win32Error", json::Value::Number(result.error));
            row.Set(L"instanceId", json::Value::String(device->instanceId));
            PrintLine(row.Dump());
        } else {
            PrintLine(device->displayName + L": " + result.stage + L" (" + str::Win32ErrorMessage(result.error) + L")");
        }
        return result.error ? 1 : 0;
    }
    const EjectResult result = Ejector::Eject(device->instanceId);
    if (jsonOutput) {
        auto report=EjectJson(result, device->instanceId);
        if (files && result.status==EjectStatus::Vetoed)
            report.Set(L"fileInspection",InspectOpenFiles(device->instanceId,result));
        PrintLine(report.Dump());
        return result.Succeeded() ? 0 : 1;
    }
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
    if (argc == 6 && std::wstring_view(argv[1]) == L"--file-worker") {
        int rc=qe::FileWorker(argv[2],argv[3],argv[4],argv[5]);
        LocalFree(argv); return rc;
    }
    if (argc == 3 && std::wstring_view(argv[1]) == L"--restart-held-usb") {
        // Elevated helper: no tray, settings, log files or external commands.
        const DWORD result = qe::ReconnectUsbPort(argv[2], false);
        LocalFree(argv);
        return static_cast<int>(result);
    }
    if (argc == 4 && std::wstring_view(argv[1]) == L"--restart-held-node") {
        const DWORD result = qe::Ejector::RestartStopped(argv[2], argv[3]);
        LocalFree(argv);
        return static_cast<int>(result);
    }
    const qe::Options options = qe::ParseOptions(argc, argv);

    // Log and settings come first: even a CLI run should leave a trace and use
    // the configured language.
    const bool commandLineAction =
        options.listDevices || options.showHelp || options.showVersion ||
        options.usageError || !options.ejectTarget.empty();
    qe::Logger::Instance().Init(qe::LogDirectory(), commandLineAction ? L"usb-switch-cli.log" : L"usb-switch.log");
    QE_LOG_INFO(L"%ls starting (%ls)", QUICKEJECT_NAME_AND_VERSION_W, commandLineAction ? L"CLI" : L"tray");
    qe::Settings::Instance().Load();
    qe::i18n::I18n::Instance().Init(qe::Settings::Instance().Data().language);

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
        exitCode = qe::RunListDevices(options.jsonOutput);
    } else if (!options.ejectTarget.empty()) {
        exitCode = qe::RunEject(options.ejectTarget, options.jsonOutput, options.mediaOnly, options.inspectFiles);
    } else {
        qe::App app(instance);
        const qe::InitStatus status = app.Init(options.openSettings);
        if (status == qe::InitStatus::Ok) {
            exitCode = app.Run();
        } else {
            exitCode = (status == qe::InitStatus::FatalError) ? 1 : 0;
        }
    }

    qe::Logger::Instance().Shutdown();
    LocalFree(argv);
    // A successful removal stays successful even if its confirmation cannot be printed.
    if (exitCode == 0 && options.ejectTarget.empty() && qe::g_writeFailed) return 1;
    return exitCode;
}

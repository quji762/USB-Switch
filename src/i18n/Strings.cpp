#include "i18n/Strings.h"

#include <iterator>

#include "app/AppInfo.h"

namespace qe::i18n {
namespace {

struct Entry {
    StrId id;
    const wchar_t* zh;  // Simplified Chinese
    const wchar_t* en;  // English
};

// Keep this table in the same order as the enum above; the code indexes it
// directly and a static_assert guards the size.
const Entry kStrings[] = {
    // App identity / tray
    {StrId::AppTooltip, L"USB-Switch — USB 安全弹出", L"USB-Switch — USB Safe Remove"},
    {StrId::TrayHintRunning, L"USB-Switch 已在运行，请使用托盘图标。",
     L"USB-Switch is already running; use the tray icon."},

    // Tray menu
    {StrId::MenuDevices, L"可移动设备(%1)", L"Removable devices (%1)"},
    {StrId::MenuNoDevices, L"(没有可移动设备)", L"(no removable devices)"},
    {StrId::MenuEjectItem, L"%1 — 点击安全弹出", L"%1 — click to safely remove"},
    {StrId::MenuRefresh, L"刷新设备列表", L"Refresh device list"},
    {StrId::MenuSettings, L"设置…", L"Settings…"},
    {StrId::MenuAbout, L"关于 USB-Switch", L"About USB-Switch"},
    {StrId::MenuExit, L"退出", L"Exit"},

    // Eject flow
    {StrId::ConfirmEjectTitle, L"确认安全弹出", L"Confirm safe removal"},
    {StrId::ConfirmEjectText, L"确定要安全弹出“%1”吗？\n\n请确认该设备上的文件已保存并已关闭。",
     L"Safely remove “%1”?\n\nMake sure files on the device are saved and closed."},
    {StrId::EjectOkTitle, L"已安全弹出", L"Safely removed"},
    {StrId::EjectOkText, L"“%1”已停止，现在可以安全拔出了。",
     L"“%1” has been stopped; it is safe to unplug now."},
    {StrId::EjectFailTitle, L"弹出失败", L"Removal failed"},
    {StrId::EjectFailText, L"无法弹出“%1”。\n\n%2", L"Could not remove “%1”.\n\n%2"},

    // Hot-plug balloons
    {StrId::DeviceInserted, L"设备已插入：%1", L"Device connected: %1"},
    {StrId::DeviceRemoved, L"设备已拔出：%1", L"Device disconnected: %1"},

    // PnP veto reasons
    {StrId::VetoUnknown, L"系统拒绝了弹出请求，但未说明原因。可稍后重试，或先关闭可能使用该设备的程序。",
     L"The system refused the removal without giving a reason. Try again shortly, or close programs that may use the device."},
    {StrId::VetoLegacyDevice, L"有旧版驱动或程序与该设备关联，导致系统无法停止它。",
     L"A legacy driver or program is tied to this device, so it cannot be stopped."},
    {StrId::VetoPendingClose, L"系统正在关闭该设备的句柄，请稍等几秒后重试。",
     L"The system is closing handles to the device; try again in a few seconds."},
    {StrId::VetoWindowsApp, L"有应用程序正在占用该设备（常见于打开了其中文件的资源管理器、Office、播放器等）。请先关闭相关程序。",
     L"An application is using the device (commonly Explorer, Office or a media player with a file open). Close it first."},
    {StrId::VetoWindowsService, L"有系统服务正在占用该设备。可尝试在服务管理中停止相关服务，或以管理员身份运行本工具。",
     L"A system service is using the device. Stop the related service, or run this tool as administrator."},
    {StrId::VetoOutstandingOpen, L"该设备上仍有打开的文件或句柄。请关闭所有访问该磁盘的程序（包括资源管理器窗口）。",
     L"There are still open files or handles on the device. Close every program accessing the disk, including Explorer windows."},
    {StrId::VetoDevice, L"该设备自身或其子设备阻止了移除。",
     L"The device or one of its child devices refused the removal."},
    {StrId::VetoDriver, L"有驱动程序正在占用该设备。",
     L"A driver is using the device."},
    {StrId::VetoIllegalDeviceRequest, L"设备拒绝了此次移除请求，可能它本身不支持热插拔。可尝试先卸载盘符再拔出。",
     L"The device rejected the removal request; it may not support hot-unplugging. Try unmounting the drive letter first."},
    {StrId::VetoInsufficientPower, L"系统电源策略阻止了移除，常见于通过 USB 集线器或扩展坞连接的设备。请改用设备自身的接口。",
     L"A power policy blocked the removal, common for devices behind a USB hub or dock. Try the device's own port instead."},
    {StrId::VetoNonDisableable, L"该设备被系统标记为不可禁用，因此无法安全移除。",
     L"The device is marked as non-disableable, so it cannot be safely removed."},
    {StrId::VetoLegacyDriver, L"有旧版驱动程序正在占用该设备。可在设备管理器中卸载对应设备后重试。",
     L"A legacy driver is using the device. Uninstall the device in Device Manager and try again."},
    {StrId::VetoInsufficientRights, L"权限不足，系统拒绝了移除请求。请以管理员身份运行 USB-Switch 后重试。",
     L"Insufficient rights: the system refused the removal. Run USB-Switch as administrator and try again."},
    {StrId::VetoAlreadyRemoved, L"该设备已经被移除或已断开连接，无需再次弹出。",
     L"The device has already been removed or disconnected; no further action is needed."},
    // The text PnP supplies alongside a veto is an application, service or
    // driver name only for these three types.
    {StrId::VetoAppSuffix, L"（占用：%1）", L"(used by: %1)"},
    {StrId::VetoDriverSuffix, L"（驱动：%1）", L"(driver: %1)"},
    // For the other types the same buffer holds a device instance path.
    {StrId::VetoDevicePathSuffix, L"（相关设备：%1）", L"(related device: %1)"},

    // Settings dialog
    {StrId::SettingsTitle, L"USB-Switch 设置", L"USB-Switch Settings"},
    {StrId::SettingsStartup, L"登录 Windows 时自动运行", L"Run when I sign in to Windows"},
    {StrId::SettingsNotify, L"显示设备插入 / 拔出通知", L"Show device connect / disconnect notifications"},
    {StrId::SettingsConfirm, L"弹出前显示确认对话框", L"Ask for confirmation before removing"},
    {StrId::SettingsLanguage, L"语言：", L"Language:"},
    {StrId::SettingsLanguageAuto, L"跟随系统", L"Follow system"},
    {StrId::SettingsLanguageZh, L"中文", L"中文"},
    {StrId::SettingsLanguageEn, L"English", L"English"},
    {StrId::SettingsNote, L"语言更改即时生效；本对话框的文字将在下次打开时更新。",
     L"Language changes apply immediately; this dialog updates next time you open it."},
    {StrId::SettingsSaveFailed, L"无法保存设置。请检查用户配置目录是否可写，然后重试。",
     L"Settings could not be saved. Check that your user configuration directory is writable and try again."},
    {StrId::SettingsStartupFailed, L"无法更新 Windows 开机启动项。请检查注册表权限，或确认同名启动项是否指向其他程序。",
     L"Could not update the Windows startup entry. Check registry permissions and whether another program owns this entry."},
    // The .rc templates hardcode English for these; INITDIALOG overwrites them.
    {StrId::DialogOk, L"确定", L"OK"},
    {StrId::DialogCancel, L"取消", L"Cancel"},

    // About dialog
    {StrId::AboutTitle, L"关于 USB-Switch", L"About USB-Switch"},
    {StrId::AboutAppName, L"USB-Switch " QUICKEJECT_VERSION_STRING_W,
     L"USB-Switch " QUICKEJECT_VERSION_STRING_W},
    {StrId::AboutDescription,
     L"一个轻量的 USB 安全弹出工具：快速枚举可移动设备、一键安全弹出、托盘常驻。\n\n"
     L"本软件按“原样”提供，不对数据丢失承担责任。拔出设备前请确认数据已保存。",
     L"A lightweight USB safe-removal tool: fast device enumeration, one-click safe removal, tray resident.\n\n"
     L"Provided “as is”; not liable for data loss. Make sure your data is saved before unplugging."},
    {StrId::AboutWebsite, L"项目主页：%1", L"Project home: %1"},

    // Command line
    {StrId::CliUsage,
     L"用法: usb-switch [选项]\n"
     L"  --list            列出可移动设备后退出\n"
     L"  --list --json     以 JSON 数组输出设备列表\n"
     L"  --eject <盘符>    安全弹出指定设备(如 --eject E:)\n"
     L"  --version         显示版本\n"
     L"  --help            显示本帮助\n"
     L"无参数时启动托盘程序。",
     L"Usage: usb-switch [options]\n"
     L"  --list            list removable devices and exit\n"
     L"  --list --json     output the device list as a JSON array\n"
     L"  --eject <drive>   safely remove a device (e.g. --eject E:)\n"
     L"  --version         print version\n"
     L"  --help            show this help\n"
     L"Without options the tray application starts."},
    {StrId::CliListTitle, L"USB-Switch " QUICKEJECT_VERSION_STRING_W L" — 可移动设备",
     L"USB-Switch " QUICKEJECT_VERSION_STRING_W L" — removable devices"},
    {StrId::CliNoDevices, L"未发现可移动设备。", L"No removable devices found."},
    {StrId::CliEjectOk, L"已安全弹出：%1", L"Safely removed: %1"},
    {StrId::CliEjectFailed, L"弹出失败：%1（%2）", L"Removal failed: %1 (%2)"},
    {StrId::CliUnknownDevice, L"找不到匹配的设备：%1", L"No device matches: %1"},
    {StrId::CliVersion, L"usb-switch " QUICKEJECT_VERSION_STRING_W L" (Windows)",
     L"usb-switch " QUICKEJECT_VERSION_STRING_W L" (Windows)"},

    // Errors
    {StrId::ErrorGeneric, L"发生错误：%1", L"An error occurred: %1"},
    {StrId::ErrorEnumFailed, L"无法枚举系统设备，可能是系统组件损坏或权限不足。",
     L"Could not enumerate system devices; system components may be damaged or permissions are insufficient."},
    {StrId::ErrorWindowCreation, L"无法创建程序窗口，USB-Switch 无法启动。",
     L"Could not create the application window; USB-Switch cannot start."},
    {StrId::ErrorTrayCreation, L"无法创建系统托盘图标，USB-Switch 无法启动。请确认 Windows 资源管理器正在运行。",
     L"Could not create the system tray icon; USB-Switch cannot start. Check that Windows Explorer is running."},
    {StrId::ErrorConsoleAttach, L"无法连接到父进程的控制台，命令行输出不可用。",
     L"Could not attach to the parent console; command line output is unavailable."},
};

static_assert(static_cast<size_t>(StrId::kCount) == std::size(kStrings),
              "string table out of sync with StrId enum");

}  // namespace

// Implementation detail of I18n.cpp; declared in I18n.h.
const wchar_t* Lookup(StrId id, bool chinese) {
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(StrId::kCount)) return L"";
    const Entry& entry = kStrings[index];
    return chinese ? entry.zh : entry.en;
}

}  // namespace qe::i18n

#pragma once
// Localized string identifiers.
//
// The table itself lives in Strings.cpp: one entry per id with the Chinese and
// English text side by side, so a translation pass never has to jump between
// files, and adding a language means adding one column.

namespace qe::i18n {

enum class StrId {
    // App identity / tray
    AppTooltip,
    TrayHintRunning,

    // Tray menu
    MenuDevices,
    MenuScanUnavailable,
    MenuNoDevices,
    MenuEjectItem,
    MenuRefresh,
    MenuSettings,
    MenuLogs,
    LogOpenFailed,
    MenuAliases,
    MenuMedia,
    MediaConfirm,
    MediaChanged,
    MediaLockFailed,
    MediaOk,
    AliasHint,
    HotkeyLabel,
    HotkeyOff,
    HotkeyConflict,
    MenuAbout,
    MenuExit,

    // Eject flow
    ConfirmEjectTitle,
    ConfirmEjectText,
    EjectOkTitle,
    EjectOkText,
    EjectIneligible,
    EventProcesses,
    NoEventProcess,
    EjectFailTitle,
    EjectFailText,

    // Hot-plug balloons
    DeviceInserted,
    DeviceRemoved,

    // PnP veto reasons
    VetoUnknown,
    VetoLegacyDevice,
    VetoPendingClose,
    VetoWindowsApp,
    VetoWindowsService,
    VetoOutstandingOpen,
    VetoDevice,
    VetoDriver,
    VetoIllegalDeviceRequest,
    VetoInsufficientPower,
    VetoNonDisableable,
    VetoLegacyDriver,
    VetoInsufficientRights,
    VetoAlreadyRemoved,
    // Suffixes. The text PnP returns alongside a veto is only an application,
    // service or driver name for some types; for others it is a device
    // instance path, and for two types there is no text at all.
    VetoAppSuffix,
    VetoDriverSuffix,
    VetoDevicePathSuffix,

    // Settings dialog
    SettingsTitle,
    SettingsStartup,
    SettingsNotify,
    SettingsConfirm,
    SettingsLanguage,
    SettingsLanguageAuto,
    SettingsLanguageZh,
    SettingsLanguageEn,
    SettingsNote,
    SettingsSaveFailed,
    SettingsStartupFailed,

    // Dialog buttons (set at runtime; the .rc templates keep English defaults)
    DialogOk,
    DialogCancel,

    // About dialog
    AboutTitle,
    AboutAppName,
    AboutDescription,
    AboutWebsite,

    // Command line
    CliUsage,
    CliListTitle,
    CliNoDevices,
    CliEjectOk,
    CliEjectFailed,
    CliUnknownDevice,
    CliVersion,

    // Errors
    ErrorGeneric,
    ErrorEnumFailed,
    ErrorWindowCreation,
    ErrorTrayCreation,
    ErrorConsoleAttach,

    kCount
};

}  // namespace qe::i18n

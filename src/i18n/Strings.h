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
    MenuNoDevices,
    MenuEjectItem,
    MenuRefresh,
    MenuSettings,
    MenuAbout,
    MenuExit,

    // Eject flow
    ConfirmEjectTitle,
    ConfirmEjectText,
    EjectOkTitle,
    EjectOkText,
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
    VetoParentDevice,
    VetoAppSuffix,

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
    ErrorConsoleAttach,

    kCount
};

}  // namespace qe::i18n

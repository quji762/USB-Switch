#pragma once
// Resource identifiers shared by the .rc files and the C++ sources.
// Keep numeric values stable: they are part of the binary's public surface
// (used by the installer and by future localization builds).
//
// The application icon is *not* listed here: it is a named resource
// ("USBSwitchIcon") declared in version.rc, so no id can drift.

#define IDD_ABOUT                       201
#define IDD_ALIAS                       203
#define IDD_OPEN_FILES                  204
#define IDC_FILES_TEXT                  601
#define IDC_ALIAS_NAME                  501
#define IDC_ALIAS_HINT                  502
#define IDC_SET_KEY_LABEL               407
#define IDC_SET_KEY                     408
#define IDC_SET_BEHAVIOR                409
#define IDC_SET_INPUT                   410
#define IDC_DIALOG_HEADER               701
#define IDD_SETTINGS                    202

// About dialog
#define IDC_ABOUT_APPNAME               301
#define IDC_ABOUT_DESC                  302
#define IDC_ABOUT_WEBSITE               303

// Settings dialog
#define IDC_SET_STARTUP                 401
#define IDC_SET_NOTIFY                  402
#define IDC_SET_CONFIRM                 403
#define IDC_SET_LANG_LABEL              404
#define IDC_SET_LANG                    405
#define IDC_SET_NOTE                    406

// winres.h on some toolchains does not define IDC_STATIC.
#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

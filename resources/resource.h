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
#define IDD_DEVICES                     205
#define IDD_SMART                       206
#define IDC_FILES_TEXT                  601
#define IDC_DEVICE_LIST                 801
#define IDC_DEVICE_EMPTY                802
#define IDC_DEVICE_EJECT                803
#define IDC_DEVICE_MEDIA                804
#define IDC_DEVICE_REFRESH              805
#define IDC_DEVICE_SMART                806
#define IDC_SMART_TEMP                  901
#define IDC_SMART_HOURS                 902
#define IDC_SMART_CYCLES                903
#define IDC_SMART_REALLOC               904
#define IDC_SMART_PENDING               905
#define IDC_SMART_NOTE                  906
#define IDC_ALIAS_NAME                  501
#define IDC_ALIAS_HINT                  502
#define IDC_SET_KEY_LABEL               407
#define IDC_SET_KEY                     408
#define IDC_SET_BEHAVIOR                409
#define IDC_SET_INPUT                   410
#define IDC_SET_DIVIDER                 411
#define IDC_SET_THEME                   413
#define IDC_SET_THEME_LABEL             414
#define IDC_SET_FOOTER                  412
#define IDC_DIALOG_HEADER               701
#define IDD_SETTINGS                    202

// About dialog
#define IDC_ABOUT_DESC                  302
#define IDC_ABOUT_WEBSITE               303

// Settings dialog
#define IDC_SET_STARTUP                 401
#define IDC_SET_NOTIFY                  402
#define IDC_SET_CONFIRM                 403
#define IDC_SET_LANG_LABEL              404
#define IDC_SET_LANG                    405

// winres.h on some toolchains does not define IDC_STATIC.
#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

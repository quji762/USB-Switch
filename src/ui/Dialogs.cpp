#include "ui/Dialogs.h"

#include <windows.h>

#include <iterator>

#include "app/AppInfo.h"
#include "app/Logger.h"
#include "app/Settings.h"
#include "i18n/I18n.h"
#include "i18n/Strings.h"
#include "resource.h"
#include "util/Str.h"

namespace qe {
namespace {

constexpr wchar_t kLanguageAuto[] = L"auto";
constexpr wchar_t kLanguageZh[] = L"zh";
constexpr wchar_t kLanguageEn[] = L"en";

INT_PTR CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
        case WM_INITDIALOG: {
            const auto& strings = i18n::I18n::Instance();
            SetWindowTextW(hwnd, strings.Get(i18n::StrId::AboutTitle));
            SetDlgItemTextW(hwnd, IDOK, strings.Get(i18n::StrId::DialogOk));
            SetDlgItemTextW(hwnd, IDC_ABOUT_APPNAME, strings.Get(i18n::StrId::AboutAppName));
            SetDlgItemTextW(hwnd, IDC_ABOUT_DESC, strings.Get(i18n::StrId::AboutDescription));
            SetDlgItemTextW(hwnd, IDC_ABOUT_WEBSITE,
                            strings.Format(i18n::StrId::AboutWebsite, QUICKEJECT_WEBSITE_W).c_str());
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, LOWORD(wParam));
                return TRUE;
            }
            break;
        default:
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
        case WM_INITDIALOG: {
            const auto& strings = i18n::I18n::Instance();
            SetWindowTextW(hwnd, strings.Get(i18n::StrId::SettingsTitle));
            SetDlgItemTextW(hwnd, IDOK, strings.Get(i18n::StrId::DialogOk));
            SetDlgItemTextW(hwnd, IDCANCEL, strings.Get(i18n::StrId::DialogCancel));
            SetDlgItemTextW(hwnd, IDC_SET_STARTUP, strings.Get(i18n::StrId::SettingsStartup));
            SetDlgItemTextW(hwnd, IDC_SET_NOTIFY, strings.Get(i18n::StrId::SettingsNotify));
            SetDlgItemTextW(hwnd, IDC_SET_CONFIRM, strings.Get(i18n::StrId::SettingsConfirm));
            SetDlgItemTextW(hwnd, IDC_SET_LANG_LABEL, strings.Get(i18n::StrId::SettingsLanguage));
            SetDlgItemTextW(hwnd, IDC_SET_NOTE, strings.Get(i18n::StrId::SettingsNote));

            const AppSettings& settings = Settings::Instance().Data();
            CheckDlgButton(hwnd, IDC_SET_STARTUP, settings.runAtStartup ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_SET_NOTIFY, settings.showNotifications ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_SET_CONFIRM, settings.confirmBeforeEject ? BST_CHECKED : BST_UNCHECKED);

            HWND combo = GetDlgItem(hwnd, IDC_SET_LANG);
            const wchar_t* items[] = {strings.Get(i18n::StrId::SettingsLanguageAuto),
                                      strings.Get(i18n::StrId::SettingsLanguageZh),
                                      strings.Get(i18n::StrId::SettingsLanguageEn)};
            for (const wchar_t* item : items) {
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
            }
            int selected = 0;
            if (str::EqualsI(settings.language, kLanguageZh)) {
                selected = 1;
            } else if (str::EqualsI(settings.language, kLanguageEn)) {
                selected = 2;
            }
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
            return TRUE;
        }
        case WM_COMMAND: {
            const UINT commandId = LOWORD(wParam);
            if (commandId == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            if (commandId != IDOK) break;

            AppSettings& settings = Settings::Instance().Data();
            settings.runAtStartup = IsDlgButtonChecked(hwnd, IDC_SET_STARTUP) == BST_CHECKED;
            settings.showNotifications = IsDlgButtonChecked(hwnd, IDC_SET_NOTIFY) == BST_CHECKED;
            settings.confirmBeforeEject = IsDlgButtonChecked(hwnd, IDC_SET_CONFIRM) == BST_CHECKED;

            HWND combo = GetDlgItem(hwnd, IDC_SET_LANG);
            const int selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            const wchar_t* languages[] = {kLanguageAuto, kLanguageZh, kLanguageEn};
            if (selected >= 0 && selected < static_cast<int>(std::size(languages))) {
                settings.language = languages[selected];
            }

            if (!Settings::Instance().Save()) {
                QE_LOG_WARN(L"settings could not be written; changes are kept for this session only");
            }
            i18n::I18n::Instance().SetLanguage(settings.language);
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        default:
            break;
    }
    return FALSE;
}

}  // namespace

INT_PTR Dialogs::ShowAbout(HINSTANCE instance, HWND owner) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutDialogProc, 0);
}

INT_PTR Dialogs::ShowSettings(HINSTANCE instance, HWND owner) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDialogProc, 0);
}

}  // namespace qe

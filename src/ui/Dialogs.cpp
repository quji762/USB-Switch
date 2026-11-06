#include "ui/Dialogs.h"
#include "ui/DialogHeader.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <iterator>

#include "app/AppInfo.h"
#include "app/AutoStart.h"
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
    if (auto result = ThemeDialog(hwnd,message,wParam,lParam)) return result;
    if (PaintDialogHeader(message,lParam)) return TRUE;
    switch (message) {
        case WM_INITDIALOG: {
            const auto& strings = i18n::I18n::Instance();
            SetWindowTextW(hwnd, strings.Get(i18n::StrId::AboutTitle));
            SetDlgItemTextW(hwnd, IDOK, strings.Get(i18n::StrId::DialogOk));
            SetDlgItemTextW(hwnd, IDC_DIALOG_HEADER, strings.Get(i18n::StrId::AboutTitle));
            SetDlgItemTextW(hwnd, IDC_ABOUT_DESC, strings.Get(i18n::StrId::AboutDescription));
            SetDlgItemTextW(hwnd, IDC_ABOUT_WEBSITE,
                            strings.Format(i18n::StrId::AboutWebsite, L"<a>" QUICKEJECT_WEBSITE_W L"</a>").c_str());
            return TRUE;
        }
        case WM_NOTIFY: {
            const auto* event = reinterpret_cast<const NMHDR*>(lParam);
            if (event && event->idFrom == IDC_ABOUT_WEBSITE && (event->code == NM_CLICK || event->code == NM_RETURN)) {
                ShellExecuteW(hwnd,L"open",QUICKEJECT_WEBSITE_W,nullptr,nullptr,SW_SHOWNORMAL);
                return TRUE;
            }
            break;
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
    if (auto result = ThemeDialog(hwnd,message,wParam,lParam)) return result;
    if (PaintDialogHeader(message,lParam)) return TRUE;
    switch (message) {
        case WM_INITDIALOG: {
            const auto& strings = i18n::I18n::Instance();
            SetWindowTextW(hwnd, strings.Get(i18n::StrId::SettingsTitle));
            SetDlgItemTextW(hwnd, IDOK, strings.Get(i18n::StrId::DialogSave));
            SetDlgItemTextW(hwnd, IDCANCEL, strings.Get(i18n::StrId::DialogCancel));
            SetDlgItemTextW(hwnd, IDC_SET_STARTUP, strings.Get(i18n::StrId::SettingsStartup));
            SetDlgItemTextW(hwnd, IDC_SET_NOTIFY, strings.Get(i18n::StrId::SettingsNotify));
            SetDlgItemTextW(hwnd, IDC_SET_CONFIRM, strings.Get(i18n::StrId::SettingsConfirm));
            SetDlgItemTextW(hwnd, IDC_SET_LANG_LABEL, strings.Get(i18n::StrId::SettingsLanguage));
            SetDlgItemTextW(hwnd, IDC_DIALOG_HEADER, strings.Get(i18n::StrId::SettingsHeading));
            SetDlgItemTextW(hwnd, IDC_SET_BEHAVIOR, strings.Get(i18n::StrId::SettingsBehavior));
            SetDlgItemTextW(hwnd, IDC_SET_INPUT, strings.Get(i18n::StrId::SettingsInput));

            const AppSettings& settings = Settings::Instance().Data();
            SetDlgItemTextW(hwnd, IDC_SET_THEME_LABEL, strings.Get(i18n::StrId::ThemeLabel));
            for (auto id : {i18n::StrId::SettingsLanguageAuto, i18n::StrId::ThemeLight, i18n::StrId::ThemeDark})
                SendDlgItemMessageW(hwnd, IDC_SET_THEME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(strings.Get(id)));
            SendDlgItemMessageW(hwnd, IDC_SET_THEME, CB_SETCURSEL,
                settings.appearance == L"dark" ? 2 : settings.appearance == L"light" ? 1 : 0, 0);
            const auto entry = AutoStart::Inspect(QUICKEJECT_APP_NAME_W, AutoStart::CommandLine());
            const bool owned = entry == AutoStart::Entry::Owned;
            SetWindowLongPtrW(hwnd, DWLP_USER, owned);
            CheckDlgButton(hwnd, IDC_SET_STARTUP, owned ? BST_CHECKED : BST_UNCHECKED);
            if (entry == AutoStart::Entry::Error)
                EnableWindow(GetDlgItem(hwnd, IDC_SET_STARTUP), FALSE);
            CheckDlgButton(hwnd, IDC_SET_NOTIFY, settings.showNotifications ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_SET_CONFIRM, settings.confirmBeforeEject ? BST_CHECKED : BST_UNCHECKED);

            SetDlgItemTextW(hwnd, IDC_SET_KEY_LABEL, strings.Get(i18n::StrId::HotkeyLabel));
            HWND keys = GetDlgItem(hwnd, IDC_SET_KEY);
            SendMessageW(keys, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(strings.Get(i18n::StrId::HotkeyOff)));
            for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
                auto label = std::wstring(L"Ctrl + Alt + ") + letter;
                SendMessageW(keys, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendMessageW(keys, CB_SETCURSEL, settings.menuHotkey.empty() ? 0 : settings.menuHotkey[0] - L'A' + 1, 0);
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
            if (commandId == IDC_SET_THEME && HIWORD(wParam) == CBN_SELCHANGE) {
                ApplyDialogTheme(hwnd, true);
                return TRUE;
            }
            if (commandId != IDOK) break;

            AppSettings& settings = Settings::Instance().Data();
            const AppSettings previous = settings;
            const auto mode = SendDlgItemMessageW(hwnd, IDC_SET_THEME, CB_GETCURSEL, 0, 0);
            settings.appearance = mode == 2 ? L"dark" : mode == 1 ? L"light" : L"auto";
            const auto key = SendDlgItemMessageW(hwnd, IDC_SET_KEY, CB_GETCURSEL, 0, 0);
            if (key >= 0 && key <= 26) settings.menuHotkey = key ? std::wstring(1, static_cast<wchar_t>(L'A'+key-1)) : L"";
            settings.runAtStartup = IsDlgButtonChecked(hwnd, IDC_SET_STARTUP) == BST_CHECKED;
            settings.showNotifications = IsDlgButtonChecked(hwnd, IDC_SET_NOTIFY) == BST_CHECKED;
            settings.confirmBeforeEject = IsDlgButtonChecked(hwnd, IDC_SET_CONFIRM) == BST_CHECKED;

            HWND combo = GetDlgItem(hwnd, IDC_SET_LANG);
            const int selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            const wchar_t* languages[] = {kLanguageAuto, kLanguageZh, kLanguageEn};
            if (selected >= 0 && selected < static_cast<int>(std::size(languages))) {
                settings.language = languages[selected];
            }

            const auto path = AutoStart::CommandLine();
            const bool originallyOwned = GetWindowLongPtrW(hwnd, DWLP_USER) != 0;
            const bool changed = settings.runAtStartup != originallyOwned;
            auto apply = [&](bool enabled) {
                return enabled ? AutoStart::SetEnabled(QUICKEJECT_APP_NAME_W, path)
                               : AutoStart::ClearIfMatches(QUICKEJECT_APP_NAME_W, path);
            };
            const auto& strings = i18n::I18n::Instance();
            if (changed && !apply(settings.runAtStartup)) {
                settings = previous;
                MessageBoxW(hwnd, strings.Get(i18n::StrId::SettingsStartupFailed),
                            strings.Get(i18n::StrId::SettingsTitle), MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            if (!changed) {
                const auto current = AutoStart::Inspect(QUICKEJECT_APP_NAME_W, path);
                settings.runAtStartup = current == AutoStart::Entry::Error ? previous.runAtStartup
                                                                           : current == AutoStart::Entry::Owned;
            }
            if (!Settings::Instance().Save()) {
                if (changed && !apply(originallyOwned)) {
                    const auto actual = AutoStart::Inspect(QUICKEJECT_APP_NAME_W, path);
                    const bool owned = actual == AutoStart::Entry::Owned;
                    SetWindowLongPtrW(hwnd, DWLP_USER, owned);
                    CheckDlgButton(hwnd, IDC_SET_STARTUP, owned ? BST_CHECKED : BST_UNCHECKED);
                    MessageBoxW(hwnd, strings.Get(i18n::StrId::SettingsStartupFailed),
                                strings.Get(i18n::StrId::SettingsTitle), MB_OK | MB_ICONWARNING);
                }
                settings = previous;
                MessageBoxW(hwnd, strings.Get(i18n::StrId::SettingsSaveFailed),
                            strings.Get(i18n::StrId::SettingsTitle), MB_OK | MB_ICONERROR);
                return TRUE;
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

struct AliasData { std::wstring id, display; };

INT_PTR CALLBACK AliasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (auto result = ThemeDialog(hwnd,msg,wp,lp)) return result;
    if (PaintDialogHeader(msg,lp)) return TRUE;
    auto& settings = Settings::Instance();
    const auto& strings = i18n::I18n::Instance();
    if (msg == WM_INITDIALOG) {
        auto* data = reinterpret_cast<AliasData*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, lp);
        SetWindowTextW(hwnd, strings.Get(i18n::StrId::MenuAliases));
        SetDlgItemTextW(hwnd, IDC_DIALOG_HEADER, strings.Get(i18n::StrId::MenuAliases));
        SetDlgItemTextW(hwnd, IDC_ALIAS_HINT, strings.Format(i18n::StrId::AliasHint, data->display).c_str());
        SetDlgItemTextW(hwnd, IDOK, strings.Get(i18n::StrId::DialogSave));
        SetDlgItemTextW(hwnd, IDCANCEL, strings.Get(i18n::StrId::DialogCancel));
        SendDlgItemMessageW(hwnd, IDC_ALIAS_NAME, EM_SETLIMITTEXT, 64, 0);
        const auto& names = settings.Data().aliases;
        auto it = names.find(data->id);
        if (it != names.end()) SetDlgItemTextW(hwnd, IDC_ALIAS_NAME, it->second.c_str());
        return TRUE;
    }
    if (msg != WM_COMMAND) return FALSE;
    if (LOWORD(wp) == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
    if (LOWORD(wp) != IDOK) return FALSE;
    auto* data = reinterpret_cast<AliasData*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    wchar_t text[65]{};
    GetDlgItemTextW(hwnd, IDC_ALIAS_NAME, text, 65);
    auto alias = str::Trim(text);
    auto& names = settings.Data().aliases;
    const auto previous = names;
    if (alias.empty()) names.erase(data->id); else names[data->id] = alias;
    if (!settings.Save()) {
        names = previous;
        MessageBoxW(hwnd, strings.Get(i18n::StrId::SettingsSaveFailed), strings.Get(i18n::StrId::MenuAliases), MB_ICONERROR);
    } else EndDialog(hwnd, IDOK);
    return TRUE;
}
}  // namespace

void Dialogs::EditAlias(HINSTANCE instance, HWND owner, const std::wstring& id, const std::wstring& display) {
    AliasData data{id, display};
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ALIAS), owner, AliasProc, reinterpret_cast<LPARAM>(&data));
}

INT_PTR Dialogs::ShowAbout(HINSTANCE instance, HWND owner) {
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_LINK_CLASS};
    InitCommonControlsEx(&controls);
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutDialogProc, 0);
}

INT_PTR Dialogs::ShowSettings(HINSTANCE instance, HWND owner) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsDialogProc, 0);
}

}  // namespace qe

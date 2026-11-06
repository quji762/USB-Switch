#pragma once
// Modal dialogs (about, settings). Both templates live in resources/dialogs.rc
// and every visible string is set at runtime so the binary stays language
// independent.

#include <windows.h>

namespace qe {

class Dialogs {
public:
    // Returns the value passed to EndDialog (IDOK / IDCANCEL), or -1 on error.
    static INT_PTR ShowAbout(HINSTANCE instance, HWND owner);
    static INT_PTR ShowSettings(HINSTANCE instance, HWND owner);
};

}  // namespace qe

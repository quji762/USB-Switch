#pragma once
// Update checking - extension point.
//
// Shipping an updater is a product decision, not a code question: it needs a
// distribution channel, a trust model (signatures), and a rollback story. The
// interface below keeps that decision open without blocking anything else.
//
// A production implementation would:
//   1. GET https://<host>/usb-switch/latest.json over WinHTTP (TLS pinned cert
//      store, no custom validation).
//   2. Compare versions, then download a signed installer or a package.
//   3. Verify an Authenticode signature (WinVerifyTrust) before running it.
//   4. Hand off to the installer and exit.
// WinSparkle (https://winsparkle.org) implements exactly this and can be
// dropped in behind this interface.

#include <functional>
#include <string>

namespace qe {

struct UpdateInfo {
    bool available = false;
    std::wstring version;   // e.g. L"1.2.0"
    std::wstring url;       // download page or installer URL
    std::wstring notes;     // short release notes (may be empty)
};

class Updater {
public:
    virtual ~Updater() = default;

    using ResultCallback = std::function<void(const UpdateInfo&)>;

    // Non-blocking; the callback must be invoked exactly once (on any thread).
    // The default implementation reports "no update available".
    virtual void CheckAsync(ResultCallback onResult) const {
        UpdateInfo info;
        if (onResult) onResult(info);
    }
};

// Single updater for the process. Swap the implementation in Updater.cpp.
Updater& GetUpdater();

}  // namespace qe

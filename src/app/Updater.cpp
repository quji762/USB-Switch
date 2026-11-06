#include "app/Updater.h"

#include "app/Logger.h"

namespace qe {
namespace {

// No-op updater: the app ships without self-update until a distribution
// channel exists. Replace with a WinHTTP + WinVerifyTrust implementation (see
// Updater.h for the recipe) or integrate WinSparkle behind this interface.
class NullUpdater : public Updater {
public:
    void CheckAsync(ResultCallback onResult) const override {
        QE_LOG_DEBUG(L"update check skipped: no distribution channel configured");
        UpdateInfo info;
        if (onResult) onResult(info);
    }
};

NullUpdater kUpdater;

}  // namespace

Updater& GetUpdater() {
    return kUpdater;
}

}  // namespace qe

#pragma once

#include <taskbar_audio_spectrum/platform.h>

namespace tas {

constexpr PCWSTR kExplorerAdvancedRegistryPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr PCWSTR kSearchSettingsRegistryPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Search";

class RegistryChangeWatcher {
public:
    RegistryChangeWatcher() = default;
    RegistryChangeWatcher(const RegistryChangeWatcher&) = delete;
    RegistryChangeWatcher& operator=(const RegistryChangeWatcher&) = delete;
    ~RegistryChangeWatcher() { Stop(); }

    bool Start(PCWSTR subKey) {
        Stop();
        changedEvent_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!changedEvent_) return false;
        const LSTATUS result = RegOpenKeyExW(
            HKEY_CURRENT_USER, subKey, 0, KEY_NOTIFY, &key_);
        if (result == ERROR_SUCCESS && Arm()) return true;
        Stop();
        return false;
    }

    bool Arm() const {
        return key_ && changedEvent_ &&
               RegNotifyChangeKeyValue(
                   key_, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
                   changedEvent_.get(), TRUE) == ERROR_SUCCESS;
    }

    void Stop() {
        if (key_) RegCloseKey(key_);
        key_ = nullptr;
        changedEvent_.reset();
    }

    HANDLE changedEvent() const { return changedEvent_.get(); }

private:
    HKEY key_ = nullptr;
    ScopedHandle changedEvent_;
};

}  // namespace tas

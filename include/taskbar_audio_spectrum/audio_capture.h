#pragma once

#include "platform.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

namespace tas {

class SignalWindowTracker {
public:
    explicit SignalWindowTracker(size_t windowFrames)
        : windowFrames_(std::max<size_t>(1, windowFrames)),
          framesSinceSignal_(windowFrames_) {}

    bool ContainsSignal() const {
        return framesSinceSignal_ < windowFrames_;
    }

    void PushFrame(bool containsSignal) {
        if (containsSignal) {
            framesSinceSignal_ = 0;
        } else if (framesSinceSignal_ < windowFrames_) {
            ++framesSinceSignal_;
        }
    }

    void ResetToSilence() { framesSinceSignal_ = windowFrames_; }

private:
    size_t windowFrames_;
    size_t framesSinceSignal_;
};

class EndpointNotificationClient final : public IMMNotificationClient {
public:
    explicit EndpointNotificationClient(HANDLE changedEvent);
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override;
private:
    std::atomic<ULONG> references_{1};
    HANDLE changedEvent_;
};
DWORD WINAPI AudioThreadProc(void*);
}  // namespace tas

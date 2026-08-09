#pragma once

#include "platform.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

namespace tas {
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

#include <taskbar_audio_spectrum/audio_capture.h>
#include <taskbar_audio_spectrum/host.h>
#include <taskbar_audio_spectrum/spectrum_analysis.h>

#include "runtime_context.h"

#include <new>

namespace tas {

EndpointNotificationClient::EndpointNotificationClient(HANDLE changedEvent)
    : changedEvent_(changedEvent) {}

ULONG STDMETHODCALLTYPE EndpointNotificationClient::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE EndpointNotificationClient::Release() {
    const ULONG remaining =
        references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!remaining) delete this;
    return remaining;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationClient::QueryInterface(
    REFIID iid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == __uuidof(IMMNotificationClient)) {
        *object = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationClient::OnDefaultDeviceChanged(
    EDataFlow flow, ERole role, LPCWSTR) {
    if (flow == eRender && role == eConsole) SetEvent(changedEvent_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationClient::OnDeviceAdded(LPCWSTR) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationClient::OnDeviceRemoved(LPCWSTR) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationClient::OnDeviceStateChanged(
    LPCWSTR, DWORD) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE EndpointNotificationClient::OnPropertyValueChanged(
    LPCWSTR, const PROPERTYKEY) {
    return S_OK;
}

DWORD WINAPI AudioThreadProc(void* parameter) {
    auto& context = *static_cast<ApplicationContext*>(parameter);
    const Settings& settings = context.settings;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(hr);
    if (FAILED(hr)) {
        Log(L"Audio COM initialization failed: 0x%08X", hr);
        return 0;
    }

    while (WaitForSingleObject(context.stopEvent.get(), 0) != WAIT_OBJECT_0) {
        while (!context.visualizerActive.load(std::memory_order_acquire)) {
            HANDLE inactiveWaits[] = {context.stopEvent.get(),
                                      context.activityChangedEvent.get()};
            const DWORD waitResult = WaitForMultipleObjects(
                ARRAYSIZE(inactiveWaits), inactiveWaits, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0) goto finished;
            if (waitResult != WAIT_OBJECT_0 + 1) {
                Log(L"Audio activity wait failed: %u", GetLastError());
                goto finished;
            }
        }

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* audioClient = nullptr;
        IAudioCaptureClient* captureClient = nullptr;
        WAVEFORMATEX* format = nullptr;
        HANDLE audioEvent = nullptr;
        HANDLE deviceChangedEvent = nullptr;
        EndpointNotificationClient* notification = nullptr;
        bool notificationRegistered = false;
        bool audioStarted = false;
        AnalysisPlan plan{};

        hr = S_OK;
        do {
            deviceChangedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!deviceChangedEvent || !audioEvent) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }

            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
            if (FAILED(hr)) break;

            notification = new (std::nothrow)
                EndpointNotificationClient(deviceChangedEvent);
            if (notification) {
                const HRESULT notificationResult =
                    enumerator->RegisterEndpointNotificationCallback(notification);
                if (SUCCEEDED(notificationResult)) {
                    notificationRegistered = true;
                } else {
                    Log(L"Audio endpoint notification unavailable: 0x%08X",
                        notificationResult);
                }
            }

            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
            if (FAILED(hr)) break;
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&audioClient));
            if (FAILED(hr)) break;
            hr = audioClient->GetMixFormat(&format);
            if (FAILED(hr)) break;
            hr = audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0, 0, format, nullptr);
            if (FAILED(hr)) break;
            hr = audioClient->SetEventHandle(audioEvent);
            if (FAILED(hr)) break;
            hr = audioClient->GetService(
                __uuidof(IAudioCaptureClient),
                reinterpret_cast<void**>(&captureClient));
            if (FAILED(hr)) break;
            hr = audioClient->Start();
            if (FAILED(hr)) break;
            audioStarted = true;

            plan = MakeAnalysisPlan(format->nSamplesPerSec, settings);
            const size_t fftSamples = static_cast<size_t>(plan.fftSize);
            const size_t hopSamples = static_cast<size_t>(plan.hopSamples);
            BandLevels levels{};
            AnalysisScratch analysisScratch;
            std::vector<BYTE> packetData;
            ClearPublishedBands(context);
            Log(L"WASAPI loopback started: %u Hz, %u channels, %u bit; "
                L"fft=%d overlap=%d%% hop=%d window=%ls",
                format->nSamplesPerSec, format->nChannels,
                format->wBitsPerSample, plan.fftSize,
                settings.fftOverlapPercent, plan.hopSamples,
                WindowFunctionName(settings.windowFunction));

            const int analysisChannels =
                std::max(1, static_cast<int>(format->nChannels));
            std::vector<std::vector<float>> samples(
                analysisChannels, std::vector<float>(fftSamples));
            size_t writeIndex = 0;
            size_t bufferedFrames = 0;
            size_t framesSinceAnalysis = 0;
            const DWORD idleTimeoutMs = std::max<DWORD>(
                100, static_cast<DWORD>(
                    (static_cast<uint64_t>(plan.hopSamples) * 2000 +
                     format->nSamplesPerSec - 1) /
                    format->nSamplesPerSec));
            bool streamIdle = false;
            bool signalSeen = false;
            ULONGLONG lastPacketTick = GetTickCount64();
            const auto enterStreamIdle = [&] {
                if (streamIdle) return;
                for (auto& channel : samples) {
                    std::fill(channel.begin(), channel.end(), 0.0f);
                }
                writeIndex = 0;
                bufferedFrames = fftSamples;
                framesSinceAnalysis = 0;
                ClearBands(&levels);
                ClearPublishedBands(context);
                signalSeen = false;
                streamIdle = true;
                Log(L"Audio stream idle for %u ms; spectrum cleared",
                    idleTimeoutMs);
            };
            HANDLE waits[] = {context.stopEvent.get(),
                              context.activityChangedEvent.get(),
                              deviceChangedEvent, audioEvent};
            while (true) {
                const DWORD waitResult = WaitForMultipleObjects(
                    ARRAYSIZE(waits), waits, FALSE, idleTimeoutMs);
                if (waitResult == WAIT_OBJECT_0) {
                    hr = S_OK;
                    break;
                }
                if (waitResult == WAIT_OBJECT_0 + 1) {
                    if (!context.visualizerActive.load(
                            std::memory_order_acquire)) {
                        hr = S_FALSE;
                        break;
                    }
                    continue;
                }
                if (waitResult == WAIT_OBJECT_0 + 2) {
                    hr = AUDCLNT_E_DEVICE_INVALIDATED;
                    Log(L"Default playback device changed; reconnecting");
                    break;
                }
                if (waitResult == WAIT_TIMEOUT) {
                    enterStreamIdle();
                    continue;
                }
                if (waitResult != WAIT_OBJECT_0 + 3) {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    break;
                }

                UINT32 packetFrames = 0;
                bool receivedFrames = false;
                while (true) {
                    hr = captureClient->GetNextPacketSize(&packetFrames);
                    if (FAILED(hr) || packetFrames == 0) break;
                    BYTE* data = nullptr;
                    DWORD flags = 0;
                    UINT32 frames = 0;
                    hr = captureClient->GetBuffer(
                        &data, &frames, &flags, nullptr, nullptr);
                    if (FAILED(hr)) break;
                    if (frames > 0) receivedFrames = true;

                    const bool silent =
                        (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    if (!silent && frames > 0) {
                        const size_t packetBytes =
                            static_cast<size_t>(frames) * format->nBlockAlign;
                        packetData.resize(packetBytes);
                        memcpy(packetData.data(), data, packetBytes);
                    }
                    const HRESULT releaseResult =
                        captureClient->ReleaseBuffer(frames);
                    if (FAILED(releaseResult)) {
                        hr = releaseResult;
                        break;
                    }

                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                        writeIndex = 0;
                        bufferedFrames = 0;
                        framesSinceAnalysis = 0;
                    }
                    const BYTE* copiedData = silent ? nullptr
                                                    : packetData.data();
                    for (UINT32 frame = 0; frame < frames; ++frame) {
                        for (int channel = 0; channel < analysisChannels;
                             ++channel) {
                            samples[channel][writeIndex] =
                                silent ? 0.0f
                                       : ReadSample(copiedData, frame, channel,
                                                    format);
                        }
                        writeIndex = (writeIndex + 1) % fftSamples;
                        bufferedFrames = std::min(
                            bufferedFrames + 1, fftSamples);
                        ++framesSinceAnalysis;
                        if (bufferedFrames == fftSamples &&
                            framesSinceAnalysis >= hopSamples) {
                            AnalyzeChannels(samples, writeIndex, plan,
                                            &analysisScratch, &levels);
                            PublishBandLevels(context, levels);
                            const float strongest = *std::max_element(
                                levels.begin(), levels.begin() + plan.bars);
                            if (strongest > 0.03f && !signalSeen) {
                                Log(L"Audio signal detected, peak band level %.3f",
                                    strongest);
                                signalSeen = true;
                            }
                            framesSinceAnalysis = 0;
                        }
                    }
                }
                if (FAILED(hr)) break;
                if (receivedFrames) {
                    lastPacketTick = GetTickCount64();
                    if (streamIdle) {
                        streamIdle = false;
                        Log(L"Audio stream resumed");
                    }
                } else if (GetTickCount64() - lastPacketTick >=
                           idleTimeoutMs) {
                    enterStreamIdle();
                }
            }
        } while (false);

        ClearPublishedBands(context);
        if (audioStarted) audioClient->Stop();
        if (notificationRegistered) {
            enumerator->UnregisterEndpointNotificationCallback(notification);
        }
        if (notification) notification->Release();
        if (audioEvent) CloseHandle(audioEvent);
        if (deviceChangedEvent) CloseHandle(deviceChangedEvent);
        if (format) CoTaskMemFree(format);
        SafeRelease(captureClient);
        SafeRelease(audioClient);
        SafeRelease(device);
        SafeRelease(enumerator);

        if (WaitForSingleObject(context.stopEvent.get(), 0) == WAIT_OBJECT_0) {
            break;
        }
        if (!context.visualizerActive.load(std::memory_order_acquire)) continue;
        Log(L"WASAPI session ended (0x%08X); reconnecting", hr);
        HANDLE retryWaits[] = {context.stopEvent.get(),
                               context.activityChangedEvent.get()};
        if (WaitForMultipleObjects(ARRAYSIZE(retryWaits), retryWaits, FALSE,
                                   1000) == WAIT_OBJECT_0) {
            break;
        }
    }

finished:
    ClearPublishedBands(context);
    if (uninitialize) CoUninitialize();
    return 0;
}

}  // namespace tas

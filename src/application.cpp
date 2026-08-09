#include <taskbar_audio_spectrum/application.h>
#include <taskbar_audio_spectrum/audio_capture.h>
#include <taskbar_audio_spectrum/host.h>
#include <taskbar_audio_spectrum/overlay_window.h>
#include <taskbar_audio_spectrum/search_locator.h>
#include <taskbar_audio_spectrum/spectrum_analysis.h>

#include "runtime_context.h"

namespace tas {

void ClearPublishedBands(ApplicationContext& context) {
    for (auto& band : context.bands) {
        band.store(0.0f, std::memory_order_relaxed);
    }
}

void PublishBandLevels(ApplicationContext& context, const BandLevels& levels) {
    for (size_t index = 0; index < levels.size(); ++index) {
        context.bands[index].store(levels[index], std::memory_order_relaxed);
    }
}

namespace {

bool WaitForThread(HANDLE thread, DWORD timeout, PCWSTR name) {
    if (!thread) return true;
    const DWORD result = WaitForSingleObject(thread, timeout);
    if (result == WAIT_OBJECT_0) return true;
    if (result == WAIT_TIMEOUT) {
        Log(L"%ls thread did not stop within %u ms", name, timeout);
    } else {
        Log(L"%ls thread wait failed: %u", name, GetLastError());
    }
    return false;
}

}  // namespace

Runtime& AppRuntime() {
#ifdef TAS_WINDHAWK_HOST
    [[clang::no_destroy]] static Runtime runtime;
#else
    static Runtime runtime;
#endif
    return runtime;
}

Runtime::Runtime() = default;

Runtime::~Runtime() {
    Stop();
}

void SetVisualizerActive(ApplicationContext& context, bool active) {
    if (context.visualizerActive.exchange(active, std::memory_order_acq_rel) !=
            active &&
        context.activityChangedEvent) {
        SetEvent(context.activityChangedEvent.get());
    }
}

bool Runtime::running() const {
    return context_ && static_cast<bool>(context_->stopEvent);
}

bool Runtime::Start(const Settings& settings) {
    if (running()) return false;
    context_ = std::make_unique<ApplicationContext>();
    context_->settings = settings;
    ClearPublishedBands(*context_);
    context_->visualizerActive.store(false, std::memory_order_release);
    context_->locatorShutdownIncomplete.store(false, std::memory_order_release);
    context_->stopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    context_->activityChangedEvent.reset(
        CreateEventW(nullptr, FALSE, FALSE, nullptr));
    context_->layoutChangedEvent.reset(
        CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!context_->stopEvent || !context_->activityChangedEvent ||
        !context_->layoutChangedEvent) {
        context_.reset();
        return false;
    }

    locatorThread_.reset(CreateThread(
        nullptr, 0, SearchLocatorThreadProc, context_.get(), 0, nullptr));
    overlayThread_.reset(CreateThread(
        nullptr, 0, OverlayThreadProc, context_.get(), 0, &overlayThreadId_));
    audioThread_.reset(CreateThread(
        nullptr, 0, AudioThreadProc, context_.get(), 0, nullptr));
    if (!locatorThread_ || !overlayThread_ || !audioThread_) {
        Log(L"Failed to create runtime threads: %u", GetLastError());
        Stop();
        return false;
    }
    Log(L"Runtime started");
    return true;
}

bool Runtime::Stop() {
    if (!running()) return true;
    SetVisualizerActive(*context_, false);
    SetEvent(context_->stopEvent.get());
    if (context_->activityChangedEvent) {
        SetEvent(context_->activityChangedEvent.get());
    }
    if (overlayThreadId_) PostThreadMessageW(overlayThreadId_, WM_QUIT, 0, 0);

    const bool locatorStopped =
        WaitForThread(locatorThread_.get(), 5500, L"Locator");
    const bool overlayStopped =
        WaitForThread(overlayThread_.get(), 3000, L"Overlay");
    const bool audioStopped =
        WaitForThread(audioThread_.get(), 3000, L"Audio");
    if (!locatorStopped ||
        context_->locatorShutdownIncomplete.load(std::memory_order_acquire) ||
        !overlayStopped || !audioStopped) {
        Log(L"Runtime shutdown incomplete; abandoning stale runtime state");
        locatorThread_.reset();
        overlayThread_.reset();
        audioThread_.reset();
        overlayThreadId_ = 0;
        context_.release();
        return false;
    }

    locatorThread_.reset();
    overlayThread_.reset();
    audioThread_.reset();
    overlayThreadId_ = 0;
    ClearPublishedBands(*context_);
    context_.reset();
    Log(L"Runtime stopped cleanly");
    return true;
}

}  // namespace tas

#pragma once

#include <taskbar_audio_spectrum/search_locator.h>
#include <taskbar_audio_spectrum/spectrum_analysis.h>

namespace tas {

struct SearchLayoutState {
    std::mutex mutex;
    SearchLayout latest;
    uint64_t generation = 0;
    RECT templateTaskbarRect{};
    RECT templateSearchRect{};
    UINT templateDpi = 96;
    bool templateValid = false;
};

struct ApplicationContext {
    Settings settings;
    std::array<std::atomic<float>, kMaxBars> bands{};
    std::atomic<bool> publishedSignalActive{false};
    std::atomic<bool> visualizerActive{false};
    std::atomic<bool> locatorShutdownIncomplete{false};
    ScopedHandle stopEvent;
    ScopedHandle activityChangedEvent;
    ScopedHandle bandActivityEvent;
    ScopedHandle layoutChangedEvent;
    SearchLayoutState searchLayout;
};

void SetVisualizerActive(ApplicationContext& context, bool active);
void ClearPublishedBands(ApplicationContext& context);
void PublishBandLevels(ApplicationContext& context, const BandLevels& levels);
bool ReadLatestSearchLayout(ApplicationContext& context, SearchLayout* layout);

}  // namespace tas

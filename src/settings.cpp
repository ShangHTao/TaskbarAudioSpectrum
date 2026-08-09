#include <taskbar_audio_spectrum/settings.h>
#include <taskbar_audio_spectrum/host.h>

#include <cstdlib>

namespace tas {

namespace {

int HexValue(wchar_t character) {
    if (character >= L'0' && character <= L'9') return character - L'0';
    if (character >= L'a' && character <= L'f') return character - L'a' + 10;
    if (character >= L'A' && character <= L'F') return character - L'A' + 10;
    return -1;
}

int ThirdOctaveBandCount(float minimumCenter, float maximumCenter,
                         float referenceFrequency) {
    if (minimumCenter <= 0.0f || maximumCenter < minimumCenter ||
        referenceFrequency <= 0.0f) return 0;
    const int first = static_cast<int>(std::lround(
        3.0 * std::log2(minimumCenter / referenceFrequency)));
    const int last = static_cast<int>(std::lround(
        3.0 * std::log2(maximumCenter / referenceFrequency)));
    return last - first + 1;
}

}  // namespace

bool TokenEquals(PCWSTR text, PCWSTR expected) {
    if (!text || !expected) return false;
    while (*text == L' ' || *text == L'\t') ++text;
    const wchar_t* end = text + wcslen(text);
    while (end > text && (end[-1] == L' ' || end[-1] == L'\t')) --end;
    const size_t length = static_cast<size_t>(end - text);
    return length == wcslen(expected) &&
           _wcsnicmp(text, expected, length) == 0;
}

// TAS_WINDHAWK_EXCLUDE_BEGIN
bool TryParseAutoMode(PCWSTR text, bool* automatic) {
    if (!automatic) return false;
    if (TokenEquals(text, L"1") || TokenEquals(text, L"true") ||
        TokenEquals(text, L"yes") || TokenEquals(text, L"on") ||
        TokenEquals(text, L"auto")) {
        *automatic = true;
        return true;
    }
    if (TokenEquals(text, L"0") || TokenEquals(text, L"false") ||
        TokenEquals(text, L"no") || TokenEquals(text, L"off") ||
        TokenEquals(text, L"manual") || TokenEquals(text, L"userdefine")) {
        *automatic = false;
        return true;
    }
    return false;
}
// TAS_WINDHAWK_EXCLUDE_END

// TAS_WINDHAWK_EXCLUDE_BEGIN
bool TryParseClampedInt(PCWSTR text, int minimum, int maximum, int* value) {
    if (!text || !value || minimum > maximum) return false;
    while (*text == L' ' || *text == L'\t') ++text;
    if (!*text) return false;
    wchar_t* numberEnd = nullptr;
    const __int64 number = _wcstoi64(text, &numberEnd, 10);
    if (numberEnd == text) return false;
    while (*numberEnd == L' ' || *numberEnd == L'\t') ++numberEnd;
    if (*numberEnd) return false;
    *value = static_cast<int>(std::clamp<__int64>(number, minimum, maximum));
    return true;
}
// TAS_WINDHAWK_EXCLUDE_END

bool TryParseClampedFloat(PCWSTR text, float minimum, float maximum,
                          float* value) {
    if (!text || !value || !std::isfinite(minimum) ||
        !std::isfinite(maximum) || minimum > maximum) {
        return false;
    }
    while (*text == L' ' || *text == L'\t') ++text;
    if (!*text) return false;
    wchar_t* numberEnd = nullptr;
    const double number = wcstod(text, &numberEnd);
    if (numberEnd == text || !std::isfinite(number)) return false;
    while (*numberEnd == L' ' || *numberEnd == L'\t') ++numberEnd;
    if (*numberEnd) return false;
    *value = std::clamp(static_cast<float>(number), minimum, maximum);
    return true;
}

bool TryParseFrequencyScale(PCWSTR text, FrequencyScale* scale) {
    if (!scale) return false;
    if (TokenEquals(text, L"thirdOctave") ||
        TokenEquals(text, L"octave") ||
        TokenEquals(text, L"third-octave") ||
        TokenEquals(text, L"third_octave") ||
        TokenEquals(text, L"thirdOctaveNominal") ||
        TokenEquals(text, L"1/3 octave")) {
        *scale = FrequencyScale::ThirdOctaveNominal;
        return true;
    }
    if (TokenEquals(text, L"bark")) {
        *scale = FrequencyScale::Bark;
        return true;
    }
    if (TokenEquals(text, L"log") ||
        TokenEquals(text, L"logarithmic")) {
        *scale = FrequencyScale::Logarithmic;
        return true;
    }
    if (TokenEquals(text, L"mel") || TokenEquals(text, L"htk-mel") ||
        TokenEquals(text, L"htk_mel") || TokenEquals(text, L"htkmel")) {
        *scale = FrequencyScale::HtkMel;
        return true;
    }
    if (TokenEquals(text, L"linear")) {
        *scale = FrequencyScale::Linear;
        return true;
    }
    return false;
}

PCWSTR FrequencyScaleName(FrequencyScale scale) {
    switch (scale) {
        case FrequencyScale::ThirdOctaveNominal:
            return L"thirdOctave";
        case FrequencyScale::Bark:
            return L"bark";
        case FrequencyScale::HtkMel:
            return L"mel";
        case FrequencyScale::Linear:
            return L"linear";
        default:
            return L"log";
    }
}

bool TryParseBandAggregation(PCWSTR text, BandAggregation* aggregation) {
    if (!aggregation) return false;
    if (TokenEquals(text, L"peak") || TokenEquals(text, L"peakDb") ||
        TokenEquals(text, L"max")) {
        *aggregation = BandAggregation::Peak;
        return true;
    }
    if (TokenEquals(text, L"energy") || TokenEquals(text, L"sum")) {
        *aggregation = BandAggregation::Energy;
        return true;
    }
    if (TokenEquals(text, L"mean") || TokenEquals(text, L"average") ||
        TokenEquals(text, L"density")) {
        *aggregation = BandAggregation::Mean;
        return true;
    }
    if (TokenEquals(text, L"slaney") ||
        TokenEquals(text, L"slaneyPower")) {
        *aggregation = BandAggregation::Slaney;
        return true;
    }
    return false;
}

PCWSTR BandAggregationName(BandAggregation aggregation) {
    switch (aggregation) {
        case BandAggregation::Energy:
            return L"energy";
        case BandAggregation::Mean:
            return L"mean";
        case BandAggregation::Slaney:
            return L"slaney";
        default:
            return L"peak";
    }
}

bool TryParseFrequencyWeighting(PCWSTR text, FrequencyWeighting* weighting) {
    if (!weighting) return false;
    if (TokenEquals(text, L"none") || TokenEquals(text, L"off") ||
        TokenEquals(text, L"flat")) {
        *weighting = FrequencyWeighting::None;
        return true;
    }
    if (TokenEquals(text, L"a") || TokenEquals(text, L"aWeighting") ||
        TokenEquals(text, L"a-weighting")) {
        *weighting = FrequencyWeighting::A;
        return true;
    }
    if (TokenEquals(text, L"c") || TokenEquals(text, L"cWeighting") ||
        TokenEquals(text, L"c-weighting")) {
        *weighting = FrequencyWeighting::C;
        return true;
    }
    return false;
}

PCWSTR FrequencyWeightingName(FrequencyWeighting weighting) {
    switch (weighting) {
        case FrequencyWeighting::A:
            return L"A";
        case FrequencyWeighting::C:
            return L"C";
        default:
            return L"none";
    }
}

bool TryParseWindowFunction(PCWSTR text, WindowFunction* function) {
    if (!function) return false;
    if (TokenEquals(text, L"hann") || TokenEquals(text, L"hanning")) {
        *function = WindowFunction::Hann;
        return true;
    }
    if (TokenEquals(text, L"hamming")) {
        *function = WindowFunction::Hamming;
        return true;
    }
    if (TokenEquals(text, L"blackmanHarris") ||
        TokenEquals(text, L"blackman-harris") ||
        TokenEquals(text, L"blackman_harris")) {
        *function = WindowFunction::BlackmanHarris;
        return true;
    }
    return false;
}

PCWSTR WindowFunctionName(WindowFunction function) {
    switch (function) {
        case WindowFunction::Hamming:
            return L"hamming";
        case WindowFunction::BlackmanHarris:
            return L"blackmanHarris";
        default:
            return L"hann";
    }
}

bool IsSupportedFftSize(int fftSize) {
    return fftSize == 4096 || fftSize == 8192 || fftSize == 16384;
}

int CalculateFftHopSamples(int fftSize, int overlapPercent) {
    if (!IsSupportedFftSize(fftSize)) fftSize = kDefaultFftSamples;
    overlapPercent = std::clamp(overlapPercent, 0, 95);
    return std::max(
        1, (fftSize * (100 - overlapPercent) + 50) / 100);
}

// TAS_WINDHAWK_EXCLUDE_BEGIN
bool InstanceTakeoverSucceeded(DWORD waitResult) {
    return waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED_0;
}
// TAS_WINDHAWK_EXCLUDE_END

COLORREF ParseColor(PCWSTR text, COLORREF fallback) {
    if (!text || text[0] != L'#' || wcslen(text) != 7) return fallback;
    int digits[6];
    for (int index = 0; index < 6; ++index) {
        digits[index] = HexValue(text[index + 1]);
        if (digits[index] < 0) return fallback;
    }
    return RGB(digits[0] * 16 + digits[1], digits[2] * 16 + digits[3],
               digits[4] * 16 + digits[5]);
}

// TAS_WINDHAWK_EXCLUDE_BEGIN
std::wstring StartupCommandForExecutable(PCWSTR executablePath) {
    if (!executablePath || !*executablePath) return {};
    return L"\"" + std::wstring(executablePath) + L"\"";
}
// TAS_WINDHAWK_EXCLUDE_END

Settings LoadSettings() {
    Settings loaded;
    const int configuredFftSize =
        HostGetIntSetting(L"fftSize", kDefaultFftSamples);
    if (!IsSupportedFftSize(configuredFftSize)) {
        Log(L"Unsupported fftSize=%d; using %d",
            configuredFftSize, kDefaultFftSamples);
        loaded.fftSize = kDefaultFftSamples;
    } else {
        loaded.fftSize = configuredFftSize;
    }
    loaded.fftOverlapPercent = std::clamp(
        HostGetIntSetting(L"overlapPercent", kDefaultFftOverlapPercent),
        0, 95);
    const std::wstring windowFunction =
        HostGetStringSetting(L"windowFunction", L"hann");
    if (!TryParseWindowFunction(windowFunction.c_str(),
                                &loaded.windowFunction)) {
        loaded.windowFunction = WindowFunction::Hann;
        Log(L"Invalid windowFunction '%ls'; using hann",
            windowFunction.c_str());
    }
    loaded.barCount = std::clamp(
        HostGetIntSetting(L"nonOctaveBarCount", 28), 8, kMaxBars);
    loaded.fps = std::clamp(
        HostGetIntSetting(L"framesPerSecond", 30), 1, 100);
    loaded.opacity = static_cast<BYTE>(std::clamp(
        HostGetIntSetting(L"opacity", 180), 0, 255));
    loaded.sensitivity = std::clamp(
        HostGetIntSetting(L"sensitivity", 100), 25, 500) / 100.0f;
    const std::wstring minimumDecibels =
        HostGetStringSetting(L"minimumDecibels", L"-72");
    const std::wstring maximumDecibels =
        HostGetStringSetting(L"maximumDecibels", L"-6");
    const bool minimumDecibelsValid = TryParseClampedFloat(
        minimumDecibels.c_str(), -160.0f, -1.0f,
        &loaded.minimumDecibels);
    const bool maximumDecibelsValid = TryParseClampedFloat(
        maximumDecibels.c_str(), -120.0f, 0.0f,
        &loaded.maximumDecibels);
    if (!minimumDecibelsValid || !maximumDecibelsValid ||
        loaded.maximumDecibels <= loaded.minimumDecibels) {
        Log(L"Invalid spectrum dB range '%ls' to '%ls'; using -72 to -6",
            minimumDecibels.c_str(), maximumDecibels.c_str());
        loaded.minimumDecibels = -72.0f;
        loaded.maximumDecibels = -6.0f;
    }
    loaded.attackMs = std::clamp(
        HostGetIntSetting(L"attackMs", 20), 0, 2000);
    loaded.releaseMs = std::clamp(
        HostGetIntSetting(L"releaseMs", 220), 0, 5000);
    const std::wstring minimumFrequency =
        HostGetStringSetting(L"minimumFrequency", L"31.5");
    if (!TryParseClampedFloat(minimumFrequency.c_str(), 1.0f, 20000.0f,
                              &loaded.minimumFrequency)) {
        loaded.minimumFrequency = 31.5f;
        Log(L"Invalid minimumFrequency '%ls'; using 31.5",
            minimumFrequency.c_str());
    }
    const std::wstring maximumFrequency =
        HostGetStringSetting(L"maximumFrequency", L"16000");
    if (!TryParseClampedFloat(maximumFrequency.c_str(), 20.0f, 24000.0f,
                              &loaded.maximumFrequency)) {
        loaded.maximumFrequency = 16000.0f;
        Log(L"Invalid maximumFrequency '%ls'; using 16000",
            maximumFrequency.c_str());
    }
    if (loaded.maximumFrequency <= loaded.minimumFrequency) {
        Log(L"Invalid frequency range %.2f-%.2f Hz; using 31.5-16000 Hz",
            loaded.minimumFrequency, loaded.maximumFrequency);
        loaded.minimumFrequency = 31.5f;
        loaded.maximumFrequency = 16000.0f;
    }
    const std::wstring minimumCenterFrequency =
        HostGetStringSetting(L"minimumCenterFrequency", L"31.5");
    if (!TryParseClampedFloat(minimumCenterFrequency.c_str(), 1.0f, 20000.0f,
                              &loaded.minimumCenterFrequency)) {
        loaded.minimumCenterFrequency = 31.5f;
        Log(L"Invalid minCenterFrequency '%ls'; using 31.5",
            minimumCenterFrequency.c_str());
    }
    const std::wstring maximumCenterFrequency =
        HostGetStringSetting(L"maximumCenterFrequency", L"16000");
    if (!TryParseClampedFloat(maximumCenterFrequency.c_str(), 20.0f, 24000.0f,
                              &loaded.maximumCenterFrequency)) {
        loaded.maximumCenterFrequency = 16000.0f;
        Log(L"Invalid maxCenterFrequency '%ls'; using 16000",
            maximumCenterFrequency.c_str());
    }
    const std::wstring referenceFrequency =
        HostGetStringSetting(L"referenceFrequency", L"1000");
    if (!TryParseClampedFloat(referenceFrequency.c_str(), 20.0f, 20000.0f,
                              &loaded.referenceFrequency)) {
        loaded.referenceFrequency = 1000.0f;
        Log(L"Invalid referenceFrequency '%ls'; using 1000",
            referenceFrequency.c_str());
    }
    if (loaded.maximumCenterFrequency <= loaded.minimumCenterFrequency) {
        Log(L"Invalid nominal center range %.2f-%.2f Hz; using 31.5-16000 Hz",
            loaded.minimumCenterFrequency, loaded.maximumCenterFrequency);
        loaded.minimumCenterFrequency = 31.5f;
        loaded.maximumCenterFrequency = 16000.0f;
    }
    const std::wstring frequencyScale =
        HostGetStringSetting(L"frequencyScale", L"thirdOctave");
    if (!TryParseFrequencyScale(frequencyScale.c_str(),
                                &loaded.frequencyScale)) {
        loaded.frequencyScale = FrequencyScale::ThirdOctaveNominal;
        Log(L"Invalid frequencyScale '%ls'; using thirdOctave",
            frequencyScale.c_str());
    }
    if (loaded.frequencyScale == FrequencyScale::ThirdOctaveNominal) {
        const int nominalBandCount = ThirdOctaveBandCount(
            loaded.minimumCenterFrequency, loaded.maximumCenterFrequency,
            loaded.referenceFrequency);
        if (nominalBandCount < 8 || nominalBandCount > kMaxBars) {
            Log(L"Invalid third-octave center range; using 31.5-16000 Hz");
            loaded.minimumCenterFrequency = 31.5f;
            loaded.maximumCenterFrequency = 16000.0f;
            loaded.referenceFrequency = 1000.0f;
            loaded.barCount = 28;
        } else {
            loaded.barCount = nominalBandCount;
        }
    }
    const std::wstring bandAggregation =
        HostGetStringSetting(L"bandAggregation", L"energy");
    if (!TryParseBandAggregation(bandAggregation.c_str(),
                                 &loaded.bandAggregation)) {
        loaded.bandAggregation = BandAggregation::Energy;
        Log(L"Invalid bandAggregation '%ls'; using energy",
            bandAggregation.c_str());
    }
    const std::wstring frequencyWeighting =
        HostGetStringSetting(L"frequencyWeighting", L"none");
    if (!TryParseFrequencyWeighting(frequencyWeighting.c_str(),
                                    &loaded.frequencyWeighting)) {
        loaded.frequencyWeighting = FrequencyWeighting::None;
        Log(L"Invalid frequencyWeighting '%ls'; using none",
            frequencyWeighting.c_str());
    }
    loaded.foldBelowMinimum =
        HostGetIntSetting(L"foldBelowMinimum", 0) != 0;
    loaded.hideWhenSilent = HostGetIntSetting(L"hideWhenSilent", 0) != 0;
    const std::wstring silenceThreshold =
        HostGetStringSetting(L"silenceThreshold", L"0.015");
    if (!TryParseClampedFloat(silenceThreshold.c_str(), 0.0f, 1.0f,
                              &loaded.silenceThreshold)) {
        loaded.silenceThreshold = 0.015f;
        Log(L"Invalid silenceThreshold '%ls'; using 0.015",
            silenceThreshold.c_str());
    }
    loaded.silenceHideDelayMs = std::clamp(
        HostGetIntSetting(L"silenceHideDelayMs", 500), 0, 10000);
    loaded.peakEnabled = HostGetIntSetting(L"peakEnabled", 1) != 0;
    loaded.peakShowWhenSilent =
        HostGetIntSetting(L"peakShowWhenSilent", 1) != 0;
    loaded.peakHoldMs = std::clamp(
        HostGetIntSetting(L"peakHoldMs", 160), 0, 5000);
    const std::wstring peakGravity =
        HostGetStringSetting(L"peakGravity", L"3.2");
    if (!TryParseClampedFloat(peakGravity.c_str(), 0.1f, 50.0f,
                              &loaded.peakGravity)) {
        loaded.peakGravity = 3.2f;
        Log(L"Invalid peakGravity '%ls'; using 3.2", peakGravity.c_str());
    }
    loaded.peakHeight = std::clamp(
        HostGetIntSetting(L"peakHeight", 2), 1, 8);
    loaded.peakGap = std::clamp(
        HostGetIntSetting(L"peakGap", 1), 0, 8);
    loaded.bottomPadding = std::clamp(
        HostGetIntSetting(L"bottomPadding", 5), 0, 12);
    loaded.topPadding = std::clamp(
        HostGetIntSetting(L"topPadding", 5), 0, 12);
    loaded.rightPadding = std::clamp(
        HostGetIntSetting(L"rightPadding", 10), 0, 40);
    loaded.barWidthPercent = std::clamp(
        HostGetIntSetting(L"barWidthPercent", 48), 15, 95);
    loaded.color = ParseColor(
        HostGetStringSetting(L"barColor", L"#FF78D4").c_str(),
        RGB(255, 120, 212));
    loaded.secondColor = ParseColor(
        HostGetStringSetting(L"secondColor", L"#00C2FF").c_str(),
        RGB(0, 194, 255));

    loaded.leftPadding = std::clamp(
        HostGetIntSetting(L"spectrumLeftOffset", 66), 0, 4096);
    loaded.autoPosition = HostGetIntSetting(L"autoPosition", 1) != 0;

    // TAS_WINDHAWK_EXCLUDE_BEGIN
    if (HostSupportsStartup()) {
        std::wstring autoMode;
        const bool explicitAuto = HostReadRawSetting(L"auto", &autoMode);
        loaded.autoPosition = true;
        if (explicitAuto &&
            !TryParseAutoMode(autoMode.c_str(), &loaded.autoPosition)) {
            Log(L"Invalid auto mode '%ls'; using auto=1", autoMode.c_str());
            loaded.autoPosition = true;
        }

        std::wstring position;
        if (!HostReadRawSetting(L"spectrumLeftOffset", &position)) {
            position = L"auto";
        }
        int offset = 66;
        if (TryParseClampedInt(position.c_str(), 0, 4096, &offset)) {
            loaded.leftPadding = offset;
            if (!explicitAuto) loaded.autoPosition = false;
        } else if (TokenEquals(position.c_str(), L"auto")) {
            loaded.leftPadding = 66;
            if (!explicitAuto) loaded.autoPosition = true;
            if (explicitAuto && !loaded.autoPosition) {
                Log(L"spectrumLeftOffset=auto has no manual value; using 66");
            }
        } else {
            loaded.leftPadding = 66;
            Log(L"Invalid spectrumLeftOffset '%ls'; using 66", position.c_str());
        }

        std::wstring startup;
        if (HostReadRawSetting(L"startWithWindows", &startup)) {
            bool startWithWindows = false;
            if (TryParseAutoMode(startup.c_str(), &startWithWindows)) {
                HostSyncStartup(startWithWindows);
            } else {
                Log(L"Invalid startWithWindows value '%ls'; registry unchanged",
                    startup.c_str());
            }
        }
    }
    // TAS_WINDHAWK_EXCLUDE_END

    Log(L"Settings loaded: fft=%d window=%ls "
        L"overlap=%d%% hop=%d bars=%d fps=%d opacity=%u "
        L"sensitivity=%.2f dB=%.1f..%.1f attack=%dms release=%dms "
        L"range=%.2f-%.2f centers=%.2f-%.2f/ref=%.1f "
        L"scale=%ls aggregation=%ls weighting=%ls foldLow=%d "
        L"auto=%d insets=%d/%d/%d/%d "
        L"peak=%d/%dms/%.2f/%d/%d showSilent=%d "
        L"silence=%d/%.3f/%dms "
        L"colors=#%02X%02X%02X/#%02X%02X%02X",
        loaded.fftSize,
        WindowFunctionName(loaded.windowFunction),
        loaded.fftOverlapPercent,
        CalculateFftHopSamples(loaded.fftSize, loaded.fftOverlapPercent),
        loaded.barCount, loaded.fps,
        loaded.opacity, loaded.sensitivity,
        loaded.minimumDecibels, loaded.maximumDecibels,
        loaded.attackMs, loaded.releaseMs,
        loaded.minimumFrequency, loaded.maximumFrequency,
        loaded.minimumCenterFrequency, loaded.maximumCenterFrequency,
        loaded.referenceFrequency,
        FrequencyScaleName(loaded.frequencyScale),
        BandAggregationName(loaded.bandAggregation),
        FrequencyWeightingName(loaded.frequencyWeighting),
        loaded.foldBelowMinimum,
        loaded.autoPosition, loaded.leftPadding, loaded.rightPadding,
        loaded.topPadding, loaded.bottomPadding,
        loaded.peakEnabled, loaded.peakHoldMs, loaded.peakGravity,
        loaded.peakHeight, loaded.peakGap, loaded.peakShowWhenSilent,
        loaded.hideWhenSilent, loaded.silenceThreshold,
        loaded.silenceHideDelayMs,
        GetRValue(loaded.color), GetGValue(loaded.color), GetBValue(loaded.color),
        GetRValue(loaded.secondColor), GetGValue(loaded.secondColor),
        GetBValue(loaded.secondColor));
    return loaded;
}

}  // namespace tas

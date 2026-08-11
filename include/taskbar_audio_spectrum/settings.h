#pragma once

#include "platform.h"

namespace tas {

enum class FrequencyScale { ThirdOctaveNominal, Bark, Logarithmic, HtkMel, Linear };
enum class BandAggregation { Peak, Energy, Mean, Slaney };
enum class FrequencyWeighting { None, A, C };
enum class WindowFunction { Hann, Hamming, BlackmanHarris };

struct Settings {
    int fftSize = kDefaultFftSamples;
    int fftOverlapPercent = kDefaultFftOverlapPercent;
    WindowFunction windowFunction = WindowFunction::Hann;
    int barCount = 28;
    int fps = 30;
    COLORREF color = RGB(255, 120, 212);
    COLORREF secondColor = RGB(0, 194, 255);
    BYTE opacity = 180;
    float sensitivity = 1.0f;
    float minimumDecibels = -72.0f;
    float maximumDecibels = -6.0f;
    int attackMs = 20;
    int releaseMs = 220;
    float minimumFrequency = 31.5f;
    float maximumFrequency = 16000.0f;
    float minimumCenterFrequency = 31.5f;
    float maximumCenterFrequency = 16000.0f;
    float referenceFrequency = 1000.0f;
    FrequencyScale frequencyScale = FrequencyScale::ThirdOctaveNominal;
    BandAggregation bandAggregation = BandAggregation::Energy;
    FrequencyWeighting frequencyWeighting = FrequencyWeighting::None;
    bool foldBelowMinimum = false;
    bool hideWhenSilent = false;
    float silenceThreshold = 0.015f;
    int silenceHideDelayMs = 500;
    bool peakEnabled = true;
    bool peakShowWhenSilent = true;
    int peakHoldMs = 160;
    float peakGravity = 3.2f;
    int peakHeight = 2;
    int peakGap = 1;
    int bottomPadding = 5;
    int topPadding = 5;
    int leftPadding = 66;
    int rightPadding = 10;
    int barWidthPercent = 48;
    bool autoPosition = true;
};

bool TokenEquals(PCWSTR text, PCWSTR expected);
// TAS_WINDHAWK_EXCLUDE_BEGIN
bool TryParseAutoMode(PCWSTR text, bool* automatic);
// TAS_WINDHAWK_EXCLUDE_END
// TAS_WINDHAWK_EXCLUDE_BEGIN
bool TryParseClampedInt(PCWSTR text, int minimum, int maximum, int* value);
// TAS_WINDHAWK_EXCLUDE_END
bool TryParseClampedFloat(PCWSTR text, float minimum, float maximum, float* value);
bool TryParseFrequencyScale(PCWSTR text, FrequencyScale* scale);
PCWSTR FrequencyScaleName(FrequencyScale scale);
bool TryParseBandAggregation(PCWSTR text, BandAggregation* aggregation);
PCWSTR BandAggregationName(BandAggregation aggregation);
bool TryParseFrequencyWeighting(PCWSTR text, FrequencyWeighting* weighting);
PCWSTR FrequencyWeightingName(FrequencyWeighting weighting);
bool TryParseWindowFunction(PCWSTR text, WindowFunction* function);
PCWSTR WindowFunctionName(WindowFunction function);
bool IsSupportedFftSize(int fftSize);
int CalculateFftHopSamples(int fftSize, int overlapPercent);
// TAS_WINDHAWK_EXCLUDE_BEGIN
bool InstanceTakeoverSucceeded(DWORD waitResult);
std::wstring StartupCommandForExecutable(PCWSTR executablePath);
// TAS_WINDHAWK_EXCLUDE_END
bool TryParseColor(PCWSTR text, COLORREF* color);
Settings LoadSettings();

}  // namespace tas

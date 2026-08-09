#pragma once

#include "settings.h"

#include <mmreg.h>

namespace tas {

struct AnalysisPlan {
    int bars = 0;
    int sampleRate = 0;
    int fftSize = 0;
    int fftBins = 0;
    int hopSamples = 0;
    float sensitivity = 1.0f;
    float binWidth = 0.0f;
    double windowSumSquares = 0.0;
    float releaseRetention = 0.0f;
    float minimumDecibels = -72.0f;
    float maximumDecibels = -6.0f;
    BandAggregation bandAggregation = BandAggregation::Energy;
    bool foldBelowMinimum = false;
    std::vector<float> window;
    std::vector<float> binPowerGain;
    std::array<float, kMaxBars> centerFrequency{};
    std::array<float, kMaxBars> lowerFrequency{};
    std::array<float, kMaxBars> upperFrequency{};
    std::vector<std::vector<std::pair<int, float>>> binWeights;
};

struct PeakState { float level = 0.0f; float velocity = 0.0f; float holdSeconds = 0.0f; };
struct LevelPixelCoverage { int fullPixels = 0; float partialPixel = 0.0f; };
using BandLevels = std::array<float, kMaxBars>;

struct AnalysisScratch {
    std::vector<std::complex<float>> transformed;
    std::vector<double> spectrumPower;
};

float ReadSample(const BYTE* data, UINT32 frame, int channel,
                 const WAVEFORMATEX* format);
AnalysisPlan MakeAnalysisPlan(int sampleRate, const Settings& settings);
// TAS_WINDHAWK_EXCLUDE_BEGIN
void Analyze(const std::vector<float>& samples, const AnalysisPlan& plan,
             BandLevels* levels);
// TAS_WINDHAWK_EXCLUDE_END
void AnalyzeChannels(const std::vector<std::vector<float>>& channels,
                     size_t oldestSample, const AnalysisPlan& plan,
                     AnalysisScratch* scratch, BandLevels* levels);
// TAS_WINDHAWK_EXCLUDE_BEGIN
void PublishSilence(const AnalysisPlan& plan, BandLevels* levels);
// TAS_WINDHAWK_EXCLUDE_END
void ClearBands(BandLevels* levels);
float SmoothDisplayLevel(float current, float target, float deltaSeconds,
                         int attackMs, int releaseMs);
int CalculateRenderedLevelHeight(float level, int maximumHeight, int minimumHeight);
LevelPixelCoverage CalculateLevelPixelCoverage(float level, int maximumHeight);
int CalculatePeakBlockBottom(int peakLevelHeight, int spectrumTop,
                             int spectrumBottom, int peakHeight, int peakGap,
                             bool keepVisibleAtZero);
void UpdatePeak(PeakState* peak, float level, float deltaSeconds,
                float holdSeconds, float gravity);

}  // namespace tas

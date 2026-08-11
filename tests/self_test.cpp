#include <taskbar_audio_spectrum/application.h>
#include <taskbar_audio_spectrum/audio_capture.h>
#include <taskbar_audio_spectrum/host.h>
#include <taskbar_audio_spectrum/json_config.h>
#include <taskbar_audio_spectrum/search_locator.h>
#include <taskbar_audio_spectrum/standalone_host.h>
#include <taskbar_audio_spectrum/spectrum_analysis.h>

#include "../src/runtime_context.h"

#include <iostream>
#include <limits>

namespace {

int g_failures = 0;
tas::Settings g_settings;
tas::BandLevels g_bands{};
tas::JsonConfig g_standaloneConfig;

void Check(bool condition, const char* description) {
    if (condition) {
        std::cout << "PASS: " << description << '\n';
    } else {
        std::cerr << "FAIL: " << description << '\n';
        ++g_failures;
    }
}

}  // namespace

void TestConfigurationAndPlatform() {
    using namespace tas;
    InitializeStandaloneHost(L"C:\\Temp\\TaskbarAudioSpectrum.exe");

    const Settings defaults;
    Check(defaults.fftSize == 16384 &&
              defaults.fftOverlapPercent == 75 &&
              defaults.barCount == 28 && defaults.fps == 30 &&
              defaults.frequencyScale == FrequencyScale::ThirdOctaveNominal &&
              defaults.bandAggregation == BandAggregation::Energy &&
              defaults.minimumDecibels == -72.0f &&
              defaults.maximumDecibels == -6.0f &&
              defaults.attackMs == 20 && defaults.releaseMs == 220 &&
              defaults.color == RGB(255, 120, 212) &&
              defaults.secondColor == RGB(0, 194, 255) &&
              defaults.peakEnabled && defaults.peakShowWhenSilent &&
              !defaults.foldBelowMinimum && defaults.autoPosition &&
              defaults.leftPadding == 66,
          "professional default spectrum profile");

    Check(ParseColor(L"#1234AB", RGB(0, 0, 0)) == RGB(0x12, 0x34, 0xAB),
          "valid RGB color parsing");
    Check(ParseColor(L"invalid", RGB(1, 2, 3)) == RGB(1, 2, 3),
          "invalid RGB color fallback");
    COLORREF parsedColor = 0;
    Check(TryParseColor(L"#ABCDEF", &parsedColor) &&
              parsedColor == RGB(0xAB, 0xCD, 0xEF) &&
              !TryParseColor(L"#GG0000", &parsedColor),
          "RGB color validation reports invalid input");
    bool automatic = false;
    Check(TryParseAutoMode(L" 1 ", &automatic) && automatic,
          "numeric automatic mode parsing");
    Check(TryParseAutoMode(L"userdefine", &automatic) && !automatic,
          "named manual mode parsing");
    Check(!TryParseAutoMode(L"sometimes", &automatic),
          "invalid automatic mode rejection");

    JsonConfig json;
    std::wstring jsonError;
    std::wstring jsonValue;
    JsonConfig examplePreset;
    Check(examplePreset.LoadFile(L"spectrum.example.json", &jsonError) &&
              examplePreset.Paths().size() == 39,
          "example preset contains all 38 supported settings");
    const bool parsedJson = json.Parse(
        LR"({
          "audio": {
            "fftSize": 8192,
            "overlapPercent": 75,
            "windowFunction": "hann"
          },
          "spectrum": {
            "bands": 24,
            "frequencyScale": "bark",
            "minFrequency": 31.5,
            "maxFrequency": 16000,
            "bandAggregation": "peak",
            "frequencyWeighting": "none",
            "foldBelowMinimum": true
          },
          "peak": {
            "showWhenSilent": true
          }
        })",
        &jsonError);
    Check(parsedJson &&
              json.Get(L"audio.overlapPercent", &jsonValue) &&
              jsonValue == L"75" &&
              json.Get(L"audio.windowFunction", &jsonValue) &&
              jsonValue == L"hann" &&
              json.Get(L"spectrum.minFrequency", &jsonValue) &&
              jsonValue == L"31.5" &&
              json.Get(L"spectrum.frequencyScale", &jsonValue) &&
              jsonValue == L"bark" &&
              json.Get(L"spectrum.bandAggregation", &jsonValue) &&
              jsonValue == L"peak" &&
              json.Get(L"spectrum.frequencyWeighting", &jsonValue) &&
              jsonValue == L"none" &&
              json.Get(L"spectrum.foldBelowMinimum", &jsonValue) &&
              jsonValue == L"true" &&
              json.Get(L"peak.showWhenSilent", &jsonValue) &&
              jsonValue == L"true",
          "nested JSON configuration parsing");
    Check(!json.Parse(LR"({"spectrum":{"bands":24,"bands":30}})",
                      &jsonError) &&
              jsonError.find(L"duplicate") != std::wstring::npos,
          "duplicate JSON setting rejection");
    Check(!json.Parse(LR"({"spectrum":{"bands":[24]}})", &jsonError) &&
              jsonError.find(L"arrays") != std::wstring::npos,
          "unsupported JSON array rejection");
    Check(!json.Parse(LR"({"spectrum":{"bands":24,}})", &jsonError),
          "malformed JSON rejection");
    Check(json.Parse(LR"({"z":1,"a":{"second":2,"first":3}})",
                     &jsonError) &&
              json.Paths() == std::vector<std::wstring>{
                  L"a.first", L"a.second", L"z"},
          "JSON leaf paths are enumerated deterministically for validation");

    const bool fullRuntimeConfigParsed = g_standaloneConfig.Parse(
        LR"({
          "audio": {
            "fftSize": 4096,
            "overlapPercent": 40,
            "windowFunction": "hamming"
          },
          "spectrum": {
            "bands": 37,
            "frequencyScale": "bark",
            "minFrequency": 40.5,
            "maxFrequency": 18000,
            "minCenterFrequency": 50,
            "maxCenterFrequency": 12500,
            "referenceFrequency": 440,
            "bandAggregation": "mean",
            "frequencyWeighting": "C",
            "foldBelowMinimum": true,
            "sensitivity": 125,
            "minimumDecibels": -84.5,
            "maximumDecibels": -3.5,
            "attackMs": 12,
            "releaseMs": 345
          },
          "peak": {
            "enabled": false,
            "showWhenSilent": false,
            "holdMs": 321,
            "gravity": 4.5,
            "height": 3,
            "gap": 2
          },
          "silence": {
            "enabled": true,
            "threshold": 0.123,
            "hideDelayMs": 678
          },
          "display": {
            "framesPerSecond": 55,
            "barColor": "#123456",
            "secondColor": "#ABCDEF",
            "opacity": 201,
            "barWidthPercent": 67
          },
          "position": {
            "automatic": false,
            "leftOffset": 123,
            "rightOffset": 12,
            "topOffset": 4,
            "bottomOffset": 6
          }
        })",
        &jsonError);
    std::wstring fullSchemaError;
    Check(fullRuntimeConfigParsed &&
              ValidateStandaloneConfig(g_standaloneConfig, &fullSchemaError),
          "all documented JSON parameters pass strict schema validation");

    Check(g_standaloneConfig.Parse(
              LR"({
                "display": {
                  "rightPadding": 39,
                  "topPadding": 11,
                  "bottomPadding": 10
                },
                "position": {
                  "rightOffset": 7,
                  "topOffset": 3,
                  "bottomOffset": 4
                }
              })",
              &jsonError),
          "legacy position-path migration sample parsing");
    std::wstring schemaError;
    Check(!ValidateStandaloneConfig(g_standaloneConfig, &schemaError) &&
              schemaError.find(L"display.rightPadding") != std::wstring::npos &&
              schemaError.find(L"position.rightOffset") != std::wstring::npos,
          "legacy position paths are rejected with migration guidance");
    g_standaloneConfig.Parse(L"{}", &jsonError);
    g_settings = Settings{};
    Check(TokenEquals(L"  --exit  ", L"--exit") &&
              !TokenEquals(L"--exit-now", L"--exit"),
          "exact command-line exit switch parsing");
    Check(InstanceTakeoverSucceeded(WAIT_OBJECT_0) &&
              InstanceTakeoverSucceeded(WAIT_ABANDONED_0) &&
              !InstanceTakeoverSucceeded(WAIT_TIMEOUT),
          "single-instance takeover result handling");
    Check(StartupCommandForExecutable(
              L"C:\\Spectrum App\\spectrum.exe") ==
              L"\"C:\\Spectrum App\\spectrum.exe\"",
          "startup command quotes executable path");
    Check(StartupCommandForExecutable(L"").empty(),
          "empty executable path is rejected");

    int parsedOffset = -1;
    Check(TryParseClampedInt(L" 0 ", 0, 4096, &parsedOffset) &&
              parsedOffset == 0,
          "zero manual offset parsing");
    Check(TryParseClampedInt(L"99999", 0, 4096, &parsedOffset) &&
              parsedOffset == 4096,
          "manual offset upper bound");
    Check(!TryParseClampedInt(L"12px", 0, 4096, &parsedOffset),
          "invalid manual offset rejection");
    Check(CalculateFftHopSamples(8192, 40) == 4915 &&
              CalculateFftHopSamples(8192, 75) == 2048 &&
              CalculateFftHopSamples(4096, 50) == 2048 &&
              CalculateFftHopSamples(16384, 95) == 819 &&
              CalculateFftHopSamples(8192, 100) == 410,
          "FFT overlap to hop-size conversion");
    float parsedFrequency = 0.0f;
    Check(TryParseClampedFloat(L" 31.5 ", 1.0f, 24000.0f,
                               &parsedFrequency) &&
              std::abs(parsedFrequency - 31.5f) < 0.001f,
          "floating-point frequency parsing");
    Check(TryParseClampedFloat(L"99999", 1.0f, 24000.0f,
                               &parsedFrequency) &&
              parsedFrequency == 24000.0f &&
              !TryParseClampedFloat(L"31.5Hz", 1.0f, 24000.0f,
                                    &parsedFrequency),
          "frequency parsing clamps values and rejects suffixes");
    FrequencyScale parsedScale = FrequencyScale::Linear;
    Check(TryParseFrequencyScale(L" LOGARITHMIC ", &parsedScale) &&
              parsedScale == FrequencyScale::Logarithmic &&
              TryParseFrequencyScale(L"htk-mel", &parsedScale) &&
              parsedScale == FrequencyScale::HtkMel &&
              TryParseFrequencyScale(L"linear", &parsedScale) &&
              parsedScale == FrequencyScale::Linear &&
              TryParseFrequencyScale(L"third-octave", &parsedScale) &&
              parsedScale == FrequencyScale::ThirdOctaveNominal &&
              TryParseFrequencyScale(L"bark", &parsedScale) &&
              parsedScale == FrequencyScale::Bark &&
              !TryParseFrequencyScale(L"erb", &parsedScale),
          "frequency-scale parsing");
    BandAggregation parsedAggregation = BandAggregation::Energy;
    Check(TryParseBandAggregation(L"peakDb", &parsedAggregation) &&
              parsedAggregation == BandAggregation::Peak &&
              TryParseBandAggregation(L"energy", &parsedAggregation) &&
              parsedAggregation == BandAggregation::Energy &&
              TryParseBandAggregation(L"mean", &parsedAggregation) &&
              parsedAggregation == BandAggregation::Mean &&
              TryParseBandAggregation(L"slaney", &parsedAggregation) &&
              parsedAggregation == BandAggregation::Slaney &&
              !TryParseBandAggregation(L"median", &parsedAggregation),
          "band-amplitude aggregation parsing");
    FrequencyWeighting parsedWeighting = FrequencyWeighting::A;
    Check(TryParseFrequencyWeighting(L"none", &parsedWeighting) &&
              parsedWeighting == FrequencyWeighting::None &&
              TryParseFrequencyWeighting(L"A-weighting", &parsedWeighting) &&
              parsedWeighting == FrequencyWeighting::A &&
              TryParseFrequencyWeighting(L"c", &parsedWeighting) &&
              parsedWeighting == FrequencyWeighting::C &&
              !TryParseFrequencyWeighting(L"K", &parsedWeighting),
          "frequency-weighting parsing");
    WindowFunction parsedWindow = WindowFunction::Hamming;
    Check(TryParseWindowFunction(L"Hann", &parsedWindow) &&
              parsedWindow == WindowFunction::Hann &&
              TryParseWindowFunction(L"blackman-harris", &parsedWindow) &&
              parsedWindow == WindowFunction::BlackmanHarris &&
              !TryParseWindowFunction(L"rectangle", &parsedWindow),
          "FFT window-function parsing");

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.wBitsPerSample = 32;
    format.nBlockAlign = 8;
    const float stereo[] = {0.25f, -0.5f};
    Check(std::abs(ReadSample(reinterpret_cast<const BYTE*>(stereo), 0, 0,
                              &format) - 0.25f) < 0.00001f,
          "left floating-point sample decoding");
    Check(std::abs(ReadSample(reinterpret_cast<const BYTE*>(stereo), 0, 1,
                              &format) + 0.5f) < 0.00001f,
          "right floating-point sample decoding");
    constexpr float invalidFloat = std::numeric_limits<float>::quiet_NaN();
    Check(ReadSample(reinterpret_cast<const BYTE*>(&invalidFloat), 0, 0,
                     &format) == 0.0f &&
              ReadSample(reinterpret_cast<const BYTE*>(stereo), 0, 2,
                         &format) == 0.0f,
          "invalid floating-point samples and channel indices are rejected");
    format.nChannels = 1;
    format.wBitsPerSample = 64;
    format.nBlockAlign = 8;
    const double float64 = 0.75;
    Check(std::abs(ReadSample(reinterpret_cast<const BYTE*>(&float64), 0, 0,
                              &format) - 0.75f) < 0.00001f,
          "64-bit floating-point sample decoding");

    WAVEFORMATEXTENSIBLE extensible{};
    extensible.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    extensible.Format.nChannels = 1;
    extensible.Format.wBitsPerSample = 32;
    extensible.Format.nBlockAlign = 4;
    extensible.Format.cbSize =
        sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    extensible.Samples.wValidBitsPerSample = 32;
    extensible.SubFormat = {
        WAVE_FORMAT_IEEE_FLOAT, 0x0000, 0x0010,
        {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    constexpr float extensibleFloat = 1.0f;
    Check(std::abs(ReadSample(
              reinterpret_cast<const BYTE*>(&extensibleFloat), 0, 0,
              &extensible.Format) - extensibleFloat) < 0.00001f,
          "extensible floating-point sample decoding");
    extensible.SubFormat.Data4[0] ^= 1;
    Check(std::abs(ReadSample(
              reinterpret_cast<const BYTE*>(&extensibleFloat), 0, 0,
              &extensible.Format) - extensibleFloat) > 0.1f,
          "extensible sample subtype requires an exact GUID match");

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.wBitsPerSample = 8;
    format.nBlockAlign = 1;
    const BYTE pcm8[] = {0, 128, 255};
    Check(ReadSample(pcm8, 0, 0, &format) == -1.0f &&
              ReadSample(pcm8, 1, 0, &format) == 0.0f,
          "8-bit PCM sample decoding");
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    const int16_t pcm16 = INT16_MIN;
    Check(ReadSample(reinterpret_cast<const BYTE*>(&pcm16), 0, 0,
                     &format) == -1.0f,
          "16-bit PCM sample decoding");
    format.wBitsPerSample = 24;
    format.nBlockAlign = 3;
    const BYTE pcm24[] = {0x00, 0x00, 0x80};
    Check(ReadSample(pcm24, 0, 0, &format) == -1.0f,
          "24-bit PCM sign extension");
    format.wBitsPerSample = 32;
    format.nBlockAlign = 4;
    const int32_t pcm32 = INT32_MIN;
    Check(ReadSample(reinterpret_cast<const BYTE*>(&pcm32), 0, 0,
                     &format) == -1.0f,
          "32-bit PCM sample decoding");

    SearchLayout layout;
    layout.rect = {0, 0, 275, 40};
    layout.dpi = 120;
    g_settings.autoPosition = true;
    g_settings.leftPadding = 66;
    g_settings.rightPadding = 10;
    g_settings.topPadding = 5;
    g_settings.bottomPadding = 5;
    const RECT automaticBounds = CalculateSpectrumBounds(layout, g_settings);
    Check(automaticBounds.left == 83 && automaticBounds.right == 262 &&
              automaticBounds.top == 6 && automaticBounds.bottom == 34,
          "automatic positioning honors all four DPI-scaled edge offsets");
    g_settings.autoPosition = false;
    g_settings.leftPadding = 120;
    Check(CalculateSpectrumBounds(layout, g_settings).left == 150,
          "manual offset receives DPI scaling");
    g_settings.leftPadding = 0;
    Check(CalculateSpectrumBounds(layout, g_settings).left == 0,
          "zero manual offset remains user-defined");
    Check(GetWindowDpiOrDefault(nullptr) == 96,
          "DPI compatibility helper has a stable fallback");
    Check(RectEquals({1, 2, 3, 4}, {1, 2, 3, 4}) &&
              !RectEquals({1, 2, 3, 4}, {1, 2, 3, 5}),
          "layout rectangle equality");
    Check(IsSearchExecutableName(
              L"C:\\Windows\\SystemApps\\SearchHost.exe") &&
              IsSearchExecutableName(L"searchapp.EXE") &&
              IsSearchExecutableName(L"C:/Windows/SearchUI.exe") &&
              !IsSearchExecutableName(L"explorer.exe") &&
              !IsSearchExecutableName(L"SearchHost.exe.disabled"),
          "search interface executable recognition");
    Check(DetectSearchHostKind(L"SearchUI.exe") ==
              SearchHostKind::Windows10 &&
              DetectSearchHostKind(L"SearchHost.exe") ==
              SearchHostKind::Windows11 &&
              DetectSearchHostKind(L"explorer.exe") ==
              SearchHostKind::Unknown,
          "Windows 10 and Windows 11 search hosts are distinguished");
    Check(!IsSearchBoxMode(0, SearchHostKind::Windows10) &&
              !IsSearchBoxMode(1, SearchHostKind::Windows10) &&
              IsSearchBoxMode(2, SearchHostKind::Windows10) &&
              !IsSearchBoxMode(3, SearchHostKind::Windows10) &&
              !IsSearchBoxMode(2, SearchHostKind::Windows11) &&
              IsSearchBoxMode(3, SearchHostKind::Windows11) &&
              !IsSearchBoxMode(2, SearchHostKind::Unknown) &&
              IsSearchBoxMode(3, SearchHostKind::Unknown),
          "search-box mode respects the Windows search host generation");
}

void TestSpectrumAnalysis() {
    using namespace tas;
    g_settings.barCount = 24;
    g_settings.fftSize = 8192;
    g_settings.fftOverlapPercent = 40;
    g_settings.windowFunction = WindowFunction::Hann;
    g_settings.sensitivity = 1.0f;
    g_settings.minimumDecibels = -72.0f;
    g_settings.maximumDecibels = -12.0f;
    g_settings.bandAggregation = BandAggregation::Peak;
    g_settings.frequencyWeighting = FrequencyWeighting::None;
    g_settings.releaseMs = 220;
    g_settings.minimumFrequency = 31.5f;
    g_settings.maximumFrequency = 16000.0f;
    g_settings.frequencyScale = FrequencyScale::Bark;
    const AnalysisPlan barkPlan = MakeAnalysisPlan(48000, g_settings);
    bool equalBarkSteps = true;
    const auto barkValue = [](float frequency) {
        return 26.81f * frequency / (1960.0f + frequency) - 0.53f;
    };
    const float expectedBarkStep =
        (barkValue(16000.0f) - barkValue(31.5f)) / 24.0f;
    for (int band = 0; band + 1 < barkPlan.bars; ++band) {
        equalBarkSteps = equalBarkSteps &&
            std::abs(barkValue(barkPlan.upperFrequency[band]) -
                     barkValue(barkPlan.lowerFrequency[band]) -
                     expectedBarkStep) < 0.001f &&
            std::abs(barkPlan.upperFrequency[band] -
                     barkPlan.lowerFrequency[band + 1]) < 0.01f;
    }
    Check(barkPlan.bars == 24 && equalBarkSteps &&
              std::abs(barkPlan.lowerFrequency[0] - 31.5f) < 0.01f &&
              std::abs(barkPlan.upperFrequency[23] - 16000.0f) < 1.0f,
          "24 equal-Bark critical-band visualizer plan generation");

    g_settings.frequencyScale = FrequencyScale::Logarithmic;
    g_settings.foldBelowMinimum = true;
    const AnalysisPlan plan = MakeAnalysisPlan(48000, g_settings);
    Check(plan.bars == 24 && plan.fftSize == 8192 &&
              plan.fftBins == 4097 && plan.hopSamples == 4915 &&
              plan.window.size() == 8192 && plan.windowSumSquares > 3000.0 &&
              std::abs(plan.binWidth - 5.859375f) < 0.001f &&
              std::abs(plan.lowerFrequency[0] - 31.5f) < 0.01f &&
              std::abs(plan.upperFrequency[23] - 16000.0f) < 1.0f &&
              !plan.binWeights[0].empty() &&
              !plan.binWeights[23].empty(),
          "8192-point continuous-band FFT plan generation");
    bool contiguousBands = true;
    bool equalRatioBands = true;
    const double expectedRatio = std::pow(16000.0 / 31.5, 1.0 / 24.0);
    for (int band = 0; band + 1 < plan.bars; ++band) {
        contiguousBands = contiguousBands &&
            std::abs(plan.upperFrequency[band] -
                     plan.lowerFrequency[band + 1]) < 0.01f;
        equalRatioBands = equalRatioBands &&
            std::abs(plan.upperFrequency[band] /
                         plan.lowerFrequency[band] -
                     expectedRatio) < 0.0001;
    }
    Check(contiguousBands, "logarithmic analysis bands have no gaps");
    Check(equalRatioBands &&
              std::abs(plan.upperFrequency[0] - 40.84f) < 0.1f &&
              std::abs(plan.upperFrequency[3] - 88.98f) < 0.1f &&
              std::abs(plan.lowerFrequency[14] - 1193.16f) < 1.0f &&
              std::abs(plan.upperFrequency[15] - 2005.32f) < 1.0f &&
              std::abs(plan.lowerFrequency[23] - 12341.78f) < 2.0f,
          "24 equal-ratio bands match the planned musical layout");
    Check(plan.binWeights[0].front().first == 1,
          "sub-31.5 Hz FFT bins fold into the first band");

    g_settings.barCount = 28;
    g_settings.fftOverlapPercent = 75;
    g_settings.minimumCenterFrequency = 31.5f;
    g_settings.maximumCenterFrequency = 16000.0f;
    g_settings.referenceFrequency = 1000.0f;
    g_settings.frequencyScale = FrequencyScale::ThirdOctaveNominal;
    const AnalysisPlan thirdOctavePlan =
        MakeAnalysisPlan(48000, g_settings);
    bool thirdOctaveContiguous = true;
    bool thirdOctaveRatio = true;
    for (int band = 0; band + 1 < thirdOctavePlan.bars; ++band) {
        thirdOctaveContiguous = thirdOctaveContiguous &&
            std::abs(thirdOctavePlan.upperFrequency[band] -
                     thirdOctavePlan.lowerFrequency[band + 1]) < 0.01f;
        thirdOctaveRatio = thirdOctaveRatio &&
            std::abs(thirdOctavePlan.centerFrequency[band + 1] /
                         thirdOctavePlan.centerFrequency[band] -
                     std::pow(2.0f, 1.0f / 3.0f)) < 0.0001f;
    }
    Check(thirdOctavePlan.bars == 28 &&
              thirdOctavePlan.hopSamples == 2048 &&
              std::abs(48000.0f / thirdOctavePlan.hopSamples -
                       23.4375f) < 0.0001f &&
              std::abs(thirdOctavePlan.centerFrequency[0] - 31.25f) < 0.01f &&
              std::abs(thirdOctavePlan.centerFrequency[27] - 16000.0f) < 0.1f &&
              std::abs(thirdOctavePlan.lowerFrequency[0] - 27.84f) < 0.1f &&
              std::abs(thirdOctavePlan.upperFrequency[27] - 17959.4f) < 1.0f &&
              thirdOctaveContiguous && thirdOctaveRatio,
          "28 nominal-center third-octave FFT bands and update rate");

    std::vector<float> thirdOctaveSamples(thirdOctavePlan.fftSize);
    bool thirdOctaveCentersCovered = true;
    for (int band = 0; band < thirdOctavePlan.bars; ++band) {
        const double center = thirdOctavePlan.centerFrequency[band];
        for (int index = 0; index < thirdOctavePlan.fftSize; ++index) {
            thirdOctaveSamples[index] = static_cast<float>(
                0.2 * std::sin(2.0 * kPi * center * index / 48000.0));
        }
        ClearBands(&g_bands);
        Analyze(thirdOctaveSamples, thirdOctavePlan, &g_bands);
        thirdOctaveCentersCovered = thirdOctaveCentersCovered &&
            g_bands[band] > 0.35f;
    }
    Check(thirdOctaveCentersCovered,
          "all 28 nominal third-octave center tones drive their own peak bar");

    g_settings.barCount = 24;
    g_settings.fftOverlapPercent = 40;
    g_settings.frequencyScale = FrequencyScale::Logarithmic;

    bool dynamicPlansValid = true;
    const int fftSizes[] = {4096, 8192, 16384};
    const WindowFunction windows[] = {
        WindowFunction::Hann,
        WindowFunction::Hamming,
        WindowFunction::BlackmanHarris,
    };
    for (const int fftSize : fftSizes) {
        for (const WindowFunction window : windows) {
            g_settings.fftSize = fftSize;
            g_settings.windowFunction = window;
            const AnalysisPlan dynamicPlan =
                MakeAnalysisPlan(48000, g_settings);
            dynamicPlansValid = dynamicPlansValid &&
                dynamicPlan.fftSize == fftSize &&
                dynamicPlan.fftBins == fftSize / 2 + 1 &&
                dynamicPlan.window.size() == static_cast<size_t>(fftSize) &&
                dynamicPlan.windowSumSquares > 0.0 &&
                std::abs(dynamicPlan.binWidth - 48000.0f / fftSize) < 0.001f &&
                !dynamicPlan.binWeights[0].empty() &&
                !dynamicPlan.binWeights[23].empty();
        }
    }
    Check(dynamicPlansValid,
          "all supported FFT sizes and window functions build valid plans");
    g_settings.fftSize = 8192;
    g_settings.windowFunction = WindowFunction::Hann;

    g_settings.frequencyScale = FrequencyScale::HtkMel;
    g_settings.bandAggregation = BandAggregation::Slaney;
    const AnalysisPlan melPlan = MakeAnalysisPlan(48000, g_settings);
    bool overlappingMelTriangles = true;
    bool slaneyNormalized = true;
    for (int band = 0; band < melPlan.bars; ++band) {
        double weightSum = 0.0;
        for (const auto& [bin, weight] : melPlan.binWeights[band]) {
            (void)bin;
            weightSum += weight;
        }
        slaneyNormalized = slaneyNormalized &&
            std::abs(weightSum - 1.0) < 0.05;
        if (band + 1 < melPlan.bars) {
            overlappingMelTriangles = overlappingMelTriangles &&
                std::abs(melPlan.centerFrequency[band] -
                         melPlan.lowerFrequency[band + 1]) < 0.01f &&
                std::abs(melPlan.upperFrequency[band] -
                         melPlan.centerFrequency[band + 1]) < 0.01f;
        }
    }
    Check(std::abs(melPlan.lowerFrequency[0] - 31.5f) < 0.01f &&
              std::abs(melPlan.upperFrequency[23] - 16000.0f) < 1.0f &&
              overlappingMelTriangles && slaneyNormalized,
          "overlapping HTK Mel triangles with Slaney area normalization");

    g_settings.frequencyScale = FrequencyScale::Linear;
    g_settings.bandAggregation = BandAggregation::Peak;
    const AnalysisPlan linearPlan = MakeAnalysisPlan(48000, g_settings);
    const float linearWidth = (16000.0f - 31.5f) / 24.0f;
    bool equalLinearWidths = true;
    for (int band = 0; band < linearPlan.bars; ++band) {
        equalLinearWidths = equalLinearWidths &&
            std::abs((linearPlan.upperFrequency[band] -
                      linearPlan.lowerFrequency[band]) - linearWidth) < 0.1f;
    }
    Check(equalLinearWidths &&
              std::abs(linearPlan.lowerFrequency[0] - 31.5f) < 0.01f &&
              std::abs(linearPlan.upperFrequency[0] - 696.8542f) < 0.1f &&
              std::abs(linearPlan.lowerFrequency[23] - 15334.645f) < 0.1f &&
              std::abs(linearPlan.upperFrequency[23] - 16000.0f) < 0.1f,
          "linear frequency-scale plan generation");

    g_settings.frequencyWeighting = FrequencyWeighting::A;
    const AnalysisPlan aWeightedPlan = MakeAnalysisPlan(48000, g_settings);
    const int bin100 = static_cast<int>(std::lround(
        100.0f / aWeightedPlan.binWidth));
    const int bin1000 = static_cast<int>(std::lround(
        1000.0f / aWeightedPlan.binWidth));
    Check(aWeightedPlan.binPowerGain[bin100] < 0.02f &&
              aWeightedPlan.binPowerGain[bin1000] > 0.9f &&
              aWeightedPlan.binPowerGain[bin1000] < 1.1f,
          "A-weighting is applied per FFT bin and normalized near 1 kHz");
    g_settings.frequencyWeighting = FrequencyWeighting::None;

    g_settings.minimumFrequency = 50.0f;
    g_settings.maximumFrequency = 8000.0f;
    g_settings.frequencyScale = FrequencyScale::Logarithmic;
    g_settings.foldBelowMinimum = false;
    const AnalysisPlan customPlan = MakeAnalysisPlan(48000, g_settings);
    Check(std::abs(customPlan.lowerFrequency[0] - 50.0f) < 0.01f &&
              std::abs(customPlan.upperFrequency[23] - 8000.0f) < 0.1f &&
              customPlan.binWeights[0].front().first > 1,
          "custom frequency range without low-frequency folding");
    std::vector<float> belowRangeSamples(customPlan.fftSize);
    for (int index = 0; index < customPlan.fftSize; ++index) {
        belowRangeSamples[index] = static_cast<float>(
            0.2 * std::sin(2.0 * kPi * 20.0 * index / 48000.0));
    }
    tas::ClearBands(&g_bands);
    Analyze(belowRangeSamples, customPlan, &g_bands);
    float strongestBelowRangeBand = 0.0f;
    for (int band = 0; band < customPlan.bars; ++band) {
        strongestBelowRangeBand = std::max(
            strongestBelowRangeBand,
            g_bands[band]);
    }
    Check(strongestBelowRangeBand < 0.05f,
          "foldBelowMinimum=false excludes a separated 20 Hz test tone");

    g_settings.minimumFrequency = 31.5f;
    g_settings.maximumFrequency = 16000.0f;
    g_settings.frequencyScale = FrequencyScale::Logarithmic;
    g_settings.foldBelowMinimum = true;

    std::vector<float> samples(plan.fftSize);
    constexpr int testBand = 10;
    const double frequency = std::sqrt(
        plan.lowerFrequency[testBand] * plan.upperFrequency[testBand]);
    for (int index = 0; index < plan.fftSize; ++index) {
        samples[index] = static_cast<float>(
            0.2 * std::sin(2.0 * kPi * frequency * index / 48000.0));
    }
    ClearBands(&g_bands);
    Analyze(samples, plan, &g_bands);
    Check(g_bands[testBand] > 0.35f,
          "FFT response at planned band center");

    bool boundariesCovered = true;
    for (int band = 0; band + 1 < plan.bars; ++band) {
        const double boundary = plan.upperFrequency[band];
        for (int index = 0; index < plan.fftSize; ++index) {
            samples[index] = static_cast<float>(
                0.2 * std::sin(2.0 * kPi * boundary * index / 48000.0));
        }
        ClearBands(&g_bands);
        Analyze(samples, plan, &g_bands);
        const float left =
            g_bands[band];
        const float right =
            g_bands[band + 1];
        boundariesCovered = boundariesCovered &&
            std::max(left, right) > 0.35f;
    }
    Check(boundariesCovered,
          "every logarithmic band boundary responds without a blind spot");

    bool sweepCovered = true;
    for (int point = 0; point <= 120; ++point) {
        const double sweepFrequency = 31.5 * std::pow(
            16000.0 / 31.5, point / 120.0);
        for (int index = 0; index < plan.fftSize; ++index) {
            samples[index] = static_cast<float>(
                0.2 * std::sin(
                    2.0 * kPi * sweepFrequency * index / 48000.0));
        }
        ClearBands(&g_bands);
        Analyze(samples, plan, &g_bands);
        float strongest = 0.0f;
        for (int band = 0; band < plan.bars; ++band) {
            strongest = std::max(
                strongest,
                g_bands[band]);
        }
        sweepCovered = sweepCovered && strongest > 0.35f;
    }
    Check(sweepCovered,
          "31.5 Hz to 16 kHz logarithmic sweep has no response gaps");

    for (int index = 0; index < plan.fftSize; ++index) {
        samples[index] = static_cast<float>(
            0.2 * std::sin(2.0 * kPi * 20.0 * index / 48000.0));
    }
    ClearBands(&g_bands);
    Analyze(samples, plan, &g_bands);
    Check(g_bands[0] > 0.35f,
          "20 Hz sub-bass contributes to the first band");

    std::fill(samples.begin(), samples.end(), 0.2f);
    ClearBands(&g_bands);
    Analyze(samples, plan, &g_bands);
    float strongestDcBand = 0.0f;
    for (int band = 0; band < plan.bars; ++band) {
        strongestDcBand = std::max(
            strongestDcBand,
            g_bands[band]);
    }
    Check(strongestDcBand < 0.001f,
          "DC offset is removed before sub-bass analysis");

    for (int index = 0; index < plan.fftSize; ++index) {
        samples[index] = static_cast<float>(
            0.2 * std::sin(2.0 * kPi * 18000.0 * index / 48000.0));
    }
    ClearBands(&g_bands);
    Analyze(samples, plan, &g_bands);
    float strongestUltrahighBand = 0.0f;
    for (int band = 0; band < plan.bars; ++band) {
        strongestUltrahighBand = std::max(
            strongestUltrahighBand,
            g_bands[band]);
    }
    Check(strongestUltrahighBand < 0.05f,
          "content above 16 kHz is ignored");

    std::vector<std::vector<float>> stereoChannels(
        2, std::vector<float>(plan.fftSize));
    constexpr size_t oldestSample = 713;
    for (size_t index = 0; index < static_cast<size_t>(plan.fftSize); ++index) {
        const float value = static_cast<float>(
            0.2 * std::sin(2.0 * kPi * frequency * index / 48000.0));
        const size_t destination = (oldestSample + index) % plan.fftSize;
        stereoChannels[0][destination] = value;
        stereoChannels[1][destination] = -value;
    }
    ClearBands(&g_bands);
    AnalysisScratch analysisScratch;
    AnalyzeChannels(stereoChannels, oldestSample, plan,
                    &analysisScratch, &g_bands);
    Check(g_bands[testBand] > 0.35f,
          "opposite-phase stereo channels retain spectral power");
}

void TestRenderingAndNotifications() {
    using namespace tas;
    ApplicationContext context;
    context.bandActivityEvent.reset(
        CreateEventW(nullptr, FALSE, FALSE, nullptr));
    Check(static_cast<bool>(context.bandActivityEvent),
          "band activity event creation");
    if (context.bandActivityEvent) {
        BandLevels levels{};
        ClearPublishedBands(context);
        PublishBandLevels(context, levels);
        Check(WaitForSingleObject(context.bandActivityEvent.get(), 0) ==
                  WAIT_TIMEOUT,
              "silent bands don't wake the renderer");
        levels[3] = 0.25f;
        PublishBandLevels(context, levels);
        Check(WaitForSingleObject(context.bandActivityEvent.get(), 0) ==
                  WAIT_OBJECT_0,
              "first non-zero band wakes the renderer");
        levels[3] = 0.5f;
        PublishBandLevels(context, levels);
        Check(WaitForSingleObject(context.bandActivityEvent.get(), 0) ==
                  WAIT_TIMEOUT,
              "continuous signal doesn't flood renderer wakeups");
        ClearPublishedBands(context);
        PublishBandLevels(context, levels);
        Check(WaitForSingleObject(context.bandActivityEvent.get(), 0) ==
                  WAIT_OBJECT_0,
              "signal after silence wakes the renderer again");
    }

    float smoothedAt60Fps = 0.0f;
    for (int frame = 0; frame < 3; ++frame) {
        smoothedAt60Fps = SmoothDisplayLevel(
            smoothedAt60Fps, 1.0f, 1.0f / 60.0f, 35, 220);
    }
    const float smoothedSingleStep = SmoothDisplayLevel(
        0.0f, 1.0f, 3.0f / 60.0f, 35, 220);
    Check(smoothedAt60Fps > 0.7f && smoothedAt60Fps < 0.8f &&
              std::abs(smoothedAt60Fps - smoothedSingleStep) < 0.0001f,
          "60 FPS attack smoothing is elapsed-time invariant between FFT updates");
    const float releasedAt60Fps = SmoothDisplayLevel(
        smoothedAt60Fps, 0.0f, 1.0f / 60.0f, 35, 220);
    Check(releasedAt60Fps > 0.0f && releasedAt60Fps < smoothedAt60Fps,
          "60 FPS release smoothing moves continuously toward the latest FFT target");
    float releasedToZero = 1.0f;
    for (int frame = 0; frame < 240; ++frame) {
        releasedToZero = SmoothDisplayLevel(
            releasedToZero, 0.0f, 1.0f / 60.0f, 25, 180);
    }
    Check(releasedToZero == 0.0f,
          "release smoothing reaches exact zero after silence");
    Check(CalculateRenderedLevelHeight(0.0f, 28, 0) == 0 &&
              CalculateRenderedLevelHeight(0.0001f, 28, 0) == 0 &&
              CalculateRenderedLevelHeight(1.0f, 28, 2) == 28,
          "zero levels render no residual baseline");
    const LevelPixelCoverage halfPixel =
        CalculateLevelPixelCoverage(0.5f / 28.0f, 28);
    const LevelPixelCoverage oneAndQuarterPixels =
        CalculateLevelPixelCoverage(1.25f / 28.0f, 28);
    Check(halfPixel.fullPixels == 0 &&
              std::abs(halfPixel.partialPixel - 0.5f) < 0.0001f &&
              oneAndQuarterPixels.fullPixels == 1 &&
              std::abs(oneAndQuarterPixels.partialPixel - 0.25f) < 0.0001f,
          "subpixel bar heights fade smoothly instead of holding at two pixels");
    Check(CalculatePeakBlockBottom(0, 0, 28, 2, 1, true) == 28 &&
              CalculatePeakBlockBottom(0, 0, 28, 2, 1, false) == -1 &&
              CalculatePeakBlockBottom(2, 0, 28, 2, 1, true) == 25 &&
              CalculatePeakBlockBottom(0, 0, 1, 2, 1, true) == -1,
          "peak blocks remain continuously visible when their own band reaches zero");

    PeakState peak;
    UpdatePeak(&peak, 0.8f, 1.0f / 30.0f, 0.16f, 3.2f);
    for (int frame = 0; frame < 4; ++frame) {
        UpdatePeak(&peak, 0.0f, 1.0f / 30.0f, 0.16f, 3.2f);
    }
    Check(std::abs(peak.level - 0.8f) < 0.001f,
          "peak block holds briefly before falling");
    for (int frame = 0; frame < 12; ++frame) {
        UpdatePeak(&peak, 0.0f, 1.0f / 30.0f, 0.16f, 3.2f);
    }
    Check(peak.level > 0.0f && peak.level < 0.8f && peak.velocity > 0.0f,
          "peak block accelerates downward");
    for (int frame = 0; frame < 120; ++frame) {
        UpdatePeak(&peak, 0.0f, 1.0f / 30.0f, 0.16f, 3.2f);
    }
    Check(peak.level == 0.0f,
          "peak block settles at the current spectrum level");

    ScopedHandle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    Check(static_cast<bool>(event), "notification test event creation");
    if (!event) return;
    auto* notification = new EndpointNotificationClient(event.get());
    notification->OnDefaultDeviceChanged(eCapture, eConsole, nullptr);
    Check(WaitForSingleObject(event.get(), 0) == WAIT_TIMEOUT,
          "capture-device changes are ignored");
    notification->OnDefaultDeviceChanged(eRender, eMultimedia, nullptr);
    Check(WaitForSingleObject(event.get(), 0) == WAIT_TIMEOUT,
          "non-console render changes are ignored");
    notification->OnDefaultDeviceChanged(eRender, eConsole, nullptr);
    Check(WaitForSingleObject(event.get(), 0) == WAIT_OBJECT_0,
          "default console render change requests reconnect");
    notification->Release();

    Check(wcsstr(kOverlayWindowClass, L"Windhawk") == nullptr,
          "overlay class has no legacy host coupling");
}

int main() {
    TestConfigurationAndPlatform();
    TestSpectrumAnalysis();
    TestRenderingAndNotifications();
    tas::ClearBands(&g_bands);
    std::cout << (g_failures ? "SELF-TEST FAILED" : "SELF-TEST PASSED")
              << '\n';
    return g_failures ? 1 : 0;
}

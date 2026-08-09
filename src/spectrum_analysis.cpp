#include <taskbar_audio_spectrum/spectrum_analysis.h>

namespace tas {

namespace {

constexpr GUID kIeeeFloatSubformat = {
    WAVE_FORMAT_IEEE_FLOAT, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

}  // namespace

void ClearBands(BandLevels* levels) {
    if (levels) levels->fill(0.0f);
}

float ReadSample(const BYTE* data, UINT32 frame, int channel,
                 const WAVEFORMATEX* format) {
    if (!data || !format) return 0.0f;
    const int channels = format->nChannels;
    const int bits = format->wBitsPerSample;
    const int bytesPerSample = channels > 0 ? format->nBlockAlign / channels : 0;
    if (channel < 0 || channel >= channels || bytesPerSample <= 0) return 0.0f;
    const BYTE* sample = data + static_cast<size_t>(frame) * format->nBlockAlign +
                         static_cast<size_t>(channel) * bytesPerSample;

    bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        isFloat = IsEqualGUID(extensible->SubFormat, kIeeeFloatSubformat);
    }
    if (isFloat && bits == 32) {
        float value;
        memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    if (isFloat && bits == 64) {
        double value;
        memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
    }
    if (bits == 8) return (static_cast<int>(sample[0]) - 128) / 128.0f;
    if (bits == 16) {
        int16_t value;
        memcpy(&value, sample, sizeof(value));
        return value / 32768.0f;
    }
    if (bits == 24) {
        int32_t value = sample[0] | (sample[1] << 8) | (sample[2] << 16);
        if (value & 0x800000) value |= ~0xFFFFFF;
        return value / 8388608.0f;
    }
    if (bits == 32) {
        int32_t value;
        memcpy(&value, sample, sizeof(value));
        return value / 2147483648.0f;
    }
    return 0.0f;
}

namespace {

double FrequencyToScale(double frequency, FrequencyScale scale) {
    switch (scale) {
        case FrequencyScale::Bark:
            return 26.81 * frequency / (1960.0 + frequency) - 0.53;
        case FrequencyScale::HtkMel:
            return 2595.0 * std::log10(1.0 + frequency / 700.0);
        case FrequencyScale::Linear:
            return frequency;
        default:
            return std::log(frequency);
    }
}

double ScaleToFrequency(double value, FrequencyScale scale) {
    switch (scale) {
        case FrequencyScale::Bark:
            return 1960.0 / (26.81 / (value + 0.53) - 1.0);
        case FrequencyScale::HtkMel:
            return 700.0 * (std::pow(10.0, value / 2595.0) - 1.0);
        case FrequencyScale::Linear:
            return value;
        default:
            return std::exp(value);
    }
}

float FrequencyWeightingDecibels(float frequency,
                                 FrequencyWeighting weighting) {
    if (weighting == FrequencyWeighting::None || frequency <= 0.0f) {
        return 0.0f;
    }
    const double frequencySquared =
        static_cast<double>(frequency) * frequency;
    constexpr double kReferenceSquared = 12194.0 * 12194.0;
    if (weighting == FrequencyWeighting::A) {
        const double response =
            kReferenceSquared * frequencySquared * frequencySquared /
            ((frequencySquared + 20.6 * 20.6) *
             std::sqrt((frequencySquared + 107.7 * 107.7) *
                       (frequencySquared + 737.9 * 737.9)) *
             (frequencySquared + kReferenceSquared));
        return static_cast<float>(2.0 + 20.0 * std::log10(
            std::max(response, 1.0e-30)));
    }
    const double response =
        kReferenceSquared * frequencySquared /
        ((frequencySquared + 20.6 * 20.6) *
         (frequencySquared + kReferenceSquared));
    return static_cast<float>(0.06 + 20.0 * std::log10(
        std::max(response, 1.0e-30)));
}

float WindowValue(int index, int size, WindowFunction function) {
    const double phase = 2.0 * kPi * index / std::max(1, size - 1);
    switch (function) {
        case WindowFunction::Hamming:
            return static_cast<float>(0.54 - 0.46 * std::cos(phase));
        case WindowFunction::BlackmanHarris:
            return static_cast<float>(
                0.35875 - 0.48829 * std::cos(phase) +
                0.14128 * std::cos(2.0 * phase) -
                0.01168 * std::cos(3.0 * phase));
        default:
            return static_cast<float>(0.5 - 0.5 * std::cos(phase));
    }
}

void FourierTransform(std::vector<std::complex<float>>* values) {
    if (!values || values->empty()) return;
    const size_t size = values->size();
    for (size_t index = 1, reversed = 0; index < size; ++index) {
        size_t bit = size >> 1;
        while (reversed & bit) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed) std::swap((*values)[index], (*values)[reversed]);
    }
    for (size_t length = 2; length <= size; length <<= 1) {
        const float angle = static_cast<float>(-2.0 * kPi / length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t offset = 0; offset < size; offset += length) {
            std::complex<float> rotation(1.0f, 0.0f);
            const size_t half = length >> 1;
            for (size_t index = 0; index < half; ++index) {
                const std::complex<float> even = (*values)[offset + index];
                const std::complex<float> odd =
                    (*values)[offset + index + half] * rotation;
                (*values)[offset + index] = even + odd;
                (*values)[offset + index + half] = even - odd;
                rotation *= step;
            }
        }
    }
}

void AccumulateChannelPower(const std::vector<float>& samples,
                            size_t oldestSample,
                            std::vector<double>* accumulatedPower,
                            std::vector<std::complex<float>>* transformed,
                            const AnalysisPlan& plan) {
    if (!accumulatedPower || !transformed || samples.size() !=
            static_cast<size_t>(plan.fftSize)) return;
    transformed->resize(plan.fftSize);
    double mean = 0.0;
    for (int index = 0; index < plan.fftSize; ++index) {
        mean += samples[(oldestSample + index) % plan.fftSize];
    }
    mean /= plan.fftSize;
    for (int index = 0; index < plan.fftSize; ++index) {
        const size_t source = (oldestSample + index) % plan.fftSize;
        (*transformed)[index] =
            static_cast<float>(samples[source] - mean) * plan.window[index];
    }
    FourierTransform(transformed);
    for (int bin = 1; bin < plan.fftBins; ++bin) {
        (*accumulatedPower)[bin] += std::norm((*transformed)[bin]);
    }
}

double InterpolateSpectrumPower(const std::vector<double>& spectrumPower,
                                float frequency,
                                const AnalysisPlan& plan) {
    if (spectrumPower.size() < 2 || plan.binWidth <= 0.0f) return 0.0;
    const float coordinate = std::clamp(
        frequency / plan.binWidth, 1.0f,
        static_cast<float>(plan.fftBins - 1));
    const int lowerBin = static_cast<int>(std::floor(coordinate));
    const int upperBin = std::min(lowerBin + 1, plan.fftBins - 1);
    const float fraction = coordinate - lowerBin;
    const double lowerPower =
        spectrumPower[lowerBin] * plan.binPowerGain[lowerBin];
    const double upperPower =
        spectrumPower[upperBin] * plan.binPowerGain[upperBin];
    return lowerPower * (1.0f - fraction) + upperPower * fraction;
}

double PeakBandPower(const std::vector<double>& spectrumPower, int band,
                      const AnalysisPlan& plan) {
    const float lower = band == 0 && plan.foldBelowMinimum
        ? plan.binWidth : plan.lowerFrequency[band];
    const float upper = plan.upperFrequency[band];
    double peak = std::max(
        InterpolateSpectrumPower(spectrumPower, lower, plan),
        InterpolateSpectrumPower(spectrumPower, upper, plan));
    const int firstBin = std::max(
        1, static_cast<int>(std::ceil(lower / plan.binWidth)));
    const int lastBin = std::min(
        plan.fftBins - 1,
        static_cast<int>(std::floor(upper / plan.binWidth)));
    for (int bin = firstBin; bin <= lastBin; ++bin) {
        peak = std::max(
            peak, spectrumPower[bin] * plan.binPowerGain[bin]);
    }
    return peak;
}

void PublishBandPower(const std::vector<double>& spectrumPower,
                      size_t channels, const AnalysisPlan& plan,
                      BandLevels* levels) {
    if (!levels || !channels || plan.bars <= 0 ||
        plan.windowSumSquares <= 0.0) return;
    const double normalization =
        2.0 / (plan.fftSize * plan.windowSumSquares * channels);
    const float gainDecibels =
        20.0f * std::log10(std::max(0.01f, plan.sensitivity));
    const float decibelSpan =
        plan.maximumDecibels - plan.minimumDecibels;

    for (int band = 0; band < plan.bars; ++band) {
        double power = 0.0;
        if (plan.bandAggregation == BandAggregation::Peak) {
            power = PeakBandPower(spectrumPower, band, plan);
        } else {
            double weightSum = 0.0;
            for (const auto& [bin, weight] : plan.binWeights[band]) {
                power += spectrumPower[bin] * plan.binPowerGain[bin] * weight;
                weightSum += weight;
            }
            if (plan.bandAggregation == BandAggregation::Mean &&
                weightSum > 0.0) {
                power /= weightSum;
            }
        }
        power *= normalization;
        const float decibels = static_cast<float>(
            10.0 * std::log10(std::max(power, 1.0e-16))) + gainDecibels;
        const float level = std::clamp(
            (decibels - plan.minimumDecibels) / decibelSpan,
            0.0f, 1.0f);
        (*levels)[band] = level;
    }
    for (int band = plan.bars; band < kMaxBars; ++band) {
        (*levels)[band] = 0.0f;
    }
}

}  // namespace

AnalysisPlan MakeAnalysisPlan(int sampleRate, const Settings& settings) {
    AnalysisPlan plan;
    if (sampleRate <= 0) return plan;
    plan.bars = std::clamp(settings.barCount, 1, kMaxBars);
    plan.binWeights.resize(kMaxBars);
    plan.fftSize = IsSupportedFftSize(settings.fftSize)
        ? settings.fftSize : kDefaultFftSamples;
    plan.fftBins = plan.fftSize / 2 + 1;
    plan.hopSamples = CalculateFftHopSamples(
        plan.fftSize, settings.fftOverlapPercent);
    plan.sensitivity = settings.sensitivity;
    plan.minimumDecibels = settings.minimumDecibels;
    plan.maximumDecibels = settings.maximumDecibels;
    plan.bandAggregation = settings.bandAggregation;
    plan.foldBelowMinimum = settings.foldBelowMinimum;
    plan.binWidth = sampleRate / static_cast<float>(plan.fftSize);
    plan.binPowerGain.resize(plan.fftBins, 1.0f);
    for (int bin = 1; bin < plan.fftBins; ++bin) {
        const float weightingDecibels = FrequencyWeightingDecibels(
            bin * plan.binWidth, settings.frequencyWeighting);
        plan.binPowerGain[bin] = std::pow(10.0f, weightingDecibels / 10.0f);
    }
    plan.window.resize(plan.fftSize);
    for (int index = 0; index < plan.fftSize; ++index) {
        const float value = WindowValue(
            index, plan.fftSize, settings.windowFunction);
        plan.window[index] = value;
        plan.windowSumSquares += static_cast<double>(value) * value;
    }
    if (settings.frequencyScale == FrequencyScale::ThirdOctaveNominal) {
        const int firstIndex = static_cast<int>(std::lround(
            3.0 * std::log2(settings.minimumCenterFrequency /
                            settings.referenceFrequency)));
        const int lastIndex = static_cast<int>(std::lround(
            3.0 * std::log2(settings.maximumCenterFrequency /
                            settings.referenceFrequency)));
        plan.bars = std::clamp(lastIndex - firstIndex + 1, 0, kMaxBars);
        const double halfBandRatio = std::pow(2.0, 1.0 / 6.0);
        for (int band = 0; band < plan.bars; ++band) {
            const float center = static_cast<float>(
                settings.referenceFrequency *
                std::pow(2.0, (firstIndex + band) / 3.0));
            plan.centerFrequency[band] = center;
            plan.lowerFrequency[band] = static_cast<float>(
                center / halfBandRatio);
            plan.upperFrequency[band] = std::min(
                static_cast<float>(center * halfBandRatio),
                sampleRate * 0.5f);
        }
    } else if (settings.frequencyScale == FrequencyScale::HtkMel) {
        const float minimumFrequency = settings.minimumFrequency;
        const float maximumFrequency =
            std::min(settings.maximumFrequency, sampleRate * 0.5f);
        if (minimumFrequency <= 0.0f || maximumFrequency <= minimumFrequency) {
            plan.bars = 0;
            return plan;
        }
        const double scaledMinimum = FrequencyToScale(
            minimumFrequency, FrequencyScale::HtkMel);
        const double scaledMaximum = FrequencyToScale(
            maximumFrequency, FrequencyScale::HtkMel);
        std::vector<float> melPoints(plan.bars + 2);
        for (int point = 0; point < plan.bars + 2; ++point) {
            melPoints[point] = static_cast<float>(ScaleToFrequency(
                scaledMinimum + (scaledMaximum - scaledMinimum) * point /
                    (plan.bars + 1),
                FrequencyScale::HtkMel));
        }
        for (int band = 0; band < plan.bars; ++band) {
            plan.lowerFrequency[band] = melPoints[band];
            plan.centerFrequency[band] = melPoints[band + 1];
            plan.upperFrequency[band] = melPoints[band + 2];
        }
    } else {
        const float minimumFrequency = settings.minimumFrequency;
        const float maximumFrequency =
            std::min(settings.maximumFrequency, sampleRate * 0.5f);
        if (minimumFrequency <= 0.0f || maximumFrequency <= minimumFrequency) {
            plan.bars = 0;
            return plan;
        }
        const double scaledMinimum = FrequencyToScale(
            minimumFrequency, settings.frequencyScale);
        const double scaledMaximum = FrequencyToScale(
            maximumFrequency, settings.frequencyScale);
        for (int band = 0; band < plan.bars; ++band) {
            const float lower = static_cast<float>(ScaleToFrequency(
                scaledMinimum + (scaledMaximum - scaledMinimum) * band /
                    plan.bars,
                settings.frequencyScale));
            const float upper = band + 1 == plan.bars ? maximumFrequency :
                static_cast<float>(ScaleToFrequency(
                    scaledMinimum + (scaledMaximum - scaledMinimum) *
                        (band + 1) / plan.bars,
                    settings.frequencyScale));
            plan.lowerFrequency[band] = lower;
            plan.upperFrequency[band] = upper;
            plan.centerFrequency[band] = static_cast<float>(ScaleToFrequency(
                scaledMinimum + (scaledMaximum - scaledMinimum) *
                    (band + 0.5) / plan.bars,
                settings.frequencyScale));
        }
    }

    for (int band = 0; band < plan.bars; ++band) {
        const float lower = plan.lowerFrequency[band];
        const float upper = plan.upperFrequency[band];
        if (settings.frequencyScale == FrequencyScale::HtkMel) {
            const float center = plan.centerFrequency[band];
            const float slaneyNormalization =
                settings.bandAggregation == BandAggregation::Slaney
                ? 2.0f * plan.binWidth / (upper - lower) : 1.0f;
            for (int bin = 1; bin < plan.fftBins; ++bin) {
                const float frequency = bin * plan.binWidth;
                const float lowerSlope = (frequency - lower) /
                                         (center - lower);
                const float upperSlope = (upper - frequency) /
                                         (upper - center);
                const float triangle = std::max(
                    0.0f, std::min(lowerSlope, upperSlope));
                if (triangle > 0.0f) {
                    plan.binWeights[band].emplace_back(
                        bin, triangle * slaneyNormalization);
                }
            }
            continue;
        }
        const float analysisLower =
            band == 0 && settings.foldBelowMinimum ? 0.0f : lower;
        for (int bin = 1; bin < plan.fftBins; ++bin) {
            const float binLower = std::max(0.0f, (bin - 0.5f) * plan.binWidth);
            const float binUpper = (bin + 0.5f) * plan.binWidth;
            const float overlap = std::min(upper, binUpper) -
                                  std::max(analysisLower, binLower);
            if (overlap > 0.0f) {
                plan.binWeights[band].emplace_back(
                    bin, overlap / plan.binWidth);
            }
        }
    }
    return plan;
}

// TAS_WINDHAWK_EXCLUDE_BEGIN
void Analyze(const std::vector<float>& samples, const AnalysisPlan& plan,
             BandLevels* levels) {
    AnalysisScratch scratch;
    scratch.spectrumPower.assign(plan.fftBins, 0.0);
    AccumulateChannelPower(samples, 0, &scratch.spectrumPower,
                           &scratch.transformed, plan);
    PublishBandPower(scratch.spectrumPower, 1, plan, levels);
}
// TAS_WINDHAWK_EXCLUDE_END

void AnalyzeChannels(const std::vector<std::vector<float>>& channels,
                     size_t oldestSample, const AnalysisPlan& plan,
                     AnalysisScratch* scratch, BandLevels* levels) {
    if (!scratch || !levels || channels.empty()) return;
    scratch->spectrumPower.assign(plan.fftBins, 0.0);
    for (const auto& samples : channels) {
        AccumulateChannelPower(samples, oldestSample,
                               &scratch->spectrumPower,
                               &scratch->transformed, plan);
    }
    PublishBandPower(scratch->spectrumPower, channels.size(), plan, levels);
}

float SmoothDisplayLevel(float current, float target, float deltaSeconds,
                         int attackMs, int releaseMs) {
    current = std::clamp(current, 0.0f, 1.0f);
    target = std::clamp(target, 0.0f, 1.0f);
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.25f);
    const int smoothingMs = target > current ? attackMs : releaseMs;
    if (smoothingMs <= 0) return target;
    const float retention = std::exp(
        -deltaSeconds / (smoothingMs / 1000.0f));
    const float smoothed =
        current * retention + target * (1.0f - retention);
    return target <= 0.0f && smoothed < 0.001f ? 0.0f : smoothed;
}

int CalculateRenderedLevelHeight(float level, int maximumHeight,
                                 int minimumHeight) {
    if (level <= 0.0f || maximumHeight <= 0) return 0;
    level = std::clamp(level, 0.0f, 1.0f);
    minimumHeight = std::clamp(minimumHeight, 0, maximumHeight);
    return std::clamp(
        static_cast<int>(level * maximumHeight), minimumHeight,
        maximumHeight);
}

LevelPixelCoverage CalculateLevelPixelCoverage(float level,
                                               int maximumHeight) {
    LevelPixelCoverage coverage;
    if (level <= 0.0f || maximumHeight <= 0) return coverage;
    const float pixelHeight =
        std::clamp(level, 0.0f, 1.0f) * maximumHeight;
    coverage.fullPixels = std::clamp(
        static_cast<int>(std::floor(pixelHeight)), 0, maximumHeight);
    if (coverage.fullPixels < maximumHeight) {
        coverage.partialPixel = std::clamp(
            pixelHeight - coverage.fullPixels, 0.0f, 1.0f);
    }
    return coverage;
}

int CalculatePeakBlockBottom(int peakLevelHeight, int spectrumTop,
                             int spectrumBottom, int peakHeight, int peakGap,
                             bool keepVisibleAtZero) {
    if (spectrumBottom <= spectrumTop || peakHeight <= 0 ||
        peakHeight > spectrumBottom - spectrumTop) return -1;
    peakLevelHeight = std::max(0, peakLevelHeight);
    if (peakLevelHeight == 0 && !keepVisibleAtZero) return -1;
    const int offset = peakLevelHeight > 0
        ? peakLevelHeight + std::max(0, peakGap)
        : 0;
    return std::clamp(
        spectrumBottom - offset,
        spectrumTop + peakHeight, spectrumBottom);
}

void UpdatePeak(PeakState* peak, float level, float deltaSeconds,
                float holdSeconds, float gravity) {
    if (!peak) return;
    level = std::clamp(level, 0.0f, 1.0f);
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.25f);
    holdSeconds = std::max(0.0f, holdSeconds);
    gravity = std::max(0.0f, gravity);
    if (level >= peak->level) {
        peak->level = level;
        peak->velocity = 0.0f;
        peak->holdSeconds = holdSeconds;
        return;
    }
    float fallingTime = deltaSeconds;
    if (peak->holdSeconds > 0.0f) {
        const float held = std::min(peak->holdSeconds, fallingTime);
        peak->holdSeconds -= held;
        fallingTime -= held;
    }
    if (fallingTime <= 0.0f) return;
    peak->velocity += gravity * fallingTime;
    peak->level -= peak->velocity * fallingTime;
    if (peak->level <= level) {
        peak->level = level;
        peak->velocity = 0.0f;
    }
}

}  // namespace tas

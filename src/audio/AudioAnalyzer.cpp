#include "AudioAnalyzer.h"
#include "FFT.h"
#include <cmath>
#include <algorithm>

// raylib's device mixing rate is fixed at 44100 Hz (see raylib config.h,
// AUDIO_DEVICE_SAMPLE_RATE). Audio stream processors receive float32
// samples interleaved as stereo (2 channels) regardless of source format.
namespace {
constexpr float kSampleRate = 44100.0f;
constexpr int kDeviceChannels = 2;
}

AudioAnalyzer* AudioAnalyzer::sActive = nullptr;

AudioAnalyzer::AudioAnalyzer() {
    ringCapacity_ = static_cast<size_t>(kFFTSize) * 4;
    ring_.assign(ringCapacity_, 0.0f);

    // Hann window, precomputed once.
    for (int i = 0; i < kFFTSize; i++) {
        window_[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i / (kFFTSize - 1));
    }
}

AudioAnalyzer::~AudioAnalyzer() {
    Detach();
}

void AudioAnalyzer::AttachTo(Music& music) {
    Detach();
    attached_ = &music;
    sActive = this;
    AttachAudioStreamProcessor(music.stream, AudioAnalyzer::AudioCallback);
}

void AudioAnalyzer::Detach() {
    if (attached_ != nullptr) {
        DetachAudioStreamProcessor(attached_->stream, AudioAnalyzer::AudioCallback);
        attached_ = nullptr;
    }
    if (sActive == this) sActive = nullptr;
}

void AudioAnalyzer::AudioCallback(void* buffer, unsigned int frames) {
    if (sActive != nullptr) {
        sActive->ProcessCallback(static_cast<const float*>(buffer), frames);
    }
}

void AudioAnalyzer::ProcessCallback(const float* buffer, unsigned int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (unsigned int f = 0; f < frames; f++) {
        float mono = 0.0f;
        for (int c = 0; c < kDeviceChannels; c++) {
            mono += buffer[f * kDeviceChannels + c];
        }
        mono /= static_cast<float>(kDeviceChannels);

        ring_[writePos_] = mono;
        writePos_ = (writePos_ + 1) % ringCapacity_;
        totalWritten_++;
    }
}

void AudioAnalyzer::Update() {
    std::array<float, kFFTSize> samples{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (totalWritten_ < static_cast<size_t>(kFFTSize)) {
            // Not enough audio yet (e.g. right at startup).
            return;
        }
        // Copy the most recent kFFTSize samples ending at writePos_.
        size_t start = (writePos_ + ringCapacity_ - kFFTSize) % ringCapacity_;
        for (int i = 0; i < kFFTSize; i++) {
            samples[i] = ring_[(start + i) % ringCapacity_];
        }
    }

    std::vector<fft::Complex> buf(kFFTSize);
    for (int i = 0; i < kFFTSize; i++) {
        buf[i] = fft::Complex(samples[i] * window_[i], 0.0f);
    }
    fft::Transform(buf);

    const float norm = 2.0f / static_cast<float>(kFFTSize);
    for (int i = 0; i < kSpectrumBins; i++) {
        float mag = std::abs(buf[i]) * norm;
        spectrum_[i] = mag;
        // Exponential smoothing for a less jittery display.
        smoothedSpectrum_[i] += (mag - smoothedSpectrum_[i]) * 0.5f;
    }

    // Log-spaced bars from ~30 Hz to Nyquist, averaging bins within
    // each bar's frequency range. Log spacing matches how the ear
    // perceives pitch and keeps bass from dominating every bar.
    const float minFreq = 30.0f;
    const float maxFreq = kSampleRate / 2.0f;
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(maxFreq);
    for (int b = 0; b < kNumBars; b++) {
        float f0 = std::pow(10.0f, logMin + (logMax - logMin) * (static_cast<float>(b) / kNumBars));
        float f1 = std::pow(10.0f, logMin + (logMax - logMin) * (static_cast<float>(b + 1) / kNumBars));
        int bin0 = std::max(1, static_cast<int>(f0 * kFFTSize / kSampleRate));
        int bin1 = std::min(kSpectrumBins - 1, static_cast<int>(f1 * kFFTSize / kSampleRate));
        if (bin1 < bin0) bin1 = bin0;

        float sum = 0.0f;
        int count = 0;
        for (int i = bin0; i <= bin1; i++) {
            sum += smoothedSpectrum_[i];
            count++;
        }
        float avg = count > 0 ? sum / count : 0.0f;
        float shaped = std::sqrt(std::max(0.0f, avg)) * 2.2f; // perceptual-ish scaling
        bars_[b] += (shaped - bars_[b]) * 0.6f;
    }

    auto bandAvg = [&](float f0, float f1) {
        int bin0 = std::max(1, static_cast<int>(f0 * kFFTSize / kSampleRate));
        int bin1 = std::min(kSpectrumBins - 1, static_cast<int>(f1 * kFFTSize / kSampleRate));
        if (bin1 < bin0) bin1 = bin0;
        float sum = 0.0f;
        int count = 0;
        for (int i = bin0; i <= bin1; i++) {
            sum += smoothedSpectrum_[i];
            count++;
        }
        return count > 0 ? sum / count : 0.0f;
    };

    bass_ = bandAvg(20.0f, 250.0f);
    mid_ = bandAvg(250.0f, 2000.0f);
    treble_ = bandAvg(2000.0f, 10000.0f);
    energy_ = (bass_ * 1.5f + mid_ + treble_ * 0.7f) / 3.0f;

    // Simple energy-based beat detector: compare current bass energy
    // against the recent rolling average. Fires when bass spikes well
    // above the local average, with a cooldown to avoid double-fires.
    energyHistory_.push_back(bass_);
    constexpr size_t kHistoryLen = 45; // ~0.75s at 60Hz updates
    while (energyHistory_.size() > kHistoryLen) energyHistory_.pop_front();

    float avgEnergy = 0.0f;
    for (float e : energyHistory_) avgEnergy += e;
    avgEnergy /= static_cast<float>(energyHistory_.empty() ? 1 : energyHistory_.size());

    beatCooldown_ = std::max(0.0f, beatCooldown_ - GetFrameTime());

    const float threshold = avgEnergy * 1.4f + 0.02f;
    if (bass_ > threshold && beatCooldown_ <= 0.0f && energyHistory_.size() >= kHistoryLen / 2) {
        beatTriggered_ = true;
        float ratio = avgEnergy > 0.0001f ? (bass_ - avgEnergy) / avgEnergy : 1.0f;
        beatIntensity_ = std::clamp(ratio, 0.0f, 3.0f) / 3.0f;
        beatCooldown_ = 0.16f;
    } else {
        beatTriggered_ = false;
    }
}

#include "AudioAnalyzer.h"
#include "FFT.h"
#include <cmath>
#include <algorithm>
#include <chrono>

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

float AudioAnalyzer::Bass() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.bass;
}
float AudioAnalyzer::Mid() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.mid;
}
float AudioAnalyzer::Treble() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.treble;
}
float AudioAnalyzer::Energy() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.energy;
}
bool AudioAnalyzer::BeatTriggered() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.beatTriggered;
}
float AudioAnalyzer::BeatIntensity() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.beatIntensity;
}
float AudioAnalyzer::BassLevel() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.bassLevel;
}
float AudioAnalyzer::MidLevel() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.midLevel;
}
float AudioAnalyzer::TrebleLevel() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.trebleLevel;
}
float AudioAnalyzer::EnergyLevel() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.energyLevel;
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

    float bass = bandAvg(20.0f, 250.0f);
    float mid = bandAvg(250.0f, 2000.0f);
    float treble = bandAvg(2000.0f, 10000.0f);
    float energy = (bass * 1.5f + mid + treble * 0.7f) / 3.0f;

    // Real elapsed time since the last Update() call -- not GetFrameTime()
    // (that tracks the *main render loop's* last frame duration, which is
    // meaningless here now that Update() runs on its own dedicated audio
    // thread at its own cadence -- see the class comment). First call has
    // no prior timestamp to diff against, so it contributes dt=0 (a no-op
    // for the lag/decay below) rather than an undefined/huge value.
    auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if (hasLastUpdateTime_) {
        dt = std::chrono::duration<float>(now - lastUpdateTime_).count();
    }
    lastUpdateTime_ = now;
    hasLastUpdateTime_ = true;

    // Simple energy-based beat detector: compare current bass energy
    // against a rolling average. Fires when bass spikes well above that
    // average, with a cooldown to avoid double-fires. avgBassEnergy_ is a
    // time-based first-order lag (not a fixed-size sample window) so its
    // effective ~0.75s time constant holds regardless of how often
    // Update() is actually called.
    constexpr float kAvgTimeConstant = 0.75f;
    constexpr float kMinWarmupSeconds = 0.4f; // avoid false-positive beats before avgBassEnergy_ has settled
    avgBassEnergy_ += (bass - avgBassEnergy_) * std::min(1.0f, dt / kAvgTimeConstant);
    warmupElapsed_ += dt;

    // Rolling-peak normalization -- see BassLevel() etc.'s header comment.
    // Each band's peak decays linearly toward the current raw value at a
    // fixed rate/sec (so a sudden loud passage is tracked almost
    // immediately -- max() lets it jump straight up -- while a quiet
    // stretch afterward doesn't leave `level` pinned near 0 forever: the
    // peak itself relaxes back down over a couple seconds, the same
    // "auto gain control" shape a VU meter uses). kPeakFloor keeps the
    // divide well-defined during true silence (bass/etc. == 0) instead of
    // level flickering on FP noise near zero.
    constexpr float kPeakDecayPerSecond = 0.4f;
    constexpr float kPeakFloor = 0.02f;
    auto updateLevel = [&](float raw, float& peak) {
        peak = std::max(raw, peak - kPeakDecayPerSecond * dt);
        return std::clamp(raw / std::max(peak, kPeakFloor), 0.0f, 1.0f);
    };
    float bassLevel = updateLevel(bass, bassPeak_);
    float midLevel = updateLevel(mid, midPeak_);
    float trebleLevel = updateLevel(treble, treblePeak_);
    float energyLevel = updateLevel(energy, energyPeak_);

    beatCooldown_ = std::max(0.0f, beatCooldown_ - dt);

    bool beatTriggered = false;
    float beatIntensity = 0.0f;
    const float threshold = avgBassEnergy_ * 1.4f + 0.02f;
    if (bass > threshold && beatCooldown_ <= 0.0f && warmupElapsed_ >= kMinWarmupSeconds) {
        beatTriggered = true;
        float ratio = avgBassEnergy_ > 0.0001f ? (bass - avgBassEnergy_) / avgBassEnergy_ : 1.0f;
        beatIntensity = std::clamp(ratio, 0.0f, 3.0f) / 3.0f;
        beatCooldown_ = 0.16f;
    }

    // Publish the whole derived-scalar set as one unit -- see
    // AudioSnapshot's comment on why this must be atomic-as-a-group, not
    // per-field.
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.bass = bass;
        snapshot_.mid = mid;
        snapshot_.treble = treble;
        snapshot_.energy = energy;
        snapshot_.beatTriggered = beatTriggered;
        snapshot_.beatIntensity = beatIntensity;
        snapshot_.bassLevel = bassLevel;
        snapshot_.midLevel = midLevel;
        snapshot_.trebleLevel = trebleLevel;
        snapshot_.energyLevel = energyLevel;
    }
}

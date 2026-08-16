#pragma once
#include <array>
#include <vector>
#include <deque>
#include <mutex>
#include "raylib.h"

// Captures live samples from a playing Music stream via raylib's audio
// stream processor callback (runs on raylib's audio thread), then on the
// main thread performs windowing + FFT to produce a magnitude spectrum,
// coarse frequency bands (bass/mid/treble), and simple energy-based beat
// detection. All public getters are safe to call from the main thread
// after Update().
class AudioAnalyzer {
public:
    static constexpr int kFFTSize = 1024;               // power of two
    static constexpr int kSpectrumBins = kFFTSize / 2;
    static constexpr int kNumBars = 48;                  // log-spaced display bars

    AudioAnalyzer();
    ~AudioAnalyzer();

    // Attaches a raw-sample tap to the given music stream. Safe to call
    // once after LoadMusicStream. Detach() is called automatically by
    // the destructor / by attaching a new stream.
    void AttachTo(Music& music);
    void Detach();

    // Call once per frame (main thread) after UpdateMusicStream().
    void Update();

    const std::array<float, kSpectrumBins>& Spectrum() const { return smoothedSpectrum_; }
    const std::array<float, kNumBars>& Bars() const { return bars_; }

    float Bass() const { return bass_; }
    float Mid() const { return mid_; }
    float Treble() const { return treble_; }
    float Energy() const { return energy_; }

    bool BeatTriggered() const { return beatTriggered_; }
    float BeatIntensity() const { return beatIntensity_; }

private:
    static void AudioCallback(void* buffer, unsigned int frames);
    void ProcessCallback(const float* buffer, unsigned int frames);

    static AudioAnalyzer* sActive;

    // Ring buffer of mono samples, filled on the audio thread, drained
    // on the main thread. Protected by mutex_ since raylib's audio
    // callback runs on a separate thread.
    std::mutex mutex_;
    std::vector<float> ring_;
    size_t ringCapacity_ = 0;
    size_t writePos_ = 0;
    size_t totalWritten_ = 0;

    std::array<float, kFFTSize> window_{};
    std::array<float, kSpectrumBins> spectrum_{};
    std::array<float, kSpectrumBins> smoothedSpectrum_{};
    std::array<float, kNumBars> bars_{};

    float bass_ = 0.0f, mid_ = 0.0f, treble_ = 0.0f, energy_ = 0.0f;

    std::deque<float> energyHistory_;
    bool beatTriggered_ = false;
    float beatIntensity_ = 0.0f;
    float beatCooldown_ = 0.0f;

    Music* attached_ = nullptr;
};

#pragma once
#include <array>
#include <vector>
#include <mutex>
#include <chrono>
#include "raylib.h"

// Captures live samples from a playing Music stream via raylib's audio
// stream processor callback (runs on raylib's own audio-mixing thread),
// then performs windowing + FFT to produce a magnitude spectrum, coarse
// frequency bands (bass/mid/treble), and simple energy-based beat
// detection. Update() runs on a dedicated audio-update thread (see
// App's AudioThread) rather than inline in the main render loop -- so it
// keeps advancing (music keeps reacting) even while the main thread is
// blocked inside an interactive window resize/move, which on Windows
// blocks the OS message pump for the whole drag. Every public getter is
// therefore a genuine cross-thread read and is safe to call from any
// thread at any time -- see snapshot_/snapshotMutex_.
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

    // Call regularly (any single thread -- see the class comment; today
    // that's App's dedicated audio-update thread, not the main loop).
    void Update();

    // Spectrum/Bars are read today only from the same thread that calls
    // Update() (the Spectrum Ring visualizer, currently disabled --
    // see App.cpp's kAudioEnabled-era comment); unlike the scalar
    // getters below they are not snapshot-published, so treat them as
    // same-thread-as-Update() only unless/until something cross-thread
    // needs them too.
    const std::array<float, kSpectrumBins>& Spectrum() const { return smoothedSpectrum_; }
    const std::array<float, kNumBars>& Bars() const { return bars_; }

    // Cross-thread-safe: each call takes a brief lock on the latest
    // published AudioSnapshot (see Update()'s comment). Called every
    // frame from the main thread while Update() runs on the audio
    // thread, so this must never be a plain unsynchronized field read.
    float Bass() const;
    float Mid() const;
    float Treble() const;
    float Energy() const;
    bool BeatTriggered() const;
    float BeatIntensity() const;

    // Normalized companions to Bass()/Mid()/Treble()/Energy() above, each
    // in [0,1] -- those raw band averages are un-normalized FFT magnitudes
    // (typically 0.0-0.15), which is why a hand-tuned "hue += Bass() * 15"
    // style scale barely moves anything: the raw value is too small for
    // its own scale to matter. These divide by a per-band rolling peak
    // (a decaying max, not a fixed calibration constant) instead, so a
    // loud passage in *this* track reads as ~1.0 regardless of the track's
    // absolute loudness or mix. Existing callers of the raw getters above
    // are deliberately untouched -- only new audio-reactive color code
    // consumes these.
    float BassLevel() const;
    float MidLevel() const;
    float TrebleLevel() const;
    float EnergyLevel() const;

private:
    static void AudioCallback(void* buffer, unsigned int frames);
    void ProcessCallback(const float* buffer, unsigned int frames);

    static AudioAnalyzer* sActive;

    // Ring buffer of mono samples, filled on raylib's own audio-mixing
    // thread (the AttachAudioStreamProcessor callback), drained by
    // whichever thread calls Update() (see the class comment). Protected
    // by mutex_ -- distinct from snapshotMutex_ below so publishing a
    // finished snapshot never contends with the audio-mixing thread's
    // per-buffer sample writes.
    std::mutex mutex_;
    std::vector<float> ring_;
    size_t ringCapacity_ = 0;
    size_t writePos_ = 0;
    size_t totalWritten_ = 0;

    std::array<float, kFFTSize> window_{};
    std::array<float, kSpectrumBins> spectrum_{};
    std::array<float, kSpectrumBins> smoothedSpectrum_{};
    std::array<float, kNumBars> bars_{};

    // The whole set of derived scalars, published as one unit at the end
    // of each Update() so a reader on another thread never sees a torn
    // combination (e.g. bass_ from one update tick paired with
    // beatTriggered_ from the next) -- plain per-field floats/bools, as
    // this used to be, are a real data race once Update() no longer runs
    // on the same thread as the getters' callers.
    struct AudioSnapshot {
        float bass = 0.0f, mid = 0.0f, treble = 0.0f, energy = 0.0f;
        bool beatTriggered = false;
        float beatIntensity = 0.0f;
        // Rolling-peak-normalized companions -- see BassLevel() etc.'s
        // comment in the header for why these exist alongside the raw
        // fields above rather than replacing them.
        float bassLevel = 0.0f, midLevel = 0.0f, trebleLevel = 0.0f, energyLevel = 0.0f;
    };
    mutable std::mutex snapshotMutex_;
    AudioSnapshot snapshot_;

    // Decaying peak trackers for the normalization above -- one per band,
    // time-based decay (not a fixed sample window) for the same reason
    // avgBassEnergy_'s lag below is time-based: Update()'s calling cadence
    // is a dedicated ~120Hz thread, not tied to the render frame rate.
    float bassPeak_ = 0.0f, midPeak_ = 0.0f, treblePeak_ = 0.0f, energyPeak_ = 0.0f;

    // Rolling bass average the beat detector compares against, and how
    // long Update() has been running -- both time-based (a first-order
    // lag / elapsed-seconds gate) rather than a fixed sample count, so
    // behavior doesn't change if Update()'s calling cadence ever changes
    // (it moved from "once per render frame" to a dedicated ~120Hz audio
    // thread -- see the class comment -- without this these would have
    // silently retuned the beat detector's sensitivity).
    float avgBassEnergy_ = 0.0f;
    float warmupElapsed_ = 0.0f;
    float beatCooldown_ = 0.0f;
    std::chrono::steady_clock::time_point lastUpdateTime_{};
    bool hasLastUpdateTime_ = false;

    Music* attached_ = nullptr;
};

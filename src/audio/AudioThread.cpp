#include "AudioThread.h"
#include "AudioAnalyzer.h"
#include <chrono>

AudioThread::~AudioThread() {
    Stop();
}

void AudioThread::Start(Music& music, AudioAnalyzer& analyzer) {
    if (running_.load()) return; // already started -- see the header's comment on why this is a no-op, not an error

    music_ = &music;
    analyzer_ = &analyzer;
    running_.store(true);
    thread_ = std::thread(&AudioThread::Run, this);
}

void AudioThread::Stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void AudioThread::Run() {
    // ~120 Hz: comfortably faster than the audio buffer needs refilling
    // (UpdateMusicStream/AudioAnalyzer::Update are both lightweight --
    // see AudioAnalyzer::Update's own comment on why it no longer trusts
    // GetFrameTime(), which only reflects the *main* render loop's
    // cadence, not this thread's).
    constexpr auto kTickInterval = std::chrono::milliseconds(8);

    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lock(musicMutex_);
            UpdateMusicStream(*music_);
        }
        analyzer_->Update(); // internally mutex-protected for its own ring buffer -- see AudioAnalyzer::mutex_

        std::this_thread::sleep_for(kTickInterval);
    }
}

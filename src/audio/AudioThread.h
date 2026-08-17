#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include "raylib.h"

class AudioAnalyzer;

// Owns a dedicated background thread that keeps a playing Music stream's
// buffer refilled (UpdateMusicStream) and feeds the AudioAnalyzer, both
// independent of the main render loop. Motivation: on Windows,
// interactively resizing/moving the window blocks the main thread's
// message pump for the whole drag (a documented raylib/GLFW limitation,
// not something this project can fix for rendering -- see the project
// history), which would otherwise silently stall audio (buffer
// underruns/pops) right along with rendering. This thread has nothing to
// do with rendering or the GL context at all, so it keeps ticking
// through that stall -- music keeps playing and reacting even while the
// visual frame is frozen mid-drag.
//
// `music` becomes genuinely cross-thread state once this thread is
// started: every touch -- this thread's periodic UpdateMusicStream, and
// the main thread's Play/Pause/Resume/SetVolume/track-switch Load/Unload
// (see App::PlayTrack et al.) -- must go through MusicMutex(), held only
// for the duration of each individual raylib call, so main-thread button
// clicks essentially never contend with it.
class AudioThread {
public:
    ~AudioThread();

    // Starts the thread, ticking `music`/`analyzer` at ~120 Hz. `music`
    // must already hold a valid, loaded, playable stream (App calls this
    // only after its first successful PlayTrack) and must outlive the
    // AudioThread. Safe to call at most once per instance -- App starts
    // this lazily, the first time audio is enabled, and never stops/
    // restarts it for the rest of the run (see App::ApplyAudioSettings);
    // a second Start() call is a no-op.
    void Start(Music& music, AudioAnalyzer& analyzer);

    // Stops and joins the thread. Safe to call even if never started, or
    // more than once. Must run before the Music/AudioAnalyzer it was
    // started with are torn down -- see App::Shutdown's ordering.
    void Stop();

    // The mutex guarding every touch of the Music this thread updates --
    // see the class comment. Callers on the main thread must lock this
    // for the duration of their own raylib Music calls (Play/Pause/
    // Resume/SetVolume/Load/Unload) once the thread has been started.
    std::mutex& MusicMutex() { return musicMutex_; }

private:
    void Run();

    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::mutex musicMutex_;
    Music* music_ = nullptr;
    AudioAnalyzer* analyzer_ = nullptr;
};

#pragma once
#include <vector>

// Synthesizes a short procedural track (kick + bass + arpeggio + hi-hats)
// as an in-memory WAV byte buffer, so the engine has something rhythmic
// and spectrally varied to visualize even with no audio file supplied.
// The returned buffer must stay alive for the lifetime of the Music
// loaded from it (LoadMusicStreamFromMemory keeps a pointer into it).
std::vector<unsigned char> GenerateDemoTrackWav(float durationSeconds = 90.0f, int sampleRate = 44100);

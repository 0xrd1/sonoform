#include "DemoTrack.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <algorithm>

namespace {

void PushU32(std::vector<unsigned char>& buf, uint32_t v) {
    buf.push_back(static_cast<unsigned char>(v & 0xFF));
    buf.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
}

void PushU16(std::vector<unsigned char>& buf, uint16_t v) {
    buf.push_back(static_cast<unsigned char>(v & 0xFF));
    buf.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

void PushTag(std::vector<unsigned char>& buf, const char* tag) {
    buf.insert(buf.end(), tag, tag + 4);
}

float TriangleWave(float phase) {
    // phase in [0,1)
    return 4.0f * std::fabs(phase - std::floor(phase + 0.5f)) - 1.0f;
}

} // namespace

std::vector<unsigned char> GenerateDemoTrackWav(float durationSeconds, int sampleRate) {
    const int numSamples = static_cast<int>(durationSeconds * sampleRate);
    std::vector<float> mix(numSamples, 0.0f);

    const float bpm = 120.0f;
    const float beatDur = 60.0f / bpm; // 0.5s at 120 BPM

    // Chord progression (root frequencies, Hz) - Am, F, C, G style loop.
    const float chordRoots[] = { 55.00f, 43.65f, 65.41f, 49.00f }; // A1, F1, C2, G1
    const int chordCount = 4;
    const float chordDur = beatDur * 4.0f; // each chord holds 4 beats

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

    // --- Bass layer (continuous sine tied to chord progression) ---
    for (int i = 0; i < numSamples; i++) {
        float t = static_cast<float>(i) / sampleRate;
        int chordIdx = static_cast<int>(std::fmod(t / chordDur, static_cast<float>(chordCount)));
        float root = chordRoots[chordIdx % chordCount];
        float phase = t * root;
        float bass = std::sin(2.0f * 3.14159265f * phase) * 0.35f;
        bass += std::sin(2.0f * 3.14159265f * phase * 0.5f) * 0.15f; // sub octave
        mix[i] += bass;
    }

    // --- Kick drum (short pitched thump on every beat) ---
    {
        float t = 0.0f;
        while (t < durationSeconds) {
            int startSample = static_cast<int>(t * sampleRate);
            int decaySamples = static_cast<int>(0.18f * sampleRate);
            for (int s = 0; s < decaySamples && (startSample + s) < numSamples; s++) {
                float lt = static_cast<float>(s) / sampleRate;
                float freq = 90.0f * std::exp(-lt * 22.0f) + 40.0f; // pitch drop
                float env = std::exp(-lt * 16.0f);
                float sample = std::sin(2.0f * 3.14159265f * freq * lt) * env * 0.9f;
                mix[startSample + s] += sample;
            }
            t += beatDur;
        }
    }

    // --- Hi-hats (filtered-ish noise burst on off-beats) ---
    {
        float t = beatDur * 0.5f;
        while (t < durationSeconds) {
            int startSample = static_cast<int>(t * sampleRate);
            int decaySamples = static_cast<int>(0.06f * sampleRate);
            float prev = 0.0f;
            for (int s = 0; s < decaySamples && (startSample + s) < numSamples; s++) {
                float lt = static_cast<float>(s) / sampleRate;
                float env = std::exp(-lt * 60.0f);
                float n = noiseDist(rng);
                // crude high-pass: emphasize change between samples
                float hp = n - prev;
                prev = n;
                mix[startSample + s] += hp * env * 0.25f;
            }
            t += beatDur;
        }
    }

    // --- Lead arpeggio (steps through chord tones every 1/4 beat) ---
    {
        float stepDur = beatDur * 0.25f;
        float t = 0.0f;
        int step = 0;
        while (t < durationSeconds) {
            int chordIdx = static_cast<int>(std::fmod(t / chordDur, static_cast<float>(chordCount)));
            float root = chordRoots[chordIdx % chordCount] * 4.0f; // up two octaves
            static const float ratios[4] = { 1.0f, 1.25f, 1.5f, 2.0f }; // maj triad + octave
            float freq = root * ratios[step % 4];

            int startSample = static_cast<int>(t * sampleRate);
            int noteSamples = static_cast<int>(stepDur * sampleRate * 0.9f);
            for (int s = 0; s < noteSamples && (startSample + s) < numSamples; s++) {
                float lt = static_cast<float>(s) / sampleRate;
                float env = std::exp(-lt * 10.0f);
                float sample = TriangleWave(lt * freq) * env * 0.18f;
                mix[startSample + s] += sample;
            }
            t += stepDur;
            step++;
        }
    }

    // --- Normalize / soft-clip to avoid harsh clipping ---
    float peak = 0.0001f;
    for (float v : mix) peak = std::max(peak, std::fabs(v));
    float norm = std::min(1.0f, 0.95f / peak);
    for (int i = 0; i < numSamples; i++) {
        float v = mix[i] * norm;
        mix[i] = std::tanh(v * 1.4f); // gentle soft clip / warmth
    }

    // --- Encode as 16-bit PCM mono WAV ---
    const int channels = 1;
    const int bitsPerSample = 16;
    const uint32_t dataSize = static_cast<uint32_t>(numSamples) * channels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));

    std::vector<unsigned char> wav;
    wav.reserve(44 + dataSize);

    PushTag(wav, "RIFF");
    PushU32(wav, 36 + dataSize);
    PushTag(wav, "WAVE");

    PushTag(wav, "fmt ");
    PushU32(wav, 16);
    PushU16(wav, 1); // PCM
    PushU16(wav, static_cast<uint16_t>(channels));
    PushU32(wav, static_cast<uint32_t>(sampleRate));
    PushU32(wav, byteRate);
    PushU16(wav, blockAlign);
    PushU16(wav, static_cast<uint16_t>(bitsPerSample));

    PushTag(wav, "data");
    PushU32(wav, dataSize);

    for (int i = 0; i < numSamples; i++) {
        float v = std::clamp(mix[i], -1.0f, 1.0f);
        int16_t s = static_cast<int16_t>(v * 32767.0f);
        wav.push_back(static_cast<unsigned char>(s & 0xFF));
        wav.push_back(static_cast<unsigned char>((s >> 8) & 0xFF));
    }

    return wav;
}

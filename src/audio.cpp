#include "audio.h"
#include "platform.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace sfx {
namespace {

constexpr float kSr = 48000.0f;
constexpr float kTwoPi = 6.28318530718f;

struct Voice {
    float freq = 520.0f;
    float delay = 0.0f;
    float dur = 0.12f;
    float t = 0.0f;
    float amp = 0.10f;
    float pan = 0.0f;
};

std::mutex g_mtx;
std::vector<Voice> g_voices;
std::atomic<bool> g_enabled{true};

void push(float freq, float delay, float dur, float amp, float pan = 0.0f) {
    g_voices.push_back(Voice{freq, delay, dur, 0.0f, amp, pan});
}

void mix(float *out, int frames, void *) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_voices.empty()) return;

    const float dt = 1.0f / kSr;
    for (int i = 0; i < frames; ++i) {
        float L = 0.0f, R = 0.0f;
        for (auto &v : g_voices) {
            v.t += dt;
            if (v.t < v.delay) continue;
            float lt = v.t - v.delay;
            if (lt >= v.dur) continue;
            // Soft glass droplet envelope
            float env = std::min(1.0f, lt / 0.005f) * std::exp(-lt * (6.2f / std::max(0.04f, v.dur)));
            float ph = kTwoPi * v.freq * lt;
            float s = std::sin(ph) + 0.16f * std::sin(ph * 2.0f) * std::exp(-lt * 16.0f);
            float sample = s * v.amp * env;
            L += sample * (0.5f + 0.5f * (1.0f - v.pan) * 0.5f);
            R += sample * (0.5f + 0.5f * (1.0f + v.pan) * 0.5f);
        }
        out[i * 2 + 0] = std::tanh(L * 0.85f);
        out[i * 2 + 1] = std::tanh(R * 0.85f);
    }

    g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(),
                                  [](const Voice &v) { return v.t >= v.delay + v.dur; }),
                   g_voices.end());
}

}  // namespace

void init() { plat::audioStart(mix, nullptr); }
void shutdown() { plat::audioStop(); }
void setEnabled(bool en) { g_enabled.store(en, std::memory_order_relaxed); }
bool enabled() { return g_enabled.load(std::memory_order_relaxed); }

void play(Sound s) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    switch (s) {
        case Sound::Move:
            push(640.0f, 0.00f, 0.06f, 0.065f, 0.0f);
            break;
        case Sound::Tab:
            push(523.25f, 0.00f, 0.09f, 0.075f, -0.15f);
            push(659.25f, 0.03f, 0.11f, 0.070f,  0.15f);
            break;
        case Sound::Launch:
            push(440.00f, 0.00f, 0.24f, 0.12f, -0.2f);
            push(554.37f, 0.05f, 0.28f, 0.12f,  0.0f);
            push(659.25f, 0.10f, 0.36f, 0.13f,  0.2f);
            push(880.00f, 0.16f, 0.45f, 0.10f,  0.0f);
            break;
        case Sound::Fav:
            push(783.99f, 0.00f, 0.12f, 0.09f, -0.1f);
            push(1046.50f, 0.05f, 0.18f, 0.09f,  0.1f);
            break;
        case Sound::Notify:
            push(587.33f, 0.00f, 0.18f, 0.11f, -0.1f);
            push(880.00f, 0.07f, 0.26f, 0.11f,  0.1f);
            break;
        case Sound::Back:
            push(480.0f, 0.00f, 0.08f, 0.07f, 0.0f);
            break;
    }
}

}  // namespace sfx

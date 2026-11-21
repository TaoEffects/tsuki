#pragma once

#include <cstdint>
#include <string>

namespace plat {

constexpr int kW = 1920;
constexpr int kH = 1080;

struct Input {
    float mx = kW * 0.5f;
    float my = kH * 0.5f;
    float dx = 0.0f;
    float dy = 0.0f;

    bool pointerVisible = false;  // shown when using mouse/nmouse, hidden when navigating via gamepad
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    bool touchActive = false;
    float wheelX = 0.0f;
    float wheelY = 0.0f;

    // Navigation (with smooth auto-repeat on D-pad / Left stick)
    bool navLeft = false;
    bool navRight = false;
    bool navUp = false;
    bool navDown = false;

    bool btnA = false;      // Launch / Confirm
    bool btnB = false;      // Back
    bool btnX = false;      // Search
    bool btnY = false;      // Favorite
    bool btnL = false;      // Prev tab
    bool btnR = false;      // Next tab
    bool btnZL = false;     // Filter favorites / Quick jump
    bool btnZR = false;     // All apps / Sort / New folder
    bool btnMinus = false;  // Toggle FTP
    bool btnPlus = false;   // Refresh SD

    bool quitRequested = false;
};

struct StatusInfo {
    int hour = 0;
    int minute = 0;
    int batteryPercent = 100;
    bool charging = false;
    uint64_t sdFreeBytes = 0;
    uint64_t sdTotalBytes = 0;
    std::string ipAddress;
};

using AudioMixFn = void (*)(float *interleavedStereo, int frames, void *user);

bool init();
void shutdown();

void pollInput(Input &out);
void swapBuffers();

double nowSeconds();
StatusInfo queryStatus();

bool audioStart(AudioMixFn fn, void *user);
void audioStop();

bool showKeyboard(const char *header, const std::string &initial, std::string &out, int maxLen = 128);
bool launchNro(const std::string &path);
// Returns pointer to Switch 2's built-in standard shared font TTF (for Japanese / CJK / extended Unicode fallback)
const uint8_t *systemFontBytes();

}  // namespace plat

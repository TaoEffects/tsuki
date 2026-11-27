#include "platform.h"

#include <switch.h>
#include <switch2/services/nmouse.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>

namespace plat {
namespace {

EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLSurface g_surf = EGL_NO_SURFACE;
EGLContext g_ctx = EGL_NO_CONTEXT;

bool g_hidInit = false;
bool g_nmouseInit = false;
bool g_psmInit = false;
bool g_nifmInit = false;
bool g_sockInit = false;
bool g_plInit = false;
PlFontData g_sharedFont{};
bool g_hasSharedFont = false;
PadState g_pad{};

float g_mx = kW * 0.5f;
float g_my = kH * 0.5f;
float g_prevMx = kW * 0.5f;
float g_prevMy = kH * 0.5f;
bool g_prevLeft = false;
bool g_pointerVis = false;
uint64_t g_nmouseSampling = 0;
uint32_t g_nmouseBtns = 0;
double g_prevPollTime = 0.0;

// Smooth directional auto-repeat for D-pad & Left stick
struct RepeatDir {
    bool held = false;
    float timer = 0.0f;
    bool update(bool nowHeld, float dt) {
        if (!nowHeld) { held = false; timer = 0.0f; return false; }
        if (!held) { held = true; timer = 0.28f; return true; }
        timer -= dt;
        if (timer <= 0.0f) { timer = 0.095f; return true; }
        return false;
    }
};
RepeatDir g_repL, g_repR, g_repU, g_repD;

// Audio thread
std::atomic<bool> g_audioRunning{false};
pthread_t g_audioThread{};
bool g_audioThreadStarted = false;
AudioMixFn g_audioFn = nullptr;
void *g_audioUser = nullptr;

void fillAudioBuf(AudioOutBuffer *buf, std::vector<float> &scratch, int frames) {
    const size_t samples = static_cast<size_t>(frames) * 2;
    scratch.assign(samples, 0.0f);
    if (g_audioFn) g_audioFn(scratch.data(), frames, g_audioUser);
    int16_t *dst = static_cast<int16_t *>(buf->buffer);
    for (size_t i = 0; i < samples; ++i) {
        float v = std::clamp(scratch[i], -1.0f, 1.0f);
        dst[i] = static_cast<int16_t>(v * 32767.0f);
    }
    buf->data_size = samples * sizeof(int16_t);
}

void *audioMain(void *) {
    constexpr size_t kAlign = 0x1000;
    constexpr size_t kBufSize = 0x4000;
    constexpr int kFrames = 512;

    void *mem[2] = {aligned_alloc(kAlign, kBufSize), aligned_alloc(kAlign, kBufSize)};
    if (!mem[0] || !mem[1]) {
        free(mem[0]);
        free(mem[1]);
        return nullptr;
    }
    std::memset(mem[0], 0, kBufSize);
    std::memset(mem[1], 0, kBufSize);

    AudioOutBuffer bufs[2]{};
    for (int i = 0; i < 2; ++i) {
        bufs[i].buffer = mem[i];
        bufs[i].buffer_size = kBufSize;
    }
    std::vector<float> scratch(kFrames * 2, 0.0f);
    for (int i = 0; i < 2; ++i) {
        fillAudioBuf(&bufs[i], scratch, kFrames);
        audoutAppendAudioOutBuffer(&bufs[i]);
    }
    while (g_audioRunning.load(std::memory_order_acquire)) {
        AudioOutBuffer *rel = nullptr;
        u32 count = 0;
        Result rc = audoutWaitPlayFinish(&rel, &count, 50000000ULL);
        if (R_SUCCEEDED(rc) && count > 0 && rel) {
            fillAudioBuf(rel, scratch, kFrames);
            audoutAppendAudioOutBuffer(rel);
        }
    }
    audoutStopAudioOut();
    audoutExit();
    free(mem[0]);
    free(mem[1]);
    return nullptr;
}

}  // namespace

double nowSeconds() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

bool init() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/tsuki", 0777);

    if (R_SUCCEEDED(psmInitialize())) g_psmInit = true;
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) g_nifmInit = true;
    if (R_SUCCEEDED(socketInitializeDefault())) g_sockInit = true;
    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        g_plInit = true;
        if (R_SUCCEEDED(plGetSharedFontByType(&g_sharedFont, PlSharedFontType_Standard)) && g_sharedFont.address) {
            g_hasSharedFont = true;
        }
    }

    if (R_SUCCEEDED(hidInitialize())) {
        g_hidInit = true;
        hidInitializeTouchScreen();
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&g_pad);

        if (R_SUCCEEDED(hidInitializeNmouse(nullptr, 0, kW, kH, HidNmousePointerType_Default))) {
            g_nmouseInit = true;
            // Default: USB mouse only (Right Joy-Con mouse is opt-in via Settings to avoid button glitches in air)
            hidSetMergedNmouseSourceTypes(HidNmouseType_StandardMouse);
            HidNmouseButtonConfigJoyRightJcms rcfg{{1, 2, 0, 0, 0, 0, 3, 0}};
            hidSetNmouseButtonConfigJoyRightJcms(&rcfg);
        }
    }

    NWindow *nw = nwindowGetDefault();
    if (!nw) return false;
    nwindowSetDimensions(nw, kW, kH);
    nwindowSetFrameRates(nw, 0, 0, true);  // Native 1080p @ 120 Hz + VRR

    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, nullptr, nullptr)) return false;
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint cfgAttr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, cfgAttr, &cfg, 1, &n) || n < 1) return false;

    g_surf = eglCreateWindowSurface(g_dpy, cfg, nw, nullptr);
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (g_surf == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT) return false;
    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) return false;
    eglSwapInterval(g_dpy, 1);

    g_prevPollTime = nowSeconds();
    return true;
}

void shutdown() {
    audioStop();
    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
        if (g_surf != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_surf);
        eglTerminate(g_dpy);
        g_dpy = EGL_NO_DISPLAY;
    }
    if (g_nmouseInit) { hidFinalizeNmouse(); g_nmouseInit = false; }
    if (g_hidInit) { hidExit(); g_hidInit = false; }
    if (g_plInit) { plExit(); g_plInit = false; g_hasSharedFont = false; }
    if (g_sockInit) { socketExit(); g_sockInit = false; }
    if (g_nifmInit) { nifmExit(); g_nifmInit = false; }
    if (g_psmInit) { psmExit(); g_psmInit = false; }
}

void pollInput(Input &out) {
    out = Input{};
    if (!appletMainLoop()) {
        out.quitRequested = true;
        return;
    }

    const double now = nowSeconds();
    const float dt = std::clamp(static_cast<float>(now - g_prevPollTime), 0.001f, 0.05f);
    g_prevPollTime = now;

    g_prevMx = g_mx;
    g_prevMy = g_my;

    if (g_hidInit) {
        padUpdate(&g_pad);
        const u64 k = padGetButtons(&g_pad);
        const u64 down = padGetButtonsDown(&g_pad);

        const HidAnalogStickState ls = padGetStickPos(&g_pad, 0);
        const HidAnalogStickState rs = padGetStickPos(&g_pad, 1);
        float lx = ls.x / 32767.0f;
        float ly = ls.y / 32767.0f;
        float rx = rs.x / 32767.0f;
        float ry = rs.y / 32767.0f;

        bool hL = (k & HidNpadButton_Left) != 0 || lx < -0.45f;
        bool hR = (k & HidNpadButton_Right) != 0 || lx > 0.45f;
        bool hU = (k & HidNpadButton_Up) != 0 || ly > 0.45f;
        bool hD = (k & HidNpadButton_Down) != 0 || ly < -0.45f;

        out.navLeft = g_repL.update(hL, dt);
        out.navRight = g_repR.update(hR, dt);
        out.navUp = g_repU.update(hU, dt);
        out.navDown = g_repD.update(hD, dt);

        if (std::fabs(rx) > 0.20f) out.wheelX += rx * 14.0f * dt;
        if (std::fabs(ry) > 0.20f) out.wheelY += ry * 14.0f * dt;

        out.btnA = (down & HidNpadButton_A) != 0;
        out.btnB = (down & HidNpadButton_B) != 0;
        out.btnX = (down & HidNpadButton_X) != 0;
        out.btnY = (down & HidNpadButton_Y) != 0;
        out.btnL = (down & HidNpadButton_L) != 0;
        out.btnR = (down & HidNpadButton_R) != 0;
        out.btnZL = (down & HidNpadButton_ZL) != 0;
        out.btnZR = (down & HidNpadButton_ZR) != 0;
        out.btnMinus = (down & HidNpadButton_Minus) != 0;
        out.btnPlus = (down & HidNpadButton_Plus) != 0;

        if (down != 0 || hL || hR || hU || hD) {
            g_pointerVis = false;
        }
    }

    // Joy-Con 2 Mouse Mode & USB Mouse
    if (g_nmouseInit) {
        HidNmouseHandle h{};
        if (R_SUCCEEDED(hidGetMergedNmouseHandle(&h))) {
            HidNmouseStateWithDelta st{};
            size_t total = 0;
            if (R_SUCCEEDED(hidGetNmouseStatesWithDelta(h, &st, 1, &total)) && total > 0 &&
                st.sampling_number != g_nmouseSampling) {
                g_nmouseSampling = st.sampling_number;
                if (st.delta_x != 0 || st.delta_y != 0) {
                    g_mx += static_cast<float>(st.delta_x);
                    g_my += static_cast<float>(st.delta_y);
                    g_pointerVis = true;
                }
                g_nmouseBtns = st.buttons & 0x7u;
                u32 types = 0;
                if (R_SUCCEEDED(hidGetNmouseTypeSet(h, &types)) && (types & HidNmouseType_StandardMouse)) {
                    out.wheelY += static_cast<float>(st.wheel_delta_y);
                }
            }
        }
    }

    // Touch screen (1280x720 -> 1920x1080)
    bool touchClick = false;
    if (g_hidInit) {
        HidTouchScreenState ts{};
        if (hidGetTouchScreenStates(&ts, 1) > 0 && ts.count > 0) {
            g_mx = static_cast<float>(ts.touches[0].x) * (static_cast<float>(kW) / 1280.0f);
            g_my = static_cast<float>(ts.touches[0].y) * (static_cast<float>(kH) / 720.0f);
            touchClick = true;
            out.touchActive = true;
            g_pointerVis = false;
        }
    }

    g_mx = std::clamp(g_mx, 0.0f, static_cast<float>(kW - 1));
    g_my = std::clamp(g_my, 0.0f, static_cast<float>(kH - 1));

    bool leftDown = ((g_nmouseBtns & 1u) != 0) || touchClick;
    out.mx = g_mx;
    out.my = g_my;
    out.dx = g_mx - g_prevMx;
    out.dy = g_my - g_prevMy;
    out.pointerVisible = g_pointerVis;
    out.leftDown = leftDown;
    out.leftPressed = leftDown && !g_prevLeft;
    out.leftReleased = !leftDown && g_prevLeft;
    g_prevLeft = leftDown;
}

void swapBuffers() {
    eglSwapBuffers(g_dpy, g_surf);
}

StatusInfo queryStatus() {
    StatusInfo s;
    time_t t = time(nullptr);
    struct tm lt{};
    localtime_r(&t, &lt);
    s.hour = lt.tm_hour;
    s.minute = lt.tm_min;

    if (g_psmInit) {
        u32 pct = 100;
        PsmChargerType ct = PsmChargerType_Unconnected;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) s.batteryPercent = static_cast<int>(pct);
        if (R_SUCCEEDED(psmGetChargerType(&ct))) s.charging = (ct != PsmChargerType_Unconnected);
    }

    struct statvfs sv{};
    if (statvfs("sdmc:/", &sv) == 0) {
        s.sdFreeBytes = static_cast<uint64_t>(sv.f_bavail) * static_cast<uint64_t>(sv.f_frsize);
        s.sdTotalBytes = static_cast<uint64_t>(sv.f_blocks) * static_cast<uint64_t>(sv.f_frsize);
    }

    if (g_nifmInit) {
        u32 ip = 0;
        if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                          (ip & 0xFFu), ((ip >> 8) & 0xFFu), ((ip >> 16) & 0xFFu), ((ip >> 24) & 0xFFu));
            s.ipAddress = buf;
        }
    }
    return s;
}

bool audioStart(AudioMixFn fn, void *user) {
    audioStop();
    if (R_FAILED(audoutInitialize())) return false;
    if (R_FAILED(audoutStartAudioOut())) { audoutExit(); return false; }
    g_audioFn = fn;
    g_audioUser = user;
    g_audioRunning.store(true, std::memory_order_release);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    if (pthread_create(&g_audioThread, &attr, audioMain, nullptr) != 0) {
        pthread_attr_destroy(&attr);
        g_audioRunning.store(false, std::memory_order_release);
        audoutStopAudioOut();
        audoutExit();
        return false;
    }
    pthread_attr_destroy(&attr);
    g_audioThreadStarted = true;
    return true;
}

void audioStop() {
    if (g_audioThreadStarted) {
        g_audioRunning.store(false, std::memory_order_release);
        pthread_join(g_audioThread, nullptr);
        g_audioThreadStarted = false;
    }
}

bool showKeyboard(const char *header, const std::string &initial, std::string &out, int maxLen) {
    SwkbdConfig kbd{};
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    if (header && *header) swkbdConfigSetHeaderText(&kbd, header);
    if (!initial.empty()) swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, std::clamp(maxLen, 16, 256));
    std::vector<char> buf(static_cast<size_t>(std::clamp(maxLen, 16, 256)) + 16, '\0');
    Result rc = swkbdShow(&kbd, buf.data(), buf.size() - 1);
    swkbdClose(&kbd);
    if (R_SUCCEEDED(rc)) {
        out = buf.data();
        return true;
    }
    return false;
}

bool launchNro(const std::string &path) {
    if (!envHasNextLoad()) return false;
    std::string argv = "\"" + path + "\"";
    return R_SUCCEEDED(envSetNextLoad(path.c_str(), argv.c_str()));
}

void setJoyconMouseEnabled(bool enabled) {
    if (!g_nmouseInit) return;
    if (enabled) {
        hidSetMergedNmouseSourceTypes(HidNmouseType_JoyRight | HidNmouseType_StandardMouse);
    } else {
        hidSetMergedNmouseSourceTypes(HidNmouseType_StandardMouse);
        g_pointerVis = false;
        g_nmouseBtns = 0;
    }
}

const uint8_t *systemFontBytes() {
    return g_hasSharedFont ? static_cast<const uint8_t *>(g_sharedFont.address) : nullptr;
}

}  // namespace plat

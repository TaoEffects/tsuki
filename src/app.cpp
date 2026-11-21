#include "app.h"
#include "audio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace tsuki {
namespace {

float easeOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

std::string toLowerUtf8(const std::string &s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'A' && c <= 'Z') {
            out[i] = static_cast<char>(c + 32);
        } else if (c == 0xC3 && i + 1 < out.size()) {
            unsigned char n = static_cast<unsigned char>(out[i + 1]);
            if (n >= 0x80 && n <= 0x9E) out[i + 1] = static_cast<char>(n + 0x20);
            ++i;
        }
    }
    return out;
}

struct ThemeSpec {
    i18n::Key nameKey;
    gfx::Color bgTop;
    gfx::Color bgBot;
    gfx::Color accent;
    gfx::Color accentDeep;
    gfx::Color glassTint;
    gfx::Color cardFill;
    float orbAlpha;
};

const ThemeSpec kThemes[6] = {
    // 0: Framboise (Yoru signature rose/plum)
    {i18n::Key::ThemeFramboise, gfx::hex(0x26111C), gfx::hex(0x411D30), gfx::hex(0xF5A4C4), gfx::hex(0xB83B6E),
     gfx::hex(0x4A2637, 0.44f), gfx::hex(0x160A10, 0.56f), 0.34f},
    // 1: Nuit
    {i18n::Key::ThemeNuit, gfx::hex(0x0E1211), gfx::hex(0x19211D), gfx::hex(0xB7D9A8), gfx::hex(0x4A6B46),
     gfx::hex(0x202924, 0.46f), gfx::hex(0x0A0D0C, 0.58f), 0.28f},
    // 2: Iris
    {i18n::Key::ThemeIris, gfx::hex(0x1B122A), gfx::hex(0x361F54), gfx::hex(0xD8B4FE), gfx::hex(0x8B5CF6),
     gfx::hex(0x33204C, 0.44f), gfx::hex(0x120B1C, 0.56f), 0.34f},
    // 3: Lagon
    {i18n::Key::ThemeLagon, gfx::hex(0x0D1E26), gfx::hex(0x143D49), gfx::hex(0x7DD3FC), gfx::hex(0x0D9488),
     gfx::hex(0x163642, 0.44f), gfx::hex(0x09151B, 0.56f), 0.34f},
    // 4: Pêche
    {i18n::Key::ThemePeche, gfx::hex(0x2A1718), gfx::hex(0x492522), gfx::hex(0xFDBA74), gfx::hex(0xE06D53),
     gfx::hex(0x4D2A26, 0.44f), gfx::hex(0x1A0E0E, 0.56f), 0.34f},
    // 5: OLED
    {i18n::Key::ThemeOled, gfx::hex(0x000000), gfx::hex(0x040204), gfx::hex(0xF5A4C4), gfx::hex(0x9D3662),
     gfx::hex(0x1A1116, 0.52f), gfx::hex(0x0D090B, 0.72f), 0.20f},
};

constexpr float kMarginX = 56.0f;

// Unified Yoru-style Top Liquid Glass Pill Bar
constexpr float kBarX = 48.0f;
constexpr float kBarY = 24.0f;
constexpr float kBarW = 1824.0f;
constexpr float kBarH = 76.0f;

// 5 Tabs centered inside unified bar, flanked by L / R pills
constexpr float kTabW[5] = {178.0f, 152.0f, 186.0f, 148.0f, 198.0f};
constexpr float kTabGap = 8.0f;
constexpr float kTabsX0 = 446.0f;

// Home Unified Hero Banner
constexpr float kHeroX = 56.0f;
constexpr float kHeroY = 124.0f;
constexpr float kHeroW = 1808.0f;
constexpr float kHeroH = 400.0f;

// Home Horizontal Rail below Library header
constexpr float kCardW = 326.0f;
constexpr float kCardH = 332.0f;
constexpr float kCardGap = 28.0f;
constexpr float kCardStep = kCardW + kCardGap;
constexpr float kRailY = 660.0f;
constexpr float kRailPadX = 56.0f;

// Android-style App Drawer Vertical Grid (5 columns)
constexpr int kDrawerCols = 5;
constexpr float kDrawerX0 = 84.0f;
constexpr float kDrawerY0 = 244.0f;
constexpr float kDrawerCellW = 328.0f;
constexpr float kDrawerCellH = 314.0f;
constexpr float kDrawerGapX = 28.0f;
constexpr float kDrawerGapY = 24.0f;
constexpr float kDrawerStepX = kDrawerCellW + kDrawerGapX;
constexpr float kDrawerStepY = kDrawerCellH + kDrawerGapY;
constexpr float kDrawerViewH = plat::kH - kDrawerY0 - 24.0f;

// Settings list geometry (scrollable 13 rows!)
constexpr int kSettingsCount = 13;
constexpr float kSetListY = 278.0f;
constexpr float kSetListH = 716.0f;
constexpr float kSetRowH = 80.0f;
constexpr float kSetRowStep = 92.0f;

float tabStartX(int i) {
    float x = kTabsX0;
    for (int k = 0; k < i; ++k) x += kTabW[k] + kTabGap;
    return x;
}

}  // namespace

const char *App::T(i18n::Key k) const {
    return i18n::tr(cfg.lang, k);
}

std::string App::fmt(const std::string &s) const {
    // Japanese has no uppercase/lowercase distinction
    if (!cfg.lowercase || cfg.lang == static_cast<int>(i18n::Lang::Ja)) return s;
    return toLowerUtf8(s);
}

std::string App::fmtKey(i18n::Key k) const {
    return fmt(T(k));
}

void App::pushToast(const std::string &title, const std::string &body) {
    toast.title = title;
    toast.body = body;
    toast.timer = toast.dur;
}

void App::init() {
    nro::loadConfig(cfg);
    sfx::setEnabled(cfg.soundsEnabled);
    status = plat::queryStatus();
    nro::scanApps(allApps, cfg);
    rebuildVisible();
    fileBrowser.init("sdmc:/switch");

    tab = (cfg.startupTab == 1) ? Tab::Apps : Tab::Home;
    tabPillX = tabStartX(static_cast<int>(tab));
    tabPillW = kTabW[static_cast<int>(tab)];
    heroAnim = 0.0f;
    viewFade = 0.0f;
    listFade = 0.0f;

    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    curGlowA = th.accent;
    curGlowB = th.accentDeep;

    if (cfg.ftpAutoStart) {
        uint16_t port = cfg.ftpPort();
        if (ftpSrv.start(port)) {
            std::string ip = status.ipAddress.empty() ? "10.77.0.4" : status.ipAddress;
            pushToast(T(i18n::Key::FtpReady), "ftp://" + ip + ":" + std::to_string(port));
        }
    }
}

void App::shutdown() {
    fileBrowser.shutdown();
    ftpSrv.stop();
    nro::saveConfig(cfg);
    nro::freeApps(allApps);
}

void App::rebuildVisible(const std::string &keepPath) {
    prevHeroAppIdx = -1;
    visibleIdx.clear();
    std::string q = toLowerUtf8(searchFilter);

    for (int i = 0; i < static_cast<int>(allApps.size()); ++i) {
        const auto &a = allApps[static_cast<size_t>(i)];
        if (favOnly && !a.favorite) continue;
        if (!q.empty()) {
            if (toLowerUtf8(a.name).find(q) == std::string::npos &&
                toLowerUtf8(a.author).find(q) == std::string::npos) {
                continue;
            }
        }
        visibleIdx.push_back(i);
    }

    std::sort(visibleIdx.begin(), visibleIdx.end(), [&](int ia, int ib) {
        const auto &a = allApps[static_cast<size_t>(ia)];
        const auto &b = allApps[static_cast<size_t>(ib)];
        if (cfg.sortMode == 0) {
            if (a.favorite != b.favorite) return a.favorite > b.favorite;
            if (a.lastLaunched != b.lastLaunched) return a.lastLaunched > b.lastLaunched;
            if (a.mtime != b.mtime) return a.mtime > b.mtime;
            return toLowerUtf8(a.name) < toLowerUtf8(b.name);
        } else if (cfg.sortMode == 1) {
            return toLowerUtf8(a.name) < toLowerUtf8(b.name);
        } else {
            return a.fileSize > b.fileSize;
        }
    });

    if (!keepPath.empty()) {
        for (int i = 0; i < static_cast<int>(visibleIdx.size()); ++i) {
            if (allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(i)])].path == keepPath) {
                selectedVis = i;
                drawerVis = i;
                return;
            }
        }
    }
    int maxI = std::max(0, static_cast<int>(visibleIdx.size()) - 1);
    selectedVis = std::clamp(selectedVis, 0, maxI);
    drawerVis = std::clamp(drawerVis, 0, maxI);
}

void App::selectHomeIndex(int newVis, bool playSfx) {
    if (visibleIdx.empty()) return;
    newVis = std::clamp(newVis, 0, static_cast<int>(visibleIdx.size()) - 1);
    if (newVis != selectedVis) {
        if (selectedVis >= 0 && selectedVis < static_cast<int>(visibleIdx.size())) {
            prevHeroAppIdx = visibleIdx[static_cast<size_t>(selectedVis)];
        }
        selectedVis = newVis;
        drawerVis = newVis;
        heroAnim = 0.0f;
        if (playSfx) sfx::play(sfx::Sound::Move);
    }
}

void App::setTab(Tab t) {
    if (detailAppIdx >= 0) closeDetail(true);
    if (tab == t) return;
    tabDir = (static_cast<int>(t) > static_cast<int>(tab)) ? 1.0f : -1.0f;
    tab = t;
    viewFade = 0.0f;
    listFade = 0.0f;
    sfx::play(sfx::Sound::Tab);
    if (tab == Tab::Home) heroAnim = 0.0f;
    else if (tab == Tab::Files) fileBrowser.refresh();
}

void App::toggleFavoriteApp(int appIndex) {
    if (appIndex < 0 || appIndex >= static_cast<int>(allApps.size())) return;
    auto &a = allApps[static_cast<size_t>(appIndex)];
    a.favorite = !a.favorite;
    if (a.favorite) cfg.favorites.insert(a.path);
    else cfg.favorites.erase(a.path);
    nro::saveConfig(cfg);
    sfx::play(sfx::Sound::Fav);
    pushToast(T(a.favorite ? i18n::Key::AddedFav : i18n::Key::RemovedFav), a.name);
    rebuildVisible(a.path);
}

void App::toggleFavOnly() {
    favOnly = !favOnly;
    rebuildVisible();
    heroAnim = 0.0f;
    listFade = 0.0f;
    sfx::play(sfx::Sound::Tab);
}

void App::cycleSort(int dir) {
    std::string keep = (!visibleIdx.empty() && selectedVis < static_cast<int>(visibleIdx.size()))
                           ? allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(selectedVis)])].path
                           : "";
    cfg.sortMode = (cfg.sortMode + dir + 3) % 3;
    nro::saveConfig(cfg);
    rebuildVisible(keep);
    heroAnim = 0.0f;
    listFade = 0.0f;
    sfx::play(sfx::Sound::Tab);
}

void App::cycleFtpPort(int dir) {
    cfg.ftpPortIdx = (cfg.ftpPortIdx + dir + 3) % 3;
    nro::saveConfig(cfg);
    sfx::play(sfx::Sound::Tab);
    if (ftpSrv.isRunning()) {
        ftpSrv.stop();
        uint16_t port = cfg.ftpPort();
        if (ftpSrv.start(port)) {
            std::string ip = status.ipAddress.empty() ? "10.77.0.4" : status.ipAddress;
            pushToast(T(i18n::Key::FtpStarted), "ftp://" + ip + ":" + std::to_string(port));
        }
    }
}

void App::toggleFtp() {
    if (ftpSrv.isRunning()) {
        ftpSrv.stop();
        sfx::play(sfx::Sound::Back);
        pushToast(T(i18n::Key::FtpStopped), T(i18n::Key::FtpStandbySub));
    } else {
        uint16_t port = cfg.ftpPort();
        if (ftpSrv.start(port)) {
            sfx::play(sfx::Sound::Notify);
            std::string ip = status.ipAddress.empty() ? "10.77.0.4" : status.ipAddress;
            pushToast(T(i18n::Key::FtpStarted), "ftp://" + ip + ":" + std::to_string(port));
        }
    }
}

void App::openDetail(int appIndex) {
    if (appIndex < 0 || appIndex >= static_cast<int>(allApps.size())) return;
    detailAppIdx = appIndex;
    detailClosing = false;
    detailAnim = 0.0f;
    sfx::play(sfx::Sound::Tab);
}

void App::closeDetail(bool immediate) {
    if (immediate) {
        detailAppIdx = -1;
        detailClosing = false;
        detailAnim = 0.0f;
    } else if (detailAppIdx >= 0 && !detailClosing) {
        detailClosing = true;
        sfx::play(sfx::Sound::Back);
    }
}

void App::triggerLaunch(int appIndex) {
    if (appIndex < 0 || appIndex >= static_cast<int>(allApps.size()) || launching) return;
    auto &a = allApps[static_cast<size_t>(appIndex)];
    a.lastLaunched = static_cast<uint64_t>(time(nullptr));
    cfg.launchTimes[a.path] = a.lastLaunched;
    nro::saveConfig(cfg);
    launchPath = a.path;
    launchName = a.name;
    launching = true;
    launchFade = 0.0f;
    sfx::play(sfx::Sound::Launch);
}

void App::triggerLaunchPath(const std::string &path) {
    for (int i = 0; i < static_cast<int>(allApps.size()); ++i) {
        if (allApps[static_cast<size_t>(i)].path == path) {
            openDetail(i);
            return;
        }
    }
    launchPath = path;
    size_t sl = path.rfind('/');
    launchName = (sl == std::string::npos) ? path : path.substr(sl + 1);
    launching = true;
    launchFade = 0.0f;
    sfx::play(sfx::Sound::Launch);
}

void App::update(const plat::Input &in, float dt) {
    timeSec += dt;
    if (toast.timer > 0.0f) toast.timer = std::max(0.0f, toast.timer - dt);

    statusTimer += dt;
    if (statusTimer >= 1.5f) {
        statusTimer = 0.0f;
        status = plat::queryStatus();
    }

    std::string ftpFile;
    if (ftpSrv.consumeLibraryDirty(ftpFile)) {
        std::string keep = (!visibleIdx.empty() && selectedVis < static_cast<int>(visibleIdx.size()))
                               ? allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(selectedVis)])].path
                               : "";
        nro::scanApps(allApps, cfg);
        rebuildVisible(keep);
        fileBrowser.refresh();
        sfx::play(sfx::Sound::Notify);
        pushToast(T(i18n::Key::LibUpdated), ftpFile);
    }

    if (launching) {
        launchFade = std::min(1.0f, launchFade + dt * 3.2f);
        if (launchFade >= 1.0f) {
            ftpSrv.stop();
            if (plat::launchNro(launchPath)) wantExit = true;
            else {
                launching = false;
                launchFade = 0.0f;
                if (cfg.ftpAutoStart) ftpSrv.start(cfg.ftpPort());
                pushToast(T(i18n::Key::LaunchFailed), T(i18n::Key::LaunchFailedSub));
            }
        }
        return;
    }

    // Smooth, crisp animations (including Detail OPEN and CLOSE!)
    heroAnim = std::min(1.0f, heroAnim + dt * 4.2f);
    viewFade = std::min(1.0f, viewFade + dt * 4.6f);
    listFade = std::min(1.0f, listFade + dt * 4.8f);
    settingPopAnim = std::min(1.0f, settingPopAnim + dt * 4.8f);

    if (detailAppIdx >= 0) {
        if (detailClosing) {
            detailAnim = std::max(0.0f, detailAnim - dt * 4.8f);
            if (detailAnim <= 0.004f) {
                detailAppIdx = -1;
                detailClosing = false;
                detailAnim = 0.0f;
            }
        } else {
            detailAnim = std::min(1.0f, detailAnim + dt * 4.2f);
        }
    }

    float tgtTabX = tabStartX(static_cast<int>(tab));
    float tgtTabW = kTabW[static_cast<int>(tab)];
    float spring = std::min(1.0f, dt * 15.0f);
    tabPillX += (tgtTabX - tabPillX) * spring;
    tabPillW += (tgtTabW - tabPillW) * spring;

    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    gfx::Color targetA = th.accent;
    gfx::Color targetB = th.accentDeep;
    int glowIdx = -1;
    if (detailAppIdx >= 0 && detailAppIdx < static_cast<int>(allApps.size()) && !detailClosing) glowIdx = detailAppIdx;
    else if (!visibleIdx.empty()) {
        int vi = (tab == Tab::Apps) ? drawerVis : selectedVis;
        if (vi >= 0 && vi < static_cast<int>(visibleIdx.size())) glowIdx = visibleIdx[static_cast<size_t>(vi)];
    }
    if (glowIdx >= 0) {
        targetA = gfx::lerpColor(th.accent, allApps[static_cast<size_t>(glowIdx)].colorA, 0.28f);
        targetB = gfx::lerpColor(th.accentDeep, allApps[static_cast<size_t>(glowIdx)].colorB, 0.25f);
    }
    float glowK = std::min(1.0f, dt * 4.0f);
    curGlowA = gfx::lerpColor(curGlowA, targetA, glowK);
    curGlowB = gfx::lerpColor(curGlowB, targetB, glowK);

    // 1. Fullscreen App Detail Page (blocks input while closing smoothly)
    if (detailAppIdx >= 0 && detailAppIdx < static_cast<int>(allApps.size())) {
        if (detailClosing) return;
        if (in.btnB || (in.leftPressed && gfx::Rect{56, 126, 224, 62}.contains(in.mx, in.my))) {
            closeDetail(false);
            return;
        }
        if (in.btnA || (in.leftPressed && gfx::Rect{556, 738, 290, 76}.contains(in.mx, in.my))) {
            triggerLaunch(detailAppIdx);
            return;
        }
        if (in.btnY || (in.leftPressed && gfx::Rect{866, 738, 270, 76}.contains(in.mx, in.my))) {
            toggleFavoriteApp(detailAppIdx);
            return;
        }
        if (in.btnX || (in.leftPressed && gfx::Rect{1156, 738, 360, 76}.contains(in.mx, in.my))) {
            std::string p = allApps[static_cast<size_t>(detailAppIdx)].path;
            size_t sl = p.rfind('/');
            std::string dir = (sl == std::string::npos) ? "sdmc:/switch" : p.substr(0, sl);
            closeDetail(true);
            fileBrowser.navigate(dir);
            setTab(Tab::Files);
            return;
        }
        return;
    }

    // Global navigation
    bool fileModal = (tab == Tab::Files && (fileBrowser.previewOpen || fileBrowser.confirmDeleteOpen));
    if (!fileModal) {
        if (in.btnL) setTab(static_cast<Tab>((static_cast<int>(tab) + 4) % 5));
        if (in.btnR) setTab(static_cast<Tab>((static_cast<int>(tab) + 1) % 5));
        if (in.btnMinus) toggleFtp();

        if (in.btnPlus) {
            std::string keep = (!visibleIdx.empty() && selectedVis < static_cast<int>(visibleIdx.size()))
                                   ? allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(selectedVis)])].path
                                   : "";
            nro::scanApps(allApps, cfg);
            rebuildVisible(keep);
            fileBrowser.refresh();
            status = plat::queryStatus();
            listFade = 0.0f;
            sfx::play(sfx::Sound::Notify);
            pushToast(T(i18n::Key::SdRefreshed), std::to_string(allApps.size()) + " " + T(i18n::Key::AppsCount));
        }

        if (in.leftPressed && in.my >= kBarY && in.my <= kBarY + kBarH) {
            for (int i = 0; i < 5; ++i) {
                if (gfx::Rect{tabStartX(i), kBarY + 6.0f, kTabW[i], kBarH - 12.0f}.contains(in.mx, in.my)) {
                    setTab(static_cast<Tab>(i));
                    return;
                }
            }
        }
    }

    // 2. Per-tab logic (100% gamepad-accessible!)
    if (tab == Tab::Home) {
        if (in.navLeft) selectHomeIndex(selectedVis - 1);
        if (in.navRight) selectHomeIndex(selectedVis + 1);
        if (in.navUp && !visibleIdx.empty()) {
            openDetail(visibleIdx[static_cast<size_t>(selectedVis)]);
            return;
        }
        if (in.btnY && !visibleIdx.empty()) toggleFavoriteApp(visibleIdx[static_cast<size_t>(selectedVis)]);
        if (in.btnX) cycleSort(1);
        if (in.btnZL) toggleFavOnly();
        if (in.btnZR) { setTab(Tab::Apps); return; }
        if (in.btnB && (!searchFilter.empty() || favOnly)) {
            searchFilter.clear();
            favOnly = false;
            rebuildVisible();
            heroAnim = 0.0f;
            listFade = 0.0f;
            sfx::play(sfx::Sound::Back);
        }
        if (in.btnA && !visibleIdx.empty()) {
            triggerLaunch(visibleIdx[static_cast<size_t>(selectedVis)]);
            return;
        }

        float contentW = static_cast<float>(visibleIdx.size()) * kCardStep - kCardGap;
        float maxScroll = std::max(0.0f, contentW - (plat::kW - kRailPadX * 2.0f));
        if (std::fabs(in.wheelX) > 0.01f) railVelX += in.wheelX * 520.0f;
        if (std::fabs(in.wheelY) > 0.01f) railVelX -= in.wheelY * 280.0f;

        if (in.leftPressed && in.my >= kRailY - 20.0f) {
            railDragging = true;
            railDragLastX = in.mx;
            railDragAccum = 0.0f;
        }
        if (railDragging && in.leftDown) {
            float dx = in.mx - railDragLastX;
            railDragLastX = in.mx;
            railDragAccum += std::fabs(dx);
            if (railDragAccum > 14.0f) {
                railScrollX -= dx;
                railVelX = -dx / std::max(0.004f, dt);
            }
        }
        if (railDragging && !in.leftDown) {
            railDragging = false;
            if (railDragAccum < 16.0f) {
                for (int i = 0; i < static_cast<int>(visibleIdx.size()); ++i) {
                    float cx = kRailPadX + i * kCardStep - railScrollX;
                    if (gfx::Rect{cx, kRailY - 16.0f, kCardW, kCardH + 16.0f}.contains(in.mx, in.my)) {
                        if (i == selectedVis) openDetail(visibleIdx[static_cast<size_t>(i)]);
                        else selectHomeIndex(i);
                        break;
                    }
                }
            }
        }

        if (!railDragging) {
            if (std::fabs(railVelX) > 6.0f) {
                railScrollX += railVelX * dt;
                railVelX *= std::pow(0.002f, dt);
            } else {
                float targetCenter = selectedVis * kCardStep - (plat::kW * 0.5f - kRailPadX - kCardW * 0.5f);
                float targetScroll = std::clamp(targetCenter, 0.0f, maxScroll);
                railScrollX += (targetScroll - railScrollX) * std::min(1.0f, dt * 10.0f);
            }
        }
        railScrollX = std::clamp(railScrollX, 0.0f, maxScroll);

        // Touch/mouse hit targets on Home (matching every gamepad button!)
        if (in.leftPressed && in.my < kRailY - 20.0f) {
            float by = kHeroY + 288.0f;
            if (!visibleIdx.empty() && gfx::Rect{112, by, 250, 68}.contains(in.mx, in.my)) {
                triggerLaunch(visibleIdx[static_cast<size_t>(selectedVis)]);
                return;
            }
            if (!visibleIdx.empty() && gfx::Rect{378, by, 240, 68}.contains(in.mx, in.my)) {
                toggleFavoriteApp(visibleIdx[static_cast<size_t>(selectedVis)]);
                return;
            }
            if (gfx::Rect{634, by, 258, 68}.contains(in.mx, in.my)) { cycleSort(1); return; }
            if (!visibleIdx.empty() && gfx::Rect{908, by, 186, 68}.contains(in.mx, in.my)) {
                openDetail(visibleIdx[static_cast<size_t>(selectedVis)]);
                return;
            }
            if (!visibleIdx.empty() && gfx::Rect{1524, kHeroY + 60.0f, 280, 280}.contains(in.mx, in.my)) {
                openDetail(visibleIdx[static_cast<size_t>(selectedVis)]);
                return;
            }

            // Section header buttons (y = 564..620): FTP pill + Favorites (ZL) + See all (ZR)
            if (gfx::Rect{958, 564, 382, 56}.contains(in.mx, in.my)) { setTab(Tab::Ftp); return; }
            if (gfx::Rect{1356, 564, 244, 56}.contains(in.mx, in.my)) { toggleFavOnly(); return; }
            if (gfx::Rect{1616, 564, 248, 56}.contains(in.mx, in.my)) { setTab(Tab::Apps); return; }
        }

        for (int i = 0; i < static_cast<int>(visibleIdx.size()); ++i) {
            auto &a = allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(i)])];
            float cx = kRailPadX + i * kCardStep - railScrollX;
            bool hov = in.pointerVisible && gfx::Rect{cx, kRailY - 14.0f, kCardW, kCardH + 14.0f}.contains(in.mx, in.my);
            float fTarget = (i == selectedVis) ? 1.0f : 0.0f;
            float fRate = (fTarget > a.focusAnim) ? 11.5f : 6.2f;  // snappy focus-in, silky smooth fade-out!
            a.focusAnim += (fTarget - a.focusAnim) * std::min(1.0f, dt * fRate);
            a.hoverAnim += ((hov ? 1.0f : 0.0f) - a.hoverAnim) * std::min(1.0f, dt * 10.0f);
        }
    } else if (tab == Tab::Apps) {
        int count = static_cast<int>(visibleIdx.size());
        if (count > 0) {
            if (in.navLeft && drawerVis > 0) { drawerVis--; sfx::play(sfx::Sound::Move); }
            if (in.navRight && drawerVis + 1 < count) { drawerVis++; sfx::play(sfx::Sound::Move); }
            if (in.navUp && drawerVis - kDrawerCols >= 0) { drawerVis -= kDrawerCols; sfx::play(sfx::Sound::Move); }
            if (in.navDown && drawerVis + kDrawerCols < count) { drawerVis += kDrawerCols; sfx::play(sfx::Sound::Move); }
            if (in.btnA) { openDetail(visibleIdx[static_cast<size_t>(drawerVis)]); return; }
            if (in.btnY) { toggleFavoriteApp(visibleIdx[static_cast<size_t>(drawerVis)]); return; }
        }
        // Full gamepad mapping matching every chip in the Apps header!
        if (in.btnZL) toggleFavOnly();
        if (in.btnZR) cycleSort(1);
        if (in.btnX) {
            std::string q;
            if (plat::showKeyboard(T(i18n::Key::FilterAppsPrompt), searchFilter, q, 64)) {
                searchFilter = q;
                rebuildVisible();
                listFade = 0.0f;
            }
        }
        if (in.btnB) {
            if (!searchFilter.empty() || favOnly) {
                searchFilter.clear();
                favOnly = false;
                rebuildVisible();
                listFade = 0.0f;
                sfx::play(sfx::Sound::Back);
            } else {
                setTab(Tab::Home);
            }
            return;
        }

        if (in.leftPressed && in.my >= 142.0f && in.my <= 212.0f) {
            if (gfx::Rect{1012, 146, 246, 60}.contains(in.mx, in.my)) { toggleFavOnly(); return; }
            if (gfx::Rect{1274, 146, 286, 60}.contains(in.mx, in.my)) { cycleSort(1); return; }
            if (gfx::Rect{1576, 146, 260, 60}.contains(in.mx, in.my)) {
                std::string q;
                if (plat::showKeyboard(T(i18n::Key::FilterAppsPrompt), searchFilter, q, 64)) {
                    searchFilter = q;
                    rebuildVisible();
                    listFade = 0.0f;
                }
                return;
            }
        }

        int totalRows = (count + kDrawerCols - 1) / kDrawerCols;
        float contentH = std::max(0.0f, totalRows * kDrawerStepY - kDrawerGapY + 40.0f);
        float maxScroll = std::max(0.0f, contentH - kDrawerViewH);

        if (std::fabs(in.wheelY) > 0.01f) drawerVelY -= in.wheelY * 340.0f;

        if (in.leftPressed && in.my >= kDrawerY0) {
            drawerDragging = true;
            drawerDragLastY = in.my;
            drawerDragAccum = 0.0f;
        }
        if (drawerDragging && in.leftDown) {
            float dy = in.my - drawerDragLastY;
            drawerDragLastY = in.my;
            drawerDragAccum += std::fabs(dy);
            if (drawerDragAccum > 14.0f) {
                drawerScrollY -= dy;
                drawerVelY = -dy / std::max(0.004f, dt);
            }
        }
        if (drawerDragging && !in.leftDown) {
            drawerDragging = false;
            if (drawerDragAccum < 16.0f) {
                for (int i = 0; i < count; ++i) {
                    int col = i % kDrawerCols, row = i / kDrawerCols;
                    float cx = kDrawerX0 + col * kDrawerStepX;
                    float cy = kDrawerY0 + row * kDrawerStepY - drawerScrollY;
                    if (gfx::Rect{cx, cy, kDrawerCellW, kDrawerCellH}.contains(in.mx, in.my)) {
                        drawerVis = i;
                        openDetail(visibleIdx[static_cast<size_t>(i)]);
                        break;
                    }
                }
            }
        }

        if (!drawerDragging) {
            if (std::fabs(drawerVelY) > 6.0f) {
                drawerScrollY += drawerVelY * dt;
                drawerVelY *= std::pow(0.002f, dt);
            } else if (count > 0) {
                int row = drawerVis / kDrawerCols;
                float rTop = row * kDrawerStepY;
                float rBot = rTop + kDrawerCellH;
                float target = drawerScrollY;
                if (rTop < drawerScrollY + 24.0f) target = rTop - 24.0f;
                else if (rBot > drawerScrollY + kDrawerViewH - 24.0f) target = rBot - kDrawerViewH + 24.0f;
                target = std::clamp(target, 0.0f, maxScroll);
                drawerScrollY += (target - drawerScrollY) * std::min(1.0f, dt * 11.0f);
            }
        }
        drawerScrollY = std::clamp(drawerScrollY, 0.0f, maxScroll);

        for (int i = 0; i < count; ++i) {
            auto &a = allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(i)])];
            int col = i % kDrawerCols, row = i / kDrawerCols;
            float cx = kDrawerX0 + col * kDrawerStepX;
            float cy = kDrawerY0 + row * kDrawerStepY - drawerScrollY;
            bool hov = in.pointerVisible && gfx::Rect{cx, cy, kDrawerCellW, kDrawerCellH}.contains(in.mx, in.my);
            float fTarget = (i == drawerVis) ? 1.0f : 0.0f;
            float fRate = (fTarget > a.focusAnim) ? 11.5f : 6.2f;
            a.focusAnim += (fTarget - a.focusAnim) * std::min(1.0f, dt * fRate);
            a.hoverAnim += ((hov ? 1.0f : 0.0f) - a.hoverAnim) * std::min(1.0f, dt * 10.0f);
        }
    } else if (tab == Tab::Files) {
        fileBrowser.update(
            in, dt, cfg.lang,
            [this](const std::string &nroPath) { triggerLaunchPath(nroPath); },
            [this](const std::string &t, const std::string &b) { pushToast(t, b); });
    } else if (tab == Tab::Ftp) {
        if (in.btnB) { setTab(Tab::Home); return; }
        bool toggleHit = in.btnA || (in.leftPressed && gfx::Rect{1240, 206, 360, 68}.contains(in.mx, in.my));
        if (toggleHit) { toggleFtp(); return; }
        bool portHit = in.btnX || (in.leftPressed && gfx::Rect{696, 916, 310, 64}.contains(in.mx, in.my));
        if (portHit) { cycleFtpPort(1); return; }
        if (in.leftPressed && gfx::Rect{280, 916, 390, 64}.contains(in.mx, in.my)) {
            nro::scanApps(allApps, cfg);
            rebuildVisible();
            sfx::play(sfx::Sound::Notify);
            pushToast(T(i18n::Key::SdRefreshed), std::to_string(allApps.size()) + " " + T(i18n::Key::AppsCount));
        }
    } else if (tab == Tab::Settings) {
        if (in.btnB) { setTab(Tab::Home); return; }
        if (in.navUp) { settingsCursor = (settingsCursor + kSettingsCount - 1) % kSettingsCount; sfx::play(sfx::Sound::Move); }
        if (in.navDown) { settingsCursor = (settingsCursor + 1) % kSettingsCount; sfx::play(sfx::Sound::Move); }

        auto applySetting = [&](int idx, int dir) {
            sfx::play(sfx::Sound::Tab);
            settingPopIdx = idx;
            settingPopAnim = 0.0f;
            switch (idx) {
                case 0: {
                    cfg.lang = (cfg.lang + dir + static_cast<int>(i18n::Lang::Count)) % static_cast<int>(i18n::Lang::Count);
                    nro::scanApps(allApps, cfg);
                    rebuildVisible();
                    break;
                }
                case 1: cfg.themeIdx = (cfg.themeIdx + dir + 6) % 6; break;
                case 2: cfg.startupTab = (cfg.startupTab + 1) % 2; break;
                case 3: cfg.clock24h = !cfg.clock24h; break;
                case 4: cfg.showBatteryPct = !cfg.showBatteryPct; break;
                case 5: cfg.glowLevel = (cfg.glowLevel + dir + 3) % 3; break;
                case 6: cfg.serifTitles = !cfg.serifTitles; break;
                case 7: cfg.lowercase = !cfg.lowercase; break;
                case 8: {
                    cfg.hideSelf = !cfg.hideSelf;
                    nro::scanApps(allApps, cfg);
                    rebuildVisible();
                    break;
                }
                case 9: cfg.ftpAutoStart = !cfg.ftpAutoStart; break;
                case 10: cycleFtpPort(dir); return;
                case 11: cfg.soundsEnabled = !cfg.soundsEnabled; sfx::setEnabled(cfg.soundsEnabled); break;
                case 12: wantExit = true; return;
            }
            nro::saveConfig(cfg);
        };

        if (in.btnA || in.navRight) applySetting(settingsCursor, 1);
        if (in.navLeft) applySetting(settingsCursor, -1);

        // Smooth scrolling for 13 settings rows inside the Liquid Glass panel
        float contentH = kSettingsCount * kSetRowStep + 16.0f;
        float maxScroll = std::max(0.0f, contentH - kSetListH);
        if (std::fabs(in.wheelY) > 0.01f) {
            settingsScrollY = std::clamp(settingsScrollY - in.wheelY * 90.0f, 0.0f, maxScroll);
        } else {
            float rTop = settingsCursor * kSetRowStep;
            float rBot = rTop + kSetRowH;
            float target = settingsScrollY;
            if (rTop < settingsScrollY + 24.0f) target = rTop - 24.0f;
            else if (rBot > settingsScrollY + kSetListH - 24.0f) target = rBot - kSetListH + 24.0f;
            target = std::clamp(target, 0.0f, maxScroll);
            settingsScrollY += (target - settingsScrollY) * std::min(1.0f, dt * 13.0f);
        }

        if (in.leftPressed && in.my >= kSetListY && in.my <= kSetListY + kSetListH) {
            for (int i = 0; i < kSettingsCount; ++i) {
                float ry = kSetListY + 8.0f + i * kSetRowStep - settingsScrollY;
                if (gfx::Rect{276, ry, 1368, kSetRowH}.contains(in.mx, in.my)) {
                    settingsCursor = i;
                    applySetting(i, 1);
                    return;
                }
            }
        }
    }
}

void App::renderBackground(gfx::Renderer &r) {
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    r.fillRectGradDiag(0, 0, plat::kW, plat::kH, th.bgTop, th.bgBot);

    float glowMult = (cfg.glowLevel == 0) ? 0.55f : (cfg.glowLevel == 1 ? 1.0f : 1.38f);
    float t = static_cast<float>(timeSec);

    r.drawGlowOrb(1560.0f + std::sin(t * 0.18f) * 70.0f, 180.0f + std::cos(t * 0.14f) * 50.0f,
                  880.0f, 620.0f, gfx::withAlpha(curGlowB, th.orbAlpha * glowMult));
    r.drawGlowOrb(380.0f + std::cos(t * 0.16f) * 80.0f, 860.0f + std::sin(t * 0.20f) * 60.0f,
                  760.0f, 520.0f, gfx::withAlpha(curGlowA, th.orbAlpha * 0.55f * glowMult));
}

void App::renderTopBar(gfx::Renderer &r, const plat::Input &in) {
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];

    r.drawShadow(kBarX, kBarY + 6, kBarW, kBarH, 28, 38, gfx::rgba(0, 0, 0, 0.36f));
    r.drawLiquidGlass(kBarX, kBarY, kBarW, kBarH, 38.0f, th.glassTint, 0.0f, gfx::withAlpha(th.accent, 0.24f));

    // Left: Fixed crescent moon + star + "Tsuki" in InstrumentSerif-Italic
    r.drawYoruMoon(kBarX + 42.0f, kBarY + kBarH * 0.5f, 15.0f, th.accent);
    r.drawTextBoxed(gfx::Font::SerifItalic, 46.0f, kBarX + 70.0f, kBarY, kBarH, "Tsuki",
                    gfx::hex(0xFFFFFF, 0.96f), gfx::Align::Left);

    // Subtle L / R shoulder badges flanking the 5 tabs so docked/gamepad users see tab controls immediately!
    r.drawSoftCard(kTabsX0 - 52.0f, kBarY + 21.0f, 38.0f, 34.0f, 11.0f,
                   gfx::rgba(255, 224, 238, 0.10f), gfx::rgba(255, 224, 238, 0.14f));
    r.drawTextBoxed(gfx::Font::Semi, 18.0f, kTabsX0 - 33.0f, kBarY + 21.0f, 34.0f, "L",
                    gfx::hex(0xFFFFFF, 0.72f), gfx::Align::Center);

    float tabsEndX = tabStartX(4) + kTabW[4];
    r.drawSoftCard(tabsEndX + 14.0f, kBarY + 21.0f, 38.0f, 34.0f, 11.0f,
                   gfx::rgba(255, 224, 238, 0.10f), gfx::rgba(255, 224, 238, 0.14f));
    r.drawTextBoxed(gfx::Font::Semi, 18.0f, tabsEndX + 33.0f, kBarY + 21.0f, 34.0f, "R",
                    gfx::hex(0xFFFFFF, 0.72f), gfx::Align::Center);

    // Center: Sliding active tab Liquid Glass pill with soft inner glow
    r.drawShadow(tabPillX, kBarY + 10, tabPillW, kBarH - 20, 18, 28, gfx::withAlpha(th.accent, 0.24f));
    r.drawLiquidGlass(tabPillX, kBarY + 9.0f, tabPillW, kBarH - 18.0f, 29.0f,
                      gfx::withAlpha(th.accent, 0.22f), 0.72f, gfx::withAlpha(th.accent, 0.75f));

    const i18n::Key tabKeys[5] = {
        i18n::Key::TabHome, i18n::Key::TabApps, i18n::Key::TabFiles, i18n::Key::TabFtp, i18n::Key::TabSettings,
    };
    const gfx::UiIcon tabIc[5] = {
        gfx::UiIcon::Home, gfx::UiIcon::Apps, gfx::UiIcon::Folder, gfx::UiIcon::Ftp, gfx::UiIcon::Settings,
    };
    for (int i = 0; i < 5; ++i) {
        float tx = tabStartX(i);
        bool act = (static_cast<int>(tab) == i);
        bool hov = in.pointerVisible && gfx::Rect{tx, kBarY + 8, kTabW[i], kBarH - 16}.contains(in.mx, in.my);
        gfx::Color fg = act ? gfx::hex(0xFFFFFF) : (hov ? gfx::hex(0xFFFFFF, 0.92f) : gfx::hex(0xFFFFFF, 0.68f));
        std::string lbl = fmtKey(tabKeys[i]);
        float tw = r.measureText(act ? gfx::Font::Semi : gfx::Font::Sans, 23.0f, lbl.c_str());
        float totalW = 22.0f + 10.0f + tw + (i == 3 ? 16.0f : 0.0f);
        float startX = tx + (kTabW[i] - totalW) * 0.5f;

        r.drawUiIcon(tabIc[i], startX + 10.0f, kBarY + kBarH * 0.5f, 20.0f, fg);
        r.drawTextBoxed(act ? gfx::Font::Semi : gfx::Font::Sans, 23.0f, startX + 30.0f, kBarY, kBarH,
                        lbl.c_str(), fg, gfx::Align::Left);
        if (i == 3) {
            float pulse = ftpSrv.isRunning() ? (0.65f + 0.35f * std::sin(static_cast<float>(timeSec) * 3.8f)) : 0.30f;
            r.fillCircle(startX + totalW - 4.0f, kBarY + kBarH * 0.5f, 5.5f,
                         ftpSrv.isRunning() ? gfx::hex(0x6EE7B7, pulse) : gfx::hex(0xFFFFFF, 0.30f));
        }
    }

    // Right inside unified bar: SD Free · Clock (24h or 12h) · Battery (with optional %)
    std::string sdStr = nro::formatBytes(status.sdFreeBytes, cfg.lang);
    char clkBuf[24], batBuf[16];
    if (cfg.clock24h) {
        std::snprintf(clkBuf, sizeof(clkBuf), "%02d:%02d", status.hour, status.minute);
    } else {
        int h12 = status.hour % 12;
        if (h12 == 0) h12 = 12;
        std::snprintf(clkBuf, sizeof(clkBuf), "%d:%02d %s", h12, status.minute, status.hour >= 12 ? "PM" : "AM");
    }
    std::snprintf(batBuf, sizeof(batBuf), "%d%%", status.batteryPercent);

    float curX = kBarX + kBarW - 32.0f;
    if (cfg.showBatteryPct) {
        float bw = r.measureText(gfx::Font::Semi, 22.0f, batBuf);
        r.drawTextBoxed(gfx::Font::Semi, 22.0f, curX, kBarY, kBarH, batBuf, gfx::hex(0xFFFFFF, 0.90f), gfx::Align::Right);
        curX -= (bw + 12.0f);
    }
    r.drawBattery(curX - 44.0f, kBarY + (kBarH - 22.0f) * 0.5f, 44.0f, 22.0f,
                  status.batteryPercent, status.charging, gfx::hex(0xFFFFFF, 0.90f));
    curX -= 62.0f;

    float clkW = r.measureText(gfx::Font::Semi, 24.0f, clkBuf);
    r.drawTextBoxed(gfx::Font::Semi, 24.0f, curX, kBarY, kBarH, clkBuf, gfx::hex(0xFFFFFF, 0.96f), gfx::Align::Right);
    curX -= (clkW + 18.0f);

    r.fillCircle(curX, kBarY + kBarH * 0.5f, 2.5f, gfx::rgba(255, 220, 236, 0.38f));
    curX -= 16.0f;
    r.drawTextBoxed(gfx::Font::Sans, 21.0f, curX, kBarY, kBarH, sdStr.c_str(),
                    gfx::hex(0xFFFFFF, 0.78f), gfx::Align::Right);
}

void App::renderHomeView(gfx::Renderer &r, const plat::Input &in) {
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    float vf = easeOutCubic(viewFade);
    float slideX = (1.0f - vf) * 32.0f * tabDir;
    r.pushAlpha(vf);

    if (visibleIdx.empty()) {
        r.drawTextCleanReveal(gfx::Font::Semi, 52.0f, 660.0f + slideX, 420.0f, 900.0f, fmtKey(i18n::Key::NoAppFound).c_str(),
                              gfx::hex(0xFFFFFF), viewFade);
        r.drawText(gfx::Font::Sans, 25.0f, plat::kW * 0.5f + slideX, 500.0f,
                   T(i18n::Key::NoAppHint), gfx::hex(0xFFFFFF, 0.65f), gfx::Align::Center);
        r.popAlpha();
        return;
    }

    const auto &sel = allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(selectedVis)])];
    const i18n::Key sortKeys[3] = {i18n::Key::SortRecent, i18n::Key::SortAZ, i18n::Key::SortSize};

    // 1. UNIFIED HERO BANNER (100% crisp rounded 32px corners — zero square overlays or seams!)
    float heroX = kHeroX + slideX;
    r.drawShadow(heroX, kHeroY + 14, kHeroW, kHeroH, 38, 32, gfx::rgba(0, 0, 0, 0.44f));
    r.drawSoftCard(heroX, kHeroY, kHeroW, kHeroH, 32.0f, th.cardFill, gfx::rgba(255, 224, 238, 0.08f));

    // Clean, unobstructed 280x280 rounded app icon on the right (60px symmetric padding top/bottom/right)
    float hEase = easeOutCubic(heroAnim);
    float iconSz = 280.0f;
    float iconX = heroX + kHeroW - 60.0f - iconSz;
    float iconY = kHeroY + (kHeroH - iconSz) * 0.5f;
    float iconCx = iconX + iconSz * 0.5f;
    float iconCy = iconY + iconSz * 0.5f;

    // Soft ambient halo centered behind icon (radius 195px fits completely inside 400px-tall Hero!)
    r.drawGlowOrb(iconCx, iconCy, 195.0f, 195.0f, gfx::withAlpha(curGlowA, 0.30f));

    auto drawHeroIcon = [&](const nro::AppEntry &app, float alpha, float scale) {
        if (alpha <= 0.005f) return;
        float sz = iconSz * scale;
        float dx = iconCx - sz * 0.5f;
        float dy = iconCy - sz * 0.5f;
        r.pushAlpha(alpha);
        if (app.icon.id) {
            r.drawRoundedTexture(app.icon, dx, dy, sz, sz, 38.0f, {1, 1, 1, 1}, 0.08f);
        } else {
            r.drawGradientPill(dx, dy, sz, sz, 38.0f, app.colorA, app.colorB);
            char mono[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(app.name.empty() ? 'H' : app.name[0]))), 0};
            r.drawTextBoxed(gfx::Font::Semi, 112.0f, iconCx, dy, sz, mono, gfx::hex(0xFFFFFF), gfx::Align::Center);
        }
        r.popAlpha();
    };

    // Single steady drop shadow + smooth cross-fade from previous app icon to newly selected app icon!
    r.drawShadow(iconX, iconY + 12.0f, iconSz, iconSz, 30.0f, 38.0f, gfx::rgba(0, 0, 0, 0.48f));
    if (prevHeroAppIdx >= 0 && prevHeroAppIdx < static_cast<int>(allApps.size()) &&
        prevHeroAppIdx != visibleIdx[static_cast<size_t>(selectedVis)] && hEase < 0.995f) {
        drawHeroIcon(allApps[static_cast<size_t>(prevHeroAppIdx)], 1.0f - hEase, 1.0f - 0.03f * hEase);
    }
    drawHeroIcon(sel, hEase, 0.97f + 0.03f * hEase);

    float hx = heroX + 56.0f;
    std::string kicker = "v" + sel.version + "  •  " + nro::formatBytes(sel.fileSize, cfg.lang) +
                         (sel.author.empty() ? "" : ("  •  " + sel.author));
    r.drawTextCleanReveal(gfx::Font::Semi, 22.0f, hx, kHeroY + 48.0f, 1320.0f,
                          fmt(kicker).c_str(), th.accent, std::clamp(heroAnim * 1.3f, 0.0f, 1.0f));

    r.drawTextCleanReveal(cfg.serifTitles ? gfx::Font::Serif : gfx::Font::Semi,
                          cfg.serifTitles ? 100.0f : 76.0f,
                          hx, cfg.serifTitles ? (kHeroY + 88.0f) : (kHeroY + 100.0f), 1320.0f,
                          fmt(sel.name).c_str(), gfx::hex(0xFFFFFF, 0.98f), heroAnim);

    r.drawTextCleanReveal(gfx::Font::Sans, 24.0f, hx, kHeroY + 210.0f, 1320.0f,
                          sel.displayPath.c_str(), gfx::hex(0xFFFFFF, 0.62f),
                          std::clamp((heroAnim - 0.10f) * 1.3f, 0.0f, 1.0f));

    // Aligned Hero Action Row — EVERY pill clearly shows its gamepad button!
    float by = kHeroY + 288.0f;

    // 1) Launch (A)
    r.drawShadow(hx, by + 6, 250, 68, 22, 34, gfx::withAlpha(th.accent, 0.34f));
    r.fillRoundedRect(hx, by, 250, 68, 34.0f, th.accent);
    r.drawUiIcon(gfx::UiIcon::Play, hx + 44.0f, by + 34.0f, 21.0f, th.bgTop);
    r.drawTextBoxed(gfx::Font::Semi, 24.0f, hx + 72.0f, by, 68.0f, fmtKey(i18n::Key::LaunchA).c_str(),
                    th.bgTop, gfx::Align::Left);

    // 2) Favorite (Y)
    bool hFav = in.pointerVisible && gfx::Rect{378, by, 240, 68}.contains(in.mx, in.my);
    r.drawSoftCard(378 + slideX, by, 240, 68, 34.0f, gfx::rgba(255, 224, 238, hFav ? 0.18f : 0.10f), gfx::rgba(255, 224, 238, 0.10f));
    r.drawStar(416.0f + slideX, by + 34.0f, 11.0f, sel.favorite, sel.favorite ? th.accent : gfx::hex(0xFFFFFF, 0.85f));
    r.drawTextBoxed(gfx::Font::Semi, 22.0f, 442.0f + slideX, by, 68.0f,
                    fmtKey(sel.favorite ? i18n::Key::FavoriteY : i18n::Key::PinY).c_str(),
                    gfx::hex(0xFFFFFF, 0.94f), gfx::Align::Left);

    // 3) Sort (X)
    bool hSort = in.pointerVisible && gfx::Rect{634, by, 258, 68}.contains(in.mx, in.my);
    r.drawSoftCard(634 + slideX, by, 258, 68, 34.0f, gfx::rgba(255, 224, 238, hSort ? 0.18f : 0.10f), gfx::rgba(255, 224, 238, 0.10f));
    r.drawUiIcon(gfx::UiIcon::Sort, 672.0f + slideX, by + 34.0f, 19.0f, gfx::hex(0xFFFFFF, 0.82f));
    std::string sortLbl = fmtKey(sortKeys[cfg.sortMode]) + " (X)";
    r.drawTextBoxed(gfx::Font::Semi, 22.0f, 698.0f + slideX, by, 68.0f, sortLbl.c_str(),
                    gfx::hex(0xFFFFFF, 0.92f), gfx::Align::Left);

    // 4) Details (↑)
    bool hDet = in.pointerVisible && gfx::Rect{908, by, 186, 68}.contains(in.mx, in.my);
    r.drawSoftCard(908 + slideX, by, 186, 68, 34.0f, gfx::rgba(255, 224, 238, hDet ? 0.18f : 0.08f), gfx::rgba(255, 224, 238, 0.09f));
    std::string detLbl = fmtKey(i18n::Key::Details) + " (↑)";
    r.drawTextBoxed(gfx::Font::Semi, 22.0f, 1001.0f + slideX, by, 68.0f, detLbl.c_str(),
                    gfx::hex(0xFFFFFF, 0.88f), gfx::Align::Center);

    // 2. Section Header Row: Library title on left · FTP (-) · Favorites (ZL) · See all (ZR) on right!
    float secY = 564.0f;
    std::string libTitle = fmtKey(i18n::Key::TabApps);
    if (cfg.lang == static_cast<int>(i18n::Lang::Fr)) libTitle = fmt("Bibliothèque");
    else if (cfg.lang == static_cast<int>(i18n::Lang::En)) libTitle = fmt("Library");
    else if (cfg.lang == static_cast<int>(i18n::Lang::Ja)) libTitle = "ライブラリ";
    else if (cfg.lang == static_cast<int>(i18n::Lang::Es)) libTitle = fmt("Biblioteca");
    else if (cfg.lang == static_cast<int>(i18n::Lang::De)) libTitle = fmt("Bibliothek");

    r.drawTextBoxed(gfx::Font::Semi, 36.0f, kMarginX + slideX, secY, 56.0f, libTitle.c_str(),
                    gfx::hex(0xFFFFFF, 0.96f), gfx::Align::Left);
    char cntStr[48];
    std::snprintf(cntStr, sizeof(cntStr), "%zu %s", visibleIdx.size(), fmtKey(i18n::Key::AppsCount).c_str());
    float titleEnd = kMarginX + slideX + r.measureText(gfx::Font::Semi, 36.0f, libTitle.c_str()) + 20.0f;
    r.drawTextBoxed(gfx::Font::Sans, 22.0f, titleEnd, secY + 4.0f, 52.0f, cntStr,
                    gfx::hex(0xFFFFFF, 0.50f), gfx::Align::Left);

    // Compact FTP Status Pill cleanly placed in Section Header Row (NEVER overlaps Hero icon!)
    std::string ip = status.ipAddress.empty() ? "10.77.0.4" : status.ipAddress;
    std::string ftpTxt = ftpSrv.isRunning()
                             ? ("ftp://" + ip + ":" + std::to_string(cfg.ftpPort()) + " (-)")
                             : (fmtKey(i18n::Key::FtpStandby) + " (-)");
    bool hFtp = in.pointerVisible && gfx::Rect{958, secY, 382, 56}.contains(in.mx, in.my);
    r.drawSoftCard(958 + slideX, secY, 382, 56, 28.0f,
                   gfx::rgba(255, 224, 238, hFtp ? 0.14f : 0.08f), gfx::rgba(255, 224, 238, 0.10f));
    float pulse = ftpSrv.isRunning() ? (0.65f + 0.35f * std::sin(static_cast<float>(timeSec) * 3.8f)) : 0.30f;
    r.fillCircle(988.0f + slideX, secY + 28.0f, 5.5f, ftpSrv.isRunning() ? gfx::hex(0x6EE7B7, pulse) : gfx::hex(0xFFFFFF, 0.30f));
    r.drawTextBoxed(gfx::Font::Semi, 21.0f, 1008.0f + slideX, secY, 56.0f, ftpTxt.c_str(),
                    gfx::hex(0xFFFFFF, 0.90f), gfx::Align::Left);

    // Favorites filter pill — explicitly mapped to (ZL)!
    r.drawSoftCard(1356 + slideX, secY, 244, 56, 28.0f,
                   favOnly ? gfx::withAlpha(th.accent, 0.28f) : gfx::rgba(255, 224, 238, 0.08f),
                   favOnly ? gfx::withAlpha(th.accent, 0.55f) : gfx::rgba(255, 224, 238, 0.08f));
    r.drawStar(1390.0f + slideX, secY + 28.0f, 9.5f, favOnly, favOnly ? th.accent : gfx::hex(0xFFFFFF, 0.78f));
    std::string favBtnLbl = fmtKey(i18n::Key::Favorites) + " (ZL)";
    r.drawTextBoxed(gfx::Font::Semi, 21.0f, 1412.0f + slideX, secY, 56.0f, favBtnLbl.c_str(),
                    gfx::hex(0xFFFFFF, 0.94f), gfx::Align::Left);

    // See all pill — explicitly mapped to (ZR)!
    r.drawSoftCard(1616 + slideX, secY, 248, 56, 28.0f, gfx::rgba(255, 224, 238, 0.10f), gfx::rgba(255, 224, 238, 0.10f));
    r.drawUiIcon(gfx::UiIcon::ArrowRight, 1650.0f + slideX, secY + 28.0f, 17.0f, gfx::hex(0xFFFFFF, 0.90f));
    std::string allBtnLbl = fmtKey(i18n::Key::SeeAll) + " (ZR)";
    r.drawTextBoxed(gfx::Font::Semi, 21.0f, 1672.0f + slideX, secY, 56.0f, allBtnLbl.c_str(),
                    gfx::hex(0xFFFFFF, 0.94f), gfx::Align::Left);

    // 3. Borderless Velvet App Rail (smooth listFade on filter/sort changes!)
    float lf = easeOutCubic(listFade);
    float listOffY = (1.0f - lf) * 18.0f;
    r.pushAlpha(lf);
    for (int i = 0; i < static_cast<int>(visibleIdx.size()); ++i) {
        const auto &a = allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(i)])];
        float cx = kRailPadX + slideX + i * kCardStep - railScrollX;
        if (cx + kCardW < -80.0f || cx > plat::kW + 80.0f) continue;

        float fa = std::clamp(a.focusAnim, 0.0f, 1.0f);
        float lift = fa * 14.0f + a.hoverAnim * 5.0f;
        float cy = kRailY - lift + listOffY;

        // Smooth cross-faded dark shadow <-> accent glow (ZERO sudden pop when unfocusing!)
        if (fa < 0.99f) {
            r.drawShadow(cx, cy + 10, kCardW, kCardH, 18.0f, 26.0f, gfx::rgba(0, 0, 0, 0.28f * (1.0f - fa)));
        }
        if (fa > 0.005f) {
            r.drawShadow(cx + 8, cy + 12, kCardW - 16, kCardH - 8, 36.0f, 28.0f,
                         gfx::withAlpha(th.accent, 0.36f * fa));
        }

        gfx::Color cFill = gfx::lerpColor(gfx::rgba(255, 220, 236, 0.05f), gfx::rgba(255, 220, 236, 0.13f), fa);
        gfx::Color cRim = gfx::lerpColor(gfx::rgba(255, 220, 236, 0.04f), gfx::withAlpha(th.accent, 0.62f), fa);
        r.drawSoftCard(cx, cy, kCardW, kCardH, 28.0f, cFill, cRim);

        float isz = 214.0f;
        float ix = cx + (kCardW - isz) * 0.5f;
        float iy = cy + 18.0f;
        if (a.icon.id) {
            r.drawShadow(ix, iy + 8, isz, isz, 18, 28, gfx::rgba(0, 0, 0, 0.38f));
            r.drawRoundedTexture(a.icon, ix, iy, isz, isz, 28.0f, {1, 1, 1, 1}, 0.06f);
        } else {
            r.drawGradientPill(ix, iy, isz, isz, 28.0f, a.colorA, a.colorB);
            char mono[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(a.name.empty() ? 'H' : a.name[0]))), 0};
            r.drawTextBoxed(gfx::Font::Semi, 86.0f, ix + isz * 0.5f, iy, isz, mono, gfx::hex(0xFFFFFF), gfx::Align::Center);
        }

        if (a.favorite) {
            r.drawStar(cx + kCardW - 28.0f, cy + 28.0f, 11.0f, true, th.accent);
        }

        r.drawTextClipped(gfx::Font::Semi, 26.0f, cx + kCardW * 0.5f, cy + 248.0f, kCardW - 32.0f,
                          fmt(a.name).c_str(), gfx::hex(0xFFFFFF, 0.84f + 0.15f * fa), gfx::Align::Center);
        std::string sub = nro::formatBytes(a.fileSize, cfg.lang) + "  •  v" + a.version;
        r.drawTextClipped(gfx::Font::Sans, 19.0f, cx + kCardW * 0.5f, cy + 288.0f, kCardW - 32.0f,
                          sub.c_str(), gfx::hex(0xFFFFFF, 0.48f + 0.14f * fa), gfx::Align::Center);
    }
    r.popAlpha();

    // Subtle bottom control legend bar for docked / gamepad clarity
    r.drawTextBoxed(gfx::Font::Sans, 19.0f, plat::kW * 0.5f + slideX, 1022.0f, 40.0f,
                    fmtKey(i18n::Key::DetailsHint).c_str(), gfx::hex(0xFFFFFF, 0.40f), gfx::Align::Center);

    r.popAlpha();
}

void App::renderAppDrawerView(gfx::Renderer &r, const plat::Input &in) {
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    float vf = easeOutCubic(viewFade);
    float slideX = (1.0f - vf) * 32.0f * tabDir;
    r.pushAlpha(vf);

    r.drawTextCleanReveal(cfg.serifTitles ? gfx::Font::Serif : gfx::Font::Semi, cfg.serifTitles ? 70.0f : 50.0f,
                          84.0f + slideX, 138.0f, 700.0f, fmtKey(i18n::Key::TabApps).c_str(), gfx::hex(0xFFFFFF), viewFade);

    const i18n::Key sortKeys[3] = {i18n::Key::SortRecent, i18n::Key::SortAZ, i18n::Key::SortSize};
    auto chip = [&](float bx, float bw, const std::string &lbl, bool active) {
        bool hov = in.pointerVisible && gfx::Rect{bx, 146, bw, 60}.contains(in.mx, in.my);
        r.drawSoftCard(bx + slideX, 146, bw, 60, 30.0f,
                       active ? gfx::withAlpha(th.accent, 0.26f) : gfx::rgba(255, 224, 238, hov ? 0.16f : 0.09f),
                       active ? gfx::withAlpha(th.accent, 0.55f) : gfx::rgba(255, 224, 238, 0.08f));
        r.drawTextBoxed(gfx::Font::Semi, 21.0f, bx + slideX + bw * 0.5f, 146.0f, 60.0f, lbl.c_str(),
                        gfx::hex(0xFFFFFF, 0.94f), gfx::Align::Center);
    };
    // Every filter/sort/search pill explicitly shows its gamepad trigger!
    chip(1012, 246, fmtKey(i18n::Key::Favorites) + " (ZL)", favOnly);
    chip(1274, 286, fmtKey(sortKeys[cfg.sortMode]) + " (ZR)", false);
    chip(1576, 260, (searchFilter.empty() ? fmtKey(i18n::Key::Search) : searchFilter) + " (X)", !searchFilter.empty());

    float lf = easeOutCubic(listFade);
    float listOffY = (1.0f - lf) * 18.0f;
    r.pushAlpha(lf);
    r.pushClip(40.0f, kDrawerY0 - 16.0f, plat::kW - 80.0f, kDrawerViewH + 30.0f);
    if (visibleIdx.empty()) {
        r.drawTextBoxed(gfx::Font::Semi, 30.0f, plat::kW * 0.5f + slideX, kDrawerY0, kDrawerViewH,
                        fmtKey(i18n::Key::NoAppFound).c_str(), gfx::hex(0xFFFFFF, 0.55f), gfx::Align::Center);
    } else {
        for (int i = 0; i < static_cast<int>(visibleIdx.size()); ++i) {
            int col = i % kDrawerCols, row = i / kDrawerCols;
            float cx = kDrawerX0 + slideX + col * kDrawerStepX;
            float cy = kDrawerY0 + row * kDrawerStepY - drawerScrollY + listOffY;
            if (cy + kDrawerCellH < kDrawerY0 - 60.0f || cy > plat::kH + 60.0f) continue;

            const auto &a = allApps[static_cast<size_t>(visibleIdx[static_cast<size_t>(i)])];
            float fa = std::clamp(a.focusAnim, 0.0f, 1.0f);
            float lift = fa * 10.0f + a.hoverAnim * 4.0f;
            float y0 = cy - lift;

            if (fa > 0.005f) {
                r.drawShadow(cx + 8, y0 + 10, kDrawerCellW - 16, kDrawerCellH - 8, 32.0f, 28.0f,
                             gfx::withAlpha(th.accent, 0.36f * fa));
            }
            gfx::Color cFill = gfx::lerpColor(gfx::rgba(255, 220, 236, 0.045f), gfx::rgba(255, 220, 236, 0.13f), fa);
            gfx::Color cRim = gfx::lerpColor(gfx::rgba(255, 220, 236, 0.0f), gfx::withAlpha(th.accent, 0.62f), fa);
            r.drawSoftCard(cx, y0, kDrawerCellW, kDrawerCellH, 28.0f, cFill, cRim);

            float isz = 208.0f;
            float ix = cx + (kDrawerCellW - isz) * 0.5f;
            float iy = y0 + 20.0f;
            if (a.icon.id) {
                r.drawShadow(ix, iy + 8, isz, isz, 16, 28, gfx::rgba(0, 0, 0, 0.38f));
                r.drawRoundedTexture(a.icon, ix, iy, isz, isz, 28.0f, {1, 1, 1, 1}, 0.06f);
            } else {
                r.drawGradientPill(ix, iy, isz, isz, 28.0f, a.colorA, a.colorB);
                char mono[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(a.name.empty() ? 'H' : a.name[0]))), 0};
                r.drawTextBoxed(gfx::Font::Semi, 84.0f, ix + isz * 0.5f, iy, isz, mono, gfx::hex(0xFFFFFF), gfx::Align::Center);
            }

            if (a.favorite) r.drawStar(cx + kDrawerCellW - 28.0f, y0 + 28.0f, 11.0f, true, th.accent);

            r.drawTextClipped(gfx::Font::Semi, 26.0f, cx + kDrawerCellW * 0.5f, y0 + 248.0f, kDrawerCellW - 28.0f,
                              fmt(a.name).c_str(), gfx::hex(0xFFFFFF, 0.96f), gfx::Align::Center);
        }
    }
    r.popClip();
    r.popAlpha();
    r.popAlpha();
}

void App::renderDetailPage(gfx::Renderer &r, const plat::Input &in) {
    if (detailAppIdx < 0 || detailAppIdx >= static_cast<int>(allApps.size())) return;
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    const auto &a = allApps[static_cast<size_t>(detailAppIdx)];
    float da = easeOutCubic(detailAnim);
    float up = (1.0f - da) * 34.0f;

    r.pushAlpha(da);
    r.fillRect(0, 0, plat::kW, plat::kH, gfx::withAlpha(th.bgTop, 0.44f));

    // Back pill (slides smoothly on both open & close!)
    bool hBack = in.pointerVisible && gfx::Rect{56, 126, 224, 62}.contains(in.mx, in.my);
    r.drawLiquidGlass(56, 126 + up * 0.35f, 224, 62, 31.0f, th.glassTint, hBack ? 0.35f : 0.0f, gfx::withAlpha(th.accent, 0.25f));
    r.drawUiIcon(gfx::UiIcon::ArrowLeft, 92.0f, 157.0f + up * 0.35f, 20.0f, gfx::hex(0xFFFFFF));
    r.drawTextBoxed(gfx::Font::Semi, 23.0f, 116.0f, 126.0f + up * 0.35f, 62.0f, fmtKey(i18n::Key::BackB).c_str(),
                    gfx::hex(0xFFFFFF), gfx::Align::Left);

    // Main Real-Blur Liquid Glass Showcase Panel
    float cx = 56.0f, cy = 212.0f + up, cw = 1808.0f, ch = 788.0f;
    r.drawShadow(cx, cy + 16, cw, ch, 44, 36, gfx::rgba(0, 0, 0, 0.46f));
    r.drawLiquidGlass(cx, cy, cw, ch, 36.0f, th.glassTint, 0.0f, gfx::withAlpha(th.accent, 0.20f));

    // Left 380x380 Icon
    float ix = cx + 64.0f, iy = cy + 100.0f, isz = 380.0f;
    r.drawShadow(ix, iy + 14, isz, isz, 36, 40, gfx::rgba(0, 0, 0, 0.48f));
    if (a.icon.id) {
        r.drawRoundedTexture(a.icon, ix, iy, isz, isz, 40.0f, {1, 1, 1, 1}, 0.08f);
    } else {
        r.drawGradientPill(ix, iy, isz, isz, 40.0f, a.colorA, a.colorB);
        char mono[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(a.name.empty() ? 'H' : a.name[0]))), 0};
        r.drawTextBoxed(gfx::Font::Semi, 138.0f, ix + isz * 0.5f, iy, isz, mono, gfx::hex(0xFFFFFF), gfx::Align::Center);
    }

    // Right column
    float rx = 556.0f;
    r.drawTextCleanReveal(cfg.serifTitles ? gfx::Font::Serif : gfx::Font::Semi, cfg.serifTitles ? 104.0f : 78.0f,
                          rx, cy + 48.0f, 1220.0f, fmt(a.name).c_str(), gfx::hex(0xFFFFFF), detailAnim);

    struct InfoBox { std::string label; std::string value; };
    InfoBox infos[4] = {
        {fmtKey(i18n::Key::Author), a.author},
        {fmtKey(i18n::Key::Version), "v" + a.version},
        {fmtKey(i18n::Key::Size), nro::formatBytes(a.fileSize, cfg.lang)},
        {fmtKey(i18n::Key::Format), a.nroFlags == 0 ? "NRO0 · OK (flags=0)" : ("NRO0 · flags=" + std::to_string(a.nroFlags))},
    };
    for (int i = 0; i < 4; ++i) {
        float bx = rx + (i % 2) * 590.0f;
        float by0 = cy + 178.0f + (i / 2) * 108.0f;
        r.drawSoftCard(bx, by0, 566.0f, 92.0f, 22.0f, gfx::rgba(0, 0, 0, 0.24f), gfx::rgba(255, 224, 238, 0.06f));
        r.drawText(gfx::Font::Sans, 19.0f, bx + 26.0f, by0 + 16.0f, infos[i].label.c_str(), gfx::hex(0xFFFFFF, 0.54f));
        r.drawTextClipped(gfx::Font::Semi, 27.0f, bx + 26.0f, by0 + 46.0f, 514.0f, infos[i].value.c_str(), gfx::hex(0xFFFFFF, 0.96f));
    }

    // Path bar
    r.drawSoftCard(rx, cy + 404.0f, 1156.0f, 62.0f, 18.0f, gfx::rgba(0, 0, 0, 0.24f), gfx::rgba(255, 224, 238, 0.06f));
    r.drawTextBoxed(gfx::Font::Sans, 23.0f, rx + 26.0f, cy + 404.0f, 62.0f, a.displayPath.c_str(),
                    gfx::hex(0xFFFFFF, 0.78f), gfx::Align::Left);

    // Action row
    float by = 738.0f + up;
    r.drawShadow(556, by + 8, 290, 76, 26, 38, gfx::withAlpha(th.accent, 0.42f));
    r.fillRoundedRect(556, by, 290, 76, 38.0f, th.accent);
    r.drawUiIcon(gfx::UiIcon::Play, 610.0f, by + 38.0f, 23.0f, th.bgTop);
    r.drawTextBoxed(gfx::Font::Semi, 26.0f, 640.0f, by, 76.0f, fmtKey(i18n::Key::LaunchA).c_str(), th.bgTop, gfx::Align::Left);

    bool hFav = in.pointerVisible && gfx::Rect{866, by, 270, 76}.contains(in.mx, in.my);
    r.drawSoftCard(866, by, 270, 76, 38.0f, gfx::rgba(255, 224, 238, hFav ? 0.18f : 0.10f), gfx::rgba(255, 224, 238, 0.10f));
    r.drawStar(906.0f, by + 38.0f, 12.0f, a.favorite, a.favorite ? th.accent : gfx::hex(0xFFFFFF, 0.85f));
    r.drawTextBoxed(gfx::Font::Semi, 24.0f, 932.0f, by, 76.0f,
                    fmtKey(a.favorite ? i18n::Key::FavoriteY : i18n::Key::PinY).c_str(),
                    gfx::hex(0xFFFFFF, 0.95f), gfx::Align::Left);

    bool hDir = in.pointerVisible && gfx::Rect{1156, by, 360, 76}.contains(in.mx, in.my);
    r.drawSoftCard(1156, by, 360, 76, 38.0f, gfx::rgba(255, 224, 238, hDir ? 0.18f : 0.10f), gfx::rgba(255, 224, 238, 0.10f));
    r.drawUiIcon(gfx::UiIcon::Folder, 1198.0f, by + 38.0f, 22.0f, gfx::hex(0xFFFFFF, 0.88f));
    r.drawTextBoxed(gfx::Font::Semi, 24.0f, 1226.0f, by, 76.0f, fmtKey(i18n::Key::OpenFolderX).c_str(),
                    gfx::hex(0xFFFFFF, 0.95f), gfx::Align::Left);

    r.popAlpha();
}

void App::renderFtpView(gfx::Renderer &r, const plat::Input &) {
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    float vf = easeOutCubic(viewFade);
    float slideX = (1.0f - vf) * 32.0f * tabDir;
    r.pushAlpha(vf);

    float px = 220.0f + slideX, py = 144.0f, pw = 1480.0f, ph = 870.0f;
    r.drawShadow(px, py + 14, pw, ph, 38, 34, gfx::rgba(0, 0, 0, 0.44f));
    r.drawLiquidGlass(px, py, pw, ph, 34.0f, th.glassTint, 0.0f, gfx::withAlpha(th.accent, 0.18f));

    r.drawTextCleanReveal(cfg.serifTitles ? gfx::Font::Serif : gfx::Font::Semi, cfg.serifTitles ? 74.0f : 56.0f,
                          px + 56.0f, py + 32.0f, 920.0f, fmtKey(i18n::Key::FtpTitle).c_str(), gfx::hex(0xFFFFFF), viewFade);
    r.drawText(gfx::Font::Sans, 24.0f, px + 58.0f, py + 122.0f,
               T(i18n::Key::FtpSubtitle), gfx::hex(0xFFFFFF, 0.65f));

    bool on = ftpSrv.isRunning();
    r.fillRoundedRect(1240 + slideX, 206, 360, 68, 34.0f, on ? gfx::hex(0x10B981) : th.accent);
    r.drawTextBoxed(gfx::Font::Semi, 24.0f, 1420.0f + slideX, 206.0f, 68.0f,
                    fmtKey(on ? i18n::Key::FtpStopBtn : i18n::Key::FtpStartBtn).c_str(),
                    on ? gfx::hex(0xFFFFFF) : th.bgTop, gfx::Align::Center);

    r.drawSoftCard(px + 56, py + 190, 668, 152, 24.0f, gfx::rgba(0, 0, 0, 0.24f), gfx::rgba(255, 224, 238, 0.07f));
    r.drawText(gfx::Font::Sans, 21.0f, px + 84, py + 214, fmtKey(i18n::Key::FtpConnAddr).c_str(), gfx::hex(0xFFFFFF, 0.56f));
    std::string ip = status.ipAddress.empty() ? "10.77.0.4" : status.ipAddress;
    std::string addr = on ? ("ftp://" + ip + ":" + std::to_string(cfg.ftpPort())) : fmtKey(i18n::Key::FtpServerIdle);
    r.drawText(gfx::Font::Semi, 36.0f, px + 84, py + 258, addr.c_str(), gfx::hex(0xFFFFFF));

    r.drawSoftCard(px + 756, py + 190, 668, 152, 24.0f, gfx::rgba(0, 0, 0, 0.24f), gfx::rgba(255, 224, 238, 0.07f));
    r.drawText(gfx::Font::Sans, 21.0f, px + 784, py + 214, fmtKey(i18n::Key::FtpStorage).c_str(), gfx::hex(0xFFFFFF, 0.56f));
    std::string sdTxt = nro::formatBytes(status.sdFreeBytes, cfg.lang) + " " + fmtKey(i18n::Key::FreeSpace) + " / " +
                        nro::formatBytes(status.sdTotalBytes, cfg.lang);
    r.drawText(gfx::Font::Semi, 31.0f, px + 784, py + 252, sdTxt.c_str(), gfx::hex(0xFFFFFF));
    float usedRatio = (status.sdTotalBytes > 0)
                          ? std::clamp(1.0f - static_cast<float>(status.sdFreeBytes) / static_cast<float>(status.sdTotalBytes), 0.0f, 1.0f)
                          : 0.0f;
    r.fillRoundedRect(px + 784, py + 310, 612, 10, 5.0f, gfx::rgba(255, 224, 238, 0.14f));
    r.fillRoundedRect(px + 784, py + 310, std::max(12.0f, 612.0f * usedRatio), 10, 5.0f, th.accent);

    r.drawSoftCard(px + 56, py + 370, pw - 112, 366, 24.0f, gfx::rgba(0, 0, 0, 0.24f), gfx::rgba(255, 224, 238, 0.07f));
    r.drawText(gfx::Font::Sans, 21.0f, px + 84, py + 394, fmtKey(i18n::Key::FtpLiveLog).c_str(), gfx::hex(0xFFFFFF, 0.54f));
    auto logs = ftpSrv.recentLogs();
    float ly = py + 438.0f;
    for (size_t i = (logs.size() > 7 ? logs.size() - 7 : 0); i < logs.size(); ++i) {
        r.drawTextClipped(gfx::Font::Sans, 23.0f, px + 84, ly, pw - 168, logs[i].c_str(), gfx::hex(0xFFFFFF, 0.88f));
        ly += 40.0f;
    }

    r.drawSoftCard(280 + slideX, 916, 390, 64, 32.0f, gfx::rgba(255, 224, 238, 0.12f), gfx::rgba(255, 224, 238, 0.12f));
    r.drawTextBoxed(gfx::Font::Semi, 23.0f, 475.0f + slideX, 916.0f, 64.0f,
                    fmtKey(i18n::Key::FtpRefreshBtn).c_str(), gfx::hex(0xFFFFFF), gfx::Align::Center);

    std::string portBtn = fmtKey(i18n::Key::FtpPortBtn) + " : " + std::to_string(cfg.ftpPort());
    r.drawSoftCard(696 + slideX, 916, 310, 64, 32.0f, gfx::rgba(255, 224, 238, 0.12f), gfx::rgba(255, 224, 238, 0.12f));
    r.drawTextBoxed(gfx::Font::Semi, 23.0f, 851.0f + slideX, 916.0f, 64.0f,
                    portBtn.c_str(), gfx::hex(0xFFFFFF), gfx::Align::Center);
    r.popAlpha();
}

void App::renderSettingsView(gfx::Renderer &r, const plat::Input &) {
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    float vf = easeOutCubic(viewFade);
    float slideX = (1.0f - vf) * 32.0f * tabDir;
    r.pushAlpha(vf);

    float px = 220.0f + slideX, py = 132.0f, pw = 1480.0f, ph = 900.0f;
    r.drawShadow(px, py + 14, pw, ph, 38, 34, gfx::rgba(0, 0, 0, 0.44f));
    r.drawLiquidGlass(px, py, pw, ph, 34.0f, th.glassTint, 0.0f, gfx::withAlpha(th.accent, 0.18f));

    r.drawTextCleanReveal(cfg.serifTitles ? gfx::Font::Serif : gfx::Font::Semi, cfg.serifTitles ? 72.0f : 56.0f,
                          px + 56, py + 24, 700.0f, fmtKey(i18n::Key::SettingsTitle).c_str(), gfx::hex(0xFFFFFF), viewFade);
    r.drawText(gfx::Font::Sans, 22.0f, px + 58, py + 106,
               T(i18n::Key::SettingsSubtitle), gfx::hex(0xFFFFFF, 0.62f));

    const i18n::Key glowKeys[3] = {i18n::Key::GlowSoft, i18n::Key::GlowNormal, i18n::Key::GlowIntense};
    struct Row { std::string title; std::string val; bool danger = false; };
    Row rows[kSettingsCount] = {
        {fmtKey(i18n::Key::SetLanguage), i18n::langDisplayName(cfg.lang)},
        {fmtKey(i18n::Key::SetTheme), fmtKey(kThemes[static_cast<size_t>(cfg.themeIdx % 6)].nameKey)},
        {fmtKey(i18n::Key::SetStartupTab), fmtKey(cfg.startupTab == 0 ? i18n::Key::TabHome : i18n::Key::TabApps)},
        {fmtKey(i18n::Key::SetClockFormat), fmtKey(cfg.clock24h ? i18n::Key::Clock24h : i18n::Key::Clock12h)},
        {fmtKey(i18n::Key::SetBatteryPct), fmtKey(cfg.showBatteryPct ? i18n::Key::On : i18n::Key::Off)},
        {fmtKey(i18n::Key::SetGlow), fmtKey(glowKeys[cfg.glowLevel % 3])},
        {fmtKey(i18n::Key::SetTitleStyle), fmtKey(cfg.serifTitles ? i18n::Key::TitleSerif : i18n::Key::TitleSans)},
        {fmtKey(i18n::Key::SetLowercase), fmtKey(cfg.lowercase ? i18n::Key::On : i18n::Key::Off)},
        {fmtKey(i18n::Key::SetHideSelf), fmtKey(cfg.hideSelf ? i18n::Key::On : i18n::Key::Off)},
        {fmtKey(i18n::Key::SetFtpAuto), fmtKey(cfg.ftpAutoStart ? i18n::Key::On : i18n::Key::Off)},
        {fmtKey(i18n::Key::SetFtpPort), std::to_string(cfg.ftpPort())},
        {fmtKey(i18n::Key::SetSounds), fmtKey(cfg.soundsEnabled ? i18n::Key::SoundsOn : i18n::Key::SoundsMuted)},
        {fmtKey(i18n::Key::SetExit), fmtKey(i18n::Key::ExitDesc), true},
    };

    r.pushClip(px + 32.0f, kSetListY, pw - 64.0f, kSetListH);
    for (int i = 0; i < kSettingsCount; ++i) {
        float ry = kSetListY + 8.0f + i * kSetRowStep - settingsScrollY;
        if (ry + kSetRowH < kSetListY - 20.0f || ry > kSetListY + kSetListH + 20.0f) continue;
        float rx = 276.0f + slideX, rw = 1352.0f;
        bool sel = (settingsCursor == i);
        if (sel) {
            r.drawShadow(rx, ry + 6, rw, kSetRowH, 20, 24, gfx::withAlpha(th.accent, 0.24f));
            r.drawLiquidGlass(rx, ry, rw, kSetRowH, 24.0f, gfx::withAlpha(th.accent, 0.16f), 0.78f,
                              gfx::withAlpha(th.accent, 0.78f));
        } else {
            r.drawSoftCard(rx, ry, rw, kSetRowH, 24.0f, gfx::rgba(0, 0, 0, 0.20f), gfx::rgba(255, 224, 238, 0.05f));
        }
        r.drawTextBoxed(sel ? gfx::Font::Semi : gfx::Font::Sans, 26.0f, rx + 34.0f, ry, kSetRowH, rows[i].title.c_str(),
                        rows[i].danger ? gfx::hex(0xFCA5A5) : gfx::hex(0xFFFFFF, sel ? 0.99f : 0.84f), gfx::Align::Left);

        float popOff = (settingPopIdx == i) ? (1.0f - easeOutCubic(settingPopAnim)) * 14.0f : 0.0f;
        std::string valDisp = sel && !rows[i].danger ? ("‹  " + rows[i].val + "  ›") : rows[i].val;
        r.drawTextBoxed(gfx::Font::Semi, 25.0f, rx + rw - 34.0f + popOff, ry, kSetRowH, valDisp.c_str(),
                        sel ? th.accent : gfx::hex(0xFFFFFF, 0.64f), gfx::Align::Right);
    }
    r.popClip();

    // Minimal glass scrollbar indicator on right edge
    float contentH = kSettingsCount * kSetRowStep + 16.0f;
    float maxScroll = std::max(1.0f, contentH - kSetListH);
    float thumbH = std::max(60.0f, kSetListH * (kSetListH / contentH));
    float thumbY = kSetListY + (kSetListH - thumbH) * std::clamp(settingsScrollY / maxScroll, 0.0f, 1.0f);
    r.fillRoundedRect(px + pw - 36.0f, kSetListY + 8.0f, 6.0f, kSetListH - 16.0f, 3.0f, gfx::rgba(255, 224, 238, 0.08f));
    r.fillRoundedRect(px + pw - 36.0f, thumbY, 6.0f, thumbH, 3.0f, gfx::withAlpha(th.accent, 0.55f));

    r.popAlpha();
}

void App::renderToast(gfx::Renderer &r) {
    if (toast.timer <= 0.0f) return;
    const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
    float enter = std::min(1.0f, (toast.dur - toast.timer) * 5.0f);
    float exit = std::min(1.0f, toast.timer * 4.0f);
    float a = easeOutCubic(std::min(enter, exit));

    float tw = 520.0f, thH = 92.0f;
    float tx = plat::kW - kMarginX - tw;
    float ty = 118.0f - (1.0f - a) * 20.0f;
    r.pushAlpha(a);
    r.drawShadow(tx, ty + 8, tw, thH, 26, 26, gfx::rgba(0, 0, 0, 0.44f));
    r.drawLiquidGlass(tx, ty, tw, thH, 26.0f, th.glassTint, 0.25f, gfx::withAlpha(th.accent, 0.45f));
    r.fillCircle(tx + 30.0f, ty + thH * 0.5f, 7.0f, th.accent);
    r.drawTextClipped(gfx::Font::Semi, 24.0f, tx + 54.0f, ty + 16.0f, tw - 72.0f, fmt(toast.title).c_str(), gfx::hex(0xFFFFFF));
    r.drawTextClipped(gfx::Font::Sans, 21.0f, tx + 54.0f, ty + 48.0f, tw - 72.0f, toast.body.c_str(), gfx::hex(0xFFFFFF, 0.76f));
    r.popAlpha();
}

void App::render(gfx::Renderer &r, const plat::Input &in) {
    // PASS 1: Render background + base content into offscreen FBO
    renderBackground(r);
    if (tab == Tab::Home) {
        renderHomeView(r, in);
    } else if (tab == Tab::Apps) {
        renderAppDrawerView(r, in);
    }

    // PASS 2: Real-time 3-pass Kawase Blur of backdrop + present crisp scene
    r.captureBackdropAndPresentScene();

    // PASS 3: True Real-Time Backdrop-Blurred Liquid Glass surfaces on top
    renderTopBar(r, in);

    if (detailAppIdx >= 0) {
        renderDetailPage(r, in);
    } else if (tab == Tab::Files) {
        const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
        float vf = easeOutCubic(viewFade);
        float slideX = (1.0f - vf) * 32.0f * tabDir;
        r.pushAlpha(vf);
        fileBrowser.render(r, in, th.glassTint, th.accent, th.accentDeep, slideX, viewFade,
                           [this](const std::string &s) { return fmt(s); }, cfg.serifTitles);
        r.popAlpha();
    } else if (tab == Tab::Ftp) {
        renderFtpView(r, in);
    } else if (tab == Tab::Settings) {
        renderSettingsView(r, in);
    }

    renderToast(r);

    if (in.pointerVisible) r.drawCursor(in.mx, in.my);

    if (launching) {
        const auto &th = kThemes[static_cast<size_t>(cfg.themeIdx % 6)];
        float a = easeOutCubic(launchFade);
        r.fillRect(0, 0, plat::kW, plat::kH, gfx::withAlpha(th.bgTop, a * 0.96f));
        r.pushAlpha(a);
        r.drawTextBoxed(gfx::Font::Semi, 74.0f, plat::kW * 0.5f, plat::kH * 0.41f, 96.0f,
                        fmt(launchName).c_str(), gfx::hex(0xFFFFFF), gfx::Align::Center);
        r.drawTextBoxed(gfx::Font::Sans, 26.0f, plat::kW * 0.5f, plat::kH * 0.53f, 44.0f,
                        fmtKey(i18n::Key::Launching).c_str(), th.accent, gfx::Align::Center);
        r.popAlpha();
    }
}

}  // namespace tsuki

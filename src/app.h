#pragma once

#include "file_manager.h"
#include "ftp_server.h"
#include "gfx.h"
#include "i18n.h"
#include "nro_scanner.h"
#include "platform.h"

#include <string>
#include <vector>

namespace tsuki {

enum class Tab {
    Home = 0,       // Hero + horizontal rail + FTP capsule
    Apps = 1,       // Android-style vertical App Drawer (logo + name -> fullscreen detail page)
    Files = 2,      // Full SD File Manager
    Ftp = 3,        // Integrated FTP server
    Settings = 4,   // Customization & release preferences
};

struct Toast {
    std::string title;
    std::string body;
    float timer = 0.0f;
    float dur = 3.6f;
};

class App {
public:
    nro::Config cfg;
    std::vector<nro::AppEntry> allApps;
    std::vector<int> visibleIdx;
    ftp::Server ftpSrv;
    files::Browser fileBrowser;
    plat::StatusInfo status;

    Tab tab = Tab::Home;
    int selectedVis = 0;
    int drawerVis = 0;
    bool favOnly = false;
    int settingsCursor = 0;
    float settingsScrollY = 0.0f;
    std::string searchFilter;

    // Fullscreen App Detail Page (with smooth open AND close animations!)
    int detailAppIdx = -1;
    bool detailClosing = false;
    float detailAnim = 0.0f;

    // Smooth 120 FPS kinetic animations
    double timeSec = 0.0;
    float tabPillX = 0.0f;
    float tabPillW = 178.0f;
    float tabDir = 1.0f;

    // Home horizontal rail scroll
    float railScrollX = 0.0f;
    float railVelX = 0.0f;
    bool railDragging = false;
    float railDragLastX = 0.0f;
    float railDragAccum = 0.0f;

    // Android-style App Drawer vertical scroll
    float drawerScrollY = 0.0f;
    float drawerVelY = 0.0f;
    bool drawerDragging = false;
    float drawerDragLastY = 0.0f;
    float drawerDragAccum = 0.0f;

    float heroAnim = 1.0f;
    float viewFade = 1.0f;
    float listFade = 1.0f;
    float settingPopAnim = 1.0f;
    int settingPopIdx = -1;

    gfx::Color curGlowA = gfx::hex(0xF5A4C4);
    gfx::Color curGlowB = gfx::hex(0xB83B6E);

    Toast toast;
    float statusTimer = 0.0f;

    bool launching = false;
    float launchFade = 0.0f;
    std::string launchPath;
    std::string launchName;
    bool wantExit = false;

    void init();
    void shutdown();

    void update(const plat::Input &in, float dt);
    void render(gfx::Renderer &r, const plat::Input &in);

private:
    const char *T(i18n::Key k) const;
    std::string fmt(const std::string &s) const;
    std::string fmtKey(i18n::Key k) const;
    void rebuildVisible(const std::string &keepPath = "");
    void selectHomeIndex(int newVis, bool playSfx = true);
    void setTab(Tab t);
    void toggleFavoriteApp(int appIndex);
    void toggleFavOnly();
    void cycleSort(int dir);
    void cycleFtpPort(int dir);
    void toggleFtp();
    void openDetail(int appIndex);
    void closeDetail(bool immediate = false);
    void triggerLaunch(int appIndex);
    void triggerLaunchPath(const std::string &path);
    void pushToast(const std::string &title, const std::string &body);

    void renderBackground(gfx::Renderer &r);
    void renderTopBar(gfx::Renderer &r, const plat::Input &in);
    void renderHomeView(gfx::Renderer &r, const plat::Input &in);
    void renderAppDrawerView(gfx::Renderer &r, const plat::Input &in);
    void renderDetailPage(gfx::Renderer &r, const plat::Input &in);
    void renderFtpView(gfx::Renderer &r, const plat::Input &in);
    void renderSettingsView(gfx::Renderer &r, const plat::Input &in);
    void renderToast(gfx::Renderer &r);
};

}  // namespace tsuki

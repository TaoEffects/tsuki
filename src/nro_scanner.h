#pragma once

#include "gfx.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nro {

struct AppEntry {
    std::string path;         // sdmc:/switch/.../foo.nro
    std::string displayPath;  // /switch/.../foo.nro
    std::string name;
    std::string author;
    std::string version;
    uint64_t fileSize = 0;
    uint64_t mtime = 0;
    uint64_t lastLaunched = 0;
    uint32_t nroFlags = 0;
    bool favorite = false;

    gfx::Texture icon;
    gfx::Color colorA = gfx::hex(0x38BDF8);
    gfx::Color colorB = gfx::hex(0x6366F1);

    // Smooth UI animation state per card
    float focusAnim = 0.0f;
    float hoverAnim = 0.0f;
};

struct Config {
    int lang = 0;           // 0=Français, 1=English, 2=日本語, 3=Español, 4=Deutsch
    int themeIdx = 0;       // 0=Framboise, 1=Nuit, 2=Iris, 3=Lagon, 4=Pêche, 5=OLED
    int startupTab = 0;     // 0=Home, 1=Apps
    int glowLevel = 1;      // 0=Doux, 1=Normal, 2=Intense
    int sortMode = 0;       // 0=Récents, 1=A-Z, 2=Taille
    int ftpPortIdx = 0;     // 0=1337, 1=5000, 2=2121
    bool clock24h = true;
    bool showBatteryPct = true;
    bool lowercase = true;  // Yoru-style lowercase labels (ignored for Japanese)
    bool serifTitles = false;
    bool hideSelf = true;
    bool ftpAutoStart = true;
    bool soundsEnabled = true;
    std::unordered_set<std::string> favorites;
    std::unordered_map<std::string, uint64_t> launchTimes;

    uint16_t ftpPort() const {
        static constexpr uint16_t kPorts[3] = {1337, 5000, 2121};
        return kPorts[ftpPortIdx % 3];
    }
};

void loadConfig(Config &cfg);
void saveConfig(const Config &cfg);

void freeApps(std::vector<AppEntry> &apps);
void scanApps(std::vector<AppEntry> &apps, const Config &cfg);

std::string formatBytes(uint64_t bytes, int langIdx = 0);

}  // namespace nro

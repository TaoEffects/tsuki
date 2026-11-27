#include "nro_scanner.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace nro {
namespace {

constexpr const char *kConfigPath = "sdmc:/switch/tsuki/config.ini";

uint32_t readU32(const uint8_t *p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

uint64_t readU64(const uint8_t *p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

std::string readCString(const uint8_t *p, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char *>(p), n);
}

std::string lowerStr(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void fallbackColors(const std::string &name, gfx::Color &a, gfx::Color &b) {
    static const uint32_t kPairs[6][2] = {
        {0x38BDF8, 0x2563EB},
        {0xF472B6, 0x9333EA},
        {0x2DD4BF, 0x0F766E},
        {0xA78BFA, 0x4F46E5},
        {0xFB923C, 0xC2410C},
        {0x34D399, 0x047857},
    };
    size_t h = 0;
    for (char c : name) h = h * 31 + static_cast<unsigned char>(c);
    a = gfx::hex(kPairs[h % 6][0]);
    b = gfx::hex(kPairs[h % 6][1]);
}

bool parseNroFile(const std::string &path, int langIdx, AppEntry &out) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    uint8_t hdr[0x80]{};
    f.read(reinterpret_cast<char *>(hdr), sizeof(hdr));
    if (f.gcount() < 0x30) return false;
    if (std::memcmp(hdr + 0x10, "NRO0", 4) != 0) return false;

    uint32_t nroSize = readU32(hdr + 0x18);
    uint32_t flags = readU32(hdr + 0x1C);

    out.path = path;
    out.displayPath = (path.rfind("sdmc:", 0) == 0) ? path.substr(5) : path;
    out.fileSize = static_cast<uint64_t>(st.st_size);
    out.mtime = static_cast<uint64_t>(st.st_mtime);
    out.nroFlags = flags;

    // Default name from filename
    size_t sl = path.rfind('/');
    std::string base = (sl == std::string::npos) ? path : path.substr(sl + 1);
    if (base.size() > 4 && lowerStr(base.substr(base.size() - 4)) == ".nro") {
        base = base.substr(0, base.size() - 4);
    }
    out.name = base;
    out.author = "Homebrew Switch 2";
    out.version = "1.0";
    fallbackColors(out.name, out.colorA, out.colorB);

    // Check for ASET section right after nroSize
    if (nroSize > 0 && static_cast<uint64_t>(nroSize) + 0x38 <= out.fileSize) {
        f.seekg(static_cast<std::streamoff>(nroSize), std::ios::beg);
        uint8_t aset[0x38]{};
        f.read(reinterpret_cast<char *>(aset), sizeof(aset));
        if (f.gcount() == 0x38 && std::memcmp(aset, "ASET", 4) == 0) {
            uint64_t iconOff = readU64(aset + 0x08);
            uint64_t iconSz = readU64(aset + 0x10);
            uint64_t nacpOff = readU64(aset + 0x18);
            uint64_t nacpSz = readU64(aset + 0x20);

            if (iconSz > 32 && iconSz < 2 * 1024 * 1024 && nroSize + iconOff + iconSz <= out.fileSize) {
                std::vector<uint8_t> jpg(static_cast<size_t>(iconSz));
                f.seekg(static_cast<std::streamoff>(nroSize + iconOff), std::ios::beg);
                f.read(reinterpret_cast<char *>(jpg.data()), static_cast<std::streamsize>(iconSz));
                if (f) {
                    out.icon = gfx::decodeImageMemory(jpg.data(), jpg.size(), out.colorA, out.colorB);
                }
            }

            if (nacpSz >= 0x3070 && nroSize + nacpOff + 0x3070 <= out.fileSize) {
                std::vector<uint8_t> nacp(0x3070);
                f.seekg(static_cast<std::streamoff>(nroSize + nacpOff), std::ios::beg);
                f.read(reinterpret_cast<char *>(nacp.data()), static_cast<std::streamsize>(nacp.size()));
                if (f) {
                    // NACP language index: 0=en-US, 1=en-GB, 2=ja, 3=fr, 4=de, 5=es-419, 6=es
                    int prefNacp = 0;
                    if (langIdx == 0) prefNacp = 3;       // FR
                    else if (langIdx == 1) prefNacp = 0;  // EN
                    else if (langIdx == 2) prefNacp = 2;  // JA
                    else if (langIdx == 3) prefNacp = 6;  // ES
                    else if (langIdx == 4) prefNacp = 4;  // DE
                    const int order[5] = {prefNacp, 0, 1, 2, 3};
                    for (int lang : order) {
                        std::string nm = readCString(nacp.data() + lang * 0x300, 0x200);
                        std::string au = readCString(nacp.data() + lang * 0x300 + 0x200, 0x100);
                        if (!nm.empty()) {
                            out.name = nm;
                            if (!au.empty()) out.author = au;
                            break;
                        }
                    }
                    std::string ver = readCString(nacp.data() + 0x3060, 0x10);
                    if (!ver.empty()) out.version = ver;
                }
            }
        }
    }
    return true;
}

}  // namespace

void loadConfig(Config &cfg) {
    std::ifstream f(kConfigPath);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if (k == "lang") cfg.lang = std::clamp(std::atoi(v.c_str()), 0, 4);
        else if (k == "theme") cfg.themeIdx = std::clamp(std::atoi(v.c_str()), 0, 5);
        else if (k == "startup_tab") cfg.startupTab = std::clamp(std::atoi(v.c_str()), 0, 1);
        else if (k == "glow") cfg.glowLevel = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "sort") cfg.sortMode = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "ftp_port") cfg.ftpPortIdx = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "clock24h") cfg.clock24h = (v == "1");
        else if (k == "battery_pct") cfg.showBatteryPct = (v == "1");
        else if (k == "lowercase") cfg.lowercase = (v == "1");
        else if (k == "serif_titles") cfg.serifTitles = (v == "1");
        else if (k == "hide_self") cfg.hideSelf = (v == "1");
        else if (k == "ftp_auto") cfg.ftpAutoStart = (v == "1");
        else if (k == "sounds") cfg.soundsEnabled = (v == "1");
        else if (k == "joycon_mouse") cfg.joyconMouse = (v == "1");
        else if (k == "fav" && !v.empty()) cfg.favorites.insert(v);
        else if (k == "recent") {
            size_t sep = v.find('|');
            if (sep != std::string::npos) {
                cfg.launchTimes[v.substr(0, sep)] = std::strtoull(v.substr(sep + 1).c_str(), nullptr, 10);
            }
        }
    }
}

void saveConfig(const Config &cfg) {
    mkdir("sdmc:/switch/tsuki", 0777);
    std::ofstream f(kConfigPath, std::ios::trunc);
    if (!f) return;
    f << "lang=" << cfg.lang << "\n";
    f << "theme=" << cfg.themeIdx << "\n";
    f << "startup_tab=" << cfg.startupTab << "\n";
    f << "glow=" << cfg.glowLevel << "\n";
    f << "sort=" << cfg.sortMode << "\n";
    f << "ftp_port=" << cfg.ftpPortIdx << "\n";
    f << "clock24h=" << (cfg.clock24h ? 1 : 0) << "\n";
    f << "battery_pct=" << (cfg.showBatteryPct ? 1 : 0) << "\n";
    f << "lowercase=" << (cfg.lowercase ? 1 : 0) << "\n";
    f << "serif_titles=" << (cfg.serifTitles ? 1 : 0) << "\n";
    f << "hide_self=" << (cfg.hideSelf ? 1 : 0) << "\n";
    f << "ftp_auto=" << (cfg.ftpAutoStart ? 1 : 0) << "\n";
    f << "sounds=" << (cfg.soundsEnabled ? 1 : 0) << "\n";
    f << "joycon_mouse=" << (cfg.joyconMouse ? 1 : 0) << "\n";
    for (const auto &fav : cfg.favorites) f << "fav=" << fav << "\n";
    for (const auto &kv : cfg.launchTimes) f << "recent=" << kv.first << "|" << kv.second << "\n";
}

void freeApps(std::vector<AppEntry> &apps) {
    for (auto &a : apps) a.icon.destroy();
    apps.clear();
}

void scanApps(std::vector<AppEntry> &apps, const Config &cfg) {
    freeApps(apps);

    auto tryAdd = [&](const std::string &fullPath) {
        std::string low = lowerStr(fullPath);
        if (low.size() < 4 || low.substr(low.size() - 4) != ".nro") return;
        // Skip ourselves when hideSelf is enabled
        if (cfg.hideSelf) {
            if (low == "sdmc:/switch/tsuki/tsuki.nro" || low == "sdmc:/switch/tsuki.nro") return;
        }
        AppEntry e;
        if (parseNroFile(fullPath, cfg.lang, e)) {
            e.favorite = cfg.favorites.count(e.path) > 0;
            auto it = cfg.launchTimes.find(e.path);
            if (it != cfg.launchTimes.end()) e.lastLaunched = it->second;
            apps.push_back(std::move(e));
        }
    };

    DIR *root = opendir("sdmc:/switch");
    if (!root) return;
    while (struct dirent *de = readdir(root)) {
        if (!de->d_name[0] || de->d_name[0] == '.') continue;
        std::string full = std::string("sdmc:/switch/") + de->d_name;
        struct stat st{};
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            DIR *sub = opendir(full.c_str());
            if (!sub) continue;
            while (struct dirent *sde = readdir(sub)) {
                if (!sde->d_name[0] || sde->d_name[0] == '.') continue;
                tryAdd(full + "/" + sde->d_name);
            }
            closedir(sub);
        } else {
            tryAdd(full);
        }
    }
    closedir(root);
}

std::string formatBytes(uint64_t bytes, int langIdx) {
    const bool fr = (langIdx == 0);
    char buf[64];
    if (bytes < 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%llu %s", static_cast<unsigned long long>(bytes), fr ? "o" : "B");
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f %s", bytes / 1024.0, fr ? "Ko" : "KB");
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f %s", bytes / (1024.0 * 1024.0), fr ? "Mo" : "MB");
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", bytes / (1024.0 * 1024.0 * 1024.0), fr ? "Go" : "GB");
    }
    return buf;
}

}  // namespace nro

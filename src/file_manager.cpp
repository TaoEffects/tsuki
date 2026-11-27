#include "file_manager.h"
#include "audio.h"
#include "i18n.h"
#include "nro_scanner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace files {
namespace {

float easeOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

std::string normPath(std::string p) {
    if (p.empty()) return "sdmc:/";
    if (p.rfind("sdmc:/", 0) != 0) p = (p[0] == '/') ? ("sdmc:" + p) : ("sdmc:/" + p);
    while (p.size() > 6 && p.back() == '/') p.pop_back();
    return p;
}

std::string parentOf(const std::string &p) {
    std::string n = normPath(p);
    if (n == "sdmc:/") return n;
    size_t sl = n.rfind('/');
    return (sl == std::string::npos || sl <= 5) ? "sdmc:/" : n.substr(0, sl);
}

std::string joinPath(const std::string &dir, const std::string &name) {
    std::string n = normPath(dir);
    return n.back() == '/' ? (n + name) : (n + "/" + name);
}

std::string lowerExt(const std::string &s) {
    size_t dot = s.rfind('.');
    if (dot == std::string::npos) return "";
    std::string e = s.substr(dot);
    for (char &c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

bool isTextExt(const std::string &e) {
    return e == ".txt" || e == ".ini" || e == ".cfg" || e == ".log" || e == ".md" ||
           e == ".json" || e == ".toml" || e == ".yaml" || e == ".yml" || e == ".xml" ||
           e == ".c" || e == ".cpp" || e == ".h" || e == ".py" || e == ".rs" || e == ".sh" || e == ".csv";
}

bool isImgExt(const std::string &e) {
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp";
}

constexpr float kRowH = 74.0f;
constexpr float kListX = 112.0f;
constexpr float kListY = 336.0f;
constexpr float kListW = 1696.0f;
constexpr float kListH = 594.0f;

}  // namespace

void Browser::init(const std::string &startPath) {
    navigate(startPath);
}

void Browser::shutdown() {
    closePreview(true);
}

void Browser::closePreview(bool immediate) {
    if (immediate) {
        previewOpen = false;
        previewClosing = false;
        previewAnim = 0.0f;
        previewIsImage = false;
        previewTex.destroy();
        previewLines.clear();
        previewScroll = 0;
    } else if (previewOpen && !previewClosing) {
        previewClosing = true;
    }
}

void Browser::navigate(const std::string &path) {
    curPath = normPath(path);
    cursor = 0;
    scrollY = 0.0f;
    scrollVel = 0.0f;
    manualScroll = false;
    listFade = 0.0f;
    refresh();
}

void Browser::refresh() {
    entries.clear();
    DIR *d = opendir(curPath.c_str());
    if (!d) return;
    while (struct dirent *de = readdir(d)) {
        if (!de->d_name[0] || std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
        Entry e;
        e.name = de->d_name;
        e.fullPath = joinPath(curPath, e.name);
        struct stat st{};
        if (stat(e.fullPath.c_str(), &st) == 0) {
            e.isDir = S_ISDIR(st.st_mode);
            e.size = e.isDir ? 0 : static_cast<uint64_t>(st.st_size);
        } else {
            e.isDir = (de->d_type == DT_DIR);
        }
        if (e.isDir) {
            e.kind = 1;
        } else {
            std::string ext = lowerExt(e.name);
            if (ext == ".nro") e.kind = 2;
            else if (isTextExt(ext)) e.kind = 3;
            else if (isImgExt(ext)) e.kind = 4;
            else e.kind = 0;
        }
        entries.push_back(std::move(e));
    }
    closedir(d);

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        std::string la = a.name, lb = b.name;
        for (char &c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char &c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return la < lb;
    });
    cursor = std::clamp(cursor, 0, std::max(0, static_cast<int>(entries.size()) - 1));
}

void Browser::openEntry(int idx, const std::function<void(const std::string &)> &onOpenNro,
                        const std::function<void(const std::string &, const std::string &)> &onToast) {
    if (idx < 0 || idx >= static_cast<int>(entries.size())) return;
    const auto &e = entries[static_cast<size_t>(idx)];
    if (e.isDir) {
        sfx::play(sfx::Sound::Tab);
        navigate(e.fullPath);
        return;
    }
    if (e.kind == 2) {
        if (onOpenNro) onOpenNro(e.fullPath);
        return;
    }
    if (e.kind == 3) {
        closePreview(true);
        std::ifstream f(e.fullPath);
        std::string ln;
        while (std::getline(f, ln) && previewLines.size() < 600) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            previewLines.push_back(ln);
        }
        if (previewLines.empty()) previewLines.push_back(i18n::tr(langIdx, i18n::Key::EmptyFile));
        previewTitle = e.name;
        previewIsImage = false;
        previewOpen = true;
        previewClosing = false;
        previewAnim = 0.0f;
        sfx::play(sfx::Sound::Tab);
        return;
    }
    if (e.kind == 4) {
        closePreview(true);
        std::ifstream f(e.fullPath, std::ios::binary);
        if (f) {
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!bytes.empty() && bytes.size() < 16 * 1024 * 1024) {
                gfx::Color ca, cb;
                previewTex = gfx::decodeImageMemory(bytes.data(), bytes.size(), ca, cb);
                if (previewTex.id) {
                    previewTitle = e.name;
                    previewIsImage = true;
                    previewOpen = true;
                    previewClosing = false;
                    previewAnim = 0.0f;
                    sfx::play(sfx::Sound::Tab);
                    return;
                }
            }
        }
    }
    if (onToast) onToast(e.name, nro::formatBytes(e.size, langIdx) + "  •  " + e.fullPath.substr(5));
}

void Browser::update(const plat::Input &in, float dt, int lang,
                     const std::function<void(const std::string &)> &onOpenNro,
                     const std::function<void(const std::string &, const std::string &)> &onToast) {
    langIdx = lang;
    listFade = std::min(1.0f, listFade + dt * 5.2f);

    // Animate preview & delete modals smoothly (open + close!)
    if (previewOpen) {
        if (previewClosing) {
            previewAnim = std::max(0.0f, previewAnim - dt * 5.2f);
            if (previewAnim <= 0.005f) closePreview(true);
        } else {
            previewAnim = std::min(1.0f, previewAnim + dt * 4.8f);
        }
    }
    if (confirmDeleteOpen) {
        if (confirmDeleteClosing) {
            deleteAnim = std::max(0.0f, deleteAnim - dt * 5.5f);
            if (deleteAnim <= 0.005f) {
                confirmDeleteOpen = false;
                confirmDeleteClosing = false;
            }
        } else {
            deleteAnim = std::min(1.0f, deleteAnim + dt * 5.0f);
        }
    }

    auto triggerNewFolder = [&]() {
        std::string dn;
        if (plat::showKeyboard(i18n::tr(langIdx, i18n::Key::NewFolderPrompt),
                               i18n::tr(langIdx, i18n::Key::NewFolderDefault), dn, 64) && !dn.empty()) {
            mkdir(joinPath(curPath, dn).c_str(), 0777);
            refresh();
            if (onToast) onToast(i18n::tr(langIdx, i18n::Key::FolderCreated), dn);
        }
    };

    auto triggerRename = [&]() {
        if (entries.empty()) return;
        const auto &e = entries[static_cast<size_t>(cursor)];
        std::string nn;
        if (plat::showKeyboard(i18n::tr(langIdx, i18n::Key::RenamePrompt), e.name, nn, 96) && !nn.empty() && nn != e.name) {
            if (rename(e.fullPath.c_str(), joinPath(curPath, nn).c_str()) == 0) {
                refresh();
                if (onToast) onToast(i18n::tr(langIdx, i18n::Key::Renamed), nn);
            }
        }
    };

    auto triggerDelete = [&]() {
        if (entries.empty()) return;
        const auto &e = entries[static_cast<size_t>(cursor)];
        pendingDeletePath = e.fullPath;
        pendingDeleteName = e.name;
        pendingDeleteIsDir = e.isDir;
        confirmDeleteOpen = true;
        confirmDeleteClosing = false;
        deleteAnim = 0.0f;
        sfx::play(sfx::Sound::Tab);
    };

    // 1. Delete confirmation modal
    if (confirmDeleteOpen) {
        if (confirmDeleteClosing) return;
        bool confirm = in.btnA || (in.leftPressed && gfx::Rect{670, 588, 260, 72}.contains(in.mx, in.my));
        bool cancel = in.btnB || (in.leftPressed && gfx::Rect{990, 588, 260, 72}.contains(in.mx, in.my));
        if (confirm) {
            int rc = pendingDeleteIsDir ? rmdir(pendingDeletePath.c_str()) : unlink(pendingDeletePath.c_str());
            confirmDeleteClosing = true;
            if (rc == 0) {
                sfx::play(sfx::Sound::Notify);
                if (onToast) onToast(i18n::tr(langIdx, i18n::Key::Deleted), pendingDeleteName);
                refresh();
            } else {
                sfx::play(sfx::Sound::Back);
                if (onToast) onToast(i18n::tr(langIdx, i18n::Key::DeleteFailed),
                                     pendingDeleteIsDir ? i18n::tr(langIdx, i18n::Key::DirMustBeEmpty) : pendingDeleteName);
            }
        } else if (cancel) {
            confirmDeleteClosing = true;
            sfx::play(sfx::Sound::Back);
        }
        return;
    }

    // 2. Built-in text / image preview overlay
    if (previewOpen) {
        if (previewClosing) return;
        if (in.btnB || (in.leftPressed && gfx::Rect{1540, 162, 220, 62}.contains(in.mx, in.my))) {
            closePreview(false);
            sfx::play(sfx::Sound::Back);
            return;
        }
        if (!previewIsImage) {
            int maxScr = std::max(0, static_cast<int>(previewLines.size()) - 18);
            if (in.navUp) previewScroll = std::max(0, previewScroll - 2);
            if (in.navDown) previewScroll = std::min(maxScr, previewScroll + 2);
            if (std::fabs(in.wheelY) > 0.01f) {
                previewScroll = std::clamp(previewScroll - static_cast<int>(std::round(in.wheelY * 3.0f)), 0, maxScr);
            }
        }
        return;
    }

    // 3. Toolbar buttons (touch/mouse)
    if (in.leftPressed && in.my >= 246.0f && in.my <= 314.0f) {
        if (gfx::Rect{112, 248, 210, 60}.contains(in.mx, in.my)) {
            if (curPath != "sdmc:/") { navigate(parentOf(curPath)); sfx::play(sfx::Sound::Back); }
            return;
        }
        if (gfx::Rect{338, 248, 236, 60}.contains(in.mx, in.my)) {
            navigate(curPath == "sdmc:/switch" ? "sdmc:/" : "sdmc:/switch");
            sfx::play(sfx::Sound::Tab);
            return;
        }
        if (gfx::Rect{590, 248, 250, 60}.contains(in.mx, in.my)) {
            triggerNewFolder();
            return;
        }
        if (!entries.empty() && gfx::Rect{1292, 248, 250, 60}.contains(in.mx, in.my)) {
            triggerRename();
            return;
        }
        if (!entries.empty() && gfx::Rect{1558, 248, 250, 60}.contains(in.mx, in.my)) {
            triggerDelete();
            return;
        }
    }

    // 4. Full 100% Gamepad Controls (every toolbar button mapped!)
    if (in.btnB && curPath != "sdmc:/") {
        navigate(parentOf(curPath));
        sfx::play(sfx::Sound::Back);
        return;
    }
    if (in.btnZL) {
        navigate(curPath == "sdmc:/switch" ? "sdmc:/" : "sdmc:/switch");
        sfx::play(sfx::Sound::Tab);
        return;
    }
    if (in.btnZR) {
        triggerNewFolder();
        return;
    }
    if (!entries.empty()) {
        if (in.navUp && cursor > 0) { cursor--; manualScroll = false; sfx::play(sfx::Sound::Move); }
        if (in.navDown && cursor + 1 < static_cast<int>(entries.size())) { cursor++; manualScroll = false; sfx::play(sfx::Sound::Move); }
        if (in.btnA) { openEntry(cursor, onOpenNro, onToast); return; }
        if (in.btnX) { triggerRename(); return; }
        if (in.btnY) { triggerDelete(); return; }
    }

    // Smooth vertical inertial scroll (never snaps back when scrolling manually!)
    float maxScroll = std::max(0.0f, static_cast<float>(entries.size()) * kRowH - kListH);
    if (std::fabs(in.wheelY) > 0.01f) {
        scrollVel -= in.wheelY * 320.0f;
        manualScroll = true;
    }

    if (in.leftPressed && gfx::Rect{kListX, kListY, kListW, kListH}.contains(in.mx, in.my)) {
        dragging = true;
        dragLastY = in.my;
        dragAccum = 0.0f;
    }
    if (dragging && in.leftDown) {
        float dy = in.my - dragLastY;
        dragLastY = in.my;
        dragAccum += std::fabs(dy);
        if (dragAccum > 14.0f) {
            manualScroll = true;
            scrollY -= dy;
            scrollVel = -dy / std::max(0.004f, dt);
        }
    }
    if (dragging && !in.leftDown) {
        dragging = false;
        if (dragAccum < 16.0f) {
            int idx = static_cast<int>((in.my - kListY + scrollY) / kRowH);
            if (idx >= 0 && idx < static_cast<int>(entries.size())) {
                if (idx == cursor) openEntry(idx, onOpenNro, onToast);
                else { cursor = idx; sfx::play(sfx::Sound::Move); }
            }
        }
    }

    if (!dragging) {
        if (std::fabs(scrollVel) > 6.0f) {
            scrollY += scrollVel * dt;
            scrollVel *= std::pow(0.002f, dt);
        } else if (!manualScroll && !entries.empty()) {
            float itemTop = cursor * kRowH;
            float itemBot = itemTop + kRowH;
            float target = scrollY;
            if (itemTop < scrollY + 40.0f) target = itemTop - 40.0f;
            else if (itemBot > scrollY + kListH - 40.0f) target = itemBot - kListH + 40.0f;
            target = std::clamp(target, 0.0f, maxScroll);
            scrollY += (target - scrollY) * std::min(1.0f, dt * 12.0f);
        }
    }
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);
}

void Browser::render(gfx::Renderer &r, const plat::Input &in, gfx::Color glassTint,
                     gfx::Color accentA, gfx::Color accentB, float slideX, float viewFade,
                     const std::function<std::string(const std::string &)> &fmtFn, bool useSerifTitle) {
    (void)accentB;
    float px = 56.0f + slideX, py = 128.0f, pw = 1808.0f, ph = 910.0f;
    r.drawShadow(px, py + 12, pw, ph, 36, 32, gfx::rgba(0, 0, 0, 0.42f));
    r.drawLiquidGlass(px, py, pw, ph, 32.0f, glassTint, 0.0f, gfx::withAlpha(accentA, 0.16f));

    // Header + Current Path
    r.drawTextCleanReveal(useSerifTitle ? gfx::Font::Serif : gfx::Font::Semi, useSerifTitle ? 68.0f : 52.0f,
                          px + 48.0f, py + 28.0f, 700.0f, fmtFn(i18n::tr(langIdx, i18n::Key::FilesTitle)).c_str(),
                          gfx::hex(0xFFFFFF), viewFade);

    std::string shownPath = (curPath.rfind("sdmc:", 0) == 0) ? curPath.substr(5) : curPath;
    if (shownPath.empty()) shownPath = "/";
    r.drawTextBoxed(gfx::Font::Semi, 25.0f, px + pw - 48.0f, py + 32.0f, 56.0f, shownPath.c_str(),
                    accentA, gfx::Align::Right);

    // Toolbar pills — EVERY SINGLE ONE mapped to a gamepad button!
    auto toolBtn = [&](float bx, float bw, const std::string &lbl, bool danger = false) {
        bool hov = in.pointerVisible && gfx::Rect{bx, 248, bw, 60}.contains(in.mx, in.my);
        gfx::Color fill = danger ? gfx::hex(0xF87171, hov ? 0.28f : 0.16f)
                                 : gfx::rgba(255, 224, 238, hov ? 0.16f : 0.08f);
        r.drawSoftCard(bx, 248, bw, 60, 30.0f, fill, gfx::rgba(255, 224, 238, 0.09f));
        r.drawTextBoxed(gfx::Font::Semi, 22.0f, bx + bw * 0.5f, 248.0f, 60.0f, lbl.c_str(),
                        danger ? gfx::hex(0xFCA5A5) : gfx::hex(0xFFFFFF, 0.94f), gfx::Align::Center);
    };
    toolBtn(112 + slideX, 210, fmtFn(i18n::tr(langIdx, i18n::Key::ParentB)));
    toolBtn(338 + slideX, 236, curPath == "sdmc:/switch" ? "/ (ZL)" : fmtFn(i18n::tr(langIdx, i18n::Key::RootSwitch)));
    toolBtn(590 + slideX, 250, fmtFn(i18n::tr(langIdx, i18n::Key::NewFolderZR)));
    if (!entries.empty()) {
        toolBtn(1292 + slideX, 250, fmtFn(i18n::tr(langIdx, i18n::Key::RenameX)));
        toolBtn(1558 + slideX, 250, fmtFn(i18n::tr(langIdx, i18n::Key::DeleteY)), true);
    }

    // File list box
    r.drawSoftCard(kListX + slideX, kListY, kListW, kListH, 24.0f, gfx::rgba(0, 0, 0, 0.24f), gfx::rgba(255, 224, 238, 0.06f));

    float lf = easeOutCubic(listFade);
    r.pushAlpha(lf);
    r.pushClip(kListX + slideX + 6, kListY + 6, kListW - 12, kListH - 12);
    if (entries.empty()) {
        r.drawTextBoxed(gfx::Font::Sans, 28.0f, kListX + slideX + kListW * 0.5f, kListY, kListH,
                        fmtFn(i18n::tr(langIdx, i18n::Key::EmptyFolder)).c_str(), gfx::hex(0xFFFFFF, 0.52f), gfx::Align::Center);
    } else {
        float yShift = (1.0f - lf) * 14.0f;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            float ry = kListY + 8.0f + i * kRowH - scrollY + yShift;
            if (ry + kRowH < kListY - 20.0f || ry > kListY + kListH + 20.0f) continue;
            const auto &e = entries[static_cast<size_t>(i)];
            bool sel = (i == cursor);
            float rx = kListX + slideX + 12.0f;
            float rw = kListW - 24.0f;
            if (sel) {
                r.drawLiquidGlass(rx, ry, rw, kRowH - 6.0f, 18.0f, gfx::withAlpha(accentA, 0.16f), 0.75f,
                                  gfx::withAlpha(accentA, 0.65f));
            }

            gfx::Color icCol = (e.kind == 1) ? gfx::hex(0xFBBF24)
                             : (e.kind == 2) ? accentA
                             : (e.kind == 3) ? gfx::hex(0x7DD3FC)
                             : (e.kind == 4) ? gfx::hex(0xF0ABFC)
                                             : gfx::hex(0xCBD5E1);
            const char *tag = (e.kind == 1) ? "DIR" : (e.kind == 2) ? "NRO" : (e.kind == 3) ? "TXT" : (e.kind == 4) ? "IMG" : "FILE";
            r.fillRoundedRect(rx + 18.0f, ry + 13.0f, 76.0f, 42.0f, 12.0f, gfx::withAlpha(icCol, 0.18f));
            r.drawTextBoxed(gfx::Font::Semi, 18.0f, rx + 56.0f, ry + 13.0f, 42.0f, tag, icCol, gfx::Align::Center);

            r.drawTextBoxed(sel ? gfx::Font::Semi : gfx::Font::Sans, 26.0f, rx + 114.0f, ry, kRowH - 6.0f,
                            e.name.c_str(), gfx::hex(0xFFFFFF, sel ? 0.99f : 0.86f), gfx::Align::Left);
            if (!e.isDir) {
                r.drawTextBoxed(gfx::Font::Sans, 22.0f, rx + rw - 28.0f, ry, kRowH - 6.0f,
                                nro::formatBytes(e.size, langIdx).c_str(), gfx::hex(0xFFFFFF, 0.58f), gfx::Align::Right);
            } else {
                r.drawUiIcon(gfx::UiIcon::ArrowRight, rx + rw - 34.0f, ry + (kRowH - 6.0f) * 0.5f, 20.0f,
                             gfx::hex(0xFFFFFF, 0.50f));
            }
        }
    }
    r.popClip();
    r.popAlpha();

    r.drawTextBoxed(gfx::Font::Sans, 20.0f, px + 48.0f, py + ph - 74.0f, 56.0f,
                    fmtFn(i18n::tr(langIdx, i18n::Key::FilesFooter)).c_str(),
                    gfx::hex(0xFFFFFF, 0.52f), gfx::Align::Left);
    char cnt[64];
    std::snprintf(cnt, sizeof(cnt), "%zu %s", entries.size(), fmtFn(i18n::tr(langIdx, i18n::Key::ItemsCount)).c_str());
    r.drawTextBoxed(gfx::Font::Semi, 22.0f, px + pw - 48.0f, py + ph - 74.0f, 56.0f, cnt,
                    gfx::hex(0xFFFFFF, 0.65f), gfx::Align::Right);

    if (previewOpen) {
        float pa = easeOutCubic(previewAnim);
        float pOff = (1.0f - pa) * 24.0f;
        r.pushAlpha(pa);
        r.fillRect(0, 0, plat::kW, plat::kH, gfx::rgba(14, 6, 10, 0.82f));
        float mx = 140, my = 136 + pOff, mw = 1640, mh = 840;
        r.drawShadow(mx, my + 12, mw, mh, 40, 32, gfx::rgba(0, 0, 0, 0.60f));
        r.drawLiquidGlass(mx, my, mw, mh, 32.0f, glassTint, 0.0f, gfx::withAlpha(accentA, 0.22f));
        r.drawTextClipped(gfx::Font::Semi, 34.0f, mx + 44, my + 32, 1260, previewTitle.c_str(), gfx::hex(0xFFFFFF));
        r.drawSoftCard(1540, my + 26, 220, 60, 30.0f, gfx::rgba(255, 224, 238, 0.14f), gfx::rgba(255, 224, 238, 0.14f));
        r.drawTextBoxed(gfx::Font::Semi, 23.0f, 1650, my + 26, 60, fmtFn(i18n::tr(langIdx, i18n::Key::CloseB)).c_str(),
                        gfx::hex(0xFFFFFF), gfx::Align::Center);

        if (previewIsImage && previewTex.id) {
            float maxW = mw - 88, maxH = mh - 144;
            float aspect = static_cast<float>(previewTex.w) / std::max(1.0f, static_cast<float>(previewTex.h));
            float dw = maxW, dh = dw / aspect;
            if (dh > maxH) { dh = maxH; dw = dh * aspect; }
            r.drawRoundedTexture(previewTex, mx + (mw - dw) * 0.5f, my + 108 + (maxH - dh) * 0.5f, dw, dh, 18.0f, {1, 1, 1, 1}, 0.02f);
        } else {
            r.fillRoundedRect(mx + 36, my + 106, mw - 72, mh - 142, 20.0f, gfx::rgba(0, 0, 0, 0.42f));
            r.pushClip(mx + 50, my + 118, mw - 100, mh - 166);
            float ty = my + 126.0f;
            for (int i = previewScroll; i < static_cast<int>(previewLines.size()) && i < previewScroll + 19; ++i) {
                r.drawTextClipped(gfx::Font::Sans, 24.0f, mx + 58, ty, mw - 116,
                                  previewLines[static_cast<size_t>(i)].c_str(), gfx::hex(0xFFFFFF, 0.90f));
                ty += 34.0f;
            }
            r.popClip();
        }
        r.popAlpha();
    }

    if (confirmDeleteOpen) {
        float da = easeOutCubic(deleteAnim);
        float dOff = (1.0f - da) * 20.0f;
        r.pushAlpha(da);
        r.fillRect(0, 0, plat::kW, plat::kH, gfx::rgba(14, 6, 10, 0.80f));
        float dx = 540, dy = 360 + dOff, dw = 840, dh = 340;
        r.drawShadow(dx, dy + 12, dw, dh, 40, 32, gfx::rgba(0, 0, 0, 0.65f));
        r.drawLiquidGlass(dx, dy, dw, dh, 32.0f, glassTint, 0.15f, gfx::withAlpha(accentA, 0.30f));
        r.drawTextBoxed(gfx::Font::Semi, 34.0f, dx + dw * 0.5f, dy + 32, 54,
                        fmtFn(i18n::tr(langIdx, i18n::Key::ConfirmDelete)).c_str(),
                        gfx::hex(0xFFFFFF), gfx::Align::Center);
        r.drawTextClipped(gfx::Font::Sans, 26.0f, dx + dw * 0.5f, dy + 120, dw - 80,
                          pendingDeleteName.c_str(), gfx::hex(0xFCA5A5), gfx::Align::Center);

        r.drawGradientPill(670, dy + 228, 260, 72, 36.0f, gfx::hex(0xEF4444), gfx::hex(0xB91C1C));
        r.drawTextBoxed(gfx::Font::Semi, 25.0f, 800, dy + 228, 72,
                        fmtFn(i18n::tr(langIdx, i18n::Key::DeleteA)).c_str(), gfx::hex(0xFFFFFF), gfx::Align::Center);

        r.drawSoftCard(990, dy + 228, 260, 72, 36.0f, gfx::rgba(255, 224, 238, 0.12f), gfx::rgba(255, 224, 238, 0.12f));
        r.drawTextBoxed(gfx::Font::Semi, 25.0f, 1120, dy + 228, 72,
                        fmtFn(i18n::tr(langIdx, i18n::Key::CancelB)).c_str(), gfx::hex(0xFFFFFF), gfx::Align::Center);
        r.popAlpha();
    }
}

}  // namespace files

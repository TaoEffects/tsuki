#pragma once

#include "gfx.h"
#include "platform.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace files {

struct Entry {
    std::string name;
    std::string fullPath;
    bool isDir = false;
    uint64_t size = 0;
    int kind = 0;  // 0=file, 1=dir, 2=nro, 3=text, 4=image
};

class Browser {
public:
    std::string curPath = "sdmc:/";
    std::vector<Entry> entries;
    int cursor = 0;
    float scrollY = 0.0f;
    float scrollVel = 0.0f;
    bool dragging = false;
    float dragLastY = 0.0f;
    float dragAccum = 0.0f;

    // Built-in preview overlay (text or image)
    bool previewOpen = false;
    bool previewClosing = false;
    float previewAnim = 0.0f;
    std::string previewTitle;
    bool previewIsImage = false;
    gfx::Texture previewTex;
    std::vector<std::string> previewLines;
    int previewScroll = 0;

    // Delete confirmation modal
    bool confirmDeleteOpen = false;
    bool confirmDeleteClosing = false;
    float deleteAnim = 0.0f;
    std::string pendingDeletePath;
    std::string pendingDeleteName;
    bool pendingDeleteIsDir = false;

    int langIdx = 0;
    float listFade = 1.0f;

    void init(const std::string &startPath = "sdmc:/");
    void shutdown();
    void navigate(const std::string &path);
    void refresh();

    void update(const plat::Input &in, float dt, int lang,
                const std::function<void(const std::string &nroPath)> &onOpenNro,
                const std::function<void(const std::string &, const std::string &)> &onToast);

    void render(gfx::Renderer &r, const plat::Input &in, gfx::Color glassTint,
                gfx::Color accentA, gfx::Color accentB, float slideX, float viewFade,
                const std::function<std::string(const std::string &)> &fmtFn, bool useSerifTitle);

private:
    void openEntry(int idx, const std::function<void(const std::string &)> &onOpenNro,
                   const std::function<void(const std::string &, const std::string &)> &onToast);
    void closePreview(bool immediate = false);
};

}  // namespace files

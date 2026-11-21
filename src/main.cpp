#include "app.h"
#include "audio.h"
#include "gfx.h"
#include "platform.h"

#include <algorithm>

int main(int, char **) {
    if (!plat::init()) return 1;

    gfx::Renderer renderer;
    if (!renderer.init(plat::kW, plat::kH)) {
        plat::shutdown();
        return 1;
    }

    sfx::init();

    tsuki::App app;
    app.init();

    plat::Input input{};
    double lastTime = plat::nowSeconds();

    while (true) {
        double now = plat::nowSeconds();
        float dt = std::clamp(static_cast<float>(now - lastTime), 0.0005f, 0.06f);
        lastTime = now;

        plat::pollInput(input);
        if (input.quitRequested) break;

        app.update(input, dt);
        if (app.wantExit) break;

        renderer.beginScene(gfx::hex(0x200E17));
        app.render(renderer, input);
        renderer.endFrame();

        plat::swapBuffers();
    }

    app.shutdown();
    sfx::shutdown();
    renderer.shutdown();
    plat::shutdown();
    return 0;
}

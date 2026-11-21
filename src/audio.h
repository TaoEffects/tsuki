#pragma once

namespace sfx {

enum class Sound {
    Move,
    Tab,
    Launch,
    Fav,
    Notify,
    Back,
};

void init();
void shutdown();
void play(Sound s);
void setEnabled(bool en);
bool enabled();

}  // namespace sfx

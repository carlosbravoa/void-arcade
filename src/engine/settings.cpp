#include "settings.h"

#include <SDL.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "json.hpp"

using json = nlohmann::json;

namespace {
// Same data directory the scores file uses (RETRO_ARCADE_DATA or the exe dir).
std::string data_dir() {
    const char* env = std::getenv("RETRO_ARCADE_DATA");
    std::string dir;
    if (env) {
        dir = env;
    } else {
        char* base = SDL_GetBasePath();
        dir = base ? base : "./";
        if (base) SDL_free(base);
    }
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return dir + "settings.json";
}
}  // namespace

void Settings::load() {
    std::ifstream f(data_dir());
    if (!f) return;
    try {
        json j;
        f >> j;
        glow        = j.value("glow", glow);
        persistence = j.value("persistence", persistence);
        scanline    = j.value("scanline", scanline);
        volume      = j.value("volume", volume);
        fullscreen  = j.value("fullscreen", fullscreen);
    } catch (...) {
        // corrupt file — keep defaults
    }
}

void Settings::save() const {
    json j;
    j["glow"] = glow;
    j["persistence"] = persistence;
    j["scanline"] = scanline;
    j["volume"] = volume;
    j["fullscreen"] = fullscreen;
    std::ofstream f(data_dir());
    if (f) f << j.dump(2);
}

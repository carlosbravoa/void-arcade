#include "settings.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include "json.hpp"
#include "paths.h"

using json = nlohmann::json;

namespace {
std::string settings_path() { return data_dir() + "settings.json"; }

// Read a number/bool only if the stored value really has that type, so a
// hand-edited or damaged file can't throw or feed us nonsense.
float num(const json& j, const char* k, float def, float lo, float hi) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_number()) return def;
    return std::clamp(it->get<float>(), lo, hi);
}
}  // namespace

void Settings::load() {
    std::ifstream f(settings_path());
    if (!f) return;
    try {
        json j = json::parse(f);
        if (!j.is_object()) return;
        glow        = num(j, "glow", glow, GLOW_MIN, GLOW_MAX);
        persistence = num(j, "persistence", persistence, PERS_MIN, PERS_MAX);
        scanline    = num(j, "scanline", scanline, SCAN_MIN, SCAN_MAX);
        volume      = int(num(j, "volume", float(volume), VOL_MIN, VOL_MAX));
        music       = int(num(j, "music", float(music), VOL_MIN, VOL_MAX));
        if (auto it = j.find("fullscreen"); it != j.end() && it->is_boolean())
            fullscreen = it->get<bool>();
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
    j["music"] = music;
    j["fullscreen"] = fullscreen;
    write_file_atomic(settings_path(), j.dump(2));
}

#include "paths.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

namespace {
std::string with_slash(std::string d) {
    if (!d.empty() && d.back() != '/') d += '/';
    return d;
}
void mkdirs(const std::string& dir) {
    for (size_t i = 1; i < dir.size(); ++i)
        if (dir[i] == '/') ::mkdir(dir.substr(0, i).c_str(), 0755);
}
std::string resolve() {
    if (const char* env = std::getenv("RETRO_ARCADE_DATA"); env && *env) {
        std::string d = with_slash(env);
        mkdirs(d);
        return d;
    }
    char* base = SDL_GetBasePath();
    std::string exe = with_slash(base ? base : "./");
    if (base) SDL_free(base);
    if (::access(exe.c_str(), W_OK) == 0) return exe;

    std::string d;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) d = with_slash(xdg);
    else if (const char* home = std::getenv("HOME"); home && *home)
        d = with_slash(home) + ".local/share/";
    else return exe;
    d += "void-arcade/";
    mkdirs(d);
    return d;
}
}  // namespace

std::string data_dir() {
    static const std::string dir = resolve();
    return dir;
}

bool write_file_atomic(const std::string& path, const std::string& text) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        f.flush();
        if (!f) { std::remove(tmp.c_str()); return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

// game.h — the Game interface and shared Context passed to every game/launcher.
#pragma once

class Font;
class Surface;
class SoundManager;
class ScoreManager;
class Telemetry;
struct Input;
struct Juice;
struct Combo;

struct Fonts {
    Font* big = nullptr;
    Font* med = nullptr;
    Font* small = nullptr;
};

struct Context {
    int W = 900, H = 720;
    Fonts fonts;
    SoundManager* sfx = nullptr;
    ScoreManager* scores = nullptr;
    Juice* fx = nullptr;
    Telemetry* tele = nullptr;
    Combo* combo = nullptr;
    // When true, the game runs itself (self-play bot) as an idle attract "demo":
    // bot-controlled, loops on game-over, and never writes to the leaderboard.
    bool demo = false;
    // Set when running as a snap with a controller physically present but the
    // `joystick` interface not connected (so it can't be opened). The launcher
    // surfaces a hint telling the user to run `snap connect void-arcade:joystick`.
    bool pad_blocked = false;
};

// Base for the launcher and each game. Mirrors the Python update/draw contract.
class Scene {
public:
    explicit Scene(const Context& ctx) : ctx_(ctx) {}
    virtual ~Scene() = default;

    virtual void update(float dt, const Input& in) = 0;
    virtual void draw(Surface& s) = 0;
    virtual void on_exit() {}

    // Set by a game to request returning to the launcher (e.g. pause -> quit).
    bool quit_to_menu = false;

protected:
    Context ctx_;
};

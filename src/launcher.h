// launcher.h — animated bundle launcher (game select), ported from launcher.py.
#pragma once

#include <string>
#include <vector>

#include "engine/color.h"
#include "engine/game.h"

class Surface;
struct Input;

struct Star {
    float x, y, z;
    int   size = 1;
    Color color{255, 255, 255};
    void reset(int w, int h);
    void update(float dt, int w, int h, float speed = 0.14f);
    void draw(Surface& s, int w, int h) const;
};

class Launcher : public Scene {
public:
    explicit Launcher(const Context& ctx);
    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;

    // Result polled by the app after update().
    std::string selection_id;   // non-empty when a game was chosen
    bool        quit_requested = false;
    bool        options_requested = false;  // 'O' pressed → open the Options screen

    // The game the player is currently hovering — the attract demo plays this one.
    std::string current_id() const;
    // Called when an attract demo ends; `advance` cycles to the next game so a
    // long idle walks the whole roster (and the carousel visibly follows).
    void on_demo_end(bool advance);

private:
    void draw_title(Surface& s);
    void draw_cards(Surface& s);
    void draw_card(Surface& s, int gi, int cx, int cy, int cw, int ch, bool focus, float dim);
    void draw_stats(Surface& s, int game_index, int cx, int y, int cw, float anim);
    void draw_attract(Surface& s);

    int    selected_ = 0;
    float  scroll_ = 0.0f;     // smoothed carousel position (lerps toward selected_)
    float  t_ = 0.0f;
    float  idle_t_ = 0.0f;     // seconds since last input
    float  attract_t_ = 0.0f;  // time on current attract page
    bool   attract_ = false;
    int    attract_game_ = 0;
    std::vector<float> card_anim_;
    std::vector<Star>  stars_;
    std::string        title_;
    std::vector<float> char_phase_;
};

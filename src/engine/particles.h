// particles.h — reusable particle system, ported from shared/particles.py.
#pragma once

#include <vector>

#include "color.h"

class Surface;

struct Particle {
    float x, y, vx, vy;
    float life, max_life;
    Color color, end_color;
    float size;
    float gravity;
    bool  pixel;
    int   trail_len;
    std::vector<std::pair<float, float>> trail;

    bool  alive() const { return life > 0; }
    float t() const { return 1.0f - life / max_life; }
    void  update(float dt);
    void  draw(Surface& s) const;
};

class ParticleSystem {
public:
    void clear() { particles_.clear(); }
    void update(float dt);
    void draw(Surface& s) const;
    size_t count() const { return particles_.size(); }

    void explode_invader(float x, float y, Color color, int count = 28);
    void explode_player(float x, float y, int count = 50);
    void explode_brick(float x, float y, Color color, int count = 18);
    void ball_trail(float x, float y, Color color);
    void paddle_hit(float x, float y, int count = 10);
    void bullet_trail(float x, float y, Color color, bool up = true);
    void bunker_hit(float x, float y, int count = 12);
    void ufo_explode(float x, float y, int count = 40);
    void ambient_star(int width, int height);

private:
    void add(Particle p) { particles_.push_back(std::move(p)); }
    std::vector<Particle> particles_;
};

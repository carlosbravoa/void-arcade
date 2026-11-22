// particles.h — reusable particle system, ported from shared/particles.py.
#pragma once

#include <array>
#include <utility>
#include <vector>

#include "color.h"

class Surface;

struct Particle {
    static constexpr int kTrailMax = 6;          // trail samples kept per particle
    static constexpr float kTrailStep = 1.0f / 60.0f;  // seconds between samples

    float x, y, vx, vy;
    float life, max_life;
    Color color, end_color;
    float size;
    float gravity;
    bool  pixel;
    int   trail_len = 0;                         // 0..kTrailMax
    // Trail history, oldest first. Sampled on a fixed time step (not once per
    // frame) so trails are the same length at 60, 144 or 240 fps, and stored
    // inline so a particle never allocates.
    std::array<std::pair<float, float>, kTrailMax> trail{};
    int   trail_n = 0;
    float trail_t = 0.0f;

    bool  alive() const { return life > 0; }
    float t() const { return 1.0f - life / max_life; }
    void  update(float dt);
    void  draw(Surface& s) const;
};

class ParticleSystem {
public:
    void clear() { particles_.clear(); recycle_ = 0; }
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
    // Hard cap: past it, new particles replace the oldest ones, so an emitter
    // that runs away (or a paused game) can never grow the system without bound.
    static constexpr size_t kMaxParticles = 2500;
    void add(const Particle& p);
    std::vector<Particle> particles_;
    size_t recycle_ = 0;   // next (oldest-first) slot to overwrite when full
};

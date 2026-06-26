#include "particles.h"

#include <algorithm>
#include <cmath>

#include "rng.h"
#include "surface.h"

namespace {
constexpr float TAU = 6.28318530717958647692f;

// Additive pixel: particles emit light, so they accumulate rather than alpha
// over — this makes explosions blow out and bloom hard through the CRT shader.
void add_at(Surface& s, int x, int y, Color c) {
    if (c.a == 0) return;
    Color d = s.get_at(x, y);
    float a = c.a / 255.0f;
    s.set_at(x, y, Color(std::min(255, d.r + int(c.r * a)),
                         std::min(255, d.g + int(c.g * a)),
                         std::min(255, d.b + int(c.b * a)), 255));
}

void blit_pixel(Surface& s, Color color, int x, int y, int sz, bool pixel) {
    if (sz <= 0) return;
    if (pixel) {
        int x0 = x - sz / 2, y0 = y - sz / 2;
        for (int yy = 0; yy < sz; ++yy)
            for (int xx = 0; xx < sz; ++xx) add_at(s, x0 + xx, y0 + yy, color);
    } else {
        for (int dy = -sz; dy <= sz; ++dy)
            for (int dx = -sz; dx <= sz; ++dx)
                if (dx * dx + dy * dy <= sz * sz) add_at(s, x + dx, y + dy, color);
    }
}
}  // namespace

void Particle::update(float dt) {
    if (trail_len > 0) {
        trail.emplace_back(x, y);
        if ((int)trail.size() > trail_len) trail.erase(trail.begin());
    }
    x += vx * dt;
    y += vy * dt;
    vy += gravity * dt;
    life -= dt;
}

void Particle::draw(Surface& s) const {
    if (!alive()) return;
    Color base = lerp_color(color, end_color, std::min(t(), 1.0f));
    int alpha = int(255 * std::max(0.0f, life / max_life));
    int sz = std::max(1, int(size * (1.0f - t() * 0.6f)));
    if (trail_len > 0 && !trail.empty()) {
        for (size_t i = 0; i < trail.size(); ++i) {
            float trail_t = float(i) / std::max<size_t>(1, trail.size());
            int trail_a = int(alpha * trail_t * 0.4f);
            int trail_sz = std::max(1, int(sz * trail_t));
            Color tc = lerp_color(color, end_color, trail_t);
            tc.a = uint8_t(trail_a);
            blit_pixel(s, tc, int(trail[i].first), int(trail[i].second),
                       trail_sz, pixel);
        }
    }
    Color c = base;
    c.a = uint8_t(alpha);
    blit_pixel(s, c, int(x), int(y), sz, pixel);
}

void ParticleSystem::update(float dt) {
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return !p.alive(); }),
        particles_.end());
    for (auto& p : particles_) p.update(dt);
}

void ParticleSystem::draw(Surface& s) const {
    for (auto& p : particles_) p.draw(s);
}

static Particle mk(float x, float y, float vx, float vy, float life, Color c,
                   Color end, float size, float gravity, bool pixel,
                   int trail_len) {
    Particle p;
    p.x = x; p.y = y; p.vx = vx; p.vy = vy;
    p.life = p.max_life = life;
    p.color = c; p.end_color = end;
    p.size = size; p.gravity = gravity; p.pixel = pixel;
    p.trail_len = trail_len;
    return p;
}

void ParticleSystem::explode_invader(float x, float y, Color color, int count) {
    Color bright = brighten(color);
    Color dim(color.r / 6, color.g / 6, color.b / 6);
    for (int i = 0; i < count; ++i) {
        float angle = rnd_uniform(0, TAU);
        float speed = rnd_uniform(40, 220);
        float life = rnd_uniform(0.45f, 1.1f);
        int size = rnd_int(2, 5);
        Color c = lerp_color(bright, color, rnd01() * 0.5f);
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life, c,
               dim, size, 60.0f, true, rnd_int(0, 4)));
    }
    for (int i = 0; i < 8; ++i) {
        float angle = rnd_uniform(0, TAU);
        float speed = rnd_uniform(180, 350);
        float life = rnd_uniform(0.2f, 0.5f);
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life,
               Color(255, 255, 200), color, 2, 0.0f, true, 6));
    }
}

void ParticleSystem::explode_player(float x, float y, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = rnd_uniform(0, TAU);
        float speed = rnd_uniform(30, 280);
        float life = rnd_uniform(0.6f, 1.8f);
        int size = rnd_int(2, 7);
        Color c = rnd_choice<Color>({{80, 200, 255}, {40, 120, 200},
                                     {255, 255, 255}, {200, 240, 255}});
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life, c,
               Color(20, 20, 60), size, 40.0f, true, 5));
    }
}

void ParticleSystem::explode_brick(float x, float y, Color color, int count) {
    Color bright = brighten(color, 1.6f);
    Color dim(color.r / 8, color.g / 8, color.b / 8);
    for (int i = 0; i < count; ++i) {
        float angle = rnd_uniform(0, TAU);
        float speed = rnd_uniform(60, 260);
        float life = rnd_uniform(0.3f, 0.9f);
        int size = rnd_int(2, 6);
        Color c = lerp_color(bright, color, rnd01());
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life, c,
               dim, size, 120.0f, true, rnd_int(0, 3)));
    }
}

void ParticleSystem::ball_trail(float x, float y, Color color) {
    for (int i = 0; i < 3; ++i) {
        float life = rnd_uniform(0.04f, 0.12f);
        add(mk(x, y, rnd_uniform(-20, 20), rnd_uniform(-20, 20), life, color,
               Color(0, 0, 0), rnd_int(2, 5), 0.0f, true, 0));
    }
}

void ParticleSystem::paddle_hit(float x, float y, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = rnd_uniform(-3.14159265f, 0);
        float speed = rnd_uniform(80, 200);
        float life = rnd_uniform(0.15f, 0.4f);
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life,
               Color(255, 255, 180), Color(200, 120, 0), 2, 60.0f, true, 4));
    }
}

void ParticleSystem::bullet_trail(float x, float y, Color color, bool up) {
    for (int i = 0; i < 2; ++i) {
        float life = rnd_uniform(0.05f, 0.15f);
        add(mk(x, y, rnd_uniform(-15, 15), rnd_uniform(20, 60) * (up ? 1 : -1),
               life, color, Color(0, 0, 0), 2, 0.0f, true, 0));
    }
}

void ParticleSystem::bunker_hit(float x, float y, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = rnd_uniform(-3.14159265f, 0);
        float speed = rnd_uniform(30, 120);
        float life = rnd_uniform(0.3f, 0.7f);
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life,
               Color(80, 255, 80), Color(20, 60, 20), 3, 80.0f, true, 0));
    }
}

void ParticleSystem::ufo_explode(float x, float y, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = rnd_uniform(0, TAU);
        float speed = rnd_uniform(50, 250);
        float life = rnd_uniform(0.5f, 1.3f);
        int size = rnd_int(2, 6);
        Color c = rnd_choice<Color>({{255, 60, 60}, {255, 180, 0},
                                     {255, 255, 100}, {200, 0, 0}});
        add(mk(x, y, std::cos(angle) * speed, std::sin(angle) * speed, life, c,
               Color(60, 0, 0), size, 30.0f, true, 3));
    }
}

void ParticleSystem::ambient_star(int width, int height) {
    float x = rnd_int(0, width);
    float y = rnd_int(0, height);
    float life = rnd_uniform(1.0f, 3.0f);
    Color c = rnd_choice<Color>({{200, 200, 255}, {150, 255, 200}, {255, 220, 150}});
    add(mk(x, y, rnd_uniform(-8, 8), rnd_uniform(-8, 8), life, c, Color(0, 0, 0),
           1, 0.0f, true, 0));
}

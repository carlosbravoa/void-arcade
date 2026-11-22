#include "particles.h"

#include <algorithm>
#include <cmath>

#include "rng.h"
#include "surface.h"

namespace {
constexpr float TAU = 6.28318530717958647692f;

// Additive filled rect: particles emit light, so they accumulate rather than
// alpha over — this makes explosions blow out and bloom hard through the CRT
// shader. The rect is clipped ONCE and then written straight into the pixel
// rows (no per-pixel bounds checks).
void add_rect(Surface& s, int x0, int y0, int w, int h, Color c) {
    if (c.a == 0) return;
    int x1 = std::min(s.width(), x0 + w), y1 = std::min(s.height(), y0 + h);
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    if (x0 >= x1 || y0 >= y1) return;
    int ar = c.r * c.a / 255, ag = c.g * c.a / 255, ab = c.b * c.a / 255;
    uint8_t* base = s.data();
    int stride = s.width() * 4;
    for (int y = y0; y < y1; ++y) {
        uint8_t* p = base + size_t(y) * stride + size_t(x0) * 4;
        for (int x = x0; x < x1; ++x, p += 4) {
            p[0] = uint8_t(std::min(255, p[0] + ar));
            p[1] = uint8_t(std::min(255, p[1] + ag));
            p[2] = uint8_t(std::min(255, p[2] + ab));
            p[3] = 255;
        }
    }
}

void blit_pixel(Surface& s, Color color, int x, int y, int sz, bool pixel) {
    if (sz <= 0) return;
    if (pixel) {
        add_rect(s, x - sz / 2, y - sz / 2, sz, sz, color);
    } else {
        // disc: one clipped span per row
        for (int dy = -sz; dy <= sz; ++dy) {
            int half = int(std::sqrt(float(sz * sz - dy * dy)));
            add_rect(s, x - half, y + dy, 2 * half + 1, 1, color);
        }
    }
}
}  // namespace

void Particle::update(float dt) {
    if (trail_len > 0) {
        trail_t += dt;
        int cap = std::min(trail_len, kTrailMax);
        // at most one sample per update: a long frame must not smear the trail
        if (trail_t >= kTrailStep) {
            trail_t = std::fmod(trail_t, kTrailStep);
            if (trail_n >= cap) {
                for (int i = 1; i < trail_n; ++i) trail[i - 1] = trail[i];
                trail_n = cap - 1;
            }
            trail[trail_n++] = {x, y};
        }
    }
    x += vx * dt;
    y += vy * dt;
    vy += gravity * dt;
    life -= dt;
}

void Particle::draw(Surface& s) const {
    if (!alive()) return;
    Color base = lerp_color(color, end_color, std::min(t(), 1.0f));
    int alpha = int(255 * std::clamp(life / max_life, 0.0f, 1.0f));
    int sz = std::max(1, int(size * (1.0f - t() * 0.6f)));
    // whole particle (head + trail) off-screen? skip it in one test
    const float m = float(sz) + 2.0f;
    bool head_on = x > -m && y > -m && x < s.width() + m && y < s.height() + m;
    if (trail_n > 0) {
        for (int i = 0; i < trail_n; ++i) {
            float trail_t01 = float(i) / trail_n;
            int trail_a = int(alpha * trail_t01 * 0.4f);
            if (trail_a <= 0) continue;
            int trail_sz = std::max(1, int(sz * trail_t01));
            Color tc = lerp_color(color, end_color, trail_t01);
            tc.a = uint8_t(trail_a);
            blit_pixel(s, tc, int(trail[i].first), int(trail[i].second),
                       trail_sz, pixel);
        }
    }
    if (!head_on) return;
    Color c = base;
    c.a = uint8_t(alpha);
    blit_pixel(s, c, int(x), int(y), sz, pixel);
}

void ParticleSystem::update(float dt) {
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return !p.alive(); }),
        particles_.end());
    recycle_ = 0;   // after compaction the oldest survivors are at the front again
    for (auto& p : particles_) p.update(dt);
}

void ParticleSystem::draw(Surface& s) const {
    for (auto& p : particles_) p.draw(s);
}

void ParticleSystem::add(const Particle& p) {
    if (particles_.size() < kMaxParticles) { particles_.push_back(p); return; }
    particles_[recycle_] = p;                    // full: replace the oldest
    recycle_ = (recycle_ + 1) % kMaxParticles;
}

static Particle mk(float x, float y, float vx, float vy, float life, Color c,
                   Color end, float size, float gravity, bool pixel,
                   int trail_len) {
    Particle p;
    p.x = x; p.y = y; p.vx = vx; p.vy = vy;
    p.life = p.max_life = life;
    p.color = c; p.end_color = end;
    p.size = size; p.gravity = gravity; p.pixel = pixel;
    p.trail_len = std::clamp(trail_len, 0, Particle::kTrailMax);
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

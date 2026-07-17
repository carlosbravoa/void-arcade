// juice.h — game-feel service shared via Context. Games request feedback
// (screen shake, hit-stop freeze, gamepad rumble, palette drift, CRT shockwave);
// the app consumes it.
#pragma once

#include <algorithm>
#include <initializer_list>
#include <vector>

struct Juice {
    float shake_mag = 0.0f;   // current shake magnitude in px (decays)
    float hitstop_t = 0.0f;   // remaining freeze time in seconds

    // Rumble request, consumed (and cleared) by the app each frame.
    float rumble_low = 0.0f, rumble_high = 0.0f;
    int   rumble_ms = 0;

    // Global hue drift (radians) applied by the CRT shader — games set this per
    // wave/zone so deep waves *feel* different. 0 = no drift.
    float palette_hue = 0.0f;

    // CRT shockwave request: a one-shot screen-space ripple centred at (cx,cy)
    // in top-left fractional screen coords (like draw space). Consumed by the app.
    bool  shock_req = false;
    float shock_cx = 0.5f, shock_cy = 0.5f;

    // Capped so a burst of events (e.g. a charged beam shredding a crowd, each
    // kill re-requesting shake every frame) can never pin the screen at a huge
    // offset — that read as judder/"lag", not impact.
    void shake(float m)   { shake_mag = std::min(20.0f, std::max(shake_mag, m)); }
    void hitstop(float s) { hitstop_t = std::max(hitstop_t, s); }
    void rumble(float low, float high, int ms) {
        rumble_low = std::max(rumble_low, low);
        rumble_high = std::max(rumble_high, high);
        rumble_ms = std::max(rumble_ms, ms);
    }
    void shockwave(float cx = 0.5f, float cy = 0.5f) {
        shock_req = true; shock_cx = cx; shock_cy = cy;
    }

    // ── Characterful rumble ──────────────────────────────────────────────
    // A rumble "pattern" is a sequence of timed segments; tick_rumble() plays it
    // out over real time by re-asserting the current segment each frame through
    // the single-shot fields the app already consumes. Different events get
    // distinct envelopes so the controller speaks a vocabulary, not one buzz.
    struct RSeg { float low, high; int ms; };
    std::vector<RSeg> rpat_;
    int   rpat_i_ = 0;
    float rpat_t_ = 0.0f;

    void rumble_pattern(std::initializer_list<RSeg> segs) {
        rpat_.assign(segs.begin(), segs.end());
        rpat_i_ = 0; rpat_t_ = 0.0f;
    }
    // Advance the active pattern by dt (call once per real frame, before the app
    // consumes the rumble fields). Emits the current segment via rumble().
    void tick_rumble(float dt) {
        if (rpat_i_ >= (int)rpat_.size()) return;
        const RSeg& s = rpat_[rpat_i_];
        rumble(s.low, s.high, s.ms);
        rpat_t_ += dt * 1000.0f;
        if (rpat_t_ >= s.ms) { rpat_t_ -= s.ms; ++rpat_i_; }
    }

    // Event-flavoured presets — give each its own "feel".
    void rumble_tap()       { rumble(0.0f, 0.55f, 55); }   // crisp pip: UI / light shot
    void rumble_hit()       { rumble(0.50f, 0.30f, 95); }  // you took a hit
    void rumble_pickup()    { rumble(0.0f, 0.42f, 45); }   // tiny high blip
    void rumble_explosion() { rumble_pattern({{1.0f, 0.6f, 70}, {0.55f, 0.22f, 150}}); }   // boom + tail
    void rumble_boss_step() { rumble_pattern({{1.0f, 0.0f, 95}, {0.0f, 0.0f, 80}}); }       // stomp cadence
    void rumble_death()     { rumble_pattern({{0.95f, 0.9f, 130}, {0.6f, 0.4f, 170}, {0.3f, 0.15f, 210}}); } // descending
    void rumble_charge(float t01) { rumble(0.18f + 0.55f * t01, 0.10f + 0.30f * t01, 40); } // builds with charge
};

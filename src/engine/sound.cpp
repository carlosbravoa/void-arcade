#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

namespace {
constexpr int   SR = 44100;
constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 2.0f * PI;

using Wave = std::vector<float>;

std::mt19937 g_rng(0xC0FFEE);  // fixed seed: sounds are deterministic
std::uniform_real_distribution<float> g_uni(-1.0f, 1.0f);

int nsamp(float dur) { return int(SR * dur); }

// np.linspace(start, stop, m, endpoint=False)
Wave linspace_open(float a, float b, int m) {
    Wave v(std::max(0, m));
    if (m <= 0) return v;
    float step = (b - a) / m;
    for (int i = 0; i < m; ++i) v[i] = a + step * i;
    return v;
}
// np.linspace(start, stop, m, endpoint=True)
Wave linspace_closed(float a, float b, int m) {
    Wave v(std::max(0, m));
    if (m <= 0) return v;
    if (m == 1) { v[0] = a; return v; }
    float step = (b - a) / (m - 1);
    for (int i = 0; i < m; ++i) v[i] = a + step * i;
    return v;
}

float sgn(float x) { return (x > 0) - (x < 0); }

Wave square(float freq, float dur, float duty = 0.5f) {
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        w[i] = sgn(std::sin(TAU * freq * t) - (2 * duty - 1));
    }
    return w;
}

Wave sawtooth(float freq, float dur) {
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        w[i] = 2.0f * (t * freq - std::floor(t * freq + 0.5f));
    }
    return w;
}

Wave noise(float dur) {
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) w[i] = g_uni(g_rng);
    return w;
}

Wave sine(float freq, float dur) {
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        w[i] = std::sin(TAU * freq * t);
    }
    return w;
}

Wave sweep(float f0, float f1, float dur, const char* wave = "square",
           float duty = 0.5f) {
    int n = nsamp(dur);
    Wave freq = linspace_closed(f0, f1, n);
    Wave phase(n);
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) { acc += TAU * freq[i] / SR; phase[i] = acc; }
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(wave, "square") == 0)
            w[i] = sgn(std::sin(phase[i]) - (2 * duty - 1));
        else if (std::strcmp(wave, "sawtooth") == 0) {
            float p = phase[i] / TAU;
            w[i] = 2.0f * (p - std::floor(p + 0.5f));
        } else
            w[i] = std::sin(phase[i]);
    }
    return w;
}

Wave adsr(Wave sig, float attack, float decay, float sustain, float release,
          float /*total*/) {
    int n = (int)sig.size();
    int a_s = int(attack * SR), d_s = int(decay * SR), r_s = int(release * SR);
    int s_s = std::max(0, n - a_s - d_s - r_s);
    Wave env;
    env.reserve(n + 4);
    auto app = [&](const Wave& w) { env.insert(env.end(), w.begin(), w.end()); };
    app(linspace_open(0, 1, a_s));
    app(linspace_open(1, sustain, d_s));
    app(Wave(s_s, sustain));
    app(linspace_open(sustain, 0, r_s));
    env.resize(n, 0.0f);  // truncate or zero-pad to n
    for (int i = 0; i < n; ++i) sig[i] *= env[i];
    return sig;
}

Wave concat(std::initializer_list<Wave> parts) {
    Wave out;
    for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

Wave mix2(std::initializer_list<Wave> waves) {
    size_t n = SIZE_MAX;
    for (auto& w : waves) n = std::min(n, w.size());
    Wave out(n, 0.0f);
    for (auto& w : waves)
        for (size_t i = 0; i < n; ++i) out[i] += w[i];
    for (auto& s : out) s /= waves.size();
    return out;
}

Wave roll(const Wave& w, int k) {
    int n = (int)w.size();
    Wave out(n);
    for (int i = 0; i < n; ++i) out[(i + k) % n] = w[i];
    return out;
}

Mix_Chunk* to_chunk(Wave wave, float volume = 0.6f) {
    int n = (int)wave.size();
    int bytes = n * 2 * (int)sizeof(int16_t);  // stereo int16
    auto* buf = (Uint8*)std::malloc(bytes);
    auto* out = (int16_t*)buf;
    for (int i = 0; i < n; ++i) {
        float s = std::clamp(wave[i], -1.0f, 1.0f) * 32767.0f * volume;
        int16_t v = (int16_t)std::lround(s);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    auto* c = (Mix_Chunk*)std::malloc(sizeof(Mix_Chunk));
    c->allocated = 1;  // Mix_FreeChunk will free abuf
    c->abuf = buf;
    c->alen = (Uint32)bytes;
    c->volume = 128;
    return c;
}

// ── sound makers (ported 1:1 from sound.py) ──────────────────────────────────
Mix_Chunk* make_shoot() {
    Wave s = sweep(260, 80, 0.10f, "square", 0.4f);
    s = adsr(s, 0.001f, 0.008f, 0.5f, 0.07f, 0.10f);
    return to_chunk(s, 0.58f);
}

Mix_Chunk* make_invader_hit() {
    float dur = 0.28f;
    Wave n = noise(dur);
    int total = nsamp(dur);
    Wave thump(total, 0.0f);
    Wave sq = square(60, 0.04f);
    int thump_len = std::min((int)sq.size(), int(0.04f * SR));
    for (int i = 0; i < thump_len; ++i) thump[i] = sq[i] * 0.8f;
    Wave snap(total, 0.0f);
    Wave sn = noise(0.008f);
    for (int i = 0; i < (int)sn.size() && i < total; ++i) snap[i] = sn[i];
    Wave mix(total);
    for (int i = 0; i < total; ++i)
        mix[i] = n[i] * 0.70f + thump[i] * 0.30f + snap[i] * 0.60f;
    mix = adsr(mix, 0.0f, 0.015f, 0.25f, 0.20f, dur);
    return to_chunk(mix, 0.78f);
}

Mix_Chunk* make_player_death() {
    int pad = int(SR * 0.6f);
    Wave crack = noise(0.015f);
    Wave crack_pad(pad, 0.0f);
    for (int i = 0; i < (int)crack.size(); ++i) crack_pad[i] = crack[i];

    Wave body = noise(0.25f);
    body = adsr(body, 0.0f, 0.02f, 0.6f, 0.20f, 0.25f);
    Wave body_pad(pad, 0.0f);
    for (int i = 0; i < (int)body.size(); ++i) body_pad[i] = body[i];

    Wave r1 = noise(0.5f);
    Wave r2 = roll(r1, 3);
    Wave rumble(r1.size());
    for (size_t i = 0; i < r1.size(); ++i) rumble[i] = (r1[i] + r2[i]) * 0.5f;
    rumble = adsr(rumble, 0.003f, 0.05f, 0.4f, 0.40f, 0.5f);
    Wave rumble_pad(pad, 0.0f);
    for (int i = 0; i < (int)rumble.size(); ++i) rumble_pad[i] = rumble[i];

    Wave total(pad);
    for (int i = 0; i < pad; ++i)
        total[i] = std::clamp(
            crack_pad[i] * 0.80f + body_pad[i] * 0.65f + rumble_pad[i] * 0.55f,
            -1.0f, 1.0f);
    return to_chunk(total, 0.90f);
}

Mix_Chunk* make_ufo_loop() {
    float dur = 0.6f;
    int n = nsamp(dur);
    Wave mix(n);
    float p1 = 0, p2 = 0;
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float lfo = std::sin(TAU * 8 * t) * 0.5f + 0.5f;
        p1 += TAU * (160 + lfo * 120) / SR;
        p2 += TAU * (180 + lfo * 100) / SR;
        mix[i] = sgn(std::sin(p1)) * 0.5f + sgn(std::sin(p2)) * 0.5f;
    }
    int fade = int(0.02f * SR);
    for (int i = 0; i < fade; ++i) {
        mix[i] *= float(i) / fade;
        mix[n - 1 - i] *= float(i) / fade;
    }
    return to_chunk(mix, 0.35f);
}

Mix_Chunk* make_march_note(float freq, float dur = 0.09f) {
    Wave s = adsr(square(freq, dur), 0.002f, 0.01f, 0.7f, 0.04f, dur);
    return to_chunk(s, 0.5f);
}

Mix_Chunk* make_bunker_hit() {
    float dur = 0.14f;
    Wave n = noise(dur);
    Wave n2 = roll(n, 6);
    Wave mix(n.size());
    for (size_t i = 0; i < n.size(); ++i) mix[i] = n[i] * 0.55f + n2[i] * 0.45f;
    mix = adsr(mix, 0.0f, 0.01f, 0.35f, 0.10f, dur);
    return to_chunk(mix, 0.65f);
}

Mix_Chunk* make_ball_bounce_wall() {
    Wave s = adsr(square(440, 0.06f, 0.3f), 0.001f, 0.005f, 0.5f, 0.04f, 0.06f);
    return to_chunk(s, 0.4f);
}
Mix_Chunk* make_ball_bounce_paddle() {
    Wave s = adsr(sweep(300, 220, 0.08f, "square"), 0.001f, 0.01f, 0.6f, 0.05f,
                  0.08f);
    return to_chunk(s, 0.5f);
}
Mix_Chunk* make_brick_hit() {
    float dur = 0.1f;
    Wave a = sweep(600, 200, dur, "square");
    Wave b = noise(dur);
    for (auto& v : a) v *= 0.7f;
    for (auto& v : b) v *= 0.4f;
    Wave mix = mix2({a, b});
    mix = adsr(mix, 0.001f, 0.01f, 0.4f, 0.06f, dur);
    return to_chunk(mix, 0.6f);
}
Mix_Chunk* make_brick_break() {
    float dur = 0.24f;
    Wave n = noise(dur);
    Wave n2 = roll(n, 4);
    int total = nsamp(dur);
    Wave body(total);
    for (int i = 0; i < total; ++i) body[i] = n[i] * 0.6f + n2[i] * 0.4f;
    Wave snap(total, 0.0f);
    Wave sn = noise(0.010f);
    for (int i = 0; i < (int)sn.size() && i < total; ++i) snap[i] = sn[i];
    Wave mix(total);
    for (int i = 0; i < total; ++i) mix[i] = body[i] + snap[i] * 0.55f;
    mix = adsr(mix, 0.0f, 0.012f, 0.35f, 0.20f, dur);
    return to_chunk(mix, 0.80f);
}
Mix_Chunk* make_life_lost() {
    Wave s1 = adsr(sweep(440, 100, 0.3f, "square"), 0.005f, 0.05f, 0.7f, 0.1f,
                   0.3f);
    Wave s2 = adsr(sweep(100, 40, 0.25f, "sawtooth"), 0.005f, 0.05f, 0.5f, 0.1f,
                   0.25f);
    return to_chunk(concat({s1, s2}), 0.7f);
}
Mix_Chunk* make_level_clear() {
    float notes[] = {261, 329, 392, 523, 659, 784};
    Wave out;
    for (float f : notes) {
        Wave s = adsr(square(f, 0.12f), 0.005f, 0.02f, 0.8f, 0.04f, 0.12f);
        out.insert(out.end(), s.begin(), s.end());
    }
    return to_chunk(out, 0.65f);
}
Mix_Chunk* make_laser_shoot() {
    Wave s = sweep(700, 300, 0.1f, "square", 0.4f);
    s = adsr(s, 0.001f, 0.01f, 0.5f, 0.06f, 0.1f);
    return to_chunk(s, 0.5f);
}
Mix_Chunk* make_powerup_collect() {
    Wave s = adsr(sweep(300, 700, 0.15f, "sine"), 0.005f, 0.02f, 0.8f, 0.06f,
                  0.15f);
    return to_chunk(s, 0.6f);
}

Mix_Chunk* make_dive_warble() {
    float dur = 0.22f;
    int n = nsamp(dur);
    Wave w(n);
    float phase = 0;
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float lfo = std::sin(TAU * 14 * t) * 0.5f + 0.5f;
        phase += TAU * (180 + lfo * 220) / SR;
        w[i] = sgn(std::sin(phase));
    }
    w = adsr(w, 0.005f, 0.02f, 0.7f, 0.08f, dur);
    return to_chunk(w, 0.4f);
}
Mix_Chunk* make_tractor_loop() {
    float dur = 0.8f;
    int n = nsamp(dur);
    Wave mix(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float lfo = std::sin(TAU * 3 * t) * 0.5f + 0.5f;
        float w1 = std::sin(TAU * 110 * t);
        float w2 = std::sin(TAU * 117 * t);
        mix[i] = (w1 * 0.5f + w2 * 0.5f) * (0.6f + 0.4f * lfo);
    }
    int fade = int(0.03f * SR);
    for (int i = 0; i < fade; ++i) {
        mix[i] *= float(i) / fade;
        mix[n - 1 - i] *= float(i) / fade;
    }
    return to_chunk(mix, 0.45f);
}
Mix_Chunk* make_capture_jingle() {
    float notes[] = {523, 440, 349, 262};
    Wave out;
    for (float f : notes) {
        Wave s = adsr(square(f, 0.1f), 0.005f, 0.02f, 0.7f, 0.04f, 0.1f);
        out.insert(out.end(), s.begin(), s.end());
    }
    return to_chunk(out, 0.65f);
}
Mix_Chunk* make_rescue_jingle() {
    float notes[] = {330, 392, 494, 659, 784};
    Wave out;
    for (float f : notes) {
        Wave s = adsr(square(f, 0.09f), 0.003f, 0.01f, 0.8f, 0.03f, 0.09f);
        out.insert(out.end(), s.begin(), s.end());
    }
    return to_chunk(out, 0.7f);
}
Mix_Chunk* make_low_energy_pip() {
    Wave s = square(180, 0.055f, 0.35f);
    s = adsr(s, 0.001f, 0.005f, 0.7f, 0.035f, 0.055f);
    return to_chunk(s, 0.50f);
}
Mix_Chunk* make_spawn_chime() {
    float notes[] = {220, 330, 440};
    Wave out;
    int gap = int(0.018f * SR);
    for (float f : notes) {
        Wave s = adsr(square(f, 0.09f, 0.4f), 0.003f, 0.01f, 0.75f, 0.04f, 0.09f);
        out.insert(out.end(), s.begin(), s.end());
        out.insert(out.end(), gap, 0.0f);
    }
    Wave s4 = adsr(square(660, 0.32f, 0.4f), 0.003f, 0.02f, 0.80f, 0.18f, 0.32f);
    out.insert(out.end(), s4.begin(), s4.end());
    return to_chunk(out, 0.55f);
}
Mix_Chunk* make_boss_explosion() {
    Wave crack = noise(0.025f);
    Wave b1 = noise(0.45f);
    Wave b2 = roll(b1, 5), b3 = roll(b1, 11);
    Wave body(b1.size());
    for (size_t i = 0; i < b1.size(); ++i) body[i] = (b1[i] + b2[i] + b3[i]) / 3.0f;
    body = adsr(body, 0.002f, 0.04f, 0.5f, 0.35f, 0.45f);
    Wave t1 = noise(0.55f);
    Wave t2 = roll(t1, 8);
    Wave tail(t1.size());
    for (size_t i = 0; i < t1.size(); ++i) tail[i] = (t1[i] + t2[i]) * 0.5f;
    tail = adsr(tail, 0.005f, 0.08f, 0.3f, 0.45f, 0.55f);

    int total = int(SR * 0.65f);
    Wave out(total, 0.0f);
    for (int i = 0; i < (int)crack.size() && i < total; ++i) out[i] += crack[i] * 0.90f;
    for (int i = 0; i < (int)body.size() && i < total; ++i) out[i] += body[i] * 0.75f;
    for (int i = 0; i < (int)tail.size() && i < total; ++i) out[i] += tail[i] * 0.55f;
    for (auto& s : out) s = std::clamp(s, -1.0f, 1.0f);
    return to_chunk(out, 0.88f);
}

// Looping ambient bed. All partials are integer multiples of 1/duration
// (0.25 Hz) so the 4-second buffer loops seamlessly. Quiet and slow-moving.
Mix_Chunk* make_drone(int variant) {
    float dur = 4.0f;
    int n = nsamp(dur);
    float f1, f2, f3;
    switch (variant) {
        case 1: f1 = 49.0f;   f2 = 73.5f; f3 = 98.0f;   break;  // rift
        case 2: f1 = 65.25f;  f2 = 98.0f; f3 = 130.5f;  break;  // calm/warm
        default: f1 = 55.0f;  f2 = 82.5f; f3 = 110.0f;  break;  // deep space
    }
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float lfo  = 0.5f + 0.5f * std::sin(TAU * 0.25f * t);
        float lfo2 = 0.5f + 0.5f * std::sin(TAU * 0.5f * t + 1.0f);
        w[i] = std::sin(TAU * f1 * t) * 0.5f +
               std::sin(TAU * f2 * t) * 0.3f * lfo +
               std::sin(TAU * f3 * t) * 0.2f * lfo2;
    }
    return to_chunk(w, 0.16f);
}

// ── newer-game / spectacular bank ────────────────────────────────────────────
// FM tone: a carrier phase-modulated by a sine — cheap metallic/alien timbres.
Wave fm_tone(float carrier, float ratio, float index, float dur) {
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float mod = std::sin(TAU * carrier * ratio * t) * index;
        w[i] = std::sin(TAU * carrier * t + mod);
    }
    return w;
}
// Fade the head and tail so a looped buffer has no seam click.
void edge_fade(Wave& w, float secs) {
    int f = std::min((int)w.size() / 2, nsamp(secs));
    for (int i = 0; i < f; ++i) { w[i] *= float(i) / f; w[w.size() - 1 - i] *= float(i) / f; }
}

Mix_Chunk* make_enemy_shot() {
    // grittier and lower than the player shot so the ear separates them.
    Wave s = sweep(440, 110, 0.10f, "sawtooth");
    s = adsr(s, 0.001f, 0.012f, 0.5f, 0.05f, 0.10f);
    Wave g = noise(0.10f);
    for (auto& v : g) v *= 0.28f;
    Wave mix = mix2({s, g});
    return to_chunk(mix, 0.46f);
}
Mix_Chunk* make_zap() {
    Wave s = sweep(1000, 280, 0.07f, "square", 0.35f);
    s = adsr(s, 0.0005f, 0.006f, 0.5f, 0.03f, 0.07f);
    return to_chunk(s, 0.48f);
}
Mix_Chunk* make_big_explosion() {
    // boss_explosion's bigger cousin: harder crack, sub thump, longer rumble tail.
    Wave crack = noise(0.03f);
    Wave b1 = noise(0.6f), b2 = roll(b1, 5), b3 = roll(b1, 13);
    Wave body(b1.size());
    for (size_t i = 0; i < b1.size(); ++i) body[i] = (b1[i] + b2[i] + b3[i]) / 3.0f;
    body = adsr(body, 0.002f, 0.05f, 0.55f, 0.45f, 0.6f);
    Wave sub = sweep(90, 38, 0.5f, "sine");
    sub = adsr(sub, 0.002f, 0.06f, 0.7f, 0.35f, 0.5f);
    Wave t1 = noise(0.75f), t2 = roll(t1, 9);
    Wave tail(t1.size());
    for (size_t i = 0; i < t1.size(); ++i) tail[i] = (t1[i] + t2[i]) * 0.5f;
    tail = adsr(tail, 0.01f, 0.1f, 0.3f, 0.6f, 0.75f);
    int total = int(SR * 0.85f);
    Wave out(total, 0.0f);
    for (int i = 0; i < (int)crack.size() && i < total; ++i) out[i] += crack[i] * 0.95f;
    for (int i = 0; i < (int)body.size() && i < total; ++i) out[i] += body[i] * 0.75f;
    for (int i = 0; i < (int)sub.size()  && i < total; ++i) out[i] += sub[i]  * 0.70f;
    for (int i = 0; i < (int)tail.size() && i < total; ++i) out[i] += tail[i] * 0.50f;
    for (auto& s : out) s = std::clamp(s, -1.0f, 1.0f);
    return to_chunk(out, 0.95f);
}
Mix_Chunk* make_shield_hit() {
    Wave w = fm_tone(620, 2.76f, 6.0f, 0.18f);  // inharmonic ratio = metallic
    w = adsr(w, 0.001f, 0.02f, 0.3f, 0.14f, 0.18f);
    return to_chunk(w, 0.5f);
}
Mix_Chunk* make_alien_chirp(float base) {
    float dur = 0.14f;
    int n = nsamp(dur);
    Wave w(n);
    float ph = 0;
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float vib = std::sin(TAU * 30 * t) * 0.5f + 0.5f;     // fast warble
        ph += TAU * (base + vib * base * 0.8f) / SR;
        w[i] = sgn(std::sin(ph));
    }
    w = adsr(w, 0.005f, 0.02f, 0.7f, 0.05f, dur);
    return to_chunk(w, 0.40f);
}
Mix_Chunk* make_warp() {
    // up-whoosh then down, with a noise swell riding on top.
    Wave up = sweep(160, 1300, 0.18f, "sine");
    Wave dn = sweep(1300, 120, 0.22f, "sine");
    Wave tone = concat({up, dn});
    Wave nz = noise(tone.size() / float(SR));
    int n = (int)tone.size();
    Wave out(n);
    for (int i = 0; i < n; ++i) {
        float env = std::sin(PI * float(i) / n);  // swell up and back
        out[i] = std::clamp(tone[i] * 0.7f * env + nz[i] * 0.25f * env, -1.0f, 1.0f);
    }
    return to_chunk(out, 0.5f);
}
Mix_Chunk* make_lock_on() {
    Wave a = adsr(square(1200, 0.035f, 0.4f), 0.001f, 0.004f, 0.6f, 0.02f, 0.035f);
    Wave gap(nsamp(0.025f), 0.0f);
    Wave b = adsr(square(1600, 0.045f, 0.4f), 0.001f, 0.004f, 0.7f, 0.02f, 0.045f);
    return to_chunk(concat({a, gap, b}), 0.4f);
}
Mix_Chunk* make_energy_refill() {
    float notes[] = {392, 523, 659, 784, 1046};
    Wave out;
    for (float f : notes) {
        Wave s = adsr(square(f, 0.07f, 0.45f), 0.003f, 0.015f, 0.8f, 0.03f, 0.07f);
        Wave shimmer = sine(f * 2, 0.07f);
        for (size_t i = 0; i < s.size(); ++i) s[i] = std::clamp(s[i] * 0.8f + shimmer[i] * 0.2f, -1.0f, 1.0f);
        out.insert(out.end(), s.begin(), s.end());
    }
    return to_chunk(out, 0.6f);
}
Mix_Chunk* make_beam_fire() {
    // bright convergent laser: high square zap with a quick down-sweep + sparkle.
    Wave s = sweep(900, 360, 0.12f, "square", 0.3f);
    s = adsr(s, 0.001f, 0.01f, 0.6f, 0.07f, 0.12f);
    Wave spark = sine(1800, 0.05f);
    for (size_t i = 0; i < spark.size() && i < s.size(); ++i) s[i] = std::clamp(s[i] + spark[i] * 0.25f, -1.0f, 1.0f);
    return to_chunk(s, 0.5f);
}
Mix_Chunk* make_charge_loop() {
    // bright detuned shimmer; partials chosen as multiples of 1/dur (2.5 Hz) so
    // the buffer loops seamlessly while the player holds the charge.
    float dur = 0.4f;
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) * dur / n;
        float trem = 0.55f + 0.45f * std::sin(TAU * 12.5f * t);
        w[i] = (sgn(std::sin(TAU * 220 * t)) * 0.28f +
                std::sin(TAU * 440 * t) * 0.30f +
                std::sin(TAU * 660 * t) * 0.22f) * trem;
    }
    edge_fade(w, 0.01f);
    return to_chunk(w, 0.32f);
}
Mix_Chunk* make_charged_blast() {
    // the payoff: a screaming down-sweep + a fat boom + a sub thump.
    Wave sw = sweep(1500, 90, 0.45f, "sawtooth");
    sw = adsr(sw, 0.002f, 0.05f, 0.7f, 0.25f, 0.45f);
    Wave boom = noise(0.5f), boom2 = roll(boom, 7);
    Wave b(boom.size());
    for (size_t i = 0; i < boom.size(); ++i) b[i] = (boom[i] + boom2[i]) * 0.5f;
    b = adsr(b, 0.0f, 0.04f, 0.5f, 0.4f, 0.5f);
    Wave sub = sweep(120, 45, 0.5f, "sine");
    sub = adsr(sub, 0.001f, 0.08f, 0.6f, 0.3f, 0.5f);
    int total = int(SR * 0.52f);
    Wave out(total, 0.0f);
    for (int i = 0; i < total; ++i) {
        float v = (i < (int)sw.size() ? sw[i] * 0.6f : 0.0f) +
                  (i < (int)b.size()  ? b[i]  * 0.7f : 0.0f) +
                  (i < (int)sub.size()? sub[i]* 0.75f : 0.0f);
        out[i] = std::clamp(v, -1.0f, 1.0f);
    }
    return to_chunk(out, 0.97f);
}
}  // namespace

// ── SoundManager ─────────────────────────────────────────────────────────────
bool SoundManager::init() {
    if (Mix_OpenAudio(SR, AUDIO_S16SYS, 2, 512) != 0) {
        std::printf("[SFX] Mix_OpenAudio failed: %s — running silent.\n",
                    Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(kChannels);
    // Reserve the low channels for the looping beds (UFO/tractor/ambient/charge)
    // so one-shots picked with channel -1 can never steal a loop. Group the rest
    // so we can steal the oldest one-shot if every channel is busy.
    Mix_ReserveChannels(kReserved);
    Mix_GroupChannels(kReserved, kChannels - 1, kSfxGroup);
    shoot_       = make_shoot();
    inv_hit_     = make_invader_hit();
    death_       = make_player_death();
    bunker_      = make_bunker_hit();
    ufo_         = make_ufo_loop();
    float mn[] = {160, 130, 100, 80};
    for (int i = 0; i < 4; ++i) march_[i] = make_march_note(mn[i]);
    wall_        = make_ball_bounce_wall();
    paddle_      = make_ball_bounce_paddle();
    brick_hit_   = make_brick_hit();
    brick_break_ = make_brick_break();
    life_lost_   = make_life_lost();
    level_clear_ = make_level_clear();
    laser_       = make_laser_shoot();
    powerup_     = make_powerup_collect();
    dive_        = make_dive_warble();
    tractor_     = make_tractor_loop();
    capture_     = make_capture_jingle();
    rescue_      = make_rescue_jingle();
    boss_expl_   = make_boss_explosion();
    low_energy_  = make_low_energy_pip();
    spawn_       = make_spawn_chime();
    ambient_[0]  = make_drone(0);
    ambient_[1]  = make_drone(1);
    ambient_[2]  = make_drone(2);
    enemy_shot_  = make_enemy_shot();
    zap_         = make_zap();
    big_expl_    = make_big_explosion();
    shield_      = make_shield_hit();
    chirp_[0]    = make_alien_chirp(300);
    chirp_[1]    = make_alien_chirp(500);
    chirp_[2]    = make_alien_chirp(200);
    warp_        = make_warp();
    lock_        = make_lock_on();
    refill_      = make_energy_refill();
    beam_        = make_beam_fire();
    charge_loop_ = make_charge_loop();
    charged_     = make_charged_blast();
    Mix_Volume(-1, master_vol_);
    ready_ = true;
    std::printf("[SFX] Procedural sound engine ready.\n");
    return true;
}

void SoundManager::play(Mix_Chunk* c, int loops, int channel) {
    if (!ready_ || muted_ || !c) return;
    int ch = Mix_PlayChannel(channel, c, loops);
    if (ch < 0 && channel == -1) {
        // Every one-shot channel is busy — steal the oldest so an important
        // sound (an explosion in a firefight) is never silently dropped.
        int oldest = Mix_GroupOldest(kSfxGroup);
        if (oldest >= 0) { Mix_HaltChannel(oldest); ch = Mix_PlayChannel(oldest, c, loops); }
    }
    if (ch >= 0 && pending_pan_ >= 0.0f) {
        float x = std::clamp(pending_pan_, 0.0f, 1.0f);
        Mix_SetPanning(ch, Uint8(64 + (1.0f - x) * 191), Uint8(64 + x * 191));
    }
    pending_pan_ = -1.0f;
}

void SoundManager::shoot()        { play(shoot_); }
void SoundManager::invader_hit()  { play(inv_hit_); }
void SoundManager::player_death() { play(death_); }
void SoundManager::bunker_hit()   { play(bunker_); }

void SoundManager::march_tick() {
    if (!ready_ || muted_) return;
    Mix_PlayChannel(-1, march_[march_idx_ % 4], 0);
    march_idx_++;
}
void SoundManager::start_ufo() {
    if (!ready_ || muted_ || !ufo_) return;
    if (!Mix_Playing(kUfoChannel)) Mix_PlayChannel(kUfoChannel, ufo_, -1);
}
void SoundManager::stop_ufo() { Mix_HaltChannel(kUfoChannel); }

void SoundManager::ball_wall()       { play(wall_); }
void SoundManager::ball_paddle()     { play(paddle_); }
void SoundManager::brick_hit()       { play(brick_hit_); }
void SoundManager::brick_break()     { play(brick_break_); }
void SoundManager::life_lost()       { play(life_lost_); }
void SoundManager::level_clear()     { play(level_clear_); }
void SoundManager::laser_shoot()     { play(laser_); }
void SoundManager::powerup_collect() { play(powerup_); }

void SoundManager::dive_warble()    { play(dive_); }
void SoundManager::capture()        { play(capture_); }
void SoundManager::rescue()         { play(rescue_); }
void SoundManager::boss_explosion() { play(boss_expl_); }
void SoundManager::low_energy_pip() { play(low_energy_); }
void SoundManager::spawn_chime()    { play(spawn_); }

void SoundManager::enemy_shot()     { play(enemy_shot_); }
void SoundManager::zap()            { play(zap_); }
void SoundManager::big_explosion()  { play(big_expl_); }
void SoundManager::shield_hit()     { play(shield_); }
void SoundManager::alien_chirp(int v) { play(chirp_[std::clamp(v, 0, 2)]); }
void SoundManager::warp()           { play(warp_); }
void SoundManager::lock_on()        { play(lock_); }
void SoundManager::energy_refill()  { play(refill_); }
void SoundManager::beam_fire()      { play(beam_); }
void SoundManager::charged_blast()  { play(charged_); }

void SoundManager::start_charge() {
    if (!ready_ || muted_ || !charge_loop_) return;
    if (!Mix_Playing(kChargeChannel)) Mix_PlayChannel(kChargeChannel, charge_loop_, -1);
}
void SoundManager::stop_charge() { if (ready_) Mix_HaltChannel(kChargeChannel); }

void SoundManager::start_tractor() {
    if (!ready_ || muted_ || !tractor_) return;
    if (!Mix_Playing(kTractorChannel))
        Mix_PlayChannel(kTractorChannel, tractor_, -1);
}
void SoundManager::stop_tractor() { Mix_HaltChannel(kTractorChannel); }

void SoundManager::start_ambient(int variant) {
    if (!ready_) return;
    variant = std::clamp(variant, 0, 2);
    if (!ambient_[variant]) return;
    Mix_HaltChannel(kAmbientChannel);
    Mix_PlayChannel(kAmbientChannel, ambient_[variant], -1);
}
void SoundManager::stop_ambient() {
    if (ready_) Mix_HaltChannel(kAmbientChannel);
}
void SoundManager::pan_next(float x_frac) { pending_pan_ = x_frac; }
void SoundManager::set_ufo_pan(float x_frac) {
    if (!ready_) return;
    float x = std::clamp(x_frac, 0.0f, 1.0f);
    Mix_SetPanning(kUfoChannel, Uint8(64 + (1.0f - x) * 191), Uint8(64 + x * 191));
}
void SoundManager::set_master_volume(int v) {
    master_vol_ = std::clamp(v, 0, 128);
    if (ready_) Mix_Volume(-1, master_vol_);
}
int SoundManager::nudge_volume(int delta) {
    set_master_volume(master_vol_ + delta);
    return master_vol_;
}

bool SoundManager::toggle_mute() {
    muted_ = !muted_;
    if (muted_) Mix_Pause(-1);
    else Mix_Resume(-1);
    return muted_;
}
void SoundManager::stop_all() {
    if (ready_) Mix_HaltChannel(-1);
}
void SoundManager::shutdown() {
    if (!ready_) return;
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
}

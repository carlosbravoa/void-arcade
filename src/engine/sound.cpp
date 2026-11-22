#include "sound.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <random>
#include <vector>

namespace {
constexpr int   SR = 44100;
constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 2.0f * PI;
constexpr double TAU_D = 6.283185307179586476925;

using Wave = std::vector<float>;

std::mt19937 g_rng(0xC0FFEE);  // fixed seed: the sound bank is deterministic
std::uniform_real_distribution<float> g_uni(-1.0f, 1.0f);

int nsamp(float dur) { return std::max(1, int(SR * dur)); }

// ── oscillators ──────────────────────────────────────────────────────────────
// Phase is in cycles [0,1). Pulse and saw are band-limited with polyBLEP so the
// higher bleeps don't alias into harsh grit, and the pulse is DC-free.
enum Osc { SINE, PULSE, SAW, TRI };

float poly_blep(double t, double dt) {
    if (t < dt) { double x = t / dt; return float(x + x - x * x - 1.0); }
    if (t > 1.0 - dt) { double x = (t - 1.0) / dt; return float(x * x + x + x + 1.0); }
    return 0.0f;
}
float osc(Osc k, double ph, double dt, float duty) {
    switch (k) {
        case SINE: return std::sin(float(TAU_D * ph));
        case TRI:  return float(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
        case SAW:  return float(2.0 * ph - 1.0) - poly_blep(ph, dt);
        case PULSE: {
            float v = ph < duty ? 1.0f : -1.0f;
            v += poly_blep(ph, dt);
            double t2 = ph + 1.0 - duty; t2 -= std::floor(t2);
            v -= poly_blep(t2, dt);
            return v - (2.0f * duty - 1.0f);   // remove the duty-cycle DC offset
        }
    }
    return 0.0f;
}

// A tone whose frequency glides linearly f0 -> f1.
Wave tone(Osc k, float f0, float f1, float dur, float duty = 0.5f) {
    int n = nsamp(dur);
    Wave w(n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) {
        double f = f0 + (f1 - f0) * double(i) / std::max(1, n - 1);
        double dt = f / SR;
        w[i] = osc(k, ph, dt, duty);
        ph += dt; ph -= std::floor(ph);
    }
    return w;
}

// Constant-frequency sine by rotation (no per-sample sin()); exact enough over
// several seconds in double precision. Used for the long loop beds.
Wave sine_fast(double freq, int n, double phase0 = 0.0) {
    Wave w(n);
    double c = std::cos(TAU_D * phase0), s = std::sin(TAU_D * phase0);
    double cd = std::cos(TAU_D * freq / SR), sd = std::sin(TAU_D * freq / SR);
    for (int i = 0; i < n; ++i) {
        w[i] = float(s);
        double nc = c * cd - s * sd, ns = s * cd + c * sd;
        c = nc; s = ns;
    }
    return w;
}

Wave white(int n) {
    Wave w(n);
    for (int i = 0; i < n; ++i) w[i] = g_uni(g_rng);
    return w;
}

// ── shaping ──────────────────────────────────────────────────────────────────
// Linear ADSR. If attack+decay+release don't fit the buffer they are scaled
// down together so the envelope always reaches zero at the last sample.
void adsr(Wave& w, float attack, float decay, float sustain, float release) {
    int n = (int)w.size();
    float a = attack * SR, d = decay * SR, r = release * SR;
    float sum = a + d + r;
    if (sum > n) { float k = n / sum; a *= k; d *= k; r *= k; }
    int a_s = int(a), d_s = int(d), r_s = int(r);
    int rel_at = n - r_s;
    for (int i = 0; i < n; ++i) {
        float e;
        if (i < a_s) e = float(i) / a_s;
        else if (i < a_s + d_s) e = 1.0f + (sustain - 1.0f) * float(i - a_s) / d_s;
        else e = sustain;
        if (i >= rel_at && r_s > 0) e *= float(n - 1 - i) / r_s;
        w[i] *= e;
    }
}
// Exponential decay with time-constant tau (seconds) after a short attack.
void decay(Wave& w, float tau, float attack = 0.001f) {
    int a_s = std::max(1, int(attack * SR));
    for (int i = 0; i < (int)w.size(); ++i) {
        float e = std::exp(-float(i) / (tau * SR));
        if (i < a_s) e *= float(i) / a_s;
        w[i] *= e;
    }
}
// One-pole low-pass whose cutoff glides fc0 -> fc1 (exponentially).
void lowpass(Wave& w, float fc0, float fc1) {
    int n = (int)w.size();
    if (n < 2) return;
    double ratio = std::pow(double(fc1) / fc0, 1.0 / (n - 1));
    double fc = fc0;
    float y = 0.0f;
    for (int i = 0; i < n; ++i) {
        float a = 1.0f - std::exp(-TAU * float(fc) / SR);
        y += a * (w[i] - y);
        w[i] = y;
        fc *= ratio;
    }
}
void highpass(Wave& w, float fc) {
    float a = 1.0f - std::exp(-TAU * fc / SR), lp = 0.0f;
    for (auto& s : w) { lp += a * (s - lp); s -= lp; }
}
float peak_of(const Wave& w) {
    float p = 0.0f;
    for (float s : w) p = std::max(p, std::fabs(s));
    return p;
}
void normalize(Wave& w, float target = 1.0f) {
    float p = peak_of(w);
    if (p > 1e-6f) for (auto& s : w) s *= target / p;
}
// dst += src * g, starting at sample `at` (dst grows if needed).
void mix_into(Wave& dst, const Wave& src, float g = 1.0f, int at = 0) {
    if (dst.size() < src.size() + at) dst.resize(src.size() + at, 0.0f);
    for (size_t i = 0; i < src.size(); ++i) dst[at + i] += src[i] * g;
}
void append(Wave& dst, const Wave& src) { dst.insert(dst.end(), src.begin(), src.end()); }
// Smooth saturation instead of a hard clamp when layers are summed.
void soft_clip(Wave& w, float drive = 1.0f) {
    for (auto& s : w) s = std::tanh(s * drive);
}

// ── PCM chunk ────────────────────────────────────────────────────────────────
Mix_Chunk* chunk_from(const Wave& wave) {
    int n = (int)wave.size();
    Uint32 bytes = Uint32(n) * 2 * sizeof(int16_t);  // stereo int16
    auto* buf = (Uint8*)SDL_malloc(bytes);
    auto* c = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
    if (!buf || !c) { SDL_free(buf); SDL_free(c); return nullptr; }
    auto* out = (int16_t*)buf;
    for (int i = 0; i < n; ++i) {
        float s = std::clamp(wave[i], -1.0f, 1.0f) * 32767.0f;
        int16_t v = (int16_t)(s < 0 ? s - 0.5f : s + 0.5f);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    c->allocated = 1;  // Mix_FreeChunk frees abuf
    c->abuf = buf;
    c->alen = bytes;
    c->volume = MIX_MAX_VOLUME;
    return c;
}
// One-shot: peak-normalised to `level` (leaving headroom so several can overlap
// without clipping the mix), with a tiny fade at both ends so it can never click.
Mix_Chunk* one_shot(Wave w, float level) {
    normalize(w, level);
    int n = (int)w.size();
    int fi = std::min(n / 4, 24), fo = std::min(n / 4, 200);
    for (int i = 0; i < fi; ++i) w[i] *= float(i) / fi;
    for (int i = 0; i < fo; ++i) w[n - 1 - i] *= float(i) / fo;
    return chunk_from(w);
}
// Loop: normalised only — the content itself must be periodic over the buffer.
Mix_Chunk* loop_chunk(Wave w, float level) {
    normalize(w, level);
    return chunk_from(w);
}

// Mix levels (peak, of full scale). The master volume scales these further.
constexpr float L_UI = 0.20f, L_SMALL = 0.26f, L_MID = 0.32f, L_BIG = 0.42f,
                L_HUGE = 0.52f;
// Variant v (0..2) -> small pitch offset so repeated sounds don't machine-gun.
float var_pitch(int v) { return v == 1 ? 0.945f : v == 2 ? 1.055f : 1.0f; }
float var_level(int v) { return v == 1 ? 0.88f : v == 2 ? 0.94f : 1.0f; }

// ── explosions: filtered noise with a falling cutoff + a pitched thump ───────
Wave explosion(float dur, float fc0, float fc1, float thump_f0, float thump_f1,
               float thump_amt, float crack_amt, float pitch = 1.0f) {
    int n = nsamp(dur);
    Wave body = white(n);
    lowpass(body, fc0 * pitch, fc1 * pitch);
    lowpass(body, fc0 * pitch, fc1 * pitch);
    decay(body, dur / 3.6f, 0.002f);
    normalize(body);
    Wave out(n, 0.0f);
    mix_into(out, body, 0.8f);
    if (thump_amt > 0.0f) {
        float td = std::min(dur, 0.32f);
        Wave th = tone(SINE, thump_f0 * pitch, thump_f1 * pitch, td);
        decay(th, td / 3.0f, 0.002f);
        mix_into(out, th, thump_amt);
    }
    if (crack_amt > 0.0f) {
        Wave cr = white(nsamp(0.02f));
        highpass(cr, 1800.0f);
        decay(cr, 0.005f, 0.0003f);
        mix_into(out, cr, crack_amt);
    }
    out.resize(n);
    soft_clip(out, 1.3f);
    return out;
}

// ── one-shot makers ──────────────────────────────────────────────────────────
Mix_Chunk* make_shoot(int v) {
    float p = var_pitch(v);
    Wave s = tone(PULSE, 260 * p, 80 * p, 0.10f, 0.4f);
    adsr(s, 0.001f, 0.008f, 0.5f, 0.07f);
    return one_shot(s, L_SMALL * var_level(v));
}
Mix_Chunk* make_invader_hit(int v) {
    return one_shot(explosion(0.28f, 5200, 600, 170, 60, 0.55f, 0.5f, var_pitch(v)),
                    L_MID * var_level(v));
}
Mix_Chunk* make_player_death() {
    Wave w = explosion(0.70f, 3600, 180, 120, 34, 0.9f, 0.7f);
    Wave tail = white(nsamp(0.5f));
    lowpass(tail, 700, 120);
    decay(tail, 0.2f, 0.03f);
    normalize(tail);
    mix_into(w, tail, 0.35f, nsamp(0.12f));
    soft_clip(w, 1.1f);
    return one_shot(w, L_BIG);
}
Mix_Chunk* make_bunker_hit(int v) {
    return one_shot(explosion(0.14f, 3200, 800, 140, 80, 0.3f, 0.3f, var_pitch(v)),
                    L_SMALL * var_level(v));
}
Mix_Chunk* make_march_note(float freq) {
    Wave s = tone(PULSE, freq, freq, 0.09f);
    adsr(s, 0.002f, 0.01f, 0.7f, 0.04f);
    return one_shot(s, L_SMALL);
}
Mix_Chunk* make_ball_wall(int v) {
    float f = 440 * var_pitch(v);
    Wave s = tone(PULSE, f, f, 0.06f, 0.3f);
    adsr(s, 0.001f, 0.005f, 0.5f, 0.04f);
    return one_shot(s, L_UI * var_level(v));
}
Mix_Chunk* make_ball_paddle(int v) {
    float p = var_pitch(v);
    Wave s = tone(PULSE, 300 * p, 220 * p, 0.08f);
    adsr(s, 0.001f, 0.01f, 0.6f, 0.05f);
    return one_shot(s, L_SMALL * var_level(v));
}
Mix_Chunk* make_brick_hit(int v) {
    float p = var_pitch(v);
    Wave s = tone(PULSE, 600 * p, 200 * p, 0.10f);
    Wave nz = white((int)s.size());
    lowpass(nz, 6000, 1500);
    mix_into(s, nz, 0.5f);
    adsr(s, 0.001f, 0.01f, 0.4f, 0.06f);
    return one_shot(s, L_SMALL * var_level(v));
}
Mix_Chunk* make_brick_break(int v) {
    return one_shot(explosion(0.24f, 6500, 1100, 190, 70, 0.4f, 0.6f, var_pitch(v)),
                    L_MID * var_level(v));
}
Mix_Chunk* make_life_lost() {
    Wave s1 = tone(PULSE, 440, 100, 0.3f);
    adsr(s1, 0.005f, 0.05f, 0.7f, 0.1f);
    Wave s2 = tone(SAW, 100, 40, 0.25f);
    adsr(s2, 0.005f, 0.05f, 0.5f, 0.1f);
    append(s1, s2);
    return one_shot(s1, L_MID);
}
// A run of short pulse notes (jingles).
Wave jingle(std::initializer_list<float> notes, float dur, float duty, float rel,
            float gap = 0.0f) {
    Wave out;
    for (float f : notes) {
        Wave s = tone(PULSE, f, f, dur, duty);
        adsr(s, 0.004f, 0.02f, 0.75f, rel);
        append(out, s);
        if (gap > 0.0f) out.insert(out.end(), nsamp(gap), 0.0f);
    }
    return out;
}
Mix_Chunk* make_level_clear() {
    return one_shot(jingle({261, 329, 392, 523, 659, 784}, 0.12f, 0.5f, 0.04f), L_MID);
}
Mix_Chunk* make_laser_shoot(int v) {
    float p = var_pitch(v);
    Wave s = tone(PULSE, 700 * p, 300 * p, 0.10f, 0.4f);
    adsr(s, 0.001f, 0.01f, 0.5f, 0.06f);
    return one_shot(s, L_SMALL * var_level(v));
}
Mix_Chunk* make_powerup_collect() {
    Wave s = tone(SINE, 300, 700, 0.15f);
    adsr(s, 0.005f, 0.02f, 0.8f, 0.06f);
    return one_shot(s, L_MID);
}
// A square whose pitch warbles between base and base+span at lfo_hz.
Wave warble(float base, float span, float lfo_hz, float dur) {
    int n = nsamp(dur);
    Wave w(n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) {
        float t = float(i) / SR;
        float lfo = std::sin(TAU * lfo_hz * t) * 0.5f + 0.5f;
        double dt = (base + lfo * span) / SR;
        w[i] = osc(PULSE, ph, dt, 0.5f);
        ph += dt; ph -= std::floor(ph);
    }
    return w;
}
Mix_Chunk* make_dive_warble() {
    Wave w = warble(180, 220, 14, 0.22f);
    adsr(w, 0.005f, 0.02f, 0.7f, 0.08f);
    return one_shot(w, L_SMALL);
}
Mix_Chunk* make_capture_jingle() {
    return one_shot(jingle({523, 440, 349, 262}, 0.10f, 0.5f, 0.04f), L_MID);
}
Mix_Chunk* make_rescue_jingle() {
    return one_shot(jingle({330, 392, 494, 659, 784}, 0.09f, 0.5f, 0.03f), L_MID);
}
Mix_Chunk* make_boss_explosion() {
    return one_shot(explosion(0.65f, 3000, 220, 105, 38, 0.9f, 0.6f), L_BIG);
}
Mix_Chunk* make_low_energy_pip() {
    Wave s = tone(PULSE, 180, 180, 0.055f, 0.35f);
    adsr(s, 0.001f, 0.005f, 0.7f, 0.035f);
    return one_shot(s, L_SMALL);
}
Mix_Chunk* make_spawn_chime() {
    Wave out = jingle({220, 330, 440}, 0.09f, 0.4f, 0.04f, 0.018f);
    Wave s4 = tone(PULSE, 660, 660, 0.32f, 0.4f);
    adsr(s4, 0.003f, 0.02f, 0.8f, 0.18f);
    append(out, s4);
    return one_shot(out, L_SMALL);
}
Mix_Chunk* make_enemy_shot(int v) {
    // grittier and lower than the player shot so the ear separates them; the
    // noise layer rides the same envelope as the tone (it used to gate off).
    float p = var_pitch(v);
    Wave s = tone(SAW, 440 * p, 110 * p, 0.10f);
    Wave g = white((int)s.size());
    lowpass(g, 5000, 1500);
    mix_into(s, g, 0.45f);
    adsr(s, 0.001f, 0.012f, 0.5f, 0.05f);
    return one_shot(s, L_SMALL * 0.9f * var_level(v));
}
Mix_Chunk* make_zap(int v) {
    float p = var_pitch(v);
    Wave s = tone(PULSE, 1000 * p, 280 * p, 0.07f, 0.35f);
    adsr(s, 0.0005f, 0.006f, 0.5f, 0.03f);
    return one_shot(s, L_SMALL * 0.9f * var_level(v));
}
Mix_Chunk* make_big_explosion() {
    Wave w = explosion(0.95f, 2600, 140, 92, 30, 1.0f, 0.8f);
    Wave sub = tone(SINE, 70, 32, 0.5f);
    adsr(sub, 0.004f, 0.08f, 0.6f, 0.35f);
    mix_into(w, sub, 0.5f, nsamp(0.03f));
    w.resize(nsamp(0.95f));
    soft_clip(w, 1.2f);
    return one_shot(w, L_HUGE);
}
// FM tone: a carrier phase-modulated by a sine — cheap metallic timbres.
Wave fm_tone(float carrier, float ratio, float index, float dur) {
    int n = nsamp(dur);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        float t = float(i) / SR;
        float k = index * std::exp(-t / (dur * 0.4f));   // brightness dies away
        w[i] = std::sin(TAU * carrier * t + std::sin(TAU * carrier * ratio * t) * k);
    }
    return w;
}
Mix_Chunk* make_shield_hit(int v) {
    Wave w = fm_tone(620 * var_pitch(v), 2.76f, 5.0f, 0.18f);  // inharmonic = metallic
    adsr(w, 0.001f, 0.02f, 0.3f, 0.14f);
    return one_shot(w, L_MID * var_level(v));
}
Mix_Chunk* make_alien_chirp(float base) {
    Wave w = warble(base, base * 0.8f, 30, 0.14f);
    adsr(w, 0.005f, 0.02f, 0.7f, 0.05f);
    return one_shot(w, L_SMALL);
}
Mix_Chunk* make_warp() {
    Wave t = tone(SINE, 160, 1300, 0.18f);
    append(t, tone(SINE, 1300, 120, 0.22f));
    int n = (int)t.size();
    Wave nz = white(n);
    lowpass(nz, 4000, 4000);
    for (int i = 0; i < n; ++i) {
        float env = std::sin(PI * float(i) / n);  // swell up and back
        t[i] = (t[i] * 0.7f + nz[i] * 0.4f) * env;
    }
    return one_shot(t, L_MID);
}
Mix_Chunk* make_lock_on() {
    Wave a = tone(PULSE, 1200, 1200, 0.035f, 0.4f);
    adsr(a, 0.001f, 0.004f, 0.6f, 0.02f);
    a.insert(a.end(), nsamp(0.025f), 0.0f);
    Wave b = tone(PULSE, 1600, 1600, 0.045f, 0.4f);
    adsr(b, 0.001f, 0.004f, 0.7f, 0.02f);
    append(a, b);
    return one_shot(a, L_UI);
}
Mix_Chunk* make_energy_refill() {
    // "Recharging" swell: rising pitch with an accelerating whir that grows as
    // the tank fills, capped by a soft "topped-off" ping.
    float dur = 0.5f;
    int n = nsamp(dur);
    Wave w(n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) {
        float t = float(i) / n;
        float sec = float(i) / SR;
        double f = 160.0 + 620.0 * t;
        float trem = 0.6f + 0.4f * std::sin(TAU * (8.0f + 26.0f * t) * sec);
        float body = std::sin(float(TAU_D * ph)) * 0.6f +
                     std::sin(float(TAU_D * 2.0 * ph)) * 0.2f;
        w[i] = body * trem * (t * t);
        ph += f / SR; ph -= std::floor(ph);
    }
    Wave ping = tone(SINE, 1046, 1046, 0.10f);
    adsr(ping, 0.002f, 0.02f, 0.6f, 0.05f);
    mix_into(w, ping, 0.5f, n - (int)ping.size());
    w.resize(n);
    return one_shot(w, L_MID);
}
Mix_Chunk* make_beam_fire(int v) {
    float p = var_pitch(v);
    Wave s = tone(PULSE, 900 * p, 360 * p, 0.12f, 0.3f);
    Wave spark = tone(SINE, 1800 * p, 1500 * p, 0.05f);
    decay(spark, 0.02f);
    mix_into(s, spark, 0.3f);
    s.resize(nsamp(0.12f));
    adsr(s, 0.001f, 0.01f, 0.6f, 0.07f);
    return one_shot(s, L_SMALL * var_level(v));
}
// Charged-shot payoff. Scales with the charge level `c` (0..1): a small charge
// is a quick zap, a full charge a roar — deeper sweep, fatter boom, more sub.
Mix_Chunk* make_charged_blast(float c) {
    float dur = 0.28f + 0.30f * c;
    Wave sw = tone(SAW, 1000.0f + 400.0f * c, 220.0f - 130.0f * c, dur * 0.9f);
    lowpass(sw, 7000, 1800);
    adsr(sw, 0.002f, 0.05f, 0.7f, dur * 0.5f);
    Wave boom = explosion(dur, 3200, 300, 120, 42, 0.5f + 0.5f * c, 0.4f);
    Wave out(nsamp(dur), 0.0f);
    mix_into(out, sw, 0.55f);
    mix_into(out, boom, 0.45f + 0.45f * c);
    out.resize(nsamp(dur));
    soft_clip(out, 1.2f);
    return one_shot(out, L_MID + (L_HUGE - L_MID) * c);
}

// ── shared cues ──────────────────────────────────────────────────────────────
Mix_Chunk* make_boss_alarm() {
    // Two-tone klaxon, three sweeps: unmistakably a warning, not a death.
    Wave out;
    for (int k = 0; k < 3; ++k) {
        Wave hi = tone(SAW, 392, 392, 0.17f);
        Wave lo = tone(SAW, 311, 294, 0.17f);
        adsr(hi, 0.008f, 0.02f, 0.85f, 0.03f);
        adsr(lo, 0.008f, 0.02f, 0.85f, 0.05f);
        append(out, hi);
        append(out, lo);
    }
    lowpass(out, 2600, 2600);
    Wave rumble = tone(SINE, 62, 48, float(out.size()) / SR);
    adsr(rumble, 0.05f, 0.1f, 0.8f, 0.3f);
    mix_into(out, rumble, 0.5f);
    return one_shot(out, L_BIG);
}
Mix_Chunk* make_one_up() {
    Wave out = jingle({523, 659, 784}, 0.07f, 0.25f, 0.03f);
    Wave top = tone(PULSE, 1047, 1047, 0.26f, 0.25f);
    adsr(top, 0.004f, 0.03f, 0.6f, 0.2f);
    append(out, top);
    lowpass(out, 6000, 6000);
    return one_shot(out, L_MID * 0.9f);
}
Mix_Chunk* make_game_over() {
    // Slow descending minor phrase, ending on a low held tonic.
    Wave out;
    for (float f : {330.0f, 262.0f, 220.0f}) {
        Wave s = tone(PULSE, f, f * 0.985f, 0.24f, 0.5f);
        adsr(s, 0.006f, 0.04f, 0.7f, 0.10f);
        append(out, s);
    }
    Wave last = tone(PULSE, 165, 162, 0.7f, 0.5f);
    Wave low = tone(TRI, 82.5f, 81.0f, 0.7f);
    mix_into(last, low, 0.8f);
    last.resize(nsamp(0.7f));
    adsr(last, 0.006f, 0.08f, 0.7f, 0.5f);
    append(out, last);
    lowpass(out, 3200, 3200);
    return one_shot(out, L_MID);
}
Mix_Chunk* make_armor_tink(int v) {
    // Short dull metallic tick: "that hit landed but didn't kill".
    Wave w = fm_tone(1150 * var_pitch(v), 2.41f, 2.2f, 0.055f);
    decay(w, 0.014f, 0.0005f);
    Wave click = white(nsamp(0.004f));
    highpass(click, 2500);
    mix_into(w, click, 0.25f);
    w.resize(nsamp(0.055f));
    return one_shot(w, L_UI * var_level(v));
}
Mix_Chunk* make_ui_move() {
    Wave s = tone(PULSE, 587, 587, 0.03f, 0.25f);
    adsr(s, 0.001f, 0.004f, 0.6f, 0.02f);
    return one_shot(s, L_UI * 0.8f);
}
Mix_Chunk* make_ui_select() {
    Wave out = jingle({587, 880}, 0.05f, 0.25f, 0.03f);
    return one_shot(out, L_UI);
}

// ── loops (all exactly periodic over their buffer: no seam, no edge fade) ────
Mix_Chunk* make_ufo_loop() {
    // 0.5 s: 8 Hz LFO = 4 cycles; the two warbling squares average 220 Hz and
    // 230 Hz = 110 and 115 whole cycles, so phase closes at the loop point.
    int n = SR / 2;
    Wave w(n);
    double p1 = 0.0, p2 = 0.0;
    for (int i = 0; i < n; ++i) {
        double lfo = std::sin(TAU_D * 8.0 * i / SR) * 0.5 + 0.5;
        double d1 = (160.0 + lfo * 120.0) / SR, d2 = (180.0 + lfo * 100.0) / SR;
        w[i] = osc(PULSE, p1, d1, 0.5f) * 0.5f + osc(PULSE, p2, d2, 0.5f) * 0.5f;
        p1 += d1; p1 -= std::floor(p1);
        p2 += d2; p2 -= std::floor(p2);
    }
    return loop_chunk(w, 0.22f);
}
Mix_Chunk* make_tractor_loop() {
    // 1 s: 110 Hz + 117 Hz beating, 3 Hz tremolo — all whole cycles.
    int n = SR;
    Wave a = sine_fast(110, n), b = sine_fast(117, n), l = sine_fast(3, n);
    Wave w(n);
    for (int i = 0; i < n; ++i)
        w[i] = (a[i] + b[i]) * 0.5f * (0.8f + 0.2f * l[i]);
    return loop_chunk(w, 0.28f);
}
// Noise that loops seamlessly: equal-power crossfade of the tail into the head.
Wave looped_noise(int n, float fc) {
    int f = n / 5;
    Wave x = white(n + f);
    lowpass(x, fc, fc);
    lowpass(x, fc, fc);
    Wave w(n);
    for (int i = 0; i < n; ++i) {
        if (i < f) {
            float t = float(i) / f;
            w[i] = x[i] * std::sin(t * PI / 2) + x[i + n] * std::cos(t * PI / 2);
        } else w[i] = x[i];
    }
    return w;
}
Mix_Chunk* make_thrust_loop() {
    int n = SR;
    Wave w = looped_noise(n, 420);
    normalize(w);
    Wave sub = sine_fast(55, n), wob = sine_fast(7, n);
    for (int i = 0; i < n; ++i) w[i] = w[i] * (0.85f + 0.15f * wob[i]) + sub[i] * 0.35f;
    return loop_chunk(w, 0.26f);
}
// Charge: a one-shot that climbs in pitch for ~1.1 s (the time a full charge
// takes) while the sustained top-note shimmer below fades in on a second
// channel, so the hum genuinely rises and then holds for as long as it is held.
Mix_Chunk* make_charge_rise() {
    float dur = 1.15f;
    int n = nsamp(dur);
    Wave w(n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) {
        float t = float(i) / n;
        double f = 150.0 * std::pow(440.0 / 150.0, double(std::min(1.0f, t / 0.92f)));
        float trem = 0.7f + 0.3f * std::sin(TAU * (6.0f + 7.0f * t) * float(i) / SR);
        float env = (0.25f + 0.75f * t) * std::min(1.0f, (1.0f - t) / 0.2f);
        w[i] = (osc(PULSE, ph, f / SR, 0.5f) * 0.3f + std::sin(float(TAU_D * ph)) * 0.5f +
                std::sin(float(TAU_D * 2.0 * ph)) * 0.2f) * trem * env;
        ph += f / SR; ph -= std::floor(ph);
    }
    return one_shot(w, 0.24f);
}
Mix_Chunk* make_charge_loop() {
    // 0.4 s; 440/660/880 Hz and the 12.5 Hz tremolo are all multiples of 2.5 Hz.
    int n = int(SR * 0.4);
    Wave a = sine_fast(440, n), b = sine_fast(660, n), c = sine_fast(880, n),
         t = sine_fast(12.5, n);
    Wave w(n);
    for (int i = 0; i < n; ++i)
        w[i] = (a[i] * 0.5f + b[i] * 0.3f + c[i] * 0.2f) * (0.72f + 0.28f * t[i]);
    return loop_chunk(w, 0.22f);
}
// Ambient bed. Every partial is a multiple of 1/4 Hz so the 4 s buffer loops
// seamlessly; a few quiet upper partials keep it audible on small speakers.
Mix_Chunk* make_drone(int variant) {
    int n = SR * 4;
    double f1;
    switch (variant) {
        case 1: f1 = 49.0; break;    // rift
        case 2: f1 = 65.25; break;   // calm/warm
        default: f1 = 55.0; break;   // deep space
    }
    Wave p1 = sine_fast(f1, n), p2 = sine_fast(f1 * 1.5, n), p3 = sine_fast(f1 * 2, n),
         p4 = sine_fast(f1 * 4, n), p5 = sine_fast(f1 * 6, n);
    Wave l1 = sine_fast(0.25, n), l2 = sine_fast(0.5, n, 0.16), l3 = sine_fast(0.75, n, 0.4);
    Wave w(n);
    for (int i = 0; i < n; ++i)
        w[i] = p1[i] * 0.5f + p2[i] * 0.3f * (0.5f + 0.5f * l1[i]) +
               p3[i] * 0.2f * (0.5f + 0.5f * l2[i]) +
               p4[i] * 0.07f * (0.5f + 0.5f * l3[i]) +
               p5[i] * 0.04f * (0.5f - 0.5f * l1[i]);
    return loop_chunk(w, 0.13f);
}

// ── music ────────────────────────────────────────────────────────────────────
// A tiny step sequencer: lead + bass + arpeggio + noise/sine percussion, 16
// bars laid out A A' B A'. Melodies are written in scale degrees over a
// diatonic chord progression (so everything stays in key); bass and arpeggio
// are derived from the chords. Every note is added modulo the buffer length,
// so tails wrap around the loop point and the track loops without a seam.
//
// Lead notation, 8 eighth-notes per bar, bars separated by '|':
//   1..7  scale degrees in the lead octave      q w e r t y u  the octave above
//   z x c v b n m  the octave below             -  hold        .  rest
// Bass notation, 8 eighth-notes per bar: r root, f fifth, o octave, - hold, . rest
// Drum rows: 16 sixteenth-steps per bar, 'x' = hit.
struct TrackSpec {
    float bpm;
    int   tonic;          // MIDI note of scale degree 1 in the lead octave
    bool  minor;
    int   prog_a[4];      // chord roots (0-based scale degrees), one per bar
    int   prog_b[4];
    const char* lead_a;   // 4 bars
    const char* lead_e;   // 1 bar: replaces bar 4 of A the second time (A')
    const char* lead_b;   // 4 bars
    const char* bass;     // 1 bar
    const char* kick;
    const char* snare;
    const char* hat;
    float lead_duty, lead_gain, bass_gain, arp_gain;
    int   arp_steps;      // sixteenths per arpeggio note (0 = no arpeggio)
    float echo;           // lead echo level (dotted-eighth delay)
    float drum_gain;
};

const TrackSpec TRACKS[8] = {
    // 0 invaders — tense march, A minor
    {112, 57, true, {0, 0, 5, 6}, {3, 0, 5, 6},
     "1.1.3-5-|5.5.4-3-|6-.6q-6-|7-.7w-7-", "7-w-7-5-",
     "4-6-q-6-|5-3-1-3-|6-q-e-q-|w-7-5-7-",
     "r.r.r.f.", "x...x...x...x...", "....x.......x.x.", "..x...x...x...x.",
     0.25f, 0.24f, 0.34f, 0.07f, 2, 0.0f, 1.0f},
    // 1 squadrons — bouncy, C major
    {132, 60, false, {0, 5, 3, 4}, {3, 0, 1, 4},
     "5.3.5.q.|6.3.6.q-|4.6.q.6.|5.7.w-7.", "w.7.5.7-",
     "q-6-4-6-|5-3-1-3-|2-4-6-4-|5-7-w---",
     "r.o.r.o.", "x.....x.x.......", "....x.......x...", "x.x.x.x.x.x.x.x.",
     0.5f, 0.21f, 0.32f, 0.08f, 1, 0.0f, 0.9f},
    // 2 raid — driving, E minor
    {140, 52, true, {0, 6, 5, 6}, {0, 3, 5, 4},
     "1.135.3.|7.7w7.4.|6.6q6.3.|7.7w7.w-", "7.w.r.w-",
     "q-5-3-5-|q-6-4-6-|q-6-3-6-|w-7-5-7-",
     "rrorrror", "x...x...x...x...", "....x.......x...", "x.xxx.xxx.xxx.xx",
     0.25f, 0.23f, 0.34f, 0.06f, 1, 0.2f, 1.0f},
    // 3 breaker — bright, G major
    {120, 55, false, {0, 3, 0, 4}, {5, 3, 0, 4},
     "3-5-q-5-|4-6-q-6-|3-5-q-e-|w-7-5---", "w-7-w---",
     "6-q-e-q-|6-q-r-q-|5-q-e-q-|7-w-7-5-",
     "r-.fr-f.", "x.......x.x.....", "....x.......x...", "..x...x...x...x.",
     0.5f, 0.20f, 0.30f, 0.09f, 1, 0.25f, 0.8f},
    // 4 drift — sparse and eerie, D minor
    {84, 62, true, {0, 0, 5, 4}, {3, 3, 0, 0},
     "5-------|3---1---|6-------|5---2---", "5---7---",
     "4---6---|q-------|5---3---|1-------",
     "r-------", "x...............", "................", "........x.......",
     0.5f, 0.17f, 0.34f, 0.08f, 4, 0.45f, 0.6f},
    // 5 gunner — urgent, B minor
    {152, 59, true, {0, 0, 3, 4}, {5, 6, 0, 0},
     "1155.5.3|1155.5.q|4466.6.q|5577.7.w", "5577w-7-",
     "6-q-e-q-|7-w-r-w-|q-.5q-.5|q---5---",
     "rrrrrrrr", "x..x..x.x..x..x.", "....x.......x...", "xxxxxxxxxxxxxxxx",
     0.25f, 0.22f, 0.33f, 0.05f, 1, 0.0f, 1.0f},
    // 6 storm — heroic, E-flat major
    {126, 51, false, {0, 4, 5, 3}, {3, 4, 0, 4},
     "1-.15-.5|5-.57-w-|6-.6q-e-|r-e-q-6-", "r-e-q-w-",
     "4-6-q-6-|5-7-w-7-|q-e-t-e-|w---7---",
     "r.rfr.ro", "x.....x.x.......", "....x.......x..x", "x.x.x.x.x.x.x.x.",
     0.5f, 0.23f, 0.33f, 0.07f, 2, 0.2f, 0.95f},
    // 7 menu — calm, F major
    {92, 53, false, {0, 5, 3, 4}, {0, 2, 3, 4},
     "3---5---|3---1---|4---6---|5---2---", "5---7---",
     "5---q---|5---3---|6---4---|5-------",
     "r---f---", "x.......x.......", "................", "....x.......x...",
     0.5f, 0.17f, 0.30f, 0.10f, 2, 0.35f, 0.45f},
};

const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};

int degree_semis(int deg, bool minor) {
    int oct = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
    int d = deg - oct * 7;
    return (minor ? MINOR : MAJOR)[d] + 12 * oct;
}
float midi_hz(float m) { return 440.0f * std::pow(2.0f, (m - 69.0f) / 12.0f); }
// Lead token -> scale degree (0-based); -100 = hold, -200 = rest.
int lead_token(char c) {
    if (c >= '1' && c <= '7') return c - '1';
    if (const char* p = std::strchr("qwertyu", c)) return 7 + int(p - "qwertyu");
    if (const char* p = std::strchr("zxcvbnm", c)) return -7 + int(p - "zxcvbnm");
    return c == '-' ? -100 : -200;
}
// Strip bar lines; returns exactly bars*8 tokens (padded with rests).
std::vector<int> parse_lead(const char* s, int bars) {
    std::vector<int> out;
    for (; *s; ++s) if (*s != '|' && *s != ' ') out.push_back(lead_token(*s));
    out.resize(size_t(bars) * 8, -200);
    return out;
}

Wave render_track(const TrackSpec& t) {
    const int BARS = 16;
    const int step = int(std::lround(SR * 60.0 / t.bpm / 4.0));  // one sixteenth
    const int total = BARS * 16 * step;                          // exact bar multiple
    Wave out(size_t(total), 0.0f);

    // kind 0 lead (sustained pulse), 1 bass (triangle + a little pulse), 2 arp (pluck)
    auto add_note = [&](int start, int len, float hz, int kind, float g) {
        int rel = int(SR * (kind == 0 ? 0.05f : kind == 1 ? 0.03f : 0.015f));
        int att = int(SR * (kind == 2 ? 0.002f : 0.005f));
        double ph = 0.0, dt = hz / SR;
        float tau = SR * (kind == 0 ? 0.25f : kind == 1 ? 0.5f : 0.07f);
        float floor_ = kind == 0 ? 0.6f : kind == 1 ? 0.7f : 0.0f;
        float dec = 1.0f, dk = std::exp(-1.0f / tau);     // running exp(-i/tau)
        int idx = start % total;
        for (int i = 0; i < len + rel; ++i) {
            float e = floor_ + (1.0f - floor_) * dec;
            dec *= dk;
            if (i < att) e *= float(i) / att;
            if (i >= len) e *= 1.0f - float(i - len) / rel;
            float v;
            if (kind == 0) v = osc(PULSE, ph, dt, t.lead_duty);
            else if (kind == 1) v = osc(TRI, ph, dt, 0.5f) * 0.85f + osc(PULSE, ph, dt, 0.5f) * 0.15f;
            else v = osc(PULSE, ph, dt, 0.125f);
            out[size_t(idx)] += v * e * g;
            if (++idx == total) idx = 0;
            ph += dt; if (ph >= 1.0) ph -= 1.0;
        }
    };

    // ── lead: A, A' (A with the ending bar), B, A' ──
    std::vector<int> la = parse_lead(t.lead_a, 4), le = parse_lead(t.lead_e, 1),
                     lb = parse_lead(t.lead_b, 4);
    std::vector<int> lead;
    auto put = [&](const std::vector<int>& v, int from, int n) {
        lead.insert(lead.end(), v.begin() + from, v.begin() + from + n);
    };
    put(la, 0, 32);
    put(la, 0, 24); put(le, 0, 8);
    put(lb, 0, 32);
    put(la, 0, 24); put(le, 0, 8);
    const int eighth = step * 2;
    for (int i = 0; i < (int)lead.size(); ++i) {
        if (lead[i] <= -100) continue;
        int k = 1;
        while (i + k < (int)lead.size() && lead[i + k] == -100) ++k;
        float hz = midi_hz(float(t.tonic + degree_semis(lead[i], t.minor)));
        int len = int(k * eighth * 0.9f);
        add_note(i * eighth, len, hz, 0, t.lead_gain);
        if (t.echo > 0.0f) add_note(i * eighth + step * 3, len, hz, 0, t.lead_gain * t.echo * 0.5f);
    }

    // ── bass + arpeggio from the chord of each bar ──
    for (int bar = 0; bar < BARS; ++bar) {
        int section = bar / 4;
        int root = (section == 2 ? t.prog_b : t.prog_a)[bar % 4];
        int root_semis = degree_semis(root, t.minor);
        if (root_semis > 7) root_semis -= 12;            // keep the bass line low
        float bass_root = float(t.tonic - 12 + root_semis);
        if (bass_root < 38) bass_root += 12;             // not below ~D2: stays audible
        int bar_at = bar * 16 * step;
        for (int i = 0; i < 8 && t.bass[i]; ++i) {
            char c = t.bass[i];
            if (c == '.' || c == '-') continue;
            int k = 1;
            while (i + k < 8 && t.bass[i + k] == '-') ++k;
            float m = bass_root + (c == 'f' ? 7 : c == 'o' ? 12 : 0);
            add_note(bar_at + i * eighth, int(k * eighth * 0.85f), midi_hz(m), 1, t.bass_gain);
        }
        if (t.arp_steps > 0 && t.arp_gain > 0.0f) {
            static const int ORDER[4] = {0, 1, 2, 1};
            int n = 0;
            for (int s16 = 0; s16 < 16; s16 += t.arp_steps, ++n) {
                int deg = root + 2 * ORDER[n % 4];
                float m = float(t.tonic + degree_semis(deg, t.minor));
                if (root >= 4) m -= 12;                  // keep the arpeggio under the lead
                add_note(bar_at + s16 * step, int(step * t.arp_steps * 0.9f), midi_hz(m), 2,
                         t.arp_gain);
            }
        }
    }

    // ── percussion ──
    Wave kick = tone(SINE, 125, 46, 0.13f);
    decay(kick, 0.045f, 0.001f);
    Wave snare = white(nsamp(0.12f));
    highpass(snare, 1400);
    lowpass(snare, 6500, 3500);
    decay(snare, 0.035f, 0.001f);
    normalize(snare);
    Wave body = tone(SINE, 210, 170, 0.06f);
    decay(body, 0.02f);
    mix_into(snare, body, 0.5f);
    Wave hat = white(nsamp(0.035f));
    highpass(hat, 6500);
    decay(hat, 0.009f, 0.0005f);
    normalize(hat);
    auto hits = [&](const char* row, const Wave& w, float g) {
        for (int bar = 0; bar < BARS; ++bar)
            for (int s16 = 0; s16 < 16 && row[s16]; ++s16) {
                if (row[s16] != 'x') continue;
                int at = (bar * 16 + s16) * step;
                for (size_t i = 0; i < w.size(); ++i)
                    out[size_t((at + int(i)) % total)] += w[i] * g;
            }
    };
    hits(t.kick, kick, 0.42f * t.drum_gain);
    hits(t.snare, snare, 0.20f * t.drum_gain);
    hits(t.hat, hat, 0.07f * t.drum_gain);

    // Gentle low-pass to round the pulse edges. The filter is first settled on
    // the tail of the loop, so its state is periodic and the seam stays clean.
    {
        float a = 1.0f - std::exp(-TAU * 7000.0f / SR), y = 0.0f;
        for (int i = std::max(0, total - 2048); i < total; ++i) y += a * (out[i] - y);
        for (int i = 0; i < total; ++i) { y += a * (out[i] - y); out[i] = y; }
    }
    // Same loudness for every track (RMS), then a soft limiter for the peaks
    // (x / sqrt(1 + x^2): smooth, monotonic, never exceeds the ceiling).
    double sq = 0.0;
    for (float s : out) sq += double(s) * s;
    float rms = float(std::sqrt(sq / total));
    float k = (rms > 1e-6f ? 0.19f / rms : 1.0f) / 0.7f;
    for (auto& s : out) { float x = s * k; s = 0.7f * x / std::sqrt(1.0f + x * x); }
    return out;
}

enum SfxId {
    S_SHOOT, S_INV_HIT, S_DEATH, S_BUNKER, S_MARCH0, S_MARCH1, S_MARCH2, S_MARCH3,
    S_WALL, S_PADDLE, S_BRICK_HIT, S_BRICK_BREAK, S_LIFE_LOST, S_LEVEL_CLEAR,
    S_LASER, S_POWERUP, S_DIVE, S_CAPTURE, S_RESCUE, S_BOSS_EXPL, S_LOW_ENERGY,
    S_SPAWN, S_ENEMY_SHOT, S_ZAP, S_BIG_EXPL, S_SHIELD, S_CHIRP0, S_CHIRP1,
    S_CHIRP2, S_WARP, S_LOCK, S_REFILL, S_BEAM, S_CHARGED0, S_CHARGED1,
    S_CHARGED2, S_BOSS_ALARM, S_ONE_UP, S_GAME_OVER, S_TINK, S_UI_MOVE,
    S_UI_SELECT, S_COUNT
};

constexpr Uint32 kRetriggerMs = 45;   // min gap between two plays of one sound

// Constant-power pan with FULL level at centre: the louder side stays at 255
// and only the far side is attenuated, so panning never makes a sound quieter
// than its unpanned twin.
void pan_gains(float x, Uint8& l, Uint8& r) {
    x = std::clamp(x, 0.0f, 1.0f);
    float th = x * PI / 2;
    float gl = std::min(1.0f, std::cos(th) * 1.41421356f);
    float gr = std::min(1.0f, std::sin(th) * 1.41421356f);
    l = Uint8(std::max(0.22f, gl) * 255.0f);
    r = Uint8(std::max(0.22f, gr) * 255.0f);
}
}  // namespace

// ── SoundManager ─────────────────────────────────────────────────────────────
bool SoundManager::init() {
    // No allowed format changes: every chunk here is hand-built as 44.1 kHz
    // stereo S16, so let SDL convert for the device rather than hand us a
    // 48 kHz stream that would play everything sharp.
    if (Mix_OpenAudioDevice(SR, AUDIO_S16SYS, 2, 512, nullptr, 0) != 0) {
        std::printf("[SFX] Mix_OpenAudio failed: %s — running silent.\n",
                    Mix_GetError());
        return false;
    }
    int freq = 0, chans = 0; Uint16 fmt = 0;
    if (!Mix_QuerySpec(&freq, &fmt, &chans) || freq != SR || chans != 2 ||
        fmt != AUDIO_S16SYS) {
        std::printf("[SFX] unexpected audio format (%d Hz, %d ch) — running silent.\n",
                    freq, chans);
        Mix_CloseAudio();
        return false;
    }
    Mix_AllocateChannels(kChannels);
    // Reserve the low channels for loops and music so one-shots picked from the
    // SFX group can never steal them.
    Mix_ReserveChannels(kReserved);
    Mix_GroupChannels(kReserved, kChannels - 1, kSfxGroup);

    auto one = [&](int id, Mix_Chunk* c) { voices_[id].v[0] = c; voices_[id].n = c ? 1 : 0; };
    auto three = [&](int id, Mix_Chunk* (*mk)(int)) {
        Voice& v = voices_[id];
        v.n = 0;
        for (int i = 0; i < 3; ++i)
            if (Mix_Chunk* c = mk(i)) v.v[v.n++] = c;
    };
    three(S_SHOOT, make_shoot);
    three(S_INV_HIT, make_invader_hit);
    one(S_DEATH, make_player_death());
    three(S_BUNKER, make_bunker_hit);
    const float mn[4] = {160, 130, 100, 80};
    for (int i = 0; i < 4; ++i) one(S_MARCH0 + i, make_march_note(mn[i]));
    three(S_WALL, make_ball_wall);
    three(S_PADDLE, make_ball_paddle);
    three(S_BRICK_HIT, make_brick_hit);
    three(S_BRICK_BREAK, make_brick_break);
    one(S_LIFE_LOST, make_life_lost());
    one(S_LEVEL_CLEAR, make_level_clear());
    three(S_LASER, make_laser_shoot);
    one(S_POWERUP, make_powerup_collect());
    one(S_DIVE, make_dive_warble());
    one(S_CAPTURE, make_capture_jingle());
    one(S_RESCUE, make_rescue_jingle());
    one(S_BOSS_EXPL, make_boss_explosion());
    one(S_LOW_ENERGY, make_low_energy_pip());
    one(S_SPAWN, make_spawn_chime());
    three(S_ENEMY_SHOT, make_enemy_shot);
    three(S_ZAP, make_zap);
    one(S_BIG_EXPL, make_big_explosion());
    three(S_SHIELD, make_shield_hit);
    one(S_CHIRP0, make_alien_chirp(300));
    one(S_CHIRP1, make_alien_chirp(500));
    one(S_CHIRP2, make_alien_chirp(200));
    one(S_WARP, make_warp());
    one(S_LOCK, make_lock_on());
    one(S_REFILL, make_energy_refill());
    three(S_BEAM, make_beam_fire);
    one(S_CHARGED0, make_charged_blast(0.30f));
    one(S_CHARGED1, make_charged_blast(0.65f));
    one(S_CHARGED2, make_charged_blast(1.0f));
    one(S_BOSS_ALARM, make_boss_alarm());
    one(S_ONE_UP, make_one_up());
    one(S_GAME_OVER, make_game_over());
    three(S_TINK, make_armor_tink);
    one(S_UI_MOVE, make_ui_move());
    one(S_UI_SELECT, make_ui_select());

    ufo_         = make_ufo_loop();
    tractor_     = make_tractor_loop();
    thrust_      = make_thrust_loop();
    charge_rise_ = make_charge_rise();
    charge_loop_ = make_charge_loop();
    for (int i = 0; i < 3; ++i) ambient_[i] = make_drone(i);

    ready_ = true;
    applied_sfx_ = applied_music_ = -1;
    apply_volumes();
    std::printf("[SFX] Procedural sound engine ready.\n");
    return true;
}

// ── volume / mute ────────────────────────────────────────────────────────────
// Mute is "volume 0", not "pause": everything keeps running silently, so
// nothing stale resumes on un-mute and loops started while muted are there
// when the sound comes back.
int SoundManager::sfx_volume() const { return muted_ ? 0 : master_vol_; }
int SoundManager::music_channel_volume() const {
    return muted_ ? 0 : music_vol_ * master_vol_ / MIX_MAX_VOLUME;
}
void SoundManager::apply_volumes() {
    if (!ready_) return;
    int sv = sfx_volume(), mv = music_channel_volume();
    Uint32 now = SDL_GetTicks();
    bool settling = resync_until_ != 0 && !SDL_TICKS_PASSED(now, resync_until_);
    if (sv == applied_sfx_ && mv == applied_music_ && !settling) {
        resync_until_ = 0;
        return;
    }
    bool any_fading = false;
    for (int ch = 0; ch < kChannels; ++ch) {
        // A channel mid-fade owns its volume; it is picked up once it settles.
        if (Mix_FadingChannel(ch) != MIX_NO_FADING) { any_fading = true; continue; }
        Mix_Volume(ch, ch == kMusicChannel ? mv : sv);
    }
    if (any_fading && !settling) resync_until_ = now + 1500;
    applied_sfx_ = sv; applied_music_ = mv;
}
void SoundManager::set_master_volume(int v) {
    master_vol_ = std::clamp(v, 0, MIX_MAX_VOLUME);
    apply_volumes();
}
void SoundManager::set_music_volume(int v) {
    music_vol_ = std::clamp(v, 0, MIX_MAX_VOLUME);
    apply_volumes();
}
int SoundManager::nudge_volume(int delta) {
    set_master_volume(master_vol_ + delta);
    return master_vol_;
}
bool SoundManager::toggle_mute() {
    muted_ = !muted_;
    apply_volumes();
    return muted_;
}

// ── one-shots ────────────────────────────────────────────────────────────────
void SoundManager::play_voice(int id) {
    float pan = pending_pan_;
    pending_pan_ = -1.0f;                 // consumed even if nothing plays
    if (!ready_ || muted_ || id < 0 || id >= kVoices) return;
    Voice& v = voices_[id];
    if (v.n <= 0) return;
    Uint32 now = SDL_GetTicks();
    if (v.played && now - v.last_ms < kRetriggerMs) return;   // no machine-gunning
    v.played = true;
    v.last_ms = now;
    int var = 0;
    if (v.n > 1) {                        // any variant but the last one used
        var = int(g_rng() % Uint32(v.n - 1));
        if (var >= v.last_var && v.last_var >= 0) ++var;
        var %= v.n;
    }
    v.last_var = var;

    int ch = Mix_GroupAvailable(kSfxGroup);
    if (ch < 0) {                         // should not happen (see below)
        ch = Mix_GroupOldest(kSfxGroup);
        if (ch < 0) return;
        Mix_HaltChannel(ch);
    }
    Mix_Volume(ch, sfx_volume());
    // Channel effects are dropped by SDL_mixer when a channel finishes, so an
    // idle channel is always centred; set the pan before the sound starts.
    if (pan >= 0.0f) {
        Uint8 l, r;
        pan_gains(pan, l, r);
        Mix_SetPanning(ch, l, r);
    }
    Mix_PlayChannel(ch, v.v[var], 0);
    // Keep one channel free: if that was the last, fade the oldest sound out
    // quickly (no click) so the next request never has to hard-cut anything.
    if (Mix_GroupAvailable(kSfxGroup) < 0) {
        int oldest = Mix_GroupOldest(kSfxGroup);
        if (oldest >= 0 && oldest != ch && Mix_FadingChannel(oldest) == MIX_NO_FADING) {
            Mix_FadeOutChannel(oldest, 25);
            resync_until_ = now + 1500;
        }
    }
}

void SoundManager::shoot()        { play_voice(S_SHOOT); }
void SoundManager::invader_hit()  { play_voice(S_INV_HIT); }
void SoundManager::player_death() { play_voice(S_DEATH); }
void SoundManager::bunker_hit()   { play_voice(S_BUNKER); }
void SoundManager::march_tick() {
    play_voice(S_MARCH0 + (march_idx_ & 3));
    march_idx_++;
}

void SoundManager::ball_wall()       { play_voice(S_WALL); }
void SoundManager::ball_paddle()     { play_voice(S_PADDLE); }
void SoundManager::brick_hit()       { play_voice(S_BRICK_HIT); }
void SoundManager::brick_break()     { play_voice(S_BRICK_BREAK); }
void SoundManager::life_lost()       { play_voice(S_LIFE_LOST); }
void SoundManager::level_clear()     { play_voice(S_LEVEL_CLEAR); }
void SoundManager::laser_shoot()     { play_voice(S_LASER); }
void SoundManager::powerup_collect() { play_voice(S_POWERUP); }

void SoundManager::dive_warble()    { play_voice(S_DIVE); }
void SoundManager::capture()        { play_voice(S_CAPTURE); }
void SoundManager::rescue()         { play_voice(S_RESCUE); }
void SoundManager::boss_explosion() { play_voice(S_BOSS_EXPL); }
void SoundManager::low_energy_pip() { play_voice(S_LOW_ENERGY); }
void SoundManager::spawn_chime()    { play_voice(S_SPAWN); }

void SoundManager::enemy_shot()     { play_voice(S_ENEMY_SHOT); }
void SoundManager::zap()            { play_voice(S_ZAP); }
void SoundManager::big_explosion()  { play_voice(S_BIG_EXPL); }
void SoundManager::shield_hit()     { play_voice(S_SHIELD); }
void SoundManager::alien_chirp(int v) { play_voice(S_CHIRP0 + std::clamp(v, 0, 2)); }
void SoundManager::warp()           { play_voice(S_WARP); }
void SoundManager::lock_on()        { play_voice(S_LOCK); }
void SoundManager::energy_refill()  { play_voice(S_REFILL); }
void SoundManager::beam_fire()      { play_voice(S_BEAM); }
void SoundManager::charged_blast(float charge) {
    play_voice(charge >= 0.80f ? S_CHARGED2 : charge >= 0.50f ? S_CHARGED1 : S_CHARGED0);
}

void SoundManager::boss_alarm()  { play_voice(S_BOSS_ALARM); }
void SoundManager::one_up()      { play_voice(S_ONE_UP); }
void SoundManager::game_over()   { play_voice(S_GAME_OVER); }
void SoundManager::armor_tink()  { play_voice(S_TINK); }
void SoundManager::ui_move()     { play_voice(S_UI_MOVE); }
void SoundManager::ui_select()   { play_voice(S_UI_SELECT); }

void SoundManager::pan_next(float x_frac) { pending_pan_ = x_frac; }

// ── loops ────────────────────────────────────────────────────────────────────
// Loops always start (silently if muted) and always fade in/out over a few
// tens of ms, so starting or stopping one can never pop.
void SoundManager::start_loop(int channel, Mix_Chunk* c, int fade_ms) {
    if (!ready_ || !c) return;
    if (Mix_Playing(channel)) {
        if (Mix_GetChunk(channel) == c && Mix_FadingChannel(channel) != MIX_FADING_OUT)
            return;                       // already running: don't restart it
        Mix_HaltChannel(channel);         // (restores the channel's own volume)
    }
    Mix_Volume(channel, channel == kMusicChannel ? music_channel_volume() : sfx_volume());
    Mix_FadeInChannel(channel, c, -1, std::max(1, fade_ms));
    resync_until_ = SDL_GetTicks() + Uint32(fade_ms) + 500;
}
void SoundManager::stop_loop(int channel, int fade_ms) {
    if (!ready_) return;
    if (Mix_Playing(channel) && Mix_FadingChannel(channel) != MIX_FADING_OUT) {
        Mix_FadeOutChannel(channel, std::max(1, fade_ms));
        resync_until_ = SDL_GetTicks() + Uint32(fade_ms) + 500;
    }
}

void SoundManager::start_ufo()     { start_loop(kUfoChannel, ufo_, 30); }
void SoundManager::stop_ufo()      { stop_loop(kUfoChannel, 60); }
void SoundManager::start_tractor() { start_loop(kTractorChannel, tractor_, 40); }
void SoundManager::stop_tractor()  { stop_loop(kTractorChannel, 80); }
void SoundManager::start_thrust()  { start_loop(kThrustChannel, thrust_, 60); }
void SoundManager::stop_thrust()   { stop_loop(kThrustChannel, 120); }

void SoundManager::start_charge() {
    if (!ready_ || !charge_rise_ || !charge_loop_) return;
    if (Mix_Playing(kCharge2Channel) && Mix_FadingChannel(kCharge2Channel) != MIX_FADING_OUT)
        return;                           // already charging
    Mix_HaltChannel(kChargeChannel);
    Mix_HaltChannel(kCharge2Channel);
    Mix_Volume(kChargeChannel, sfx_volume());
    Mix_Volume(kCharge2Channel, sfx_volume());
    Mix_PlayChannel(kChargeChannel, charge_rise_, 0);          // the climb…
    Mix_FadeInChannel(kCharge2Channel, charge_loop_, -1, 1100); // …into the held note
    resync_until_ = SDL_GetTicks() + 1600;
}
void SoundManager::stop_charge() {
    stop_loop(kChargeChannel, 40);
    stop_loop(kCharge2Channel, 40);
}

void SoundManager::start_ambient(int variant) {
    start_loop(kAmbientChannel, ambient_[std::clamp(variant, 0, 2)], 400);
}
void SoundManager::stop_ambient() { stop_loop(kAmbientChannel, 200); }

void SoundManager::set_ufo_pan(float x_frac) {
    if (!ready_ || !Mix_Playing(kUfoChannel)) return;
    Uint8 l, r;
    pan_gains(x_frac, l, r);
    Mix_SetPanning(kUfoChannel, l, r);
}

// ── music ────────────────────────────────────────────────────────────────────
void SoundManager::start_music(int track) {
    if (track < 0 || track >= kMusicTracks) { stop_music(); return; }
    music_track_ = track;
    if (!ready_) return;
    if (music_[track] && Mix_Playing(kMusicChannel) &&
        Mix_GetChunk(kMusicChannel) == music_[track] &&
        Mix_FadingChannel(kMusicChannel) != MIX_FADING_OUT)
        return;                           // this track is already playing
    Mix_HaltChannel(kMusicChannel);
    // Only the current track is kept in memory (each is a few MB of PCM);
    // rendering one takes a few milliseconds.
    for (int i = 0; i < kMusicTracks; ++i)
        if (i != track && music_[i]) { Mix_FreeChunk(music_[i]); music_[i] = nullptr; }
    if (!music_[track]) music_[track] = chunk_from(render_track(TRACKS[track]));
    start_loop(kMusicChannel, music_[track], 350);
}
void SoundManager::stop_music() {
    music_track_ = -1;
    stop_loop(kMusicChannel, 250);
}

// ── shared ───────────────────────────────────────────────────────────────────
void SoundManager::stop_all() {
    if (!ready_) return;
    music_track_ = -1;
    pending_pan_ = -1.0f;
    for (int ch = 0; ch < kChannels; ++ch)
        if (Mix_Playing(ch) && Mix_FadingChannel(ch) != MIX_FADING_OUT)
            Mix_FadeOutChannel(ch, 60);   // short fade: no pop, music included
    resync_until_ = SDL_GetTicks() + 600;
}
void SoundManager::shutdown() {
    if (!ready_) return;
    ready_ = false;
    Mix_HaltChannel(-1);
    auto drop = [](Mix_Chunk*& c) { if (c) { Mix_FreeChunk(c); c = nullptr; } };
    for (auto& v : voices_) {
        for (auto& c : v.v) drop(c);
        v.n = 0;
    }
    drop(ufo_); drop(tractor_); drop(thrust_); drop(charge_rise_); drop(charge_loop_);
    for (auto& c : ambient_) drop(c);
    for (auto& c : music_) drop(c);
    Mix_CloseAudio();
}

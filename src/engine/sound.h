// sound.h — procedural retro sound synthesis, ported from shared/sound.py.
// All sounds are generated as numpy-style float waveforms, converted to int16
// PCM and wrapped as SDL_mixer chunks. No audio files. Low-latency 512-sample
// buffer to keep action->sound tight (a key "feels native" factor).
#pragma once

#include <SDL_mixer.h>

class SoundManager {
public:
    bool init();          // open mixer + synthesize every chunk
    void shutdown();

    // ── Void Invaders ──
    void shoot();
    void invader_hit();
    void player_death();
    void bunker_hit();
    void march_tick();
    void start_ufo();
    void stop_ufo();

    // ── Void Breaker (arkanoid) ──
    void ball_wall();
    void ball_paddle();
    void brick_hit();
    void brick_break();
    void life_lost();
    void level_clear();
    void laser_shoot();
    void powerup_collect();

    // ── Void Squadrons (galaga) ──
    void dive_warble();
    void capture();
    void rescue();
    void boss_explosion();
    void low_energy_pip();
    void spawn_chime();
    void start_tractor();
    void stop_tractor();

    // ── newer-game / spectacular bank (shared) ──
    void enemy_shot();          // enemy fire — grittier, lower than the player shot
    void zap();                 // sharp energy zap (generic hit)
    void big_explosion();       // chain / charged destruction — bigger than boss_explosion
    void shield_hit();          // metallic shield ping
    void alien_chirp(int variant = 0);  // enemy vocalisation (0/1/2 flavours)
    void warp();                // teleport / hyperspace whoosh
    void lock_on();             // target-lock blip
    void energy_refill();       // energy tank / big pickup
    void beam_fire();           // bright convergent laser (void_gunner)
    // charge beam (void_storm): a looping rising hum while held, a payoff on release
    void start_charge();
    void stop_charge();
    void charged_blast();       // the spectacular charged-shot release

    // ── ambient bed (per-game looping drone) ──
    void start_ambient(int variant);   // 0 deep-space, 1 rift, 2 calm
    void stop_ambient();

    // ── spatialisation / mix ──
    void pan_next(float x_frac);        // pan the very next one-shot (0=left,1=right)
    void set_ufo_pan(float x_frac);     // continuously pan the UFO loop channel
    void set_master_volume(int v);      // 0..128
    int  master_volume() const { return master_vol_; }
    int  nudge_volume(int delta);       // adjust & return new master volume

    // ── shared ──
    bool toggle_mute();   // returns new muted state
    void stop_all();

private:
    void play(Mix_Chunk* c, int loops = 0, int channel = -1);

    bool ready_ = false;
    bool muted_ = false;
    int  march_idx_ = 0;
    int  master_vol_ = 100;     // 0..128 (MIX_MAX_VOLUME)
    float pending_pan_ = -1.0f; // >=0 means apply to next one-shot

    // Reserved looping-channel slots (one-shots are kept off these via
    // Mix_ReserveChannels so they can never steal a loop).
    static constexpr int kUfoChannel = 0;
    static constexpr int kTractorChannel = 1;
    static constexpr int kAmbientChannel = 2;
    static constexpr int kChargeChannel = 3;
    static constexpr int kReserved = 4;
    static constexpr int kChannels = 32;   // plenty of headroom for overlap
    static constexpr int kSfxGroup = 1;    // non-reserved one-shot channels

    Mix_Chunk* shoot_ = nullptr;
    Mix_Chunk* inv_hit_ = nullptr;
    Mix_Chunk* death_ = nullptr;
    Mix_Chunk* bunker_ = nullptr;
    Mix_Chunk* ufo_ = nullptr;
    Mix_Chunk* march_[4] = {};
    Mix_Chunk* wall_ = nullptr;
    Mix_Chunk* paddle_ = nullptr;
    Mix_Chunk* brick_hit_ = nullptr;
    Mix_Chunk* brick_break_ = nullptr;
    Mix_Chunk* life_lost_ = nullptr;
    Mix_Chunk* level_clear_ = nullptr;
    Mix_Chunk* laser_ = nullptr;
    Mix_Chunk* powerup_ = nullptr;
    Mix_Chunk* dive_ = nullptr;
    Mix_Chunk* tractor_ = nullptr;
    Mix_Chunk* capture_ = nullptr;
    Mix_Chunk* rescue_ = nullptr;
    Mix_Chunk* boss_expl_ = nullptr;
    Mix_Chunk* low_energy_ = nullptr;
    Mix_Chunk* spawn_ = nullptr;
    Mix_Chunk* ambient_[3] = {};
    // newer-game / spectacular bank
    Mix_Chunk* enemy_shot_ = nullptr;
    Mix_Chunk* zap_ = nullptr;
    Mix_Chunk* big_expl_ = nullptr;
    Mix_Chunk* shield_ = nullptr;
    Mix_Chunk* chirp_[3] = {};
    Mix_Chunk* warp_ = nullptr;
    Mix_Chunk* lock_ = nullptr;
    Mix_Chunk* refill_ = nullptr;
    Mix_Chunk* beam_ = nullptr;
    Mix_Chunk* charge_loop_ = nullptr;
    Mix_Chunk* charged_ = nullptr;
};

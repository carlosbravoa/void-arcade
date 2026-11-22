// sound.h — procedural retro sound synthesis. Every effect and every music
// track is generated at runtime as a float waveform, converted to int16 PCM
// and wrapped as an SDL_mixer chunk. No audio files. Low-latency 512-sample
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
    void charged_blast(float charge = 1.0f);  // charged-shot release; scales with charge 0..1

    // ── cues shared by every game ──
    void boss_alarm();          // boss arrival warning (NOT the player-death sound)
    void one_up();              // extra life
    void game_over();           // game-over sting
    void armor_tink();          // shot deflected / armour chipped, no kill
    void ui_move();             // menu cursor
    void ui_select();           // menu confirm
    void start_thrust();        // looping engine rumble (void_drift)
    void stop_thrust();
    // ── music: one procedural looping track per game ──
    // 0 invaders, 1 squadrons, 2 raid, 3 breaker, 4 drift, 5 gunner, 6 storm, 7 menu
    void start_music(int track);
    void stop_music();
    void set_music_volume(int v);       // 0..128, independent of master

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
    // One logical sound = up to 3 pre-rendered variants (slightly different
    // pitch/level) rotated at random so repeats don't machine-gun, plus the
    // time it last played so per-frame callers can't retrigger it every frame.
    struct Voice {
        Mix_Chunk* v[3] = {};
        int    n = 0;
        int    last_var = -1;
        Uint32 last_ms = 0;
        bool   played = false;
    };
    void play_voice(int id);
    void start_loop(int channel, Mix_Chunk* c, int fade_ms);
    void stop_loop(int channel, int fade_ms);
    void apply_volumes();
    int  sfx_volume() const;
    int  music_channel_volume() const;

    bool ready_ = false;
    bool muted_ = false;
    int  march_idx_ = 0;
    int  master_vol_ = 100;     // 0..128 (MIX_MAX_VOLUME)
    int  music_vol_ = 45;       // 0..128, multiplied by master
    int  music_track_ = -1;     // track currently requested (-1 = none)
    float pending_pan_ = -1.0f; // >=0 means apply to next one-shot
    // apply_volumes() runs every frame (main sets the master volume each frame)
    // so it only touches the mixer when a value changed or a fade is settling.
    int    applied_sfx_ = -1, applied_music_ = -1;
    Uint32 resync_until_ = 0;

    // Reserved looping-channel slots (one-shots are kept off these via
    // Mix_ReserveChannels so they can never steal a loop).
    static constexpr int kUfoChannel = 0;
    static constexpr int kTractorChannel = 1;
    static constexpr int kAmbientChannel = 2;
    static constexpr int kChargeChannel = 3;    // rising one-shot of the charge
    static constexpr int kCharge2Channel = 4;   // sustained top-of-charge loop
    static constexpr int kThrustChannel = 5;
    static constexpr int kMusicChannel = 6;
    static constexpr int kReserved = 7;
    static constexpr int kChannels = 32;   // plenty of headroom for overlap
    static constexpr int kSfxGroup = 1;    // non-reserved one-shot channels
    static constexpr int kMusicTracks = 8;
    static constexpr int kVoices = 64;

    Voice voices_[kVoices];
    Mix_Chunk* ufo_ = nullptr;
    Mix_Chunk* tractor_ = nullptr;
    Mix_Chunk* thrust_ = nullptr;
    Mix_Chunk* charge_rise_ = nullptr;
    Mix_Chunk* charge_loop_ = nullptr;
    Mix_Chunk* ambient_[3] = {};
    Mix_Chunk* music_[kMusicTracks] = {};   // rendered lazily on first use
};

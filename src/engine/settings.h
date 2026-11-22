// settings.h — player-tunable options, persisted to settings.json next to the
// scores file. Edited live by the in-game Options screen; applied by the app to
// the CRT renderer, sound, and window each frame.
#pragma once

struct Settings {
    float glow = 0.95f;        // CRT bloom intensity
    float persistence = 0.58f; // CRT phosphor afterglow (motion trails)
    float scanline = 0.18f;    // CRT scanline depth
    int   volume = 100;        // master volume 0..128
    int   music = 48;          // music volume 0..128 (scaled by master)
    bool  fullscreen = false;

    void load();
    void save() const;
    void reset_defaults() { *this = Settings{}; }

    // Slider bounds for the Options screen.
    static constexpr float GLOW_MIN = 0.0f, GLOW_MAX = 1.8f;
    static constexpr float PERS_MIN = 0.0f, PERS_MAX = 0.9f;
    static constexpr float SCAN_MIN = 0.0f, SCAN_MAX = 0.5f;
    static constexpr int   VOL_MIN = 0,     VOL_MAX = 128;
};

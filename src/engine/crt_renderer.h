// crt_renderer.h — CRT post-processing with phosphor persistence (afterglow).
//
// Multi-pass at game resolution (900x720), all in FBOs:
//   1. CRT shader (crt.vert/crt.frag, verbatim) : game texture -> scene FBO
//   2. combine : accum = max(scene, prevAccum * persistence)  (motion trails)
//   3. present : accum -> letterboxed viewport on the default framebuffer
#pragma once

#include <GL/gl.h>

#include <string>

class Surface;

class CRTRenderer {
public:
    CRTRenderer(int screen_w, int screen_h, const std::string& shader_dir);
    ~CRTRenderer();

    bool enabled() const { return enabled_; }
    void set_persistence(float p) { persistence_ = p; }
    void set_glow(float g) { glow_ = g; }            // bloom intensity
    void set_scanline(float s) { scanline_ = s; }    // scanline depth
    float persistence() const { return persistence_; }
    float glow() const { return glow_; }
    float scanline() const { return scanline_; }

    // Per-wave/zone global hue drift (radians) applied by the CRT pass.
    void set_hue(float h) { hue_ = h; }
    // Fire a one-shot screen-space shockwave ripple centred at (cx,cy) in
    // top-left fractional screen coords (matches game draw space).
    void trigger_shockwave(float cx, float cy) {
        shock_cx_ = cx; shock_cy_ = cy; shock_age_ = 0.0f; shock_on_ = true;
    }

    // Upload `surf`, run the full pipeline, and draw the result into the given
    // letterbox viewport. Clears the whole (win_w x win_h) framebuffer to black
    // first. Caller swaps buffers afterwards.
    void present(const Surface& surf, float dt, int win_w, int win_h,
                 int vx, int vy, int vw, int vh);

private:
    GLuint compile(GLenum type, const std::string& src, const char* label);
    GLuint link(const std::string& vsrc, const std::string& fsrc, const char* label);
    GLuint make_tex(bool nearest);
    GLuint make_fbo(GLuint tex);
    bool   build(const std::string& shader_dir);

    int    w_, h_;
    bool   enabled_ = false;
    float  time_ = 0.0f;
    float  persistence_ = 0.58f;  // phosphor afterglow — lowered to cut motion smear
    float  glow_ = 0.95f;         // bloom intensity (Options-tunable)
    float  scanline_ = 0.18f;     // scanline depth (Options-tunable)
    float  hue_ = 0.0f;

    // Shockwave state (advances in present()).
    static constexpr float kShockDur = 0.75f;   // seconds
    static constexpr float kShockSpeed = 1.7f;  // ring radius units per second
    bool   shock_on_ = false;
    float  shock_age_ = 0.0f, shock_cx_ = 0.5f, shock_cy_ = 0.5f;

    GLuint vao_ = 0, vbo_ = 0;
    GLuint tex_src_ = 0;
    GLuint prog_crt_ = 0, prog_combine_ = 0, prog_present_ = 0;
    GLuint fbo_scene_ = 0, tex_scene_ = 0;
    GLuint fbo_accum_[2] = {0, 0}, tex_accum_[2] = {0, 0};
    int    accum_cur_ = 0;
    bool   accum_primed_ = false;

    GLint u_crt_time_ = -1, u_crt_res_ = -1, u_crt_tex_ = -1;
    GLint u_crt_hue_ = -1, u_crt_shock_ = -1, u_crt_shock_r_ = -1, u_crt_shock_c_ = -1;
    GLint u_crt_glow_ = -1, u_crt_scan_ = -1;
    GLint u_comb_scene_ = -1, u_comb_prev_ = -1, u_comb_persist_ = -1;
    GLint u_pres_tex_ = -1;
};

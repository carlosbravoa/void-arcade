#include "crt_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "gl_loader.h"
#include "surface.h"

namespace {
std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

// Combine pass: persistence trails. New light overrides; old light decays.
const char* kCombineFrag = R"(#version 330 core
uniform sampler2D scene;
uniform sampler2D prev;
uniform float persistence;
in vec2 uv;
out vec4 fragColor;
void main() {
    vec3 s = texture(scene, uv).rgb;
    vec3 p = texture(prev, uv).rgb * persistence;
    fragColor = vec4(max(s, p), 1.0);
}
)";

const char* kPresentFrag = R"(#version 330 core
uniform sampler2D tex;
in vec2 uv;
out vec4 fragColor;
void main() { fragColor = vec4(texture(tex, uv).rgb, 1.0); }
)";

// Fallback when the CRT shader is unavailable: show the raw game surface
// (Y-flipped: the surface is top-left origin) so the game is still playable.
const char* kPlainVert = R"(#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
out vec2 uv;
void main() { uv = in_uv; gl_Position = vec4(in_pos, 0.0, 1.0); }
)";
const char* kPlainFrag = R"(#version 330 core
uniform sampler2D tex;
in vec2 uv;
out vec4 fragColor;
void main() { fragColor = vec4(texture(tex, vec2(uv.x, 1.0 - uv.y)).rgb, 1.0); }
)";
}  // namespace

CRTRenderer::CRTRenderer(int w, int h, const std::string& shader_dir)
    : w_(w), h_(h) {
    if (build(shader_dir)) {
        enabled_ = true;
        std::printf("[CRT] GL CRT shader + phosphor persistence active.\n");
    } else {
        std::printf("[CRT] Shader init failed — running without CRT shader.\n");
        build_plain();
    }
}

GLuint CRTRenderer::compile(GLenum type, const std::string& src, const char* label) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[CRT] %s compile error:\n%s\n", label, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint CRTRenderer::link(const std::string& vsrc, const std::string& fsrc,
                         const char* label) {
    GLuint vs = compile(GL_VERTEX_SHADER, vsrc, label);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc, label);
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[CRT] %s link error:\n%s\n", label, log);
        return 0;
    }
    return prog;
}

GLuint CRTRenderer::make_tex(bool nearest, int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    GLint f = nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

GLuint CRTRenderer::make_fbo(GLuint tex) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "[CRT] framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

void CRTRenderer::make_quad() {
    if (vao_) return;
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void CRTRenderer::build_plain() {
    prog_plain_ = link(kPlainVert, kPlainFrag, "plain");
    if (!prog_plain_) return;
    make_quad();
    if (!tex_src_) tex_src_ = make_tex(false, w_, h_);
    u_plain_tex_ = glGetUniformLocation(prog_plain_, "tex");
}

bool CRTRenderer::build(const std::string& shader_dir) {
    std::string vsrc = read_file(shader_dir + "/crt.vert");
    std::string fsrc = read_file(shader_dir + "/crt.frag");
    if (vsrc.empty() || fsrc.empty()) {
        std::fprintf(stderr, "[CRT] missing shader files in %s\n", shader_dir.c_str());
        return false;
    }
    prog_crt_ = link(vsrc, fsrc, "crt");
    prog_combine_ = link(vsrc, kCombineFrag, "combine");
    prog_present_ = link(vsrc, kPresentFrag, "present");
    if (!prog_crt_ || !prog_combine_ || !prog_present_) return false;
    out_w_ = w_; out_h_ = h_;

    make_quad();

    // The game surface is sampled with a sharp-bilinear kernel in the shader
    // (crisp texels, smooth edges), which needs LINEAR filtering here.
    tex_src_ = make_tex(false, w_, h_);
    tex_scene_ = make_tex(false, out_w_, out_h_);
    tex_accum_[0] = make_tex(false, out_w_, out_h_);
    tex_accum_[1] = make_tex(false, out_w_, out_h_);
    fbo_scene_ = make_fbo(tex_scene_);
    fbo_accum_[0] = make_fbo(tex_accum_[0]);
    fbo_accum_[1] = make_fbo(tex_accum_[1]);

    u_crt_time_ = glGetUniformLocation(prog_crt_, "time");
    u_crt_res_  = glGetUniformLocation(prog_crt_, "resolution");
    u_crt_out_  = glGetUniformLocation(prog_crt_, "out_resolution");
    u_crt_tex_  = glGetUniformLocation(prog_crt_, "screen_texture");
    u_crt_hue_     = glGetUniformLocation(prog_crt_, "hue");
    u_crt_shock_   = glGetUniformLocation(prog_crt_, "shock");
    u_crt_shock_r_ = glGetUniformLocation(prog_crt_, "shock_radius");
    u_crt_shock_c_ = glGetUniformLocation(prog_crt_, "shock_center");
    u_crt_glow_    = glGetUniformLocation(prog_crt_, "glow");
    u_crt_scan_    = glGetUniformLocation(prog_crt_, "scan_strength");
    u_comb_scene_   = glGetUniformLocation(prog_combine_, "scene");
    u_comb_prev_    = glGetUniformLocation(prog_combine_, "prev");
    u_comb_persist_ = glGetUniformLocation(prog_combine_, "persistence");
    u_pres_tex_ = glGetUniformLocation(prog_present_, "tex");
    return true;
}

void CRTRenderer::present(const Surface& surf, float dt, int win_w, int win_h,
                          int vx, int vy, int vw, int vh) {
    time_ += dt;
    if (time_ > 3600.0f) time_ -= 3600.0f;  // keep float precision for the flicker
    if (!enabled_) {
        glViewport(0, 0, win_w, win_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!prog_plain_) return;
        glDisable(GL_BLEND);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_src_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE,
                        surf.data());
        glViewport(vx, vy, vw, vh);
        glUseProgram(prog_plain_);
        glUniform1i(u_plain_tex_, 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        return;
    }

    // Run the CRT passes at the size the image is actually shown at, so
    // scanlines, curvature and bloom stay crisp in a big window or fullscreen
    // instead of being a 900x720 image stretched afterwards.
    int ow = std::clamp(vw, 16, 8192), oh = std::clamp(vh, 16, 8192);
    if (ow != out_w_ || oh != out_h_) {
        out_w_ = ow; out_h_ = oh;
        for (GLuint t : {tex_scene_, tex_accum_[0], tex_accum_[1]}) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, out_w_, out_h_, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
        }
        accum_primed_ = false;  // old trails are the wrong size — start clean
    }

    glDisable(GL_BLEND);
    glBindVertexArray(vao_);

    // Upload the finished game surface.
    glBindTexture(GL_TEXTURE_2D, tex_src_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE,
                    surf.data());

    // Pass 1 — CRT effects into the scene FBO.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_scene_);
    glViewport(0, 0, out_w_, out_h_);
    glUseProgram(prog_crt_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_src_);
    glUniform1i(u_crt_tex_, 0);
    glUniform1f(u_crt_time_, time_);
    glUniform2f(u_crt_res_, float(w_), float(h_));
    glUniform2f(u_crt_out_, float(out_w_), float(out_h_));
    glUniform1f(u_crt_hue_, hue_);
    glUniform1f(u_crt_glow_, glow_);
    glUniform1f(u_crt_scan_, scanline_);

    // Advance + upload the shockwave ripple.
    float shock_env = 0.0f, shock_radius = 0.0f;
    if (shock_on_) {
        shock_age_ += dt;
        if (shock_age_ >= kShockDur) {
            shock_on_ = false;
        } else {
            float t = shock_age_ / kShockDur;       // 0..1
            shock_env = (1.0f - t) * (1.0f - t);    // ease-out fade
            shock_radius = shock_age_ * kShockSpeed;
        }
    }
    glUniform1f(u_crt_shock_, shock_env);
    glUniform1f(u_crt_shock_r_, shock_radius);
    glUniform2f(u_crt_shock_c_, shock_cx_, shock_cy_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    int prev = accum_cur_;
    int cur = 1 - accum_cur_;

    // Pass 2 — accumulate persistence: accum = max(scene, prev*persistence).
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_accum_[cur]);
    glViewport(0, 0, out_w_, out_h_);
    glUseProgram(prog_combine_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_scene_);
    glUniform1i(u_comb_scene_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_accum_[prev]);
    glUniform1i(u_comb_prev_, 1);
    // `persistence_` is the fraction of light kept per 1/60 s; convert to this
    // frame's dt so trails are the same length at 60, 144 or 240 Hz.
    float keep = persistence_ > 0.0f ? std::pow(persistence_, dt * 60.0f) : 0.0f;
    glUniform1f(u_comb_persist_, accum_primed_ ? keep : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    accum_primed_ = true;

    // Pass 3 — present accumulation into the letterbox viewport on screen.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vx, vy, vw, vh);
    glUseProgram(prog_present_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_accum_[cur]);
    glUniform1i(u_pres_tex_, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    accum_cur_ = cur;
}

CRTRenderer::~CRTRenderer() {
    if (tex_src_) glDeleteTextures(1, &tex_src_);
    if (tex_scene_) glDeleteTextures(1, &tex_scene_);
    glDeleteTextures(2, tex_accum_);
    if (fbo_scene_) glDeleteFramebuffers(1, &fbo_scene_);
    glDeleteFramebuffers(2, fbo_accum_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_crt_) glDeleteProgram(prog_crt_);
    if (prog_combine_) glDeleteProgram(prog_combine_);
    if (prog_present_) glDeleteProgram(prog_present_);
    if (prog_plain_) glDeleteProgram(prog_plain_);
}

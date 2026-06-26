#include "gl_loader.h"

#include <SDL.h>
#include <cstdio>

PFNGLCREATESHADERPROC             glCreateShader             = nullptr;
PFNGLSHADERSOURCEPROC             glShaderSource             = nullptr;
PFNGLCOMPILESHADERPROC            glCompileShader            = nullptr;
PFNGLGETSHADERIVPROC              glGetShaderiv              = nullptr;
PFNGLGETSHADERINFOLOGPROC         glGetShaderInfoLog         = nullptr;
PFNGLDELETESHADERPROC             glDeleteShader             = nullptr;
PFNGLCREATEPROGRAMPROC            glCreateProgram            = nullptr;
PFNGLATTACHSHADERPROC             glAttachShader             = nullptr;
PFNGLLINKPROGRAMPROC              glLinkProgram              = nullptr;
PFNGLGETPROGRAMIVPROC             glGetProgramiv             = nullptr;
PFNGLGETPROGRAMINFOLOGPROC        glGetProgramInfoLog        = nullptr;
PFNGLUSEPROGRAMPROC               glUseProgram               = nullptr;
PFNGLDELETEPROGRAMPROC            glDeleteProgram            = nullptr;
PFNGLGENBUFFERSPROC               glGenBuffers               = nullptr;
PFNGLBINDBUFFERPROC               glBindBuffer               = nullptr;
PFNGLBUFFERDATAPROC               glBufferData               = nullptr;
PFNGLDELETEBUFFERSPROC            glDeleteBuffers            = nullptr;
PFNGLGENVERTEXARRAYSPROC          glGenVertexArrays          = nullptr;
PFNGLBINDVERTEXARRAYPROC          glBindVertexArray          = nullptr;
PFNGLDELETEVERTEXARRAYSPROC       glDeleteVertexArrays       = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC  glEnableVertexAttribArray  = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC      glVertexAttribPointer      = nullptr;
PFNGLGETUNIFORMLOCATIONPROC       glGetUniformLocation       = nullptr;
PFNGLUNIFORM1IPROC                glUniform1i                = nullptr;
PFNGLUNIFORM1FPROC                glUniform1f                = nullptr;
PFNGLUNIFORM2FPROC                glUniform2f                = nullptr;
PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers          = nullptr;
PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer          = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC     glFramebufferTexture2D     = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC       glDeleteFramebuffers       = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus   = nullptr;

namespace {
bool g_ok = true;

template <typename T>
void resolve(T& ptr, const char* name) {
    ptr = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!ptr) {
        std::fprintf(stderr, "[gl] failed to load %s\n", name);
        g_ok = false;
    }
}
}  // namespace

namespace gl {

bool load() {
    g_ok = true;
#define GL_RESOLVE(fn) resolve(fn, #fn)
    GL_RESOLVE(glCreateShader);
    GL_RESOLVE(glShaderSource);
    GL_RESOLVE(glCompileShader);
    GL_RESOLVE(glGetShaderiv);
    GL_RESOLVE(glGetShaderInfoLog);
    GL_RESOLVE(glDeleteShader);
    GL_RESOLVE(glCreateProgram);
    GL_RESOLVE(glAttachShader);
    GL_RESOLVE(glLinkProgram);
    GL_RESOLVE(glGetProgramiv);
    GL_RESOLVE(glGetProgramInfoLog);
    GL_RESOLVE(glUseProgram);
    GL_RESOLVE(glDeleteProgram);
    GL_RESOLVE(glGenBuffers);
    GL_RESOLVE(glBindBuffer);
    GL_RESOLVE(glBufferData);
    GL_RESOLVE(glDeleteBuffers);
    GL_RESOLVE(glGenVertexArrays);
    GL_RESOLVE(glBindVertexArray);
    GL_RESOLVE(glDeleteVertexArrays);
    GL_RESOLVE(glEnableVertexAttribArray);
    GL_RESOLVE(glVertexAttribPointer);
    GL_RESOLVE(glGetUniformLocation);
    GL_RESOLVE(glUniform1i);
    GL_RESOLVE(glUniform1f);
    GL_RESOLVE(glUniform2f);
    GL_RESOLVE(glGenFramebuffers);
    GL_RESOLVE(glBindFramebuffer);
    GL_RESOLVE(glFramebufferTexture2D);
    GL_RESOLVE(glDeleteFramebuffers);
    GL_RESOLVE(glCheckFramebufferStatus);
#undef GL_RESOLVE
    return g_ok;
}

}  // namespace gl

// gl_loader.h — minimal OpenGL 3.3 core function loader.
//
// We deliberately avoid GLEW/GLAD/epoxy (none are installed) to keep the
// binary self-contained. <GL/gl.h> provides the classic GL 1.1 entry points
// (glViewport, glClear, glGenTextures, glTexImage2D, ...) which libGL exports
// directly. Everything newer than 1.1 is declared here as a function pointer
// and resolved at runtime through SDL_GL_GetProcAddress().
//
// IMPORTANT: do NOT define GL_GLEXT_PROTOTYPES — we want glext.h to give us the
// PFNGL*PROC typedefs and enum constants only, not real prototypes that would
// collide with the pointers below.
#pragma once

#include <GL/gl.h>
#include <GL/glext.h>

namespace gl {

// Resolve all modern GL entry points. Returns false if any required pointer
// could not be loaded. Call once after a GL context is current.
bool load();

}  // namespace gl

// ── modern GL entry points, defined in gl_loader.cpp ─────────────────────────
// Note: glActiveTexture is a real prototype in Mesa's gl.h (GL 1.3) and is
// exported by libGL, so we call it directly rather than loading a pointer.
extern PFNGLCREATESHADERPROC             glCreateShader;
extern PFNGLSHADERSOURCEPROC             glShaderSource;
extern PFNGLCOMPILESHADERPROC            glCompileShader;
extern PFNGLGETSHADERIVPROC              glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC         glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC             glDeleteShader;
extern PFNGLCREATEPROGRAMPROC            glCreateProgram;
extern PFNGLATTACHSHADERPROC             glAttachShader;
extern PFNGLLINKPROGRAMPROC              glLinkProgram;
extern PFNGLGETPROGRAMIVPROC             glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC        glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC               glUseProgram;
extern PFNGLDELETEPROGRAMPROC            glDeleteProgram;
extern PFNGLGENBUFFERSPROC               glGenBuffers;
extern PFNGLBINDBUFFERPROC               glBindBuffer;
extern PFNGLBUFFERDATAPROC               glBufferData;
extern PFNGLDELETEBUFFERSPROC            glDeleteBuffers;
extern PFNGLGENVERTEXARRAYSPROC          glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC          glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC       glDeleteVertexArrays;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC  glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC      glVertexAttribPointer;
extern PFNGLGETUNIFORMLOCATIONPROC       glGetUniformLocation;
extern PFNGLUNIFORM1IPROC                glUniform1i;
extern PFNGLUNIFORM1FPROC                glUniform1f;
extern PFNGLUNIFORM2FPROC                glUniform2f;
extern PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC     glFramebufferTexture2D;
extern PFNGLDELETEFRAMEBUFFERSPROC       glDeleteFramebuffers;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus;

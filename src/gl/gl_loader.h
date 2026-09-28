// Minimal hand-written OpenGL 4.6 core entry-point loader (no external deps).
// Loads dynamically from opengl32.dll + wglGetProcAddress so the lab stays isolated.
#pragma once
#include <windows.h>
#include <cstddef>

using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLchar = char;
using GLhalf = unsigned short;
using GLuint64 = unsigned __int64;

// <GL/gl.h> declares the OpenGL 1.1 entry points as real exported functions and
// expects the caller to link opengl32.lib. This lab loads every entry point
// dynamically instead, so the 1.1 declarations are renamed out of the way via
// macros before gl.h is included, then undefined. Types and enums are kept.
#define glClear          glClear_legacy_unused
#define glClearColor     glClearColor_legacy_unused
#define glCullFace       glCullFace_legacy_unused
#define glDepthFunc      glDepthFunc_legacy_unused
#define glDepthMask      glDepthMask_legacy_unused
#define glDisable        glDisable_legacy_unused
#define glDrawBuffer     glDrawBuffer_legacy_unused
#define glDrawBuffers    glDrawBuffers_legacy_unused
#define glEnable         glEnable_legacy_unused
#define glFrontFace      glFrontFace_legacy_unused
#define glGetError       glGetError_legacy_unused
#define glGetIntegerv    glGetIntegerv_legacy_unused
#define glGetString      glGetString_legacy_unused
#define glPixelStorei    glPixelStorei_legacy_unused
#define glReadBuffer     glReadBuffer_legacy_unused
#define glScissor        glScissor_legacy_unused
#define glViewport       glViewport_legacy_unused
#define glBindTexture    glBindTexture_legacy_unused
#define glGenTextures    glGenTextures_legacy_unused
#define glTexImage2D     glTexImage2D_legacy_unused
#define glTexParameteri  glTexParameteri_legacy_unused
#define glFinish         glFinish_legacy_unused
#define glFlush          glFlush_legacy_unused
#define glDrawArrays     glDrawArrays_legacy_unused
#define glDrawElements   glDrawElements_legacy_unused
#define glDeleteTextures glDeleteTextures_legacy_unused

#include <GL/gl.h>

#undef glClear
#undef glClearColor
#undef glCullFace
#undef glDepthFunc
#undef glDepthMask
#undef glDisable
#undef glDrawBuffer
#undef glDrawBuffers
#undef glEnable
#undef glFrontFace
#undef glGetError
#undef glGetIntegerv
#undef glGetString
#undef glPixelStorei
#undef glReadBuffer
#undef glScissor
#undef glViewport
#undef glBindTexture
#undef glGenTextures
#undef glTexImage2D
#undef glTexParameteri
#undef glFinish
#undef glFlush
#undef glDrawArrays
#undef glDrawElements
#undef glDeleteTextures

#if defined(_M_X64) || defined(__x86_64__)
#define LAB_GLAPI
#else
#define LAB_GLAPI __stdcall
#endif

// The SDK's gl.h has no glext.h, so the PFNGL*PROC typedefs we need are
// declared here. Signatures mirror the GL API exactly.
#define LAB_GL_TYPEDEFS_A(X)                                                    \
  X(PFNGLGETSTRINGPROC, glGetString, const GLubyte*, GLenum) \
  X(PFNGLGETINTEGERVPROC, glGetIntegerv, void, GLenum, GLint*) \
  X(PFNGLGETERRORPROC, glGetError, GLenum, void) \
  X(PFNGLENABLEPROC, glEnable, void, GLenum) \
  X(PFNGLDISABLEPROC, glDisable, void, GLenum) \
  X(PFNGLVIEWPORTPROC, glViewport, void, GLint, GLint, GLsizei, GLsizei) \
  X(PFNGLCLEARCOLORPROC, glClearColor, void, GLfloat, GLfloat, GLfloat, GLfloat) \
  X(PFNGLCLEARPROC, glClear, void, GLbitfield) \
  X(PFNGLDEPTHFUNCPROC, glDepthFunc, void, GLenum) \
  X(PFNGLDEPTHMASKPROC, glDepthMask, void, GLboolean) \
  X(PFNGLCULLFACEPROC, glCullFace, void, GLenum) \
  X(PFNGLFRONTFACEPROC, glFrontFace, void, GLenum) \
  X(PFNGLSCISSORPROC, glScissor, void, GLint, GLint, GLsizei, GLsizei) \
  X(PFNGLDRAWBUFFERSPROC, glDrawBuffers, void, GLsizei, const GLenum*) \
  X(PFNGLDRAWBUFFERPROC, glDrawBuffer, void, GLenum) \
  X(PFNGLREADBUFFERPROC, glReadBuffer, void, GLenum) \
  X(PFNGLPIXELSTOREIPROC, glPixelStorei, void, GLenum, GLint) \
  X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced, void, GLenum, GLint, GLsizei, GLenum, const void*, GLsizei) \
  X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced, void, GLenum, GLint, GLsizei, GLsizei) \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer, void, GLuint, GLint, GLint, GLboolean, GLsizei, const void*) \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray, void, GLuint) \
  X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor, void, GLuint, GLuint) \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray, void, GLuint) \
  X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays, void, GLsizei, GLuint*) \
  X(PFNGLDRAWARRAYSPROC, glDrawArrays, void, GLenum, GLint, GLsizei) \
  X(PFNGLDRAWELEMENTSPROC, glDrawElements, void, GLenum, GLint, GLenum, const void*) \
  X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus, GLenum, GLenum)


#define LAB_GL_TYPEDEFS_B(X)                                                    \
  X(PFNGLBUFFERSUBDATAPROC, glBufferSubData, void, GLenum, GLintptr, GLsizeiptr, const void*) \
  X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange, void, GLenum, GLuint, GLuint, GLuint, GLintptr, GLsizeiptr) \
  X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase, void, GLenum, GLuint, GLuint) \
  X(PFNGLGENBUFFERSPROC, glGenBuffers, void, GLsizei, GLuint*) \
  X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers, void, GLsizei, const GLuint*) \
  X(PFNGLBINDBUFFERPROC, glBindBuffer, void, GLenum, GLuint) \
  X(PFNGLBUFFERDATAPROC, glBufferData, void, GLenum, GLsizeiptr, const void*, GLenum) \
  X(PFNGLGETBUFFERSUBDATAPROC, glGetBufferSubData, void, GLenum, GLintptr, GLsizeiptr, void*) \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture, void, GLenum) \
  X(PFNGLGENTEXTURESPROC, glGenTextures, void, GLsizei, GLuint*) \
  X(PFNGLBINDTEXTUREPROC, glBindTexture, void, GLenum, GLuint) \
  X(PFNGLTEXIMAGE2DPROC, glTexImage2D, void, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
  X(PFNGLTEXPARAMETERIPROC, glTexParameteri, void, GLenum, GLenum, GLint) \
  X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap, void, GLenum) \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers, void, GLsizei, GLuint*) \
  X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer, void, GLenum, GLuint) \
  X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer, void, GLenum, GLenum, GLenum, GLuint) \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D, void, GLenum, GLenum, GLenum, GLuint, GLint) \
  X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers, void, GLsizei, GLuint*) \
  X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer, void, GLenum, GLuint) \
  X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage, void, GLenum, GLenum, GLsizei, GLsizei)

#define LAB_GL_TYPEDEFS_C(X)                                                    \
  X(PFNGLCREATESHADERPROC, glCreateShader, GLuint, GLenum) \
  X(PFNGLSHADERSOURCEPROC, glShaderSource, void, GLuint, GLsizei, const GLchar* const*, const GLint*) \
  X(PFNGLCOMPILESHADERPROC, glCompileShader, void, GLuint) \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv, void, GLuint, GLenum, GLint*) \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog, void, GLuint, GLsizei, GLsizei*, GLchar*) \
  X(PFNGLDELETESHADERPROC, glDeleteShader, void, GLuint) \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram, GLuint, void) \
  X(PFNGLATTACHSHADERPROC, glAttachShader, void, GLuint, GLuint) \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram, void, GLuint) \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv, void, GLuint, GLenum, GLint*) \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog, void, GLuint, GLsizei, GLsizei*, GLchar*) \
  X(PFNGLUSEPROGRAMPROC, glUseProgram, void, GLuint) \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation, GLint, GLuint, const GLchar*) \
  X(PFNGLUNIFORM1IPROC, glUniform1i, void, GLint, GLint) \
  X(PFNGLUNIFORM1UIPROC, glUniform1ui, void, GLint, GLuint) \
  X(PFNGLUNIFORM1FPROC, glUniform1f, void, GLint, GLfloat) \
  X(PFNGLUNIFORM2FPROC, glUniform2f, void, GLint, GLfloat, GLfloat) \
  X(PFNGLUNIFORM3UIVPROC, glUniform3uiv, void, GLint, GLsizei, const GLuint*) \
  X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv, void, GLint, GLsizei, GLboolean, const GLfloat*) \
  X(PFNGLDISPATCHCOMPUTEPROC, glDispatchCompute, void, GLuint, GLuint, GLuint) \
  X(PFNGLMEMORYBARRIERPROC, glMemoryBarrier, void, GLbitfield) \
  X(PFNGLFINISHPROC, glFinish, void, void) \
  X(PFNGLFLUSHPROC, glFlush, void, void) \
  X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v, void, GLuint, GLenum, GLuint64*) \
  X(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv, void, GLuint, GLenum, GLint*) \
  X(PFNGLQUERYCOUNTERPROC, glQueryCounter, void, GLuint, GLenum)                   \
  X(PFNGLDELETEPROGRAMPROC, glDeleteProgram, void, GLuint)                         \
  X(PFNGLSHADERPROGRAMPROC, glDetachShader, void, GLuint, GLuint)                   \
  X(PFNGLDELETETEXTURESPROC, glDeleteTextures, void, GLsizei, const GLuint*)         \
  X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers, void, GLsizei, const GLuint*) \
  X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers, void, GLsizei, const GLuint*)  \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays, void, GLsizei, const GLuint*)       \
  X(PFNGLBEGINQUERYPROC, glBeginQuery, void, GLenum, GLuint)                       \
  X(PFNGLENDQUERYPROC, glEndQuery, void, GLenum)                                   \
  X(PFNGLGENQUERIESPROC, glGenQueries, void, GLsizei, GLuint*)                      \
  X(PFNGLDELETEQUERIESPROC, glDeleteQueries, void, GLsizei, const GLuint*)

#define LAB_GL_TYPEDEFS(X)                                                      \
  LAB_GL_TYPEDEFS_A(X) LAB_GL_TYPEDEFS_B(X) LAB_GL_TYPEDEFS_C(X)

// Emit the PFN typedef itself from the (pfn, name, ret, args...) quadruple.
#define X(pfn, name, ret, ...) typedef ret(LAB_GLAPI* pfn)(__VA_ARGS__);
LAB_GL_TYPEDEFS(X)
#undef X

// Function pointer objects, one per entry point.
#define X(pfn, name, ...) extern pfn name;
LAB_GL_TYPEDEFS(X)
#undef X

// Enums / constants absent from <GL/gl.h> that the lab relies on.
// NOTE: <GL/gl.h> already #defines the 1.1-era names (GL_TEXTURE_2D,
// GL_NEAREST, GL_COLOR_BUFFER_BIT, GL_LESS, GL_CULL_FACE, GL_BACK, GL_CCW,
// GL_UNPACK_ALIGNMENT, GL_FRAMEBUFFER, GL_RENDERBUFFER, GL_COLOR_ATTACHMENT0,
// GL_DEPTH_ATTACHMENT, GL_TEXTURE0). Re-declaring those would be a macro
// redefinition, so only the post-1.1 values are supplied here.
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_SHADER_STORAGE_BUFFER = 0x90D2;
constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;
constexpr GLenum GL_DEPTH_COMPONENT32F = 0x8CAC;
constexpr GLenum GL_RGBA16F = 0x881A;
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_SHADER_STORAGE_BARRIER_BIT = 0x00002000;
constexpr GLenum GL_TEXTURE_FETCH_BARRIER_BIT = 0x00000008;
constexpr GLenum GL_TIME_ELAPSED = 0x88BF;
constexpr GLenum GL_QUERY_RESULT = 0x8866;
constexpr GLenum GL_QUERY_RESULT_AVAILABLE = 0x8867;
constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
constexpr GLenum GL_MAX_COMPUTE_WORK_GROUP_COUNT = 0x91BE;
constexpr GLenum GL_MAX_COMPUTE_WORK_GROUP_SIZE = 0x91BF;
constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COLOR_ATTACHMENT1 = 0x8CE1;
// FBO / texture-state enums that live in glext.h, not the SDK's gl.h.
// The rest (GL_NEAREST, GL_RGBA, GL_FLOAT, GL_TRIANGLES, GL_CLAMP_TO_EDGE,
// GL_TEXTURE_*_FILTER/WRAP_*) are already #defined by gl.h.
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE0 = 0x84C0;

// Loads every entry point listed above. Returns false on the first missing one.
bool LoadOpenGL();
const char* GLLoaderLastError();

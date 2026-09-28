#include "gl_loader.h"
#include <cstdio>

// Define one function pointer object per entry point.
#define X(pfn, name, ...) pfn name = nullptr;
LAB_GL_TYPEDEFS(X)
#undef X

static const char* g_lastError = "";

const char* GLLoaderLastError() { return g_lastError; }

bool LoadOpenGL() {
  HMODULE gl = GetModuleHandleA("opengl32.dll");
  if (!gl) gl = LoadLibraryA("opengl32.dll");
  if (!gl) { g_lastError = "opengl32.dll not found"; return false; }

  bool ok = true;
#define X(pfn, name, ...)                                                      \
  {                                                                            \
    /* wglGetProcAddress FIRST. Everything above OpenGL 1.1 lives in the     */ \
    /* vendor ICD and is ONLY reachable through wglGetProcAddress, and only  */ \
    /* after a context is current. GetProcAddress(opengl32, ...) only finds  */ \
    /* the 1.1 exports, so preferring it yields NULL for every modern entry  */ \
    /* point and every compute call silently fails.                           */ \
    pfn resolved = reinterpret_cast<pfn>(wglGetProcAddress(#name));            \
    if (!resolved) resolved = reinterpret_cast<pfn>(GetProcAddress(gl, #name)); \
    if (!resolved) {                                                           \
      g_lastError = "missing GL entry point: " #name;                          \
      ok = false;                                                              \
    }                                                                          \
    name = resolved;                                                           \
  }
  LAB_GL_TYPEDEFS(X)
#undef X
  return ok;
}

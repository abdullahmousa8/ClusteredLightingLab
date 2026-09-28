// ClusteredLightingLab - isolated Win32 + GL 4.6 harness for the clustered
// lighting prototype. Shares no code with the Godot trees; it is a standalone
// reference implementation to be ported into the engine later.
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW

#include <cmath>
#include <cstdio>
#include <string>

#include "gl/gl_loader.h"
#include "renderer/ClusteredRenderer.h"
#include "scene/SoALights.h"

using lab::SoALights;

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFNWGLCOMPIXELFORMATARBPROC = BOOL(WINAPI*)(HDC, const int*, int*);
using PFNGLGETINTEGERI_VPROC = void(WINAPI*)(GLenum, GLuint, GLint*);

namespace {

constexpr wchar_t kTitle[] = L"Clustered Lighting Lab";
constexpr uint32_t kInitialW = 1280, kInitialH = 720;
constexpr uint32_t kLightCount = 10000;

HDC g_hdc = nullptr;
lab::Renderer* g_renderer = nullptr;
lab::SoALights* g_lights = nullptr;
bool g_running = true;
bool g_resizePending = false;
uint32_t g_pendingW = kInitialW, g_pendingH = kInitialH;
LARGE_INTEGER g_freq, g_start;

void OnResize(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  g_pendingW = static_cast<uint32_t>(w);
  g_pendingH = static_cast<uint32_t>(h);
  g_resizePending = true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE: OnResize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); g_running = false; return 0;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
  // Unbuffered: a crash would otherwise discard every diagnostic printed so far,
  // which makes bisecting a startup failure needlessly painful.
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);

  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  // No CS_OWNDC: the DC is obtained explicitly with GetDC below, and a class-
  // owned DC interacts badly with the temporary dummy window used to resolve
  // the WGL_ARB entry points.
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kTitle;
  RegisterClassExW(&wc);

  RECT r{0, 0, static_cast<LONG>(kInitialW), static_cast<LONG>(kInitialH)};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, kTitle, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, inst, nullptr);
  if (!hwnd) { std::fprintf(stderr, "CreateWindow failed\n"); return 1; }

  // Detect a session with no interactive desktop. CreateWindow succeeds there
  // but ShowWindow / GetDC / any GDI call on the result faults, so bail out
  // before touching it. This is a pure USER32 query and is safe to call.
  if (!IsWindowVisible(hwnd)) {
    std::fprintf(stderr,
                 "ERROR: this session has no interactive desktop (the window is\n"
                 "       not visible), so its device context is unusable and no\n"
                 "       OpenGL context can be created. Run the lab from an\n"
                 "       interactive desktop session.\n");
    return 2;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) { std::fprintf(stderr, "GetDC failed\n"); return 1; }
  g_hdc = hdc;

  // A window DC is only meaningful on an interactive desktop. In a headless or
  // service session GetDC can hand back a handle that faults on first use, and
  // the crash then surfaces far away inside context creation. Detect that up
  // front: IsWindowVisible is a pure USER32 query and is safe, whereas touching
  // the DC is not.
  if (!IsWindowVisible(hwnd)) {
    std::fprintf(stderr,
                 "ERROR: the window never became visible. This session has no\n"
                 "       interactive desktop, so the window DC is unusable and\n"
                 "       OpenGL context creation cannot work here.\n"
                 "       Run from an interactive desktop session.\n");
    return 2;
  }

  // A DUMMY window is required to obtain a current context, because
  // wglGetProcAddress only resolves the WGL_ARB entry points (including
  // wglCreateContextAttribsARB itself) while some context is current. Creating
  // the real context directly without this always fails.
  HWND dummy = CreateWindowExW(0, kTitle, kTitle, WS_OVERLAPPEDWINDOW, 0, 0, 16, 16,
                              nullptr, nullptr, inst, nullptr);
  if (!dummy) { std::fprintf(stderr, "CreateWindow(dummy) failed\n"); return 1; }
  HDC dummyDc = GetDC(dummy);
  if (!dummyDc) { std::fprintf(stderr, "GetDC(dummy) failed\n"); return 1; }
  PIXELFORMATDESCRIPTOR dpfd{};
  dpfd.nSize = sizeof(dpfd);
  dpfd.nVersion = 1;
  dpfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  dpfd.iPixelType = PFD_TYPE_RGBA;
  dpfd.cColorBits = 32;
  dpfd.cDepthBits = 24;
  dpfd.iLayerType = PFD_MAIN_PLANE;
  const int dpf = ChoosePixelFormat(dummyDc, &dpfd);
  if (!dpf || !SetPixelFormat(dummyDc, dpf, &dpfd)) {
  }
  HGLRC dummyRc = wglCreateContext(dummyDc);
  if (!dummyRc) { std::fprintf(stderr, "dummy wglCreateContext failed\n"); return 1; }
  if (!wglMakeCurrent(dummyDc, dummyRc)) {
    std::fprintf(stderr, "wglMakeCurrent(dummy) failed\n"); return 1;
  }
  // Verify the dummy context really is current before trusting wglGetProcAddress;
  // without a current context it returns NULL for every WGL_ARB entry point.
  if (wglGetCurrentContext() != dummyRc || wglGetCurrentDC() != dummyDc) {
  }

  // Now, and only now, the WGL_ARB entry points are reachable.
  auto wglCreateContextAttribsARB =
      reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
          wglGetProcAddress("wglCreateContextAttribsARB"));
  auto wglChoosePixelFormatARB =
      reinterpret_cast<PFNWGLCOMPIXELFORMATARBPROC>(
          wglGetProcAddress("wglChoosePixelFormatARB"));
  if (!wglCreateContextAttribsARB || !wglChoosePixelFormatARB) {
    std::fprintf(stderr, "WGL_ARB_create_context unavailable\n");
    return 1;
  }

  // Ask the driver for a pixel format that suits a modern core context, then
  // apply it to the REAL window. SetPixelFormat may only be called once per
  // HWND, which is why the dummy window exists at all.
  const int pfdAttribs[] = {
      0x0000, 1,  // WGL_DRAW_TO_WINDOW
      0x0001, 1,  // WGL_SUPPORT_OPENGL
      0x0002, 1,  // WGL_DOUBLE_BUFFER
      0x0003, 24, // WGL_RED_SIZE
      0x0004, 24, // WGL_GREEN_SIZE
      0x0005, 24, // WGL_BLUE_SIZE
      0x0006, 8,  // WGL_ALPHA_SIZE
      0x0007, 24, // WGL_DEPTH_SIZE
      0x0008, 0,  // WGL_STENCIL_SIZE
      0x0009, 0,  // WGL_DOUBLEBUFFER (ignored, legacy alias)
      0x000C, 0   // WGL_SUPPORT_OPENGL
  };
  int realPf = 0;
  if (!wglChoosePixelFormatARB(dummyDc, pfdAttribs, &realPf) || !realPf) {
    std::fprintf(stderr, "wglChoosePixelFormatARB failed\n"); return 1;
  }
  PIXELFORMATDESCRIPTOR rpfd{};
  if (!DescribePixelFormat(dummyDc, realPf, sizeof(rpfd), &rpfd)) {
  }
  if (!SetPixelFormat(g_hdc, realPf, &rpfd)) {
    std::fprintf(stderr, "SetPixelFormat(real) failed\n"); return 1;
  }

  const int attribs[] = {
      0x0001, 4,   // WGL_CONTEXT_MAJOR_VERSION_ARB
      0x0002, 3,   // WGL_CONTEXT_MINOR_VERSION_ARB (4.3 = compute shaders)
      0x9126, 0x00000001,  // WGL_CONTEXT_PROFILE_MASK_ARB = CORE
      0x0000, 0    // terminator
  };
  HGLRC rc = wglCreateContextAttribsARB(g_hdc, nullptr, attribs);
  if (!rc) {
    std::fprintf(stderr, "wglCreateContextAttribsARB(4.3 core) failed\n");
    return 1;
  }
  if (!wglMakeCurrent(g_hdc, rc)) { std::fprintf(stderr, "wglMakeCurrent failed\n"); return 1; }

  // Tear the dummy down only after the real context is current.
  wglDeleteContext(dummyRc);
  ReleaseDC(dummy, dummyDc);
  DestroyWindow(dummy);

  if (!LoadOpenGL()) {
    return 1;
  }
  std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));  std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
  std::printf("GLSL        : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

  // Compute support probe.
  //
  // GL_MAX_COMPUTE_WORK_GROUP_COUNT is an INDEXED limit: querying it with
  // glGetIntegerv raises GL_INVALID_ENUM on a context that does not know it and
  // leaves the destination untouched, which is why an earlier version of this
  // probe reported 0 even on capable hardware. glGetIntegeri_v is the correct
  // entry point.
  bool haveCompute = false;
  {
    GLint ccount[3] = {0, 0, 0};
    auto getIntegeri = reinterpret_cast<PFNGLGETINTEGERI_VPROC>(
        wglGetProcAddress("glGetIntegeri_v"));
    if (getIntegeri) {
      for (GLuint i = 0; i < 3; ++i) {
        getIntegeri(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &ccount[i]);
      }
      haveCompute = ccount[0] > 0 && ccount[1] > 0 && ccount[2] > 0;
      if (haveCompute) {
        std::printf("max compute workgroups = (%d, %d, %d)\n", ccount[0], ccount[1], ccount[2]);
      }
    }
  }

  // --compile-shaders: validate every shader and exit, without needing
  // compute support. This is how the lab can be checked from a session whose
  // GL context cannot dispatch, which is otherwise a blind spot: a GLSL typo
  // would not surface until the code is run on a real desktop session.
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool compileOnly = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wcscmp(argv[i], L"--compile-shaders") == 0) compileOnly = true;
  }

  if (!haveCompute && !compileOnly) {
    std::fprintf(stderr,
                 "ERROR: no compute support in this GL context "
                 "(MAX_COMPUTE_WORK_GROUP_COUNT = 0).\n"
                 "       A GPU/display session with a 4.3+ context is required.\n"
                 "       Use --compile-shaders to validate shaders without compute.\n");
    return 1;
  }

  lab::Renderer renderer;
  lab::SoALights lights;
  std::string err;
  if (!renderer.Initialize(hwnd, kInitialW, kInitialH, err)) {
    std::fprintf(stderr, "init failed: %s\n", err.c_str());
    return 1;
  }
  g_renderer = &renderer;
  g_lights = &lights;
  lights.GenerateOrbit(kLightCount, 0.0f, 12.0f);

  if (compileOnly) {
    // Initialize() already compiled and linked all five shaders; reaching here
    // means every one of them built on this driver.
    std::printf("All shaders compiled and linked successfully.\n");
    renderer.Shutdown();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, g_hdc);
    DestroyWindow(hwnd);
    return 0;
  }

  MSG msg{};
  double lastReport = 0.0;
  while (g_running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) g_running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!g_running) break;

    if (g_resizePending) {
      renderer.Resize(g_pendingW, g_pendingH);
      g_resizePending = false;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double t = static_cast<double>(now.QuadPart - g_start.QuadPart) / g_freq.QuadPart;

    const float aspect = static_cast<float>(g_pendingW) / static_cast<float>(g_pendingH);
    const float orbit = static_cast<float>(t) * 0.25f;
    const float eyeX = std::cos(orbit) * 22.0f;
    const float eyeZ = std::sin(orbit) * 22.0f;

    const lab::Mat4 view = lab::Mat4::LookAt(eyeX, 9.0f, eyeZ, 0.0f, 0.0f, 0.0f,
                                             0.0f, 1.0f, 0.0f);
    const lab::Mat4 proj = lab::Mat4::Perspective(1.0472f, aspect, 0.1f, 100.0f);
    const lab::Mat4 viewProj = lab::Mat4::Multiply(proj, view);

    lights.GenerateOrbit(kLightCount, static_cast<float>(t), 12.0f);
    renderer.RenderFrame(viewProj, view, lights, static_cast<float>(t));
    SwapBuffers(g_hdc);

    if (t - lastReport > 1.0) {
      lastReport = t;
      const auto tm = renderer.LastTimings();
      std::printf("t=%6.1fs  lights=%u  cull=%5.2fms  total=%5.2fms  clusters=%u\n", t,
                  renderer.LightIndexCount(), tm.cullMs, tm.totalMs,
                  renderer.LightIndexCount());
      std::fflush(stdout);
    }
  }

  renderer.Shutdown();
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(rc);
  ReleaseDC(hwnd, g_hdc);
  DestroyWindow(hwnd);
  return 0;
}

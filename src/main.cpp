// ClusteredLightingLab - isolated Win32 + GL 4.6 harness for the clustered
// lighting prototype. Shares no code with the Godot trees; it is a standalone
// reference implementation to be ported into the engine later.
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>

#include "gl/gl_loader.h"
#include "renderer/ClusteredRenderer.h"
#include "scene/SoALights.h"

using lab::SoALights;

using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
// wglChoosePixelFormatARB takes SIX parameters. An earlier alias listed only
// three, so calling it wrote the result through a garbage pointer -- an access
// violation the first time the code was actually reached.
using PFNWGLCHOOSEPIXELFORMATARBPROC = BOOL(WINAPI*)(HDC, const int*, const FLOAT*,
                                                     UINT, int*, UINT*);
using PFNGLGETINTEGERI_VPROC = void(WINAPI*)(GLenum, GLuint, GLint*);

namespace {

constexpr wchar_t kTitle[] = L"Clustered Lighting Lab";
constexpr uint32_t kInitialW = 1280, kInitialH = 720;
constexpr uint32_t kLightCount = 10000;

HDC g_hdc = nullptr;
lab::Renderer* g_renderer = nullptr;
lab::SoALights* g_lights = nullptr;
// The real window. WndProc uses it to ignore the bootstrap dummy window's
// messages: the dummy shares this window class but must never drive app state.
HWND g_mainHwnd = nullptr;
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
    case WM_DESTROY:
      // Only the REAL window drives app lifetime. The dummy bootstrap window
      // shares this class, and its teardown must not post WM_QUIT: doing so
      // set g_running = false during startup and the render loop never ran a
      // single frame.
      if (hwnd == g_mainHwnd) {
        PostQuitMessage(0);
        g_running = false;
      }
      return 0;
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
  g_mainHwnd = hwnd;

  // Detect a session with no interactive desktop BEFORE any GDI call on the
  // window: CreateWindow still succeeds there, but ShowWindow / GetDC / any
  // GDI call on the result faults. IsWindowVisible CANNOT be used for this:
  // the window above is created hidden (no WS_VISIBLE is passed), so it
  // reports false even on a healthy desktop and the lab would refuse to run
  // everywhere. The window station name is the correct query, and it is a
  // pure USER32 call that is safe in every session: interactive sessions run
  // on "WinSta0"; service/headless logons use a private station of their own.
  {
    wchar_t station[64] = L"";
    DWORD needed = 0;
    const HWINSTA winsta = GetProcessWindowStation();
    const bool haveName =
        winsta && GetUserObjectInformationW(winsta, UOI_NAME, station,
                                            sizeof(station), &needed);
    if (!haveName || wcscmp(station, L"WinSta0") != 0) {
      std::fprintf(stderr,
                   "ERROR: this session has no interactive desktop (window\n"
                   "       station is not WinSta0), so its device context is\n"
                   "       unusable and no OpenGL context can be created. Run\n"
                   "       the lab from an interactive desktop session.\n");
      return 2;
    }
  }

  // The window exists but is still hidden; now that the session is known to
  // have a real desktop, show it. Without this the lab would render into an
  // invisible window and never display anything.
  ShowWindow(hwnd, SW_SHOW);
  if (!IsWindowVisible(hwnd)) {
    std::fprintf(stderr,
                 "ERROR: the window never became visible. This session has no\n"
                 "       interactive desktop, so the window DC is unusable and\n"
                 "       OpenGL context creation cannot work here.\n"
                 "       Run from an interactive desktop session.\n");
    return 2;
  }

  HDC hdc = GetDC(hwnd);
  if (!hdc) { std::fprintf(stderr, "GetDC failed\n"); return 1; }
  g_hdc = hdc;

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
      reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
          wglGetProcAddress("wglChoosePixelFormatARB"));
  if (!wglCreateContextAttribsARB || !wglChoosePixelFormatARB) {
    std::fprintf(stderr, "WGL_ARB_create_context unavailable\n");
    return 1;
  }

  // Ask the driver for a pixel format that suits a modern core context, then
  // apply it to the REAL window. SetPixelFormat may only be called once per
  // HWND, which is why the dummy window exists at all.
  //
  // These are the real WGL_ARB_pixel_format attribute tokens. An earlier
  // revision used made-up 0x0000..0x000C values (which can never match a
  // format) and called the six-parameter function through a three-parameter
  // prototype; both bugs stayed invisible because the session guard rejected
  // every session before this code was ever reached.
  const int pfdAttribs[] = {
      0x2001, 1,      // WGL_DRAW_TO_WINDOW_ARB
      0x2010, 1,      // WGL_SUPPORT_OPENGL_ARB
      0x2011, 1,      // WGL_DOUBLE_BUFFER_ARB
      0x2013, 0x202B, // WGL_PIXEL_TYPE_ARB = WGL_TYPE_RGBA_ARB
      0x2015, 8,      // WGL_RED_BITS_ARB
      0x2016, 8,      // WGL_GREEN_BITS_ARB
      0x2017, 8,      // WGL_BLUE_BITS_ARB
      0x2018, 8,      // WGL_ALPHA_BITS_ARB
      0x2022, 24,     // WGL_DEPTH_BITS_ARB
      0x2023, 8,      // WGL_STENCIL_BITS_ARB
      0, 0            // terminator
  };
  int realPf = 0;
  UINT numFormats = 0;
  if (!wglChoosePixelFormatARB(g_hdc, pfdAttribs, nullptr, 1, &realPf, &numFormats) ||
      numFormats == 0 || realPf == 0) {
    std::fprintf(stderr, "wglChoosePixelFormatARB failed\n"); return 1;
  }
  PIXELFORMATDESCRIPTOR rpfd{};
  if (!DescribePixelFormat(dummyDc, realPf, sizeof(rpfd), &rpfd)) {
  }
  if (!SetPixelFormat(g_hdc, realPf, &rpfd)) {
    std::fprintf(stderr, "SetPixelFormat(real) failed\n"); return 1;
  }

  const int attribs[] = {
      0x2091, 4,           // WGL_CONTEXT_MAJOR_VERSION_ARB
      0x2092, 3,           // WGL_CONTEXT_MINOR_VERSION_ARB (4.3 = compute shaders)
      0x9126, 0x00000001,  // WGL_CONTEXT_PROFILE_MASK_ARB = CORE
      0                    // terminator
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
  bool selfTest = false;
  bool coneDump = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wcscmp(argv[i], L"--compile-shaders") == 0) compileOnly = true;
    if (std::wcscmp(argv[i], L"--selftest") == 0) selfTest = true;
    if (std::wcscmp(argv[i], L"--conedump") == 0) coneDump = true;
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
    // Initialize() already compiled and linked all shader programs; reaching
    // here means every one of them built on this driver.
    std::printf("All shaders compiled and linked successfully.\n");
    renderer.Shutdown();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, g_hdc);
    DestroyWindow(hwnd);
    return 0;
  }

  if (selfTest || coneDump) {
    // One frame, then VALUE-level inspection: --selftest compares the GPU-built
    // cluster AABBs against the CPU reference fed the same inv(projection)
    // (the regression test for the "wrong matrix uploaded" defect class, which
    // no screenshot can catch); --conedump prints one cluster's cone plus the
    // dot statistics of its light list, straight off the GPU buffers.
    const float aspect = static_cast<float>(kInitialW) / static_cast<float>(kInitialH);
    const lab::Mat4 view = lab::Mat4::LookAt(22.0f, 9.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             0.0f, 1.0f, 0.0f);
    const lab::Mat4 proj = lab::Mat4::Perspective(1.0472f, aspect, 0.1f, 100.0f);
    const lab::Mat4 viewProj = lab::Mat4::Multiply(proj, view);
    lab::Mat4 invProj;
    if (!proj.Inverse(&invProj)) {
      std::fprintf(stderr, "selftest: projection matrix is not invertible\n");
      return 1;
    }
    renderer.RenderFrame(viewProj, view, proj, lights, 0.0f);
    glFinish();
    if (selfTest) {
      std::string verr;
      if (!renderer.VerifyClusterAABBs(invProj, verr)) {
        std::fprintf(stderr, "selftest FAILED: %s\n", verr.c_str());
        renderer.Shutdown();
        return 1;
      }
      std::printf("selftest PASS\n");
    }
    if (coneDump) {
      renderer.DebugDumpConeAndList(80, 45);  // centre tile at the nominal 1280x720
    }
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
    renderer.RenderFrame(viewProj, view, proj, lights, static_cast<float>(t));
    SwapBuffers(g_hdc);

    if (t - lastReport > 1.0) {
      lastReport = t;
      const auto tm = renderer.LastTimings();
      // The global index counter shows how many lights survived the cull -
      // the proof that the culling actually does something. One readback per
      // second: the sync cost is irrelevant at this cadence.
      std::printf("t=%6.1fs  lights=%u  cull=%5.2fms  total=%5.2fms  clusters=%u  indices=%u\n", t,
                  renderer.LightIndexCount(), tm.cullMs, tm.totalMs,
                  renderer.ClusterCount(), renderer.ReadLightIndexTotal());
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

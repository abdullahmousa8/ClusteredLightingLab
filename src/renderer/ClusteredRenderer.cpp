#include "ClusteredRenderer.h"
#include "Shaders.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace lab {
namespace {

constexpr GLenum GL_COLOR_ATTACHMENT1 = 0x8CE1;

void CheckGl(const char* where) {
  GLenum e = glGetError();
  if (e != 0) std::fprintf(stderr, "[GL] %s -> 0x%04X\n", where, e);
}

std::string ShaderLog(GLuint sh) {
  GLint len = 0;
  glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
  std::string log(static_cast<size_t>(len > 1 ? len : 1), '\0');
  glGetShaderInfoLog(sh, len, nullptr, log.data());
  return log;
}

std::string ProgramLog(GLuint p) {
  GLint len = 0;
  glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
  std::string log(static_cast<size_t>(len > 1 ? len : 1), '\0');
  glGetProgramInfoLog(p, len, nullptr, log.data());
  return log;
}

void MakeBuffer(GLuint& buf, GLsizeiptr bytes, const void* data, GLenum usage) {
  glGenBuffers(1, &buf);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, usage);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

}  // namespace

GLuint Renderer::Compile(GLenum type, const char* src, std::string& err) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    err = std::string("shader compile failed: ") + ShaderLog(sh);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

GLuint Renderer::Link(GLuint vs, GLuint fs, const char* vsName, std::string& err) {
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  if (fs) glAttachShader(p, fs);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    err = std::string("link failed (") + vsName + "): " + ProgramLog(p);
    glDeleteProgram(p);
    return 0;
  }
  glDetachShader(p, vs);
  if (fs) glDetachShader(p, fs);
  return p;
}

bool Renderer::CreateShaders(std::string& err) {
  GLuint bc = Compile(GL_COMPUTE_SHADER, kBuildClustersComp, err);
  if (!bc) return false;
  buildProg_ = Link(bc, 0, "build_clusters", err);
  glDeleteShader(bc);

  GLuint cc = Compile(GL_COMPUTE_SHADER, kCullLightsComp, err);
  if (!cc) return false;
  cullProg_ = Link(cc, 0, "cull_lights", err);
  glDeleteShader(cc);
  if (!buildProg_ || !cullProg_) return false;

  GLuint conc = Compile(GL_COMPUTE_SHADER, kBuildConesComp, err);
  if (!conc) return false;
  coneProg_ = Link(conc, 0, "build_cones", err);
  glDeleteShader(conc);
  if (!coneProg_) return false;

  GLuint gv = Compile(GL_VERTEX_SHADER, kGBufferVert, err);
  GLuint gf = Compile(GL_FRAGMENT_SHADER, kGBufferFrag, err);
  if (!gv || !gf) return false;
  gbufProg_ = Link(gv, gf, "gbuffer", err);
  glDeleteShader(gv);
  glDeleteShader(gf);
  if (!gbufProg_) return false;

  GLuint sv = Compile(GL_VERTEX_SHADER,
                     "#version 460 core\nvoid main(){\n"
                     "  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2);\n"
                     "  gl_Position = vec4(p*2.0-1.0, 0.0, 1.0);\n}\n", err);
  if (!sv) return false;
  GLuint sf = Compile(GL_FRAGMENT_SHADER, kShadeFrag, err);
  if (!sf) return false;
  shadeProg_ = Link(sv, sf, "resolve", err);
  glDeleteShader(sv);
  glDeleteShader(sf);
  return shadeProg_ != 0;
}


void Renderer::UpdateGrid(uint32_t width, uint32_t height) {
  // Tile size follows the paper: 16x9 tiles of 8x8 px at 1440p.
  const uint32_t tilePx = 8;
  grid_.gridSizeX = (width + tilePx - 1) / tilePx;
  grid_.gridSizeY = (height + tilePx - 1) / tilePx;
  grid_.gridSizeZ = 24;
  grid_.zNear = zNear_;
  grid_.zFar = zFar_;
  gridSizeUvec3_[0] = grid_.gridSizeX;
  gridSizeUvec3_[1] = grid_.gridSizeY;
  gridSizeUvec3_[2] = grid_.gridSizeZ;

  const size_t clusters = grid_.numClusters();
  MakeBuffer(clusterAabb_, static_cast<GLsizeiptr>(clusters * sizeof(Vec4) * 2), nullptr, GL_DYNAMIC_DRAW);
  MakeBuffer(clusterGrid_, static_cast<GLsizeiptr>(clusters * sizeof(Vec4)), nullptr, GL_DYNAMIC_DRAW);
  // Normal cones start as "incoherent" (w = 0), which never culls, matching the
  // shader's `coneRaw.w > 0.1` validity test. The reduction pass overwrites the
  // clusters that actually contain geometry.
  {
    std::vector<float> cones(static_cast<size_t>(clusters) * 4, 0.0f);
    MakeBuffer(clusterCone_, static_cast<GLsizeiptr>(cones.size() * sizeof(float)),
               cones.data(), GL_DYNAMIC_DRAW);
  }
}

bool Renderer::CreateTargets(std::string& err) {
  glGenTextures(1, &gAlbedo_);
  glBindTexture(GL_TEXTURE_2D, gAlbedo_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &gNormal_);
  glBindTexture(GL_TEXTURE_2D, gNormal_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Depth must be a TEXTURE, not a renderbuffer: the resolve pass reads it
  // through u_GBufferDepth. Binding a renderbuffer name with glBindTexture
  // does NOT put the depth data behind a sampler -- it silently creates an
  // empty texture under that name instead, so every pixel shades with a wrong
  // reconstructed position and nothing reports an error.
  glGenTextures(1, &gDepth_);
  glBindTexture(GL_TEXTURE_2D, gDepth_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width_, height_, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gAlbedo_, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal_, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepth_, 0);

  const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, bufs);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    err = "gbuffer framebuffer incomplete";
    return false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

// Procedural ground grid so the lab needs no asset pipeline.
void BuildGroundMesh(std::vector<float>& verts, std::vector<uint32_t>& indices,
                     int divisions, float extent) {
  verts.clear();
  indices.clear();
  for (int z = 0; z <= divisions; ++z) {
    for (int x = 0; x <= divisions; ++x) {
      const float fx = static_cast<float>(x) / divisions * 2.0f - 1.0f;
      const float fz = static_cast<float>(z) / divisions * 2.0f - 1.0f;
      verts.insert(verts.end(), {fx * extent, 0.0f, fz * extent,
                                 0.0f, 1.0f, 0.0f});
    }
  }
  const int row = divisions + 1;
  for (int z = 0; z < divisions; ++z) {
    for (int x = 0; x < divisions; ++x) {
      const uint32_t a = static_cast<uint32_t>(z * row + x);
      const uint32_t b = a + 1, c = a + static_cast<uint32_t>(row), d = c + 1;
      indices.insert(indices.end(), {a, c, b, b, c, d});
    }
  }
}

bool Renderer::CreateBuffers(std::string& err) {
  std::vector<float> verts;
  std::vector<uint32_t> indices;
  BuildGroundMesh(verts, indices, 64, 25.0f);
  meshIndexCount_ = static_cast<uint32_t>(indices.size());

  glGenVertexArrays(1, &meshVao_);
  glBindVertexArray(meshVao_);
  glGenBuffers(1, &meshVbo_);
  glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
               verts.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &meshIbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshIbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
               indices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        reinterpret_cast<void*>(3 * sizeof(float)));

  glGenVertexArrays(1, &emptyVao_);

  // Light SoA buffers: sized for the max light count, updated per frame.
  MakeBuffer(lightView_, 1 << 20, nullptr, GL_DYNAMIC_DRAW);
  MakeBuffer(lightColor_, 1 << 20, nullptr, GL_DYNAMIC_DRAW);
  MakeBuffer(lightSpot_, 1 << 20, nullptr, GL_DYNAMIC_DRAW);
  MakeBuffer(lightSpotInner_, 1 << 20, nullptr, GL_DYNAMIC_DRAW);
  MakeBuffer(indexCounter_, sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);
  // The shared-memory cap is 1024 lights per cluster (see kMaxLocalLights), so
  // the global index list must hold at least that many per cluster or the copy
  // loop writes out of bounds. Clamp to the driver's max SSBO block size.
  GLint maxBlockSize = 0;
  glGetIntegerv(0x90DE /* GL_MAX_SHADER_STORAGE_BLOCK_SIZE */, &maxBlockSize);
  const size_t wantedBytes = static_cast<size_t>(grid_.numClusters()) * 1024 * sizeof(uint32_t);
  const size_t maxBytes = maxBlockSize > 0 ? static_cast<size_t>(maxBlockSize) : wantedBytes;
  if (wantedBytes > maxBytes) {    std::fprintf(stderr,
                 "[warn] global index list clamped: %.1f MB wanted, %.1f MB available. "
                 "Per-cluster cap is %u lights.\n",
                 wantedBytes / 1048576.0, maxBytes / 1048576.0,
                 static_cast<unsigned>(maxBytes / (sizeof(uint32_t) * grid_.numClusters())));
  }
  MakeBuffer(globalIndex_, static_cast<GLsizeiptr>(wantedBytes < maxBytes ? wantedBytes : maxBytes),
             nullptr, GL_DYNAMIC_DRAW);
  maxLightsPerCluster_ = static_cast<uint32_t>(
      (wantedBytes < maxBytes ? wantedBytes : maxBytes) / (sizeof(uint32_t) * grid_.numClusters()));
  if (maxLightsPerCluster_ > 1024u) maxLightsPerCluster_ = 1024u;

  glGenQueries(1, &query_);
  (void)err;
  return true;
}

bool Renderer::Initialize(void* hwnd, uint32_t width, uint32_t height, std::string& err) {
  (void)hwnd;
  width_ = width;
  height_ = height;

  if (!CreateShaders(err)) return false;
  UpdateGrid(width, height);
  if (!CreateTargets(err)) return false;
  if (!CreateBuffers(err)) return false;

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  return true;
}

void Renderer::Resize(uint32_t width, uint32_t height) {
  if (width == width_ && height == height_) return;
  width_ = width;
  height_ = height;
  glDeleteTextures(1, &gAlbedo_);
  glDeleteTextures(1, &gNormal_);
  glDeleteTextures(1, &gDepth_);
  glDeleteFramebuffers(1, &fbo_);
  // All four buffers are sized by the grid dimensions. Delete them BEFORE
  // UpdateGrid runs, which recreates the AABB/grid/cone trio at the new size;
  // deleting afterwards would leak the old buffers, and skipping the cone's
  // recreation (as an earlier revision did) leaves a deleted name bound to
  // binding 5 on every later frame. The global index list is rebuilt below
  // with the new capacity clamp.
  glDeleteBuffers(1, &clusterAabb_);
  glDeleteBuffers(1, &clusterGrid_);
  glDeleteBuffers(1, &clusterCone_);
  glDeleteBuffers(1, &globalIndex_);
  std::string err;
  UpdateGrid(width, height);
  CreateTargets(err);
  // Same size clamp as CreateBuffers; a larger window means more clusters.
  GLint maxBlockSize = 0;
  glGetIntegerv(0x90DE /* GL_MAX_SHADER_STORAGE_BLOCK_SIZE */, &maxBlockSize);
  const size_t wantedBytes = static_cast<size_t>(grid_.numClusters()) * 1024 * sizeof(uint32_t);
  const size_t maxBytes = maxBlockSize > 0 ? static_cast<size_t>(maxBlockSize) : wantedBytes;
  const size_t actual = wantedBytes < maxBytes ? wantedBytes : maxBytes;
  MakeBuffer(globalIndex_, static_cast<GLsizeiptr>(actual), nullptr, GL_DYNAMIC_DRAW);
  maxLightsPerCluster_ = static_cast<uint32_t>(actual / (sizeof(uint32_t) * grid_.numClusters()));
  if (maxLightsPerCluster_ > 1024u) maxLightsPerCluster_ = 1024u;
}

void Renderer::Shutdown() {
  if (query_) glDeleteQueries(1, &query_);
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  if (gAlbedo_) glDeleteTextures(1, &gAlbedo_);
  if (gNormal_) glDeleteTextures(1, &gNormal_);
  if (gDepth_) glDeleteTextures(1, &gDepth_);
  const GLuint bufs[] = {clusterAabb_, clusterGrid_, clusterCone_, globalIndex_,
                         indexCounter_, lightView_, lightColor_, lightSpot_,
                         lightSpotInner_, meshVbo_, meshIbo_};
  for (GLuint b : bufs) if (b) glDeleteBuffers(1, &b);
  if (meshVao_) glDeleteVertexArrays(1, &meshVao_);
  if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
  const GLuint progs[] = {buildProg_, cullProg_, coneProg_, gbufProg_, shadeProg_};
  for (GLuint p : progs) if (p) glDeleteProgram(p);
  query_ = 0; fbo_ = 0; gAlbedo_ = 0; gNormal_ = 0; gDepth_ = 0;
  clusterAabb_ = clusterGrid_ = globalIndex_ = indexCounter_ = 0;
  lightView_ = lightColor_ = lightSpot_ = lightSpotInner_ = 0;
  lightView_ = lightColor_ = meshVbo_ = meshIbo_ = meshVao_ = emptyVao_ = 0;
  buildProg_ = cullProg_ = coneProg_ = gbufProg_ = shadeProg_ = 0;
}

uint32_t Renderer::ReadLightIndexTotal() {
  GLuint total = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, indexCounter_);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &total);
  return total;
}

// Diagnostic helper (used by --conedump): read one cluster's cone plus its
// light list and print the light directions, mirroring the shader's back-face
// test, so the actual GPU data can be inspected instead of theorised about.
void Renderer::DebugDumpConeAndList(uint32_t tx, uint32_t ty) {
  const size_t stride = static_cast<size_t>(grid_.gridSizeX) * grid_.gridSizeY;
  for (uint32_t z = 0; z < grid_.gridSizeZ; ++z) {
    const size_t idx = static_cast<size_t>(tx) + static_cast<size_t>(ty) * grid_.gridSizeX + z * stride;
    Vec4 cone{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterCone_);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(idx * sizeof(Vec4)),
                       sizeof(Vec4), &cone);
    GLuint grid2[2] = {0, 0};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterGrid_);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(idx * sizeof(GLuint) * 2),
                       static_cast<GLsizeiptr>(sizeof(GLuint) * 2), grid2);
    const GLuint count = grid2[1];
    if (count == 0u && cone.w == 0.0f) continue;  // print only populated/valid slices
    Vec4 lo{};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterAabb_);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(idx * sizeof(Vec4) * 2),
                       sizeof(Vec4), &lo);
    std::printf("  cluster(%u,%u,%u): cone (%.3f,%.3f,%.3f) w=%.4f | offset=%u count=%u | lo=(%.2f,%.2f,%.2f)\n",
                tx, ty, z, cone.x, cone.y, cone.z, cone.w, grid2[0], count, lo.x, lo.y, lo.z);
    if (count > 0u) {
      const GLuint total = lightIndexCount_ < 1u ? 1u : lightIndexCount_;
      std::vector<Vec4> lightsAll(total, Vec4{});
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightView_);
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                         static_cast<GLsizeiptr>(total * sizeof(Vec4)), lightsAll.data());
      std::vector<GLuint> ids(count);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, globalIndex_);
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(grid2[0] * sizeof(GLuint)),
                         static_cast<GLsizeiptr>(count * sizeof(GLuint)), ids.data());
      size_t neg = 0, pos = 0;
      float minD = 1e9f, maxD = -1e9f;
      for (GLuint k = 0; k < count; ++k) {
        const Vec4& lv = lightsAll[ids[k] % total];
        const float dx = lv.x - lo.x, dy = lv.y - lo.y, dz = lv.z - lo.z;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float d = len > 1e-6f ? (dx * cone.x + dy * cone.y + dz * cone.z) / len : 0.0f;
        if (d < 0.0f) ++neg; else ++pos;
        if (d < minD) minD = d;
        if (d > maxD) maxD = d;
        if (k < 5u)
          std::printf("      light %u: (%.2f,%.2f,%.2f) r=%.2f  dot=%.4f\n",
                      ids[k], lv.x, lv.y, lv.z, lv.w, d);
      }
      std::printf("      listed=%u  dot<0: %zu  dot>=0: %zu  (min=%.3f max=%.3f)\n",
                  count, neg, pos, minD, maxD);
    }
  }
}

bool Renderer::VerifyClusterAABBs(const Mat4& invProj, std::string& err) {
  const size_t count = static_cast<size_t>(grid_.numClusters());
  std::vector<Vec4> gpu(count * 2, Vec4{});
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterAabb_);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                     static_cast<GLsizeiptr>(count * 2 * sizeof(Vec4)), gpu.data());

  std::vector<ClusterAABB> cpu(count);
  lab::BuildClusterAABBs(invProj, static_cast<float>(width_), static_cast<float>(height_),
      grid_, [&](uint32_t x, uint32_t y, uint32_t z, const ClusterAABB& c) {
        const size_t idx = static_cast<size_t>(x) +
                           static_cast<size_t>(y) * grid_.gridSizeX +
                           static_cast<size_t>(z) * grid_.gridSizeX * grid_.gridSizeY;
        cpu[idx] = c;
      });

  size_t mismatches = 0;
  float worst = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    const Vec4& lo = gpu[i * 2 + 0];
    const Vec4& hi = gpu[i * 2 + 1];
    const ClusterAABB& c = cpu[i];
    const float pairs[6][2] = {{lo.x, c.minPoint.x}, {lo.y, c.minPoint.y}, {lo.z, c.minPoint.z},
                               {hi.x, c.maxPoint.x}, {hi.y, c.maxPoint.y}, {hi.z, c.maxPoint.z}};
    bool bad = false;
    for (const auto& v : pairs) {
      const float d = std::fabs(v[0] - v[1]);
      const float tol = 1e-3f + 1e-4f * std::fabs(v[1]);
      if (d > worst) worst = d;
      if (!(d <= tol)) bad = true;
    }
    if (bad) ++mismatches;
  }
  if (mismatches != 0) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "GPU cluster AABBs differ from the CPU reference in %zu of %zu clusters (worst delta %.6f)",
                  mismatches, count, static_cast<double>(worst));
    err = buf;
    return false;
  }
  std::printf("AABB verification: all %zu clusters match the CPU reference (worst delta %.6f)\n",
              count, static_cast<double>(worst));
  return true;
}

void Renderer::RenderFrame(const Mat4& viewProj, const Mat4& view, const Mat4& proj,
                           SoALights& lights, float timeSeconds) {
  // The cluster math is ALL view space (camera at the origin, looking down
  // -Z), so the reconstructing passes need the inverse of the PROJECTION
  // alone -- NOT the inverse of view*projection. The tested CPU mirror
  // (BuildClusterAABBs / ClusterIndexForPixel) is fed inv(Perspective); the
  // renderer must upload the same matrix, or every cluster AABB and every
  // reconstructed pixel position is silently wrong.
  Mat4 invProj;
  const bool invOk = proj.Inverse(&invProj);
  const GLuint lightCount = lights.Count();

  // ---- SoA upload: view-space positions happen ONCE per light, on the CPU,
  // and are consumed directly by both cull and shade. No per-cluster transform.
  if (invOk) lights.UpdateViewSpace(view.Data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightView_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  static_cast<GLsizeiptr>(lightCount * sizeof(Vec4)),
                  lights.ViewPositionRange().data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightColor_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  static_cast<GLsizeiptr>(lightCount * sizeof(Vec4)),
                  lights.ColorIntensity().data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightSpot_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  static_cast<GLsizeiptr>(lightCount * sizeof(Vec4)),
                  lights.ViewSpotDirOuter().data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightSpotInner_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                  static_cast<GLsizeiptr>(lightCount * sizeof(Vec4)),
                  lights.SpotInnerCos().data());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, indexCounter_);
  const GLuint zero = 0;
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &zero);

  // ---- Pass 1: G-buffer
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(gbufProg_);
  glUniformMatrix4fv(glGetUniformLocation(gbufProg_, "u_ViewProj"), 1, GL_FALSE, viewProj.Data());
  glUniformMatrix4fv(glGetUniformLocation(gbufProg_, "u_View"), 1, GL_FALSE, view.Data());
  glBindVertexArray(meshVao_);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(meshIndexCount_), GL_UNSIGNED_INT, nullptr);

  // ---- Pass 2: build cluster AABBs
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, clusterAabb_);
  glUseProgram(buildProg_);
  glUniformMatrix4fv(glGetUniformLocation(buildProg_, "u_InvProjection"), 1, GL_FALSE,
                     invProj.Data());
  glUniform2f(glGetUniformLocation(buildProg_, "u_ScreenSize"),
              static_cast<float>(width_), static_cast<float>(height_));
  glUniform1f(glGetUniformLocation(buildProg_, "u_ZNear"), zNear_);
  glUniform1f(glGetUniformLocation(buildProg_, "u_ZFar"), zFar_);
  glUniform3uiv(glGetUniformLocation(buildProg_, "u_GridSize"), 1, gridSizeUvec3_);
  glDispatchCompute((grid_.gridSizeX + 7) / 8, (grid_.gridSizeY + 7) / 8, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // ---- Pass 2.5: reduce the G-buffer into one normal cone per cluster,
  // feeding the back-face test of the cull pass. This pass samples the depth
  // and normal textures, so the gbuffer FBO must be unbound first (sampling a
  // texture while it is attached to the bound framebuffer is undefined).
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);  // gbuffer draws -> texture reads
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gNormal_);
  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, gDepth_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, clusterCone_);
  glUseProgram(coneProg_);
  glUniformMatrix4fv(glGetUniformLocation(coneProg_, "u_InvProjection"), 1, GL_FALSE,
                     invProj.Data());
  glUniform2f(glGetUniformLocation(coneProg_, "u_ScreenSize"),
              static_cast<float>(width_), static_cast<float>(height_));
  glUniform1f(glGetUniformLocation(coneProg_, "u_ZNear"), zNear_);
  glUniform1f(glGetUniformLocation(coneProg_, "u_ZFar"), zFar_);
  glUniform3uiv(glGetUniformLocation(coneProg_, "u_GridSize"), 1, gridSizeUvec3_);
  glUniform1i(glGetUniformLocation(coneProg_, "u_GBufferNormal"), 0);
  glUniform1i(glGetUniformLocation(coneProg_, "u_GBufferDepth"), 1);
  glDispatchCompute(grid_.gridSizeX, grid_.gridSizeY, grid_.gridSizeZ);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // ---- Pass 3: cull lights (one workgroup per cluster)
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, clusterAabb_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, clusterGrid_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, globalIndex_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, indexCounter_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, lightView_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, clusterCone_);
  glUseProgram(cullProg_);
  glUniform1ui(glGetUniformLocation(cullProg_, "u_ActiveLightCount"), lightCount);
  glUniform1ui(glGetUniformLocation(cullProg_, "u_ClusterCount"), grid_.numClusters());
  glUniform1ui(glGetUniformLocation(cullProg_, "u_MaxLightsPerCluster"), maxLightsPerCluster_);

  // Collect results from a previous frame so we never stall the pipeline.
  // If that query is still in flight, skip timing THIS frame: ending a
  // non-active query (what an earlier revision did on the else path) is
  // GL_INVALID_OPERATION on every occurrence, and re-beginning a query whose
  // result was not yet retrieved is not portable either.
  bool timeThisFrame = true;
  if (queryPending_) {
    GLint avail = 0;
    glGetQueryObjectiv(query_, GL_QUERY_RESULT_AVAILABLE, &avail);
    if (avail) {
      GLuint64 elapsed = 0;
      glGetQueryObjectui64v(query_, GL_QUERY_RESULT, &elapsed);
      timings_.cullMs = static_cast<float>(elapsed) * 1e-6f;
      queryPending_ = false;
    } else {
      timeThisFrame = false;
    }
  }
  if (timeThisFrame) {
    glBeginQuery(GL_TIME_ELAPSED, query_);
    queryPending_ = true;
  }

  glDispatchCompute(grid_.gridSizeX, grid_.gridSizeY, grid_.gridSizeZ);
  if (timeThisFrame) glEndQuery(GL_TIME_ELAPSED);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // ---- Pass 4: deferred resolve
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
  glDisable(GL_DEPTH_TEST);
  glUseProgram(shadeProg_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, clusterGrid_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, globalIndex_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightView_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, lightColor_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, lightSpot_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, lightSpotInner_);

  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gAlbedo_);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, gNormal_);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, gDepth_);
  glUniform1i(glGetUniformLocation(shadeProg_, "u_GBufferAlbedoRoughness"), 0);
  glUniform1i(glGetUniformLocation(shadeProg_, "u_GBufferNormal"), 1);
  glUniform1i(glGetUniformLocation(shadeProg_, "u_GBufferDepth"), 2);
  glUniformMatrix4fv(glGetUniformLocation(shadeProg_, "u_InvProjection"), 1, GL_FALSE,
                     invProj.Data());
  glUniform2f(glGetUniformLocation(shadeProg_, "u_ScreenSize"),
              static_cast<float>(width_), static_cast<float>(height_));
  glUniform1f(glGetUniformLocation(shadeProg_, "u_ZNear"), zNear_);
  glUniform1f(glGetUniformLocation(shadeProg_, "u_ZFar"), zFar_);
  glUniform3uiv(glGetUniformLocation(shadeProg_, "u_GridSize"), 1, gridSizeUvec3_);

  glBindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  (void)timeSeconds;
  lightIndexCount_ = lightCount;
  CheckGl("RenderFrame");
}

}  // namespace lab

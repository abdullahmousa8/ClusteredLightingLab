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
  const GLuint progs[] = {buildProg_, cullProg_, gbufProg_, shadeProg_};
  for (GLuint p : progs) if (p) glDeleteProgram(p);
  query_ = 0; fbo_ = 0; gAlbedo_ = 0; gNormal_ = 0; gDepth_ = 0;
  clusterAabb_ = clusterGrid_ = globalIndex_ = indexCounter_ = 0;
  lightView_ = lightColor_ = lightSpot_ = lightSpotInner_ = 0;
  lightView_ = lightColor_ = meshVbo_ = meshIbo_ = meshVao_ = emptyVao_ = 0;
  buildProg_ = cullProg_ = gbufProg_ = shadeProg_ = 0;
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
  glUniformMatrix4fv(glGetUniformLocation(shadeProg_, "u_InvViewProj"), 1, GL_FALSE,
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

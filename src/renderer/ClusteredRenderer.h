// Clustered deferred lighting renderer (GL 4.6, isolated lab build).
#pragma once
#include <cstdint>
#include <string>

#include "../gl/gl_loader.h"
#include "../scene/SoALights.h"
#include "ClusterMath.h"

namespace lab {

// Mat4 lives in ClusterMath.h so the renderer and the tested CPU reference
// share ONE definition. Duplicating it here would let the matrix maths used on
// the GPU path drift from the maths the unit tests cover.

class Renderer {
 public:
  bool Initialize(void* hwnd, uint32_t width, uint32_t height, std::string& err);
  void Shutdown();
  void Resize(uint32_t width, uint32_t height);

  // Records the full frame: gbuffer -> clusters -> cull -> resolve.
  void RenderFrame(const Mat4& viewProj, const Mat4& view, const Mat4& proj,
                   SoALights& lights, float timeSeconds);

  // Reads back the last frame's GPU timings (ms) via a query ring.
  struct Timings { float gbufferMs, buildMs, cullMs, resolveMs, totalMs; };
  Timings LastTimings() const { return timings_; }

  uint32_t LightIndexCount() const { return lightIndexCount_; }
  // Number of 3D clusters in the current viewport's grid (status output).
  uint32_t ClusterCount() const { return grid_.numClusters(); }

 private:
  bool CreateShaders(std::string& err);
  bool CreateTargets(std::string& err);
  bool CreateBuffers(std::string& err);
  GLuint Compile(GLenum type, const char* src, std::string& err);
  GLuint Link(GLuint vs, GLuint fs, const char* vsName, std::string& err);
  void UpdateGrid(uint32_t width, uint32_t height);

  uint32_t width_ = 0, height_ = 0;
  float zNear_ = 0.1f, zFar_ = 100.0f;

  GLuint buildProg_ = 0, cullProg_ = 0, gbufProg_ = 0, shadeProg_ = 0;
  GLuint emptyVao_ = 0, meshVao_ = 0, meshVbo_ = 0, meshIbo_ = 0;
  uint32_t meshIndexCount_ = 0;

  GLuint fbo_ = 0, gAlbedo_ = 0, gNormal_ = 0, gDepth_ = 0;
  GLuint clusterAabb_ = 0, clusterGrid_ = 0, globalIndex_ = 0, indexCounter_ = 0;
  GLuint lightView_ = 0, lightColor_ = 0, lightSpot_ = 0, lightSpotInner_ = 0;
  GLuint clusterCone_ = 0;

  GLuint query_ = 0;
  bool queryPending_ = false;

  ClusterGridConfig grid_{};
  uint32_t gridSizeUvec3_[3] = {0, 0, 0};  // contiguous storage for u_GridSize
  uint32_t lightIndexCount_ = 0;
  uint32_t maxLightsPerCluster_ = 1024;
  Timings timings_{};
};

}  // namespace lab

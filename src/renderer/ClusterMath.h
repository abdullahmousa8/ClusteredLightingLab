// Host-side mirror of the cluster culling math in Shaders.h.
//
// Kept in one place so the GLSL and the CPU reference can be diffed by eye and
// so the correctness tests in tests/ClusterMathTests.cpp exercise exactly the
// same formulas the GPU runs. Any change here MUST be mirrored in Shaders.h.
#pragma once
#include <cstdint>
#include <functional>

namespace lab {

struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

struct ClusterGridConfig {
  uint32_t gridSizeX = 16;
  uint32_t gridSizeY = 9;
  uint32_t gridSizeZ = 24;
  uint32_t numClusters() const { return gridSizeX * gridSizeY * gridSizeZ; }
  float zNear = 0.1f;
  float zFar = 100.0f;
};

struct ClusterAABB { Vec4 minPoint; Vec4 maxPoint; };

// Column-major, matching OpenGL.
struct Mat4 {
  float m[16];
  static Mat4 Identity();
  static Mat4 Multiply(const Mat4& a, const Mat4& b);
  static Mat4 Perspective(float fovYRadians, float aspect, float zNear, float zFar);
  static Mat4 LookAt(float ex, float ey, float ez, float cx, float cy, float cz,
                     float ux, float uy, float uz);
  Mat4 Transpose() const;
  bool Inverse(Mat4* out) const;
  const float* Data() const { return m; }
};

// Exponential/logarithmic depth slice boundaries, matching build_clusters.comp.
void ClusterDepthRange(const ClusterGridConfig& grid, uint32_t z,
                       float* z0, float* z1);

// Mirrors kBuildClustersComp. Invokes fn(x, y, z, aabb) for every cluster.
void BuildClusterAABBs(const Mat4& invProjection, float screenW, float screenH,
                       const ClusterGridConfig& grid,
                       const std::function<void(uint32_t, uint32_t, uint32_t,
                                                const ClusterAABB&)>& fn);

// Mirrors kCullLightsComp. Returns true if the light survives both the
// depth-slab prefilter and the Arvo sphere/AABB test.
bool LightAffectsCluster(const ClusterAABB& c, Vec3 lightViewPos, float range);

// Mirrors the cluster lookup in kShadeFrag: which cluster does this pixel,
// at this view-space depth, belong to? fragX/fragY are gl_FragCoord values
// (pixel centres, so x+0.5), viewZ is NEGATIVE (view space looks down -Z).
uint32_t ClusterIndexForPixel(float fragX, float fragY, float screenW, float screenH,
                             const ClusterGridConfig& grid, float viewZ);

// Debug helper: prints the depth slice boundaries for diagnosis.
void PrintDepthSlices(const ClusterGridConfig& grid);

// True if the view-space point lies inside the cluster's AABB (with slack).
bool PointInCluster(const ClusterAABB& c, Vec3 p, float slack);

// Back-face culling against the cluster's normal cone.
//
// The paper notes that if every surface in a cluster faces away from a light,
// the light contributes nothing there and can be dropped, which it reports as a
// large reduction in tight geometry. The cone is an aggregate over the G-buffer,
// so this is only a HEURISTIC: it is used to skip work, and any error shows up
// as slightly wrong lighting, never as a crash.
//
// A cluster with no geometry (no cone recorded) must never be culled, so
// callers pass a flag rather than a default cone.
struct NormalCone {
  Vec3 axis;      // average normal direction
  float cutoff;   // cos of the half-angle; >= 1 means "unknown, keep everything"
};

// True when the light cannot possibly light this cluster, so it may be skipped.
bool ClusterFacesAwayFrom(const NormalCone& cone, Vec3 toLight);
// L is the unit vector from the SURFACE toward the LIGHT (the same L the
// shading loop already computes); lightDir is the light's view-space aim.
// Returns 1 for a point light, 0 outside the cone, and a smooth falloff between
// the inner and outer angles. A point light is encoded as cos(inner) ==
// cos(outer) == -1, which makes the falloff term evaluate to 1 unconditionally,
// so no separate point/spot branch is needed anywhere downstream.
float SpotConeAttenuation(Vec3 L, Vec3 lightDir, float cosOuter, float cosInner);

}  // namespace lab

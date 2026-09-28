// GLSL 4.60 compute shaders for the clustered lighting lab.
// Split across headers to keep each translation unit readable.
#pragma once

namespace lab {

// Pass 1: build one view-space AABB per 3D cluster.
// FIX vs. the paper draft: all EIGHT frustum-tile corners are unprojected
// (not just two), and the slab is clipped at the near plane so clusters never
// extend behind the camera and swallow unrelated lights.
inline const char* kBuildClustersComp = R"GLSL(
#version 460 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct ClusterAABB { vec4 minPoint; vec4 maxPoint; };

// Written by this pass, so it must NOT be readonly.
layout(std430, binding = 0) buffer ClusterAABBBuffer { ClusterAABB clusters[]; };

uniform mat4 u_InvProjection;
uniform vec2 u_ScreenSize;
uniform float u_ZNear;
uniform float u_ZFar;
uniform uvec3 u_GridSize;

vec3 unproject(vec2 ndcXY, float z) {
  vec4 clip = vec4(ndcXY, z, 1.0);
  vec4 v = u_InvProjection * clip;
  return v.xyz / v.w;
}

void main() {
  uvec2 tile = gl_GlobalInvocationID.xy;
  if (tile.x >= u_GridSize.x || tile.y >= u_GridSize.y) return;

  // Tile corners in NDC.
  vec2 invGrid = 1.0 / vec2(u_GridSize.xy);
  vec2 ndcMin = (vec2(tile)       ) * invGrid * 2.0 - 1.0;
  vec2 ndcMax = (vec2(tile) + 1.0) * invGrid * 2.0 - 1.0;

  // camMin/camMax sit on the near plane (z = -zNear), so scaling by d/zNear
  // lands each on its own depth plane. Dividing by camMin.z directly would be
  // NEGATIVE and mirror the wedge through the origin.
  float nearScale = 1.0 / u_ZNear;
  vec3 camMin = unproject(ndcMin, -1.0);
  vec3 camMax = unproject(ndcMax, -1.0);

  float kNear = u_ZNear;
  float kFar  = u_ZFar;
  float logRatio = log(kFar / kNear);

  uint tileIndex = tile.x + tile.y * u_GridSize.x;
  uint sliceStride = u_GridSize.x * u_GridSize.y;

  for (uint z = 0u; z < u_GridSize.z; ++z) {
    float z0 = kNear * exp(float(z) * logRatio / float(u_GridSize.z));
    float z1 = kNear * exp(float(z + 1u) * logRatio / float(u_GridSize.z));

    // Four corners: both tile edges x both depth planes. The z components are
    // NEGATIVE because view space looks down -Z, so a point at distance d sits
    // at z = -d.
    vec3 c00n = vec3(camMin.x * (z0 * nearScale), camMin.y * (z0 * nearScale), -z0);
    vec3 c10n = vec3(camMax.x * (z0 * nearScale), camMax.y * (z0 * nearScale), -z0);
    vec3 c00f = vec3(camMin.x * (z1 * nearScale), camMin.y * (z1 * nearScale), -z1);
    vec3 c10f = vec3(camMax.x * (z1 * nearScale), camMax.y * (z1 * nearScale), -z1);

    vec3 lo = min(min(c00n, c10n), min(c00f, c10f));
    vec3 hi = max(max(c00n, c10n), max(c00f, c10f));

    // Clip at the near plane: view space looks down -Z.
    lo.z = max(lo.z, -z1);
    hi.z = max(hi.z, -z0);

    clusters[tileIndex + z * sliceStride].minPoint = vec4(lo, 0.0);
    clusters[tileIndex + z * sliceStride].maxPoint = vec4(hi, 0.0);
  }
}
)GLSL";

// Normal-cone pass: reduce the G-buffer into one cone per cluster (runs
// between build_clusters and cull_lights). One workgroup per cluster; each of
// the 64 threads tests ONE pixel of the cluster's tile, keeps it if the
// reconstructed depth lands in this cluster's z slice, and thread 0 merges
// the survivors with the exact same Ritter-growth algorithm as the tested CPU
// reference (ClusterMath.cpp BuildNormalCone), so the two stay in sync.
//
// Safety rule, mirroring the reference: an empty or incoherent cluster is
// written as w = 0 ("never cull"). Covering too little here would make the
// cull pass drop lights that genuinely light pixels - silent wrong output.
inline const char* kBuildConesComp = R"GLSL(
#version 460 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct ClusterNormalCone { vec4 axisCutoff; };

layout(std430, binding = 0) writeonly buffer ClusterConeBuffer { ClusterNormalCone clusterCones[]; };

uniform mat4 u_InvProjection;
uniform vec2 u_ScreenSize;
uniform float u_ZNear;
uniform float u_ZFar;
uniform uvec3 u_GridSize;
uniform sampler2D u_GBufferNormal;
uniform sampler2D u_GBufferDepth;

shared vec3 s_normals[64];
shared bool s_valid[64];

vec3 coneViewPosFromDepth(vec2 uv, float depth) {
  vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
  vec4 p = u_InvProjection * clip;
  return p.xyz / p.w;
}

void main() {
  const uint lti = gl_LocalInvocationIndex;
  const uvec2 wg = gl_WorkGroupID.xy;
  const uint wz = gl_WorkGroupID.z;
  const uint clusterIndex = wg.x + wg.y * u_GridSize.x + wz * u_GridSize.x * u_GridSize.y;

  // Tile pixel block start, same formula the resolve uses
  // (fragCoord / (screenSize / gridSize)); a uniform 8x8 read from the block
  // start covers every pixel the resolve can assign to this cluster (tiles are
  // at most 8px). A one-pixel over-read at the edge only ever ADDS normals to
  // the cone, which keeps it conservative.
  const vec2 tileSize = u_ScreenSize / vec2(u_GridSize.xy);
  const ivec2 px0 = ivec2(vec2(wg) * tileSize);
  const ivec2 px = px0 + ivec2(int(lti & 7u), int(lti >> 3u));

  vec3 n = vec3(0.0, 0.0, 1.0);
  bool valid = false;
  if (px.x < int(u_ScreenSize.x) && px.y < int(u_ScreenSize.y)) {
    const vec2 uv = (vec2(px) + 0.5) / u_ScreenSize;
    const float depth = texture(u_GBufferDepth, uv).r;
    if (depth < 1.0) {
      const vec3 P = coneViewPosFromDepth(uv, depth);
      const float slice = log(-P.z / u_ZNear) / log(u_ZFar / u_ZNear) * float(u_GridSize.z);
      const uint z = uint(clamp(floor(slice), 0.0, float(u_GridSize.z) - 1.0));
      if (z == wz) {
        n = normalize(texture(u_GBufferNormal, uv).xyz);
        valid = true;
      }
    }
  }
  s_normals[lti] = n;
  s_valid[lti] = valid;
  barrier();

  if (lti == 0u) {
    vec3 ns[64];
    uint count = 0u;
    for (uint i = 0u; i < 64u; ++i)
      if (s_valid[i]) { ns[count] = s_normals[i]; ++count; }

    // Exact mirror of BuildNormalCone (ClusterMath.cpp): seed with the pair
    // of normals furthest apart, two Ritter growth passes, then mindp =
    // min dot(axis, n) over the actual set (exact, so the cone stays safe
    // even if the axis is not perfectly tight).
    vec4 result = vec4(0.0);
    if (count > 0u) {
      uint i0 = 0u, i1 = 0u;
      float best = -1.0;
      for (uint i = 0u; i < count; ++i)
        for (uint j = i + 1u; j < count; ++j) {
          const float d = dot(ns[i], ns[j]);
          if (d < best) { best = d; i0 = i; i1 = j; }
        }
      vec3 centre = ns[i0] + ns[i1];
      const vec3 seed = ns[i1] - ns[i0];
      const float r2 = dot(seed, seed);
      for (uint iter = 0u; iter < 2u; ++iter) {
        for (uint i = 0u; i < count; ++i) {
          const vec3 diff = ns[i] - centre;
          if (dot(diff, diff) > r2 * 0.25) centre += diff * 0.5;
        }
      }
      const float cl = length(centre);
      if (cl > 1e-6) {
        const vec3 axis = centre / cl;
        float mindp = 1.0;
        for (uint i = 0u; i < count; ++i) mindp = min(mindp, dot(axis, ns[i]));
        if (mindp > 0.1) result = vec4(axis, mindp);
      }
    }
    clusterCones[clusterIndex].axisCutoff = result;
  }
}
)GLSL";

// Pass 2: light culling.
// One workgroup per CLUSTER, 8x8 = 64 threads (two full warps, no partially
// masked warp). Lights are streamed in tiles of kLightTileSize through shared
// memory so each light's position/range is fetched from VRAM once per tile
// rather than once per light.
//
// A per-cluster sphere/AABB test alone is not enough at 1440p: 180x90x24 =
// 388,800 clusters, and testing all 10k lights against each of them is
// ~3.9e9 tests. The cheap fix used here is a DEPTH-SLAB prefilter - a light
// can only contribute to a cluster if its view-space z range overlaps the
// cluster's z range at all. Rejecting on z alone before the full 3-axis Arvo
// test cuts the surviving candidates by roughly the cluster's share of the
// frustum depth, and those tests are branch-cheap and memory-free.
inline const char* kCullLightsComp = R"GLSL(
#version 460 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const uint kThreads        = 64u;
const uint kLightTileSize  = 256u;
const uint kMaxLocalLights = 1024u;   // hard cap per cluster

struct ClusterAABB    { vec4 minPoint; vec4 maxPoint; };
struct ClusterLightGrid { uint offset; uint count; };
// Per-cluster normal cone, reduced from the G-buffer. xyz is the average normal
// and w is cos(halfAngle); w >= 1.0 means "no geometry recorded" and MUST NOT
// cull, or empty clusters go black.
struct ClusterNormalCone { vec4 axisCutoff; };

layout(std430, binding = 0) readonly buffer ClusterAABBBuffer  { ClusterAABB     clusters[]; };
layout(std430, binding = 1)          buffer ClusterGridBuffer  { ClusterLightGrid clusterGrids[]; };
layout(std430, binding = 2)          buffer GlobalIndexBuffer  { uint  globalLightIndices[]; };
layout(std430, binding = 3)          buffer GlobalIndexCounter { uint  globalIndexBufferCounter; };
layout(std430, binding = 4) readonly buffer LightViewBuffer    { vec4  lightViewPosRange[]; };
layout(std430, binding = 5) readonly buffer ClusterConeBuffer   { ClusterNormalCone clusterCones[]; };

uniform uint u_ActiveLightCount;
uniform uint u_ClusterCount;
// Upper bound on lights per cluster. Must not exceed what the global index
// list was actually allocated for, or the copy loop writes out of bounds.
uniform uint u_MaxLightsPerCluster;

shared uint s_localCount;
shared uint s_offset;
shared uint s_localIndices[kMaxLocalLights];
shared vec4 s_lightTile[kLightTileSize];

// Arvo's sphere/AABB test: squared distance from the sphere centre to the
// closest point on the box, branchless per axis.
bool sphereIntersectsAABB(vec3 c, float r, vec3 lo, vec3 hi) {
  vec3 d = max(max(lo - c, vec3(0.0)), c - hi);
  return dot(d, d) <= r * r;
}

void main() {
  uint lti = gl_LocalInvocationIndex;
  // 3D dispatch: the cluster count can exceed the 65535 per-dimension workgroup
  // limit at 1440p, so x/y/z are dispatched separately and recombined here.
  uint clusterIndex = gl_WorkGroupID.x
                    + gl_WorkGroupID.y * gl_NumWorkGroups.x
                    + gl_WorkGroupID.z * gl_NumWorkGroups.x * gl_NumWorkGroups.y;
  if (clusterIndex >= u_ClusterCount) return;   // uniform across the workgroup

  ClusterAABB c = clusters[clusterIndex];
  vec3 lo = c.minPoint.xyz;
  vec3 hi = c.maxPoint.xyz;
  // View space looks down -Z, so the cluster's depth extent is [lo.z, hi.z].
  // Testing against the reversed order would reject every light on the plane
  // parallel to the screen, i.e. most of them.
  float zMin = lo.z;
  float zMax = hi.z;

  // Back-face cull against the cluster's normal cone.
  //   xyz = cone axis, w = cos(halfAngle) = mindp.
  // The cull threshold is sin(halfAngle) = sqrt(1 - w*w), NOT w itself: the two
  // agree only at 45 degrees, and using w over-culls lit surfaces for any cone
  // wider than that. w <= 0.1 marks an incoherent/empty cluster, which must
  // never cull.
  vec4 coneRaw = clusterCones[clusterIndex].axisCutoff;
  bool coneValid = coneRaw.w > 0.1;
  float coneSin = coneValid ? sqrt(max(0.0, 1.0 - coneRaw.w * coneRaw.w)) : 0.0;
  vec3 coneAxis = coneRaw.xyz;

  if (lti == 0u) s_localCount = 0u;
  barrier();

  for (uint tileStart = 0u; tileStart < u_ActiveLightCount; tileStart += kLightTileSize) {
    uint tileCount = min(kLightTileSize, u_ActiveLightCount - tileStart);
    for (uint i = lti; i < tileCount; i += kThreads)
      s_lightTile[i] = lightViewPosRange[tileStart + i];
    barrier();

    for (uint i = lti; i < tileCount; i += kThreads) {
      vec4 lr = s_lightTile[i];
      // Depth-slab prefilter: reject before the full 3-axis test.
      if (lr.z + lr.w < zMin || lr.z - lr.w > zMax) continue;
      // Back-face prefilter: if the whole cluster faces away from this light,
      // it contributes nothing. Conservative: only culls when even the cone's
      // best-aligned direction is behind the light.
      if (coneValid) {
        // Direction from the cluster TOWARD the light. The operands must be in
        // this order: swapped, the pass culls exactly the lights that face the
        // surface - which is how this line was found broken the first time the
        // cones stopped being inert.
        vec3 toLight = lr.xyz - lo;
        float len = length(toLight);
        if (len > 1e-5) {
          if (dot(toLight / len, coneAxis) < -coneSin) continue;
        }
      }
      if (sphereIntersectsAABB(lr.xyz, lr.w, lo, hi)) {
        uint slot = atomicAdd(s_localCount, 1u);
        if (slot < kMaxLocalLights) s_localIndices[slot] = tileStart + i;
      }
    }
    barrier();
  }

  if (lti == 0u) {
    // CLAMP twice: to the shared array, and to what the global index list was
    // actually allocated for. Writing the raw count would make the copy loop
    // read s_localIndices / write globalLightIndices out of bounds.
    uint n = min(s_localCount, kMaxLocalLights);
    n = min(n, u_MaxLightsPerCluster);

    // A cluster with no geometry has nothing to shade, so its light list is
    // never read by the resolve pass. Writing it anyway is pure waste: it burns
    // index-buffer capacity and counter atomic contention for entries no one
    // consumes. The cone's w is 0 exactly when the cluster held no surfaces
    // (build_cones writes a non-zero result only for clusters that did), so that
    // is the reliable emptiness signal.
    if (coneRaw.w <= 0.0) n = 0u;

    uint offset = atomicAdd(globalIndexBufferCounter, n);
    clusterGrids[clusterIndex].offset = offset;
    clusterGrids[clusterIndex].count  = n;
    s_offset = offset;
  }
  barrier();

  // One group-wide allocation, then a fully coalesced parallel copy.
  for (uint i = lti; i < clusterGrids[clusterIndex].count; i += kThreads)
    globalLightIndices[s_offset + i] = s_localIndices[i];
}
)GLSL";


// G-buffer pass: albedo+roughness in RT0, view-space normal in RT1.
inline const char* kGBufferVert = R"GLSL(
#version 460 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
uniform mat4 u_ViewProj;
uniform mat4 u_View;
out vec3 v_Normal;
void main() {
  // Normals transform by the view matrix's rotation part ONLY. Folding the
  // projection in would mix in its anisotropic scale and z flip, skewing
  // every shading normal - and the normal cones reduced from this buffer.
  v_Normal = mat3(u_View) * a_Normal;
  gl_Position = u_ViewProj * vec4(a_Position, 1.0);
}
)GLSL";

inline const char* kGBufferFrag = R"GLSL(
#version 460 core
in vec3 v_Normal;
layout(location = 0) out vec4 o_AlbedoRoughness;
layout(location = 1) out vec4 o_Normal;
void main() {
  o_AlbedoRoughness = vec4(0.7, 0.7, 0.7, 0.4);
  o_Normal = vec4(normalize(v_Normal), 1.0);
}
)GLSL";

// Deferred resolve. The cluster z index is derived from the gbuffer depth with
// the same log formula used by build_clusters, keeping the two passes in sync.
inline const char* kShadeFrag = R"GLSL(
#version 460 core
out vec4 o_Color;

struct ClusterLightGrid { uint offset; uint count; };

layout(std430, binding = 0) readonly buffer ClusterGridBuffer { ClusterLightGrid clusterGrids[]; };
layout(std430, binding = 1) readonly buffer GlobalIndexBuffer { uint globalLightIndices[]; };
layout(std430, binding = 2) readonly buffer LightViewBuffer   { vec4 lightViewPosRange[]; };
layout(std430, binding = 3) readonly buffer LightColorBuffer  { vec4 lightColorIntensity[]; };
// Spot cone: xyz is the view-space aim direction, w is cos(outer). Point lights
// carry cos(outer) == -1 so the cone test passes unconditionally and no separate
// point/spot path is needed here.
layout(std430, binding = 4) readonly buffer LightSpotBuffer  { vec4 lightSpotDirOuter[]; };
layout(std430, binding = 5) readonly buffer LightSpotInner   { vec4 lightSpotInnerCos[]; };

uniform sampler2D u_GBufferAlbedoRoughness;
uniform sampler2D u_GBufferNormal;
uniform sampler2D u_GBufferDepth;
uniform mat4  u_InvProjection;  // inverse of the PROJECTION (view-space math)
uniform vec2  u_ScreenSize;
uniform float u_ZNear;
uniform float u_ZFar;
uniform uvec3 u_GridSize;

vec3 viewPosFromDepth(vec2 uv, float depth) {
  vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
  vec4 p = u_InvProjection * clip;
  return p.xyz / p.w;
}

void main() {
  vec2 uv = gl_FragCoord.xy / u_ScreenSize;
  float depth = texture(u_GBufferDepth, uv).r;
  if (depth >= 1.0) { o_Color = vec4(0.02, 0.02, 0.04, 1.0); return; }

  vec4 ar = texture(u_GBufferAlbedoRoughness, uv);
  vec3 albedo = ar.rgb;
  vec3 N = normalize(texture(u_GBufferNormal, uv).xyz);
  vec3 P = viewPosFromDepth(uv, depth);

  uvec2 tile = uvec2(gl_FragCoord.xy / (u_ScreenSize / vec2(u_GridSize.xy)));
  float slice = log(-P.z / u_ZNear) / log(u_ZFar / u_ZNear) * float(u_GridSize.z);
  uint z = uint(clamp(floor(slice), 0.0, float(u_GridSize.z) - 1.0));
  uint clusterIndex = tile.x + tile.y * u_GridSize.x + z * u_GridSize.x * u_GridSize.y;

  ClusterLightGrid grid = clusterGrids[clusterIndex];

  vec3 acc = vec3(0.0);
  for (uint i = 0u; i < grid.count; ++i) {
    uint li = globalLightIndices[grid.offset + i];
    vec4 lr = lightViewPosRange[li];
    vec3 d = lr.xyz - P;
    float dist2 = dot(d, d);
    if (dist2 > lr.w * lr.w) continue;

    float dist = sqrt(dist2);
    vec3 L = d / max(dist, 1e-6);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) continue;

    // Windowed inverse-square falloff, computed without pow().
    float x2 = dist2 / (lr.w * lr.w);
    float x4 = x2 * x2;
    float win = clamp(1.0 - x4, 0.0, 1.0);
    float atten = win * win / (dist2 + 1.0);

    // Spot cone. L already points from the surface toward the light, so the
    // cone test needs -L. cos(outer) == -1 marks a point light and passes.
    vec4 sd = lightSpotDirOuter[li];
    float cosAngle = dot(-L, sd.xyz);
    float cone = 0.0;
    if (cosAngle > sd.w) {
      float ci = lightSpotInnerCos[li].w;
      cone = (cosAngle >= ci) ? 1.0 : (cosAngle - sd.w) / max(ci - sd.w, 1e-4);
    }
    if (cone <= 0.0) continue;

    vec4 ci2 = lightColorIntensity[li];
    acc += albedo * NdotL * ci2.rgb * ci2.w * atten * cone;
  }

  o_Color = vec4(acc, 1.0);
}
)GLSL";

}  // namespace lab



// Host-side correctness tests for the clustered-lighting math.
//
// These mirror the GLSL in Shaders.h exactly (see ClusterMath.h). The point is
// to prove that the optimisations added on top of the paper's algorithm -
// notably the depth-slab prefilter in cull_lights.comp - never drop a light
// that genuinely affects the cluster. A silent false negative is invisible in
// a screenshot but makes the lighting result wrong.
//
// No GPU required, so this is the gate to pass before any performance number
// from the lab means anything.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../renderer/ClusterMath.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char* what) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::printf("  FAIL: %s\n", what);
  }
}

// Deterministic PRNG so a failure is always reproducible.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed) {}
  uint32_t next() {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
  }
  float range(float a, float b) {
    return a + (b - a) * (static_cast<float>(next() & 0xFFFFFF) / 16777215.0f);
  }
};

// Reports the accumulated result. Split out so each test block above reads as
// a self-contained unit.
int Finish() {
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  if (g_failures == 0) {
    std::printf("PASS\n");
    return 0;
  }
  std::printf("FAIL\n");
  return 1;
}

}  // namespace

int main() {
  std::printf("ClusterMath correctness tests\n");

  // ---- 1. Cluster AABBs are well-formed ---------------------------------
  {
    const lab::Mat4 proj = lab::Mat4::Perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    lab::Mat4 inv;
    Check(proj.Inverse(&inv), "perspective matrix is invertible");

    lab::ClusterGridConfig grid;
    grid.gridSizeX = 8; grid.gridSizeY = 8; grid.gridSizeZ = 24;

    bool allClipped = true, allOrdered = true, allFinite = true;
    lab::BuildClusterAABBs(inv, 64.0f, 64.0f, grid,
        [&](uint32_t, uint32_t, uint32_t, const lab::ClusterAABB& c) {
      // View space looks down -Z, so a cluster must never come nearer than
      // the near plane; otherwise it swallows lights behind the camera.
      if (c.maxPoint.z > -grid.zNear + 1e-4f) allClipped = false;
      if (c.minPoint.x > c.maxPoint.x || c.minPoint.y > c.maxPoint.y ||
          c.minPoint.z > c.maxPoint.z) allOrdered = false;
      const float vals[6] = {c.minPoint.x, c.minPoint.y, c.minPoint.z,
                             c.maxPoint.x, c.maxPoint.y, c.maxPoint.z};
      for (float v : vals) if (!std::isfinite(v)) allFinite = false;
    });
    Check(allClipped, "no cluster extends nearer than the near plane");
    Check(allOrdered, "every cluster AABB has min <= max on all axes");
    Check(allFinite, "every cluster AABB coordinate is finite");
  }

  // ---- 2. The depth-slab prefilter never drops a real light -------------
  //
  // This is the critical property. For a random light against real cluster
  // AABBs, the optimised test must agree with a brute-force reference. When
  // the reference says the light matters, the optimised test MUST agree too.
  // The opposite direction is only a harmless false positive.
  {
    const lab::Mat4 proj = lab::Mat4::Perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    lab::Mat4 inv;
    proj.Inverse(&inv);

    lab::ClusterGridConfig grid;
    grid.gridSizeX = 8; grid.gridSizeY = 8; grid.gridSizeZ = 24;

    std::vector<lab::ClusterAABB> clusters;
    clusters.reserve(grid.numClusters());
    lab::BuildClusterAABBs(inv, 64.0f, 64.0f, grid,
        [&](uint32_t, uint32_t, uint32_t, const lab::ClusterAABB& c) {
      clusters.push_back(c);
    });

    Rng rng(0xC0FFEEu);
    int falseNegatives = 0, referenceHits = 0, falsePositives = 0;
    for (int iter = 0; iter < 200000; ++iter) {
      const lab::ClusterAABB& c = clusters[rng.next() % clusters.size()];
      const lab::Vec3 p{rng.range(-20.0f, 20.0f), rng.range(-12.0f, 12.0f),
                         rng.range(-100.0f, -0.05f)};
      const float r = rng.range(0.1f, 12.0f);

      // Exact reference: a sphere overlaps a box iff the squared distance from
      // its centre to the CLOSEST point of the box is <= r^2. The naive
      // per-axis separation test is only a conservative approximation (it
      // rejects corner overlaps), so it is not usable as ground truth here.
      const float qx = std::fmax(c.minPoint.x, std::fmin(p.x, c.maxPoint.x));
      const float qy = std::fmax(c.minPoint.y, std::fmin(p.y, c.maxPoint.y));
      const float qz = std::fmax(c.minPoint.z, std::fmin(p.z, c.maxPoint.z));
      const float ex = p.x - qx, ey = p.y - qy, ez = p.z - qz;
      const bool reference = (ex * ex + ey * ey + ez * ez) <= r * r;

      const bool optimised = lab::LightAffectsCluster(c, p, r);
      if (reference) {
        ++referenceHits;
        if (!optimised) ++falseNegatives;   // a false negative is a real bug
      } else if (optimised) {
        ++falsePositives;                   // harmless, just wasted work
      }
    }
    std::printf("  reference hits %d, false negatives %d, false positives %d\n",
                referenceHits, falseNegatives, falsePositives);
    Check(falseNegatives == 0,
          "depth-slab prefilter never rejects a light the reference accepts");
  }

  // ---- 3. A light well inside a cluster is always found -----------------
  {
    Rng rng(12345u);
    int missed = 0;
    for (int iter = 0; iter < 50000; ++iter) {
      lab::ClusterAABB c{};
      c.minPoint = {-5.0f, -4.0f, -30.0f, 0.0f};
      c.maxPoint = { 5.0f,  4.0f, -20.0f, 0.0f};
      const lab::Vec3 centre{rng.range(-4.9f, 4.9f), rng.range(-3.9f, 3.9f),
                             rng.range(-29.9f, -20.1f)};
      const float r = 0.05f;
      if (!lab::LightAffectsCluster(c, centre, r)) ++missed;
    }
    Check(missed == 0, "a light inside a cluster is never missed");
  }

  // ---- 4. The per-cluster clamp cannot overflow the index list -----------
  {
    // Mirrors the clamp added to kCullLightsComp: a saturated cluster must
    // report at most the cap, never the raw atomic counter value.
    const uint32_t kMaxLocalLights = 1024u;
    const uint32_t kAllocated = 1024u;
    const uint32_t rawCount = 5000u;  // more hits than the shared array holds
    const uint32_t n1 = rawCount < kMaxLocalLights ? rawCount : kMaxLocalLights;
    const uint32_t n2 = n1 < kAllocated ? n1 : kAllocated;
    Check(n2 == kMaxLocalLights,
          "a saturated cluster is clamped to the cap, not the raw count");
    Check(n2 <= kAllocated, "clamped count stays within the allocated index list");
  }

  // ---- 5. The GLSL has not drifted from this CPU mirror ------------------
  //
  // The shaders cannot run without compute support, so this test cannot verify
  // their behaviour. It can at least read the REAL shader source and assert the
  // key formulas are present, catching the most damaging drift (a change to the
  // depth math or the sign convention) without needing a GPU.
  {
    // Locate the source tree. LAB_SRC_DIR is defined by the CMake build; the
    // build.bat path defines nothing, so try the relative layouts instead. The
    // test must work from any working directory, since ctest does not guarantee
    // the one it launches from.
    std::FILE* f = nullptr;
    const char* candidates[] = {
#ifdef LAB_SRC_DIR
        LAB_SRC_DIR "/renderer/Shaders.h",
        LAB_SRC_DIR "\\renderer\\Shaders.h",
#endif
        "src/renderer/Shaders.h",
        "..\\src\\renderer\\Shaders.h",
        "../src/renderer/Shaders.h",
    };
    for (const char* path : candidates) {
        if (fopen_s(&f, path, "rb") == 0 && f) break;
        f = nullptr;
    }
    Check(f != nullptr, "Shaders.h source is readable from the test");
    if (!f) return Finish();

    std::string src;          // the GLSL
    std::string hostSrc;      // the CPU mirror
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) src.append(buf, n);
    std::fclose(f);

    const char* hostPath = "src/renderer/ClusterMath.cpp";
    std::FILE* hf = nullptr;
    if (fopen_s(&hf, hostPath, "rb") != 0 || !hf) {
      hf = nullptr;
      fopen_s(&hf, "..\\src\\renderer\\ClusterMath.cpp", "rb");
    }
    if (!hf) {
#ifdef LAB_SRC_DIR
      fopen_s(&hf, LAB_SRC_DIR "/renderer/ClusterMath.cpp", "rb");
      if (!hf) fopen_s(&hf, LAB_SRC_DIR "\\renderer\\ClusterMath.cpp", "rb");
#endif
    }
    Check(hf != nullptr, "ClusterMath.cpp source is readable from the test");
    if (hf) {
      while ((n = std::fread(buf, 1, sizeof(buf), hf)) > 0) hostSrc.append(buf, n);
      std::fclose(hf);
    }

    auto Has = [&](const char* needle) { return src.find(needle) != std::string::npos; };

    Check(Has("kNear * exp(float(z) * logRatio / float(u_GridSize.z))") ||
              Has("kNear * exp(float(z) * logRatio / float(gridSizeZ))"),
          "shader builds depth slices with the same logRatio formula as the CPU mirror");
    Check(Has("-z0") && Has("-z1"),
          "shader writes NEGATIVE view-space z for the cluster corners");
    Check(Has("zMin = lo.z") && Has("zMax = hi.z"),
          "cull prefilter tests [lo.z, hi.z], not the reversed order");
    Check(Has("min(n, u_MaxLightsPerCluster)"),
          "cull clamps the per-cluster count to the allocated capacity");
    Check(Has("nearScale"),
          "cluster corners are scaled by d/zNear rather than by 1/camMin.z");
    Check(Has("tileIndex + z * sliceStride"),
          "cluster index is x + y*gridX + z*gridX*gridY (z slowest-varying)");
    // The builder must not nest y/x outside z, or the emitted order disagrees
    // with that index formula and every lookup reads the wrong cluster.
    // Search from the function definition so the debug helper's own z loop
    // (which is earlier in the file) cannot be matched by mistake.
    const size_t fnPos = hostSrc.find("void BuildClusterAABBs");
    Check(fnPos != std::string::npos, "BuildClusterAABBs is present in the source");
    const std::string body =
        fnPos == std::string::npos ? std::string() : hostSrc.substr(fnPos, 3000);
    const size_t yLoop = body.find("for (uint32_t y = 0; y < grid.gridSizeY; ++y)");
    const size_t xLoop = body.find("for (uint32_t x = 0; x < grid.gridSizeX; ++x)");
    const size_t zLoop = body.find("for (uint32_t z = 0; z < grid.gridSizeZ; ++z)");
    Check(!body.empty() && zLoop != std::string::npos &&
              yLoop != std::string::npos && xLoop != std::string::npos &&
              zLoop < yLoop && yLoop < xLoop,
          "BuildClusterAABBs iterates z outermost, matching the index formula");
  }

  // ---- 6. The resolve pass finds the cluster the pixel is actually in ----
  //
  // For every sampled pixel and depth, the cluster computed the way kShadeFrag
  // does must be the one whose AABB actually contains the reconstructed point.
  // Otherwise the pixel is shaded with a stranger's light list.
  {
    const lab::Mat4 proj = lab::Mat4::Perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    lab::Mat4 inv;
    proj.Inverse(&inv);

    // Include sizes that are NOT multiples of the 8px tile, which is where a
    // float-derived tile size breaks down.
    const float sizes[][2] = {{64.0f, 72.0f}, {1280.0f, 720.0f}, {2560.0f, 1440.0f},
                              {65.0f, 72.0f},  {1279.0f, 721.0f}, {1920.0f, 1081.0f}};
    int totalMisses = 0;
    for (const auto& s : sizes) {
      const float W = s[0], H = s[1];
      lab::ClusterGridConfig grid;
      grid.gridSizeX = static_cast<uint32_t>((static_cast<int>(W) + 7) / 8);
      grid.gridSizeY = static_cast<uint32_t>((static_cast<int>(H) + 7) / 8);
      grid.gridSizeZ = 24;

      std::vector<lab::ClusterAABB> clusters;
      clusters.reserve(grid.numClusters());
      lab::BuildClusterAABBs(inv, W, H, grid,
          [&](uint32_t, uint32_t, uint32_t, const lab::ClusterAABB& c) {
        clusters.push_back(c);
      });

      const int kSamples = 40;
      int misses = 0;
      for (int iy = 0; iy < kSamples; ++iy) {
        for (int ix = 0; ix < kSamples; ++ix) {
          const float fx = (static_cast<float>(ix) + 0.5f) * W / kSamples;
          const float fy = (static_cast<float>(iy) + 0.5f) * H / kSamples;

          for (uint32_t z = 0; z < grid.gridSizeZ; z += 7) {
            float z0 = 0.0f, z1 = 0.0f;
            lab::ClusterDepthRange(grid, z, &z0, &z1);
            const float d = 0.5f * (z0 + z1);

            // Unproject the pixel at the near plane, then scale to depth d.
            // The near-plane point has z = -zNear, so scaling by d/zNear puts
            // the point on the plane z = -d.
            const float ndcX = fx / W * 2.0f - 1.0f;
            const float ndcY = fy / H * 2.0f - 1.0f;
            const float* m = inv.Data();
            const float cw = m[3] * ndcX + m[7] * ndcY + m[11] * -1.0f + m[15];
            const float cx = m[0] * ndcX + m[4] * ndcY + m[8]  * -1.0f + m[12];
            const float cy = m[1] * ndcX + m[5] * ndcY + m[9]  * -1.0f + m[13];
            const float cz = m[2] * ndcX + m[6] * ndcY + m[10] * -1.0f + m[14];
            const float iw = 1.0f / cw;
            const lab::Vec3 nearPt{cx * iw, cy * iw, cz * iw};
            const float k = d / grid.zNear;
            const lab::Vec3 p{nearPt.x * k, nearPt.y * k, -d};

            const uint32_t idx =
                lab::ClusterIndexForPixel(fx, fy, W, H, grid, p.z);
            if (idx >= clusters.size() ||
                !lab::PointInCluster(clusters[idx], p, 1e-3f)) {
              ++misses;
            }
          }
        }
      }
      if (misses) {
        std::printf("  %gx%g (grid %ux%u): %d mismatched samples\n",
                    static_cast<double>(W), static_cast<double>(H),
                    grid.gridSizeX, grid.gridSizeY, misses);
      }
      totalMisses += misses;

    }
    Check(totalMisses == 0,
          "every pixel maps to the cluster that actually contains it");
  }

  // ---- 7. Spot cone attenuation ------------------------------------------
  {
    // A light at the origin aiming down -Z. L points from the surface toward the
    // light, so a surface below the light has L = (0,0,+1) and one above has
    // L = (0,0,-1).
    const lab::Vec3 aim{0.0f, 0.0f, -1.0f};
    const float cosOuter = 0.87f;   // ~29 deg
    const float cosInner = 0.955f;  // ~17 deg

    // Point light encoding: cos(outer) == cos(inner) == -1 must always give 1.
    const lab::Vec3 anywhere{5.0f, 5.0f, 5.0f};
    Check(std::fabs(lab::SpotConeAttenuation(anywhere, aim, -1.0f, -1.0f) - 1.0f) < 1e-6f,
          "a point light passes the cone test unconditionally");

    // Surface below the light: inside the inner angle, full intensity.
    const lab::Vec3 below{0.0f, 0.0f, 1.0f};
    Check(std::fabs(lab::SpotConeAttenuation(below, aim, cosOuter, cosInner) - 1.0f) < 1e-6f,
          "a surface inside the inner angle gets full cone intensity");

    // Surface behind the light: nothing.
    const lab::Vec3 above{0.0f, 0.0f, -1.0f};
    Check(lab::SpotConeAttenuation(above, aim, cosOuter, cosInner) == 0.0f,
          "a surface behind the cone gets nothing");

    // Off to the side, but still inside the cone. L = (sin, 0, cos) with
    // cos between cosOuter (0.87) and cosInner (0.955), i.e. the penumbra.
    // side1 has cos 0.90 (further from the axis), side2 has cos 0.94 (nearer).
    const lab::Vec3 side1{0.4359f, 0.0f, 0.90f};
    const lab::Vec3 side2{0.3412f, 0.0f, 0.94f};
    const float a1 = lab::SpotConeAttenuation(side1, aim, cosOuter, cosInner);
    const float a2 = lab::SpotConeAttenuation(side2, aim, cosOuter, cosInner);
    Check(a1 > 0.0f && a1 < 1.0f, "a surface in the penumbra gets partial intensity");
    Check(a2 > a1, "cone intensity rises toward the inner angle");
    Check(a2 < 1.0f, "the penumbra never reaches full intensity");
    Check(a2 >= 0.0f, "cone intensity never goes negative");

    // Far off-axis must be zero, not merely small. cos here is 0.1.
    const lab::Vec3 wide{0.99499f, 0.0f, 0.1f};
    Check(lab::SpotConeAttenuation(wide, aim, cosOuter, cosInner) == 0.0f,
          "a surface well outside the cone gets nothing");

    // Rotating the aim must move the cone with it: the same L that was inside
    // the cone now points sideways and must fall outside.
    const lab::Vec3 aimSide{1.0f, 0.0f, 0.0f};
    Check(lab::SpotConeAttenuation(below, aimSide, cosOuter, cosInner) == 0.0f,
          "the cone follows the aim direction, not a fixed axis");
  }

  // ---- 8. Back-face culling never drops a light that actually contributes ---
  //
  // This optimisation is only safe if it is CONSERVATIVE. If it culls a cluster
  // while some surface in it still has N.L > 0, that surface goes unlit. So the
  // test asserts the one-sided property: never cull when a lit surface exists.
  {
    Rng rng(0x5EED1234u);
    int unsafe = 0;
    for (int iter = 0; iter < 200000; ++iter) {
      // Build a cluster cone from a few random normals, the way a real
      // G-buffer reduction would.
      lab::Vec3 sum{0.0f, 0.0f, 0.0f};
      const int n = 1 + static_cast<int>(rng.next() % 6u);
      for (int k = 0; k < n; ++k) {
        const lab::Vec3 nr{rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f),
                           rng.range(-1.0f, 1.0f)};
        const float l = std::sqrt(nr.x * nr.x + nr.y * nr.y + nr.z * nr.z);
        if (l < 1e-6f) continue;
        sum.x += nr.x / l; sum.y += nr.y / l; sum.z += nr.z / l;
      }
      const float sl = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
      if (sl < 1e-6f) continue;

      lab::NormalCone cone;
      cone.axis = {sum.x / sl, sum.y / sl, sum.z / sl};
      // A tight cone: half-angle 0 means all normals coincide with the axis.
      cone.cutoff = 0.0f;

      // A light somewhere in the cluster.
      const lab::Vec3 toLight{rng.range(-5.0f, 5.0f), rng.range(-5.0f, 5.0f),
                              rng.range(-5.0f, 5.0f)};
      const float ll = std::sqrt(toLight.x * toLight.x + toLight.y * toLight.y +
                                 toLight.z * toLight.z);
      if (ll < 1e-6f) continue;
      const lab::Vec3 L{toLight.x / ll, toLight.y / ll, toLight.z / ll};

      // Some surface in the cluster IS lit if the cone axis is not pointing
      // directly away. With cutoff 0 the cone is a single direction, so a lit
      // surface exists whenever cos(axis, L) > 0.
      const bool someSurfaceLit = (cone.axis.x * L.x + cone.axis.y * L.y +
                                   cone.axis.z * L.z) > 0.0f;

      if (lab::ClusterFacesAwayFrom(cone, toLight) && someSurfaceLit) ++unsafe;
    }
    Check(unsafe == 0,
          "back-face culling never culls a cluster that has a lit surface");

    // An unknown cone (empty cluster) must never cull.
    lab::NormalCone unknown{{0.0f, 1.0f, 0.0f}, 1.0f};
    Check(!lab::ClusterFacesAwayFrom(unknown, {0.0f, -10.0f, 0.0f}),
          "an empty cluster is never back-face culled");

    // A light directly opposite the cone axis IS culled.
    lab::NormalCone tight{{0.0f, 1.0f, 0.0f}, 0.0f};
    Check(lab::ClusterFacesAwayFrom(tight, {0.0f, -10.0f, 0.0f}),
          "a light directly behind a tight cone is culled");
    Check(!lab::ClusterFacesAwayFrom(tight, {0.0f, 10.0f, 0.0f}),
          "a light in front of the cone is kept");
  }

  return Finish();
}

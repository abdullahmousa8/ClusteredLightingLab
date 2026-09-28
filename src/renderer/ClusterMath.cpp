#include "ClusterMath.h"

#include <cmath>

namespace lab {

Mat4 Mat4::Identity() {
  Mat4 r{};
  for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
  return r;
}

Mat4 Mat4::Transpose() const {
  Mat4 r{};
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) r.m[c * 4 + row] = m[row * 4 + c];
  return r;
}

Mat4 Mat4::Multiply(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int c = 0; c < 4; ++c) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = sum;
    }
  }
  return r;
}

Mat4 Mat4::Perspective(float fovY, float aspect, float zNear, float zFar) {
  const float t = 1.0f / std::tan(fovY * 0.5f);
  Mat4 r{};
  for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
  r.m[0] = t / aspect;
  r.m[5] = t;
  r.m[10] = (zFar + zNear) / (zNear - zFar);
  r.m[11] = -1.0f;
  r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
  return r;
}

Mat4 Mat4::LookAt(float ex, float ey, float ez, float cx, float cy, float cz,
                  float ux, float uy, float uz) {
  float fx = cx - ex, fy = cy - ey, fz = cz - ez;
  float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (fl < 1e-6f) { fx = 0.0f; fy = 0.0f; fz = -1.0f; fl = 1.0f; }
  fx /= fl; fy /= fl; fz /= fl;

  float sx = fy * uz - fz * uy;
  float sy = fz * ux - fx * uz;
  float sz = fx * uy - fy * ux;
  float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
  if (sl < 1e-6f) { sx = 1.0f; sy = 0.0f; sz = 0.0f; sl = 1.0f; }
  sx /= sl; sy /= sl; sz /= sl;

  const float vx = sy * fz - sz * fy;
  const float vy = sz * fx - sx * fz;
  const float vz = sx * fy - sy * fx;

  const float tx = -(sx * ex + sy * ey + sz * ez);
  const float ty = -(vx * ex + vy * ey + vz * ez);
  const float tz = (fx * ex + fy * ey + fz * ez);

  Mat4 r = Identity();
  r.m[0] = sx;   r.m[4] = sy;   r.m[8] = sz;   r.m[12] = tx;
  r.m[1] = vx;   r.m[5] = vy;   r.m[9] = vz;   r.m[13] = ty;
  r.m[2] = -fx;  r.m[6] = -fy;  r.m[10] = -fz; r.m[14] = tz;
  return r;
}

bool Mat4::Inverse(Mat4* out) const {
  const float* a = m;
  float inv[16];
  inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
  inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
  inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
  inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
  inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
  inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
  inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
  inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
  inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
  inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
  inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
  inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
  inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
  inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
  inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
  inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];

  float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
  if (std::fabs(det) < 1e-20f) return false;
  det = 1.0f / det;
  for (int i = 0; i < 16; ++i) out->m[i] = inv[i] * det;
  return true;
}

void ClusterDepthRange(const ClusterGridConfig& grid, uint32_t z,
                       float* z0, float* z1) {
  const float logRatio = std::log(grid.zFar / grid.zNear);
  *z0 = grid.zNear * std::exp(static_cast<float>(z) * logRatio /
                              static_cast<float>(grid.gridSizeZ));
  *z1 = grid.zNear * std::exp(static_cast<float>(z + 1) * logRatio /
                              static_cast<float>(grid.gridSizeZ));
}

void PrintDepthSlices(const ClusterGridConfig& grid) {
  std::printf("  depth slices: ");
  for (uint32_t z = 0; z < grid.gridSizeZ; z += 4) {
    float a = 0.0f, b = 0.0f;
    ClusterDepthRange(grid, z, &a, &b);
    std::printf("[%u]=%.3f ", z, a);
  }
  float last0 = 0.0f, last1 = 0.0f;
  ClusterDepthRange(grid, grid.gridSizeZ - 1, &last0, &last1);
  std::printf("[last]=%.3f far=%.3f\n", last0, static_cast<double>(grid.zFar));
}

void BuildClusterAABBs(const Mat4& invProjection, float screenW, float screenH,
                       const ClusterGridConfig& grid,
                       const std::function<void(uint32_t, uint32_t, uint32_t,
                                                const ClusterAABB&)>& fn) {
  (void)screenW;
  (void)screenH;
  const float invGridX = 1.0f / static_cast<float>(grid.gridSizeX);
  const float invGridY = 1.0f / static_cast<float>(grid.gridSizeY);

  // Iteration order MUST be z-outermost, then y, then x, because the index
  // formula used by both the cull and resolve passes is
  //     idx = x + y * gridX + z * gridX * gridY
  // which makes z the slowest-varying dimension. Looping y/x outermost (as the
  // draft did) emits clusters in a different order, so the cluster a pixel
  // looks up is not the one that contains it. The result is wrong lighting
  // with no GL error of any kind.
  for (uint32_t z = 0; z < grid.gridSizeZ; ++z) {
    float z0 = 0.0f, z1 = 0.0f;
    ClusterDepthRange(grid, z, &z0, &z1);

    for (uint32_t y = 0; y < grid.gridSizeY; ++y) {
      for (uint32_t x = 0; x < grid.gridSizeX; ++x) {
      const float ndcMinX = static_cast<float>(x) * invGridX * 2.0f - 1.0f;
      const float ndcMaxX = static_cast<float>(x + 1) * invGridX * 2.0f - 1.0f;
      const float ndcMinY = static_cast<float>(y) * invGridY * 2.0f - 1.0f;
      const float ndcMaxY = static_cast<float>(y + 1) * invGridY * 2.0f - 1.0f;

      auto unproject = [&](float ndcX, float ndcY, float ndcZ) {
        const float* m = invProjection.Data();
        const float cw = m[3] * ndcX + m[7] * ndcY + m[11] * ndcZ + m[15];
        const float cx = m[0] * ndcX + m[4] * ndcY + m[8]  * ndcZ + m[12];
        const float cy = m[1] * ndcX + m[5] * ndcY + m[9]  * ndcZ + m[13];
        const float cz = m[2] * ndcX + m[6] * ndcY + m[10] * ndcZ + m[14];
        const float iw = (std::fabs(cw) < 1e-20f) ? 0.0f : 1.0f / cw;
        return Vec3{cx * iw, cy * iw, cz * iw};
      };

      const Vec3 camMin = unproject(ndcMinX, ndcMinY, -1.0f);
      const Vec3 camMax = unproject(ndcMaxX, ndcMaxY, -1.0f);
      // unproject(..., -1) returns a point with z = -zNear, so scaling it by
      // d / zNear lands on the plane z = -d. Dividing by camMin.z directly
      // would be NEGATIVE and mirror the wedge through the origin.
      const float kMinScale = 1.0f / grid.zNear;
      const float kMaxScale = 1.0f / grid.zNear;

      {
        // Four corners: both tile edges x both depth planes. The draft used
        // only two, which under-approximated the trapezoid.
        //
        // camMin/camMax sit on the near plane (z = -zNear), so scaling by
        // d / zNear lands each on its own depth plane with z = -d.
        const Vec3 c00n{camMin.x * z0 * kMinScale, camMin.y * z0 * kMinScale, -z0};
        const Vec3 c10n{camMax.x * z0 * kMaxScale, camMax.y * z0 * kMaxScale, -z0};
        const Vec3 c00f{camMin.x * z1 * kMinScale, camMin.y * z1 * kMinScale, -z1};
        const Vec3 c10f{camMax.x * z1 * kMaxScale, camMax.y * z1 * kMaxScale, -z1};

        // A cluster only needs the extents of its own frustum wedge, so a
        // sentinel is used as the min/max seed and then clipped. The draft
        // seeded min with -1e20 and never recovered, so a tile whose wedge
        // does not extend in some axis kept the sentinel and the AABB covered
        // the entire scene. Seeding from the first corner avoids that.
        Vec3 lo = c00n;
        Vec3 hi = c00n;
        const Vec3 pts[4] = {c00n, c10n, c00f, c10f};
        for (int i = 1; i < 4; ++i) {
          lo.x = std::fmin(lo.x, pts[i].x); hi.x = std::fmax(hi.x, pts[i].x);
          lo.y = std::fmin(lo.y, pts[i].y); hi.y = std::fmax(hi.y, pts[i].y);
          lo.z = std::fmin(lo.z, pts[i].z); hi.z = std::fmax(hi.z, pts[i].z);
        }
        // Clip at the near plane; view space looks down -Z.
        lo.z = std::fmax(lo.z, -z1);
        hi.z = std::fmax(hi.z, -z0);

        ClusterAABB aabb;
        aabb.minPoint = {lo.x, lo.y, lo.z, 0.0f};
        aabb.maxPoint = {hi.x, hi.y, hi.z, 0.0f};
        fn(x, y, z, aabb);
      }
    }
    }
  }
}

bool LightAffectsCluster(const ClusterAABB& c, Vec3 lightViewPos, float range) {
  const Vec3 lo{c.minPoint.x, c.minPoint.y, c.minPoint.z};
  const Vec3 hi{c.maxPoint.x, c.maxPoint.y, c.maxPoint.z};

  // Depth-slab prefilter, as in kCullLightsComp. Sound because the cluster's
  // z extent is exactly [minPoint.z, maxPoint.z] = [-z1, -z0].
  if (lightViewPos.z + range < lo.z || lightViewPos.z - range > hi.z) return false;

  // Arvo sphere/AABB: squared distance from the centre to the closest point.
  const float dx = std::fmax(std::fmax(lo.x - lightViewPos.x, 0.0f),
                             lightViewPos.x - hi.x);
  const float dy = std::fmax(std::fmax(lo.y - lightViewPos.y, 0.0f),
                             lightViewPos.y - hi.y);
  const float dz = std::fmax(std::fmax(lo.z - lightViewPos.z, 0.0f),
                             lightViewPos.z - hi.z);
  return dx * dx + dy * dy + dz * dz <= range * range;
}

uint32_t ClusterIndexForPixel(float fragX, float fragY, float screenW, float screenH,
                             const ClusterGridConfig& grid, float viewZ) {
  // Mirrors kShadeFrag exactly, INCLUDING its use of a computed float tile size.
  const float tileW = screenW / static_cast<float>(grid.gridSizeX);
  const float tileH = screenH / static_cast<float>(grid.gridSizeY);
  const uint32_t tx = static_cast<uint32_t>(fragX / tileW);
  const uint32_t ty = static_cast<uint32_t>(fragY / tileH);

  const float slice = std::log(-viewZ / grid.zNear) /
                      std::log(grid.zFar / grid.zNear) *
                      static_cast<float>(grid.gridSizeZ);
  uint32_t z = static_cast<uint32_t>(
      std::floor(slice));
  if (z >= grid.gridSizeZ) z = grid.gridSizeZ - 1;

  return tx + ty * grid.gridSizeX + z * grid.gridSizeX * grid.gridSizeY;
}

bool ClusterFacesAwayFrom(const NormalCone& cone, Vec3 toLight) {
  // An unknown cone (empty cluster) must never cull anything.
  if (cone.cutoff >= 1.0f) return false;

  const float len = std::sqrt(toLight.x * toLight.x + toLight.y * toLight.y +
                              toLight.z * toLight.z);
  if (len < 1e-6f) return false;

  // toLight points from the cluster toward the light. If even the cone's
  // best-aligned direction is behind the light, every normal in the cluster
  // has N.L < 0 and the light cannot contribute.
  const float cosAngle = (toLight.x * cone.axis.x + toLight.y * cone.axis.y +
                          toLight.z * cone.axis.z) / len;
  return cosAngle < -cone.cutoff;
}

float SpotConeAttenuation(Vec3 L, Vec3 lightDir, float cosOuter, float cosInner) {
  // L points FROM the surface TOWARD the light (the same L the shading loop
  // already computes), so the direction from the light to the surface is -L.
  // Using L directly here would light the space behind the spot instead of in
  // front of it.
  const float len = std::sqrt(L.x * L.x + L.y * L.y + L.z * L.z);
  if (len < 1e-6f) return 0.0f;

  const float cosAngle = -(L.x * lightDir.x + L.y * lightDir.y + L.z * lightDir.z) / len;

  if (cosAngle <= cosOuter) return 0.0f;              // outside the cone
  if (cosAngle >= cosInner) return 1.0f;              // inside the hot core

  // Smooth blend between the two angles.
  const float span = cosInner - cosOuter;
  if (span <= 1e-6f) return 1.0f;
  return (cosAngle - cosOuter) / span;
}

bool PointInCluster(const ClusterAABB& c, Vec3 p, float slack) {
  return p.x >= c.minPoint.x - slack && p.x <= c.maxPoint.x + slack &&
         p.y >= c.minPoint.y - slack && p.y <= c.maxPoint.y + slack &&
         p.z >= c.minPoint.z - slack && p.z <= c.maxPoint.z + slack;
}

}  // namespace lab

#include "SoALights.h"
#include <cmath>

namespace lab {

// Every kSpotStride-th generated light is a spot rather than a point.
static constexpr uint32_t kSpotStride = 4;

void SoALights::GenerateOrbit(uint32_t count, float timeSeconds, float radius) {
  count_ = count;
  positionRange_.resize(count);
  viewPosRange_.resize(count);
  colorIntensity_.resize(count);
  spotDirOuter_.resize(count);
  viewSpotDirOuter_.resize(count);
  spotInnerCos_.resize(count);

  const float golden = 2.39996323f;  // golden angle, radians
  for (uint32_t i = 0; i < count; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(count);
    const float phi = golden * static_cast<float>(i) + timeSeconds * 0.6f;
    const float theta = std::acos(1.0f - 2.0f * t);
    const float s = 0.45f + 0.55f * t;  // shell radius factor

    const Vec3 p{
        radius * s * std::sin(theta) * std::cos(phi),
        radius * s * std::cos(theta) * 0.7f,
        radius * s * std::sin(theta) * std::sin(phi),
    };

    // Per-light range varies so cluster occupancy is realistically uneven.
    const float range = 3.0f + 6.0f * ((i * 2654435761u) % 1000u) / 1000.0f;
    positionRange_[i] = {p.x, p.y, p.z, range};

    const float h = (static_cast<float>(i) * 0.6180339887f) * 6.2831853f;
    const float hue = h - std::floor(h / 6.2831853f) * 6.2831853f;
    colorIntensity_[i] = {0.5f + 0.5f * std::cos(hue),
                          0.5f + 0.5f * std::cos(hue + 2.0943951f),
                          0.5f + 0.5f * std::cos(hue + 4.1887902f),
                          40.0f};

    // Every kSpotStride-th light is a spot aimed at the origin; the rest are
    // point lights. A point light is encoded as cos(inner) == cos(outer) == -1,
    // which makes the cone test pass unconditionally, so one shading path
    // handles both without a branch on a type flag.
    if (i % kSpotStride == 0) {
      const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
      const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
      const float inner = 0.955f;  // ~17 deg half angle
      const float outer = 0.87f;   // ~29 deg half angle
      spotDirOuter_[i] = {-p.x * inv, -p.y * inv, -p.z * inv, outer};
      spotInnerCos_[i] = {0.0f, 0.0f, 0.0f, inner};
    } else {
      spotDirOuter_[i] = {0.0f, -1.0f, 0.0f, -1.0f};
      spotInnerCos_[i] = {0.0f, 0.0f, 0.0f, -1.0f};
    }
  }
}

void SoALights::UpdateViewSpace(const float* m) {
  for (uint32_t i = 0; i < count_; ++i) {
    const Vec4& p = positionRange_[i];
    const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    viewPosRange_[i] = {x, y, z, p.w};

    // Directions transform by the rotation part only: the translation must NOT
    // be applied, or the cone would rotate about the wrong point.
    const Vec4& d = spotDirOuter_[i];
    const float dx = m[0] * d.x + m[4] * d.y + m[8]  * d.z;
    const float dy = m[1] * d.x + m[5] * d.y + m[9]  * d.z;
    const float dz = m[2] * d.x + m[6] * d.y + m[10] * d.z;
    const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float inv = dl > 1e-6f ? 1.0f / dl : 0.0f;
    viewSpotDirOuter_[i] = {dx * inv, dy * inv, dz * inv, d.w};
  }
}

size_t SoALights::GlobalIndexCapacity(const ClusterGridConfig& grid) const {
  return static_cast<size_t>(grid.numClusters()) * 256u;  // hard cap per cluster
}

}  // namespace lab

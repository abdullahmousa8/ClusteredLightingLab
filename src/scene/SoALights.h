// GPU-driven SoA light database. Mirrors the layout described in the paper:
// separate parallel arrays so the culling pass reads only position+range.
#pragma once
#include "../gl/gl_loader.h"
#include "../renderer/ClusterMath.h"

namespace lab {

class SoALights {
 public:
  // SoA arrays uploaded verbatim to SSBOs. Kept as separate parallel arrays so
  // the culling pass reads only ViewPositionRange, exactly as the paper argues:
  // the cull test needs position and range and nothing else, so every other
  // property stays out of that cache line.
  const std::vector<Vec4>& PositionRange() const { return positionRange_; }  // xyz + range
  const std::vector<Vec4>& ViewPositionRange() const { return viewPosRange_; }// view-space xyz + range
  const std::vector<Vec4>& ColorIntensity() const { return colorIntensity_; }// rgb + intensity

  // Spot cone. xyz is the view-space direction the light points in; w is
  // cos(outerAngle). An inner angle is packed as w of SpotInnerCos, kept in a
  // separate array so the cull pass never touches it. Point lights are encoded
  // as a cone with cos(inner) == cos(outer) == -1, which always passes the cone
  // test, so one code path serves both.
  const std::vector<Vec4>& ViewSpotDirOuter() const { return viewSpotDirOuter_; }
  const std::vector<Vec4>& SpotInnerCos() const { return spotInnerCos_; }

  // Builds N lights: every kSpotFraction-th is a spot, the rest are points.
  void GenerateOrbit(uint32_t count, float timeSeconds, float radius);
  uint32_t Count() const { return count_; }

  // Recomputes the view-space derived arrays from world state + view matrix.
  void UpdateViewSpace(const float* viewMatrixColumnMajor);

  // Total GPU bytes needed for the worst-case global index list.
  size_t GlobalIndexCapacity(const ClusterGridConfig& grid) const;
 private:
  uint32_t count_ = 0;
  std::vector<Vec4> positionRange_;
  std::vector<Vec4> viewPosRange_;
  std::vector<Vec4> colorIntensity_;
  std::vector<Vec4> spotDirOuter_;   // world direction xyz + cos(outer)
  std::vector<Vec4> viewSpotDirOuter_;  // view-space direction xyz + cos(outer)
  std::vector<Vec4> spotInnerCos_;   // cos(inner) in w
};

}  // namespace lab

// Input views: calibrated cameras with their frame sources, and choosing the pair to interpolate from.
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "camera.h"
#include "config.h"
#include "frame_source.h"

namespace vi {

struct InputView {
  Camera cam;            ///< intrinsics scaled to the processing resolution
  glm::mat4 projection;  ///< cam.projection()
  std::unique_ptr<BufferedSource> source;
};

struct InputSet {
  std::vector<InputView> views;
  long loop_length = 0;  ///< frames per playback loop, <= 1 when all inputs are still images
  double fps = 25;       ///< playback rate
};

/// Opens all views of the config. Videos agree on a common loop length (the shortest one, or <frames>),
/// so that all streams stay frame-synchronous.
InputSet openInputViews(const AppConfig& config);

/// Chooses the two input views to interpolate from: among views facing at most max_angle_deg away from the
/// virtual camera, the pair whose connecting segment passes closest to the virtual camera centre (plus a
/// small penalty for wide baselines). Keeps `previous` unless the new pair is clearly better.
std::pair<int, int> selectViewPair(const std::vector<InputView>& views, const glm::vec3& center,
                                   const glm::vec3& direction, float max_angle_deg, std::pair<int, int> previous);

}  // namespace vi

// Input views: calibrated cameras with their frame sources.
#pragma once

#include <memory>
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

}  // namespace vi

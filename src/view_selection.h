// Choosing the two input cameras to interpolate a virtual view from.
#pragma once

#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "camera.h"

namespace vi {

/// Chooses the two input cameras to interpolate a virtual view from (indices into `cameras`, first < second).
///
/// The choice is made by viewing directions onto the scene, seen from the scene point the virtual camera
/// looks at (`center` + `scene_depth` along `direction`). A pair is preferred when the virtual viewing
/// direction lies *between* the directions of the two cameras, so the view is interpolated rather than
/// extrapolated; among such pairs, the smallest angular deviation from the pair plus 0.25 x the angle between
/// the two cameras wins. This works for parallel rigs and for cameras converging on a scene, and moving
/// straight towards the scene does not change the choice. Pairs not containing the virtual view are used
/// only when no pair does.
///
/// Only cameras facing at most `max_angle_deg` away from `direction` are considered (all cameras if fewer
/// than two do). `previous` (or {-1, -1}) is kept while it is still about as good, to avoid flickering.
std::pair<int, int> selectViewPair(const std::vector<Camera>& cameras, const glm::vec3& center,
                                   const glm::vec3& direction, float scene_depth, float max_angle_deg,
                                   std::pair<int, int> previous);

}  // namespace vi

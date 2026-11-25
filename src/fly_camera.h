// Free-fly navigation of the virtual camera.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

struct GLFWwindow;

namespace vi {

/// Mouse/keyboard navigation of the virtual camera.
///  * The mouse is captured (hidden, unlimited motion; works on Wayland) and turns the view directly.
///    Tab releases / recaptures it, a click into the window recaptures it.
///  * W/A/S/D or arrows move, Space moves up, the right mouse button moves down.
/// The returned view matrix is the identity until the user moves; it is applied in the virtual camera's frame.
class FlyCamera {
 public:
  /// capture_mouse = false leaves the cursor alone (e.g. for headless snapshots).
  FlyCamera(GLFWwindow* window, bool capture_mouse);

  /// Processes input since the last call and returns the current view matrix.
  glm::mat4 update(GLFWwindow* window, float speed, float mouse_speed);

 private:
  void setCaptured(GLFWwindow* window, bool captured);

  glm::vec3 position_{0.f};
  float yaw_ = glm::pi<float>();  // looking along -Z in OpenGL terms
  float pitch_ = 0.f;
  double last_time_ = -1;
  bool captured_ = false;
  bool resync_ = true;    // ignore the next cursor movement (it may be relative to a stale position)
  bool focused_ = false;  // window focus in the previous update
  bool tab_was_down_ = false;
  double last_x_ = 0;
  double last_y_ = 0;
};

}  // namespace vi

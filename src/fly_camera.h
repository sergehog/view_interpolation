// Free-fly navigation of the virtual camera.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

struct GLFWwindow;

namespace vi {

/// Mouse/keyboard navigation: hold the left mouse button and drag to look around (the cursor is captured
/// during the drag, which also works on Wayland), W/A/S/D or arrows to move, Space up, right button down.
/// The returned view matrix is the identity until the user moves; it is applied in the virtual camera's frame.
class FlyCamera {
 public:
  /// Processes input since the last call and returns the current view matrix.
  glm::mat4 update(GLFWwindow* window, float speed, float mouse_speed);

 private:
  glm::vec3 position_{0.f};
  float yaw_ = glm::pi<float>();  // looking along -Z in OpenGL terms
  float pitch_ = 0.f;
  double last_time_ = -1;
  bool dragging_ = false;
  double last_x_ = 0;
  double last_y_ = 0;
};

}  // namespace vi

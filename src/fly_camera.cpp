#include "fly_camera.h"

#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace vi {

FlyCamera::FlyCamera(GLFWwindow* window, bool capture_mouse) {
  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);  // unaccelerated motion while captured
  }
  if (capture_mouse) {
    setCaptured(window, true);
  }
}

void FlyCamera::setCaptured(GLFWwindow* window, bool captured) {
  glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  glfwGetCursorPos(window, &last_x_, &last_y_);
  captured_ = captured;
  resync_ = true;
}

glm::mat4 FlyCamera::update(GLFWwindow* window, float speed, float mouse_speed) {
  const double now = glfwGetTime();
  if (last_time_ < 0) {
    last_time_ = now;
  }
  const float dt = float(now - last_time_);
  last_time_ = now;

  // Tab toggles mouse capture, a left click recaptures
  const bool tab_down = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
  if (tab_down && !tab_was_down_) {
    setCaptured(window, !captured_);
  }
  tab_was_down_ = tab_down;
  if (!captured_ && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    setCaptured(window, true);
  }

  // Mouse look while captured. After capturing (and after regaining focus) GLFW may report the first
  // movement relative to a stale position - where the cursor was, or (0, 0) if it never moved over the
  // window - a frame or two later. The first change of position after that only re-synchronises.
  const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
  if (focused && !focused_) {
    resync_ = true;
  }
  focused_ = focused;
  double x, y;
  glfwGetCursorPos(window, &x, &y);
  const bool moved = x != last_x_ || y != last_y_;
  if (captured_ && moved) {
    if (resync_) {
      resync_ = false;
    } else {
      yaw_ += mouse_speed * float(last_x_ - x);
      pitch_ -= mouse_speed * float(last_y_ - y);
    }
  }
  last_x_ = x;
  last_y_ = y;

  const glm::vec3 direction(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_));
  const glm::vec3 right(std::sin(yaw_ - glm::half_pi<float>()), 0, std::cos(yaw_ - glm::half_pi<float>()));
  const glm::vec3 up = glm::cross(right, direction);

  const auto key = [window](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
  const float step = dt * speed;
  if (key(GLFW_KEY_UP) || key(GLFW_KEY_W)) {
    position_ -= direction * step;
  }
  if (key(GLFW_KEY_DOWN) || key(GLFW_KEY_S)) {
    position_ += direction * step;
  }
  if (key(GLFW_KEY_RIGHT) || key(GLFW_KEY_D)) {
    position_ += right * step;
  }
  if (key(GLFW_KEY_LEFT) || key(GLFW_KEY_A)) {
    position_ -= right * step;
  }
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
    position_ -= up * step;
  }
  if (key(GLFW_KEY_SPACE)) {
    position_ += up * step;
  }

  return glm::lookAt(position_, position_ + direction, up);
}

}  // namespace vi

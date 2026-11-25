#include "fly_camera.h"

#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace vi {

glm::mat4 FlyCamera::update(GLFWwindow* window, float speed, float mouse_speed) {
  const double now = glfwGetTime();
  if (last_time_ < 0) {
    last_time_ = now;
  }
  const float dt = float(now - last_time_);
  last_time_ = now;

  // drag-to-look with the left mouse button
  double x, y;
  glfwGetCursorPos(window, &x, &y);
  const bool pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  if (pressed && !dragging_) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(window, &x, &y);
    last_x_ = x;  // no jump when the drag starts
    last_y_ = y;
    dragging_ = true;
  } else if (!pressed && dragging_) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    dragging_ = false;
  }
  if (dragging_) {
    yaw_ += mouse_speed * float(last_x_ - x);
    pitch_ -= mouse_speed * float(last_y_ - y);
  }
  last_x_ = x;
  last_y_ = y;

  const glm::vec3 direction(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_));
  const glm::vec3 right(std::sin(yaw_ - glm::half_pi<float>()), 0, std::cos(yaw_ - glm::half_pi<float>()));
  const glm::vec3 up = glm::cross(right, direction);

  const auto key = [window](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
  const float step = dt * speed;
  if (key(GLFW_KEY_UP) || key(GLFW_KEY_W)) {
    position_ += direction * step;
  }
  if (key(GLFW_KEY_DOWN) || key(GLFW_KEY_S)) {
    position_ -= direction * step;
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

// view_interpolation: real-time plane-sweep view interpolation from calibrated images and videos.
//
// usage: view_interpolation config.xml [--snapshot out.png [--frame N]]
//   --snapshot  render the initial virtual view of input frame N (default 0), save it as PNG and exit

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <lodepng.h>

#include "config.h"
#include "fly_camera.h"
#include "input_views.h"
#include "plane_sweep_renderer.h"
#include "view_selection.h"

namespace {

struct Options {
  std::string config_file;
  std::string snapshot_file;  ///< empty: interactive
  long snapshot_frame = 0;
};

bool parseOptions(int argc, char* argv[], Options& opt) {
  if (argc < 2) {
    return false;
  }
  opt.config_file = argv[1];
  for (int i = 2; i < argc; i++) {
    if (!std::strcmp(argv[i], "--snapshot") && i + 1 < argc) {
      opt.snapshot_file = argv[++i];
    } else if (!std::strcmp(argv[i], "--frame") && i + 1 < argc) {
      opt.snapshot_frame = std::stol(argv[++i]);
    } else {
      return false;
    }
  }
  return true;
}

/// GLFW window with an OpenGL 3.3 core context; terminates GLFW when destroyed.
class GlWindow {
 public:
  GlWindow(int width, int height, const char* title) {
    if (!glfwInit()) {
      throw std::runtime_error("Cannot initialize GLFW");
    }
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // the layer texture array is allocated for one size
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!handle_) {
      glfwTerminate();
      throw std::runtime_error("Cannot open an OpenGL 3.3 window");
    }
    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(-1);  // adaptive vsync where supported
    glfwSetInputMode(handle_, GLFW_STICKY_KEYS, GLFW_TRUE);

    glewExperimental = GL_TRUE;  // needed for core profiles
    if (glewInit() != GLEW_OK) {
      glfwTerminate();
      throw std::runtime_error("Cannot initialize GLEW");
    }
  }
  ~GlWindow() { glfwTerminate(); }

  GlWindow(const GlWindow&) = delete;
  GlWindow& operator=(const GlWindow&) = delete;

  GLFWwindow* get() const { return handle_; }
  bool shouldClose() const {
    return glfwWindowShouldClose(handle_) || glfwGetKey(handle_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  }
  /// Framebuffer size in pixels (larger than the window size on HiDPI displays).
  void framebufferSize(int& w, int& h) const { glfwGetFramebufferSize(handle_, &w, &h); }

 private:
  GLFWwindow* handle_ = nullptr;
};

void reportGlErrors() {
  for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
    printf("OpenGL error 0x%04x\n", err);
  }
}

int run(const Options& opt) {
  using namespace vi;
  const bool snapshot = !opt.snapshot_file.empty();
  const AppConfig config = loadConfig(opt.config_file);

  // Virtual camera CV = K4 * E. The free-fly motion V is applied in the camera's own frame:
  // (u*z, v*z, z, 1) of the moved virtual camera -> world = E^-1 * V * K4^-1.
  const Camera virtual_cam =
      findCamera(config.desired_camera_file, config.desired_camera).scaledTo(config.width, config.height);
  const glm::mat4 k4_inv = glm::inverse(glm::mat4(virtual_cam.intrinsics));
  const glm::mat4 e_inv = glm::inverse(glm::mat4(virtual_cam.extrinsics()));
  const glm::vec4 principal_ray(virtual_cam.intrinsics[2][0], virtual_cam.intrinsics[2][1], 1,
                                0);  // (cx, cy, 1) at infinity

  InputSet inputs = openInputViews(config);
  std::vector<InputView>& views = inputs.views;
  // typical scene depth for view selection: the middle of the depth range in inverse depth
  const float scene_depth = 2.f / (1.f / config.min_z + 1.f / config.max_z);
  std::vector<Camera> cameras;
  for (const InputView& v : views) {
    cameras.push_back(v.cam);
  }
  if (snapshot && inputs.loop_length > 1) {
    for (long f = 0; f < opt.snapshot_frame % inputs.loop_length; f++) {
      for (InputView& v : views) {
        v.source->advance();
      }
    }
  }

  GlWindow window(config.output_width, config.output_height, "View interpolation");
  int fb_width, fb_height;
  window.framebufferSize(fb_width, fb_height);
  PlaneSweepRenderer renderer(config, fb_width, fb_height);
  FlyCamera navigation(window.get(), !snapshot);

  // playback: input frames advance at inputs.fps, independently of the rendering rate
  const double frame_period = 1.0 / inputs.fps;
  double next_frame_time = glfwGetTime() + frame_period;
  long video_frame = 0;

  std::pair<int, int> pair(-1, -1);  // views bound to the two input slots
  int uploaded_view[2] = {-1, -1};   // what each slot currently holds
  unsigned long uploaded_serial[2] = {0, 0};

  double fps_time = glfwGetTime();
  unsigned fps_frames = 0;
  unsigned long rendered_frames = 0;

  while (!window.shouldClose()) {
    const double now = glfwGetTime();
    if (!snapshot && inputs.loop_length > 1 && now >= next_frame_time) {
      for (InputView& v : views) {
        v.source->advance();
      }
      video_frame = (video_frame + 1) % inputs.loop_length;
      next_frame_time += frame_period;
      if (now > next_frame_time)  // decoding can't keep up: slow down instead of skipping
      {
        next_frame_time = now + frame_period;
      }
    }

    const glm::mat4 view_matrix = navigation.update(window.get(), config.speed, config.mouse_speed);
    const glm::mat4 c_inv = e_inv * view_matrix * k4_inv;

    const glm::vec3 center(c_inv * glm::vec4(0, 0, 0, 1));
    const glm::vec3 direction = glm::normalize(glm::vec3(c_inv * principal_ray));
    const std::pair<int, int> best =
        selectViewPair(cameras, center, direction, scene_depth, config.max_view_angle, pair);
    if (best != pair) {
      pair = best;
      printf("Using views %s + %s\n", views[pair.first].cam.name.c_str(), views[pair.second].cam.name.c_str());
    }

    const int bound[2] = {pair.first, pair.second};
    for (int slot = 0; slot < 2; slot++)  // upload only when the view or its frame changed
    {
      const BufferedSource& src = *views[bound[slot]].source;
      if (uploaded_view[slot] != bound[slot] || uploaded_serial[slot] != src.serial()) {
        renderer.uploadInput(slot, src.current());
        uploaded_view[slot] = bound[slot];
        uploaded_serial[slot] = src.serial();
      }
    }

    renderer.render(views[pair.first].projection * c_inv, views[pair.second].projection * c_inv);

    if (++rendered_frames == 1) {
      reportGlErrors();
    }
    if (snapshot && rendered_frames >= 3)  // a few warm-up frames, then save and quit
    {
      const unsigned err = lodepng::encode(opt.snapshot_file, renderer.readBackBuffer(), fb_width, fb_height, LCT_RGB);
      if (err) {
        throw std::runtime_error("Cannot write " + opt.snapshot_file + ": " + lodepng_error_text(err));
      }
      printf("Snapshot saved to %s\n", opt.snapshot_file.c_str());
      break;
    }

    glfwSwapBuffers(window.get());
    glfwPollEvents();

    fps_frames++;
    if (now - fps_time >= 1.0) {
      printf("frame %ld (views %s + %s): %.1f FPS\n", video_frame, views[pair.first].cam.name.c_str(),
             views[pair.second].cam.name.c_str(), fps_frames / (now - fps_time));
      fps_frames = 0;
      fps_time = now;
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options opt;
  if (!parseOptions(argc, argv, opt)) {
    printf("usage: %s config.xml [--snapshot out.png [--frame N]]\n", argv[0]);
    return 1;
  }
  try {
    return run(opt);
  } catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 2;
  }
}

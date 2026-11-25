#include "input_views.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace vi {

InputSet openInputViews(const AppConfig& config) {
  const int w = config.width, h = config.height;
  InputSet result;
  result.fps = config.fps;

  // open all decoders first, to agree on a common loop length
  std::vector<std::unique_ptr<FrameDecoder>> decoders;
  long loop_length = 0;
  double video_fps = 0;
  for (const ViewSource& v : config.views) {
    decoders.push_back(openDecoder(v.file, w, h));
    const long count = decoders.back()->frameCount();
    printf("Input %s: camera %s, %ld frame(s), %.2f fps\n", v.file.c_str(), v.camera.c_str(), count,
           decoders.back()->fps());
    if (count > 1) {
      loop_length = loop_length == 0 ? count : std::min(loop_length, count);
    }
    if (video_fps <= 0) {
      video_fps = decoders.back()->fps();
    }
  }
  if (config.frames > 0 && (loop_length == 0 || long(config.frames) < loop_length)) {
    loop_length = config.frames;
  }
  if (result.fps <= 0) {
    result.fps = video_fps > 0 ? video_fps : 25;
  }
  result.loop_length = loop_length;
  if (loop_length > 1) {
    printf("Playing %ld frames in a loop at %.2f fps\n", loop_length, result.fps);
  } else {
    printf("All inputs are still images\n");
  }

  for (size_t i = 0; i < config.views.size(); i++) {
    InputView view;
    view.cam = findCamera(config.camera_file, config.views[i].camera).scaledTo(w, h);
    view.projection = view.cam.projection();
    std::unique_ptr<UndistortMap> undistort;
    if (view.cam.hasDistortion()) {
      undistort = std::make_unique<UndistortMap>(view.cam, w, h);
    }
    view.source = std::make_unique<BufferedSource>(std::move(decoders[i]), w, h, std::move(undistort), loop_length,
                                                   config.buffer_frames);
    result.views.push_back(std::move(view));
  }
  return result;
}

std::pair<int, int> selectViewPair(const std::vector<InputView>& views, const glm::vec3& center,
                                   const glm::vec3& direction, float max_angle_deg, std::pair<int, int> previous) {
  const int n = int(views.size());
  if (n == 2) {
    return {0, 1};
  }

  const float min_cos = std::cos(glm::radians(max_angle_deg));
  std::vector<int> candidates;
  for (int i = 0; i < n; i++) {
    if (glm::dot(views[i].cam.forward(), direction) >= min_cos) {
      candidates.push_back(i);
    }
  }
  if (candidates.size() < 2)  // nothing faces the right way: consider all views
  {
    candidates.clear();
    for (int i = 0; i < n; i++) {
      candidates.push_back(i);
    }
  }

  const auto score = [&](int a, int b) {
    const glm::vec3 ca = views[a].cam.center(), cb = views[b].cam.center();
    const glm::vec3 ab = cb - ca;
    const float s = glm::clamp(glm::dot(center - ca, ab) / glm::dot(ab, ab), 0.f, 1.f);
    return glm::length(center - (ca + s * ab)) + 0.25f * glm::length(ab);
  };

  std::pair<int, int> best(candidates[0], candidates[1]);
  float best_score = std::numeric_limits<float>::max();
  for (size_t i = 0; i < candidates.size(); i++) {
    for (size_t j = i + 1; j < candidates.size(); j++) {
      const float s = score(candidates[i], candidates[j]);
      if (s < best_score) {
        best_score = s;
        best = {candidates[i], candidates[j]};
      }
    }
  }

  // hysteresis against flickering between two similar pairs
  if (previous.first >= 0 && previous != best && score(previous.first, previous.second) <= best_score * 1.05f) {
    return previous;
  }
  return best;
}

}  // namespace vi

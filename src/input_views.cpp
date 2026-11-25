#include "input_views.h"

#include <algorithm>
#include <cstdio>

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

}  // namespace vi

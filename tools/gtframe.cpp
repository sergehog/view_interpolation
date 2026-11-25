// Ground-truth frame for snapshot tests: decodes frame N of a video, undistorts it with the camera's
// calibration (scaled to W x H) exactly like the renderer's input path, and saves it as RGB PNG.
// usage: gtframe calibration video camera frame W H out.png
#include <cstdio>
#include <cstdlib>

#include "frame_source.h"
#include "lodepng.h"

int main(int argc, char** argv) {
  if (argc != 8) {
    fprintf(stderr, "usage: %s calibration video camera frame W H out.png\n", argv[0]);
    return 1;
  }
  try {
    const int width = atoi(argv[5]), height = atoi(argv[6]);
    const long frame = atol(argv[4]);
    const vi::Camera cam = vi::findCamera(argv[1], argv[3]).scaledTo(width, height);
    std::unique_ptr<vi::UndistortMap> undistort;
    if (cam.hasDistortion()) undistort = std::make_unique<vi::UndistortMap>(cam, width, height);
    vi::BufferedSource src(vi::openDecoder(argv[2], width, height), width, height, std::move(undistort), 0);
    for (long i = 0; i < frame; i++) src.advance();

    std::vector<unsigned char> rgb(size_t(width) * height * 3);
    const unsigned char* bgr = src.current();
    for (size_t i = 0; i < size_t(width) * height; i++) {
      rgb[i * 3] = bgr[i * 3 + 2];
      rgb[i * 3 + 1] = bgr[i * 3 + 1];
      rgb[i * 3 + 2] = bgr[i * 3];
    }
    const unsigned err = lodepng::encode(argv[7], rgb, width, height, LCT_RGB);
    if (err) fprintf(stderr, "cannot write %s: %s\n", argv[7], lodepng_error_text(err));
    return err ? 1 : 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}

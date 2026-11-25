// Ground-truth frame for snapshot tests: decodes frame N of a video, undistorts it with the camera's
// calibration (scaled to W x H) exactly like the renderer's input path, and saves it as RGB PNG.
// usage: gtframe calibration video camera frame W H out.png
#include "common/frame_source.h"
#include "lodepng.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
	if (argc != 8)
	{
		fprintf(stderr, "usage: %s calibration video camera frame W H out.png\n", argv[0]);
		return 1;
	}
	try
	{
		const int W = atoi(argv[5]), H = atoi(argv[6]);
		const long N = atol(argv[4]);
		const config::camera_params cam = config::camera_loader::read_settings(argv[1], argv[3]).scaled_to(W, H);
		std::unique_ptr<vi::undistort_map> undistort;
		if (cam.has_distortion()) undistort = std::make_unique<vi::undistort_map>(cam, W, H);
		vi::buffered_source src(vi::open_decoder(argv[2], W, H), W, H, std::move(undistort), 0);
		for (long i = 0; i < N; i++) src.advance();

		std::vector<unsigned char> rgb(size_t(W) * H * 3);
		const unsigned char *bgr = src.current();
		for (size_t i = 0; i < size_t(W) * H; i++)
		{
			rgb[i * 3] = bgr[i * 3 + 2];
			rgb[i * 3 + 1] = bgr[i * 3 + 1];
			rgb[i * 3 + 2] = bgr[i * 3];
		}
		const unsigned err = lodepng::encode(argv[7], rgb, W, H, LCT_RGB);
		if (err) fprintf(stderr, "cannot write %s: %s\n", argv[7], lodepng_error_text(err));
		return err ? 1 : 0;
	}
	catch (const std::exception &e)
	{
		fprintf(stderr, "error: %s\n", e.what());
		return 1;
	}
}

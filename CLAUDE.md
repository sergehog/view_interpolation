# CLAUDE.md

This file gives Claude Code (claude.ai/code) guidance for working in this repository.

## What this is

Real-time novel-view synthesis (view interpolation) in C++/OpenGL. It is a university project from 2014 (Tampere University of Technology), ported from Visual Studio to CMake/Linux in 2025 and extended to multi-view video. The program takes calibrated colour views (stills or videos) and renders a virtual camera between them. The depth of each pixel is estimated on the GPU each frame by a **plane sweep** between two input views. No depth maps are used as input.

**Division of work:** the shaders do all the per-pixel computation. Everything else runs on the CPU in C++: reading files, decoding, undistortion, buffering, camera maths and choosing which views to use. Prefer changing the C++ side; the shaders are deliberately kept as they are.

## Build & run

Dependencies: OpenGL, GLFW3, GLEW, GLM, Boost headers (PropertyTree), OpenMP and FFmpeg (libavformat, libavcodec, libswscale, libavutil). On Debian/Ubuntu:
`sudo apt install libglfw3-dev libglew-dev libglm-dev libboost-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config`

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/view_interpolation georgiev.config          # run from the repo root
```

- `demo.sh` runs the original still-image demo, `./view_interpolation georgiev.config`. It expects a copy of the binary in the repo root.
- `demo_video1.sh [config] [dataset-dir]` runs the CMU Panoptic video demo. It links the dataset to `data/171204_pose1_sample`, builds Release in `build-release/` and runs `panoptic_pose1.config` (8 views) by default, or `panoptic_pose1_2views.config`. Use a Release build for video, because the Debug build decodes and undistorts slowly.
- **Always run from the repo root.** The shader paths and data paths in configs are relative to the current working directory.
- The program opens a normal, non-resizable window of `output_width`×`output_height` (default: the processing resolution). Press **Esc** to quit. Stdout shows the FPS, the current video frame and the view pair in use.
- Controls (`common/controls.cpp`): **hold the left mouse button and drag** to look around (the cursor is captured during the drag; warping the cursor isn't allowed on Wayland), W/A/S/D or the arrow keys to move, Space to move up, right mouse button to move down. Movement happens in world units per second times `speed` (Georgiev is in metres, Panoptic in cm).
- There are no tests, no linter and no CI. To check a change, build it and run a demo. To test the CPU side without OpenGL, write a small program that uses `vi::open_decoder` / `vi::buffered_source` and `camera_loader`, then projects known 3D points and saves PNGs with `lodepng`. The Panoptic `hdPose3d_stage1_coco19.tar` provides ground-truth joints for this.
- `view_interpolation.vcxproj` is the old Visual Studio project. It is not used or maintained.

## Architecture

**Setup (`view_interpolation.cpp`, `open_views`)**:
- Each view in the config becomes an `input_view`: its camera, with intrinsics scaled to the processing resolution `width`×`height`, its 4×4 projection, and a `vi::buffered_source`.
- All decoders are opened first so that they can agree on one loop length: the smallest frame count among the videos, or the config's `frames` if that is smaller. Every video then rewinds after exactly that many frames, which keeps all streams frame-synchronous.

**Main loop**, run once per rendered frame:
1. **Video clock:** when `1/fps` has passed, call `advance()` on every source, so all views stay on the same frame index. Rendering runs at vsync, independently of the video rate. If decoding falls behind, playback slows down rather than skipping frames.
2. `Cinv = View * inverse(CV)`, where `CV` is the desired virtual camera and `View` comes from the mouse/keyboard (`computeViewMatrix`). The moved virtual camera's centre is `Cinv * (0,0,0,1)`.
3. **View selection (`select_views`)**: among views facing within `max_view_angle` of the virtual camera, pick the pair whose connecting segment passes closest to the virtual centre, plus a 0.25×baseline penalty. A 5% hysteresis stops it flickering between similar pairs. With exactly 2 views, both are always used.
4. Upload the selected pair's current frames to `Texture1`/`Texture2` and generate mipmaps. An upload happens only when the view or its frame changed, so stills are uploaded once. Then set `MVP1 = P_a * Cinv` and `MVP2 = P_b * Cinv`.
5. **Internal pass:** render into an FBO whose colour attachment is a `GL_TEXTURE_2D_ARRAY` with `layers` slices. The program draws one fullscreen quad per depth layer and sets the `layer` uniform for each.
   - `view_internal.vertex`: maps layer index → depth hypothesis `z`, evenly spaced in inverse depth between `minZ` and `maxZ`. It back-projects the pixel at `z` and projects it into both input views.
   - `view_internal.geometry`: sends the triangle to slice `gl_Layer = layer`.
   - `view_internal.fragment`: writes RGB = the mean of the two input colours and A = the photo-consistency cost. The cost is the abs difference at mip levels 0 and 2, thresholded by `cost_thr`.
6. **Display pass:** generate mipmaps for the texture array, which box-filters the cost (cost aggregation). `view.fragment` averages the cost at mip levels 1–3 for each layer and outputs the colour of the layer with the lowest cost (winner-takes-all).

**Supporting code:**
- `common/frame_source.{h,cpp}` (namespace `vi`):
  - `open_decoder` picks a decoder by file extension. `*.yuv` uses `yuv_decoder`: raw YUV420 through `t3dtv::yuv_reader`, which must already be at the processing resolution. Anything else uses `ffmpeg_decoder`, which handles video files, single images and image sequences like `%04d.png`, and scales the output with swscale.
  - `undistort_map` is a precomputed OpenCV-model (k1,k2,p1,p2,k3) inverse lookup with bilinear sampling. It is built only when a camera has non-zero distortion.
  - `buffered_source` runs one decoder plus undistortion in its own thread, with a bounded queue of `buffer_frames` and recycled buffers. A single-frame source is decoded once and gets no thread.
- `common/camera_loader.h`: `camera_params` holds K, R, t, the distortion coefficients and the calibrated resolution, with helpers `projection()`, `center()`, `forward()` and `scaled_to(w,h)`. It parses two formats, with an exact name match and a cache per file:
  - Panoptic JSON (`*.json`), read with Boost.PropertyTree.
  - Text blocks (see `Georgiev/georgiev.txt`): name, 3×3 K, `k1 k2`, then 3×4 world→camera [R|t].
- `vi_config.h`: the XML config loader. Scalar parameters are declared once in the `CONFIG_PARAMS_LIST` X-macro, which expands into the members, the constructor and the `pt.get<>` calls. **To add a scalar option, add one `CONFIG_PARAM` or `CONFIG_PARAM_OPTIONAL` line there.** The `views` list is parsed separately at the end of `load_config`.
- `setup_program()` sets the common uniforms on both programs by name: `param1`, `param2`, `minZ`, `maxZ`, `minNZ`, `maxNZ`, `nan`, `width`, `height`, `screen_width`, `screen_height` and `layers`. A shader only needs to declare the uniforms it uses.
- `common/controls.cpp`: the free-fly camera, plus `LoadShaders` and `Load3Shaders` (the 3-shader version adds a geometry shader). Shaders are selected through the config.

## Data & config

Configs are XML with the structure `<config><input>…</input><settings>…</settings></config>`.

**Inputs:**
- Either `<input><views><view><file/><camera/></view>…</views>` or the legacy `color1`/`camera1` + `color2`/`camera2`.
- `camera_file` is a `.json` or text calibration file.
- `width`/`height` set the processing resolution. Intrinsics with a known calibrated resolution (from Panoptic JSON) are rescaled to it.
- Optional: `frames` (0 = auto), `fps` (0 = taken from the video, else 25), `max_view_angle` (default 60°) and `buffer_frames` (default 4).

**Datasets:**
- `georgiev.config`: still YUV frames. It interpolates `Camera22` from `Camera21` and `Camera23` (640×480, Z range 1–7 m, horizontal baseline 4 cm).
- `panoptic_pose1*.config`: CMU Panoptic `171204_pose1_sample` HD videos (1920×1080 → 960×540, 29.97 fps, 101 frames, units in cm, strong lens distortion). HD camera `00_22` is held out as the virtual view, so the result can be compared with `hd_00_22.mp4`. The HD cameras are sparse: neighbours are about 125 cm (~25°) apart, so results are much rougher than on Georgiev.
- `data/` holds symlinks to datasets and is git-ignored.

## Gotchas and quirks

- **Colour order:** input textures hold **BGR**, because the shaders sample with `.bgr`. `raw_frame::yuv2rgb` emits B,G,R planes and `ffmpeg_decoder` converts to `AV_PIX_FMT_BGR24`. Keep that order for new decoders.
- Image row 0 is the top row everywhere: K follows the y-down convention, and the textures are uploaded top-first.
- The texture array is framebuffer-sized × `layers` RGBA8 with mipmaps. At 1920×1200 and 50 layers it would be about 600 MB of GPU memory, so keep `output_width`/`output_height` moderate.
- Rendering uses the framebuffer size (`glfwGetFramebufferSize`), not the window size, so on HiDPI or scaled displays the texture array matches real pixels. The window is non-resizable because the texture array is allocated once at startup.
- OpenMP is enabled. `yuv2rgb` uses `#pragma omp`. The video path parallelises across per-source threads instead, so don't add OpenMP inside `buffered_source`, which would oversubscribe the CPU.
- `lodepng.cpp` is compiled but not used by the program. The `<type>`, `<fsaa>` and `limits_file` settings are ignored, and the code hardcodes `GLFW_SAMPLES 0`. `prepare_buffers()` is dead code left over from an older per-pixel mesh approach. `y_axis_up`/`x_axis_right` affect only that dead code.
- The code requests a GL 3.3 core context but calls `glTexStorage3D`, which needs GL 4.2 or `ARB_texture_storage`. Mesa and modern drivers provide it.
- Build outputs (`build*/`, `cmake-*/`) and the copy of the `view_interpolation` binary in the repo root should not be committed.

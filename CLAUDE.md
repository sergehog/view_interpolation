# CLAUDE.md

This file gives Claude Code (claude.ai/code) guidance for working in this repository.

## What this is

Real-time novel-view synthesis (view interpolation) in C++17/OpenGL. It is a university project from 2014 (Tampere University of Technology), revived in 2025 and extended to multi-view video. The program takes calibrated colour views (stills or videos) and renders a virtual camera between them. The depth of each pixel is estimated on the GPU each frame by a **plane sweep** between two input views. No depth maps are used as input.

**Division of work:** the shaders (`shaders/`) do all the per-pixel computation. The C++ side (`src/`) does everything else: reading and decoding files, undistortion, buffering, camera maths, navigation and choosing which views to use.

## Build & run

Dependencies: OpenGL, GLFW3, GLEW, GLM, Boost headers (PropertyTree) and FFmpeg (libavformat, libavcodec, libswscale, libavutil). On Debian/Ubuntu:
`sudo apt install libglfw3-dev libglew-dev libglm-dev libboost-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config`

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release   # the default build type is Release
cmake --build build-release
./build-release/view_interpolation configs/georgiev.config [--snapshot out.png [--frame N]]
```

- `./demo.sh` builds and runs the still-image demo (Georgiev).
- `./demo_video1.sh [config] [dataset-dir]` runs the CMU Panoptic video demo. It links the dataset to `data/171204_pose1_sample` and runs `configs/panoptic_pose1.config` (8 views) by default, or `configs/panoptic_pose1_2views.config`.
- **Always run from the repo root.** Shader paths (default `shaders/…`) and data paths in configs are relative to the current working directory.
- The program opens a normal, non-resizable window of `output_width`×`output_height` (default: the processing resolution). Press **Esc** to quit. Stdout shows the FPS, the current video frame and the view pair in use.
- Controls (`FlyCamera`, `src/fly_camera.cpp`): **hold the left mouse button and drag** to look around, W/A/S/D or the arrow keys to move, Space to move up, right mouse button to move down. The cursor is captured only during a drag, because Wayland doesn't allow warping the cursor. Movement is in world units per second times `speed` (Georgiev is in metres, Panoptic in cm).
- **Code style (Google-like):**
  - Naming: `CamelCase` types, `lowerCamelCase()` functions and methods, `snake_case` variables and struct members, a trailing underscore for private class members (`width_`) and `kConstantName` constants. Namespace `vi`.
  - Formatting: `.clang-format` (Google base, 120 columns, include order: own header, std, libraries, project). Apply it with `clang-format -i src/*.{h,cpp} tools/*.cpp` and leave `third_party/` untouched.
  - GLSL identifiers are not renamed: uniform names (`Texture1`, `MVP1`, `layers`, …) are the interface to config-overridable shaders.

## Testing

There are no unit tests and no CI. Use the **regression suite `tools/eval.sh [geo_par geo_rot temple pan2]`**: it renders held-out camera views headless and prints the PSNR against the real images of those cameras. Run it before and after any change to shaders, camera maths or input decoding. Reference numbers are in the script header (currently 34.88 / 28.77 / 12.72 / 17.85 dB); a pure refactor must reproduce them exactly. When renaming with scripts, also compare string literals: GL looks up uniforms by name, so a renamed `"layers"` compiles fine but silently breaks rendering.
- **Requirements:** `xvfb-run`, python3 with numpy and Pillow, the stereo datasets in `$STEREO_DATA` (default `~/git/stereo/data`) and the Panoptic link in `data/`. Output goes to `eval-out/`.
- **Test cases:**
  - Georgiev 21+23 → 22. The ground truth is the original render `ScenePlane/Color22.png`.
  - The same views with the cameras rotated about their centres by an exact homography (`tools/rotate_views.py`). This catches perspective and geometry bugs.
  - templeSparseRing 07+09 → 08.
  - Panoptic 00_03+00_12 → 00_22, frame 50. The ground truth is the undistorted frame from the `gtframe` target.
- **Snapshot mode:** `--snapshot out.png [--frame N]` renders the initial virtual view of input frame N, saves the framebuffer as PNG and exits. Under Xvfb it uses Mesa llvmpipe and takes about 1–2 s per image. The window size equals the processing size, so snapshots align with ground-truth images pixel for pixel.
- **Depth maps:** `DEPTH=1 tools/eval.sh` also renders them, or point `<fragment_shader>` at `tools/select_depth.frag`.
- **Other helpers:** `psnr.py`, `make_config.sh` and `middlebury_par_to_txt.py`.
- **Checking the CPU side without OpenGL:** link against the `vi_input` library, then project known 3D points (Panoptic's `hdPose3d_stage1_coco19.tar` has ground-truth joints) onto decoded frames.

## Code layout

- `src/main.cpp`: command line, `GlWindow` (GLFW + GLEW setup) and the main loop.
- `src/config.{h,cpp}`: `AppConfig`, a plain struct with documented defaults, and `loadConfig()` for the XML file. **To add an option:** add a member to `AppConfig` and one `required()`/`optional()` line to `loadConfig()`.
- `src/camera.{h,cpp}`: the `Camera` struct holds `intrinsics` (K), `rotation` (R), `translation` (t), `distortion` and the calibrated resolution, with helpers `projection()`, `center()`, `forward()` and `scaledTo(w,h)`. `loadCameras()`/`findCamera()` parse calibration files (exact names, cached per file):
  - Panoptic JSON (`*.json`).
  - Text blocks (see `Georgiev/georgiev.txt`): name, 3×3 K, `k1 k2`, then a 3×4 world→camera [R|t].
- `src/frame_source.{h,cpp}`: input decoding, all through FFmpeg.
  - `FfmpegDecoder` handles videos, images and image sequences like `%04d.png`, and scales them to the processing resolution. `*.yuv` files are read with the `rawvideo` demuxer as YUV420 at exactly the processing resolution.
  - `UndistortMap`: precomputed OpenCV-model (k1,k2,p1,p2,k3) undistortion with bilinear sampling.
  - `BufferedSource`: one decoder plus undistortion in its own thread, with a bounded queue. A single-frame source is decoded once and gets no thread.
- `src/input_views.{h,cpp}`: `openInputViews()` opens all views, scaling each camera to the processing resolution. All decoders first agree on one loop length (the shortest video, or `<frames>`), which keeps the streams frame-synchronous. `selectViewPair()` is described in the main loop below.
- `src/fly_camera.{h,cpp}`: `FlyCamera::update()` turns mouse and keyboard input into a view matrix, which is the identity until the user moves.
- `src/gl_program.{h,cpp}`: `loadProgram(vertex, geometry, fragment)`. The geometry shader is optional. Compiler warnings are printed; errors throw.
- `src/plane_sweep_renderer.{h,cpp}`: `PlaneSweepRenderer` owns all GL objects (programs, quads, input textures, layer array, FBO) and runs both passes. `uploadInput()`, `render(mvp1, mvp2)` and `readBackBuffer()`.
- `shaders/`: `sweep.{vert,geom,frag}` (pass 1) and `select.{vert,frag}` (pass 2). Configs can override them (`vertex_internal`/`geometry_internal`/`fragment_internal` for the sweep pass, `vertex_shader`/`geometry_shader`/`fragment_shader` for the select pass).
- `tools/`: the evaluation suite and data converters (see Testing). `third_party/lodepng/` is the PNG encoder used for snapshots.

## How a frame is rendered

1. **Video clock:** when `1/fps` has passed, call `advance()` on every source, so all views stay on the same frame index. Rendering runs at vsync, independently of the video rate. If decoding falls behind, playback slows down rather than skipping frames.
2. **Camera maths:** `C_inv = inverse(E) * V * inverse(K4)` maps `(u*z, v*z, z, 1)` of the moved virtual camera to world coordinates. `CV = K4 * E` is the virtual camera and `V` is the fly-camera matrix. **The motion is applied in the virtual camera's own frame;** applying it in world coordinates was only correct when the virtual camera was the world origin, as in Georgiev.
3. **View selection (`selectViewPair`)**: among views facing within `max_view_angle` of the virtual camera, pick the pair whose connecting segment passes closest to the virtual camera centre, plus a 0.25×baseline penalty. A 5% hysteresis prevents flicker between similar pairs. With 2 views, both are always used.
4. **Upload:** the selected pair's frames are uploaded only when the view or its frame changed. Then `MVP_i = P_i * C_inv`.
5. **Sweep pass:** one quad per depth layer goes into a layered FBO (a `GL_TEXTURE_2D_ARRAY` with `layers` slices at framebuffer size).
   - `sweep.vert`: depth hypotheses are uniform in inverse depth, from `maxZ` (layer 0) to `minZ` (last layer). It back-projects each pixel and projects it into both inputs, outputting **homogeneous** texture coordinates.
   - `sweep.geom`: sets `gl_Layer`.
   - `sweep.frag`: divides per fragment, then writes rgb = mean colour and a = cost. The cost is the abs difference at mip levels 0 and 2, saturated at `param1` (default 21/255). Points behind an input camera or outside its image get cost 1.
6. **Select pass:** generate mipmaps for the array, which box-filters the costs. `select.frag` averages each layer's cost over mip levels 1–3 and outputs the colour of the cheapest layer (winner-takes-all).

**Keep the perspective divide in the fragment shader.** Dividing per vertex and interpolating is exact only for parallel cameras. That was the bug that broke all rotated or converging setups. Because of this, a 2-triangle quad is exact and no dense mesh is needed.

**Half-pixel conventions:** raster `u` is continuous (pixel x spans [x, x+1]) but K uses pixel centres. `sweep.vert` therefore shifts by −0.5 before back-projecting and by +0.5 before normalising texture coordinates.

## Data & config

Configs live in `configs/` and are XML: `<config><input>…</input><settings>…</settings></config>`.

**Required:**
- `input/width`, `input/height`: the processing resolution. Intrinsics with a known calibrated resolution (from Panoptic JSON) are rescaled to it.
- `input/minZ`, `input/maxZ`, `input/camera_file`.
- The views: either `input/views/view{file,camera}…` or the legacy `color1`/`camera1` + `color2`/`camera2`.
- `settings/layers`, `settings/desired_camera`.

**Optional:**
- Input: `frames` (0 = auto) and `fps` (0 = from the video, else 25).
- Settings: `desired_camera_file`, `output_width`, `output_height`, `param1` (cost threshold), `param2`, `speed`, `mouseSpeed`, `max_view_angle` (default 60°), `buffer_frames` (default 4) and the shader paths. Unknown keys are ignored.

**Datasets:**
- `configs/georgiev.config`: still YUV frames from `Georgiev/`, which are limited-range BT.601 versions of `ScenePlane/ColorXX.png`. It interpolates `Camera22` from `Camera21` and `Camera23` (640×480, Z range 1–7 m, horizontal baseline 4 cm).
- `configs/panoptic_pose1*.config`: CMU Panoptic `171204_pose1_sample` HD videos (1920×1080 → 960×540, 29.97 fps, 101 frames, units in cm, strong lens distortion). HD camera `00_22` is held out as the virtual view. The HD cameras are sparse, about 125 cm (~25°) apart.
- `data/` holds dataset symlinks and is git-ignored.

## Gotchas

- **Colour order:** input textures hold **BGR**, because the shaders sample with `.bgr`. `FfmpegDecoder` outputs `AV_PIX_FMT_BGR24`.
- **YUV is limited-range BT.601,** as for any video. The original code used full-range conversion, which was about 9 dB off on the Georgiev data compared with the original renders.
- Image row 0 is the top row everywhere: K is y-down, and textures are uploaded top-first. `readBackBuffer()` flips the bottom-up GL rows.
- The layer array is framebuffer-sized × `layers` RGBA8 with a full mip chain: about 600 MB at 1920×1200 with 50 layers. Rendering uses the framebuffer size, which differs from the window size on HiDPI displays. The window is non-resizable because the array is allocated once.
- **Wide baselines are a fundamental limit** of this two-view plane sweep (Panoptic, about 50° between the bracketing cameras; templeSparseRing). Uniform regions (grey dome walls, black backgrounds) match perfectly at *wrong* depths, while the true surface changes appearance between views. Doubling the layers halves the frame rate for little gain (about 55 FPS at 50 layers vs 12 FPS at 200 on an Intel iGPU at 960×540). Real gains need an occlusion-aware or multi-view cost.
- `gl_Layer` must be written for every emitted vertex. Outputs are undefined after `EmitVertex()`; the original code set it once and only worked on some drivers.
- A GL 3.3 core context is requested, but the renderer needs `glTexStorage*` (GL 4.2 or `ARB_texture_storage`) and checks for it. OpenGL errors from setup and the first frame are printed once.
- FFmpeg's log level is set to errors only. `BufferedSource` parallelises across per-source threads; don't add OpenMP inside it.

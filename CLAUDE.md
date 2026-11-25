# CLAUDE.md

This file gives Claude Code (claude.ai/code) guidance for working in this repository.

## What this is

Real-time novel-view synthesis (view interpolation) in C++/OpenGL. It is a university project from 2014 (Tampere University of Technology), ported from Visual Studio to CMake/Linux in 2025. The program takes two rectified colour views plus their camera parameters and renders a virtual camera between them. The depth of each pixel is estimated on the GPU each frame by a **plane sweep**. No depth maps are used as input.

## Build & run

Dependencies: OpenGL, GLFW3, GLEW, GLM and Boost headers (Boost.PropertyTree, header-only). On Debian/Ubuntu:
`sudo apt install libglfw3-dev libglew-dev libglm-dev libboost-dev`

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # CLion uses cmake-build-debug/
cmake --build build
./build/view_interpolation georgiev.config         # run from the repo root
```

`demo.sh` runs `./view_interpolation georgiev.config`, so it expects a copy of the binary in the repo root.

- **Always run from the repo root.** The shader paths and data paths in the config are relative to the current working directory.
- The window opens **fullscreen on the primary monitor** with the cursor hidden. Press **Esc** to quit. The FPS count is printed to stdout.
- Controls (`common/controls.cpp`): mouse to look around, W/A/S/D or the arrow keys to move, Space to move up, right mouse button to move down.
- There are no tests, no linter and no CI. To check a change, build it and run the demo.
- `view_interpolation.vcxproj` is the old Visual Studio project. It is not used or maintained.

## Architecture

**Main loop (`view_interpolation.cpp`)**, run once per frame:
1. Read one frame of each input YUV file (`t3dtv::yuv_reader`, YUV420 → 444 → RGB on the CPU), deinterleave it to RGB and upload it to `Texture1` and `Texture2`, then generate mipmaps.
2. Build `MVP1 = C1 * (View * inverse(CV))` and `MVP2` the same way. `Ci = K_i * [R|t]_i` is each input camera, `CV` is the desired virtual camera, and `View` comes from the mouse/keyboard (`computeViewMatrix`).
3. **Internal pass:** render into an FBO whose colour attachment is a `GL_TEXTURE_2D_ARRAY` with `layers` slices. The program draws one fullscreen quad per depth layer and sets the `layer` uniform for each.
   - `view_internal.vertex`: maps layer index → depth hypothesis `z`, evenly spaced in inverse depth between `minZ` and `maxZ` (uniforms `minNZ`/`maxNZ`). It back-projects the pixel at `z` and projects it into both input views.
   - `view_internal.geometry`: sends the triangle to slice `gl_Layer = layer`.
   - `view_internal.fragment`: writes RGB = the mean of the two input colours and A = the photo-consistency cost. The cost is the abs difference at mip levels 0 and 2, thresholded by `cost_thr`.
4. **Display pass:** generate mipmaps for the texture array, which box-filters the cost (cost aggregation). `view.fragment` averages the cost at mip levels 1–3 for each layer and outputs the colour of the layer with the lowest cost (winner-takes-all).

**Supporting code:**
- `vi_config.h`: the XML config loader, built on Boost.PropertyTree. All parameters are declared once in the `CONFIG_PARAMS_LIST` X-macro, which expands into the members, the constructor and the `pt.get<>` calls. **To add a config option, add one `CONFIG_PARAM` or `CONFIG_PARAM_OPTIONAL` line there.** Optional defaults can refer to earlier params (e.g. `output_width` defaults to `width`).
- `setup_program()` sets the common uniforms on both programs by name: `param1`, `param2`, `minZ`, `maxZ`, `minNZ`, `maxNZ`, `nan`, `width`, `height`, `screen_width`, `screen_height` and `layers`. A shader only needs to declare the uniforms it uses.
- `common/camera_loader.h`: parses the camera text file (see `Georgiev/georgiev.txt`). Each camera entry is a name line, a 3×3 K, two ignored floats and a 3×4 [R|t]. The loader converts row-major text to GLM column-major.
- `common/controls.cpp`: the free-fly camera, plus `LoadShaders` and `Load3Shaders` (the 3-shader version adds a geometry shader). Shaders are selected through the config, so you can test alternative shaders by editing the config instead of the C++.
- `common/yuv_reader.h` and `common/raw_frame.h`: raw planar YUV I/O. Input files are headerless YUV420 (`width*height*1.5` bytes per frame). `frames` (default 1) sets how many frames are played before the loop rewinds.

## Data & config

`georgiev.config` is an XML file with a `<config><input>…</input><settings>…</settings></config>` structure. The demo interpolates `Camera22` from `Camera21` and `Camera23` (640×480, Z range 1–7, 50 layers). The cameras differ only by a horizontal baseline.

## Gotchas and quirks

- `CMakeLists.txt` does not call `find_package(Boost)` and does not enable OpenMP. Boost builds only because its headers are in the system include path. The `#pragma omp` loops run single-threaded.
- `lodepng.cpp` is compiled but nothing uses it. The `<type>png</type>`, `<fsaa>` and `limits_file` settings are also ignored: the code hardcodes `GLFW_SAMPLES 0`, and only YUV input is implemented. `prepare_buffers()` is dead code left over from an older per-pixel mesh approach.
- The code requests a GL 3.3 core context but calls `glTexStorage3D`, which needs GL 4.2 or `ARB_texture_storage`. Mesa and modern drivers provide it.
- `camera_loader` finds a camera with a **substring match** on each line, so `Camera2` would match `Camera20`. It also echoes every line it scans to stdout.
- Image orientation: shaders swizzle `.bgr`, and `y_axis_up`/`x_axis_right` affect only the unused `prepare_buffers`. Expect to check orientation and colour order visually after rendering changes.
- The build output and the untracked `view_interpolation` binary in the repo root should not be committed. `.gitignore` covers `cmake-*/` and `build/`.

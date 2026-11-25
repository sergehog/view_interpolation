# view_interpolation

Real-time view interpolation in C++/OpenGL. A virtual camera is rendered between calibrated input views
(images or videos) by a GPU plane sweep, which estimates depth on the fly in the virtual view.

The code was written in 2014 at Tampere University of Technology. It was revived in 2025 with CMake,
Linux support, FFmpeg video input, multi-view input and geometry fixes.

## Build

```bash
sudo apt install cmake libglfw3-dev libglew-dev libglm-dev libboost-dev \
                 libavformat-dev libavcodec-dev libswscale-dev libavutil-dev pkg-config
cmake -S . -B build-release
cmake --build build-release
```

## Run

From the repo root:

```bash
./demo.sh                 # still images, synthetic scene (Georgiev)
./demo_video1.sh          # multi-view video, CMU Panoptic 171204_pose1_sample (download it first)
./build-release/view_interpolation configs/<name>.config [--snapshot out.png [--frame N]]
```

The mouse is captured at startup and turns the view; Tab releases or recaptures it. W/A/S/D or the arrow
keys move, Space and the right mouse button move up and down, and Esc quits.

See `CLAUDE.md` for the architecture, the config format and the evaluation suite (`tools/eval.sh`).

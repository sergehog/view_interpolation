#!/usr/bin/env bash
# Snapshot regression suite: renders held-out camera views headless (Xvfb + Mesa) and prints the PSNR
# against the real image of that camera. Run it before and after changes to shaders or camera maths.
#
# usage: tools/eval.sh [test ...]        tests: geo_par geo_rot temple pan2 (default: all with data available)
#   geo_par  Georgiev 21+23 -> 22, parallel cameras, YUV input (repo Georgiev/; ground truth: $STEREO_DATA/ScenePlane)
#   geo_rot  same views, cameras rotated about their centres   (catches perspective/geometry bugs; PNG input)
#   temple   Middlebury templeSparseRing 07+09 -> 08, 46 deg   (needs $STEREO_DATA/templeSparseRing)
#   pan2     Panoptic 00_03+00_12 -> 00_22, frame 50           (needs data/171204_pose1_sample, see demo_video1.sh)
# env: STEREO_DATA (default ~/git/stereo/data), OUT (default eval-out), FRAME (default 50),
#      DEPTH=1 also renders depth maps (tools/select_depth.frag) as <test>_depth.png
# needs: xvfb-run, python3 with numpy and Pillow
#
# Reference results (PSNR, Nov 2025, after the geometry and YUV colour fixes):
#   geo_par 34.88, geo_rot 28.77, temple 12.72, pan2 17.85 dB
# (geo_rot can't reach geo_par: its inputs are resampled twice and lose their borders.)
set -euo pipefail
cd "$(dirname "$0")/.."
STEREO_DATA=${STEREO_DATA:-$HOME/git/stereo/data}
OUT=${OUT:-eval-out}
FRAME=${FRAME:-50}
BUILD=build-release
T=tools

cmake -S . -B $BUILD -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build $BUILD --target view_interpolation gtframe > /dev/null 2>&1 || cmake --build $BUILD --target view_interpolation gtframe
mkdir -p "$OUT/data"

prepare() { # prepare <test>: data + config + ground truth, returns 1 if data is missing
    local G=$STEREO_DATA/Georgiev D=$STEREO_DATA/templeSparseRing P=data/171204_pose1_sample
    case $1 in
    geo_par|geo_rot)
        local SP=$STEREO_DATA/ScenePlane
        [ -f "$SP/Color22.png" ] || return 1
        echo "$SP/Color22.png 120" > "$OUT/$1.gt" # crop: rotated inputs don't cover the borders
        if [ $1 = geo_par ]; then
            $T/make_config.sh "$OUT/$1.config" 640 480 1 7 50 Georgiev/georgiev.txt Camera22 \
                Georgiev/Color21.yuv Camera21 Georgiev/Color23.yuv Camera23
        else
            [ -f "$OUT/data/georot/cameras.txt" ] || python3 $T/rotate_views.py Georgiev/georgiev.txt "$OUT/data/georot" \
                "Camera21:$SP/Color21.png:10,4,6" "Camera23:$SP/Color23.png:-10,-3,-5" --keep Camera22
            $T/make_config.sh "$OUT/$1.config" 640 480 1 7 50 "$OUT/data/georot/cameras.txt" Camera22 \
                "$OUT/data/georot/Camera21.png" Camera21 "$OUT/data/georot/Camera23.png" Camera23
        fi ;;
    temple)
        [ -f "$D/templeSR_par.txt" ] || return 1
        python3 $T/middlebury_par_to_txt.py "$D/templeSR_par.txt" "$OUT/data/temple.txt"
        $T/make_config.sh "$OUT/$1.config" 640 480 0.45 0.75 64 "$OUT/data/temple.txt" templeSR0008 \
            "$D/templeSR0007.png" templeSR0007 "$D/templeSR0009.png" templeSR0009
        echo "$D/templeSR0008.png 0" > "$OUT/$1.gt" ;;
    pan2)
        [ -f "$P/hdVideos/hd_00_22.mp4" ] || return 1
        cp configs/panoptic_pose1_2views.config "$OUT/$1.config"
        [ -f "$OUT/data/pan_gt22_f$FRAME.png" ] || $BUILD/gtframe "$P/calibration_171204_pose1_sample.json" \
            "$P/hdVideos/hd_00_22.mp4" 00_22 "$FRAME" 960 540 "$OUT/data/pan_gt22_f$FRAME.png"
        echo "$OUT/data/pan_gt22_f$FRAME.png 0" > "$OUT/$1.gt" ;;
    *) echo "unknown test $1" >&2; return 1 ;;
    esac
}

snapshot() { # snapshot <config> <out.png>
    xvfb-run -a -s "-screen 0 1280x1024x24" $BUILD/view_interpolation "$1" --snapshot "$2" --frame "$FRAME" > "${2%.png}.log" 2>&1
    if grep -q "OpenGL error" "${2%.png}.log"; then grep "OpenGL error" "${2%.png}.log" | sed "s|^|  $(basename "$2"): |"; fi
}

for t in ${@:-geo_par geo_rot temple pan2}; do
    if ! prepare $t; then echo "$t: skipped (no data)"; continue; fi
    if ! snapshot "$OUT/$t.config" "$OUT/$t.png"; then echo "$t: FAILED, see $OUT/$t.log"; continue; fi
    read -r gt crop < "$OUT/$t.gt"
    echo "$t: $(python3 $T/psnr.py "$OUT/$t.png" "$gt" "$crop")"
    if [ "${DEPTH:-0}" = 1 ]; then
        sed "s|</settings>|<fragment_shader>$T/select_depth.frag</fragment_shader></settings>|" "$OUT/$t.config" > "$OUT/${t}_depth.config"
        snapshot "$OUT/${t}_depth.config" "$OUT/${t}_depth.png" || true
    fi
done

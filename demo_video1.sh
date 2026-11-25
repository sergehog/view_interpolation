#!/usr/bin/env bash
# Video view-interpolation demo on the CMU Panoptic "171204_pose1_sample" dataset.
#
# Usage: ./demo_video1.sh [config] [dataset-dir]
#   config       default: configs/panoptic_pose1.config (multi-view); or configs/panoptic_pose1_2views.config
#   dataset-dir  folder with calibration_171204_pose1_sample.json and hdVideos/
#                default: $PANOPTIC_SAMPLE or ~/git/panoptic-toolbox/171204_pose1_sample
#
# The dataset is linked into data/ (configs use relative paths) and a Release build is made in build-release/.
set -euo pipefail
cd "$(dirname "$0")"

CONFIG="${1:-configs/panoptic_pose1.config}"
DATASET="${2:-${PANOPTIC_SAMPLE:-$HOME/git/panoptic-toolbox/171204_pose1_sample}}"

if [ ! -f "$DATASET/calibration_171204_pose1_sample.json" ] || [ ! -d "$DATASET/hdVideos" ]; then
    echo "Panoptic sample not found in: $DATASET" >&2
    echo "Download it with panoptic-toolbox (scripts/getData.sh 171204_pose1_sample) and pass its path." >&2
    exit 1
fi

mkdir -p data
ln -sfn "$(realpath "$DATASET")" data/171204_pose1_sample

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-release --target view_interpolation

./build-release/view_interpolation "$CONFIG"

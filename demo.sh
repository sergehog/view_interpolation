#!/usr/bin/env bash
# Still-image demo: Georgiev synthetic scene, virtual camera 22 interpolated from cameras 21 and 23.
# Usage: ./demo.sh
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-release --target view_interpolation
./build-release/view_interpolation configs/georgiev.config

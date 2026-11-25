#!/usr/bin/env bash
# Writes a minimal config for snapshot tests.
# usage: make_config.sh out.config width height minZ maxZ layers camera_file desired_camera file1 camera1 file2 camera2 [file camera]...
# Shaders default to shaders/ (run view_interpolation from the repo root); FRAGMENT_SHADER=path overrides the
# select-pass fragment shader, e.g. tools/select_depth.frag.
set -euo pipefail
out=$1 w=$2 h=$3 minz=$4 maxz=$5 layers=$6 camfile=$7 desired=$8
shift 8
{
    echo "<config><input><width>$w</width><height>$h</height><minZ>$minz</minZ><maxZ>$maxz</maxZ>"
    echo "<camera_file>$camfile</camera_file><views>"
    while [ $# -ge 2 ]; do echo "<view><file>$1</file><camera>$2</camera></view>"; shift 2; done
    echo "</views></input><settings>"
    [ -z "${FRAGMENT_SHADER:-}" ] || echo "<fragment_shader>$FRAGMENT_SHADER</fragment_shader>"
    echo "<layers>$layers</layers><desired_camera>$desired</desired_camera><speed>-0.1</speed><mouseSpeed>0.0001</mouseSpeed>"
    echo "</settings></config>"
} > "$out"

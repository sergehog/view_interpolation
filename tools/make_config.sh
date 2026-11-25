#!/usr/bin/env bash
# Writes a minimal config for snapshot tests.
# usage: make_config.sh out.config width height minZ maxZ layers camera_file desired_camera file1 camera1 file2 camera2 [file camera]...
# Shader paths are relative to the repo root (run view_interpolation from there).
set -euo pipefail
out=$1 w=$2 h=$3 minz=$4 maxz=$5 layers=$6 camfile=$7 desired=$8
shift 8
{
    echo "<config><input><width>$w</width><height>$h</height><minZ>$minz</minZ><maxZ>$maxz</maxZ>"
    echo "<camera_file>$camfile</camera_file><views>"
    while [ $# -ge 2 ]; do echo "<view><file>$1</file><camera>$2</camera></view>"; shift 2; done
    echo "</views></input><settings>"
    echo "<vertex_shader>view.vertex</vertex_shader><fragment_shader>${FRAGMENT_SHADER:-view.fragment}</fragment_shader>"
    echo "<vertex_internal>view_internal.vertex</vertex_internal><geometry_internal>view_internal.geometry</geometry_internal><fragment_internal>view_internal.fragment</fragment_internal>"
    echo "<layers>$layers</layers><desired_camera>$desired</desired_camera><speed>-0.1</speed><mouseSpeed>0.0001</mouseSpeed>"
    echo "</settings></config>"
} > "$out"

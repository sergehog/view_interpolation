#!/usr/bin/env python3
"""Makes a rotated-camera test case from any calibrated views.

Rotating a camera about its own centre changes the image by an exact homography K Q K^-1, no depth
needed. A correct renderer should therefore interpolate the same virtual view about as well from the
rotated inputs as from the originals; perspective/geometry bugs show up as a large PSNR drop.

usage: rotate_views.py camera_file.txt out_dir NAME:image.png:yaw,pitch,roll [...] [--keep NAME ...]
writes out_dir/NAME.png (black outside the original field of view) and out_dir/cameras.txt
(text camera format: R' = Q R, t' = Q t; cameras listed with --keep are copied unchanged).
"""
import os
import sys
import numpy as np
from PIL import Image


def read_cameras(path):
    """Text camera format: name, 3x3 K, k1 k2, 3x4 [R|t] (all row-major)."""
    tokens = open(path).read().split()
    cams, i = {}, 0
    while i < len(tokens):
        name, v = tokens[i], [float(x) for x in tokens[i + 1:i + 24]]
        cams[name] = (np.array(v[:9]).reshape(3, 3), v[9:11], np.array(v[11:]).reshape(3, 4))
        i += 24
    return cams


def rotation(yaw, pitch, roll):
    y, p, r = np.radians([yaw, pitch, roll])
    Ry = np.array([[np.cos(y), 0, np.sin(y)], [0, 1, 0], [-np.sin(y), 0, np.cos(y)]])
    Rx = np.array([[1, 0, 0], [0, np.cos(p), -np.sin(p)], [0, np.sin(p), np.cos(p)]])
    Rz = np.array([[np.cos(r), -np.sin(r), 0], [np.sin(r), np.cos(r), 0], [0, 0, 1]])
    return Rz @ Rx @ Ry


def warp(img, K, Q):
    h, w, _ = img.shape
    ys, xs = np.mgrid[0:h, 0:w]
    p = np.stack([xs.ravel(), ys.ravel(), np.ones(h * w)])
    src = K @ Q.T @ np.linalg.inv(K) @ p  # new pixel -> original pixel
    sx, sy = src[0] / src[2], src[1] / src[2]
    valid = (src[2] > 0) & (sx >= 0) & (sy >= 0) & (sx <= w - 1) & (sy <= h - 1)
    x0 = np.clip(np.floor(sx).astype(int), 0, w - 2)
    y0 = np.clip(np.floor(sy).astype(int), 0, h - 2)
    ax = np.clip(sx - x0, 0, 1)[:, None]
    ay = np.clip(sy - y0, 0, 1)[:, None]
    out = (img[y0, x0] * (1 - ax) + img[y0, x0 + 1] * ax) * (1 - ay) \
        + (img[y0 + 1, x0] * (1 - ax) + img[y0 + 1, x0 + 1] * ax) * ay
    out[~valid] = 0
    return np.round(out).reshape(h, w, 3).astype(np.uint8)


def write_camera(f, name, K, dist, Rt):
    f.write(name + '\n')
    for r in K:
        f.write(' '.join('%.10g' % x for x in r) + '\n')
    f.write('%.10g %.10g\n' % tuple(dist))
    for r in Rt:
        f.write(' '.join('%.12g' % x for x in r) + '\n')
    f.write('\n')


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__)
    cams = read_cameras(argv[1])
    out_dir = argv[2]
    os.makedirs(out_dir, exist_ok=True)
    keep = argv[argv.index('--keep') + 1:] if '--keep' in argv else []
    specs = argv[3:argv.index('--keep')] if '--keep' in argv else argv[3:]
    with open(os.path.join(out_dir, 'cameras.txt'), 'w') as f:
        for spec in specs:
            name, image, angles = spec.split(':')
            K, dist, Rt = cams[name]
            Q = rotation(*[float(a) for a in angles.split(',')])
            img = np.asarray(Image.open(image).convert('RGB'), dtype=np.float64)
            Image.fromarray(warp(img, K, Q)).save(os.path.join(out_dir, name + '.png'))
            write_camera(f, name, K, dist, Q @ Rt)
        for name in keep:
            write_camera(f, name, *cams[name])


if __name__ == '__main__':
    main(sys.argv)

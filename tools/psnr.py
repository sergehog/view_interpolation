#!/usr/bin/env python3
"""PSNR between two images: psnr.py rendered.png ground_truth.png [border_crop_px]"""
import sys
import numpy as np
from PIL import Image

a = np.asarray(Image.open(sys.argv[1]).convert('RGB'), dtype=np.float64)
b = np.asarray(Image.open(sys.argv[2]).convert('RGB'), dtype=np.float64)
if a.shape != b.shape:
    sys.exit('size mismatch: %s vs %s' % (a.shape, b.shape))
crop = int(sys.argv[3]) if len(sys.argv) > 3 else 0
if crop:
    a, b = a[crop:-crop, crop:-crop], b[crop:-crop, crop:-crop]
mse = ((a - b) ** 2).mean()
print('%.2f dB' % (10 * np.log10(255 ** 2 / max(mse, 1e-12))))

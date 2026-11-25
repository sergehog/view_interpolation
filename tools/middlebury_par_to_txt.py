#!/usr/bin/env python3
"""Converts a Middlebury multi-view *_par.txt (e.g. templeSparseRing) to the text camera format.

usage: middlebury_par_to_txt.py templeSR_par.txt cameras.txt
Each camera is named after its image without extension (templeSR0001, ...).
"""
import sys

lines = [l.split() for l in open(sys.argv[1]).read().splitlines()[1:] if l.strip()]
with open(sys.argv[2], 'w') as f:
    for l in lines:
        v = [float(x) for x in l[1:]]
        K, R, t = v[:9], v[9:18], v[18:21]
        f.write(l[0].rsplit('.', 1)[0] + '\n')
        for r in range(3):
            f.write(' '.join('%.10g' % x for x in K[3 * r:3 * r + 3]) + '\n')
        f.write('0 0\n')
        for r in range(3):
            f.write(' '.join('%.12g' % x for x in R[3 * r:3 * r + 3]) + ' %.12g\n' % t[r])
        f.write('\n')

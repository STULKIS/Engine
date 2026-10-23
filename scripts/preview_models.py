#!/usr/bin/env python3
"""Offline preview of the placeholder models, without a GPU.

Rasterises assets/models/*.obj with a z-buffer, cel-ish shading and the same
light direction as the engine, then writes a PNG. Useful on machines (or CI
runners) where Frontier cannot open a GL context, and as a quick look at the
generated art.

Usage: python3 scripts/preview_models.py [--out docs/placeholder-assets.png]
"""
import argparse, math, os, struct, zlib


def load_obj(path):
    verts, faces = [], []
    for line in open(path):
        p = line.split()
        if not p: continue
        if p[0] == 'v': verts.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == 'f':
            idx = [int(c.split('/')[0]) - 1 for c in p[1:]]
            for i in range(2, len(idx)):
                faces.append((idx[0], idx[i-1], idx[i]))
    return verts, faces

def sub(a,b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def norm(a):
    l = math.sqrt(dot(a,a)) or 1.0
    return (a[0]/l, a[1]/l, a[2]/l)

W, H = 700, 780
bg = (0.13, 0.16, 0.20)
buf = [[bg for _ in range(W)] for _ in range(H)]
zbuf = [[1e9]*W for _ in range(H)]

scene = []
def add_model(path, translate, scale, color, rot=0.0):
    v, f = load_obj(path)
    c, s = math.cos(rot), math.sin(rot)
    out = []
    for x, y, z in v:
        x, z = x*c - z*s, x*s + z*c
        out.append((x*scale + translate[0], y*scale + translate[1], z*scale + translate[2]))
    scene.append((out, f, color, translate, scale))

parser = argparse.ArgumentParser()
parser.add_argument('--assets', default='assets', help='asset root (default: assets)')
parser.add_argument('--out', default='docs/placeholder-assets.png')
options = parser.parse_args()
models = os.path.join(options.assets, 'models')

add_model(os.path.join(models, 'character.obj'), (-1.35, 0.0, 0.0), 1.0, (0.30, 0.62, 0.72), 0.5)
add_model(os.path.join(models, 'crystal.obj'), (0.55, 0.0, 0.3), 0.95, (0.42, 0.78, 0.92), -0.3)
add_model(os.path.join(models, 'rock.obj'), (1.45, 0.62, -0.25), 0.62, (0.62, 0.60, 0.55), 1.1)

cam = (0.1, 1.5, 5.2)
light = norm((-0.45, 0.8, 0.5))
target = (0.0, 0.85, 0.0)
def project(p):
    # simple look-at toward target
    fwd = norm(sub(target, cam))
    right = norm(cross(fwd, (0.0, 1.0, 0.0)))
    up = cross(right, fwd)
    rel = sub(p, cam)
    x, y, z = dot(rel, right), dot(rel, up), dot(rel, fwd)
    if z <= 0.05: return None
    f = 1.35
    return (W/2 + x/z*f*H/2*0.95, H/2 - y/z*f*H/2*0.95, z)

for verts, faces, color, _, _ in scene:
    cam_verts = [project(p) for p in verts]
    for f in faces:
        a, b, c = (cam_verts[i] for i in f)
        if not (a and b and c): continue
        n = norm(cross(sub(verts[f[1]], verts[f[0]]), sub(verts[f[2]], verts[f[0]])))
        shade = max(0.0, dot(n, light))
        # cel-ish ramp with a cool terminator, matching the engine's stylisation
        lit = 0.30 + 0.70 * (1.0 if shade > 0.55 else (0.55 if shade > 0.22 else 0.12))
        col = tuple(min(1.0, ch * lit + 0.05) for ch in color)
        minx, maxx = int(min(a[0], b[0], c[0])), int(max(a[0], b[0], c[0]))
        miny, maxy = int(min(a[1], b[1], c[1])), int(max(a[1], b[1], c[1]))
        area = (b[0]-a[0])*(c[1]-a[1]) - (c[0]-a[0])*(b[1]-a[1])
        if abs(area) < 1e-6: continue
        for y in range(max(0, miny), min(H, maxy+1)):
            for x in range(max(0, minx), min(W, maxx+1)):
                px, py = x + 0.5, y + 0.5
                w0 = ((b[0]-px)*(c[1]-py) - (c[0]-px)*(b[1]-py)) / area
                w1 = ((c[0]-px)*(a[1]-py) - (a[0]-px)*(c[1]-py)) / area
                w2 = 1.0 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0: continue
                z = w0*a[2] + w1*b[2] + w2*c[2]
                if z < zbuf[y][x]:
                    zbuf[y][x] = z
                    buf[y][x] = col

raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        r, g, b = buf[y][x]
        # vignette + sRGB-ish encode
        dx, dy = (x/W-0.5), (y/H-0.5)
        v = max(0.0, 1.0 - (dx*dx+dy*dy)*0.9)
        raw += bytes(int(min(255, max(0, (ch*v)**(1/2.2)*255))) for ch in (r, g, b))

def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + \
      chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
os.makedirs(os.path.dirname(options.out) or '.', exist_ok=True)
open(options.out, 'wb').write(png)
print(f'wrote {options.out} ({len(png)} bytes, {W}x{H})')

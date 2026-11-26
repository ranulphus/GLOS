#!/usr/bin/env python3
"""mkwalls.py OUTDIR: GLOS's own wallpapers, four synthwave scenes at 1600 by 1200, drawn here from nothing (no
outside picture: the owner's choice of 2026-11-26), as PNG files:

    SUNSET.PNG     a striped sun setting over a neon grid
    NIGHTCTY.PNG   a city's skyline at night, its windows lit, the sun behind it, a grid before it
    NEONROAD.PNG   a road to the horizon between mountains with lit ridges, the sun in the pass
    STARGRID.PNG   a starfield, a nebula and a ringed planet over wireframe hills either side of a valley

Plain Python (zlib for the PNG; no numpy, no PIL): each scene's random numbers have a seed of their own, so the
same pictures every build. The floors are worked out a pixel at a time (a perspective grid, its lines as wide as
they would be at that depth, blurring into a haze where they crowd: no moire); the hills are a mesh drawn from the
back forward, each cell filled before its edges, so nearer hills hide what is behind them.
"""
import math
import os
import random
import struct
import sys
import zlib
from array import array

W, H = 1600, 1200


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def smooth(e0, e1, v):
    t = clamp((v - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def mix(a, b, t):
    return tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3))


class Pic:
    def __init__(self):
        self.px = bytearray(W * H * 3)
        self.cov = None                 # a wireframe's lines' coverage (0..1) until lay() puts them on

    def row(self, y, rgb, x0=0, x1=W):
        x0, x1 = max(0, int(x0)), min(W, int(x1))
        if 0 <= y < H and x1 > x0:
            self.px[(y * W + x0) * 3:(y * W + x1) * 3] = bytes(rgb) * (x1 - x0)

    def add(self, x, y, rgb, a):
        """rgb added at strength a (0..1), as light is"""
        if 0 <= x < W and 0 <= y < H and a > 0:
            i = (y * W + x) * 3
            p = self.px
            p[i] = min(255, p[i] + int(rgb[0] * a))
            p[i + 1] = min(255, p[i + 1] + int(rgb[1] * a))
            p[i + 2] = min(255, p[i + 2] + int(rgb[2] * a))

    def blend(self, x, y, rgb, a):
        """rgb over what is there at a (0..1)"""
        if 0 <= x < W and 0 <= y < H and a > 0:
            i = (y * W + x) * 3
            p = self.px
            a = min(1.0, a)
            for k in range(3):
                p[i + k] = int(p[i + k] * (1 - a) + rgb[k] * a)

    def cover(self, x, y, rgb, a):
        """(a wireframe's) the line's coverage here at least a"""
        if 0 <= x < W and 0 <= y < H and a > 0:
            i = y * W + x
            if a > self.cov[i]:
                self.cov[i] = a

    def span(self, y, xl, xr, rgb):
        """row y covered from xl to xr (real numbers), the end pixels in part"""
        if not 0 <= y < H or xr <= xl:
            return
        il, ir = int(math.floor(xl)), int(math.floor(xr))
        if il == ir:
            self.blend(il, y, rgb, xr - xl)
            return
        self.blend(il, y, rgb, 1 - (xl - il))
        self.row(y, rgb, il + 1, ir)
        self.blend(ir, y, rgb, xr - ir)

    def line(self, x0, y0, x1, y1, rgb, a=1.0, glow=0, plot=None):
        """an antialiased line (Wu's); glow: fainter copies that many pixels either side"""
        plot = plot or self.add
        shallow = abs(x1 - x0) >= abs(y1 - y0)
        for g in range(-glow, glow + 1):
            k = a if g == 0 else a * 0.35 / abs(g)
            if shallow:
                self._wu(x0, y0 + g, x1, y1 + g, rgb, k, plot)
            else:
                self._wu(x0 + g, y0, x1 + g, y1, rgb, k, plot)

    def _wu(self, x0, y0, x1, y1, rgb, a, plot):
        steep = abs(y1 - y0) > abs(x1 - x0)
        if steep:
            x0, y0, x1, y1 = y0, x0, y1, x1
        if x0 > x1:
            x0, x1, y0, y1 = x1, x0, y1, y0
        dx = x1 - x0
        grad = (y1 - y0) / dx if dx else 0.0
        # (only what is on the screen along the major axis is walked)
        xs, xe = max(int(round(x0)), -1), min(int(round(x1)), H if steep else W)
        y = y0 + grad * (xs - x0)
        for x in range(xs, xe + 1):
            fy = math.floor(y)
            f = y - fy
            iy = int(fy)
            if steep:
                plot(iy, x, rgb, a * (1 - f))
                plot(iy + 1, x, rgb, a * f)
            else:
                plot(x, iy, rgb, a * (1 - f))
                plot(x, iy + 1, rgb, a * f)
            y += grad

    def tri(self, a, b, c, rows):
        """a triangle filled (the pixels whose centres are in it) with rows[y]'s colour; lines under it gone"""
        (x0, y0), (x1, y1), (x2, y2) = sorted((a, b, c), key=lambda q: q[1])
        if y2 - y0 < 1e-9:
            return
        ys, ye = max(0, int(math.ceil(y0 - 0.5))), min(H, int(math.ceil(y2 - 0.5)))
        for y in range(ys, ye):
            yc = y + 0.5
            xa = x0 + (x2 - x0) * (yc - y0) / (y2 - y0)
            if yc < y1:
                xb = x0 + (x1 - x0) * (yc - y0) / (y1 - y0) if y1 > y0 else x1
            else:
                xb = x1 + (x2 - x1) * (yc - y1) / (y2 - y1) if y2 > y1 else x1
            if xa > xb:
                xa, xb = xb, xa
            il, ir = max(0, int(math.ceil(xa - 0.5))), min(W, int(math.ceil(xb - 0.5)))
            if ir > il:
                self.px[(y * W + il) * 3:(y * W + ir) * 3] = rows[y] * (ir - il)
                if self.cov is not None:
                    self.cov[y * W + il:y * W + ir] = ZEROS[:ir - il]

    def lay(self, rgb):
        """the wireframe's lines put on, in rgb"""
        cov, px = self.cov, self.px
        for i in range(W * H):
            a = cov[i]
            if a > 0.003:
                a = min(1.0, a)
                j = 3 * i
                for k in range(3):
                    px[j + k] = int(px[j + k] + (rgb[k] - px[j + k]) * a)
        self.cov = None

    def png(self, path):
        """filter Up on every row (a byte's difference from the one above, all of a row at once in a big integer)"""
        n = W * 3
        hi = int.from_bytes(b"\x80" * n, "big")
        lo = int.from_bytes(b"\x7f" * n, "big")
        rows, prev = [], 0
        for y in range(H):
            cur = int.from_bytes(self.px[y * n:(y + 1) * n], "big")
            d = ((cur | hi) - (prev & lo)) ^ ((cur ^ prev ^ hi) & hi)
            rows.append(b"\2" + (d & ((1 << (8 * n)) - 1)).to_bytes(n, "big"))
            prev = cur

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
        open(path, "wb").write(data)


ZEROS = array("f", bytes(4 * W))


def gradient(p, stops, y0, y1):
    """rows y0..y1 through the stops [(where 0..1, rgb), ...]; their colours, by row, returned"""
    out = {}
    for y in range(y0, y1):
        t = (y - y0) / max(1, y1 - y0 - 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / max(1e-9, stops[i + 1][0] - stops[i][0])
                c = mix(stops[i][1], stops[i + 1][1], u)
                p.row(y, c)
                out[y] = bytes(c)
                break
    return out


def stars(p, rng, n, y1, bright=1.0):
    for _ in range(n):
        x, y = rng.randrange(W), int(rng.random() ** 1.6 * y1)
        a = bright * (0.3 + 0.7 * rng.random()) * (1 - y / y1) ** 0.5
        c = rng.choice([(255, 255, 255), (200, 220, 255), (255, 200, 240)])
        p.add(x, y, c, a)
        if rng.random() < 0.08:                 # (a brighter one: a small cross)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                p.add(x + dx, y + dy, c, a * 0.5)


def _profile(pd, sp, lw, gw, lo, width):
    """a grid line's light at pd pixels from its middle (lw wide, glowing gw), the lines sp apart: sharp from
    lo + width pixels apart, below that blending into their average (closer, they would beat with the pixels)"""
    avg = (lw + 1.128 * gw) / sp        # (the core's width and the glow's integral, over the spacing)
    if avg > 0.2:
        avg = 0.2
    w = (sp - lo) / width
    if w <= 0:
        return avg
    core = lw * 0.5 + 0.5 - pd          # (the line's overlap with the pixel: a box filter)
    if core > lw:
        core = lw
    core = 0.0 if core < 0 else 1.0 if core > 1 else core
    sharp = core + (0.45 * math.exp(-pd * pd / (2 * gw * gw)) if pd < 4 * gw else 0.0)
    if sharp > 1:
        sharp = 1.0
    return sharp if w >= 1 else sharp * w + avg * (1 - w)


def floor(p, horizon, rgb, cell=140, depth=2.2, x_mid=W / 2, shift=0.3):
    """a neon grid on a flat floor from the horizon down, worked out per pixel: lines across and toward the
    vanishing point, the eye one unit above the floor, a cell `cell` pixels wide at the bottom row. A line is as
    wide as it would be at its depth (2.6 pixels at the bottom row), at least a third of a pixel."""
    dmax = H - horizon
    f = depth * dmax                    # (the focal length, pixels)
    s = cell / dmax                     # (a cell's side, in eye heights)
    cw_v, cg_v = 2.6 / cell, 3.6 / cell                 # (a line's width and glow over its spacing: the columns',
    sph_max = s * dmax * dmax / f
    cw_h, cg_h = 2.6 / sph_max, 3.6 / sph_max           #  and the rows')
    r, g, b = rgb
    px = p.px
    for y in range(horizon + 1, H):
        d = y - horizon + 0.5
        t = d / dmax
        z = f / d
        sph = s * d * d / f             # (the rows' spacing here, pixels)
        u = z / s + shift
        ih = _profile(abs(u - round(u)) * sph, sph, max(0.35, cw_h * sph), max(0.6, cg_h * sph), 1.5, 3.0)
        spv = s * d                     # (the columns' spacing along the row)
        inv = 1.0 / spv
        fade = (1 - math.exp(-d / 18.0)) * (0.7 + 0.3 * t)
        haze = 0.55 * math.exp(-d / 26.0)   # (the horizon's glow)
        base = y * W * 3
        d2 = d * d
        for x in range(W):
            dx = x + 0.5 - x_mid
            v = dx * inv
            sp = spv * d / math.sqrt(dx * dx + d2)      # (across the line: the side ones lie nearly flat)
            iv = _profile(abs(v - round(v)) * sp, sp, max(0.35, cw_v * sp), max(0.6, cg_v * sp), 4.0, 12.0)
            a = (1 - (1 - ih) * (1 - iv)) * fade + haze
            if a > 0.004:
                if a > 1:
                    a = 1.0
                i = base + 3 * x
                c = px[i] + int(r * a)
                px[i] = c if c < 256 else 255
                c = px[i + 1] + int(g * a)
                px[i + 1] = c if c < 256 else 255
                c = px[i + 2] + int(b * a)
                px[i + 2] = c if c < 256 else 255


def disc(p, cx, cy, r, colour, y_end=H, gap=None):
    """a disc, its rows' colour colour(t) (t 0 at its top, 1 at its bottom), edges antialiased, cut at y_end, rows
    gap(y) true left out"""
    for y in range(max(0, int(cy - r)), min(H, int(cy + r) + 1, y_end)):
        dy = y + 0.5 - cy
        if dy * dy >= r * r or (gap and gap(y)):
            continue
        w = math.sqrt(r * r - dy * dy)
        p.span(y, cx - w, cx + w, colour((dy + r) / (2 * r)))


def sun(p, cx, cy, r, top, bottom, horizon, stripes=6, glow=(255, 90, 150), start=0.1):
    """the sun, top to bottom colours, cut at the horizon, striped from start (of its radius) above its middle down:
    each band's gap at its bottom, wider lower down"""
    y0 = cy - start * r
    span = max(1.0, min(cy + r, horizon) - y0)

    def gap(y):
        if y < y0:
            return False
        pos = (y - y0) / span * stripes
        i = int(pos)
        return pos - i > 1 - (0.14 + 0.07 * i)
    p.glow(cx, cy, r * 1.7, glow, 0.35)
    disc(p, cx, cy, r, lambda t: mix(top, bottom, t), horizon, gap)


def _glow(self, cx, cy, r, rgb, a):
    """a soft disc of light: a at its middle, nothing at r"""
    for y in range(max(0, int(cy - r)), min(H, int(cy + r) + 1)):
        dy = y - cy
        span = r * r - dy * dy
        if span <= 0:
            continue
        w = math.sqrt(span)
        for x in range(max(0, int(cx - w)), min(W, int(cx + w) + 1)):
            dd = math.sqrt((x - cx) ** 2 + dy * dy) / r
            if dd < 1:
                self.add(x, y, rgb, a * (1 - dd) ** 2)


Pic.glow = _glow


def horizon_line(p, horizon, rgb):
    p.line(0, horizon, W - 1, horizon, rgb, 1.0, 3)


def sunset(path):
    rng = random.Random(1)
    p = Pic()
    horizon = int(H * 0.58)
    gradient(p, [(0, (10, 3, 34)), (0.45, (64, 10, 92)), (0.8, (214, 38, 120)), (1, (255, 128, 72))], 0, horizon)
    stars(p, rng, 500, int(horizon * 0.6))
    sun(p, W / 2, horizon - 110, 290, (255, 236, 96), (255, 56, 150), horizon, 7, start=0.3)
    gradient(p, [(0, (40, 8, 62)), (1, (8, 0, 20))], horizon, H)
    floor(p, horizon, (255, 40, 210))
    horizon_line(p, horizon, (255, 150, 220))
    p.png(path)


def nightcity(path):
    rng = random.Random(2)
    p = Pic()
    horizon = int(H * 0.7)
    gradient(p, [(0, (4, 4, 24)), (0.6, (34, 8, 66)), (1, (110, 20, 110))], 0, horizon)
    stars(p, rng, 400, int(horizon * 0.5), 0.8)
    sun_x = W * 0.6
    sun(p, sun_x, horizon - 300, 210, (255, 130, 200), (190, 40, 160), horizon, 6, (255, 60, 170), 0.2)
    for layer, shade, hmin, hmax, lit in ((0, (44, 26, 84), 90, 330, 0.18), (1, (10, 6, 26), 140, 470, 0.4)):
        x = -rng.randrange(60)
        while x < W:
            bw, bh = rng.randrange(50, 150), rng.randrange(hmin, hmax)
            if layer:                           # (the near ones lower before the sun)
                bh = int(bh * (0.4 + 0.6 * smooth(140, 420, abs(x + bw / 2 - sun_x))))
            top = horizon - bh
            for y in range(top, horizon):
                p.row(y, shade, x, x + bw)
            if rng.random() < 0.35:             # (a narrower storey on top)
                inset = bw // 4
                for y in range(top - 24, top):
                    p.row(y, shade, x + inset, x + bw - inset)
            for wy in range(top + 10, horizon - 8, 14):     # (its windows, some lit)
                for wx in range(x + 6, x + bw - 8, 11):
                    if rng.random() < lit:
                        c = rng.choice([(255, 220, 120), (120, 230, 255), (255, 120, 220)])
                        if not layer:
                            c = mix((44, 26, 84), c, 0.55)
                        for yy in range(wy, wy + 6):
                            p.row(yy, c, wx, wx + 5)
            if layer and rng.random() < 0.3:    # (an antenna's light)
                p.glow(x + bw / 2, top - 6, 7, (255, 40, 80), 0.9)
            x += bw + rng.randrange(0, 18)
    gradient(p, [(0, (34, 8, 56)), (1, (6, 0, 16))], horizon, H)
    floor(p, horizon, (60, 220, 255), cell=150)
    horizon_line(p, horizon, (120, 240, 255))
    p.png(path)


def ridge(rng, levels=8, rough=0.5):
    """a range's outline, 0..1 across the screen: midpoint displacement"""
    pts = [rng.random(), rng.random()]
    amp = 0.7
    for _ in range(levels):
        out = []
        for a, b in zip(pts, pts[1:]):
            out += [a, (a + b) / 2 + (rng.random() - 0.5) * amp]
        pts = out + [pts[-1]]
        amp *= rough
    lo, hi = min(pts), max(pts)
    return [(q - lo) / (hi - lo) for q in pts]


def mountains(p, rng, horizon, height, valley, fill, edge):
    """a range standing on the horizon, height pixels at most, lower toward the middle (valley: its half-width,
    pixels), filled, its ridge lit"""
    r = ridge(rng)
    n = len(r) - 1
    tops = []
    for i, q in enumerate(r):
        x = W * i / n
        e = 0.12 + 0.88 * smooth(valley * 0.4, valley * 1.6, abs(x - W / 2))
        tops.append((x, horizon - height * (0.25 + 0.75 * q) * e))
    for (x0, y0), (x1, y1) in zip(tops, tops[1:]):
        for x in range(int(x0), min(W, int(x1) + 1)):
            y = y0 + (y1 - y0) * (x + 0.5 - x0) / (x1 - x0)
            yi = int(math.ceil(y))
            for yy in range(max(0, yi), horizon):
                i = (yy * W + x) * 3
                p.px[i:i + 3] = fill
            p.blend(x, yi - 1, fill, yi - y)
    for (x0, y0), (x1, y1) in zip(tops, tops[1:]):
        p.line(x0, y0, x1, y1, edge, 0.9, 1)


def neonroad(path):
    rng = random.Random(3)
    p = Pic()
    horizon = int(H * 0.55)
    gradient(p, [(0, (8, 18, 58)), (0.55, (70, 30, 120)), (1, (230, 70, 150))], 0, horizon)
    stars(p, rng, 300, int(horizon * 0.5), 0.7)
    sun(p, W / 2, horizon - 150, 190, (255, 214, 92), (255, 70, 130), horizon, 6, (255, 120, 120))
    mountains(p, rng, horizon, 330, 300, bytes((42, 14, 74)), (255, 90, 220))
    mountains(p, rng, horizon, 170, 420, bytes((20, 6, 42)), (90, 230, 255))
    gradient(p, [(0, (28, 8, 52)), (1, (6, 0, 18))], horizon, H)
    floor(p, horizon, (200, 40, 220), cell=170)
    dmax = H - horizon
    f, s = 2.2 * dmax, 170 / dmax
    road = 470                                  # (its half-width at the bottom row, pixels)
    for y in range(horizon + 1, H):             # the road (wider as it comes nearer), its middle's dashes
        d = y - horizon + 0.5
        t = d / dmax
        half = road * t
        p.span(y, W / 2 - half, W / 2 + half, mix((22, 16, 40), (12, 10, 24), t))
        u = f / d / s * 0.5
        if u - math.floor(u) < 0.45 and t > 0.02:
            w = 0.6 + 4.0 * t
            p.span(y, W / 2 - w, W / 2 + w, mix((120, 100, 60), (255, 226, 110), min(1.0, t * 3)))
    for side in (-1, 1):
        p.line(W / 2, horizon, W / 2 + side * road, H - 1, (90, 240, 255), 1.0, 3)
    horizon_line(p, horizon, (255, 160, 220))
    p.png(path)


def value_noise(rng, n=64):
    g = [[rng.random() for _ in range(n)] for _ in range(n)]

    def at(x, y):
        xi, yi = int(math.floor(x)), int(math.floor(y))
        fx, fy = x - xi, y - yi
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a, b = g[yi % n][xi % n], g[yi % n][(xi + 1) % n]
        c, d = g[(yi + 1) % n][xi % n], g[(yi + 1) % n][(xi + 1) % n]
        return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy
    return at


def hills(p, rng, horizon, rgb, rows_col, cell=150, depth=2.2, z_far=30.0):
    """wireframe hills either side of a flat valley, the floor's grid's lines continued (the same cell and eye),
    from the far row forward: each cell filled with its rows' floor colour, then its edges"""
    nz = value_noise(rng)
    dmax = H - horizon
    f = depth * dmax
    s = cell / dmax
    z_near = f / dmax * 0.9

    def height(X, Z):
        e = smooth(1.2, 5.0, abs(X))
        if e <= 0:
            return 0.0
        q = 0.65 * nz(X * 0.5 + 3, Z * 0.5) + 0.35 * nz(X * 1.3 + 17, Z * 1.3 + 9)
        return e * (0.3 + 3.0 * q * q)
    # (the floor's rows are at z = (k - 0.3) s: floor()'s shift; the mesh's on the same ones)
    k0 = int(math.ceil(z_near / s + 0.3))
    zs = []
    k = k0
    while (k - 0.3) * s <= z_far:
        zs.append((k - 0.3) * s)
        k += 1
    m_max = int((W / 2) * z_far / f / s) + 2
    grid = []
    for Z in zs:
        grid.append([(W / 2 + m * s * f / Z, horizon + (1 - height(m * s, Z)) * f / Z, height(m * s, Z))
                     for m in range(-m_max, m_max + 1)])
    p.cov = array("f", bytes(4 * W * H))
    rows = {y: rows_col.get(y, rows_col[horizon]) for y in range(H)}
    for k in range(len(zs) - 2, -1, -1):
        zf = zs[k + 1]
        a_line = 0.35 + 0.65 * (z_near / zf) ** 0.6
        glow = 1 if zf < z_far * 0.4 else 0
        rn, rf = grid[k], grid[k + 1]
        for j in range(len(rn) - 1):
            a, b, c, d = rn[j], rn[j + 1], rf[j + 1], rf[j]
            if max(a[2], b[2], c[2], d[2]) <= 0:
                continue                        # (flat: the floor's grid is there already)
            xs = (a[0], b[0], c[0], d[0])
            if max(xs) < -4 or min(xs) > W + 4 or min(a[1], b[1], c[1], d[1]) > H + 4:
                continue
            p.tri(a[:2], b[:2], c[:2], rows)
            p.tri(a[:2], c[:2], d[:2], rows)
            for u, v in ((a, b), (b, c), (c, d), (d, a)):
                p.line(u[0], u[1], v[0], v[1], rgb, a_line, glow, p.cover)
    p.lay(rgb)


def stargrid(path):
    rng = random.Random(4)
    p = Pic()
    horizon = int(H * 0.62)
    sky = gradient(p, [(0, (2, 2, 12)), (0.7, (14, 6, 36)), (1, (44, 12, 76))], 0, horizon)
    for _ in range(9):                          # a nebula: soft light in places
        p.glow(rng.randrange(W), rng.randrange(int(horizon * 0.7)), rng.randrange(140, 320),
               rng.choice([(120, 40, 200), (40, 120, 220), (200, 40, 160)]), 0.2)
    stars(p, rng, 1800, horizon, 1.0)
    cx, cy, r = W * 0.76, H * 0.19, 74          # a ringed planet: the ring's far half, the planet, the near half

    def ring(near):
        for rx in range(122, 162):
            ry = rx * 0.2
            a_band = 0.01 if 138 <= rx < 142 else 0.04 if rx < 138 else 0.028
            for k in range(2880):
                ang = math.radians(k / 8)
                if (math.sin(ang) > 0) == near:
                    p.add(int(cx + math.cos(ang) * rx), int(cy + math.sin(ang) * ry), (210, 180, 255), a_band)
    ring(False)
    disc(p, cx, cy, r, lambda t: mix((176, 110, 230), (40, 18, 92), t ** 0.8))
    for k in range(3):                          # (its bands)
        yb = cy - r * 0.5 + k * r * 0.45
        for y in range(int(yb), int(yb + 7)):
            dy = y + 0.5 - cy
            if dy * dy < r * r:
                w = math.sqrt(r * r - dy * dy)
                for x in range(int(cx - w), int(cx + w)):
                    p.add(x, y, (255, 160, 230), 0.08)
    ring(True)
    floor_rows = gradient(p, [(0, (14, 4, 34)), (1, (0, 0, 8))], horizon, H)
    floor_rows[horizon - 1] = floor_rows[horizon]
    floor(p, horizon, (60, 230, 255), cell=150)
    sky_fill = dict(floor_rows)
    for y in range(horizon):                    # (hills against the sky: the horizon's floor colour)
        sky_fill[y] = floor_rows[horizon]
    horizon_line(p, horizon, (140, 240, 255))
    hills(p, rng, horizon, (60, 230, 255), sky_fill, cell=150)
    del sky
    p.png(path)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkwalls.py OUTDIR")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, fn in (("SUNSET.PNG", sunset), ("NIGHTCTY.PNG", nightcity), ("NEONROAD.PNG", neonroad),
                     ("STARGRID.PNG", stargrid)):
        fn(os.path.join(out, name))
        print("mkwalls: %s %d bytes" % (name, os.path.getsize(os.path.join(out, name))))


if __name__ == "__main__":
    main()

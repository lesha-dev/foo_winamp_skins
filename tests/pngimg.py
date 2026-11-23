"""Minimal PNG reading/writing and pixel comparison for skintest (Python 3, no dependencies).

Images are (w, h, px) with px a bytearray of RGBA pixels, row by row, colour premultiplied by alpha (as the
harness's surfaces): captures and harness renders keep their transparency, and comparisons count alpha too.
view() turns one into RGB over a checkerboard for looking at; save() writes RGB."""
import struct
import zlib


def load(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    i, idat, plte = 8, [], None
    w = h = depth = ctype = 0
    while i < len(data):
        n, t = struct.unpack('>I4s', data[i:i + 8])
        c = data[i + 8:i + 8 + n]
        i += 12 + n
        if t == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', c)
            if depth != 8 or interlace:
                raise ValueError(f'{path}: unsupported PNG (depth {depth}, interlace {interlace})')
        elif t == b'PLTE':
            plte = c
        elif t == b'IDAT':
            idat.append(c)
        elif t == b'IEND':
            break
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    prev = bytearray(stride)
    out = bytearray(w * h * 4)
    o = 0
    for y in range(h):
        f = raw[o]
        line = bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        base = y * w * 4
        if ctype == 6:
            out[base:base + w * 4] = line
            if line[3::4].count(255) != w:  # premultiply the pixels that aren't opaque
                for x in range(w):
                    a = line[x * 4 + 3]
                    if a != 255:
                        q = base + x * 4
                        out[q:q + 3] = bytes((c * a + 127) // 255 for c in line[x * 4:x * 4 + 3])
            continue
        row = bytearray(b'\xff' * (w * 4))
        if ctype == 2:
            row[0::4], row[1::4], row[2::4] = line[0::3], line[1::3], line[2::3]
        elif ctype == 3:
            for x in range(w):
                k = line[x] * 3
                row[x * 4:x * 4 + 3] = plte[k:k + 3]
        else:  # gray, gray + alpha (alpha dropped)
            v = line[0::bpp]
            row[0::4], row[1::4], row[2::4] = v, v, v
        out[base:base + w * 4] = row
    return w, h, out


_CHECKER = (0x64, 0x9c)  # the gray squares (8 px) transparency is shown over


def view(img):
    """The image as RGB (w, h, px with RGB triples) over a gray checkerboard, for looking at."""
    w, h, px = img
    out = bytearray(w * h * 3)
    out[0::3], out[1::3], out[2::3] = px[0::4], px[1::4], px[2::4]
    if px[3::4].count(255) == w * h:
        return w, h, out
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 4
            a = px[i + 3]
            if a != 255:
                k = (255 - a) * _CHECKER[((x >> 3) + (y >> 3)) & 1] // 255
                o = (y * w + x) * 3
                out[o:o + 3] = bytes(min(255, c + k) for c in px[i:i + 3])
    return w, h, out


def save(path, w, h, px):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += px[y * w * 3:(y + 1) * w * 3]

    def chunk(t, c):
        return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


def masked(w, h, rects):
    """A bytearray, 1 where a pixel is masked (excluded from comparisons); rects are (x, y, w, h)."""
    m = bytearray(w * h)
    for rx, ry, rw, rh in rects:
        for y in range(max(0, ry), min(h, ry + rh)):
            x0, x1 = max(0, rx), min(w, rx + rw)
            if x1 > x0:
                m[y * w + x0:y * w + x1] = b'\x01' * (x1 - x0)
    return m


def diff(a, b, rects=(), threshold=24):
    """Compares images a and b (as load() returns) over the union of their sizes, as modern_harness does: a pixel
    differs when a channel (alpha included) differs by more than threshold; outside an image a pixel is transparent
    (a window larger than the other only by transparent rows, e.g. Winamp's shaded main window, doesn't differ). Masked pixels
    (rects, in window coordinates) don't count. Returns (differing, counted, boxes): boxes are the bounding
    boxes (x0, y0, x1, y1, pixels) of clusters of differing pixels, largest first."""
    pts, counted = diff_points(a, b, rects, threshold)
    return len(pts), counted, clusters(pts)


def diff_points(a, b, rects=(), threshold=24):
    """diff() without the clustering: (list of differing (x, y), counted)."""
    aw, ah, ap = a
    bw, bh, bp = b
    w, h = max(aw, bw), max(ah, bh)
    m = masked(w, h, rects) if rects else None
    pts = []
    counted = 0
    for y in range(h):
        if m is None and y < ah and y < bh and aw == bw and ap[y * aw * 4:(y + 1) * aw * 4] == bp[y * bw * 4:(y + 1) * bw * 4]:
            counted += w
            continue
        for x in range(w):
            if m is not None and m[y * w + x]:
                continue
            counted += 1
            if x >= aw or y >= ah or x >= bw or y >= bh:  # against transparent (premultiplied: all zero)
                q = ap[(y * aw + x) * 4:(y * aw + x) * 4 + 4] if x < aw and y < ah else bp[(y * bw + x) * 4:(y * bw + x) * 4 + 4]
                if q and max(q) > threshold:  # outside both images: nothing either side
                    pts.append((x, y))
                continue
            i, j = (y * aw + x) * 4, (y * bw + x) * 4
            if (abs(ap[i] - bp[j]) > threshold or abs(ap[i + 1] - bp[j + 1]) > threshold or abs(ap[i + 2] - bp[j + 2]) > threshold
                    or abs(ap[i + 3] - bp[j + 3]) > threshold):
                pts.append((x, y))
    return pts, counted


def clusters(pts, gap=3, limit=4000):
    boxes = []
    for x, y in pts[:limit]:
        for bx in boxes:
            if bx[0] - gap <= x <= bx[2] + gap and bx[1] - gap <= y <= bx[3] + gap:
                bx[0], bx[1], bx[2], bx[3] = min(bx[0], x), min(bx[1], y), max(bx[2], x), max(bx[3], y)
                bx[4] += 1
                break
        else:
            boxes.append([x, y, x, y, 1])
    # merge overlapping boxes
    merged = True
    while merged:
        merged = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                p, q = boxes[i], boxes[j]
                if p[0] - gap <= q[2] and q[0] - gap <= p[2] and p[1] - gap <= q[3] and q[1] - gap <= p[3]:
                    boxes[i] = [min(p[0], q[0]), min(p[1], q[1]), max(p[2], q[2]), max(p[3], q[3]), p[4] + q[4]]
                    del boxes[j]
                    merged = True
                    break
            if merged:
                break
    boxes.sort(key=lambda bx: -bx[4])
    return [tuple(bx) for bx in boxes]


def crop_pair(a, b, box, scale, pad=4):
    """box (x0, y0, x1, y1) of both images, padded, scaled up; a above b with a 2-px magenta line between.
    Returns (w, h, px) in RGB."""
    aw, ah, ap = a = view(a)
    bw, bh, bp = b = view(b)
    x0, y0 = max(0, box[0] - pad), max(0, box[1] - pad)
    x1, y1 = min(max(aw, bw), box[2] + 1 + pad), min(max(ah, bh), box[3] + 1 + pad)
    cw, ch = x1 - x0, y1 - y0
    ow, oh = cw * scale, ch * scale * 2 + 2
    out = bytearray(b'\xff\x00\xff' * (ow * oh))
    for k, (iw, ih, ip) in enumerate((a, b)):
        top = k * (ch * scale + 2)
        for y in range(ch):
            sy = y0 + y
            row = bytearray()
            for x in range(cw):
                sx = x0 + x
                c = ip[(sy * iw + sx) * 3:(sy * iw + sx) * 3 + 3] if sx < iw and sy < ih else b'\xff\x00\xff'
                row += bytes(c) * scale
            for r in range(scale):
                o = ((top + y * scale + r) * ow) * 3
                out[o:o + ow * 3] = row
    return ow, oh, out


# --- labelled comparison sheets ----------------------------------------------------------------------------

# 5x8 bitmap font (rows of 5 bits, top to bottom; the 8th row is for descenders). Unknown characters: '?'.
_GLYPHS = {
    ' ': '', '.': '0 0 0 0 0 01100 01100', ',': '0 0 0 0 01100 00100 01000', ':': '0 01100 01100 0 01100 01100',
    ';': '0 01100 01100 0 01100 00100 01000', '-': '0 0 0 11111', '_': '0 0 0 0 0 0 11111', '+': '0 00100 00100 11111 00100 00100',
    '=': '0 0 11111 0 11111', '(': '00010 00100 01000 01000 01000 00100 00010', ')': '01000 00100 00010 00010 00010 00100 01000',
    '[': '01110 01000 01000 01000 01000 01000 01110', ']': '01110 00010 00010 00010 00010 00010 01110',
    '%': '11000 11001 00010 00100 01000 10011 00011', '/': '0 00001 00010 00100 01000 10000', '~': '0 0 01000 10101 00010',
    '>': '01000 00100 00010 00001 00010 00100 01000', '<': '00010 00100 01000 10000 01000 00100 00010',
    "'": '00100 00100 01000', '!': '00100 00100 00100 00100 00100 0 00100', '#': '01010 01010 11111 01010 11111 01010 01010',
    '?': '01110 10001 00001 00010 00100 0 00100',
    '0': '01110 10001 10011 10101 11001 10001 01110', '1': '00100 01100 00100 00100 00100 00100 01110',
    '2': '01110 10001 00001 00010 00100 01000 11111', '3': '11111 00010 00100 00010 00001 10001 01110',
    '4': '00010 00110 01010 10010 11111 00010 00010', '5': '11111 10000 11110 00001 00001 10001 01110',
    '6': '00110 01000 10000 11110 10001 10001 01110', '7': '11111 00001 00010 00100 01000 01000 01000',
    '8': '01110 10001 10001 01110 10001 10001 01110', '9': '01110 10001 10001 01111 00001 00010 01100',
    'a': '0 0 01110 00001 01111 10001 01111', 'b': '10000 10000 10110 11001 10001 10001 11110',
    'c': '0 0 01110 10000 10000 10001 01110', 'd': '00001 00001 01101 10011 10001 10001 01111',
    'e': '0 0 01110 10001 11111 10000 01110', 'f': '00110 01001 01000 11100 01000 01000 01000',
    'g': '0 0 01111 10001 10001 01111 00001 01110', 'h': '10000 10000 10110 11001 10001 10001 10001',
    'i': '00100 0 01100 00100 00100 00100 01110', 'j': '00010 0 00110 00010 00010 00010 10010 01100',
    'k': '10000 10000 10010 10100 11000 10100 10010', 'l': '01100 00100 00100 00100 00100 00100 01110',
    'm': '0 0 11010 10101 10101 10101 10101', 'n': '0 0 10110 11001 10001 10001 10001',
    'o': '0 0 01110 10001 10001 10001 01110', 'p': '0 0 11110 10001 10001 11110 10000 10000',
    'q': '0 0 01111 10001 10001 01111 00001 00001', 'r': '0 0 10110 11001 10000 10000 10000',
    's': '0 0 01111 10000 01110 00001 11110', 't': '01000 01000 11100 01000 01000 01001 00110',
    'u': '0 0 10001 10001 10001 10011 01101', 'v': '0 0 10001 10001 10001 01010 00100',
    'w': '0 0 10001 10001 10101 10101 01010', 'x': '0 0 10001 01010 00100 01010 10001',
    'y': '0 0 10001 10001 10001 01111 00001 01110', 'z': '0 0 11111 00010 00100 01000 11111',
    'A': '01110 10001 10001 11111 10001 10001 10001', 'B': '11110 10001 10001 11110 10001 10001 11110',
    'C': '01110 10001 10000 10000 10000 10001 01110', 'D': '11100 10010 10001 10001 10001 10010 11100',
    'E': '11111 10000 10000 11110 10000 10000 11111', 'F': '11111 10000 10000 11110 10000 10000 10000',
    'G': '01110 10001 10000 10111 10001 10001 01111', 'H': '10001 10001 10001 11111 10001 10001 10001',
    'I': '01110 00100 00100 00100 00100 00100 01110', 'J': '00111 00010 00010 00010 00010 10010 01100',
    'K': '10001 10010 10100 11000 10100 10010 10001', 'L': '10000 10000 10000 10000 10000 10000 11111',
    'M': '10001 11011 10101 10101 10001 10001 10001', 'N': '10001 10001 11001 10101 10011 10001 10001',
    'O': '01110 10001 10001 10001 10001 10001 01110', 'P': '11110 10001 10001 11110 10000 10000 10000',
    'Q': '01110 10001 10001 10001 10101 10010 01101', 'R': '11110 10001 10001 11110 10100 10010 10001',
    'S': '01111 10000 10000 01110 00001 00001 11110', 'T': '11111 00100 00100 00100 00100 00100 00100',
    'U': '10001 10001 10001 10001 10001 10001 01110', 'V': '10001 10001 10001 10001 10001 01010 00100',
    'W': '10001 10001 10001 10101 10101 10101 01010', 'X': '10001 10001 01010 00100 01010 10001 10001',
    'Y': '10001 10001 01010 00100 00100 00100 00100', 'Z': '11111 00001 00010 00100 01000 10000 11111',
}
_FONT = {c: [int(r, 2) for r in (g.split() + ['0'] * 8)[:8]] for c, g in _GLYPHS.items()}
GLYPH_W, GLYPH_H = 6, 8  # advance and height in pixels


def text_width(s):
    return max(0, len(s) * GLYPH_W - 1)


def draw_text(w, h, px, x, y, s, color=b'\xff\xff\xff'):
    """Draws s with its top-left corner at (x, y) into the image (w, h, px), clipped."""
    for ch in s:
        for r, bits in enumerate(_FONT.get(ch, _FONT['?'])):
            for c in range(5):
                if bits & (16 >> c) and 0 <= x + c < w and 0 <= y + r < h:
                    o = ((y + r) * w + x + c) * 3
                    px[o:o + 3] = color
        x += GLYPH_W


OUTSIDE = b'\xff\x00\xff'  # outside one of the images (sizes differ)
_DIM = bytes(64 + v // 3 for v in range(256))  # the difference panel: the image faded, differences on top


def sheet(cur, ref, labels, rects=(), threshold=24, pts=None):
    """One image (RGB) of three panels side by side: cur, ref and their difference (cur faded, differing pixels red,
    masked rects blue, pixels outside one of the images magenta), each with its label below.
    labels: three strings. pts: the differing pixels when already known (diff_points). Returns (w, h, px)."""
    if pts is None:
        pts, _ = diff_points(ref, cur, rects, threshold)
    w, h = max(cur[0], ref[0]), max(cur[1], ref[1])
    dpx = bytearray(OUTSIDE * (w * h))
    cur, ref = view(cur), view(ref)
    cw, ch, cp = cur
    faded = cp.translate(_DIM)
    for y in range(min(ch, ref[1])):
        n = min(cw, ref[0])
        dpx[y * w * 3:(y * w + n) * 3] = faded[y * cw * 3:(y * cw + n) * 3]
    for rx, ry, rw, rh in rects:
        for y in range(max(0, ry), min(h, ry + rh)):
            x0, x1 = max(0, rx), min(w, rx + rw)
            if x1 > x0:
                dpx[(y * w + x0) * 3:(y * w + x1) * 3] = b'\x20\x40\xa0' * (x1 - x0)
    for x, y in pts:
        if x < cw and y < ch and x < ref[0] and y < ref[1]:
            dpx[(y * w + x) * 3:(y * w + x) * 3 + 3] = b'\xff\x00\x00'
    panels = [cur, ref, (w, h, dpx)]
    gap, pad = 6, 4
    cols = [max(p[0], text_width(s)) for p, s in zip(panels, labels)]
    ow = sum(cols) + gap * (len(cols) + 1)
    oh = pad + h + pad + GLYPH_H + pad
    out = bytearray(b'\x30\x30\x30' * (ow * oh))
    x = gap
    for (pw, ph, pp), cwid, label in zip(panels, cols, labels):
        for y in range(ph):
            o = ((pad + y) * ow + x) * 3
            out[o:o + pw * 3] = pp[y * pw * 3:(y + 1) * pw * 3]
        draw_text(ow, oh, out, x, pad + h + pad, label)
        x += cwid + gap
    return ow, oh, out

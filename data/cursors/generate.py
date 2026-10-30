# Dibuja los cursores del estilo "Paint.NET" de Kaint (pixel art, 1 píxel = 1 píxel de pantalla) y
# sus puntos activos (hotspots.json, que se copian a Icons::pixelCursor y a la web):
#   python3 data/cursors/generate.py data/cursors && cp data/cursors/*.png KaintHTML/assets/cursors/
# Las manos son las de las capturas de referencia (abierta y agarrando).
import json, math, sys
from PIL import Image

PAL = {
    'K': (0, 0, 0, 255), 'k': (0, 0, 0, 150), 'W': (255, 255, 255, 255), 'l': (205, 205, 205, 255),
    'g': (150, 150, 150, 255), 'd': (70, 70, 70, 255), 'Y': (250, 214, 64, 255), 'y': (214, 168, 34, 255),
    'P': (238, 128, 140, 255), 'O': (232, 196, 146, 255), 'B': (70, 130, 210, 255), 'R': (214, 64, 54, 255),
}
H_DARK = (0, 0, 0, 70)      # halo de la cruz (se ve sobre blanco)
H_LIGHT = (255, 255, 255, 170)  # halo de los iconos (se ven sobre oscuro)

def blank(w=32, h=32):
    return [['.'] * w for _ in range(h)]

def to_image(grid):
    h, w = len(grid), len(grid[0])
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            c = grid[y][x]
            if c in PAL:
                im.putpixel((x, y), PAL[c])
    return im

def halo(im, color):
    # Anillo de 1 px alrededor de lo opaco (8 vecinos).
    w, h = im.size
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    src = im.load()
    for y in range(h):
        for x in range(w):
            if src[x, y][3]:
                continue
            if any(0 <= x + dx < w and 0 <= y + dy < h and src[x + dx, y + dy][3] > 100
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                out.putpixel((x, y), color)
    out.alpha_composite(im)
    return out

def cross(cx, cy, arm=7, w=32, h=32):
    g = blank(w, h)
    for d in range(-arm, arm + 1):
        for s in (-1, 0, 1):
            for (x, y) in ((cx + d, cy + s), (cx + s, cy + d)):
                g[y][x] = 'W'
    for d in range(-arm + 1, arm):
        if d == 0:
            continue
        g[cy][cx + d] = 'k'
        g[cy + d][cx] = 'k'
    g[cy][cx] = '.'
    return halo(to_image(g), H_DARK)

def icon(art, w=32, h=32, at=(0, 0)):
    g = blank(w, h)
    for j, row in enumerate(art):
        for i, c in enumerate(row):
            if c != '.':
                g[at[1] + j][at[0] + i] = c
    return halo(to_image(g), H_LIGHT)

def shape(inside, color, w=32, h=32, outline='K'):
    # Rellena lo que cumple 'inside' (x, y) con 'color' y le pone borde negro donde toca el exterior.
    g = blank(w, h)
    mask = [[inside(x, y) for x in range(w)] for y in range(h)]
    for y in range(h):
        for x in range(w):
            if not mask[y][x]:
                continue
            edge = any(not (0 <= x + dx < w and 0 <= y + dy < h and mask[y + dy][x + dx])
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            g[y][x] = outline if edge else (color(x, y) if callable(color) else color)
    return g

def over(*layers):
    out = layers[0].copy()
    for l in layers[1:]:
        out.alpha_composite(l)
    return out

cursors = {}

# Cruz sola (pincel, lazo, degradado, línea y formas).
C = (8, 8)
cursors['cross'] = (cross(*C), C)

# Selección rectangular: cruz y un rectángulo de hormigas arriba a la derecha.
C = (8, 14)
g = blank()
x0, y0, x1, y1 = 11, 2, 21, 10
for x in range(x0, x1 + 1):
    for y in range(y0, y1 + 1):
        if x in (x0, x1) or y in (y0, y1):
            g[y][x] = 'K' if ((x + y) // 2) % 2 == 0 else 'W'
cursors['rectselect'] = (over(halo(to_image(g), H_DARK), cross(*C)), C)

# Selección elíptica: cruz y un círculo punteado.
g = blank()
ex, ey, r = 16.0, 6.0, 5.0
pts = []
for a in range(0, 360, 2):
    x = int(round(ex + r * math.cos(math.radians(a)))); y = int(round(ey + r * math.sin(math.radians(a))))
    if (x, y) not in pts:
        pts.append((x, y))
for i, (x, y) in enumerate(pts):
    g[y][x] = 'K' if (i // 2) % 2 == 0 else 'W'
cursors['ellipseselect'] = (over(halo(to_image(g), H_DARK), cross(*C)), C)

# Varita mágica: cruz arriba a la izquierda y la varita bajando hacia la derecha.
C = (8, 8)
wand = shape(lambda x, y: 11 <= (x + y) / 2 <= 19.5 and abs(x - y) <= 1.0, lambda x, y: 'l' if (x + y) % 4 < 2 else 'W')
cursors['magicwand'] = (over(halo(to_image(wand), H_LIGHT), cross(*C)), C)

# Cubo de pintura: el cubo inclinado (un rombo) con su asa y la pintura, y la cruz abajo a la
# derecha, donde cae.
C = (19, 20)
bucket = [
    ".......KK.......",
    "......K..K......",
    ".....K....K.....",
    "....K..KK..K....",
    "....K.KWWK.K....",
    "....KKWWWWKK....",
    "...KKWWWWWWKK...",
    "..KlKWWWWWWWWK..",
    ".KllKWWWWWWWWWK.",
    ".KlKWWWWWWWWWK..",
    ".KKWWWWWWWWWK...",
    ".K.KWWWWWWWK....",
    ".K..KWWWWWK.....",
    ".KK..KWWWK......",
    "..K...KWK.......",
    "......KK........",
]
cursors['paintbucket'] = (over(icon(bucket, at=(0, 0)), cross(*C)), C)

# Lápiz: punta abajo a la izquierda (el punto activo), cuerpo amarillo y goma rosa arriba.
T = (1, 14)
def along(x, y):
    return (x - T[0]) + (T[1] - y)
def perp(x, y):
    return ((x - T[0]) - (T[1] - y)) / 2.0
def pencil_in(x, y):
    a, p = along(x, y), abs(perp(x, y))
    if a < 0 or a > 27:
        return False
    if a <= 6:
        return p <= a * 0.32 + 0.3
    return p <= 2.2
def pencil_col(x, y):
    a, p = along(x, y), perp(x, y)
    if a <= 2:
        return 'K'
    if a <= 6:
        return 'O'
    if a <= 20:
        return 'y' if p > 0.6 else 'Y'
    if a <= 22:
        return 'l'
    return 'P'
pen = shape(pencil_in, pencil_col)
pen[T[1]][T[0]] = 'K'
cursors['pencil'] = (halo(to_image(pen), H_LIGHT), T)

# Cuentagotas: punta abajo a la izquierda, tubo blanco, anillo y la pera negra arriba.
def dropper_in(x, y):
    a, p = along(x, y), abs(perp(x, y))
    if a < 0 or a > 26:
        return False
    if a <= 2:
        return p <= 0.6
    if a <= 14:
        return p <= 1.3
    if a <= 16:
        return p <= 2.4
    if a <= 24:
        return p <= 2.0
    return p <= 1.2
def dropper_col(x, y):
    a, p = along(x, y), perp(x, y)
    if a <= 2 or a >= 14:
        return 'K' if not (a >= 17 and p < -0.6 and a <= 22) else 'd'
    return 'W'
drop = shape(dropper_in, dropper_col)
drop[T[1]][T[0]] = 'K'
cursors['colorpicker'] = (halo(to_image(drop), H_LIGHT), T)

# Texto: la I.
art = [
    "KKK.KKK",
    "KWWKWWK",
    ".KKWKK.",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    "..KWK..",
    ".KKWKK.",
    "KWWKWWK",
    "KKK.KKK",
]
cursors['text'] = (icon(art, at=(1, 1)), (4, 9))

# Lupa: lente con el centro como punto activo y el mango abajo a la derecha.
L = (7, 7)
def lupa(x, y):
    d = math.hypot(x - L[0], y - L[1])
    if 5.5 <= d < 6.6 or 3.4 <= d < 4.5:
        return 'K'
    if 4.5 <= d < 5.5:
        return 'W'
    s = x + y
    if 2 * L[0] + 8 <= s <= 2 * L[0] + 18:
        if x - y == 0:
            return 'W'
        if abs(x - y) == 1 or (s == 2 * L[0] + 18 and abs(x - y) <= 1):
            return 'K'
    return '.'
g = blank()
for y in range(32):
    for x in range(32):
        g[y][x] = lupa(x, y)
cursors['zoom'] = (halo(to_image(g), H_LIGHT), L)

# Goma: la cruz y una goma inclinada arriba a la derecha.
C = (8, 14)
art = [
    ".......KKKK",
    "......KPPPK",
    ".....KPPPPK",
    "....KWKPPK.",
    "...KWWWKK..",
    "..KWWWWK...",
    ".KWWWWK....",
    "KWWWWK.....",
    "KKWWK......",
    ".KKK.......",
]
cursors['eraser'] = (over(cross(*C), icon(art, at=(12, 1))), C)

# Tampón de clonar: la cruz y el tampón (marco con el punto de origen).
art = [
    "KKKKKKKKKKK",
    "KWWWWWWWWWK",
    "KWdddddddWK",
    "KWddKKKddWK",
    "KWdKWWWKdWK",
    "KWdKWKWKdWK",
    "KWdKWWWKdWK",
    "KWddKKKddWK",
    "KWWWWWWWWWK",
    "KKKKKKKKKKK",
]
cursors['clonestamp'] = (over(cross(*C), icon(art, at=(11, 1))), C)

# Cambiar color: la cruz y el icono (el color de origen y la flecha hacia el nuevo).
art = [
    "..KKK.....",
    ".KWWWK.KK.",
    "KWWWWWKKK.",
    "KWWWWWK.K.",
    "KWWWWWK...",
    ".KWWWK..K.",
    "..KKK..KKK",
    "......KKKKK",
]
cursors['recolor'] = (over(cross(*C), icon(art, at=(12, 2))), C)

# Manos (las de evidencias/mano1 y mano2): abierta y agarrando.
open_hand = [
    ".......KK.......",
    "...KK.KWWKKK....",
    "..KWWKKWWKWWK...",
    "..KWWKKWWKWWK.K.",
    "...KWWKWWKWWKKWK",
    "...KWWKWWKWWKWWK",
    ".KK.KWWWWWWWKWWK",
    "KWWKKWWWWWWWWWWK",
    "KWWWKWWWWWWWWWK.",
    ".KWWWWWWWWWWWWK.",
    "..KWWWWWWWWWWWK.",
    "..KWWWWWWWWWWK..",
    "...KWWWWWWWWWK..",
    "....KWWWWWWWK...",
    ".....KWWWWWWK...",
    ".....KKKKKKKK...",
]
closed_hand = [
    "................",
    "................",
    "....KK.KK.KK....",
    "...KWWKWWKWWK...",
    "...KWWKWWKWWKKK.",
    ".KKKWWKWWKWWKWWK",
    "KWWKWWWWWWWWKWWK",
    "KWWKWWWWWWWWWWWK",
    "KWWWWWWWWWWWWWWK",
    ".KWWWWWWWWWWWWWK",
    ".KWWWWWWWWWWWWK.",
    "..KWWWWWWWWWWWK.",
    "..KWWWWWWWWWWWK.",
    "...KWWWWWWWWWK..",
    "....KWWWWWWWWK..",
    ".....KKKKKKKK...",
]
cursors['hand-open'] = (icon(open_hand, at=(1, 1)), (9, 9))
cursors['hand-closed'] = (icon(closed_hand, at=(1, 1)), (9, 9))

out = sys.argv[1]
hot = {}
for name, (im, h) in cursors.items():
    im.save(f'{out}/{name}.png')
    hot[name] = list(h)
json.dump(hot, open(f'{out}/hotspots.json', 'w'), indent=1)

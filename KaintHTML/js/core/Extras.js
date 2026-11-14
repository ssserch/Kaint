// Funciones de PhotoCraft (como las de Kaint, src/core/effects/ExtraEffects.cpp y
// GalleryEffects.cpp): ajustes, distorsiones, estilizado, objeto y la galería de filtros. Los
// algoritmos son los de PhotoCraft (ArtCraft Team y los colaboradores de PhotoCraft, licencia MIT;
// licenses/PhotoCraft-LICENSE.txt), escritos para Kaint. Están escondidas mientras no se activen en
// Configuración > Complementos y en los menús llevan la pieza de puzle de los complementos.
// Trabajan con el color directo de la capa en coma flotante (0..1); el resultado de los que
// necesitan la imagen entera se calcula una vez (prepare) y cada franja lo copia (run).

const P = {
    int: (key, label, min, max, def) => ({ key, label, type: "int", min, max, def, decimals: 0 }),
    real: (key, label, min, max, def, decimals = 2) => ({ key, label, type: "real", min, max, def, decimals }),
    bool: (key, label, def) => ({ key, label, type: "bool", def }),
    choice: (key, label, choices, def = 0) => ({ key, label, type: "choice", choices, min: 0, max: choices.length - 1, def }),
    seed: (label = "Aleatorizar") => ({ key: "seed", label, type: "seed", min: 0, max: 255, def: 0 }),
    color: (key, label, def) => ({ key, label, type: "color", def }),
};
const ORIGIN = "PhotoCraft";

// --- Imagen de trabajo ------------------------------------------------------------------------

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const clamp01 = (v) => (v <= 0 ? 0 : v >= 1 ? 1 : v);
const to8 = (v) => (v <= 0 ? 0 : v >= 1 ? 255 : (v * 255 + 0.5) | 0);
const pack = (r, g, b, a) => ((a << 24) | (b << 16) | (g << 8) | r) >>> 0;

function toFloat(img) {
    const n = img.w * img.h, d = new Float32Array(n * 4), px = img.px;
    for (let i = 0; i < n; ++i) {
        const p = px[i];
        d[i * 4] = (p & 255) / 255;
        d[i * 4 + 1] = ((p >>> 8) & 255) / 255;
        d[i * 4 + 2] = ((p >>> 16) & 255) / 255;
        d[i * 4 + 3] = (p >>> 24) / 255;
    }
    return { w: img.w, h: img.h, d };
}

function fromFloat(b, roi) {
    const out = new Uint32Array(b.w * b.h);
    for (let y = roi.top; y <= roi.bottom; ++y)
        for (let x = roi.left, i = y * b.w + roi.left; x <= roi.right; ++x, ++i) {
            const o = i * 4, al = to8(b.d[o + 3]);
            out[i] = al ? pack(to8(b.d[o]), to8(b.d[o + 1]), to8(b.d[o + 2]), al) : 0;
        }
    return out;
}

const colorOf = (c, fallback = [0, 0, 0]) => (c && typeof c === "object" ? [c.r / 255, c.g / 255, c.b / 255] : fallback);

// Efecto sobre la imagen entera: f(buf, a) cambia la zona a.roi de 'buf'.
function whole(f) {
    return {
        serial: true,
        prepare(src, roi, values, ctx) {
            const b = toFloat(src);
            f(b, { roi, values, ctx });
            return fromFloat(b, roi);
        },
        run(a) {
            const w = a.src.w, out = a.state;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let i = y * w + a.roi.left, end = y * w + a.roi.right; i <= end; ++i)
                    a.dst.px[i] = out[i];
        },
    };
}

// Píxel a píxel sobre RGB (0..1) con el alfa intacto.
function perPixel(f) {
    return {
        run(a) {
            const s = a.src.px, d = a.dst.px, w = a.src.w, c = [0, 0, 0];
            for (let y = a.y0; y <= a.y1; ++y)
                for (let i = y * w + a.roi.left, end = y * w + a.roi.right; i <= end; ++i) {
                    const p = s[i], al = p >>> 24;
                    if (!al) {
                        d[i] = p;
                        continue;
                    }
                    c[0] = (p & 255) / 255;
                    c[1] = ((p >>> 8) & 255) / 255;
                    c[2] = ((p >>> 16) & 255) / 255;
                    f(c, a.values);
                    d[i] = pack(to8(c[0]), to8(c[1]), to8(c[2]), al);
                }
        },
    };
}

// --- Color ---------------------------------------------------------------------------------

const srgbToLinear = (v) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
const linearToSrgb = (v) => (v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055);
const lumW3c = (c) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];

function clipColor(c) {
    const l = lumW3c(c), n = Math.min(c[0], c[1], c[2]), x = Math.max(c[0], c[1], c[2]);
    if (n < 0) {
        const d = l - n;
        for (let i = 0; i < 3; ++i) c[i] = l > 0 && d > 0 ? l + ((c[i] - l) * l) / d : l;
    }
    if (x > 1) {
        const d = x - l;
        for (let i = 0; i < 3; ++i) c[i] = l < 1 && d > 0 ? l + ((c[i] - l) * (1 - l)) / d : l;
    }
}

function setLum(c, l) {
    const d = l - lumW3c(c);
    for (let i = 0; i < 3; ++i) c[i] += d;
    clipColor(c);
}

// Ruido determinista en 0..1 (el mismo que en Kaint).
function hash01(x, y, c, seed) {
    let h = (Math.imul(x | 0, 0x8da6b343) ^ Math.imul(y | 0, 0xd8163841) ^ Math.imul(c >>> 0, 0xcb1ab31f) ^ Math.imul(seed >>> 0, 0x9e3779b9)) >>> 0;
    h = (h ^ (h >>> 16)) >>> 0;
    h = Math.imul(h, 0x7feb352d) >>> 0;
    h = (h ^ (h >>> 15)) >>> 0;
    h = Math.imul(h, 0x846ca68b) >>> 0;
    h = (h ^ (h >>> 16)) >>> 0;
    return (h >>> 8) / 16777216;
}

const remEuclid = (a, b) => {
    const r = a % b;
    return r < 0 ? r + b : r;
};
const smooth = (e0, e1, x) => {
    if (Math.abs(e1 - e0) < 1e-9)
        return x >= e1 ? 1 : 0;
    const t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
};

// --- Muestreo y desenfoque ----------------------------------------------------------------

const EDGE_TRANSPARENT = 0, EDGE_REPEAT = 1, EDGE_WRAP = 2;

function edgeValue(b, x, y, c, edge, r) {
    if (r.width <= 0 || r.height <= 0)
        return 0;
    if (x < r.left || x > r.right || y < r.top || y > r.bottom) {
        if (edge === EDGE_TRANSPARENT)
            return 0;
        if (edge === EDGE_REPEAT) {
            x = clamp(x, r.left, r.right);
            y = clamp(y, r.top, r.bottom);
        } else {
            x = r.left + remEuclid(x - r.left, r.width);
            y = r.top + remEuclid(y - r.top, r.height);
        }
    }
    return b.d[(y * b.w + x) * 4 + c];
}

function sample(b, x, y, edge, r, out, o) {
    const fx = x - 0.5, fy = y - 0.5, x0 = Math.floor(fx), y0 = Math.floor(fy), ax = fx - x0, ay = fy - y0;
    let r0 = 0, g0 = 0, b0 = 0, accA = 0;
    const taps = [[0, 0, (1 - ax) * (1 - ay)], [1, 0, ax * (1 - ay)], [0, 1, (1 - ax) * ay], [1, 1, ax * ay]];
    for (const [dx, dy, w] of taps) {
        if (w <= 0)
            continue;
        const al = edgeValue(b, x0 + dx, y0 + dy, 3, edge, r);
        r0 += edgeValue(b, x0 + dx, y0 + dy, 0, edge, r) * al * w;
        g0 += edgeValue(b, x0 + dx, y0 + dy, 1, edge, r) * al * w;
        b0 += edgeValue(b, x0 + dx, y0 + dy, 2, edge, r) * al * w;
        accA += al * w;
    }
    if (accA > 0) {
        r0 /= accA;
        g0 /= accA;
        b0 /= accA;
    }
    out[o] = r0;
    out[o + 1] = g0;
    out[o + 2] = b0;
    out[o + 3] = accA;
}

function remap(b, r, edge, f) {
    const res = new Float32Array(b.d);
    for (let y = r.top; y <= r.bottom; ++y)
        for (let x = r.left; x <= r.right; ++x) {
            const [sx, sy] = f(x + 0.5, y + 0.5);
            sample(b, sx, sy, edge, r, res, (y * b.w + x) * 4);
        }
    b.d = res;
}

function boxRadii(sigma) {
    const n = 3, ideal = Math.sqrt((12 * sigma * sigma) / n + 1);
    let wl = Math.floor(ideal);
    if (wl % 2 === 0)
        --wl;
    wl = Math.max(1, wl);
    const wu = wl + 2;
    const m = clamp(Math.round((12 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4 * wl - 4)), 0, n);
    const r = [0, 1, 2].map((i) => (i < m ? (wl - 1) / 2 : (wu - 1) / 2));
    if (r.every((v) => v === 0))
        r[0] = 1;
    return r;
}

// Media móvil por filas o columnas de un búfer intercalado de 'n' canales.
function boxRows(buf, w, h, n, r) {
    if (r <= 0 || w <= 0)
        return;
    const norm = 1 / (2 * r + 1), s = new Float64Array(w * n);
    for (let y = 0; y < h; ++y) {
        const base = y * w * n;
        for (let i = 0; i < w * n; ++i) s[i] = buf[base + i];
        for (let c = 0; c < n; ++c) {
            const at = (x) => s[clamp(x, 0, w - 1) * n + c];
            let acc = 0;
            for (let x = -r; x <= r; ++x) acc += at(x);
            for (let x = 0; x < w; ++x) {
                buf[base + x * n + c] = acc * norm;
                acc += at(x + r + 1) - at(x - r);
            }
        }
    }
}

function boxCols(buf, w, h, n, r) {
    if (r <= 0 || h <= 0)
        return;
    const norm = 1 / (2 * r + 1), s = new Float64Array(h * n);
    for (let x = 0; x < w; ++x) {
        for (let y = 0; y < h; ++y)
            for (let c = 0; c < n; ++c) s[y * n + c] = buf[(y * w + x) * n + c];
        for (let c = 0; c < n; ++c) {
            const at = (y) => s[clamp(y, 0, h - 1) * n + c];
            let acc = 0;
            for (let y = -r; y <= r; ++y) acc += at(y);
            for (let y = 0; y < h; ++y) {
                buf[(y * w + x) * n + c] = acc * norm;
                acc += at(y + r + 1) - at(y - r);
            }
        }
    }
}

function gaussN(buf, w, h, n, sigma) {
    if (sigma < 0.3)
        return;
    for (const r of boxRadii(sigma)) boxRows(buf, w, h, n, r);
    for (const r of boxRadii(sigma)) boxCols(buf, w, h, n, r);
}

// Gaussiano de la zona 'r' de 'b' sobre los colores premultiplicados.
function gaussian(b, r, sigma) {
    if (sigma <= 0 || r.width <= 0)
        return;
    const w = r.width, h = r.height, work = new Float32Array(w * h * 4);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const o = ((r.top + y) * b.w + r.left + x) * 4, q = (y * w + x) * 4, a = b.d[o + 3];
            work[q] = b.d[o] * a;
            work[q + 1] = b.d[o + 1] * a;
            work[q + 2] = b.d[o + 2] * a;
            work[q + 3] = a;
        }
    for (const rr of boxRadii(sigma)) {
        boxRows(work, w, h, 4, rr);
        boxCols(work, w, h, 4, rr);
    }
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const o = ((r.top + y) * b.w + r.left + x) * 4, q = (y * w + x) * 4, a = work[q + 3];
            b.d[o + 3] = a;
            for (let c = 0; c < 3; ++c) b.d[o + c] = a > 1e-6 ? work[q + c] / a : 0;
        }
}

// --- Ajustes ------------------------------------------------------------------------------

const VIB_GREY = [0.288, 0.712];
const VIB_BOOST = [0.5001, 0.0887, 0.4374, 0.7573, -0.8113, 0.5154, 2.8026];

function hsvHue(c, mx, mn) {
    const d = mx - mn;
    let h;
    if (mx === c[0])
        h = remEuclid((c[1] - c[2]) / d, 6);
    else if (mx === c[1])
        h = (c[2] - c[0]) / d + 2;
    else
        h = (c[0] - c[1]) / d + 4;
    return h * 60;
}

function vibranceLinear(c, v) {
    const mx = Math.max(c[0], c[1], c[2]), mn = Math.min(c[0], c[1], c[2]);
    if (mx <= 0 || mx - mn <= 1e-9)
        return;
    const t = 1 - mn / mx;
    const shape = t * (1 - t) * (2 - t + t * t) * mx * Math.max(0, 1 - mx);
    let t2, v2;
    if (v < 0) {
        const a = -v;
        t2 = t * (1 - a / 4) * (1 - a + (a * t * (1 + t)) / 2);
        v2 = mx - a * shape;
    } else {
        const k = VIB_BOOST, hue = hsvHue(c, mx, mn);
        const w = hue <= 30 ? 1 : hue < 45 ? (45 - hue) / 15 : hue < 300 ? 0 : (hue - 300) / 60;
        const a = v * (1 - w) + 0.64 * Math.pow(v, 1.6) * w;
        const boost = (k[0] * a + k[1] * a * a) * Math.pow(t, k[2]) * Math.pow(1 - t, k[3]) * Math.exp((k[4] + k[5] * a + k[6] * (1 - t)) * Math.max(0, 1 - mx));
        t2 = Math.min(1, t * (1 + boost));
        v2 = mx + (v / 4) * shape;
    }
    const m2 = v2 * (1 - t2), kk = (v2 - m2) / (mx - mn);
    for (let i = 0; i < 3; ++i) c[i] = m2 + (c[i] - mn) * kk;
}

function vibrance(c, v) {
    const vib = v.vibrance / 100, sat = v.saturation / 100;
    const l = c.map((x) => srgbToLinear(Math.max(0, x)));
    if (vib !== 0)
        vibranceLinear(l, vib);
    if (sat !== 0) {
        const g = VIB_GREY[0] * l[0] + VIB_GREY[1] * l[1];
        for (let i = 0; i < 3; ++i) l[i] = clamp01(g + (1 + sat) * (l[i] - g));
    }
    for (let i = 0; i < 3; ++i) c[i] = linearToSrgb(clamp01(l[i]));
}

const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

function gradientMap(b, a) {
    const f = colorOf(a.values.from), t3 = colorOf(a.values.to, [1, 1, 1]);
    const { reverse, dither } = a.values;
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4;
            if (b.d[o + 3] <= 0)
                continue;
            let t = clamp01(0.3 * b.d[o] + 0.59 * b.d[o + 1] + 0.11 * b.d[o + 2]);
            if (reverse)
                t = 1 - t;
            for (let i = 0; i < 3; ++i) {
                let v = f[i] + (t3[i] - f[i]) * t;
                if (dither)
                    v = clamp01(v + ((BAYER[(y & 3) * 4 + (x & 3)] + 0.5) / 16 - 0.5) / 255);
                b.d[o + i] = v;
            }
        }
}

const PHOTO_FILTERS = [
    ["Filtro cálido (85)", 236, 138, 0], ["Filtro cálido (LBA)", 250, 150, 0], ["Filtro cálido (81)", 235, 177, 19],
    ["Filtro frío (80)", 0, 109, 255], ["Filtro frío (LBB)", 0, 93, 255], ["Filtro frío (82)", 0, 181, 255],
    ["Rojo", 234, 26, 26], ["Naranja", 243, 132, 23], ["Amarillo", 249, 227, 28], ["Verde", 25, 201, 25], ["Cian", 29, 203, 234],
    ["Azul", 29, 53, 234], ["Violeta", 155, 29, 234], ["Magenta", 227, 24, 227], ["Sepia", 172, 122, 51], ["Rojo intenso", 255, 0, 0],
    ["Azul intenso", 0, 34, 205], ["Esmeralda intenso", 0, 140, 0], ["Amarillo intenso", 255, 213, 0], ["Bajo el agua", 0, 194, 177],
];
const D50 = [0.96422, 1, 0.82521];
const SRGB_TO_XYZ = [[0.436074, 0.385064, 0.14308], [0.222504, 0.716878, 0.060618], [0.013932, 0.097104, 0.714173]];
const XYZ_TO_SRGB = [[3.133856, -1.616867, -0.490615], [-0.978768, 1.916142, 0.033454], [0.071945, -0.228991, 1.405243]];

function photoFilterMatrix(color, density) {
    const d = Number.isFinite(density) ? clamp01(density) : 0;
    const lin = color.map((v) => srgbToLinear(clamp01(v)));
    const xyz = SRGB_TO_XYZ.map((r) => r[0] * lin[0] + r[1] * lin[1] + r[2] * lin[2]);
    const s = xyz.map((v, i) => 1 - d + (d * v) / D50[i]);
    return [0, 1, 2].map((r) => [0, 1, 2].map((c) => XYZ_TO_SRGB[r][0] * s[0] * SRGB_TO_XYZ[0][c] + XYZ_TO_SRGB[r][1] * s[1] * SRGB_TO_XYZ[1][c] + XYZ_TO_SRGB[r][2] * s[2] * SRGB_TO_XYZ[2][c]));
}

const matrixCache = new Map();
function photoFilter(c, v) {
    const preset = v.filter | 0;
    const color = preset < PHOTO_FILTERS.length ? PHOTO_FILTERS[preset].slice(1).map((x) => x / 255) : colorOf(v.color, [0.925, 0.541, 0]);
    const key = `${color}|${v.density}`;
    let m = matrixCache.get(key);
    if (!m) {
        if (matrixCache.size > 32)
            matrixCache.clear();
        matrixCache.set(key, (m = photoFilterMatrix(color, v.density / 100)));
    }
    const lin = c.map((x) => srgbToLinear(Math.max(0, x)));
    const f = m.map((r) => linearToSrgb(Math.max(0, r[0] * lin[0] + r[1] * lin[1] + r[2] * lin[2])));
    if (v.preserveLuminosity)
        setLum(f, lumW3c(c));
    for (let i = 0; i < 3; ++i) c[i] = clamp01(f[i]);
}

function selectiveColor(c, v) {
    const mx = Math.max(c[0], c[1], c[2]), mn = Math.min(c[0], c[1], c[2]), mid = c[0] + c[1] + c[2] - mx - mn;
    const top = (i) => (c[i] >= mx ? mx - mid : 0), bottom = (i) => (c[i] <= mn ? mid - mn : 0);
    const weights = [top(0), bottom(2), top(1), bottom(0), top(2), bottom(1), Math.max(0, (mn - 0.5) * 2),
                     clamp01(1 - Math.abs(mx - 0.5) - Math.abs(mn - 0.5)), Math.max(0, (0.5 - mx) * 2)];
    const w = weights[v.colors | 0] || 0;
    if (w <= 0)
        return;
    const relative = (v.method | 0) === 0;
    const adj = [v.cyan / 100, v.magenta / 100, v.yellow / 100, v.black / 100];
    for (let i = 0; i < 3; ++i) {
        const ink = 1 - c[i];
        let d = adj[i] + adj[3];
        if (relative)
            d *= ink;
        else if (d > 0)
            d *= Math.min(1, 2 * (1 - ink));
        else
            d *= Math.min(1, 2 * ink);
        c[i] = clamp01(c[i] - d * w);
    }
}

const bin = (v) => Math.round(clamp01(v) * 255);

function histograms(b, r) {
    const h = [new Float64Array(256), new Float64Array(256), new Float64Array(256)];
    for (let y = r.top; y <= r.bottom; ++y)
        for (let x = r.left; x <= r.right; ++x) {
            const o = (y * b.w + x) * 4;
            if (b.d[o + 3] <= 0)
                continue;
            for (let c = 0; c < 3; ++c) h[c][bin(b.d[o + c])]++;
        }
    return h;
}

function clipPoints(h, clip) {
    let total = 0;
    for (const v of h) total += v;
    if (!total)
        return [0, 1];
    const lim = Math.floor(total * clip);
    let acc = 0, lo = 0;
    while (lo < 254 && acc + h[lo] <= lim) acc += h[lo++];
    acc = 0;
    let hi = 255;
    while (hi > lo + 1 && acc + h[hi] <= lim) acc += h[hi--];
    return [lo / 255, hi / 255];
}

function medianOf(h) {
    let total = 0, acc = 0;
    for (const v of h) total += v;
    for (let i = 0; i < 256; ++i) {
        acc += h[i];
        if (acc * 2 >= total)
            return i / 255;
    }
    return 0.5;
}

function applyLevels(b, r, lv) {
    for (let y = r.top; y <= r.bottom; ++y)
        for (let x = r.left; x <= r.right; ++x) {
            const o = (y * b.w + x) * 4;
            if (b.d[o + 3] <= 0)
                continue;
            for (let c = 0; c < 3; ++c) {
                const t = clamp01((b.d[o + c] - lv[c][0]) / Math.max(1e-6, lv[c][1] - lv[c][0]));
                b.d[o + c] = Math.pow(t, 1 / lv[c][2]);
            }
        }
}

function autoContrast(b, a) {
    const h = histograms(b, a.roi), all = new Float64Array(256);
    for (const c of h) for (let i = 0; i < 256; ++i) all[i] += c[i];
    const [black, white] = clipPoints(all, 0.001);
    applyLevels(b, a.roi, [[black, white, 1], [black, white, 1], [black, white, 1]]);
}

function autoColor(b, a) {
    const h = histograms(b, a.roi);
    const pts = h.map((c) => clipPoints(c, 0.001));
    const ts = h.map((c, i) => clamp((medianOf(c) - pts[i][0]) / Math.max(1e-3, pts[i][1] - pts[i][0]), 0.02, 0.98));
    const want = (ts[0] + ts[1] + ts[2]) / 3;
    applyLevels(b, a.roi, [0, 1, 2].map((c) => [pts[c][0], pts[c][1], clamp(Math.log(ts[c]) / Math.log(want), 0.1, 9.99)]));
}

function equalize(b, a) {
    const h = histograms(b, a.roi), all = new Float64Array(256);
    for (const c of h) for (let i = 0; i < 256; ++i) all[i] += c[i];
    let total = 0;
    for (const v of all) total += v;
    if (!total)
        return;
    const first = all.find((v) => v > 0) || 0;
    const lut = new Float32Array(256);
    let acc = 0;
    for (let i = 0; i < 256; ++i) {
        acc += all[i];
        lut[i] = total > first ? clamp01((acc - first) / (total - first)) : i / 255;
    }
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4;
            if (b.d[o + 3] > 0)
                for (let c = 0; c < 3; ++c) b.d[o + c] = lut[bin(b.d[o + c])];
        }
}

const BW_KEYS = ["reds", "yellows", "greens", "cyans", "blues", "magentas"];

function blackWhite(c, v) {
    const w = BW_KEYS.map((k) => v[k]);
    const [r, g, bl] = c;
    const mx = Math.max(r, g, bl), mn = Math.min(r, g, bl), mid = r + g + bl - mx - mn;
    const primary = r >= g && r >= bl ? 0 : g >= bl ? 2 : 4;
    const secondary = bl <= r && bl <= g ? 1 : r <= g ? 3 : 5;
    const gray = clamp01(mn + ((mid - mn) * w[secondary]) / 100 + ((mx - mid) * w[primary]) / 100);
    if (!v.tint) {
        c[0] = c[1] = c[2] = gray;
        return;
    }
    const out = colorOf(v.tintColor, [0.882, 0.827, 0.702]);
    setLum(out, gray);
    for (let i = 0; i < 3; ++i) c[i] = clamp01(out[i]);
}

function hueOf(c, mx, d) {
    if (d <= 0)
        return 0;
    let h;
    if (mx === c[0])
        h = remEuclid((c[1] - c[2]) / d, 6);
    else if (mx === c[1])
        h = (c[2] - c[0]) / d + 2;
    else
        h = (c[0] - c[1]) / d + 4;
    return h / 6;
}

function fromHcm(h, chroma, m) {
    const hp = (h - Math.floor(h)) * 6, x = chroma * (1 - Math.abs((hp % 2) - 1));
    const [r, g, b] = [[chroma, x, 0], [x, chroma, 0], [0, chroma, x], [0, x, chroma], [x, 0, chroma], [chroma, 0, x]][Math.min(5, hp | 0)];
    return [r + m, g + m, b + m];
}

function hsbHsl(c, v) {
    const input = v.input | 0, output = v.output | 0;
    if (input === output)
        return;
    let rgb = c.map(clamp01);
    if (input === 1)
        rgb = fromHcm(rgb[0], rgb[2] * rgb[1], rgb[2] - rgb[2] * rgb[1]);
    else if (input === 2) {
        const chroma = (1 - Math.abs(2 * rgb[2] - 1)) * rgb[1];
        rgb = fromHcm(rgb[0], chroma, rgb[2] - chroma / 2);
    }
    const mx = Math.max(...rgb), mn = Math.min(...rgb), d = mx - mn;
    if (output === 0) {
        c[0] = rgb[0], c[1] = rgb[1], c[2] = rgb[2];
    } else if (output === 1) {
        c[0] = hueOf(rgb, mx, d), c[1] = mx > 0 ? d / mx : 0, c[2] = mx;
    } else {
        const l = (mx + mn) / 2;
        c[0] = hueOf(rgb, mx, d);
        c[1] = d <= 0 ? 0 : clamp01(d / Math.max(1e-7, 1 - Math.abs(2 * l - 1)));
        c[2] = l;
    }
}

// --- Distorsiones -------------------------------------------------------------------------

const TAU = Math.PI * 2;
const waveShape = (type, phase) => {
    const p = remEuclid(phase, TAU) / TAU;
    return type === 1 ? 1 - 4 * Math.abs(p - 0.5) : type === 2 ? (p < 0.5 ? 1 : -1) : Math.sin(phase);
};

function wave(b, a) {
    const v = a.values, n = clamp(v.generators | 0, 1, 64), seed = v.seed >>> 0, type = v.type | 0;
    const lerp = (lo, hi, t) => lo + Math.max(0, hi - lo) * t;
    const gens = [];
    for (let i = 0; i < n; ++i) {
        const r = (k) => hash01(i, k, 7, seed);
        gens.push([Math.max(1, lerp(v.wavelengthMin, v.wavelengthMax, r(1))), lerp(v.amplitudeMin, v.amplitudeMax, r(2)), r(3) * TAU, r(4) * TAU]);
    }
    const r = a.roi;
    remap(b, r, (v.undefined | 0) === 1 ? EDGE_REPEAT : EDGE_WRAP, (x, y) => {
        const lx = x - r.left, ly = y - r.top;
        let dx = 0, dy = 0;
        for (const [len, amp, p1, p2] of gens) {
            dx += amp * waveShape(type, (TAU * ly) / len + p1);
            dy += amp * waveShape(type, (TAU * lx) / len + p2);
        }
        return [x + dx / n, y + dy / n];
    });
}

function ripple(b, a) {
    const size = a.values.size | 0, len = size === 0 ? 8 : size === 1 ? 16 : 32, amp = ((a.values.amount / 100) * len) / 8, r = a.roi;
    remap(b, r, EDGE_REPEAT, (x, y) => [x + amp * Math.sin((TAU * (y - r.top)) / len), y + amp * Math.sin((TAU * (x - r.left)) / len)]);
}

function polarCoordinates(b, a) {
    const r = a.roi, bw = Math.max(1, r.width), bh = Math.max(1, r.height), cx = r.left + bw / 2, cy = r.top + bh / 2;
    const rmax = Math.min(bw, bh) / 2, toPolar = (a.values.mode | 0) === 0;
    remap(b, r, EDGE_REPEAT, (px, py) => {
        const x = px - 0.5, y = py - 0.5;
        let sx, sy;
        if (toPolar) {
            const dx = x - cx, dy = y - cy;
            let th = Math.atan2(-dx, -dy);
            if (th <= 0)
                th += TAU;
            sx = r.left + (th / TAU) * bw;
            sy = r.top + (Math.hypot(dx, dy) / rmax) * bh;
        } else {
            const th = ((x + 1 - r.left) / bw) * TAU, rr = ((y - r.top) / bh) * rmax;
            sx = cx - rr * Math.sin(th);
            sy = cy - rr * Math.cos(th);
        }
        return [sx + 0.5, sy + 0.5];
    });
}

function zigzag(b, a) {
    const r = a.roi, cx = r.left + r.width / 2, cy = r.top + r.height / 2, rr = Math.min(r.width, r.height) / 2;
    const am = clamp(a.values.amount / 100, -1, 1), k = Math.max(0.5, clamp(a.values.ridges, 0, 20)), style = a.values.style | 0;
    remap(b, r, EDGE_REPEAT, (x, y) => {
        const dx = x - cx, dy = y - cy, d = Math.hypot(dx, dy);
        if (d >= rr || d <= 0 || am === 0)
            return [x, y];
        const rn = d / rr, w = Math.sin(TAU * k * rn) * (1 - rn);
        let radial = 0, angular = 0;
        if (style === 1)
            radial = (am * w * rr) / (4 * k);
        else if (style === 2)
            angular = (am * w * Math.PI) / (2 * k);
        else {
            radial = (am * w * rr) / (8 * k);
            angular = (am * w * Math.PI) / (4 * k);
        }
        const th = Math.atan2(dy, dx) + angular, rs = Math.max(0, d + radial);
        return [cx + rs * Math.cos(th), cy + rs * Math.sin(th)];
    });
}

function offsetEffect(b, a) {
    const r = a.roi, dx = a.values.horizontal | 0, dy = a.values.vertical | 0, mode = a.values.undefined | 0;
    const coord = (pos, dist, start, end) => {
        const p = pos - dist, len = end - start;
        if (mode === 0)
            return start + remEuclid(p - start, len);
        if (mode === 1)
            return clamp(p, start, end - 1);
        return p < start || p >= end ? null : p;
    };
    const src = new Float32Array(b.d);
    for (let y = r.top; y <= r.bottom; ++y) {
        const sy = coord(y, dy, r.top, r.bottom + 1);
        for (let x = r.left; x <= r.right; ++x) {
            const sx = coord(x, dx, r.left, r.right + 1), o = (y * b.w + x) * 4;
            if (sx === null || sy === null)
                b.d.fill(0, o, o + 4);
            else
                for (let c = 0; c < 4; ++c) b.d[o + c] = src[(sy * b.w + sx) * 4 + c];
        }
    }
}

function shear(b, a) {
    const r = a.roi, h = Math.max(1, r.height), { top, middle, bottom } = a.values;
    remap(b, r, (a.values.undefined | 0) === 1 ? EDGE_REPEAT : EDGE_WRAP, (x, y) => {
        const t = (y - r.top) / h;
        const l0 = 2 * (t - 0.5) * (t - 1), l1 = -4 * t * (t - 1), l2 = 2 * t * (t - 0.5);
        return [x - ((top * l0 + middle * l1 + bottom * l2) / 100) * (r.width / 2), y];
    });
}

// --- Estilizado y objeto ------------------------------------------------------------------

function threshold(c, v) {
    const t = Math.round(v.level), r = Math.round(clamp01(c[0]) * 255), g = Math.round(clamp01(c[1]) * 255), bl = Math.round(clamp01(c[2]) * 255);
    c[0] = c[1] = c[2] = 30 * r + 59 * g + 11 * bl > 100 * t - 50 ? 1 : 0;
}

function highPass(b, a) {
    const blur = { w: b.w, h: b.h, d: new Float32Array(b.d) };
    gaussian(blur, a.roi, a.values.radius);
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4;
            for (let c = 0; c < 3; ++c) b.d[o + c] = clamp01(b.d[o + c] - blur.d[o + c] + 0.5);
        }
}

function colorToAlpha(b, a) {
    const k = colorOf(a.values.color, [1, 1, 1]), transparency = a.values.transparency / 100;
    const opacity = Math.max(transparency + 1e-4, a.values.opacity / 100);
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4;
            let alpha = 0;
            for (let c = 0; c < 3; ++c) {
                const d = b.d[o + c] - k[c], lim = d > 0 ? 1 - k[c] : k[c];
                if (lim > 1e-6)
                    alpha = Math.max(alpha, Math.abs(d) / lim);
                else if (Math.abs(d) > 1e-6)
                    alpha = 1;
            }
            const na = clamp01((alpha - transparency) / (opacity - transparency));
            if (na > 0)
                for (let c = 0; c < 3; ++c) b.d[o + c] = clamp01(k[c] + (b.d[o + c] - k[c]) / na);
            b.d[o + 3] *= na;
        }
}

function removeMatte(white) {
    return (b, a) => {
        for (let y = a.roi.top; y <= a.roi.bottom; ++y)
            for (let x = a.roi.left; x <= a.roi.right; ++x) {
                const o = (y * b.w + x) * 4, al = b.d[o + 3];
                if (al <= 0 || al >= 1)
                    continue;
                for (let c = 0; c < 3; ++c) b.d[o + c] = white ? clamp01((b.d[o + c] - (1 - al)) / al) : clamp01(b.d[o + c] / al);
            }
    };
}

function averageBlur(b, a) {
    const sum = [0, 0, 0];
    let wsum = 0;
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4;
            for (let c = 0; c < 3; ++c) sum[c] += b.d[o + c] * b.d[o + 3];
            wsum += b.d[o + 3];
        }
    if (wsum <= 0)
        return;
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4;
            for (let c = 0; c < 3; ++c) b.d[o + c] = sum[c] / wsum;
        }
}

// --- La galería de filtros ----------------------------------------------------------------

// La ventana de trabajo: el color (3 canales) de toda la capa; (ox, oy) es la coordenada del
// píxel (0, 0), con el origen en la esquina de la zona.
function makeWin(b, roi) {
    const n = b.w * b.h, c = new Float32Array(n * 3);
    for (let i = 0; i < n; ++i) {
        c[i * 3] = b.d[i * 4];
        c[i * 3 + 1] = b.d[i * 4 + 1];
        c[i * 3 + 2] = b.d[i * 4 + 2];
    }
    return { ox: -roi.left, oy: -roi.top, w: b.w, h: b.h, c };
}
const winLum = (win) => {
    const n = win.w * win.h, l = new Float32Array(n);
    for (let i = 0; i < n; ++i) l[i] = 0.299 * win.c[i * 3] + 0.587 * win.c[i * 3 + 1] + 0.114 * win.c[i * 3 + 2];
    return l;
};
const lumPx = (p) => 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
const getPx = (win, i) => [win.c[i * 3], win.c[i * 3 + 1], win.c[i * 3 + 2]];
const setPx = (win, i, p) => {
    win.c[i * 3] = p[0];
    win.c[i * 3 + 1] = p[1];
    win.c[i * 3 + 2] = p[2];
};
const mixPx = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const mapPx = (p, f) => [f(p[0]), f(p[1]), f(p[2])];
const posterize = (v, levels) => {
    const l = Math.max(1, levels - 1);
    return Math.round(clamp01(v) * l) / l;
};
const saturatePx = (p, k) => {
    const l = lumPx(p);
    return mapPx(p, (v) => l + (v - l) * k);
};

function winSample(win, x, y) {
    const fx = clamp(x - 0.5 - win.ox, 0, win.w - 1), fy = clamp(y - 0.5 - win.oy, 0, win.h - 1);
    const x0 = Math.floor(fx), y0 = Math.floor(fy), x1 = Math.min(x0 + 1, win.w - 1), y1 = Math.min(y0 + 1, win.h - 1);
    const ax = fx - x0, ay = fy - y0;
    const g = (xx, yy) => getPx(win, yy * win.w + xx);
    return mixPx(mixPx(g(x0, y0), g(x1, y0), ax), mixPx(g(x0, y1), g(x1, y1), ax), ay);
}
const winAt = (win, x, y) => getPx(win, clamp(y - win.oy, 0, win.h - 1) * win.w + clamp(x - win.ox, 0, win.w - 1));

function sobel(p, w, h) {
    const gx = new Float32Array(w * h), gy = new Float32Array(w * h);
    const at = (x, y) => p[clamp(y, 0, h - 1) * w + clamp(x, 0, w - 1)];
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            gx[y * w + x] = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1) - (at(x - 1, y - 1) + 2 * at(x - 1, y) + at(x - 1, y + 1))) / 8;
            gy[y * w + x] = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1) - (at(x - 1, y - 1) + 2 * at(x, y - 1) + at(x + 1, y - 1))) / 8;
        }
    return [gx, gy];
}
function gradMag(p, w, h) {
    const [gx, gy] = sobel(p, w, h);
    for (let i = 0; i < gx.length; ++i) gx[i] = Math.hypot(gx[i], gy[i]);
    return gx;
}

function lineBlur(buf, n, w, h, dx, dy, r) {
    if (r <= 0 || w <= 0 || h <= 0)
        return;
    const inside = (x, y) => x >= 0 && y >= 0 && x < w && y < h;
    const norm = 1 / (2 * r + 1);
    for (let sy = 0; sy < h; ++sy)
        for (let sx = 0; sx < w; ++sx) {
            if (inside(sx - dx, sy - dy))
                continue;
            const idx = [];
            for (let x = sx, y = sy; inside(x, y); x += dx, y += dy) idx.push((y * w + x) * n);
            const m = idx.length;
            for (let c = 0; c < n; ++c) {
                const vals = idx.map((o) => buf[o + c]);
                const at = (k) => vals[clamp(k, 0, m - 1)];
                let acc = 0;
                for (let k = -r; k <= r; ++k) acc += at(k);
                for (let k = 0; k < m; ++k) {
                    buf[idx[k] + c] = acc * norm;
                    acc += at(k + r + 1) - at(k - r);
                }
            }
        }
}

const direction = (name) => (name === "horizontal" ? [1, 0] : name === "vertical" ? [0, 1] : name === "leftDiagonal" ? [1, 1] : [1, -1]);

function kuwahara(win, r) {
    const h2 = Math.max(1, Math.ceil(r / 2)), w = win.w, h = win.h, n = w * h, m = new Float32Array(n * 5);
    for (let i = 0; i < n; ++i) {
        const l = 0.299 * win.c[i * 3] + 0.587 * win.c[i * 3 + 1] + 0.114 * win.c[i * 3 + 2];
        m[i * 5] = win.c[i * 3];
        m[i * 5 + 1] = win.c[i * 3 + 1];
        m[i * 5 + 2] = win.c[i * 3 + 2];
        m[i * 5 + 3] = l;
        m[i * 5 + 4] = l * l;
    }
    boxRows(m, w, h, 5, h2);
    boxCols(m, w, h, 5, h2);
    const get = (x, y) => (clamp(y, 0, h - 1) * w + clamp(x, 0, w - 1)) * 5;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            let best = Infinity, q = 0;
            for (const [dx, dy] of [[-h2, -h2], [h2, -h2], [-h2, h2], [h2, h2]]) {
                const o = get(x + dx, y + dy), v = m[o + 4] - m[o + 3] * m[o + 3];
                if (v < best) {
                    best = v;
                    q = o;
                }
            }
            const i = (y * w + x) * 3;
            win.c[i] = m[q];
            win.c[i + 1] = m[q + 1];
            win.c[i + 2] = m[q + 2];
        }
}

function unsharp(win, sigma, amount) {
    if (amount <= 0)
        return;
    const b = new Float32Array(win.c);
    gaussN(b, win.w, win.h, 3, sigma);
    for (let i = 0; i < b.length; ++i) win.c[i] += (win.c[i] - b[i]) * amount;
}

function vnoise(x, y, seed) {
    const ix = Math.floor(x), iy = Math.floor(y), tx = x - ix, ty = y - iy;
    const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const hh = (a, b) => hash01(a, b, 0, seed);
    const top = hh(ix, iy) + (hh(ix + 1, iy) - hh(ix, iy)) * sx;
    const bot = hh(ix, iy + 1) + (hh(ix + 1, iy + 1) - hh(ix, iy + 1)) * sx;
    return top + (bot - top) * sy;
}
const fbm = (x, y, seed) => vnoise(x, y, seed) * 0.57 + vnoise(x * 2.03, y * 2.03, (seed ^ 0x51) >>> 0) * 0.29 + vnoise(x * 4.1, y * 4.1, (seed ^ 0xa3) >>> 0) * 0.14;

function strokes(x, y, dx, dy, len, wid, seed) {
    const l = Math.max(1e-6, Math.hypot(dx, dy)), ux = dx / l, uy = dy / l;
    return vnoise((x * ux + y * uy) / Math.max(0.5, len), (-x * uy + y * ux) / Math.max(0.3, wid), seed);
}

function paintStroke(src, x, y, dx, dy, len, wid, seed) {
    const l = Math.max(1e-6, Math.hypot(dx, dy)), ux = dx / l, uy = dy / l;
    len = Math.max(1, len);
    wid = Math.max(0.5, wid);
    const fx = x + 0.5, fy = y + 0.5, u = fx * ux + fy * uy, v = -fx * uy + fy * ux;
    const lane = Math.floor(v / wid), off = hash01(lane, 0, 0, seed) * len;
    const t = remEuclid((u + off) / len, 1), du = (0.5 - t) * len;
    return [winSample(src, fx + du * ux, fy + du * uy), t];
}

function lightDir(name) {
    const s = Math.SQRT1_2;
    return { bottom: [0, 1], bottomLeft: [-s, s], left: [-1, 0], topLeft: [-s, -s], topRight: [s, -s], right: [1, 0], bottomRight: [s, s] }[name] || [0, -1];
}

function shadeOf(gx, gy, light, k) {
    const nx = -k * gx, ny = -k * gy, nz = 1, nl = Math.hypot(nx, ny, nz);
    const ll = Math.hypot(light[0], light[1], 1);
    return (nx * light[0] + ny * light[1] + nz) / (nl * ll) / (1 / ll);
}

function weave(x, y, p, noise) {
    const cx = Math.floor(x / p), cy = Math.floor(y / p), overH = remEuclid(cx + cy, 2) === 0;
    const u = remEuclid(x, p) / p, v = remEuclid(y, p) / p;
    return (overH ? Math.sin(v * Math.PI) : Math.sin(u * Math.PI)) * (1 - noise) + vnoise(x / (p * 0.7), y / (p * 2), 97) * noise;
}

function texture(kind, x, y, s) {
    x /= s;
    y /= s;
    switch (kind) {
    case "brick": {
        const bw = 24, bh = 12, row = Math.floor(y / bh);
        const xo = x + (row % 2 === 0 ? 0 : bw / 2), u = remEuclid(xo, bw), v = remEuclid(y, bh);
        return smooth(0.6, 2.2, Math.min(u, bw - u, v, bh - v)) * (0.85 + 0.15 * vnoise(x / 3, y / 3, 91));
    }
    case "burlap": return weave(x, y, 7, 0.35);
    case "canvas": return weave(x, y, 3.5, 0.18);
    case "sandstone": return fbm(x / 7, y / 7, 93) * 0.7 + hash01(Math.trunc(x), Math.trunc(y), 0, 94) * 0.3;
    case "blocks": {
        const b = 18, u = remEuclid(x, b) / b, v = remEuclid(y, b) / b;
        return Math.min(1, Math.min(u, 1 - u, v, 1 - v) * 4);
    }
    case "frosted": return fbm(x / 2.5, y / 2.5, 95);
    case "tinyLens": {
        const p = 12, u = (remEuclid(x, p) / p - 0.5) * 2, v = (remEuclid(y, p) / p - 0.5) * 2;
        return Math.sqrt(Math.max(0, 1 - (u * u + v * v)));
    }
    default: return 0.5;
    }
}

function texturizeBy(win, kind, scaling, relief, light, invert, weight) {
    if (relief <= 0)
        return;
    const s = clamp(scaling / 100, 0.5, 2), l = lightDir(light), k = (relief / 50) * 3, sign = invert ? -1 : 1;
    const n = win.w * win.h;
    for (let i = 0; i < n; ++i) {
        const fx = win.ox + (i % win.w), fy = win.oy + Math.floor(i / win.w);
        const gx = (texture(kind, fx + 1, fy, s) - texture(kind, fx - 1, fy, s)) * 0.5 * sign;
        const gy = (texture(kind, fx, fy + 1, s) - texture(kind, fx, fy - 1, s)) * 0.5 * sign;
        const p = getPx(win, i);
        const f = 1 + (clamp(shadeOf(gx, gy, l, k), 0, 2) - 1) * (weight ? weight(p) : 1);
        setPx(win, i, mapPx(p, (v) => v * f));
    }
}

function voronoi(x, y, cell, seed) {
    const cx = Math.floor(x / cell), cy = Math.floor(y / cell);
    let d1 = Infinity, d2 = Infinity, p1 = [x, y];
    for (let j = -1; j <= 1; ++j)
        for (let i = -1; i <= 1; ++i) {
            const px = (cx + i + 0.1 + 0.8 * hash01(cx + i, cy + j, 11, seed)) * cell;
            const py = (cy + j + 0.1 + 0.8 * hash01(cx + i, cy + j, 12, seed)) * cell;
            const d = Math.hypot(px - x, py - y);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                p1 = [px, py];
            } else if (d < d2) {
                d2 = d;
            }
        }
    return [d1, d2, p1];
}

function highlightsOf(p, l, area, intensity) {
    if (area <= 0)
        return p;
    const m = smooth(1 - (area / 20) * 0.75, 1.02, l) * (1 - (intensity / 10) * 0.6);
    return mapPx(p, (v) => v + (1 - v) * m);
}

let dotRanks = null;
function halftoneDotRanks() {
    if (!dotRanks) {
        const n = 128;
        dotRanks = new Float32Array(n * n);
        for (let i = 0; i < n * n; ++i)
            dotRanks[i] = (Math.cos(2 * Math.PI * (((i % n) + 0.5) / n)) + Math.cos(2 * Math.PI * ((Math.floor(i / n) + 0.5) / n))) / 2;
        dotRanks.sort();
    }
    return dotRanks;
}

function halftoneOffsets(gain) {
    const half = 0.5 / Math.max(1e-3, gain);
    const cover = (tau) => {
        const a = tau - half, z = tau + half, r0 = clamp01(a), r1 = clamp01(z);
        return clamp01(a) + (r1 > r0 ? (0.5 + tau * gain) * (r1 - r0) - 0.5 * gain * (r1 * r1 - r0 * r0) : 0);
    };
    const out = new Float32Array(256);
    for (let i = 0; i < 256; ++i) {
        let lo = -half - 1, hi = 1 + half + 1;
        for (let k = 0; k < 40; ++k) {
            const mid = (lo + hi) / 2;
            if (cover(mid) < i / 255)
                lo = mid;
            else
                hi = mid;
        }
        out[i] = i === 0 ? -half - 1 : i === 255 ? 1 + half + 1 : (lo + hi) / 2;
    }
    return out;
}

const normalize3 = (v) => {
    const l = Math.max(1e-9, Math.hypot(v[0], v[1], v[2]));
    return [v[0] / l, v[1] / l, v[2] / l];
};

// Los filtros: clave, nombre, submenú y parámetros (como en Kaint y PhotoCraft).
const GALLERY = [
    ["coloredPencil", "Lápiz de color", "artistic", "pencilWidth:1..24=4,strokePressure:0..15=8,paperBrightness:0..50=25"],
    ["cutout", "Cuarteado", "artistic", "numberOfLevels:2..8=4,edgeSimplicity:0..10=4,edgeFidelity:1..3=2"],
    ["dryBrush", "Pincel seco", "artistic", "brushSize:0..10=2,brushDetail:0..10=8,texture:1..3=1"],
    ["filmGrain", "Grano de película", "noise", "grain:0..20=4,highlightArea:0..20=0,intensity:0..10=10"],
    ["fresco", "Fresco", "artistic", "brushSize:0..10=2,brushDetail:0..10=8,texture:1..3=1"],
    ["neonGlow", "Resplandor de neón", "stylize", "glowSize:-24..24=5,glowBrightness:0..50=15"],
    ["paintDaubs", "Manchas de pintura", "artistic", "brushSize:1..50=8,sharpness:0..40=7,brushType:simple|lightRough|darkRough|wideSharp|wideBlurry|sparkle"],
    ["paletteKnife", "Espátula", "artistic", "strokeSize:1..50=25,strokeDetail:1..3=3,softness:0..10=0"],
    ["plasticWrap", "Envoltura plástica", "stylize", "highlightStrength:0..20=15,detail:1..15=9,smoothness:1..15=7"],
    ["posterEdges", "Bordes de póster", "artistic", "edgeThickness:0..10=2,edgeIntensity:0..10=1,posterization:0..6=2"],
    ["roughPastels", "Pastel grueso", "artistic", "strokeLength:0..40=6,strokeDetail:1..20=4,texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=20,light:bottom|bottomLeft|left|topLeft|top|topRight|right|bottomRight,invert:bool"],
    ["smudgeStick", "Barra difuminada", "artistic", "strokeLength:0..10=2,highlightArea:0..20=0,intensity:0..10=10"],
    ["sponge", "Esponja", "artistic", "brushSize:0..10=2,definition:0..25=12,smoothness:1..15=5"],
    ["underpainting", "Pintura subyacente", "artistic", "brushSize:0..40=6,textureCoverage:0..40=16,texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=4,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft,invert:bool"],
    ["watercolor", "Acuarela", "artistic", "brushDetail:1..14=9,shadowIntensity:0..10=1,texture:1..3=1"],
    ["accentedEdges", "Bordes acentuados", "brushstrokes", "edgeWidth:1..14=2,edgeBrightness:0..50=38,smoothness:1..15=5"],
    ["angledStrokes", "Trazos angulares", "brushstrokes", "directionBalance:0..100=50,strokeLength:3..50=15,sharpness:0..10=3"],
    ["crosshatch", "Sombreado cruzado", "brushstrokes", "strokeLength:3..50=9,sharpness:0..20=6,strength:1..3=1"],
    ["darkStrokes", "Trazos oscuros", "brushstrokes", "balance:0..10=5,blackIntensity:0..10=6,whiteIntensity:0..10=2"],
    ["inkOutlines", "Contornos con tinta", "brushstrokes", "strokeLength:1..50=4,darkIntensity:0..50=20,lightIntensity:0..50=10"],
    ["spatter", "Salpicaduras", "brushstrokes", "sprayRadius:0..25=10,smoothness:1..15=5"],
    ["sprayedStrokes", "Trazos pulverizados", "brushstrokes", "strokeLength:0..20=12,sprayRadius:0..25=7,strokeDirection:rightDiagonal|horizontal|leftDiagonal|vertical"],
    ["sumiE", "Sumi-e", "brushstrokes", "strokeWidth:3..15=10,strokePressure:0..15=2,contrast:0..40=16"],
    ["diffuseGlow", "Resplandor difuso", "photo", "graininess:0..10=6,glowAmount:0..20=10,clearAmount:0..20=15"],
    ["glass", "Vidrio", "distort", "distortion:0..20=5,smoothness:1..15=3,texture:frosted|blocks|canvas|tinyLens,scaling:50..200=100,invert:bool"],
    ["oceanRipple", "Rizo oceánico", "distort", "rippleSize:1..15=9,rippleMagnitude:0..20=9"],
    ["basRelief", "Bajorrelieve", "sketch", "detail:1..15=13,smoothness:1..15=3,light:bottom|bottomLeft|left|topLeft|top|topRight|right|bottomRight"],
    ["chalkCharcoal", "Tiza y carboncillo", "sketch", "charcoalArea:0..20=6,chalkArea:0..20=6,strokePressure:0..5=1"],
    ["charcoal", "Carboncillo", "sketch", "charcoalThickness:1..7=1,detail:0..5=5,lightDarkBalance:0..100=50"],
    ["chrome", "Cromo", "sketch", "detail:0..10=4,smoothness:0..10=7"],
    ["conteCrayon", "Crayón Conté", "sketch", "foregroundLevel:1..15=11,backgroundLevel:1..15=7,texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=4,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft,invert:bool"],
    ["graphicPen", "Pluma gráfica", "sketch", "strokeLength:1..15=15,lightDarkBalance:0..100=50,strokeDirection:rightDiagonal|horizontal|leftDiagonal|vertical"],
    ["halftonePattern", "Trama de semitonos", "distort", "size:1..12=1,contrast:0..50=5,patternType:dot|circle|line"],
    ["notePaper", "Papel de cartas", "sketch", "imageBalance:0..50=25,graininess:0..20=10,relief:0..25=11"],
    ["photocopy", "Fotocopia", "sketch", "detail:1..24=7,darkness:1..50=8"],
    ["plaster", "Yeso", "sketch", "imageBalance:0..50=20,smoothness:1..15=2,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft"],
    ["reticulation", "Reticulación", "sketch", "density:0..50=12,foregroundLevel:0..50=40,backgroundLevel:0..50=5"],
    ["stamp", "Sello", "sketch", "lightDarkBalance:0..50=25,smoothness:1..50=5"],
    ["tornEdges", "Bordes rasgados", "sketch", "imageBalance:0..50=25,smoothness:1..15=11,contrast:1..25=17"],
    ["waterPaper", "Papel húmedo", "sketch", "fiberLength:3..50=15,brightness:0..100=60,contrast:0..100=80"],
    ["glowingEdges", "Bordes brillantes", "stylize", "edgeWidth:1..14=2,edgeBrightness:0..20=6,smoothness:1..15=5"],
    ["craquelure", "Craquelado", "texture", "crackSpacing:2..100=15,crackDepth:0..10=6,crackBrightness:0..10=9"],
    ["grain", "Granulado", "texture", "intensity:0..100=40,contrast:0..100=50,grainType:regular|soft|sprinkles|clumped|contrasty|enlarged|stippled|horizontal|vertical|speckle"],
    ["mosaicTiles", "Azulejos de mosaico", "texture", "tileSize:2..100=12,groutWidth:1..15=3,lightenGrout:0..10=9"],
    ["patchwork", "Patchwork", "texture", "squareSize:0..10=4,relief:0..25=8"],
    ["stainedGlass", "Vidriera", "texture", "cellSize:2..50=10,borderThickness:1..20=4,lightIntensity:0..10=3"],
    ["texturizer", "Texturizador", "texture", "texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=4,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft,invert:bool"],
];

const FOREGROUND = new Set(["neonGlow", "basRelief", "chalkCharcoal", "charcoal", "conteCrayon", "graphicPen", "halftonePattern", "notePaper", "photocopy",
                            "plaster", "reticulation", "stamp", "tornEdges", "grain", "stainedGlass"]);
const usesBackground = (k) => k !== "stainedGlass" && (FOREGROUND.has(k) || k === "coloredPencil" || k === "diffuseGlow");

const PARAM_LABELS = {
    pencilWidth: "Ancho del lápiz", strokePressure: "Presión del trazo", paperBrightness: "Brillo del papel", numberOfLevels: "Número de niveles",
    edgeSimplicity: "Simplicidad de los bordes", edgeFidelity: "Fidelidad de los bordes", brushSize: "Tamaño del pincel", brushDetail: "Detalle del pincel",
    texture: "Textura", grain: "Grano", highlightArea: "Área de iluminación", intensity: "Intensidad", glowSize: "Tamaño del resplandor",
    glowBrightness: "Brillo del resplandor", sharpness: "Nitidez", brushType: "Tipo de pincel", strokeSize: "Tamaño del trazo", strokeDetail: "Detalle del trazo",
    softness: "Suavidad", highlightStrength: "Intensidad de las iluminaciones", detail: "Detalle", smoothness: "Suavizado", edgeThickness: "Grosor de los bordes",
    edgeIntensity: "Intensidad de los bordes", posterization: "Posterización", strokeLength: "Longitud del trazo", scaling: "Escala", relief: "Relieve",
    light: "Luz", invert: "Invertir", definition: "Definición", textureCoverage: "Cobertura de la textura", shadowIntensity: "Intensidad de las sombras",
    edgeWidth: "Ancho de los bordes", edgeBrightness: "Brillo de los bordes", directionBalance: "Equilibrio de la dirección", strength: "Fuerza",
    balance: "Equilibrio", blackIntensity: "Intensidad del negro", whiteIntensity: "Intensidad del blanco", darkIntensity: "Intensidad oscura",
    lightIntensity: "Intensidad de la luz", sprayRadius: "Radio de pulverización", strokeDirection: "Dirección del trazo", strokeWidth: "Ancho del trazo",
    contrast: "Contraste", graininess: "Granulado", glowAmount: "Cantidad de resplandor", clearAmount: "Cantidad de claridad", distortion: "Distorsión",
    rippleSize: "Tamaño del rizo", rippleMagnitude: "Magnitud del rizo", charcoalArea: "Área de carboncillo", chalkArea: "Área de tiza",
    charcoalThickness: "Grosor del carboncillo", lightDarkBalance: "Equilibrio claro/oscuro", foregroundLevel: "Nivel del primer plano",
    backgroundLevel: "Nivel del fondo", size: "Tamaño", patternType: "Tipo de trama", imageBalance: "Equilibrio de la imagen", darkness: "Oscuridad",
    density: "Densidad", fiberLength: "Longitud de las fibras", brightness: "Brillo", crackSpacing: "Separación de las grietas",
    crackDepth: "Profundidad de las grietas", crackBrightness: "Brillo de las grietas", grainType: "Tipo de grano", tileSize: "Tamaño de los azulejos",
    groutWidth: "Ancho de la lechada", lightenGrout: "Aclarar la lechada", squareSize: "Tamaño de los cuadrados", cellSize: "Tamaño de las celdas",
    borderThickness: "Grosor del borde",
};
const CHOICE_LABELS = {
    simple: "Simple", lightRough: "Claro y rugoso", darkRough: "Oscuro y rugoso", wideSharp: "Ancho y nítido", wideBlurry: "Ancho y borroso", sparkle: "Chispas",
    canvas: "Lienzo", brick: "Ladrillo", burlap: "Arpillera", sandstone: "Arenisca", frosted: "Escarchado", blocks: "Bloques", tinyLens: "Lentes pequeñas",
    bottom: "Abajo", bottomLeft: "Abajo a la izquierda", left: "Izquierda", topLeft: "Arriba a la izquierda", top: "Arriba", topRight: "Arriba a la derecha",
    right: "Derecha", bottomRight: "Abajo a la derecha", rightDiagonal: "Diagonal derecha", horizontal: "Horizontal", leftDiagonal: "Diagonal izquierda",
    vertical: "Vertical", dot: "Punto", circle: "Círculo", line: "Línea", regular: "Normal", soft: "Suave", sprinkles: "Salpicado", clumped: "Agrupado",
    contrasty: "Contrastado", enlarged: "Ampliado", stippled: "Punteado", speckle: "Moteado",
};

function parseSpec(text) {
    return text.split(",").map((part) => {
        const [key, v] = [part.slice(0, part.indexOf(":")), part.slice(part.indexOf(":") + 1)];
        if (v === "bool")
            return { key, kind: "bool" };
        const m = /^(-?\d+)\.\.(-?\d+)=(-?\d+)$/.exec(v);
        if (m)
            return { key, kind: "range", min: +m[1], max: +m[2], def: +m[3] };
        return { key, kind: "choice", choices: v.split("|") };
    });
}

function applyGallery(key, spec, values, win, bounds) {
    const g = (k) => Number(values[k]);
    const choice = (k) => {
        const s = spec.find((p) => p.key === k);
        return s.choices[values[k] | 0] || s.choices[0];
    };
    const flag = (k) => !!values[k];
    const w = win.w, h = win.h, n = w * h;
    const fg = colorOf(values.foreground, [0, 0, 0]), bg = colorOf(values.background, [1, 1, 1]);
    const forEach = (f) => {
        for (let i = 0; i < n; ++i) f(i, win.ox + (i % w), win.oy + ((i / w) | 0));
    };
    const blur1 = (p, s) => gaussN(p, w, h, 1, s);
    const blur3 = (c, s) => gaussN(c, w, h, 3, s);
    const clone = () => ({ ...win, c: new Float32Array(win.c) });
    switch (key) {
    case "coloredPencil": {
        const pw = g("pencilWidth"), pressure = g("strokePressure") / 15, paper = bg.map((v) => v * (0.7 + (0.3 * g("paperBrightness")) / 50));
        const l = winLum(win), e2 = gradMag(l, w, h);
        forEach((i, x, y) => {
            const d = 1 - l[i];
            const hatch = Math.max(strokes(x, y, 1, 1, 5 + pw * 3, 0.5 + pw * 0.45, 11), strokes(x, y, 1, -1, 5 + pw * 3, 0.5 + pw * 0.45, 12) * smooth(0.3, 0.8, d));
            const cov = clamp01(d * (0.55 + pressure) + e2[i] * 3 - (1 - hatch) * 0.8 + 0.25);
            setPx(win, i, mixPx(paper, mapPx(getPx(win, i), (v) => v * 0.88), cov));
        });
        break;
    }
    case "cutout": {
        blur3(win.c, 0.5 + g("edgeSimplicity") * (1.1 - 0.3 * g("edgeFidelity")));
        const levels = Math.round(g("numberOfLevels"));
        forEach((i) => {
            const p = getPx(win, i), l = Math.max(1e-4, lumPx(p)), k = posterize(l, levels) / l;
            setPx(win, i, mapPx(p, (v) => posterize(clamp01(v * k), levels * 2)));
        });
        break;
    }
    case "dryBrush":
    case "fresco": {
        const fresco = key === "fresco";
        kuwahara(win, 1 + Math.trunc(g("brushSize")));
        unsharp(win, 1, fresco ? 0.9 : 0.5);
        const levels = 3 + g("brushDetail"), amp = 0.02 + (g("texture") - 1) * 0.05;
        const l = fresco ? gradMag(winLum(win), w, h) : null;
        forEach((i, x, y) => {
            const nn = (hash01(x, y, 0, 21) - 0.5) * amp;
            let p = mapPx(getPx(win, i), (v) => posterize(v, levels) * 0.6 + v * 0.4 + nn);
            if (fresco)
                p = mapPx(p, (v) => Math.pow(clamp01((v - 0.5) * 1.3 + 0.45), 1.15) * (1 - clamp01(l[i] * 2.5) * 0.6));
            setPx(win, i, p);
        });
        break;
    }
    case "filmGrain": {
        const grain = g("grain") / 20, area = g("highlightArea"), inten = g("intensity");
        forEach((i, x, y) => {
            const p = getPx(win, i), l = lumPx(p), nn = (hash01(x, y, 0, 31) - 0.5) * grain * 0.5 * (0.3 + 2.8 * l * (1 - l));
            setPx(win, i, highlightsOf(mapPx(p, (v) => v + nn), l, area, inten));
        });
        break;
    }
    case "neonGlow": {
        const size = g("glowSize"), br = g("glowBrightness") / 50, glow = colorOf(values.glowColor, [0, 0.62, 1]);
        const l = winLum(win), m = size >= 0 ? new Float32Array(l) : l.map((v) => 1 - v);
        blur1(m, Math.abs(size) * 0.6 + 0.5);
        forEach((i) => {
            const base = mixPx(fg, bg, l[i]), k = clamp01(m[i] * 1.2) * br * 2;
            setPx(win, i, [0, 1, 2].map((c) => 1 - (1 - base[c]) * (1 - clamp01(glow[c] * k))));
        });
        break;
    }
    case "paintDaubs": {
        const ty = choice("brushType"), wide = ty === "wideSharp" || ty === "wideBlurry";
        kuwahara(win, Math.max(1, Math.trunc((g("brushSize") / 2) * (wide ? 1.5 : 1))));
        const sharp = g("sharpness") / 40;
        if (ty === "wideBlurry")
            blur3(win.c, 1.5);
        else if (ty === "wideSharp")
            unsharp(win, 1.5, sharp * 4);
        else
            unsharp(win, 1.2, sharp * 2.5);
        if (ty === "lightRough" || ty === "darkRough" || ty === "sparkle") {
            const edges = ty === "sparkle" ? gradMag(winLum(win), w, h) : null;
            forEach((i, x, y) => {
                const nn = fbm(x / 2, y / 2, 41) - 0.5, p = getPx(win, i);
                setPx(win, i, ty === "lightRough" ? mapPx(p, (v) => v + Math.max(0, nn) * 0.35)
                    : ty === "darkRough" ? mapPx(p, (v) => v * (1 - Math.max(0, nn) * 0.7))
                        : mapPx(p, (v) => v + clamp01(edges[i] * 4) * smooth(0.75, 0.95, hash01(x, y, 0, 42)) * 0.8));
            });
        }
        break;
    }
    case "paletteKnife": {
        kuwahara(win, Math.max(1, Math.trunc(g("strokeSize") / 3)));
        const levels = 2 + g("strokeDetail") * 3;
        forEach((i) => setPx(win, i, mapPx(getPx(win, i), (v) => posterize(v, levels) * 0.5 + v * 0.5)));
        blur3(win.c, g("softness") * 0.5);
        break;
    }
    case "plasticWrap": {
        const hs = g("highlightStrength") / 20, l = winLum(win);
        blur1(l, 0.5 + g("smoothness") * 0.5 + (15 - g("detail")) * 0.2);
        const [gx, gy] = sobel(l, w, h), lv = normalize3([-0.45, -0.65, 1]), hv = normalize3([lv[0], lv[1], lv[2] + 1]);
        forEach((i) => {
            const nn = normalize3([-14 * gx[i], -14 * gy[i], 1]), ndh = Math.max(0, nn[0] * hv[0] + nn[1] * hv[1] + nn[2] * hv[2]), tilt = 1 - nn[2];
            const spec = Math.pow(ndh, 30) * smooth(0.01, 0.12, tilt) + smooth(0.25, 0.6, tilt) * 0.25;
            setPx(win, i, mapPx(getPx(win, i), (v) => v * 0.88 + spec * hs * 1.3));
        });
        break;
    }
    case "posterEdges": {
        const l = winLum(win);
        blur1(l, 0.6 + g("edgeThickness") * 0.6);
        const edge = gradMag(l, w, h), inten = g("edgeIntensity") / 10, levels = g("posterization") + 2;
        forEach((i) => {
            const ink = smooth(0.015, 0.06, edge[i] * (1 + inten * 3)) * (0.55 + inten * 0.45);
            setPx(win, i, mapPx(getPx(win, i), (v) => posterize(v, levels) * (1 - ink)));
        });
        break;
    }
    case "roughPastels": {
        const len = g("strokeLength"), det = (g("strokeDetail") - 1) / 19;
        if (len >= 1) {
            const src = clone();
            blur3(src.c, 0.6 + (1 - det) * 1.2);
            forEach((i, x, y) => {
                const [c, t] = paintStroke(src, x, y, 1, -1, 2 + len, 1.5, 51);
                const nn = (strokes(x, y, 1, -1, 2 + len * 0.5, 0.8, 52) - 0.5) * 0.12 + (0.5 - Math.abs(t - 0.5)) * 0.06;
                setPx(win, i, mapPx(mixPx(c, getPx(win, i), det * 0.6), (v) => v + nn));
            });
        }
        texturizeBy(win, choice("texture"), g("scaling"), g("relief"), choice("light"), flag("invert"), (p) => 0.15 + 0.85 * (1 - clamp01(lumPx(p))));
        break;
    }
    case "smudgeStick": {
        lineBlur(win.c, 3, w, h, 1, 1, Math.trunc(g("strokeLength") * 1.5));
        const area = g("highlightArea"), inten = g("intensity");
        forEach((i) => {
            const p = getPx(win, i), l = lumPx(p);
            setPx(win, i, highlightsOf(mapPx(p, (v) => v * (0.7 + 0.3 * l)), l, area, inten));
        });
        break;
    }
    case "sponge": {
        kuwahara(win, 1 + Math.trunc(g("brushSize")));
        const def = g("definition") / 25, sc = 1.5 + g("smoothness") * 0.5;
        forEach((i, x, y) => {
            const hole = smooth(0.58, 0.75, fbm(x / sc, y / sc, 61));
            setPx(win, i, mapPx(getPx(win, i), (v) => clamp01((v - 0.5) * (1 + def * 0.6) + 0.5) * (1 - hole * def * 0.4)));
        });
        break;
    }
    case "underpainting": {
        const orig = new Float32Array(win.c);
        blur3(win.c, 0.5 + g("brushSize") * 0.4);
        forEach((i) => setPx(win, i, saturatePx(mixPx([orig[i * 3], orig[i * 3 + 1], orig[i * 3 + 2]], getPx(win, i), 0.8), 1.15)));
        const cov = g("textureCoverage") / 40;
        texturizeBy(win, choice("texture"), g("scaling"), g("relief") * (0.5 + cov * 1.5), choice("light"), flag("invert"));
        break;
    }
    case "watercolor": {
        kuwahara(win, 1 + Math.trunc((15 - g("brushDetail")) / 2.5));
        const sh = g("shadowIntensity") / 10, edge = gradMag(winLum(win), w, h), amp = (g("texture") - 1) * 0.04;
        forEach((i, x, y) => {
            const nn = (fbm(x / 3, y / 3, 71) - 0.5) * amp, k = 1 - clamp01(edge[i] * (3 + sh * 8)) * 0.6;
            setPx(win, i, mapPx(saturatePx(getPx(win, i), 1.2), (v) => Math.pow(clamp01(v * k + nn), 1.1 + sh * 0.6)));
        });
        break;
    }
    case "accentedEdges": {
        const l = winLum(win);
        blur1(l, 0.4 + g("smoothness") * 0.3);
        const edge = gradMag(l, w, h);
        blur1(edge, g("edgeWidth") * 0.5);
        const eb = g("edgeBrightness") / 50;
        forEach((i) => {
            const k = clamp01(edge[i] * 6), p = getPx(win, i);
            setPx(win, i, eb >= 0.5 ? mapPx(p, (v) => v + (1 - v) * k * (eb - 0.5) * 2) : mapPx(p, (v) => v * (1 - k * (0.5 - eb) * 2)));
        });
        break;
    }
    case "angledStrokes": {
        const len = Math.max(3, g("strokeLength")), bal = g("directionBalance") / 100, src = clone();
        blur3(src.c, 0.8);
        const l = winLum(src);
        forEach((i, x, y) => {
            const k = smooth(1 - bal - 0.05, 1 - bal + 0.05, l[i]);
            const down = paintStroke(src, x, y, 1, 1, len, 2, 83)[0], up = paintStroke(src, x, y, 1, -1, len, 2, 84)[0];
            const nn = (strokes(x, y, 1, 1, len * 0.5, 0.7, 85) - 0.5) * (1 - k) + (strokes(x, y, 1, -1, len * 0.5, 0.7, 86) - 0.5) * k;
            setPx(win, i, mapPx(mixPx(down, up, k), (v) => v + nn * 0.1));
        });
        unsharp(win, 1, 0.2 + (g("sharpness") / 10) * 2.3);
        break;
    }
    case "crosshatch": {
        const r = Math.trunc(g("strokeLength") / 3), st = g("strength");
        forEach((i, x, y) => {
            const nn = (hash01(x, y, 0, 81) - 0.5) * 0.12 * st;
            setPx(win, i, mapPx(getPx(win, i), (v) => v + nn));
        });
        const a = new Float32Array(win.c);
        lineBlur(a, 3, w, h, 1, 1, r);
        lineBlur(win.c, 3, w, h, 1, -1, r);
        for (let i = 0; i < win.c.length; ++i) win.c[i] = a[i] + (win.c[i] - a[i]) * 0.5;
        unsharp(win, 1, (g("sharpness") / 20) * 3);
        break;
    }
    case "darkStrokes": {
        const bal = g("balance") / 10, bi = g("blackIntensity") / 10, wi = g("whiteIntensity") / 10, l = winLum(win);
        const a = new Float32Array(win.c);
        lineBlur(a, 3, w, h, 1, 1, 3);
        lineBlur(win.c, 3, w, h, 1, -1, 8);
        const t = 0.25 + bal * 0.5;
        forEach((i) => {
            const d = [a[i * 3], a[i * 3 + 1], a[i * 3 + 2]].map((v) => v * (1 - bi * 0.85));
            const li = mapPx(getPx(win, i), (v) => v + (1 - v) * wi * 0.5);
            setPx(win, i, mixPx(d, li, smooth(t - 0.1, t + 0.1, l[i])));
        });
        break;
    }
    case "inkOutlines": {
        const di = g("darkIntensity") / 50, li = g("lightIntensity") / 50, l = winLum(win), edge = gradMag(l, w, h);
        lineBlur(win.c, 3, w, h, 1, 1, Math.trunc(g("strokeLength") / 2));
        forEach((i) => {
            const ink = clamp01(edge[i] * 5) * di * 1.5, lt = smooth(0.5, 1, l[i]) * li, dk = smooth(0.5, 0, l[i]) * di * 0.8;
            setPx(win, i, mapPx(getPx(win, i), (v) => (v + (1 - v) * lt) * (1 - dk) * (1 - clamp01(ink))));
        });
        break;
    }
    case "spatter": {
        const rad = g("sprayRadius"), feature = 1 + g("smoothness") * 0.35, reach = Math.min((rad * 1.2) / feature, rad), src = clone();
        forEach((i, x, y) => {
            const fx = x / feature, fy = y / feature;
            setPx(win, i, winSample(src, x + 0.5 + (fbm(fx, fy, 91) - 0.5) * 3 * reach, y + 0.5 + (fbm(fx, fy, 92) - 0.5) * 3 * reach));
        });
        break;
    }
    case "sprayedStrokes": {
        const rad = g("sprayRadius"), src = clone();
        forEach((i, x, y) => {
            const fx = x / 1.2, fy = y / 1.2;
            setPx(win, i, winSample(src, x + 0.5 + (vnoise(fx, fy, 91) - 0.5) * 2 * rad, y + 0.5 + (vnoise(fx, fy, 92) - 0.5) * 2 * rad));
        });
        const [dx, dy] = direction(choice("strokeDirection"));
        lineBlur(win.c, 3, w, h, dx, dy, Math.trunc(g("strokeLength") / 2));
        break;
    }
    case "sumiE": {
        const sw = g("strokeWidth"), pr = g("strokePressure") / 15, con = g("contrast") / 40;
        blur3(win.c, sw * 0.2);
        lineBlur(win.c, 3, w, h, 1, 1, Math.trunc(sw / 2));
        forEach((i) => {
            const p = getPx(win, i), ink = Math.pow(smooth(0.65, 0.15, lumPx(p)), 1 - pr * 0.5);
            setPx(win, i, mapPx(p, (v) => clamp01((v - 0.5) * (1 + con) + 0.5) * (1 - ink * 0.9 * (0.5 + con))));
        });
        break;
    }
    case "diffuseGlow": {
        const gr = g("graininess") / 10, ga = g("glowAmount") / 20, ca = g("clearAmount") / 20, thr = 0.2 + ca * 0.7;
        forEach((i, x, y) => {
            const p = getPx(win, i);
            let m = smooth(thr - 0.3, thr + 0.1, lumPx(p)) * ga * 1.4;
            if (hash01(x, y, 0, 101) < gr * 0.3 * (m + 0.15))
                m += 0.5;
            setPx(win, i, mixPx(p, bg, clamp01(m)));
        });
        break;
    }
    case "glass": {
        const dist = g("distortion"), step = 0.5 + g("smoothness") * 0.5, kind = choice("texture"), s = clamp(g("scaling") / 100, 0.5, 2);
        const sign = flag("invert") ? -1 : 1, mx = dist * 2, src = clone();
        forEach((i, x, y) => {
            const gx = texture(kind, x + step, y, s) - texture(kind, x - step, y, s), gy = texture(kind, x, y + step, s) - texture(kind, x, y - step, s);
            setPx(win, i, winSample(src, x + 0.5 + clamp(gx * sign * dist * 3, -mx, mx), y + 0.5 + clamp(gy * sign * dist * 3, -mx, mx)));
        });
        break;
    }
    case "oceanRipple": {
        const s = 2 + g("rippleSize") * 1.2, mag = g("rippleMagnitude") * 0.9, src = clone();
        forEach((i, x, y) => {
            const fx = x / s, fy = y / s;
            setPx(win, i, winSample(src, x + 0.5 + (vnoise(fx, fy, 111) - 0.5) * 2 * mag, y + 0.5 + (vnoise(fx, fy, 112) - 0.5) * 2 * mag));
        });
        break;
    }
    case "basRelief": {
        const l = winLum(win), orig = new Float32Array(l);
        blur1(l, 0.3 + g("smoothness") * 0.5 + (15 - g("detail")) * 0.12);
        const [gx, gy] = sobel(l, w, h), lt = lightDir(choice("light")), k = 3 + g("detail") * 0.4;
        forEach((i) => setPx(win, i, mixPx(fg, bg, clamp01(orig[i] * 0.5 + 0.25 + k * -(gx[i] * lt[0] + gy[i] * lt[1])))));
        break;
    }
    case "chalkCharcoal": {
        const ca = g("charcoalArea") / 20, ch = g("chalkArea") / 20, pr = g("strokePressure") / 5, mid = mixPx(fg, bg, 0.5);
        forEach((i, x, y) => {
            const l = lumPx(getPx(win, i)), cn = strokes(x, y, 1, 1, 9, 0.9, 121) - 0.5, kn = strokes(x, y, 1, -1, 9, 0.9, 122) - 0.5;
            const cc = smooth(0.25, 0.6, (1 - l) * (0.4 + ca * 1.2) + cn * (0.6 + pr * 0.4) - 0.15);
            const kc = smooth(0.25, 0.6, l * (0.4 + ch * 1.2) + kn * (0.6 + pr * 0.4) - 0.15);
            setPx(win, i, mixPx(mixPx(mid, fg, cc), bg, kc * (1 - cc)));
        });
        break;
    }
    case "charcoal": {
        const th = g("charcoalThickness"), det = g("detail") / 5, bal = g("lightDarkBalance") / 100, l = winLum(win);
        blur1(l, 1);
        const edge = gradMag(l, w, h);
        forEach((i, x, y) => {
            const sn = strokes(x, y, 1, 1, 8 + th * 3, 0.6 + th * 0.5, 131) - 0.5;
            setPx(win, i, mixPx(bg, fg, clamp01(smooth(1 - bal - 0.2, 1 - bal + 0.2, 1 - l[i] + sn * 0.6) + clamp01(edge[i] * det * 6))));
        });
        break;
    }
    case "chrome": {
        const det = g("detail") / 10, sm = g("smoothness") / 10, l = winLum(win);
        blur1(l, 1 + sm * 4);
        const edge = gradMag(l, w, h);
        forEach((i) => {
            const v = smooth(0.05, 0.95, 0.5 + 0.5 * Math.sin((l[i] * (2.5 + det * 3) + edge[i] * 6) * 2 * Math.PI));
            setPx(win, i, [v, v, v]);
        });
        break;
    }
    case "conteCrayon": {
        const fl = g("foregroundLevel") / 15, bl = g("backgroundLevel") / 15, mid = mixPx(fg, bg, 0.5);
        forEach((i, x, y) => {
            const l = lumPx(getPx(win, i)), sn = strokes(x, y, 1, -1, 7, 1, 141) - 0.5;
            const cf = clamp01((0.6 - l) * 2 * fl * 1.4 + sn * 0.5), cb = clamp01((l - 0.4) * 2 * bl * 1.4 - sn * 0.5);
            setPx(win, i, mixPx(mixPx(mid, fg, cf), bg, cb * (1 - cf)));
        });
        texturizeBy(win, choice("texture"), g("scaling"), g("relief"), choice("light"), flag("invert"));
        break;
    }
    case "graphicPen": {
        const r = Math.trunc(g("strokeLength") / 2), bal = g("lightDarkBalance") / 100, l = winLum(win), nn = new Float32Array(n);
        forEach((i, x, y) => (nn[i] = hash01(x, y, 0, 151)));
        const [dx, dy] = direction(choice("strokeDirection"));
        lineBlur(nn, 1, w, h, dx, dy, r);
        const sd = 0.2887 / Math.sqrt(2 * r + 1);
        forEach((i) => setPx(win, i, mixPx(bg, fg, smooth(bal + 0.04, bal - 0.04, l[i] + ((nn[i] - 0.5) / sd) * 0.16))));
        break;
    }
    case "halftonePattern": {
        const cell = 1.5 + 2 * clamp(g("size"), 1, 12), gain = 0.5 + g("contrast") * 0.6, offsets = halftoneOffsets(gain), l = winLum(win);
        blur1(l, cell * 0.15);
        const cx = (bounds.left + bounds.right + 1) / 2, cy = (bounds.top + bounds.bottom + 1) / 2, ty = choice("patternType"), dots = halftoneDotRanks();
        const band = (t) => Math.abs(remEuclid(t / cell, 1) - 0.5) * 2;
        forEach((i, x, y) => {
            const fx = x + 0.5, fy = y + 0.5;
            let spot;
            if (ty === "circle")
                spot = band(Math.hypot(fx - cx, fy - cy));
            else if (ty === "line")
                spot = band(fy);
            else {
                const u = remEuclid(((fx + fy) * Math.SQRT1_2) / cell, 1), v = remEuclid(((fy - fx) * Math.SQRT1_2) / cell, 1);
                const f = (Math.cos(2 * Math.PI * u) + Math.cos(2 * Math.PI * v)) / 2;
                let lo = 0, hi = dots.length;
                while (lo < hi) {
                    const mid = (lo + hi) >> 1;
                    if (dots[mid] < f)
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                spot = lo / Math.max(1, dots.length);
            }
            const tone = clamp01(1 - l[i]) * (offsets.length - 1), k = Math.floor(tone), fr = tone - k;
            const at = (j) => offsets[Math.min(j, offsets.length - 1)];
            setPx(win, i, mixPx(bg, fg, clamp01(0.5 + (at(k) + (at(k + 1) - at(k)) * fr - spot) * gain)));
        });
        break;
    }
    case "notePaper": {
        const bal = g("imageBalance") / 50, gr = g("graininess") / 20, rel = g("relief") / 25, l = winLum(win);
        blur1(l, 1);
        const m = l.map((v) => smooth(bal - 0.05, bal + 0.05, v));
        blur1(m, 1.2);
        const [gx, gy] = sobel(m, w, h), tl = lightDir("topLeft");
        forEach((i, x, y) => {
            const nn = (hash01(x, y, 0, 161) - 0.5) * gr * 0.3, f = clamp(shadeOf(gx[i], gy[i], tl, rel * 6), 0, 2);
            setPx(win, i, mapPx(mixPx(fg, bg, m[i]), (v) => (v + nn) * f));
        });
        break;
    }
    case "photocopy": {
        const det = g("detail"), dk = g("darkness") / 50, l = winLum(win), b = new Float32Array(l);
        blur1(b, 1 + det * 0.5);
        forEach((i) => setPx(win, i, mixPx(bg, fg, clamp01(smooth(0, 0.06, -(l[i] - b[i]) * (1 + dk * 8)) + smooth(0.35, 0.05, l[i]) * dk * 1.5))));
        break;
    }
    case "plaster": {
        const bal = g("imageBalance") / 50, l = winLum(win);
        blur1(l, 0.5 + g("smoothness") * 0.8);
        const m = l.map((v) => smooth(bal + 0.1, bal - 0.1, v));
        blur1(m, 1.5);
        const [gx, gy] = sobel(m, w, h), lt = lightDir(choice("light"));
        forEach((i) => {
            const f = clamp(shadeOf(gx[i], gy[i], lt, 6), 0, 2);
            setPx(win, i, mapPx(mixPx(bg, fg, m[i] * 0.6), (v) => v * f));
        });
        break;
    }
    case "reticulation": {
        const den = g("density") / 50, fl = g("foregroundLevel") / 50, bl = g("backgroundLevel") / 50;
        const ink = mixPx(bg, fg, 0.3 + fl * 0.7), paper = mixPx(bg, fg, bl * 0.6);
        forEach((i, x, y) => {
            const nn = 0.5 * hash01(x, y, 0, 171) + 0.5 * vnoise(x / 1.8, y / 1.8, 172);
            setPx(win, i, mixPx(paper, ink, smooth(0.45, 0.55, 1 - lumPx(getPx(win, i)) + (nn - 0.5) * (0.4 + den * 1.2))));
        });
        break;
    }
    case "stamp": {
        const bal = g("lightDarkBalance") / 50, l = winLum(win);
        blur1(l, 0.3 + g("smoothness") * 0.3);
        forEach((i) => setPx(win, i, mixPx(bg, fg, smooth(bal + 0.02, bal - 0.02, l[i]))));
        break;
    }
    case "tornEdges": {
        const bal = g("imageBalance") / 50, sm = g("smoothness"), con = g("contrast"), l = winLum(win);
        blur1(l, 1);
        const rough = ((16 - sm) / 15) * 0.35, aa = 0.25 / con;
        forEach((i, x, y) => setPx(win, i, mixPx(bg, fg, smooth(bal + aa, bal - aa, l[i] + (fbm(x / 2, y / 2, 181) - 0.5) * rough))));
        break;
    }
    case "waterPaper": {
        const fl = g("fiberLength"), br = g("brightness") / 100, co = g("contrast") / 100, r = Math.trunc(fl / 6);
        lineBlur(win.c, 3, w, h, 1, 0, r);
        lineBlur(win.c, 3, w, h, 0, 1, r);
        forEach((i, x, y) => {
            const fib = Math.max(strokes(x, y, 1, 0, fl, 1, 191), strokes(x, y, 0, 1, fl, 1, 192));
            setPx(win, i, mapPx(getPx(win, i), (v) => (v * (0.85 + 0.25 * fib) - 0.5) * (co * 1.6 + 0.2) + 0.5 + (br - 0.5) * 0.8));
        });
        break;
    }
    case "glowingEdges": {
        blur3(win.c, 0.3 + g("smoothness") * 0.25);
        const e3 = new Float32Array(n * 3);
        for (let c = 0; c < 3; ++c) {
            const plane = new Float32Array(n);
            for (let i = 0; i < n; ++i) plane[i] = win.c[i * 3 + c];
            const gm = gradMag(plane, w, h);
            for (let i = 0; i < n; ++i) e3[i * 3 + c] = gm[i];
        }
        gaussN(e3, w, h, 3, g("edgeWidth") * 0.4);
        const k = 2 + g("edgeBrightness") * 1.2;
        for (let i = 0; i < n * 3; ++i) win.c[i] = clamp01(e3[i] * k);
        break;
    }
    case "craquelure": {
        const sp = g("crackSpacing"), dep = g("crackDepth") / 10, bri = g("crackBrightness") / 10, cell = sp * 1.3 + 1, width = 0.8 + dep * 0.8;
        const crack = (x, y) => {
            const [d1, d2] = voronoi(x, y, cell, 201);
            return 1 - smooth(0, width, d2 - d1);
        };
        const tl = lightDir("topLeft");
        forEach((i, x, y) => {
            const fx = x + 0.5, fy = y + 0.5, c0 = crack(fx, fy);
            const f = clamp(shadeOf(-(crack(fx + 1, fy) - c0), -(crack(fx, fy + 1) - c0), tl, dep * 2), 0, 2);
            setPx(win, i, mapPx(getPx(win, i), (v) => v * (0.55 + bri * 0.5) * (1 - c0 * dep * 0.6) * f));
        });
        break;
    }
    case "grain": {
        const it = g("intensity") / 100, co = g("contrast") / 100, ty = choice("grainType");
        forEach((i, x, y) => {
            const hc = (c) => hash01(x, y, c, 211) - 0.5, p = getPx(win, i);
            let q;
            switch (ty) {
            case "soft": q = [p[0] + hc(0) * it * 0.4, p[1] + hc(1) * it * 0.4, p[2] + hc(2) * it * 0.4]; break;
            case "sprinkles": q = hash01(x, y, 3, 211) < it * 0.15 ? bg : p; break;
            case "clumped": q = mapPx(p, (v) => v + (vnoise(x / 2, y / 2, 212) - 0.5) * it * 1.2); break;
            case "contrasty": q = mapPx(p, (v) => (v - 0.5) * (1 + it * 2) + 0.5 + hc(4) * it * 0.5); break;
            case "enlarged": q = [p[0] + (vnoise(x / 3, y / 3, 213) - 0.5) * it, p[1] + (vnoise(x / 3, y / 3, 214) - 0.5) * it, p[2] + (vnoise(x / 3, y / 3, 215) - 0.5) * it]; break;
            case "stippled": q = mixPx(p, lumPx(p) > hash01(x, y, 5, 211) ? bg : fg, 0.3 + it * 0.7); break;
            case "horizontal": q = mapPx(p, (v) => v + (vnoise(x / 25, y, 216) - 0.5) * it); break;
            case "vertical": q = mapPx(p, (v) => v + (vnoise(x, y / 25, 217) - 0.5) * it); break;
            case "speckle": q = hash01(x, y, 6, 211) < it * 0.1 ? mixPx(p, fg, 0.85) : p; break;
            default: q = [p[0] + hc(0) * it * 0.8, p[1] + hc(1) * it * 0.8, p[2] + hc(2) * it * 0.8];
            }
            setPx(win, i, mapPx(q, (v) => (v - 0.5) * (0.5 + co) + 0.5));
        });
        break;
    }
    case "mosaicTiles": {
        const ts = g("tileSize"), gw = g("groutWidth"), lg = g("lightenGrout") / 10;
        const edge = (x, y) => {
            const jx = x + (vnoise((x / ts) * 0.7, (y / ts) * 0.7, 221) - 0.5) * ts * 0.3, jy = y + (vnoise((x / ts) * 0.7, (y / ts) * 0.7, 222) - 0.5) * ts * 0.3;
            const u = remEuclid(jx, ts), v = remEuclid(jy, ts);
            return Math.min(u, ts - u, v, ts - v);
        };
        forEach((i, x, y) => {
            const fx = x + 0.5, fy = y + 0.5, grout = smooth(gw * 0.5 + 0.7, gw * 0.5 - 0.3, edge(fx, fy));
            const hgt = (xx, yy) => smooth(gw * 0.5, gw * 0.5 + 2.5, edge(xx, yy));
            const s = (hgt(fx - 1, fy - 1) - hgt(fx + 1, fy + 1)) * 0.35, p = getPx(win, i), gv = clamp01(lumPx(p) * 0.5 + lg * 0.45);
            setPx(win, i, mixPx(mapPx(p, (v) => v * (1 + s)), [gv, gv, gv], grout));
        });
        break;
    }
    case "patchwork": {
        const size = 3 + g("squareSize") * 1.5, rel = g("relief") / 25, src = clone();
        const hv = (x, y) => {
            const u = remEuclid(x, size), v = remEuclid(y, size);
            return hash01(Math.floor(x / size), Math.floor(y / size), 0, 231) * smooth(0, 1.5, Math.min(u, size - u, v, size - v));
        };
        forEach((i, x, y) => {
            const fx = x + 0.5, fy = y + 0.5, c = winAt(src, Math.trunc((Math.floor(fx / size) + 0.5) * size), Math.trunc((Math.floor(fy / size) + 0.5) * size));
            const s = (hv(fx - 1, fy - 1) - hv(fx + 1, fy + 1)) * rel * 1.5;
            setPx(win, i, mapPx(c, (v) => v * (1 + s)));
        });
        break;
    }
    case "stainedGlass": {
        const cell = g("cellSize") * 1.6 + 1, bt = g("borderThickness"), li = g("lightIntensity") / 10;
        const cx = (bounds.left + bounds.right + 1) / 2, cy = (bounds.top + bounds.bottom + 1) / 2, half = Math.max(1, Math.max(bounds.width, bounds.height) / 2), src = clone();
        forEach((i, x, y) => {
            const fx = x + 0.5, fy = y + 0.5, [d1, d2, p] = voronoi(fx, fy, cell, 241);
            const border = smooth(bt * 0.5 + 0.6, bt * 0.5 - 0.4, (d2 - d1) * 0.5), dn = Math.min(1, Math.hypot(fx - cx, fy - cy) / half);
            setPx(win, i, mixPx(mapPx(winAt(src, Math.trunc(p[0]), Math.trunc(p[1])), (c) => c * (1 + li * 0.6 * (1 - dn))), fg, border));
        });
        break;
    }
    case "texturizer":
        texturizeBy(win, choice("texture"), g("scaling"), g("relief"), choice("light"), flag("invert"));
        break;
    }
}

function galleryEffects() {
    return GALLERY.map(([key, name, category, specText]) => {
        const spec = parseSpec(specText);
        const params = spec.map((s) => s.kind === "range" ? P.int(s.key, PARAM_LABELS[s.key] || s.key, s.min, s.max, s.def)
            : s.kind === "bool" ? P.bool(s.key, PARAM_LABELS[s.key] || s.key, false)
                : P.choice(s.key, PARAM_LABELS[s.key] || s.key, s.choices.map((c) => CHOICE_LABELS[c] || c), 0));
        if (FOREGROUND.has(key))
            params.push(P.color("foreground", "Primer plano", "primary"));
        if (usesBackground(key))
            params.push(P.color("background", key === "coloredPencil" ? "Papel" : "Fondo", "secondary"));
        if (key === "neonGlow")
            params.push(P.color("glowColor", "Color del resplandor", { r: 0, g: 158, b: 255, a: 255 }));
        return {
            id: `pc${key[0].toUpperCase()}${key.slice(1)}`, name, category, params,
            ...whole((b, a) => {
                const win = makeWin(b, a.roi);
                applyGallery(key, spec, a.values, win, { left: 0, top: 0, right: a.roi.width - 1, bottom: a.roi.height - 1, width: a.roi.width, height: a.roi.height });
                for (let i = 0, n = b.w * b.h; i < n; ++i)
                    for (let c = 0; c < 3; ++c) b.d[i * 4 + c] = clamp01(win.c[i * 3 + c]);
            }),
        };
    });
}

// --- Registro ------------------------------------------------------------------------------

const WRAP_REPEAT = ["Ajustar", "Repetir los píxeles del borde"];
const FAMILIES = ["Rojos", "Amarillos", "Verdes", "Cianes", "Azules", "Magentas", "Blancos", "Neutros", "Negros"];

function mixerParams() {
    const keys = [["rr", "rg", "rb", "rc"], ["gr", "gg", "gb", "gc"], ["br", "bg", "bb", "bc"]];
    const outs = ["Rojo", "Verde", "Azul"], ins = ["del rojo", "del verde", "del azul", "constante"];
    const list = [];
    for (let o = 0; o < 3; ++o)
        for (let i = 0; i < 4; ++i)
            list.push({ ...P.int(keys[o][i], `${outs[o]} ${ins[i]}`, -200, 200, i === o ? 100 : 0), tab: outs[o] });
    list.push(P.bool("monochrome", "Monocromo", false));
    return list;
}

function channelMixer(c, v) {
    const k = [["rr", "rg", "rb", "rc"], ["gr", "gg", "gb", "gc"], ["br", "bg", "bb", "bc"]];
    const mix = (o) => clamp01((v[k[o][0]] * c[0] + v[k[o][1]] * c[1] + v[k[o][2]] * c[2] + v[k[o][3]]) / 100);
    if (v.monochrome) {
        const gr = mix(0);
        c[0] = c[1] = c[2] = gr;
    } else {
        const r = mix(0), g = mix(1), b = mix(2);
        c[0] = r, c[1] = g, c[2] = b;
    }
}

const photoFilterColor = { ...P.color("color", "Color", { r: 236, g: 138, b: 0, a: 255 }),
                           readOnlyWhen: [{ key: "filter", values: PHOTO_FILTERS.map((_, i) => i) }] };

export const extraEffects = [
    { id: "pcVibrance", name: "Intensidad", params: [P.int("vibrance", "Intensidad", -100, 100, 0), P.int("saturation", "Saturación", -100, 100, 0)], ...perPixel(vibrance) },
    { id: "pcThreshold", name: "Umbral", params: [P.int("level", "Nivel", 1, 255, 128)], ...perPixel(threshold) },
    { id: "pcGradientMap", name: "Mapa de degradado",
      params: [P.color("from", "Color inicial", "primary"), P.color("to", "Color final", "secondary"), P.bool("reverse", "Invertir", false), P.bool("dither", "Tramado", false)],
      ...whole(gradientMap) },
    { id: "pcChannelMixer", name: "Mezclador de canales", params: mixerParams(), ...perPixel(channelMixer) },
    { id: "pcPhotoFilter", name: "Filtro de fotografía",
      params: [P.choice("filter", "Filtro", [...PHOTO_FILTERS.map((f) => f[0]), "Color"], 0), photoFilterColor, P.int("density", "Densidad", 1, 100, 25),
               P.bool("preserveLuminosity", "Conservar la luminosidad", true)], ...perPixel(photoFilter) },
    { id: "pcSelectiveColor", name: "Corrección selectiva",
      params: [P.choice("colors", "Colores", FAMILIES, 0), P.int("cyan", "Cian", -100, 100, 0), P.int("magenta", "Magenta", -100, 100, 0),
               P.int("yellow", "Amarillo", -100, 100, 0), P.int("black", "Negro", -100, 100, 0), P.choice("method", "Método", ["Relativo", "Absoluto"], 0)],
      ...perPixel(selectiveColor) },
    { id: "pcEqualize", name: "Ecualizar", params: [], ...whole(equalize) },
    { id: "pcAutoContrast", name: "Contraste automático", params: [], ...whole(autoContrast) },
    { id: "pcAutoColor", name: "Color automático", params: [], ...whole(autoColor) },
    { id: "pcBlackAndWhiteMixer", name: "Mezclador de blanco y negro",
      params: [...BW_KEYS.map((k, i) => P.int(k, ["Rojos", "Amarillos", "Verdes", "Cianes", "Azules", "Magentas"][i], -200, 300, [40, 60, 40, 60, 20, 80][i])),
               P.bool("tint", "Matiz", false), P.color("tintColor", "Color del matiz", { r: 225, g: 211, b: 179, a: 255 })], ...perPixel(blackWhite) },
    { id: "pcHsbHsl", name: "HSB/HSL", category: "color",
      params: [P.choice("input", "Modo de entrada", ["RGB", "HSB", "HSL"], 0), P.choice("output", "Modo de salida", ["RGB", "HSB", "HSL"], 1)], ...perPixel(hsbHsl) },
    { id: "pcOffset", name: "Desplazamiento", category: "distort",
      params: [P.int("horizontal", "Horizontal", -65535, 65535, 0), P.int("vertical", "Vertical", -65535, 65535, 0),
               P.choice("undefined", "Zonas sin definir", [...WRAP_REPEAT, "Transparente"], 0)], ...whole(offsetEffect) },
    { id: "pcPolarCoordinates", name: "Coordenadas polares", category: "distort",
      params: [P.choice("mode", "Modo", ["De rectangulares a polares", "De polares a rectangulares"], 0)], ...whole(polarCoordinates) },
    { id: "pcWave", name: "Onda", category: "distort",
      params: [P.int("generators", "Número de generadores", 1, 999, 5), P.int("wavelengthMin", "Longitud de onda mínima", 1, 998, 10),
               P.int("wavelengthMax", "Longitud de onda máxima", 2, 999, 120), P.int("amplitudeMin", "Amplitud mínima", 1, 998, 5),
               P.int("amplitudeMax", "Amplitud máxima", 1, 999, 35), P.choice("type", "Tipo", ["Seno", "Triángulo", "Cuadrado"], 0),
               P.choice("undefined", "Zonas sin definir", WRAP_REPEAT, 0), P.seed()], ...whole(wave) },
    { id: "pcRipple", name: "Rizo", category: "distort",
      params: [P.int("amount", "Cantidad", -999, 999, 100), P.choice("size", "Tamaño", ["Pequeño", "Mediano", "Grande"], 1)], ...whole(ripple) },
    { id: "pcZigZag", name: "Zigzag", category: "distort",
      params: [P.int("amount", "Cantidad", -100, 100, 10), P.int("ridges", "Crestas", 0, 20, 5),
               P.choice("style", "Estilo", ["Ondas de estanque", "Desde el centro", "Alrededor del centro"], 0)], ...whole(zigzag) },
    { id: "pcShear", name: "Inclinar", category: "distort",
      params: [P.int("top", "Arriba", -100, 100, 30), P.int("middle", "Centro", -100, 100, 0), P.int("bottom", "Abajo", -100, 100, -30),
               P.choice("undefined", "Zonas sin definir", WRAP_REPEAT, 0)], ...whole(shear) },
    { id: "pcSolarize", name: "Solarizar", category: "stylize", params: [], ...perPixel((c) => {
        for (let i = 0; i < 3; ++i) if (c[i] > 0.5) c[i] = 1 - c[i];
    }) },
    { id: "pcHighPass", name: "Paso alto", category: "stylize", params: [P.real("radius", "Radio", 0.1, 250, 10, 1)], ...whole(highPass) },
    { id: "pcColorToAlpha", name: "Color a alfa", category: "object",
      params: [P.color("color", "Color", "white"), P.int("transparency", "Umbral de transparencia", 0, 100, 0), P.int("opacity", "Umbral de opacidad", 0, 100, 100)],
      ...whole(colorToAlpha) },
    { id: "pcRemoveBlackMatte", name: "Quitar halo negro", category: "object", params: [], ...whole(removeMatte(false)) },
    { id: "pcRemoveWhiteMatte", name: "Quitar halo blanco", category: "object", params: [], ...whole(removeMatte(true)) },
    { id: "pcAverage", name: "Media", category: "blurs", params: [], ...whole(averageBlur) },
    ...galleryEffects(),
].map((e) => ({ ...e, straight: true, origin: ORIGIN }));

// --- Los que siguen complementos de BoltBait (como BoltBaitEffects.cpp de Kaint) --------------
// Se activan uno a uno desde Explorar complementos. "Detección de bordes clásica" y "Nitidez
// clásica" son los de Kaint con otro nombre (los crea Effects.js).

// La tabla de Brillo y contraste de Paint.NET (la de Kaint y la de Effects.js): apply(r, g, b) con
// valores de 0..255 da el color ajustado.
function brightnessContrast(brightness, contrast) {
    brightness = Math.round(brightness);
    contrast = Math.round(contrast);
    const [multiply, divide] = contrast < 0 ? [contrast + 100, 100] : contrast > 0 ? [100, 100 - contrast] : [1, 1];
    const table = new Uint8Array(65536);
    if (divide === 0) {
        for (let i = 0; i < 256; ++i) table[i] = i + brightness < 128 ? 0 : 255;
    } else {
        for (let i = 0; i < 256; ++i) {
            const shift = divide === 100 ? Math.trunc(((i - 127) * multiply) / divide) + 127 - i + brightness
                                         : Math.trunc(((i - 127 + brightness) * multiply) / divide) + 127 - i;
            for (let col = 0; col < 256; ++col) table[i * 256 + col] = clamp(col + shift, 0, 255);
        }
    }
    return (r, g, b) => {
        const i = (7471 * b + 38470 * g + 19595 * r) >> 16;
        if (divide === 0)
            return [table[i], table[i], table[i]];
        return [table[i * 256 + r], table[i * 256 + g], table[i * 256 + b]];
    };
}

function blackWhitePlus(b, a) {
    const type = a.values.type | 0, bc = brightnessContrast(a.values.brightness, a.values.contrast);
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4, p = [b.d[o], b.d[o + 1], b.d[o + 2]];
            const g = [0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2], (p[0] + p[1] + p[2]) / 3, (Math.max(...p) + Math.min(...p)) / 2, p[0], p[1], p[2],
                       Math.max(...p), Math.min(...p)][type] ?? 0;
            const v = to8(g), out = bc(v, v, v);
            b.d[o] = out[0] / 255;
            b.d[o + 1] = out[1] / 255;
            b.d[o + 2] = out[2] / 255;
        }
}

function combinedAdjustments(b, a) {
    const bc = brightnessContrast(a.values.brightness, a.values.contrast), sat = 1 + a.values.saturation / 100, warm = (a.values.temperature / 100) * 0.12;
    for (let y = a.roi.top; y <= a.roi.bottom; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const o = (y * b.w + x) * 4, c = bc(to8(b.d[o]), to8(b.d[o + 1]), to8(b.d[o + 2])).map((v) => v / 255);
            const l = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
            for (let i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * sat;
            c[0] += warm;
            c[2] -= warm;
            for (let i = 0; i < 3; ++i) b.d[o + i] = clamp01(c[i]);
        }
}

function dropShadowPlus(b, a) {
    const { offsetX: dx, offsetY: dy, blur, opacity, shadowOnly } = a.values, color = colorOf(a.values.color), r = a.roi;
    const shadow = { w: b.w, h: b.h, d: new Float32Array(b.d) };
    for (let y = r.top; y <= r.bottom; ++y)
        for (let x = r.left; x <= r.right; ++x) {
            const sx = x - dx, sy = y - dy, o = (y * b.w + x) * 4;
            const al = sx >= r.left && sx <= r.right && sy >= r.top && sy <= r.bottom ? b.d[(sy * b.w + sx) * 4 + 3] : 0;
            shadow.d[o] = color[0];
            shadow.d[o + 1] = color[1];
            shadow.d[o + 2] = color[2];
            shadow.d[o + 3] = (al * opacity) / 100;
        }
    gaussian(shadow, r, blur / 2);
    for (let y = r.top; y <= r.bottom; ++y)
        for (let x = r.left; x <= r.right; ++x) {
            const o = (y * b.w + x) * 4;
            if (shadowOnly) {
                for (let c = 0; c < 4; ++c) b.d[o + c] = shadow.d[o + c];
                continue;
            }
            const ta = b.d[o + 3], sa = shadow.d[o + 3] * (1 - ta), outA = ta + sa;
            for (let c = 0; c < 3; ++c) b.d[o + c] = outA > 1e-6 ? (b.d[o + c] * ta + shadow.d[o + c] * sa) / outA : 0;
            b.d[o + 3] = outA;
        }
}

function levelHorizon(b, a) {
    const angle = (a.values.angle * Math.PI) / 180, r = a.roi, cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    let zoom = 1;
    if (a.values.fill) {
        const c = Math.abs(Math.cos(angle)), s = Math.abs(Math.sin(angle));
        zoom = Math.max((r.width * c + r.height * s) / r.width, (r.width * s + r.height * c) / r.height);
    }
    const ca = Math.cos(-angle) / zoom, sa = Math.sin(-angle) / zoom;
    remap(b, r, EDGE_TRANSPARENT, (x, y) => {
        const dx = x - cx, dy = y - cy;
        return [cx + dx * ca - dy * sa, cy + dx * sa + dy * ca];
    });
}

export const boltBaitEffects = [
    { id: "bbComplementaryColors", name: "Colores complementarios", category: "color", params: [], ...perPixel((c) => {
        const mx = Math.max(c[0], c[1], c[2]), mn = Math.min(c[0], c[1], c[2]);
        for (let i = 0; i < 3; ++i) c[i] = mx + mn - c[i];
    }) },
    { id: "bbBlackAndWhitePlus", name: "Blanco y negro+",
      params: [P.choice("type", "Tipo", ["Luminosidad", "Promedio", "Desaturar", "Canal rojo", "Canal verde", "Canal azul", "Máximo", "Mínimo"], 0),
               P.int("brightness", "Brillo", -100, 100, 0), P.int("contrast", "Contraste", -100, 100, 0)], ...whole(blackWhitePlus) },
    { id: "bbCombinedAdjustments", name: "Ajustes combinados",
      params: [P.int("brightness", "Brillo", -100, 100, 0), P.int("contrast", "Contraste", -100, 100, 0), P.int("saturation", "Saturación", -100, 100, 0),
               P.int("temperature", "Temperatura", -100, 100, 0)], ...whole(combinedAdjustments) },
    { id: "bbDropShadowPlus", name: "Sombra paralela+", category: "object",
      params: [P.int("offsetX", "Desplazamiento horizontal", -200, 200, 6), P.int("offsetY", "Desplazamiento vertical", -200, 200, 6),
               P.int("blur", "Desenfoque", 0, 100, 10), P.int("opacity", "Opacidad", 0, 100, 60), P.color("color", "Color", "black"),
               P.bool("shadowOnly", "Solo la sombra", false)], ...whole(dropShadowPlus) },
    { id: "bbLevelHorizon", name: "Nivelar el horizonte", category: "photo",
      params: [P.real("angle", "Ángulo", -45, 45, 0, 2), P.bool("fill", "Ampliar para rellenar", true)], ...whole(levelHorizon) },
].map((e) => ({ ...e, straight: true, origin: "BoltBait" }));

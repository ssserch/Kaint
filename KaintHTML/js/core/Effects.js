// Ajustes y efectos de Kaint (src/core/effects): los mismos nombres, categorías, atajos,
// parámetros y algoritmos, portados a JavaScript. Como en Kaint, los ajustes trabajan con el alfa
// directo de la capa y los efectos espaciales con alfa premultiplicado de 8 bits; cada píxel se
// guarda en un entero de 32 bits (los mismos bytes que ImageData: 0xAABBGGRR).
// Cada efecto recibe la capa (ImageData), sus valores y el rectángulo que debe calcular, y
// devuelve un ImageData nuevo.

export const Categories = {
    artistic: "Artístico", blurs: "Desenfoques", color: "Color", distort: "Distorsionar", noise: "Ruido", object: "Objeto",
    photo: "Fotografía", render: "Representación", stylize: "Estilizado",
};

// --- Parámetros ---------------------------------------------------------------------------

export function defaultCurves() {
    return { mode: 0, points: [0, 1, 2, 3].map(() => [[0, 0], [255, 255]]) };
}

export function defaultLevels() {
    return { inLow: [0, 0, 0], inHigh: [255, 255, 255], outLow: [0, 0, 0], outHigh: [255, 255, 255], gamma: [1, 1, 1] };
}

const P = {
    int: (key, label, min, max, def) => ({ key, label, type: "int", min, max, def, decimals: 0 }),
    real: (key, label, min, max, def, decimals = 2) => ({ key, label, type: "real", min, max, def, decimals }),
    bool: (key, label, def) => ({ key, label, type: "bool", def }),
    choice: (key, label, choices, def = 0) => ({ key, label, type: "choice", choices, min: 0, max: choices.length - 1, def }),
    seed: (label = "Aleatorizar") => ({ key: "seed", label, type: "seed", min: 0, max: 255, def: 0 }),
    angle: (key, label, min, max, def) => ({ key, label, type: "angle", min, max, def, decimals: 2 }),
    offset: (key, label) => ({ key, label, type: "offset", min: -1, max: 1, minY: -1, maxY: 1, decimals: 2, def: { x: 0, y: 0 } }),
    // def: "primary", "secondary", "black", "white" o un color { r, g, b, a }.
    color: (key, label, def) => ({ key, label, type: "color", def }),
    curves: (key) => ({ key, label: "", type: "curves", def: defaultCurves() }),
    levels: (key) => ({ key, label: "", type: "levels", def: defaultLevels() }),
    // Rotación y giro: { x: ángulo, y: dirección de la inclinación, z: inclinación }.
    rollBall: (key, label, maxTilt = 90) => ({ key, label, type: "rollball", min: -maxTilt, max: maxTilt, decimals: 2, def: { x: 0, y: 0, z: 0 } }),
    text: (key, label, def, multiline = false) => ({ key, label, type: "text", def, multiline }),
};
// Valores vinculados: con la casilla 'key' marcada, cambiar uno cambia los demás.
const linked = (p, key) => ({ ...p, link: key });
const withProps = (p, props) => ({ ...p, ...props });

// --- Píxeles ------------------------------------------------------------------------------

const qRed = (p) => p & 255;
const qGreen = (p) => (p >>> 8) & 255;
const qBlue = (p) => (p >>> 16) & 255;
const qAlpha = (p) => p >>> 24;
const qRgba = (r, g, b, a) => ((a << 24) | (b << 16) | (g << 8) | r) >>> 0;
const qRgb = (r, g, b) => qRgba(r, g, b, 255);

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const clamp255 = (v) => (v < 0 ? 0 : v > 255 ? 255 : v);
const clamp255f = (v) => (v <= 0 ? 0 : v >= 255 ? 255 : (v + 0.5) | 0);
// División entera de C++ (trunca hacia cero) y redondeo de std::lround.
const idiv = (a, b) => Math.trunc(a / b);
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));

function qPremultiply(p) {
    const a = p >>> 24;
    if (a === 255)
        return p;
    if (a === 0)
        return 0;
    const m = (c) => {
        const t = c * a;
        return (t + (t >> 8) + 0x80) >> 8;
    };
    return qRgba(m(p & 255), m((p >>> 8) & 255), m((p >>> 16) & 255), a);
}

const INV_PREMUL = new Float64Array(256).map((_, i) => (i ? Math.round((255 * 65536) / i) : 0));
function qUnpremultiply(p) {
    const a = p >>> 24;
    if (a === 255)
        return p;
    if (a === 0)
        return 0;
    const f = INV_PREMUL[a];
    const u = (c) => Math.min(255, Math.floor((c * f + 0x8000) / 65536));
    return qRgba(u(p & 255), u((p >>> 8) & 255), u((p >>> 16) & 255), a);
}

// Imagen de trabajo: { w, h, px: Uint32Array, premul }.
const image = (w, h, premul = true) => ({ w, h, px: new Uint32Array(w * h), premul });
const at = (img, x, y) => img.px[y * img.w + x];
const clamped = (img, x, y) => img.px[clamp(y, 0, img.h - 1) * img.w + clamp(x, 0, img.w - 1)];

// Rectángulos al estilo de QRect: left/top/right/bottom incluidos.
function rect(x, y, w, h) {
    return { left: x, top: y, right: x + w - 1, bottom: y + h - 1, width: Math.max(0, w), height: Math.max(0, h) };
}
const imageRect = (img) => rect(0, 0, img.w, img.h);
const isEmpty = (r) => r.width <= 0 || r.height <= 0;
function intersected(a, b) {
    const l = Math.max(a.left, b.left), t = Math.max(a.top, b.top);
    const r = Math.min(a.right, b.right), bo = Math.min(a.bottom, b.bottom);
    return rect(l, t, r - l + 1, bo - t + 1);
}
const adjusted = (r, m) => rect(r.left - m, r.top - m, r.width + 2 * m, r.height + 2 * m);
const translated = (r, dx, dy) => rect(r.left + dx, r.top + dy, r.width, r.height);
// Filas que calcula esta ejecución: toda la zona o, al repartir el trabajo, una franja.
const band = (a) => rect(a.roi.left, a.y0, a.roi.width, a.y1 - a.y0 + 1);

function crop(img, r) {
    const out = image(r.width, r.height, img.premul);
    for (let y = 0; y < r.height; ++y)
        out.px.set(img.px.subarray((r.top + y) * img.w + r.left, (r.top + y) * img.w + r.left + r.width), y * r.width);
    return out;
}

function blit(dst, src, x0, y0) {
    for (let y = 0; y < src.h; ++y)
        dst.px.set(src.px.subarray(y * src.w, (y + 1) * src.w), (y0 + y) * dst.w + x0);
}

function straightCopy(img) {
    const out = image(img.w, img.h, false);
    if (img.premul)
        for (let i = 0; i < img.px.length; ++i) out.px[i] = qUnpremultiply(img.px[i]);
    else
        out.px.set(img.px);
    return out;
}

// Píxel por píxel con colores sin premultiplicar. f(color directo) → color directo.
function mapStraight(a, f) {
    const s = a.src.px, d = a.dst.px, w = a.src.w, pm = a.src.premul;
    for (let y = a.y0; y <= a.y1; ++y)
        for (let x = a.roi.left, i = y * w + a.roi.left; x <= a.roi.right; ++x, ++i)
            d[i] = pm ? qPremultiply(f(qUnpremultiply(s[i]))) : f(s[i]);
}

function applyLuts(a, lr, lg, lb) {
    mapStraight(a, (c) => qRgba(lr[qRed(c)], lg[qGreen(c)], lb[qBlue(c)], qAlpha(c)));
}

// Intensidad de Paint.NET (ColorBgra.GetIntensityByte).
const intensity = (c) => (7471 * qBlue(c) + 38470 * qGreen(c) + 19595 * qRed(c)) >> 16;
const intensityF = (c) => (0.114 * qBlue(c) + 0.587 * qGreen(c) + 0.299 * qRed(c)) / 255;

// Interpolación de Paint.NET (ColorBgra.Lerp), admite t fuera de 0..1.
function lerpColor(a, b, t) {
    const l = (x, y) => clamp255(Math.trunc(x + (y - x) * t));
    return qRgba(l(qRed(a), qRed(b)), l(qGreen(a), qGreen(b)), l(qBlue(a), qBlue(b)), l(qAlpha(a), qAlpha(b)));
}

// Fórmula generalizada de mezcla de Paint.NET sobre colores directos.
function blendStraight(lhs, rhs, f) {
    const la = qAlpha(lhs), ra = qAlpha(rhs);
    const y = idiv(la * (255 - ra) + 127, 255);
    const total = y + ra;
    if (total === 0)
        return 0;
    const x = idiv(la * ra + 127, 255);
    const z = ra - x;
    const ch = (A, B) => clamp255(idiv(A * y + B * z + f(A, B) * x, total));
    return qRgba(ch(qRed(lhs), qRed(rhs)), ch(qGreen(lhs), qGreen(rhs)), ch(qBlue(lhs), qBlue(rhs)), total);
}
const opScreen = (a, b) => b + a - idiv(a * b + 127, 255);
const opDarken = (a, b) => Math.min(a, b);
const opOverlay = (a, b) => (a < 128 ? idiv(2 * a * b + 127, 255) : 255 - idiv(2 * (255 - a) * (255 - b) + 127, 255));
const opColorDodge = (a, b) => (b === 255 ? 255 : Math.min(255, idiv(a * 255, 255 - b)));

// "Source over" con píxeles premultiplicados.
function overPixel(dst, src) {
    const sa = qAlpha(src);
    if (sa === 255)
        return src;
    if (sa === 0)
        return dst;
    const m = 255 - sa;
    const sc = (c) => {
        const t = c * m;
        return (t + (t >> 8) + 0x80) >> 8;
    };
    return qRgba(Math.min(255, qRed(src) + sc(qRed(dst))), Math.min(255, qGreen(src) + sc(qGreen(dst))),
                 Math.min(255, qBlue(src) + sc(qBlue(dst))), Math.min(255, sa + sc(qAlpha(dst))));
}

// Tabla de Brillo y contraste de Paint.NET (también la usan Resplandor, Bocetos...).
class BrightnessContrast {
    constructor(brightness, contrast) {
        let multiply, divide;
        if (contrast < 0) {
            multiply = contrast + 100;
            divide = 100;
        } else if (contrast > 0) {
            multiply = 100;
            divide = 100 - contrast;
        } else {
            multiply = 1;
            divide = 1;
        }
        this.divide = divide;
        this.table = new Uint8Array(65536);
        if (divide === 0) {
            for (let i = 0; i < 256; ++i)
                this.table[i] = i + brightness < 128 ? 0 : 255;
            return;
        }
        for (let i = 0; i < 256; ++i) {
            const shift = divide === 100 ? idiv((i - 127) * multiply, divide) + 127 - i + brightness
                                         : idiv((i - 127 + brightness) * multiply, divide) + 127 - i;
            for (let col = 0; col < 256; ++col)
                this.table[i * 256 + col] = clamp255(col + shift);
        }
    }

    apply(c) {
        const i = intensity(c), t = this.table;
        if (this.divide === 0) {
            const v = t[i];
            return qRgba(v, v, v, qAlpha(c));
        }
        const base = i * 256;
        return qRgba(t[base + qRed(c)], t[base + qGreen(c)], t[base + qBlue(c)], qAlpha(c));
    }
}

// sRGB <-> lineal (Viñeta, Exposición...).
function toLinear(c) {
    const v = c / 255;
    return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}

function toSrgb(l) {
    l = clamp(l, 0, 1);
    const v = l <= 0.0031308 ? l * 12.92 : 1.055 * Math.pow(l, 1 / 2.4) - 0.055;
    return clamp255f(v * 255);
}

// Generador pseudoaleatorio sin estado: el mismo (x, y, semilla) siempre da el mismo valor.
function hash3(x, y, seed) {
    let h = (Math.imul(x, 0x8da6b343) ^ Math.imul(y, 0xd8163841) ^ Math.imul(seed, 0xcb1ab31f)) >>> 0;
    h ^= h >>> 15;
    h = Math.imul(h, 0x2c1b3c6d) >>> 0;
    h ^= h >>> 12;
    h = Math.imul(h, 0x297a2d39) >>> 0;
    h ^= h >>> 15;
    return h >>> 0;
}
const hashUnit = (x, y, seed) => (hash3(x, y, seed) & 0xffffff) / 0x1000000;

// --- Muestreo -----------------------------------------------------------------------------

// Muestreo bilineal (coordenadas de centro de píxel) con el borde repetido.
function sampleBilinear(src, x, y) {
    if (!(x === x)) x = -1;
    if (!(y === y)) y = -1;
    x = Math.min(Math.max(x, -1), src.w + 1) - 0.5;
    y = Math.min(Math.max(y, -1), src.h + 1) - 0.5;
    const fx = Math.floor(x), fy = Math.floor(y);
    const tx = ((x - fx) * 256) | 0, ty = ((y - fy) * 256) | 0;
    const p00 = clamped(src, fx, fy), p10 = clamped(src, fx + 1, fy);
    const p01 = clamped(src, fx, fy + 1), p11 = clamped(src, fx + 1, fy + 1);
    return mix4(p00, p10, p01, p11, tx, ty);
}

function mix4(p00, p10, p01, p11, tx, ty) {
    const mix = (c00, c10, c01, c11) => {
        const top = c00 * (256 - tx) + c10 * tx;
        const bottom = c01 * (256 - tx) + c11 * tx;
        return (top * (256 - ty) + bottom * ty + 32768) >> 16;
    };
    return qRgba(mix(qRed(p00), qRed(p10), qRed(p01), qRed(p11)), mix(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                 mix(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)), mix(qAlpha(p00), qAlpha(p10), qAlpha(p01), qAlpha(p11)));
}

function sampleWrapped(src, x, y) {
    const wrap = (v, n) => {
        if (!Number.isFinite(v))
            return 0;
        v %= n;
        return v < 0 ? v + n : v;
    };
    x = wrap(x, src.w);
    y = wrap(y, src.h);
    const fx = x - 0.5, fy = y - 0.5;
    const x0 = Math.floor(fx), y0 = Math.floor(fy);
    const tx = ((fx - x0) * 256) | 0, ty = ((fy - y0) * 256) | 0;
    const pw = (px, py) => at(src, ((px % src.w) + src.w) % src.w, ((py % src.h) + src.h) % src.h);
    return mix4(pw(x0, y0), pw(x0 + 1, y0), pw(x0, y0 + 1), pw(x0 + 1, y0 + 1), tx, ty);
}

function sampleReflected(src, x, y) {
    // Periodo 2·max sin bucles: algunas distorsiones dan coordenadas enormes.
    const reflect = (v, max) => {
        if (!Number.isFinite(v))
            return 0;
        const period = 2 * max;
        v %= period;
        if (v < 0)
            v += period;
        return v > max ? period - v : v;
    };
    return sampleBilinear(src, reflect(x, src.w), reflect(y, src.h));
}

// Promedio de muestras premultiplicadas (ColorBgra.Blend de Paint.NET).
function average(s, n) {
    if (n <= 0)
        return 0;
    let r = 0, g = 0, b = 0, a = 0;
    for (let i = 0; i < n; ++i) {
        const p = s[i];
        r += p & 255; g += (p >>> 8) & 255; b += (p >>> 16) & 255; a += p >>> 24;
    }
    return qRgba((r / n) | 0, (g / n) | 0, (b / n) | 0, (a / n) | 0);
}

// Desplazamientos de supermuestreo en rejilla girada (Utility.GetRgssOffsets).
function rgssOffsets(quality) {
    const count = quality * quality;
    if (count <= 1)
        return [[0, 0]];
    const pts = [];
    for (let i = 0; i < count; ++i) {
        const y = (i + 1) / (count + 1);
        let x = y * quality;
        x -= Math.floor(x);
        pts.push([x - 0.5, y - 0.5]);
    }
    return pts;
}

// Comportamientos de borde.
const Edge = { Clamp: 0, Wrap: 1, Reflect: 2, Primary: 3, Secondary: 4, Transparent: 5, Original: 6 };
const EdgeNames = ["Sujetar", "Ajuste", "Reflejo", "Color principal", "Color secundario", "Transparente", "Original"];
const pdnEdges = [Edge.Clamp, Edge.Wrap, Edge.Reflect, Edge.Transparent];
const edgeParam = (list, def) => P.choice("edge", "Comportamiento de borde", list.map((e) => EdgeNames[e]), Math.max(0, list.indexOf(def)));
const edgeValue = (v, list) => list[Math.round(v.edge)] ?? Edge.Clamp;
const qualityParam = (def = 2) => P.int("quality", "Calidad", 1, 5, def);

// Muestra bilineal con el comportamiento de borde indicado fuera de la imagen (sx, sy con +0,5).
function sampleEdge(src, sx, sy, edge) {
    const inside = sx >= 0.5 && sy >= 0.5 && sx <= src.w - 0.5 && sy <= src.h - 0.5;
    if (inside)
        return sampleBilinear(src, sx, sy);
    switch (edge) {
    case Edge.Wrap: return sampleWrapped(src, sx, sy);
    case Edge.Reflect: return sampleReflected(src, sx, sy);
    case Edge.Transparent: return 0;
    default: return sampleBilinear(src, sx, sy);
    }
}

// Distorsión al estilo WarpEffectBase de Paint.NET: inverse(x, y) recibe coordenadas relativas
// al centro y devuelve las de origen (también relativas) en un array [x, y].
function warp(a, setup, inverse) {
    const src = a.src, d = a.dst.px, w = src.w;
    const xc = a.roi.left + a.roi.width * (1 + (setup.offset?.x ?? 0)) * 0.5;
    const yc = a.roi.top + a.roi.height * (1 + (setup.offset?.y ?? 0)) * 0.5;
    const aa = rgssOffsets(clamp(setup.quality ?? 2, 1, 5));
    const primary = qPremultiply(a.ctx.primary), secondary = qPremultiply(a.ctx.secondary);
    const edge = setup.edge ?? Edge.Wrap;
    const samples = new Uint32Array(aa.length);
    for (let y = a.y0; y <= a.y1; ++y) {
        const ry = y - yc;
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const rx = x - xc;
            for (let p = 0; p < aa.length; ++p) {
                const t = inverse(rx + aa[p][0], ry - aa[p][1]);
                const sx = t[0] + xc, sy = t[1] + yc;
                let s;
                if (sx >= 0 && sy >= 0 && sx <= src.w - 1 && sy <= src.h - 1) {
                    s = sampleBilinear(src, sx + 0.5, sy + 0.5);
                } else {
                    switch (edge) {
                    case Edge.Clamp: s = sampleBilinear(src, sx + 0.5, sy + 0.5); break;
                    case Edge.Wrap: s = sampleWrapped(src, sx + 0.5, sy + 0.5); break;
                    case Edge.Reflect: s = sampleReflected(src, sx + 0.5, sy + 0.5); break;
                    case Edge.Primary: s = primary; break;
                    case Edge.Secondary: s = secondary; break;
                    case Edge.Transparent: s = 0; break;
                    default: s = at(src, x, y); break;
                    }
                }
                samples[p] = s;
            }
            d[y * w + x] = average(samples, aa.length);
        }
    }
}
const defaultRadius = (roi) => Math.min(roi.width, roi.height) * 0.5;

// --- Desenfoques compartidos --------------------------------------------------------------

// Media de una ventana [lo, hi] alrededor de cada píxel en un eje, contando solo los píxeles
// dentro de la imagen (como hace Paint.NET en los bordes).
function boxPass(inImg, outImg, lo, hi, horizontal) {
    const I = inImg.px, O = outImg.px, W = inImg.w;
    const n = horizontal ? inImg.w : inImg.h, lines = horizontal ? inImg.h : inImg.w;
    const step = horizontal ? 1 : W;
    for (let line = 0; line < lines; ++line) {
        const base = horizontal ? line * W : line;
        let sr = 0, sg = 0, sb = 0, sa = 0, count = 0;
        for (let i = Math.max(0, lo); i <= Math.min(n - 1, hi); ++i) {
            const p = I[base + i * step];
            sr += p & 255; sg += (p >>> 8) & 255; sb += (p >>> 16) & 255; sa += p >>> 24;
            ++count;
        }
        for (let i = 0; i < n; ++i) {
            const c = Math.max(1, count), h = c >> 1;
            O[base + i * step] = qRgba(((sr + h) / c) | 0, ((sg + h) / c) | 0, ((sb + h) / c) | 0, ((sa + h) / c) | 0);
            const leave = i + lo, enter = i + hi + 1;
            if (leave >= 0 && leave < n) {
                const p = I[base + leave * step];
                sr -= p & 255; sg -= (p >>> 8) & 255; sb -= (p >>> 16) & 255; sa -= p >>> 24;
                --count;
            }
            if (enter >= 0 && enter < n) {
                const p = I[base + enter * step];
                sr += p & 255; sg += (p >>> 8) & 255; sb += (p >>> 16) & 255; sa += p >>> 24;
                ++count;
            }
        }
    }
}

const tentMargin = (radius) => radius + 1;

// Desenfoque "gaussiano" de Paint.NET: núcleo triangular de radio r (dos pasadas de caja de
// ancho r + 1 en cada eje). Devuelve 'area'.
function tentBlur(src, area, radius) {
    if (radius <= 0)
        return crop(src, area);
    const big = intersected(adjusted(area, tentMargin(radius)), imageRect(src));
    const a = crop(src, big), b = image(a.w, a.h, src.premul);
    const lo1 = -Math.floor(radius / 2), hi1 = Math.floor((radius + 1) / 2);
    const lo2 = -Math.floor((radius + 1) / 2), hi2 = Math.floor(radius / 2);
    boxPass(a, b, lo1, hi1, true);
    boxPass(b, a, lo2, hi2, true);
    boxPass(a, b, lo1, hi1, false);
    boxPass(b, a, lo2, hi2, false);
    return crop(a, translated(area, -big.left, -big.top));
}

// Media de una caja cuadrada (Desenfoque cuadrado).
function boxBlur(src, area, radius) {
    if (radius <= 0)
        return crop(src, area);
    const big = intersected(adjusted(area, radius + 1), imageRect(src));
    const a = crop(src, big), b = image(a.w, a.h, src.premul);
    boxPass(a, b, -radius, radius, true);
    boxPass(b, a, -radius, radius, false);
    return crop(a, translated(area, -big.left, -big.top));
}

// --- Histograma local circular (LocalHistogramEffect de Paint.NET) ------------------------

// apply(píxel directo, histograma { r, g, b, a, area }) → píxel directo.
function localHistogram(a, radius, apply) {
    const rad = Math.max(0, radius);
    const region = intersected(adjusted(band(a), rad + 1), imageRect(a.src));
    const s = straightCopy(crop(a.src, region));
    const S = s.px, SW = s.w, d = a.dst.px, W = a.src.w;
    const ox = region.left, oy = region.top, width = a.src.w, height = a.src.h;

    // Semiancho horizontal del círculo en cada fila (aprox. (r + 0,5)^2).
    const cutoff = Math.floor(((rad * 2 + 1) * (rad * 2 + 1) + 2) / 4);
    const edge = new Int32Array(rad + 1);
    for (let v = 0; v <= rad; ++v)
        for (let u = 0; u <= rad; ++u)
            if (u * u + v * v <= cutoff)
                edge[v] = u;

    const h = { r: new Int32Array(256), g: new Int32Array(256), b: new Int32Array(256), a: new Int32Array(256), area: 0 };
    const add = (x, yy, sign) => {
        const p = S[(yy - oy) * SW + x - ox];
        h.b[(p >>> 16) & 255] += sign;
        h.g[(p >>> 8) & 255] += sign;
        h.r[p & 255] += sign;
        h.a[p >>> 24] += sign;
        h.area += sign;
    };
    for (let y = a.y0; y <= a.y1; ++y) {
        h.r.fill(0); h.g.fill(0); h.b.fill(0); h.a.fill(0);
        h.area = 0;
        const top = -Math.min(rad, y), bottom = Math.min(rad, height - 1 - y), x0 = a.roi.left;
        for (let v = top; v <= bottom; ++v) {
            const w = edge[Math.abs(v)];
            for (let u = Math.max(-w, -x0); u <= Math.min(w, width - 1 - x0); ++u)
                add(x0 + u, y + v, +1);
        }
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            d[y * W + x] = qPremultiply(apply(S[(y - oy) * SW + x - ox], h));
            if (x === a.roi.right)
                break;
            for (let v = top; v <= bottom; ++v) {
                const w = edge[Math.abs(v)];
                if (x - w >= 0)
                    add(x - w, y + v, -1);
                if (x + 1 + w < width)
                    add(x + 1 + w, y + v, +1);
            }
        }
    }
}

function histogramPercentile(h, percentile) {
    const minCount = Math.trunc((h.area * percentile) / 100);
    const channel = (hist) => {
        let v = 0, count = 0;
        while (v < 255 && hist[v] === 0)
            ++v;
        while (v < 255 && count < minCount) {
            count += hist[v];
            ++v;
        }
        return v;
    };
    return qRgba(channel(h.r), channel(h.g), channel(h.b), channel(h.a));
}

// "Calidad" (1–9) de Mediana y Contorno en Paint.NET 5: bits de precisión de la búsqueda del
// percentil; con menos de 8 cada canal cae en el centro de su intervalo.
function quantizeBits(c, bits) {
    if (bits >= 8 || bits <= 0)
        return c;
    const shift = 8 - bits, levels = 1 << bits;
    const q = (v) => lround((((v >> shift) + 0.5) * 255) / levels);
    return qRgba(q(qRed(c)), q(qGreen(c)), q(qBlue(c)), q(qAlpha(c)));
}

// --- Ruido de Perlin de Paint.NET (PerlinNoise2D) -----------------------------------------

const PERMUTATION = [
    151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142,
    8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203,
    117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165,
    71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41,
    55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89,
    18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250,
    124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189,
    28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
    129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
    242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31,
    181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
    67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
];
const PERM = Int32Array.from({ length: 512 }, (_, i) => PERMUTATION[i & 255]);

const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10);
function grad(hash, x, y) {
    const h = hash & 15;
    const u = h < 8 ? x : y;
    const v = h < 4 ? y : h === 12 || h === 14 ? x : 0;
    return ((h & 1) === 0 ? u : -u) + ((h & 2) === 0 ? v : -v);
}

function noise2(x, y, seed) {
    const xf = Math.floor(x), yf = Math.floor(y);
    const ix = xf & 255, iy = yf & 255;
    x -= xf;
    y -= yf;
    const u = fade(x), v = fade(y);
    const a = PERM[ix + seed] + iy, aa = PERM[a], ab = PERM[a + 1];
    const b = PERM[ix + 1 + seed] + iy, ba = PERM[b], bb = PERM[b + 1];
    const e1 = grad(PERM[aa], x, y) + u * (grad(PERM[ba], x - 1, y) - grad(PERM[aa], x, y));
    const e2 = grad(PERM[ab], x, y - 1) + u * (grad(PERM[bb], x - 1, y - 1) - grad(PERM[ab], x, y - 1));
    return e1 + v * (e2 - e1);
}

const PERLIN_ANGLE = (137.2 / 180) * Math.PI;
const R11 = Math.cos(PERLIN_ANGLE), R12 = -Math.sin(PERLIN_ANGLE), R21 = Math.sin(PERLIN_ANGLE), R22 = Math.cos(PERLIN_ANGLE);

function pdnPerlin(x, y, detail, roughness, seed) {
    let total = 0, frequency = 1, amplitude = 1, partial = detail;
    const octaves = Math.ceil(detail);
    for (let i = 0; i < octaves; ++i) {
        const xr = x * R11 + y * R12, yr = x * R21 + y * R22;
        let n = noise2(xr * frequency, yr * frequency, seed) * amplitude;
        if (partial < 1)
            n *= partial;
        total += n;
        amplitude *= roughness;
        if (amplitude < 0.001)
            break;
        frequency += frequency;
        partial -= 1;
        x = xr + 499;
        y = yr + 506;
    }
    return total;
}

// --- Curvas y niveles ---------------------------------------------------------------------

// Interpolación cúbica monótona (Fritsch–Carlson), sin sobreoscilación.
export function curveLut(points) {
    const pts = [...points].sort((a, b) => a[0] - b[0]).filter((p, i, list) => i === 0 || Math.abs(p[0] - list[i - 1][0]) > 1e-9);
    const out = new Uint8Array(256);
    const n = pts.length;
    if (n === 0) {
        for (let i = 0; i < 256; ++i) out[i] = i;
        return out;
    }
    if (n === 1)
        return out.fill(clamp255f(pts[0][1]));
    const d = new Float64Array(n - 1), m = new Float64Array(n);
    for (let k = 0; k < n - 1; ++k)
        d[k] = (pts[k + 1][1] - pts[k][1]) / (pts[k + 1][0] - pts[k][0]);
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (let k = 1; k < n - 1; ++k)
        m[k] = d[k - 1] * d[k] <= 0 ? 0 : (d[k - 1] + d[k]) / 2;
    for (let k = 0; k < n - 1; ++k) {
        if (d[k] === 0) {
            m[k] = m[k + 1] = 0;
            continue;
        }
        const a = m[k] / d[k], b = m[k + 1] / d[k], s = a * a + b * b;
        if (s > 9) {
            const t = 3 / Math.sqrt(s);
            m[k] = t * a * d[k];
            m[k + 1] = t * b * d[k];
        }
    }
    let k = 0;
    for (let i = 0; i < 256; ++i) {
        let y;
        if (i <= pts[0][0]) {
            y = pts[0][1];
        } else if (i >= pts[n - 1][0]) {
            y = pts[n - 1][1];
        } else {
            while (k < n - 2 && i > pts[k + 1][0])
                ++k;
            const h = pts[k + 1][0] - pts[k][0], t = (i - pts[k][0]) / h, t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * pts[k][1] + (t3 - 2 * t2 + t) * h * m[k] + (-2 * t3 + 3 * t2) * pts[k + 1][1]
                + (t3 - t2) * h * m[k + 1];
        }
        out[i] = clamp255f(y);
    }
    return out;
}

// Niveles (UnaryPixelOps.Level de Paint.NET), canal c: 0 = rojo, 1 = verde, 2 = azul.
export function levelsLut(lv, c) {
    const out = new Uint8Array(256);
    const lo = lv.inLow[c], hi = Math.max(lo + 1, lv.inHigh[c]);
    const olo = lv.outLow[c], ohi = lv.outHigh[c];
    const g = clamp(lv.gamma[c], 0.1, 10);
    for (let i = 0; i < 256; ++i) {
        const v = i - lo;
        if (v < 0)
            out[i] = olo;
        else if (i >= hi)
            out[i] = ohi;
        else
            out[i] = clamp(olo + (ohi - olo) * Math.pow(v / (hi - lo), g), 0, 255) | 0;
    }
    return out;
}

// Niveles automáticos: recorta el 0,5 % de cada extremo y centra la media con la gamma.
export function autoLevels(src, r) {
    const hist = [new Float64Array(256), new Float64Array(256), new Float64Array(256)];
    const sum = [0, 0, 0];
    let total = 0;
    for (let y = r.top; y <= r.bottom; ++y)
        for (let x = r.left; x <= r.right; ++x) {
            const p = at(src, x, y);
            if (qAlpha(p) === 0)
                continue;
            const u = src.premul ? qUnpremultiply(p) : p;
            const c = [qRed(u), qGreen(u), qBlue(u)];
            for (let i = 0; i < 3; ++i) {
                ++hist[i][c[i]];
                sum[i] += c[i];
            }
            ++total;
        }
    const v = defaultLevels();
    if (total === 0)
        return v;
    const percentile = (ch, fraction) => {
        const target = Math.trunc(fraction * total);
        let acc = 0;
        for (let i = 0; i < 256; ++i) {
            acc += hist[ch][i];
            if (acc > target)
                return i;
        }
        return 255;
    };
    for (let c = 0; c < 3; ++c) {
        const lo = percentile(c, 0.005), hi = percentile(c, 0.995), md = sum[c] / total;
        v.inLow[c] = lo;
        v.inHigh[c] = Math.max(lo + 1, hi);
        if (lo < md && md < hi)
            v.gamma[c] = clamp(Math.log(0.5) / Math.log((md - lo) / (hi - lo)), 0.1, 10);
    }
    return v;
}

// Histograma (por canal y de luminosidad) de la zona: lo usa el diálogo de Niveles.
export function histogram(imageData, r = null) {
    const w = imageData.width, h = imageData.height, d = imageData.data;
    const area = r ? { x: r.x, y: r.y, w: r.w, h: r.h } : { x: 0, y: 0, w, h };
    const out = [new Float64Array(256), new Float64Array(256), new Float64Array(256)];
    for (let y = area.y; y < area.y + area.h; ++y)
        for (let x = area.x, i = (y * w + area.x) * 4; x < area.x + area.w; ++x, i += 4)
            if (d[i + 3]) {
                ++out[0][d[i]];
                ++out[1][d[i + 1]];
                ++out[2][d[i + 2]];
            }
    return out;
}

// Histograma de Niveles automático (expuesto para el diálogo).
export function automaticLevels(imageData, r = null) {
    const w = imageData.width, h = imageData.height;
    const img = { w, h, px: new Uint32Array(new Uint8ClampedArray(imageData.data).buffer), premul: false };
    return autoLevels(img, r ? rect(r.x, r.y, r.w, r.h) : imageRect(img));
}

// --- Modos de fusión de los efectos de Representación de Paint.NET 5 ----------------------

const RB = {
    Over: 0, Behind: 1, Overwrite: 2, Multiply: 3, Screen: 4, Darken: 5, Lighten: 6, Dissolve: 7, ColorBurn: 8, LinearBurn: 9,
    DarkerColor: 10, LighterColor: 11, ColorDodge: 12, LinearDodge: 13, Overlay: 14, SoftLight: 15, HardLight: 16,
    VividLight: 17, LinearLight: 18, PinLight: 19, HardMix: 20, Difference: 21, Exclusion: 22, Hue: 23, Saturation: 24,
    Color: 25, Luminosity: 26, Subtract: 27, Division: 28, Additive: 29, Reflect: 30, Glow: 31, Negation: 32, Minimum: 33,
    Maximum: 34, In: 35, DestinationIn: 36, Out: 37, DestinationOut: 38, Atop: 39, DestinationAtop: 40, Plus: 41,
    MaskInvert: 42, Xor8: 43, Xor16: 44, XorComposite: 45,
};
// Tal cual los muestra Paint.NET 5.1 en español (incluido el segundo "Superponer", que es
// Diferencia).
const RenderBlendNames = [
    "Sobre (Normal)", "Por debajo", "Sobrescribir", "Multiplicar", "Pantalla", "Oscurecer", "Aclarar", "Disolver",
    "Sobreexposición de color", "Subexposición lineal", "Color más oscuro", "Color más claro", "Subexposición de color",
    "Sobreexposición lineal", "Superponer", "Luz tenue", "Luz dura", "Luz intensa", "Luz lineal", "Luz focal", "Mezcla dura",
    "Superponer", "Exclusión", "Matiz", "Saturación", "Color", "Luminosidad", "Restar", "División", "Adición", "Reflejo",
    "Brillo", "Negativo", "Mínimo", "Máximo", "En", "Destino en", "Afuera", "Salida de destino", "Encima",
    "Destino en la cima", "Más", "Invertir máscara", "Xor (8 bits)", "Xor (16 bits)", "Xor (compuesto)",
];

const lum3 = (c) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
function clipColor(c) {
    const l = lum3(c), n = Math.min(c[0], c[1], c[2]), x = Math.max(c[0], c[1], c[2]);
    return c.map((v) => {
        if (n < 0)
            v = l + ((v - l) * l) / Math.max(1e-6, l - n);
        if (x > 1)
            v = l + ((v - l) * (1 - l)) / Math.max(1e-6, x - l);
        return v;
    });
}
const setLum = (c, l) => {
    const d = l - lum3(c);
    return clipColor([c[0] + d, c[1] + d, c[2] + d]);
};
const sat3 = (c) => Math.max(c[0], c[1], c[2]) - Math.min(c[0], c[1], c[2]);
function setSat(c, s) {
    const out = [...c];
    const idx = [0, 1, 2].sort((a, b) => out[a] - out[b]);
    const [lo, mid, hi] = idx;
    if (out[hi] > out[lo]) {
        out[mid] = ((out[mid] - out[lo]) * s) / (out[hi] - out[lo]);
        out[hi] = s;
    } else {
        out[mid] = out[hi] = 0;
    }
    out[lo] = 0;
    return out;
}

function separable(mode, b, s) {
    switch (mode) {
    case RB.Multiply: return b * s;
    case RB.Screen: return b + s - b * s;
    case RB.Darken: case RB.Minimum: return Math.min(b, s);
    case RB.Lighten: case RB.Maximum: return Math.max(b, s);
    case RB.ColorBurn: return b >= 1 ? 1 : s <= 0 ? 0 : 1 - Math.min(1, (1 - b) / s);
    case RB.LinearBurn: return Math.max(0, b + s - 1);
    case RB.ColorDodge: return b <= 0 ? 0 : s >= 1 ? 1 : Math.min(1, b / (1 - s));
    case RB.LinearDodge: case RB.Additive: return Math.min(1, b + s);
    case RB.Overlay: return b <= 0.5 ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case RB.SoftLight: {
        const d = b <= 0.25 ? ((16 * b - 12) * b + 4) * b : Math.sqrt(b);
        return s <= 0.5 ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (d - b);
    }
    case RB.HardLight: return s <= 0.5 ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case RB.VividLight:
        return s <= 0.5 ? (s <= 0 ? 0 : 1 - Math.min(1, (1 - b) / (2 * s))) : s >= 1 ? 1 : Math.min(1, b / (2 * (1 - s)));
    case RB.LinearLight: return clamp(b + 2 * s - 1, 0, 1);
    case RB.PinLight: return s <= 0.5 ? Math.min(b, 2 * s) : Math.max(b, 2 * s - 1);
    case RB.HardMix: return b + s >= 1 ? 1 : 0;
    case RB.Difference: return Math.abs(b - s);
    case RB.Exclusion: return b + s - 2 * b * s;
    case RB.Subtract: return Math.max(0, b - s);
    case RB.Division: return s <= 0 ? (b > 0 ? 1 : 0) : Math.min(1, b / s);
    case RB.Reflect: return s >= 1 ? 1 : Math.min(1, (b * b) / (1 - s));
    case RB.Glow: return b >= 1 ? 1 : Math.min(1, (s * s) / (1 - b));
    case RB.Negation: return 1 - Math.abs(1 - b - s);
    default: return s;
    }
}

// Ruido estable por píxel para Disolver.
function hashNoise(x, y) {
    let h = (Math.imul(x, 374761393) + Math.imul(y, 668265263)) >>> 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
    return ((h ^ (h >>> 16)) & 0xffff) / 65535;
}

// Compone 'top' (premultiplicada) sobre 'base' (del mismo tamaño) con el modo elegido.
function renderCompose(base, top, mode) {
    const w = Math.min(base.w, top.w), h = Math.min(base.h, top.h);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const bi = y * base.w + x;
            const bs = base.premul ? qUnpremultiply(base.px[bi]) : base.px[bi];
            const ts = qUnpremultiply(top.px[y * top.w + x]);
            const ab = qAlpha(bs) / 255, as = qAlpha(ts) / 255;
            const cb = [qRed(bs) / 255, qGreen(bs) / 255, qBlue(bs) / 255];
            const cs = [qRed(ts) / 255, qGreen(ts) / 255, qBlue(ts) / 255];
            let ao = as + ab - as * ab;
            let p = [0, 0, 0];
            const composite = (m) => {
                p = [0, 1, 2].map((i) => cs[i] * as * (1 - ab) + cb[i] * ab * (1 - as) + as * ab * m[i]);
            };
            const porterDuff = (fs, fb) => {
                ao = Math.min(1, as * fs + ab * fb);
                p = [0, 1, 2].map((i) => cs[i] * as * fs + cb[i] * ab * fb);
            };
            switch (mode) {
            case RB.Over: porterDuff(1, 1 - as); break;
            case RB.Behind: porterDuff(1 - ab, 1); break;
            case RB.Overwrite: porterDuff(1, 0); break;
            case RB.Dissolve:
                if (hashNoise(x, y) < as) {
                    porterDuff(1 / Math.max(as, 1e-6), 0);
                    ao = 1;
                } else {
                    porterDuff(0, 1);
                }
                break;
            case RB.DarkerColor: composite(lum3(cs) < lum3(cb) ? cs : cb); break;
            case RB.LighterColor: composite(lum3(cs) > lum3(cb) ? cs : cb); break;
            case RB.Hue: composite(setLum(setSat(cs, sat3(cb)), lum3(cb))); break;
            case RB.Saturation: composite(setLum(setSat(cb, sat3(cs)), lum3(cb))); break;
            case RB.Color: composite(setLum(cs, lum3(cb))); break;
            case RB.Luminosity: composite(setLum(cb, lum3(cs))); break;
            case RB.In: porterDuff(ab, 0); break;
            case RB.DestinationIn: porterDuff(0, as); break;
            case RB.Out: porterDuff(1 - ab, 0); break;
            case RB.DestinationOut: porterDuff(0, 1 - as); break;
            case RB.Atop: porterDuff(ab, 1 - as); break;
            case RB.DestinationAtop: porterDuff(1 - ab, as); break;
            case RB.Plus:
                ao = Math.min(1, as + ab);
                p = [0, 1, 2].map((i) => Math.min(1, cs[i] * as + cb[i] * ab));
                break;
            case RB.MaskInvert:
                ao = ab;
                p = [0, 1, 2].map((i) => (ab - cb[i] * ab) * as + cb[i] * ab * (1 - as));
                break;
            case RB.XorComposite:
                porterDuff(1 - ab, 1 - as);
                ao = as * (1 - ab) + ab * (1 - as);
                break;
            case RB.Xor8:
            case RB.Xor16: {
                const bits = (b, s) => (mode === RB.Xor8 ? (lround(b * 255) ^ lround(s * 255)) / 255 : (lround(b * 65535) ^ lround(s * 65535)) / 65535);
                composite([bits(cb[0], cs[0]), bits(cb[1], cs[1]), bits(cb[2], cs[2])]);
                break;
            }
            default:
                composite([separable(mode, cb[0], cs[0]), separable(mode, cb[1], cs[1]), separable(mode, cb[2], cs[2])]);
                break;
            }
            ao = clamp(ao, 0, 1);
            const a8 = lround(ao * 255);
            if (a8 === 0) {
                base.px[bi] = 0;
                continue;
            }
            const ch = (v) => clamp(lround((v / ao) * 255), 0, 255);
            const straight = qRgba(ch(p[0]), ch(p[1]), ch(p[2]), a8);
            base.px[bi] = base.premul ? qPremultiply(straight) : straight;
        }
}

const renderBlendParam = (def) => P.choice("blend", "Modo de fusión", RenderBlendNames, def);

// Compone 'rendered' (la región roi) sobre la capa con el modo elegido.
function blendResult(a, rendered) {
    const base = crop(a.src, band(a));
    renderCompose(base, rendered, Math.round(a.values.blend));
    blit(a.dst, base, a.roi.left, a.y0);
}

// --- Transformaciones (QTransform de Qt) --------------------------------------------------

// Matriz 3×3 con la convención de Qt: los puntos son filas (x, y, 1) · M; translate(), rotate()
// y scale() se aplican antes que lo que ya tenía la matriz.
class Transform {
    constructor(m = [1, 0, 0, 0, 1, 0, 0, 0, 1]) {
        this.m = m;
    }

    static multiply(a, b) {
        const A = a.m, B = b.m, r = new Array(9);
        for (let i = 0; i < 3; ++i)
            for (let j = 0; j < 3; ++j)
                r[i * 3 + j] = A[i * 3] * B[j] + A[i * 3 + 1] * B[3 + j] + A[i * 3 + 2] * B[6 + j];
        return new Transform(r);
    }

    pre(t) {
        this.m = Transform.multiply(t, this).m;
        return this;
    }

    translate(dx, dy) { return this.pre(new Transform([1, 0, 0, 0, 1, 0, dx, dy, 1])); }
    scale(sx, sy) { return this.pre(new Transform([sx, 0, 0, 0, sy, 0, 0, 0, 1])); }

    // axis: "z" (en el plano), "x" o "y" (con perspectiva a 1024 píxeles, como Qt).
    rotate(deg, axis = "z") {
        const a = (deg * Math.PI) / 180, s = Math.sin(a), c = Math.cos(a);
        if (axis === "x")
            return this.pre(new Transform([1, 0, 0, 0, c, -s / 1024, 0, 0, 1]));
        if (axis === "y")
            return this.pre(new Transform([c, 0, -s / 1024, 0, 1, 0, 0, 0, 1]));
        return this.pre(new Transform([c, s, 0, -s, c, 0, 0, 0, 1]));
    }

    map(x, y) {
        const m = this.m, w = m[2] * x + m[5] * y + m[8];
        return [(m[0] * x + m[3] * y + m[6]) / w, (m[1] * x + m[4] * y + m[7]) / w];
    }

    inverted() {
        const [a, b, c, d, e, f, g, h, i] = this.m;
        const A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
        const det = a * A + b * B + c * C;
        if (Math.abs(det) < 1e-12)
            return null;
        const k = 1 / det;
        return new Transform([
            A * k, -(b * i - c * h) * k, (b * f - c * e) * k,
            B * k, (a * i - c * g) * k, -(a * f - c * d) * k,
            C * k, -(a * h - b * g) * k, (a * e - b * d) * k,
        ]);
    }
}

const pixelOrTransparent = (img, x, y) => (x < 0 || y < 0 || x >= img.w || y >= img.h ? 0 : at(img, x, y));
function catmullRom(x) {
    x = Math.abs(x);
    return x < 1 ? 1.5 * x * x * x - 2.5 * x * x + 1 : x < 2 ? -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2 : 0;
}

function sampleBicubicTransparent(src, x, y) {
    x -= 0.5;
    y -= 0.5;
    const ix = Math.floor(x), iy = Math.floor(y), tx = x - ix, ty = y - iy;
    const wx = [0, 1, 2, 3].map((i) => catmullRom(tx - (i - 1))), wy = [0, 1, 2, 3].map((i) => catmullRom(ty - (i - 1)));
    let r = 0, g = 0, b = 0, a = 0;
    for (let j = 0; j < 4; ++j)
        for (let i = 0; i < 4; ++i) {
            const p = pixelOrTransparent(src, ix + i - 1, iy + j - 1), w = wx[i] * wy[j];
            r += qRed(p) * w; g += qGreen(p) * w; b += qBlue(p) * w; a += qAlpha(p) * w;
        }
    const al = clamp255f(a);
    return qRgba(Math.min(al, clamp255f(r)), Math.min(al, clamp255f(g)), Math.min(al, clamp255f(b)), al);
}

function sampleBilinearTransparent(src, x, y) {
    x -= 0.5;
    y -= 0.5;
    const ix = Math.floor(x), iy = Math.floor(y);
    const tx = ((x - ix) * 256) | 0, ty = ((y - iy) * 256) | 0;
    return mix4(pixelOrTransparent(src, ix, iy), pixelOrTransparent(src, ix + 1, iy), pixelOrTransparent(src, ix, iy + 1),
                pixelOrTransparent(src, ix + 1, iy + 1), tx, ty);
}

// --- Ajustes ------------------------------------------------------------------------------

// HSV de Paint.NET en coma flotante (h 0..360, s y v 0..1).
function rgbToHsv(r, g, b) {
    const rf = r / 255, gf = g / 255, bf = b / 255;
    const mx = Math.max(rf, gf, bf), mn = Math.min(rf, gf, bf), delta = mx - mn;
    const v = mx, s = mx === 0 ? 0 : delta / mx;
    if (delta === 0)
        return [0, s, v];
    let h = rf === mx ? (gf - bf) / delta : gf === mx ? 2 + (bf - rf) / delta : 4 + (rf - gf) / delta;
    h *= 60;
    if (h < 0)
        h += 360;
    return [h, s, v];
}

function hsvToRgb(h, s, v, alpha) {
    if (s === 0) {
        const c = clamp255f(v * 255);
        return qRgba(c, c, c, alpha);
    }
    h = (h % 360) / 60;
    const i = Math.floor(h), f = h - i;
    const p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    let r, g, b;
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return qRgba(clamp255f(r * 255), clamp255f(g * 255), clamp255f(b * 255), alpha);
}

// HSL de QColor (h 0..1, -1 si no hay matiz).
function rgbToHsl(r, g, b) {
    const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2;
    if (max === min)
        return [-1, 0, l];
    const d = max - min;
    const s = l < 0.5 ? d / (max + min) : d / (2 - max - min);
    let h = r === max ? (g - b) / d : g === max ? 2 + (b - r) / d : 4 + (r - g) / d;
    h /= 6;
    if (h < 0)
        h += 1;
    return [h, s, l];
}

function hslToRgb(h, s, l) {
    if (s === 0)
        return [l, l, l];
    const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    const f = (t) => {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        return t < 1 / 6 ? p + (q - p) * 6 * t : t < 1 / 2 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p;
    };
    return [f(h + 1 / 3), f(h), f(h - 1 / 3)];
}
// Canal de 8 bits de un QColor creado con valores en coma flotante (16 bits por canal).
const qcolor8 = (v) => Math.round(clamp(v, 0, 1) * 65535) >> 8;

function posterizeLevels(count) {
    // CalcLevels de Paint.NET.
    count = clamp(count, 2, 64);
    const t1 = new Uint8Array(count);
    for (let i = 1; i < count; ++i)
        t1[i] = Math.floor((255 * i) / (count - 1));
    const levels = new Uint8Array(256);
    let j = 0, k = 0;
    for (let i = 0; i < 256; ++i) {
        levels[i] = t1[Math.min(j, count - 1)];
        k += count;
        if (k > 255) {
            k -= 255;
            ++j;
        }
    }
    return levels;
}

const identityLut = () => Uint8Array.from({ length: 256 }, (_, i) => i);

const adjustments = [
    {
        id: "autolevel", name: "Nivel automático", shortcut: "Ctrl+Shift+L", params: [], straight: true,
        prepare: (src, roi) => autoLevels(src, roi),
        run: (a) => applyLuts(a, levelsLut(a.state, 0), levelsLut(a.state, 1), levelsLut(a.state, 2)),
    },
    {
        id: "blackwhite", name: "Blanco y negro", shortcut: "Ctrl+Shift+G", params: [], straight: true,
        run: (a) => mapStraight(a, (c) => {
            const i = intensity(c);
            return qRgba(i, i, i, qAlpha(c));
        }),
    },
    {
        id: "brightness", name: "Brillo y contraste", shortcut: "Ctrl+Shift+T", straight: true,
        params: [P.int("brightness", "Brillo", -100, 100, 0), P.int("contrast", "Contraste", -100, 100, 0)],
        run(a) {
            const bc = new BrightnessContrast(Math.round(a.values.brightness), Math.round(a.values.contrast));
            mapStraight(a, (c) => bc.apply(c));
        },
    },
    {
        id: "curves", name: "Curvas", shortcut: "Ctrl+Shift+M", params: [P.curves("curves")], straight: true,
        run(a) {
            const cv = a.values.curves || defaultCurves();
            if (!cv.mode) {
                const lum = curveLut(cv.points[0]);
                mapStraight(a, (c) => {
                    const i = intensity(c), diff = lum[i] - i;
                    return qRgba(clamp255(qRed(c) + diff), clamp255(qGreen(c) + diff), clamp255(qBlue(c) + diff), qAlpha(c));
                });
                return;
            }
            applyLuts(a, curveLut(cv.points[1]), curveLut(cv.points[2]), curveLut(cv.points[3]));
        },
    },
    {
        id: "exposure", name: "Exposición", params: [P.real("exposure", "", -5, 5, 0, 2)], straight: true,
        run(a) {
            const factor = Math.pow(2, a.values.exposure), lut = new Uint8Array(256);
            for (let i = 0; i < 256; ++i)
                lut[i] = toSrgb(toLinear(i) * factor);
            applyLuts(a, lut, lut, lut);
        },
    },
    {
        id: "highlightsshadows", name: "Iluminaciones y sombras",
        params: [P.int("highlights", "Iluminaciones", -100, 100, 0), P.int("shadows", "Sombras", -100, 100, 0),
                 P.int("clarity", "Claridad", -100, 100, 0), P.real("radius", "Radio", 0.25, 20, 1.25, 2)],
        // Luminosidad media del entorno de cada píxel. El radio va en % del lado mayor de la zona
        // (1,25 por defecto, como Paint.NET).
        prepare: (src, roi, v) => tentBlur(src, roi, Math.max(1, lround((v.radius / 100) * Math.max(roi.width, roi.height)))),
        run(a) {
            const bl = a.state, s = a.src.px, d = a.dst.px, w = a.src.w;
            const shadows = Math.round(a.values.shadows) / 100, highlights = Math.round(a.values.highlights) / 100;
            const clarity = Math.round(a.values.clarity) / 100;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const i = y * w + x;
                    if (qAlpha(s[i]) === 0) {
                        d[i] = 0;
                        continue;
                    }
                    const u = qUnpremultiply(s[i]);
                    const local = intensityF(qUnpremultiply(bl.px[(y - a.roi.top) * bl.w + x - a.roi.left]));
                    const localLin = toLinear(lround(local * 255));
                    const wShadow = (1 - local) ** 2, wHighlight = local ** 2, wMid = 4 * local * (1 - local);
                    const ch = (c) => {
                        let l = toLinear(c);
                        if (shadows > 0)
                            l += (1 - l) * shadows * 1.5 * wShadow * (1 - l);
                        else
                            l *= 1 + shadows * wShadow;
                        if (highlights > 0)
                            l += (1 - l) * highlights * wHighlight;
                        else
                            l *= 1 + highlights * 0.75 * wHighlight;
                        l += clarity * (l - localLin) * wMid;
                        return toSrgb(clamp(l, 0, 1));
                    };
                    d[i] = qPremultiply(qRgba(ch(qRed(u)), ch(qGreen(u)), ch(qBlue(u)), qAlpha(u)));
                }
        },
    },
    {
        id: "huesat", name: "Matiz y saturación", shortcut: "Ctrl+Shift+U", straight: true,
        params: [
            withProps(P.int("hue", "Matiz", -180, 180, 0),
                      { gradient: [[0, 255, 255], [0, 0, 255], [255, 0, 255], [255, 0, 0], [255, 255, 0], [0, 255, 0], [0, 255, 255]] }),
            withProps(P.int("saturation", "Saturación", 0, 200, 100), { gradient: [[128, 128, 128], [255, 40, 40], [255, 0, 0]] }),
            withProps(P.int("lightness", "Claridad", -100, 100, 0), { gradient: [[0, 0, 0], [128, 128, 128], [255, 255, 255]] }),
        ],
        run(a) {
            // HueSaturationLightness de Paint.NET.
            const hueDelta = Math.round(a.values.hue), satFactor = idiv(Math.round(a.values.saturation) * 1024, 100);
            const lightness = Math.round(a.values.lightness), blendAlpha = idiv(Math.abs(lightness) * 255, 100);
            const blendTo = lightness > 0 ? 255 : 0;
            mapStraight(a, (c) => {
                const i = intensity(c);
                const r = clamp255((i * 1024 + (qRed(c) - i) * satFactor) >> 10);
                const g = clamp255((i * 1024 + (qGreen(c) - i) * satFactor) >> 10);
                const b = clamp255((i * 1024 + (qBlue(c) - i) * satFactor) >> 10);
                let [h, s, v] = rgbToHsv(r, g, b);
                h = (h + hueDelta + 720) % 360;
                let out = hsvToRgb(h, s, v, qAlpha(c));
                if (lightness !== 0) {
                    const mix = (x) => x + idiv((blendTo - x) * blendAlpha + 127, 255);
                    out = qRgba(mix(qRed(out)), mix(qGreen(out)), mix(qBlue(out)), qAlpha(c));
                }
                return out;
            });
        },
    },
    {
        // Balance de color como el de GIMP: cada rango (sombras, medios tonos, luces) solo actúa
        // en su zona de luminosidad; con "conservar la luminosidad" se mantiene la del original.
        id: "colorbalance", name: "Balance de color", straight: true,
        params: [
            ...[["shadows", "Sombras"], ["midtones", "Medios tonos"], ["highlights", "Luces"]].flatMap(([prefix, tab]) => [
                withProps(P.int(`${prefix}CyanRed`, "Cian ↔ rojo", -100, 100, 0), { tab, gradient: [[0, 200, 220], [235, 235, 235], [230, 40, 40]] }),
                withProps(P.int(`${prefix}MagentaGreen`, "Magenta ↔ verde", -100, 100, 0), { tab, gradient: [[220, 40, 200], [235, 235, 235], [40, 190, 60]] }),
                withProps(P.int(`${prefix}YellowBlue`, "Amarillo ↔ azul", -100, 100, 0), { tab, gradient: [[240, 210, 30], [235, 235, 235], [40, 80, 230]] }),
            ]),
            P.bool("preserve", "Conservar la luminosidad", true),
        ],
        run(a) {
            const range = (prefix) => ["CyanRed", "MagentaGreen", "YellowBlue"].map((k) => Math.round(a.values[prefix + k] ?? 0) / 100);
            const sh = range("shadows"), md = range("midtones"), hl = range("highlights");
            const preserve = !!a.values.preserve;
            const map = (value, lightness, s, m, h) => {
                const ka = 0.25, kb = 0.333, scale = 0.7;
                s *= clamp((lightness - kb) / -ka + 0.5, 0, 1) * scale;
                m *= clamp((lightness - kb) / ka + 0.5, 0, 1) * clamp((lightness + kb - 1) / -ka + 0.5, 0, 1) * scale;
                h *= clamp((lightness + kb - 1) / ka + 0.5, 0, 1) * scale;
                return clamp(value + s + m + h, 0, 1);
            };
            mapStraight(a, (c) => {
                const r0 = qRed(c) / 255, g0 = qGreen(c) / 255, b0 = qBlue(c) / 255;
                const l = (Math.max(r0, g0, b0) + Math.min(r0, g0, b0)) / 2;
                let rgb = [map(r0, l, sh[0], md[0], hl[0]), map(g0, l, sh[1], md[1], hl[1]), map(b0, l, sh[2], md[2], hl[2])];
                rgb = rgb.map((v) => qcolor8(v) / 255);
                if (preserve) {
                    const [h, s] = rgbToHsl(rgb[0], rgb[1], rgb[2]);
                    rgb = hslToRgb(h < 0 ? 0 : h, s, l);
                }
                return qRgba(qcolor8(rgb[0]), qcolor8(rgb[1]), qcolor8(rgb[2]), qAlpha(c));
            });
        },
    },
    {
        id: "invertalpha", name: "Invertir alfa", shortcut: "Ctrl+Alt+I", params: [], straight: true,
        run: (a) => mapStraight(a, (c) => qRgba(qRed(c), qGreen(c), qBlue(c), 255 - qAlpha(c))),
    },
    {
        id: "invert", name: "Invertir colores", shortcut: "Ctrl+Shift+I", params: [], straight: true,
        run: (a) => mapStraight(a, (c) => qRgba(255 - qRed(c), 255 - qGreen(c), 255 - qBlue(c), qAlpha(c))),
    },
    {
        id: "levels", name: "Niveles", shortcut: "Ctrl+L", params: [P.levels("levels")], straight: true,
        run(a) {
            const lv = a.values.levels || defaultLevels();
            applyLuts(a, levelsLut(lv, 0), levelsLut(lv, 1), levelsLut(lv, 2));
        },
    },
    {
        id: "posterize", name: "Posterización", shortcut: "Ctrl+Shift+P", straight: true,
        params: [
            ...[["red", "Rojo"], ["green", "Verde"], ["blue", "Azul"], ["alpha", "Alfa"]].map(([key, label]) =>
                withProps(linked(P.int(key, label, 2, 64, 16), "linked"), { enableKey: `${key}On` })),
            P.bool("redOn", "Rojo", true), P.bool("greenOn", "Verde", true), P.bool("blueOn", "Azul", true), P.bool("alphaOn", "Alfa", true),
            P.bool("linked", "Vinculado", true),
        ],
        run(a) {
            // Cada canal solo si su casilla está marcada (también el alfa, como Paint.NET 5).
            const lut = (key) => (a.values[`${key}On`] ?? true ? posterizeLevels(Math.round(a.values[key])) : identityLut());
            const r = lut("red"), g = lut("green"), b = lut("blue"), al = lut("alpha");
            mapStraight(a, (c) => qRgba(r[qRed(c)], g[qGreen(c)], b[qBlue(c)], al[qAlpha(c)]));
        },
    },
    {
        id: "sepia", name: "Sepia", shortcut: "Ctrl+Shift+E", params: [P.int("intensity", "Intensidad", 0, 100, 50)], straight: true,
        run(a) {
            // Desaturar + Niveles con gamma (R 0,8 / G 1,0 / B 1,2), como Paint.NET.
            const k = Math.round(a.values.intensity) / 50;
            const lv = defaultLevels();
            lv.gamma = [Math.pow(0.8, Math.max(1, k)), 1, Math.pow(1.2, Math.max(1, k))];
            const lr = levelsLut(lv, 0), lg = levelsLut(lv, 1), lb = levelsLut(lv, 2);
            mapStraight(a, (c) => {
                const i = intensity(c), sepia = qRgba(lr[i], lg[i], lb[i], qAlpha(c));
                return k >= 1 ? sepia : lerpColor(c, sepia, k);
            });
        },
    },
    {
        id: "temperature", name: "Temperatura y tinte", straight: true,
        params: [
            withProps(P.int("temperature", "Temperatura", -100, 100, 0), { gradient: [[60, 130, 255], [235, 235, 235], [255, 140, 30]] }),
            withProps(P.int("tint", "Tinte", -100, 100, 0), { gradient: [[240, 110, 220], [235, 235, 235], [100, 210, 100]] }),
        ],
        run(a) {
            const t = Math.round(a.values.temperature) / 100, tint = Math.round(a.values.tint) / 100;
            const gr = 1 + 0.35 * t + 0.1 * tint, gg = 1 - 0.25 * tint, gb = 1 - 0.35 * t + 0.1 * tint;
            const lr = new Uint8Array(256), lg = new Uint8Array(256), lb = new Uint8Array(256);
            for (let i = 0; i < 256; ++i) {
                const l = toLinear(i);
                lr[i] = toSrgb(l * gr);
                lg[i] = toSrgb(l * gg);
                lb[i] = toSrgb(l * gb);
            }
            applyLuts(a, lr, lg, lb);
        },
    },
];

// --- Artístico ----------------------------------------------------------------------------

// Resplandor aplicado sobre dst: desenfoque + brillo/contraste + trama.
function glowInto(a, radius, brightness, contrast) {
    const bl = tentBlur(a.src, band(a), radius);
    const bc = new BrightnessContrast(brightness, contrast);
    const s = a.src.px, d = a.dst.px, w = a.src.w;
    for (let y = a.y0; y <= a.y1; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const top = bc.apply(qUnpremultiply(bl.px[(y - a.y0) * bl.w + x - a.roi.left]));
            d[y * w + x] = qPremultiply(blendStraight(qUnpremultiply(s[y * w + x]), top, opScreen));
        }
}

const INK_KERNEL = [[-1, -1, -1, -1, -1], [-1, -1, -1, -1, -1], [-1, -1, 30, -1, -1], [-1, -1, -1, -1, -1], [-1, -1, -5, -1, -1]];

const artistic = [
    {
        id: "inksketch", name: "Boceto a tinta", category: "artistic",
        params: [P.int("ink", "Contorno en tinta", 0, 99, 50), P.int("coloring", "Color", 0, 100, 50)],
        run(a) {
            const ink = Math.round(a.values.ink), coloring = Math.round(a.values.coloring);
            glowInto(a, 6, -(coloring - 50) * 2, -(coloring - 50) * 2);
            const s = straightCopy(a.src), d = a.dst.px, w = s.w, h = s.h;
            const threshold = idiv(ink * 255, 100);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    let r = 0, g = 0, b = 0;
                    for (let v = Math.max(0, y - 2); v < Math.min(h, y + 3); ++v)
                        for (let u = Math.max(0, x - 2); u < Math.min(w, x + 3); ++u) {
                            const k = INK_KERNEL[v - y + 2][u - x + 2], p = s.px[v * w + u];
                            r += qRed(p) * k; g += qGreen(p) * k; b += qBlue(p) * k;
                        }
                    const i = intensity(qRgb(clamp255(r), clamp255(g), clamp255(b)));
                    const top = i > threshold ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
                    d[y * w + x] = qPremultiply(blendStraight(top, qUnpremultiply(d[y * w + x]), opDarken));
                }
        },
    },
    {
        id: "oilpainting", name: "Pintura al óleo", category: "artistic",
        params: [P.int("brush", "Tamaño del pincel", 1, 40, 10), P.real("granularity", "Granularidad", 0, 1, 0.2, 2),
                 P.choice("kernel", "Forma del Kernel", ["Círculo", "Cuadrado"], 0)],
        run(a) {
            // Paint.NET 5: tamaño del pincel (diámetro), granularidad (0..1) y forma del núcleo.
            const brush = Math.max(1, lround(Math.round(a.values.brush) / 2));
            const levels = clamp(lround(3 + a.values.granularity * 235), 3, 255);
            const circle = Math.round(a.values.kernel) === 0;
            const s = straightCopy(a.src), S = s.px, d = a.dst.px, w = s.w, h = s.h;
            const count = new Int32Array(levels + 1), sr = new Float64Array(levels + 1), sg = new Float64Array(levels + 1);
            const sb = new Float64Array(levels + 1), sa = new Float64Array(levels + 1);
            for (let y = a.y0; y <= a.y1; ++y) {
                const top = Math.max(0, y - brush), bottom = Math.min(h, y + brush + 1);
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    count.fill(0); sr.fill(0); sg.fill(0); sb.fill(0); sa.fill(0);
                    const left = Math.max(0, x - brush), right = Math.min(w, x + brush + 1);
                    for (let j = top; j < bottom; ++j)
                        for (let i = left; i < right; ++i) {
                            if (circle && (i - x) * (i - x) + (j - y) * (j - y) > brush * brush + brush)
                                continue;
                            const p = S[j * w + i], k = Math.floor((intensity(p) * levels) / 255);
                            ++count[k];
                            sr[k] += qRed(p); sg[k] += qGreen(p); sb[k] += qBlue(p); sa[k] += qAlpha(p);
                        }
                    let best = 0;
                    for (let k = 0; k < count.length; ++k)
                        if (count[k] > count[best])
                            best = k;
                    const n = Math.max(1, count[best]);
                    d[y * w + x] = qPremultiply(qRgba(Math.floor(sr[best] / n), Math.floor(sg[best] / n), Math.floor(sb[best] / n), Math.floor(sa[best] / n)));
                }
            }
        },
    },
    {
        id: "pencilsketch", name: "Boceto a lápiz", category: "artistic",
        params: [P.real("tip", "Espesor del lápiz", 1, 20, 2, 2), P.real("range", "Intervalo", -20, 20, 0, 1)],
        run(a) {
            const tip = Math.max(1, lround(a.values.tip)), range = lround(a.values.range);
            const bl = tentBlur(a.src, band(a), tip);
            const bc = new BrightnessContrast(range, -range);
            const s = a.src.px, d = a.dst.px, w = a.src.w;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    let t = bc.apply(qUnpremultiply(bl.px[(y - a.y0) * bl.w + x - a.roi.left]));
                    t = qRgba(255 - qRed(t), 255 - qGreen(t), 255 - qBlue(t), qAlpha(t));
                    const ti = intensity(t);
                    t = qRgba(ti, ti, ti, qAlpha(t));
                    const su = qUnpremultiply(s[y * w + x]), si = intensity(su);
                    d[y * w + x] = qPremultiply(blendStraight(qRgba(si, si, si, qAlpha(su)), t, opColorDodge));
                }
        },
    },
];

// --- Desenfoques --------------------------------------------------------------------------

// Realce de gamma: los colores se elevan a 'gamma' antes de promediar y se devuelven después,
// así las luces dominan (aspecto "high key").
class GammaTables {
    constructor(g) {
        this.gamma = g;
        this.forward = new Float64Array(256).map((_, i) => Math.fround(Math.pow(i / 255, g)));
    }

    back(v) {
        return clamp255f(Math.pow(Math.max(0, v), 1 / this.gamma) * 255);
    }
}

// Desenfoque con varias pasadas de caja (Calidad): 2 = el triangular clásico de Paint.NET; más
// pasadas se acercan a la campana de Gauss con la misma varianza.
function multiBox(src, area, radius, passes) {
    const r = Math.max(0, lround(radius));
    if (passes === 2 || r === 0)
        return tentBlur(src, area, r);
    const variance = (2 * ((r + 1) * (r + 1) - 1)) / 12;
    const box = Math.max(1, lround((Math.sqrt((12 * variance) / passes + 1) - 1) / 2));
    const margin = passes * box + 1;
    const big = intersected(adjusted(area, margin), imageRect(src));
    let work = crop(src, big);
    for (let i = 0; i < passes; ++i)
        work = boxBlur(work, imageRect(work), box);
    return crop(work, translated(area, -big.left, -big.top));
}

// Aplica 'blur' con realce de gamma ("Potenciar Gamma" de Paint.NET 5: 0 = sin realce).
function blurWithGamma(a, boost, margin, blur) {
    if (boost <= 0.0001) {
        blit(a.dst, blur(a.src, band(a)), a.roi.left, a.y0);
        return;
    }
    const gt = new GammaTables(1 + boost);
    const area = band(a), big = intersected(adjusted(area, margin), imageRect(a.src));
    const boosted = crop(a.src, big);
    const f = (c) => clamp255f(gt.forward[c] * 255);
    for (let i = 0; i < boosted.px.length; ++i) {
        const u = qUnpremultiply(boosted.px[i]);
        boosted.px[i] = qPremultiply(qRgba(f(qRed(u)), f(qGreen(u)), f(qBlue(u)), qAlpha(u)));
    }
    const bl = blur(boosted, translated(area, -big.left, -big.top));
    const d = a.dst.px, w = a.dst.w, back = (c) => gt.back(c / 255);
    for (let y = a.y0; y <= a.y1; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const u = qUnpremultiply(bl.px[(y - a.y0) * bl.w + x - a.roi.left]);
            d[y * w + x] = qPremultiply(qRgba(back(qRed(u)), back(qGreen(u)), back(qBlue(u)), qAlpha(u)));
        }
}

function renderBokeh(a) {
    // Media de un disco (como Desenfocar/Unfocus de Paint.NET) con realce de gamma; con más
    // Calidad el borde del disco se suaviza.
    const r = Math.max(1, lround(a.values.radius));
    const gt = new GammaTables(1 + a.values.gamma);
    const edgeSoft = (clamp(Math.round(a.values.quality), 1, 8) - 1) / 7;
    const left = Math.max(0, a.roi.left - r), right = Math.min(a.src.w - 1, a.roi.right + r);
    const W = right - left + 1;
    const half = new Int32Array(r + 1);
    for (let v = 0; v <= r; ++v)
        half[v] = Math.floor(Math.sqrt(r * r - v * v + r));
    const S = a.src.px, d = a.dst.px, iw = a.src.w;
    // Por franjas para acotar la memoria de las sumas de prefijo por fila de (alfa,
    // alfa·color^gamma): el disco cuesta O(r) por píxel.
    const BAND = 256;
    for (let first = a.y0; first <= a.y1; first += BAND) {
        const last = Math.min(a.y1, first + BAND - 1);
        const top = Math.max(0, first - r), bottom = Math.min(a.src.h - 1, last + r), H = bottom - top + 1;
        const prefix = new Float32Array((W + 1) * H * 4);
        for (let y = 0; y < H; ++y) {
            let a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            const base = y * (W + 1) * 4, row = (top + y) * iw;
            for (let x = 0; x < W; ++x) {
                const u = qUnpremultiply(S[row + left + x]), al = qAlpha(u) / 255;
                a0 = Math.fround(a0 + al);
                a1 = Math.fround(a1 + al * gt.forward[qRed(u)]);
                a2 = Math.fround(a2 + al * gt.forward[qGreen(u)]);
                a3 = Math.fround(a3 + al * gt.forward[qBlue(u)]);
                const o = base + (x + 1) * 4;
                prefix[o] = a0; prefix[o + 1] = a1; prefix[o + 2] = a2; prefix[o + 3] = a3;
            }
        }
        const sum = new Float64Array(4);
        for (let y = first; y <= last; ++y)
            for (let x = a.roi.left; x <= a.roi.right; ++x) {
                sum.fill(0);
                let count = 0, countF = 0;
                for (let v = -r; v <= r; ++v) {
                    const yy = y + v - top;
                    if (yy < 0 || yy >= H)
                        continue;
                    const hw = half[Math.abs(v)];
                    const x0 = Math.max(0, x - hw - left), x1 = Math.min(W - 1, x + hw - left);
                    if (x1 < x0)
                        continue;
                    const base = yy * (W + 1) * 4;
                    for (let c = 0; c < 4; ++c)
                        sum[c] += prefix[base + (x1 + 1) * 4 + c] - prefix[base + x0 * 4 + c];
                    count += x1 - x0 + 1;
                    if (edgeSoft > 0) {
                        const frac = (Math.sqrt(Math.max(0, r * r - v * v)) - hw + 1) * edgeSoft * 0.5;
                        for (const xe of [x - hw - 1 - left, x + hw + 1 - left]) {
                            if (xe < 0 || xe >= W || frac <= 0)
                                continue;
                            for (let c = 0; c < 4; ++c)
                                sum[c] += frac * (prefix[base + (xe + 1) * 4 + c] - prefix[base + xe * 4 + c]);
                            countF += frac;
                        }
                    }
                }
                if (sum[0] <= 1e-6 || count === 0) {
                    d[y * iw + x] = 0;
                    continue;
                }
                const alpha = clamp255f((sum[0] / (count + countF)) * 255);
                d[y * iw + x] = qPremultiply(qRgba(gt.back(sum[1] / sum[0]), gt.back(sum[2] / sum[0]), gt.back(sum[3] / sum[0]), alpha));
            }
    }
}

function renderZoomBlur(a) {
    // Desenfoque de zum de Paint.NET 5: cada píxel se promedia a lo largo del radio hacia el
    // centro.
    const distance = a.values.distance / 10, focus = Math.max(0.1, a.values.focus);
    const quality = clamp(a.values.quality, 1, 5), off = a.values.offset || { x: 0, y: 0 };
    const cx = a.roi.left + (a.roi.width * (1 + off.x)) / 2, cy = a.roi.top + (a.roi.height * (1 + off.y)) / 2;
    const maxR = Math.max(1, Math.hypot(a.roi.width, a.roi.height) / 2);
    const src = a.src, d = a.dst.px, w = src.w;
    for (let y = a.y0; y <= a.y1; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const dx = x - cx, dy = y - cy, r = Math.hypot(dx, dy);
            const shrink = Math.min(0.95, distance * Math.pow(Math.min(1, r / maxR), focus - 1));
            const n = clamp(Math.trunc((r * shrink * quality) / 2), 1, 256);
            let r0 = 0, g0 = 0, b0 = 0, a0 = 0;
            for (let i = 0; i < n; ++i) {
                const t = 1 - shrink * (n === 1 ? 0 : i / (n - 1));
                const s = sampleBilinear(src, cx + dx * t + 0.5, cy + dy * t + 0.5);
                r0 += qRed(s); g0 += qGreen(s); b0 += qBlue(s); a0 += qAlpha(s);
            }
            d[y * w + x] = qRgba(Math.trunc(r0 / n), Math.trunc(g0 / n), Math.trunc(b0 / n), Math.trunc(a0 / n));
        }
}

const blurs = [
    {
        id: "bokeh", name: "Bokeh", category: "blurs",
        params: [P.real("radius", "Radio", 1, 200, 25, 1), P.real("gamma", "Potenciar Gamma", 0, 7, 0, 2), P.int("quality", "Calidad", 1, 8, 3)],
        run: renderBokeh,
    },
    {
        id: "fragment", name: "Fragmento", category: "blurs",
        params: [P.int("fragments", "Cantidad de fragmentos", 2, 50, 4), P.int("distance", "Distancia", 0, 100, 8),
                 P.angle("rotation", "Rotación", 0, 360, 0)],
        run(a) {
            const fragments = Math.round(a.values.fragments), distance = Math.round(a.values.distance);
            const step = (2 * Math.PI) / fragments, rot = ((a.values.rotation - 90) * Math.PI) / 180;
            const offsets = [];
            for (let i = 0; i < fragments; ++i) {
                const ang = rot + step * i;
                offsets.push([lround(distance * -Math.sin(ang)), lround(distance * -Math.cos(ang))]);
            }
            const src = a.src, d = a.dst.px, w = src.w, samples = new Uint32Array(offsets.length);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    let n = 0;
                    for (const [ox, oy] of offsets) {
                        const u = x - ox, v = y - oy;
                        if (u >= 0 && u < src.w && v >= 0 && v < src.h)
                            samples[n++] = at(src, u, v);
                    }
                    d[y * w + x] = average(samples, n);
                }
        },
    },
    {
        id: "gaussianblur", name: "Desenfoque gaussiano", category: "blurs",
        params: [P.real("radius", "Radio", 0, 200, 2, 1), P.real("gamma", "Potenciar Gamma", 0, 7, 0, 2), P.int("quality", "Calidad", 1, 4, 3)],
        run(a) {
            const radius = a.values.radius, passes = clamp(Math.round(a.values.quality), 1, 4);
            blurWithGamma(a, a.values.gamma, tentMargin(Math.ceil(radius)) + 4, (img, area) => multiBox(img, area, radius, passes));
        },
    },
    {
        id: "median", name: "Desenfoque por mediana", category: "blurs",
        params: [P.int("radius", "Radio", 1, 200, 10), P.int("percentile", "Percentil", 0, 100, 50), P.int("quality", "Calidad", 1, 9, 8)],
        run(a) {
            const percentile = Math.round(a.values.percentile), bits = Math.round(a.values.quality);
            localHistogram(a, Math.round(a.values.radius), (c, h) => quantizeBits(histogramPercentile(h, percentile), bits));
        },
    },
    {
        id: "motionblur", name: "Desenfoque de movimiento", category: "blurs",
        params: [P.angle("angle", "Ángulo", -180, 180, 25), P.real("distance", "Distancia", 1, 200, 10, 2),
                 P.bool("centered", "Centrado", true), edgeParam(pdnEdges, Edge.Clamp)],
        run(a) {
            const distance = a.values.distance, edge = edgeValue(a.values, pdnEdges);
            const theta = ((a.values.angle + 180) * 2 * Math.PI) / 360;
            let start = [0, 0], end = [distance * Math.cos(theta), -distance * Math.sin(theta)];
            if (a.values.centered) {
                start = [-end[0] / 2, -end[1] / 2];
                end = [end[0] / 2, end[1] / 2];
            }
            const count = Math.max(1, Math.trunc(((1 + distance) * 3) / 2));
            const points = [];
            for (let i = 0; i < count; ++i) {
                const t = count === 1 ? 0 : i / (count - 1);
                points.push(count === 1 ? [0, 0] : [start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t]);
            }
            const src = a.src, d = a.dst.px, w = src.w, samples = new Uint32Array(points.length);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    for (let i = 0; i < points.length; ++i)
                        samples[i] = sampleEdge(src, points[i][0] + x + 0.5, points[i][1] + y + 0.5, edge);
                    d[y * w + x] = average(samples, points.length);
                }
        },
    },
    {
        id: "radialblur", name: "Desenfoque radial", category: "blurs",
        params: [P.angle("angle", "Ángulo", 0, 360, 4), P.offset("offset", "Centrar"), P.real("quality", "Calidad", 1, 5, 2, 1)],
        run(a) {
            const angle = (a.values.angle * Math.PI) / 180, off = a.values.offset || { x: 0, y: 0 };
            const q = clamp(a.values.quality, 1, 5);
            const cx = a.roi.left + (a.roi.width * (1 + off.x)) / 2, cy = a.roi.top + (a.roi.height * (1 + off.y)) / 2;
            const n = lround(q * q * (30 + q * q)), step = angle / n, cs = Math.cos(step), sn = Math.sin(step);
            const src = a.src, S = src.px, d = a.dst.px, w = src.w, h = src.h;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const c0 = S[y * w + x];
                    let sr = qRed(c0), sg = qGreen(c0), sb = qBlue(c0), sa = qAlpha(c0), sc = 1;
                    let x1 = x - cx, y1 = y - cy, x2 = x1, y2 = y1;
                    for (let i = 0; i < n; ++i) {
                        const nx1 = x1 * cs - y1 * sn, ny1 = x1 * sn + y1 * cs;
                        const nx2 = x2 * cs + y2 * sn, ny2 = -x2 * sn + y2 * cs;
                        x1 = nx1; y1 = ny1; x2 = nx2; y2 = ny2;
                        for (let k = 0; k < 2; ++k) {
                            const u = lround((k ? x2 : x1) + cx), v = lround((k ? y2 : y1) + cy);
                            if (u > 0 && v > 0 && u < w && v < h) {
                                const s = S[v * w + u];
                                sr += qRed(s); sg += qGreen(s); sb += qBlue(s); sa += qAlpha(s);
                                ++sc;
                            }
                        }
                    }
                    d[y * w + x] = qRgba(Math.trunc(sr / sc), Math.trunc(sg / sc), Math.trunc(sb / sc), Math.trunc(sa / sc));
                }
        },
    },
    {
        id: "sketchblur", name: "Boceto borroso", category: "blurs",
        params: [P.real("radius", "Radio", 1, 100, 25, 2), P.int("percentile", "Percentil", 0, 100, 50), P.int("smoothness", "Suavizado", 1, 10, 3)],
        run(a) {
            // Aproximación del desenfoque de bosquejo de Paint.NET 5: percentil de muestras
            // aleatorias dentro del disco, como pinceladas gruesas.
            const radius = Math.max(1, lround(a.values.radius)), percentile = Math.round(a.values.percentile);
            const samples = 8 + Math.round(a.values.smoothness) * 8, seed = 0;
            const src = a.src, d = a.dst.px, w = src.w;
            const ch = [new Int32Array(samples), new Int32Array(samples), new Int32Array(samples), new Int32Array(samples)];
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    for (let i = 0; i < samples; ++i) {
                        const ang = Math.fround(hashUnit(x, y * samples + i, seed) * Math.fround(2 * Math.PI));
                        const dist = Math.fround(Math.sqrt(hashUnit(x * 7 + i, y, seed + 99)) * radius);
                        const u = clamp(Math.trunc(x + Math.cos(ang) * dist), 0, src.w - 1);
                        const v = clamp(Math.trunc(y + Math.sin(ang) * dist), 0, src.h - 1);
                        const s = qUnpremultiply(at(src, u, v));
                        ch[0][i] = qRed(s); ch[1][i] = qGreen(s); ch[2][i] = qBlue(s); ch[3][i] = qAlpha(s);
                    }
                    const k = clamp(Math.trunc((percentile * (samples - 1)) / 100), 0, samples - 1);
                    const value = ch.map((c) => c.slice().sort()[k]);
                    d[y * w + x] = qPremultiply(qRgba(value[0], value[1], value[2], value[3]));
                }
        },
    },
    {
        id: "squareblur", name: "Desenfoque cuadrado", category: "blurs",
        params: [P.real("radius", "Radio", 1, 200, 6, 1), P.real("gamma", "Potenciar Gamma", 0, 7, 0, 2)],
        run(a) {
            const radius = Math.max(0, lround(a.values.radius));
            blurWithGamma(a, a.values.gamma, radius + 2, (img, area) => boxBlur(img, area, radius));
        },
    },
    {
        id: "surfaceblur", name: "Desenfoque de superficie", category: "blurs",
        params: [P.int("radius", "Radio", 1, 100, 6), P.int("threshold", "Umbral", 1, 100, 15)],
        run(a) {
            // Desenfoque de superficie de Paint.NET: media ponderada por parecido de color.
            const threshold = Math.max(1, Math.round(a.values.threshold));
            const factor = new Int32Array(256), slope = 96 / threshold;
            for (let i = 0; i < 256; ++i)
                factor[i] = Math.max(0, lround(255 - i * slope));
            const blurChannel = (current, hist) => {
                let sum = 0, divisor = 0;
                for (let bin = 0; bin < 256; ++bin) {
                    if (hist[bin] === 0)
                        continue;
                    const wt = factor[Math.abs(bin - current)];
                    if (wt > 0) {
                        const t = hist[bin] * wt;
                        sum += t * bin;
                        divisor += t;
                    }
                }
                return divisor > 0 ? Math.floor((sum + (divisor >> 1)) / divisor) : current;
            };
            localHistogram(a, Math.round(a.values.radius), (c, h) =>
                qRgba(blurChannel(qRed(c), h.r), blurChannel(qGreen(c), h.g), blurChannel(qBlue(c), h.b), qAlpha(c)));
        },
    },
    {
        id: "zoomblur", name: "Desenfoque de zum", category: "blurs",
        params: [P.real("distance", "Distancia", 0, 9.5, 1.25, 2), P.real("focus", "Enfoque", 0.1, 10, 2, 2), P.offset("offset", "Centrar"),
                 P.real("quality", "Calidad", 1, 5, 2, 1)],
        run: renderZoomBlur,
    },
];

// --- Color: Cuantizar ---------------------------------------------------------------------

function collectColors(src, roi) {
    // Con imágenes enormes basta con una muestra uniforme de ~1 M de píxeles.
    const total = roi.width * roi.height;
    const step = Math.max(1, Math.trunc(Math.sqrt(total / 1e6)));
    const out = [];
    for (let y = roi.top; y <= roi.bottom; y += step)
        for (let x = roi.left; x <= roi.right; x += step) {
            const p = at(src, x, y);
            if (qAlpha(p) < 16)
                continue;
            const u = qUnpremultiply(p);
            out.push((qRed(u) << 16) | (qGreen(u) << 8) | qBlue(u));
        }
    return Int32Array.from(out);
}

const channelOf = (c, ch) => (c >> (16 - 8 * ch)) & 255;

// nth_element: deja en 'k' el elemento que iría ahí ordenado, menores antes y mayores después.
function nthElement(arr, lo, hi, k, key) {
    while (hi - lo > 1) {
        const pivot = key(arr[(lo + hi) >> 1]);
        let i = lo, j = hi - 1;
        while (i <= j) {
            while (key(arr[i]) < pivot) ++i;
            while (key(arr[j]) > pivot) --j;
            if (i <= j) {
                const t = arr[i]; arr[i] = arr[j]; arr[j] = t;
                ++i; --j;
            }
        }
        if (k <= j)
            hi = j + 1;
        else if (k >= i)
            lo = i;
        else
            return;
    }
}

function medianCut(colors, count) {
    const measure = (b) => {
        const lo = [255, 255, 255], hi = [0, 0, 0];
        for (let i = b.begin; i < b.end; ++i)
            for (let k = 0; k < 3; ++k) {
                const v = channelOf(colors[i], k);
                if (v < lo[k]) lo[k] = v;
                if (v > hi[k]) hi[k] = v;
            }
        b.channel = 0;
        for (let k = 1; k < 3; ++k)
            if (hi[k] - lo[k] > hi[b.channel] - lo[b.channel])
                b.channel = k;
        b.range = b.end - b.begin < 2 ? 0 : hi[b.channel] - lo[b.channel];
        return b;
    };
    const boxes = [measure({ begin: 0, end: colors.length })];
    while (boxes.length < count) {
        let best = 0;
        for (let i = 1; i < boxes.length; ++i)
            if (boxes[i].range > boxes[best].range)
                best = i;
        if (boxes[best].range === 0)
            break;
        const b = boxes[best], mid = b.begin + Math.floor((b.end - b.begin) / 2), ch = b.channel;
        nthElement(colors, b.begin, b.end, mid, (c) => channelOf(c, ch));
        boxes[best] = measure({ begin: b.begin, end: mid });
        boxes.push(measure({ begin: mid, end: b.end }));
    }
    const palette = [];
    for (const b of boxes) {
        if (b.end === b.begin)
            continue;
        let r = 0, g = 0, bl = 0;
        for (let i = b.begin; i < b.end; ++i) {
            r += channelOf(colors[i], 0); g += channelOf(colors[i], 1); bl += channelOf(colors[i], 2);
        }
        const n = b.end - b.begin;
        palette.push([Math.floor(r / n), Math.floor(g / n), Math.floor(bl / n)]);
    }
    return palette;
}

// Cuantificador por octree: los colores se agrupan en un árbol de 8 niveles y se fusionan las
// hojas menos usadas hasta quedar con 'count' colores.
function octreePalette(colors, count) {
    const reducible = [[], [], [], [], [], [], [], []], sorted = new Array(8).fill(false);
    let leaves = 0;
    const node = (level) => {
        const n = { r: 0, g: 0, b: 0, count: 0, leaf: level === 8, child: null };
        if (n.leaf)
            ++leaves;
        else {
            n.child = new Array(8).fill(null);
            reducible[level].push(n);
        }
        return n;
    };
    const root = node(0);
    for (let i = 0; i < colors.length; ++i) {
        const r = channelOf(colors[i], 0), g = channelOf(colors[i], 1), b = channelOf(colors[i], 2);
        let n = root;
        for (let level = 0; ; ++level) {
            if (n.leaf) {
                n.r += r; n.g += g; n.b += b; ++n.count;
                break;
            }
            ++n.count;
            const shift = 7 - level;
            const idx = (((r >> shift) & 1) << 2) | (((g >> shift) & 1) << 1) | ((b >> shift) & 1);
            if (!n.child[idx])
                n.child[idx] = node(level + 1);
            n = n.child[idx];
        }
    }
    const reduce = (n) => {
        let r = 0, g = 0, b = 0, c = 0, removed = 0;
        for (let i = 0; i < 8; ++i) {
            const ch = n.child[i];
            if (!ch)
                continue;
            if (!ch.leaf)
                reduce(ch);
            r += ch.r; g += ch.g; b += ch.b; c += ch.count;
            ++removed;
            n.child[i] = null;
        }
        Object.assign(n, { r, g, b, count: c, leaf: true });
        leaves -= removed - 1;
    };
    while (leaves > count) {
        let level = 7;
        while (level >= 0 && reducible[level].length === 0)
            --level;
        if (level < 0)
            break;
        const list = reducible[level];
        if (!sorted[level]) {
            list.sort((x, y) => y.count - x.count);
            sorted[level] = true;
        }
        const n = list.pop();
        if (!n.leaf)
            reduce(n);
    }
    const out = [];
    const collect = (n) => {
        if (!n)
            return;
        if (n.leaf) {
            const c = Math.max(1, n.count);
            out.push([Math.floor(n.r / c), Math.floor(n.g / c), Math.floor(n.b / c)]);
            return;
        }
        n.child.forEach(collect);
    };
    collect(root);
    return out;
}

const color = [
    {
        id: "quantize", name: "Cuantizar", category: "color",
        params: [P.choice("algorithm", "Algoritmo", ["Corte a la mediana", "Octree"], 1), P.int("colors", "Colores", 2, 256, 256),
                 P.int("dither", "Nivel de tramado", 0, 8, 7), P.int("threshold", "Umbral de transparencia", 0, 255, 128)],
        note: "Los píxeles que tengan un valor del canal alfa menor que el umbral serán totalmente transparentes.",
        // El tramado arrastra el error de una fila a la siguiente: no se reparte en franjas.
        serial: true,
        prepare(src, roi, v) {
            const count = clamp(Math.round(v.colors), 2, 256);
            const colors = collectColors(src, roi);
            if (!colors.length)
                return [[0, 0, 0]];
            return Math.round(v.algorithm) === 1 ? octreePalette(colors, count) : medianCut(colors, count);
        },
        run(a) {
            const palette = a.state, dither = Math.round(a.values.dither) / 8, threshold = Math.round(a.values.threshold ?? 0);
            const cache = new Int16Array(32768).fill(-1);
            const nearest = (r, g, b) => {
                const key = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                if (cache[key] >= 0)
                    return cache[key];
                let best = 0, bestD = Infinity;
                for (let i = 0; i < palette.length; ++i) {
                    const dr = palette[i][0] - r, dg = palette[i][1] - g, db = palette[i][2] - b;
                    const dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                    if (dd < bestD) {
                        bestD = dd;
                        best = i;
                    }
                }
                cache[key] = best;
                return best;
            };
            const w = a.roi.width, S = a.src.px, d = a.dst.px, iw = a.src.w;
            const err = [new Float32Array((w + 2) * 3), new Float32Array((w + 2) * 3)];
            for (let y = a.y0; y <= a.y1; ++y) {
                const cur = err[(y - a.roi.top) % 2], next = err[(y - a.roi.top + 1) % 2];
                next.fill(0);
                for (let i = 0; i < w; ++i) {
                    const x = a.roi.left + i, s = S[y * iw + x];
                    if (qAlpha(s) === 0 || qAlpha(s) < threshold) {
                        d[y * iw + x] = 0;
                        continue;
                    }
                    const u = qUnpremultiply(s), e = (i + 1) * 3;
                    const want = [qRed(u) + cur[e], qGreen(u) + cur[e + 1], qBlue(u) + cur[e + 2]];
                    const c = palette[nearest(clamp255f(want[0]), clamp255f(want[1]), clamp255f(want[2]))];
                    d[y * iw + x] = qPremultiply(qRgba(c[0], c[1], c[2], qAlpha(u)));
                    if (dither <= 0)
                        continue;
                    for (let k = 0; k < 3; ++k) {
                        const diff = Math.fround((want[k] - c[k]) * dither);
                        cur[(i + 2) * 3 + k] += (diff * 7) / 16;
                        next[i * 3 + k] += (diff * 3) / 16;
                        next[(i + 1) * 3 + k] += (diff * 5) / 16;
                        next[(i + 2) * 3 + k] += diff / 16;
                    }
                }
            }
        },
    },
];

// --- Distorsionar -------------------------------------------------------------------------

// Máximo o mínimo deslizante en una dirección (algoritmo de la cola monótona).
function morphPass(inImg, outImg, radius, horizontal, dilate) {
    const n = horizontal ? inImg.w : inImg.h, lines = horizontal ? inImg.h : inImg.w, W = inImg.w;
    const values = new Int32Array(n), dq = new Int32Array(n);
    for (let line = 0; line < lines; ++line)
        for (let c = 0; c < 4; ++c) {
            const shift = c * 8;
            for (let i = 0; i < n; ++i)
                values[i] = (inImg.px[horizontal ? line * W + i : i * W + line] >>> shift) & 255;
            let head = 0, tail = 0, next = 0;
            for (let i = 0; i < n; ++i) {
                while (next < n && next <= i + radius) {
                    const v = values[next];
                    while (tail > head && (dilate ? values[dq[tail - 1]] <= v : values[dq[tail - 1]] >= v))
                        --tail;
                    dq[tail++] = next++;
                }
                while (dq[head] < i - radius)
                    ++head;
                const v = values[dq[head]], k = horizontal ? line * W + i : i * W + line;
                outImg.px[k] = ((outImg.px[k] & ~(0xff << shift)) | (v << shift)) >>> 0;
            }
        }
}

function renderPixelate(a) {
    // Paint.NET 5: la rejilla pasa por el ancla; cada celda toma un color (reducir a escala) y
    // luego se amplía con el filtro elegido.
    const s = Math.max(1, Math.round(a.values.cell)), anchor = a.values.anchor || { x: 0, y: 0 };
    const down = Math.round(a.values.down), up = Math.round(a.values.up);
    const ax = a.roi.left + (a.roi.width * (1 + anchor.x)) / 2, ay = a.roi.top + (a.roi.height * (1 + anchor.y)) / 2;
    const ox = Math.floor(ax % s), oy = Math.floor(ay % s);
    const src = a.src;
    const c0 = Math.floor((a.roi.left - ox) / s) - 1, r0 = Math.floor((a.roi.top - oy) / s) - 1;
    const c1 = Math.floor((a.roi.right - ox) / s) + 1, r1 = Math.floor((a.roi.bottom - oy) / s) + 1;
    const cols = c1 - c0 + 1, rows = r1 - r0 + 1;
    const cells = new Uint32Array(cols * rows);
    // Solo las filas de celdas que tocan la franja (y las dos de alrededor para el bicúbico).
    const rFirst = Math.max(0, Math.floor((a.y0 - oy) / s) - 2 - r0), rLast = Math.min(rows - 1, Math.floor((a.y1 - oy) / s) + 2 - r0);
    for (let r = rFirst; r <= rLast; ++r)
        for (let c = 0; c < cols; ++c) {
            const left = ox + (c0 + c) * s, top = oy + (r0 + r) * s;
            const cell = intersected(rect(left, top, s, s), imageRect(src));
            let col = 0;
            if (!isEmpty(cell)) {
                const cx = left + s / 2, cy = top + s / 2;
                if (down === 3 || down === 4) {
                    const dd = down === 3 ? s / 4 : 0;
                    col = average([sampleBilinear(src, cx - dd, cy - dd), sampleBilinear(src, cx + dd, cy - dd),
                                   sampleBilinear(src, cx - dd, cy + dd), sampleBilinear(src, cx + dd, cy + dd)], 4);
                } else if (down === 5) {
                    col = clamped(src, Math.trunc(cx), Math.trunc(cy));
                } else {
                    let sr = 0, sg = 0, sb = 0, sa = 0;
                    for (let y = cell.top; y <= cell.bottom; ++y)
                        for (let x = cell.left; x <= cell.right; ++x) {
                            const p = at(src, x, y);
                            sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
                        }
                    const n = cell.width * cell.height;
                    col = qRgba(Math.floor(sr / n), Math.floor(sg / n), Math.floor(sb / n), Math.floor(sa / n));
                }
            }
            cells[r * cols + c] = col;
        }
    const cellAt = (c, r) => cells[clamp(r, 0, rows - 1) * cols + clamp(c, 0, cols - 1)];
    const weight = (t) => {
        t = Math.abs(t);
        if (up === 1)
            return Math.max(0, 1 - t);
        return t < 1 ? 1.5 * t * t * t - 2.5 * t * t + 1 : t < 2 ? -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2 : 0;
    };
    const d = a.dst.px, w = src.w, reach = up === 0 ? 2 : 1;
    for (let y = a.y0; y <= a.y1; ++y)
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const fx = (x + 0.5 - ox) / s - c0, fy = (y + 0.5 - oy) / s - r0;
            if (up === 2) {
                d[y * w + x] = cellAt(Math.floor(fx), Math.floor(fy));
                continue;
            }
            const gx = fx - 0.5, gy = fy - 0.5, ix = Math.floor(gx), iy = Math.floor(gy), tx = gx - ix, ty = gy - iy;
            let r = 0, g = 0, b = 0, al = 0, total = 0;
            for (let j = 1 - reach; j <= reach; ++j)
                for (let i = 1 - reach; i <= reach; ++i) {
                    const wt = weight(i - tx) * weight(j - ty), p = cellAt(ix + i, iy + j);
                    r += qRed(p) * wt; g += qGreen(p) * wt; b += qBlue(p) * wt; al += qAlpha(p) * wt;
                    total += wt;
                }
            const ch = (v) => clamp(lround(v / total), 0, 255);
            const A = ch(al);
            d[y * w + x] = qRgba(Math.min(A, ch(r)), Math.min(A, ch(g)), Math.min(A, ch(b)), A);
        }
}

const distort = [
    {
        id: "bulge", name: "Abultar", category: "distort",
        params: [P.real("amount", "Abultar", -1, 1, 0.45, 2), P.offset("offset", "Centrar"), edgeParam(pdnEdges, Edge.Clamp), qualityParam(2)],
        run(a) {
            const amt = a.values.amount, maxrad = defaultRadius(a.roi);
            warp(a, { offset: a.values.offset, quality: Math.round(a.values.quality), edge: edgeValue(a.values, pdnEdges) }, (u, v) => {
                const r = Math.sqrt(u * u + v * v), s1 = 1 - r / maxrad;
                if (s1 <= 0)
                    return [u, v];
                const s2 = 1 - amt * s1 * s1;
                return [u * s2, v * s2];
            });
        },
    },
    {
        id: "crystalize", name: "Cristalizar", category: "distort",
        params: [P.int("cell", "Tamaño de celda", 2, 250, 8), qualityParam(2), P.seed()],
        run(a) {
            // Diagrama de Voronoi: un punto aleatorio por celda de la rejilla; cada píxel toma el
            // color del punto más cercano.
            const cell = Math.max(2, Math.round(a.values.cell)), seed = Math.round(a.values.seed) >>> 0;
            const aa = rgssOffsets(Math.round(a.values.quality));
            const site = (cx, cy) => [(cx + hashUnit(cx, cy, seed)) * cell, (cy + hashUnit(cx, cy, seed + 7)) * cell];
            const src = a.src, d = a.dst.px, w = src.w, samples = new Uint32Array(aa.length);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    for (let p = 0; p < aa.length; ++p) {
                        const px = x + 0.5 + aa[p][0], py = y + 0.5 + aa[p][1];
                        const cx = Math.floor(px / cell), cy = Math.floor(py / cell);
                        let best = Infinity, bx = 0, by = 0;
                        for (let j = -1; j <= 1; ++j)
                            for (let i = -1; i <= 1; ++i) {
                                const s = site(cx + i, cy + j);
                                const dd = (s[0] - px) * (s[0] - px) + (s[1] - py) * (s[1] - py);
                                if (dd < best) {
                                    best = dd;
                                    bx = s[0];
                                    by = s[1];
                                }
                            }
                        samples[p] = clamped(src, Math.trunc(bx), Math.trunc(by));
                    }
                    d[y * w + x] = average(samples, aa.length);
                }
        },
    },
    {
        id: "dents", name: "Abolladuras", category: "distort",
        params: [P.real("scale", "Cambiar tamaño", 1, 200, 25, 2), P.real("refraction", "Refracción", 0, 200, 50, 2),
                 P.real("roughness", "Detalle", 0, 100, 10, 2), P.real("tension", "Turbulencia", 0, 100, 10, 2),
                 P.angle("angle", "Ángulo", -180, 180, 0), qualityParam(2), P.seed()],
        run(a) {
            // DentsEffect de Paint.NET: refracción según un ruido de Perlin girado.
            const v = a.values, seed = Math.round(v.seed) & 255;
            const scaleR = 400 / defaultRadius(a.roi) / v.scale;
            const refractionScale = v.refraction / 100 / scaleR;
            const theta = (Math.PI * 2 * v.tension) / 10, roughness = v.roughness / 100;
            let detail = 1 + v.roughness / 10;
            const maxDetail = Math.floor(Math.log(scaleR) / Math.log(0.5));
            if (detail > maxDetail && maxDetail >= 1)
                detail = maxDetail;
            const rot = (v.angle * Math.PI) / 180, cr = Math.cos(rot), sr = Math.sin(rot);
            warp(a, { quality: Math.round(v.quality), edge: Edge.Wrap }, (x, y) => {
                const rx = x * cr - y * sr, ry = x * sr + y * cr;
                const bump = theta * pdnPerlin(rx * scaleR, ry * scaleR, detail, roughness, seed);
                return [x + refractionScale * Math.sin(-bump), y + refractionScale * Math.cos(bump)];
            });
        },
    },
    {
        id: "frostedglass", name: "Cristal congelado", category: "distort",
        params: [P.real("max", "Radio de dispersión máximo", 0, 200, 3, 2), P.real("min", "Radio de dispersión mínimo", 0, 200, 0, 2),
                 P.real("diffusion", "Difusión", 0, 3, 1, 2), P.int("smoothness", "Suavizado", 1, 8, 2), P.seed()],
        run(a) {
            // Difusión: escala la distancia de dispersión (1 = la de los radios).
            const v = a.values, maxR = v.max * v.diffusion;
            const minR = Math.min(v.min * v.diffusion, Math.min(a.src.w, a.src.h) / 2);
            const samples = clamp(Math.round(v.smoothness), 1, 8), seed = Math.round(v.seed) >>> 0;
            const delta = Math.max(0, maxR - minR);
            const src = a.src, d = a.dst.px, w = src.w, s = new Uint32Array(samples);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    for (let i = 0; i < samples; ++i) {
                        let sx = x, sy = y;
                        for (let attempt = 0; attempt < 8; ++attempt) {
                            const k = i * 8 + attempt;
                            const ang = hashUnit(x, (y * 64 + k) >>> 0, seed) * 2 * Math.PI;
                            const dist = minR + hashUnit((x * 64 + k) >>> 0, y, seed + 13) * delta;
                            sx = x + Math.cos(ang) * dist;
                            sy = y + Math.sin(ang) * dist;
                            if (sx >= 0 && sx <= src.w && sy >= 0 && sy <= src.h)
                                break;
                        }
                        s[i] = sampleBilinear(src, sx + 0.5, sy + 0.5);
                    }
                    d[y * w + x] = average(s, samples);
                }
        },
    },
    {
        id: "morphology", name: "Morfología", category: "distort",
        params: [linked(P.int("width", "Anchura", 1, 100, 5), "linked"), linked(P.int("height", "Altura", 1, 100, 5), "linked"),
                 P.bool("linked", "Vinculado", true), P.choice("mode", "Modo", ["Dilatar", "Erosionar"], 0)],
        run(a) {
            const dilate = Math.round(a.values.mode) === 0, rw = Math.round(a.values.width), rh = Math.round(a.values.height);
            const big = intersected(adjusted(band(a), Math.max(rw, rh) + 1), imageRect(a.src));
            const t1 = crop(a.src, big), t2 = crop(a.src, big);
            morphPass(t1, t2, rw, true, dilate);
            morphPass(t2, t1, rh, false, dilate);
            const d = a.dst.px, w = a.dst.w;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const p = t1.px[(y - big.top) * t1.w + x - big.left], al = qAlpha(p);
                    d[y * w + x] = qRgba(Math.min(al, qRed(p)), Math.min(al, qGreen(p)), Math.min(al, qBlue(p)), al);
                }
        },
    },
    {
        id: "pixelate", name: "Pixelado", category: "distort",
        params: [P.int("cell", "Tamaño de celda", 1, 100, 2), P.offset("anchor", "Ancla"),
                 P.choice("down", "Reducir a escala", ["Anisotrópico", "Bicúbico (calidad alta)", "Multimuestra bilineal", "Bicúbico", "Bilineal",
                                                       "Vecino más próximo"], 2),
                 P.choice("up", "Aumentar a escala", ["Bicúbico", "Bilineal", "Vecino más próximo"], 2)],
        run: renderPixelate,
    },
    {
        id: "polarinversion", name: "Inversión polar", category: "distort",
        params: [P.real("amount", "Escala", -4, 4, 1, 2), P.offset("offset", "Desplazamiento"), edgeParam(pdnEdges, Edge.Reflect), qualityParam(2)],
        run(a) {
            const amount = a.values.amount, r2 = defaultRadius(a.roi) ** 2;
            warp(a, { offset: a.values.offset, quality: Math.round(a.values.quality), edge: edgeValue(a.values, pdnEdges) }, (x, y) => {
                const dd = x * x + y * y;
                if (dd < 1e-9)
                    return [x, y];
                const inv = 1 + (r2 / dd - 1) * amount;
                return [x * inv, y * inv];
            });
        },
    },
    {
        id: "tilereflection", name: "Reflejo en mosaico", category: "distort",
        params: [P.angle("angle", "Ángulo", -180, 180, 30), P.real("size", "Tamaño del mosaico", 1, 800, 40, 2),
                 P.real("curvature", "Curvatura", -100, 100, 8, 2), edgeParam(pdnEdges, Edge.Reflect), qualityParam(2)],
        run(a) {
            const rotation = -a.values.angle, size = a.values.size, curvature = a.values.curvature;
            const sn = Math.sin((rotation * Math.PI) / 180), cs = Math.cos((rotation * Math.PI) / 180);
            const scale = Math.PI / size, strength = ((curvature * curvature) / 10) * (curvature < 0 ? -1 : 1);
            let quality = Math.round(a.values.quality);
            if (quality !== 1)
                ++quality;
            const aa = rgssOffsets(quality), edge = edgeValue(a.values, pdnEdges);
            const src = a.src, d = a.dst.px, w = src.w, hw = src.w / 2, hh = src.h / 2, samples = new Uint32Array(aa.length);
            for (let y = a.y0; y <= a.y1; ++y) {
                const j = y - hh;
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const i = x - hw;
                    for (let p = 0; p < aa.length; ++p) {
                        const u1 = i + aa[p][0], v1 = j - aa[p][1];
                        const s1 = cs * u1 + sn * v1, t1 = -sn * u1 + cs * v1;
                        const s2 = s1 + strength * Math.tan(s1 * scale), t2 = t1 + strength * Math.tan(t1 * scale);
                        const u2 = cs * s2 - sn * t2, v2 = sn * s2 + cs * t2;
                        samples[p] = sampleEdge(src, hw + u2 + 0.5, hh + v2 + 0.5, edge);
                    }
                    d[y * w + x] = average(samples, aa.length);
                }
            }
        },
    },
    {
        id: "twist", name: "Giro", category: "distort",
        params: [P.int("amount", "Cantidad y dirección", -200, 200, 30), P.real("size", "Tamaño", 0.01, 2, 1, 2), P.offset("offset", "Centrar"),
                 qualityParam(2)],
        run(a) {
            const amount = -a.values.amount, size = 1 / a.values.size, off = a.values.offset || { x: 0, y: 0 };
            const twist = amount * amount * (amount < 0 ? -1 : 1);
            const hw = a.roi.left + (a.roi.width / 2) * (1 + off.x), hh = a.roi.top + (a.roi.height / 2) * (1 + off.y);
            const invmax = 1 / Math.min(a.roi.width / 2, a.roi.height / 2);
            const aa = rgssOffsets(Math.round(a.values.quality));
            const src = a.src, d = a.dst.px, w = src.w, samples = new Uint32Array(aa.length);
            for (let y = a.y0; y <= a.y1; ++y) {
                const j = y - hh;
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const i = x - hw;
                    for (let p = 0; p < aa.length; ++p) {
                        const u = i + aa[p][0], v = j + aa[p][1];
                        const rad = Math.sqrt(u * u + v * v);
                        let th = Math.atan2(v, u), t = 1 - rad * size * invmax;
                        t = t < 0 ? 0 : t * t * t;
                        th += (t * twist) / 100;
                        samples[p] = sampleBilinear(src, hw + rad * Math.cos(th) + 0.5, hh + rad * Math.sin(th) + 0.5);
                    }
                    d[y * w + x] = average(samples, aa.length);
                }
            }
        },
    },
];

// --- Ruido --------------------------------------------------------------------------------

const NOISE_TABLE = 16384;
let normalTable = null;
// Tabla de la curva normal de AddNoiseEffect: un índice aleatorio uniforme da un valor con
// distribución gaussiana.
function normalLookup() {
    if (normalTable)
        return normalTable;
    const curve = (x, scale) => scale * Math.exp((-x * x) / 2);
    let l = 5, r = 10, scale = 50, sum = 0;
    while (r - l > 0.0000001) {
        sum = 0;
        scale = (l + r) * 0.5;
        for (let i = 0; i < NOISE_TABLE; ++i) {
            sum += curve((16 * (i - NOISE_TABLE / 2)) / NOISE_TABLE, scale);
            if (sum > 1000000)
                break;
        }
        if (sum > NOISE_TABLE)
            r = scale;
        else if (sum < NOISE_TABLE)
            l = scale;
        else
            break;
    }
    const t = new Int32Array(NOISE_TABLE);
    sum = 0;
    let rounded = 0;
    for (let i = 0; i < NOISE_TABLE; ++i) {
        sum += curve((16 * (i - NOISE_TABLE / 2)) / NOISE_TABLE, scale);
        const last = rounded;
        rounded = Math.trunc(sum);
        for (let j = last; j < rounded && j < NOISE_TABLE; ++j)
            t[j] = idiv((i - NOISE_TABLE / 2) * 65536, NOISE_TABLE);
    }
    return (normalTable = t);
}

const noise = [
    {
        id: "addnoise", name: "Agregar ruido", category: "noise",
        params: [P.int("intensity", "Intensidad", 0, 100, 64), P.int("saturation", "Saturación de colores", 0, 400, 100),
                 P.real("coverage", "Cobertura", 0, 100, 100, 2), P.seed()],
        run(a) {
            const strength = Math.round(a.values.intensity), sat = idiv(Math.round(a.values.saturation) * 4096, 100);
            const coverage = a.values.coverage / 100, seed = Math.imul(Math.round(a.values.seed), 2654435761 | 0) >>> 0;
            const dev = idiv(strength * strength, 4), lookup = normalLookup();
            const S = a.src.px, d = a.dst.px, w = a.src.w;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const k = y * w + x;
                    if (hashUnit(x, y, seed) > coverage || qAlpha(S[k]) === 0) {
                        d[k] = S[k];
                        continue;
                    }
                    let r = lookup[hash3(x, y, (seed + 1) >>> 0) % NOISE_TABLE];
                    let g = lookup[hash3(x, y, (seed + 2) >>> 0) % NOISE_TABLE];
                    let b = lookup[hash3(x, y, (seed + 3) >>> 0) % NOISE_TABLE];
                    const i = (4899 * r + 9618 * g + 1867 * b) >> 14;
                    r = i + (((r - i) * sat) >> 12);
                    g = i + (((g - i) * sat) >> 12);
                    b = i + (((b - i) * sat) >> 12);
                    const u = qUnpremultiply(S[k]);
                    d[k] = qPremultiply(qRgba(clamp255(qRed(u) + ((r * dev + 32768) >> 16)), clamp255(qGreen(u) + ((g * dev + 32768) >> 16)),
                                              clamp255(qBlue(u) + ((b * dev + 32768) >> 16)), qAlpha(u)));
                }
        },
    },
    {
        id: "reducenoise", name: "Reducción de ruido", category: "noise",
        params: [P.int("radius", "Radio", 0, 200, 10), P.real("strength", "Intensidad", 0, 1, 0.4, 2)],
        run(a) {
            const strength = a.values.strength;
            localHistogram(a, Math.round(a.values.radius), (c, h) => {
                let rc = 0, gc = 0, bc = 0;
                for (let i = 0; i < qRed(c); ++i) rc += h.r[i];
                for (let i = 0; i < qGreen(c); ++i) gc += h.g[i];
                for (let i = 0; i < qBlue(c); ++i) bc += h.b[i];
                const area = Math.max(1, h.area);
                const normalized = qRgb(idiv(rc * 255, area), idiv(gc * 255, area), idiv(bc * 255, area));
                const out = lerpColor(c, normalized, strength * (1 - 0.75 * intensityF(c)));
                return qRgba(qRed(out), qGreen(out), qBlue(out), qAlpha(c));
            });
        },
    },
];

// --- Objeto -------------------------------------------------------------------------------

const object = [
    {
        id: "dropshadow", name: "Sombra paralela", category: "object",
        params: [P.real("radius", "Radio de sombra", 0, 100, 10, 1), P.real("distance", "Distancia", 0, 100, 10, 1),
                 P.angle("angle", "Ángulo", -180, 180, -45), P.real("opacity", "Opacidad", 0, 1, 0.75, 2), P.color("color", "Color", "black"),
                 P.bool("only", "Solo proyectar sombra", false)],
        note: "Este efecto requiere una capa con transparencia. Asegúrese de que el objeto para el que desea una sombra paralela esté en su propia capa.",
        run(a) {
            // Sombra: el alfa del objeto desplazado, desenfocado y teñido, debajo del objeto.
            const v = a.values, blur = lround(v.radius), distance = Math.ceil(v.distance);
            const ang = (-v.angle * Math.PI) / 180;
            const off = [lround(Math.cos(ang) * v.distance), lround(Math.sin(ang) * v.distance)];
            const col = v.color, opacity = lround(v.opacity * 255), only = !!v.only;
            const margin = tentMargin(blur) + distance + 1;
            const area = band(a), big = intersected(adjusted(area, margin), imageRect(a.src));
            const sil = image(big.width, big.height, true), src = a.src;
            for (let y = 0; y < big.height; ++y) {
                const sy = big.top + y - off[1];
                if (sy < 0 || sy >= src.h)
                    continue;
                for (let x = 0; x < big.width; ++x) {
                    const sx = big.left + x - off[0];
                    if (sx >= 0 && sx < src.w) {
                        const al = qAlpha(at(src, sx, sy));
                        sil.px[y * big.width + x] = qRgba(al, al, al, al);
                    }
                }
            }
            const bl = tentBlur(sil, translated(area, -big.left, -big.top), blur);
            const S = src.px, d = a.dst.px, w = src.w;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const al = idiv(idiv(qAlpha(bl.px[(y - a.y0) * bl.w + x - a.roi.left]) * opacity, 255) * col.a, 255);
                    const shadow = qPremultiply(qRgba(col.r, col.g, col.b, al));
                    d[y * w + x] = only ? shadow : overPixel(shadow, S[y * w + x]);
                }
        },
    },
];

// --- Fotografía ---------------------------------------------------------------------------

const photo = [
    {
        id: "glow", name: "Resplandor", category: "photo",
        params: [P.real("radius", "Radio", 1, 20, 6, 1), P.int("brightness", "Brillo", -100, 100, 10), P.int("contrast", "Contraste", -100, 100, 10)],
        run: (a) => glowInto(a, Math.max(1, lround(a.values.radius)), Math.round(a.values.brightness), Math.round(a.values.contrast)),
    },
    {
        id: "redeye", name: "Eliminar ojos rojos", category: "photo", params: [P.int("force", "Fuerza", 1, 5, 3)],
        note: "Para obtener mejores resultados, utilice primero las herramientas de selección para seleccionar cada ojo.",
        run(a) {
            // UnaryPixelOps.RedEyeRemove de Paint.NET. Paint.NET 5 tiene un solo control,
            // "Fuerza": 3 equivale a la tolerancia 70 y la saturación 90 %.
            const force = a.values.force, tolerance = lround(100 - 10 * force), strength = clamp(1.2 - 0.1 * force, 0, 1);
            mapStraight(a, (c) => {
                const r = qRed(c), g = qGreen(c), b = qBlue(c), mx = Math.max(r, g, b), mn = Math.min(r, g, b);
                const saturation = mx === 0 || mx === mn ? 0 : Math.trunc(((mx - mn) * 255) / mx);
                if (r - Math.max(b, g) > tolerance && saturation > 100)
                    return qRgba(Math.trunc(255 * intensityF(c) * strength), g, b, qAlpha(c));
                return c;
            });
        },
    },
    {
        id: "sharpen", name: "Nítido", category: "photo",
        params: [P.real("amount", "Cantidad", 1, 20, 2, 2), P.real("threshold", "Umbral", 0, 100, 0, 2)],
        run(a) {
            // SharpenEffect de Paint.NET: se aleja de la mediana local. Con Umbral solo se
            // refuerzan las diferencias mayores (no el ruido de zonas lisas).
            const threshold = lround(a.values.threshold * 2.55);
            localHistogram(a, Math.max(1, lround(a.values.amount)), (c, h) => {
                const median = histogramPercentile(h, 50);
                if (threshold > 0) {
                    const diff = Math.max(Math.abs(qRed(c) - qRed(median)), Math.abs(qGreen(c) - qGreen(median)), Math.abs(qBlue(c) - qBlue(median)));
                    if (diff < threshold)
                        return c;
                }
                return lerpColor(c, median, -0.5);
            });
        },
    },
    {
        id: "softenportrait", name: "Suavizado de retrato", category: "photo",
        params: [P.real("softness", "Suavizado", 0, 10, 5, 1), P.int("lighting", "Iluminación", -20, 20, 0), P.int("warmth", "Temperatura", 0, 20, 10)],
        run(a) {
            const lighting = Math.round(a.values.lighting), warmth = Math.round(a.values.warmth);
            const bl = tentBlur(a.src, band(a), lround(a.values.softness * 3));
            const bc = new BrightnessContrast(lighting, idiv(-lighting, 2));
            const redAdjust = 1 + warmth / 100, blueAdjust = 1 - warmth / 100;
            const S = a.src.px, d = a.dst.px, w = a.src.w;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const su = qUnpremultiply(S[y * w + x]), i = intensity(su);
                    const grey = qRgba(clamp255(Math.trunc(i * redAdjust)), i, clamp255(Math.trunc(i * blueAdjust)), qAlpha(su));
                    const top = bc.apply(qUnpremultiply(bl.px[(y - a.y0) * bl.w + x - a.roi.left]));
                    d[y * w + x] = qPremultiply(blendStraight(grey, top, opOverlay));
                }
        },
    },
    {
        id: "straighten", name: "Enderezar", category: "photo",
        params: [P.angle("angle", "Ángulo", -45, 45, 0), P.choice("sampling", "Muestreo", ["Bicúbico", "Bilineal", "Vecino más próximo"], 0)],
        run(a) {
            // Gira alrededor del centro y amplía lo justo para cubrir toda la selección.
            const angle = a.values.angle, th = (Math.abs(angle) * Math.PI) / 180;
            const w = a.roi.width, h = a.roi.height;
            const s = Math.max((w * Math.cos(th) + h * Math.sin(th)) / w, (w * Math.sin(th) + h * Math.cos(th)) / h);
            const cx = a.roi.left + w / 2, cy = a.roi.top + h / 2;
            const t = new Transform().translate(cx, cy).rotate(angle).scale(s, s).translate(-cx, -cy);
            const inv = Transform.multiply(new Transform([1, 0, 0, 0, 1, 0, a.roi.left, a.roi.top, 1]), t).inverted();
            const region = crop(a.src, a.roi), sampling = Math.round(a.values.sampling), d = a.dst.px, iw = a.dst.w;
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    let s2 = 0;
                    if (inv) {
                        const [px, py] = inv.map(x + 0.5, y + 0.5);
                        s2 = sampling === 2 ? pixelOrTransparent(region, Math.floor(px), Math.floor(py))
                           : sampling === 1 ? sampleBilinearTransparent(region, px, py) : sampleBicubicTransparent(region, px, py);
                    }
                    d[y * iw + x] = s2;
                }
        },
    },
    {
        id: "vignette", name: "Viñeta", category: "photo",
        params: [P.offset("offset", "Centrar"), P.real("radius", "Radio", 0.1, 4, 0.5, 2), P.real("amount", "Intensidad", 0, 1, 1, 2)],
        run(a) {
            // VignetteEffect de Paint.NET (en luz lineal).
            const off = a.values.offset || { x: 0, y: 0 }, amount = a.values.amount, amount1 = 1 - amount;
            const xc = a.roi.left + a.roi.width * (1 + off.x) * 0.5, yc = a.roi.top + a.roi.height * (1 + off.y) * 0.5;
            let radius = Math.max(a.roi.width, a.roi.height) * 0.5 * a.values.radius;
            radius *= radius;
            const radiusR = Math.PI / (8 * radius);
            const lin = new Float64Array(256).map((_, i) => toLinear(i));
            const S = a.src.px, d = a.dst.px, w = a.src.w;
            for (let y = a.y0; y <= a.y1; ++y) {
                const iy2 = (y - yc) * (y - yc);
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const ix = x - xc, dd = (iy2 + ix * ix) * radiusR;
                    let factor = Math.cos(dd);
                    if (factor <= 0 || dd > Math.PI) {
                        factor = amount1;
                    } else {
                        factor *= factor;
                        factor *= factor;
                        factor = amount1 + amount * factor;
                    }
                    const u = qUnpremultiply(S[y * w + x]);
                    d[y * w + x] = qPremultiply(qRgba(toSrgb(lin[qRed(u)] * factor), toSrgb(lin[qGreen(u)] * factor), toSrgb(lin[qBlue(u)] * factor), qAlpha(u)));
                }
            }
        },
    },
];

// --- Representación -----------------------------------------------------------------------

const permRender = (i) => PERMUTATION[i & 255];
function cloudNoise(ix, iy, x, y, seed) {
    const u = fade(x), v = fade(y);
    const a = permRender(ix + seed) + iy, aa = permRender(a), ab = permRender(a + 1);
    const b = permRender(ix + 1 + seed) + iy, ba = permRender(b), bb = permRender(b + 1);
    const e1 = grad(permRender(aa), x, y) + u * (grad(permRender(ba), x - 1, y) - grad(permRender(aa), x, y));
    const e2 = grad(permRender(ab), x, y - 1) + u * (grad(permRender(bb), x - 1, y - 1) - grad(permRender(ab), x, y - 1));
    return e1 + v * (e2 - e1);
}

const render = [
    {
        id: "clouds", name: "Nubes", category: "render",
        params: [P.int("scale", "Escala", 2, 1000, 250), P.real("power", "Aspereza", 0, 1, 0.5, 2), renderBlendParam(RB.Over), P.seed(),
                 P.color("color1", "Color 1", "primary"), P.color("color2", "Color 2", "secondary")]
            .map((p, i) => ({ ...p, tab: i < 4 ? "Nubes" : "Colores" })),
        run(a) {
            const scale = Math.round(a.values.scale), power = a.values.power, seed = Math.round(a.values.seed) & 255;
            const c1 = a.values.color1, c2 = a.values.color2;
            const from = qRgba(c1.r, c1.g, c1.b, c1.a), to = qRgba(c2.r, c2.g, c2.b, c2.a);
            const w = a.src.w, h = a.src.h, out = image(a.roi.width, a.y1 - a.y0 + 1, true);
            for (let y = a.y0; y <= a.y1; ++y) {
                const dy = 2 * y - h;
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const dx = 2 * x - w;
                    let val = 0, mult = 1, div = scale;
                    for (let i = 0; i < 12 && mult > 0.03 && div > 0; ++i) {
                        let dxr = 65536 + dx / div, dyr = 65536 + dy / div;
                        const dxd = Math.trunc(dxr), dyd = Math.trunc(dyr);
                        dxr -= dxd;
                        dyr -= dyd;
                        val += cloudNoise(dxd & 255, dyd & 255, dxr, dyr, (seed ^ i) & 255) * mult;
                        div = Math.trunc(div / 2);
                        mult *= power;
                    }
                    out.px[(y - a.y0) * out.w + x - a.roi.left] = qPremultiply(lerpColor(from, to, (val + 1) / 2));
                }
            }
            blendResult(a, out);
        },
    },
    {
        id: "julia", name: "Fractal Julia", category: "render",
        params: [P.real("factor", "Factor", 1, 10, 4, 2), P.real("zoom", "Zum", 0.1, 50, 1, 2), P.angle("angle", "Ángulo", -180, 180, 0),
                 qualityParam(2), renderBlendParam(RB.Overwrite)],
        run(a) {
            const v = a.values, quality = Math.round(v.quality), angleTheta = (v.angle * Math.PI * 2) / 360;
            const jr = 0.3125, ji = 0.03, log10000 = Math.log(10000);
            const w = a.src.w, h = a.src.h, invH = 1 / h, invZoom = 1 / v.zoom, invQuality = 1 / quality, aspect = h / w;
            const count = quality * quality + 1, invCount = 1 / count;
            const out = image(a.roi.width, a.y1 - a.y0 + 1, true);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    let r = 0, g = 0, b = 0, al = 0;
                    for (let i = 0; i < count; ++i) {
                        const u = (2 * x - w + i * invCount) * invH, vv = (2 * y - h + ((i * invQuality) % 1)) * invH;
                        const radius = Math.sqrt(u * u + vv * vv), theta = Math.atan2(vv, u) + angleTheta;
                        const up = radius * Math.cos(theta), vp = radius * Math.sin(theta);
                        let jx = (up - vp * aspect) * invZoom, jy = (vp + up * aspect) * invZoom, c = 0;
                        while (c < 256 && jx * jx + jy * jy < 10000) {
                            const t = jx;
                            jx = jx * jx - jy * jy + jr;
                            jy = 2 * t * jy + ji;
                            ++c;
                        }
                        c -= 2 - (2 * log10000) / Math.log(jx * jx + jy * jy);
                        c *= v.factor;
                        b += clamp255(Math.trunc(c - 768)); g += clamp255(Math.trunc(c - 512));
                        r += clamp255(Math.trunc(c - 256)); al += clamp255(Math.trunc(c));
                    }
                    out.px[(y - a.y0) * out.w + x - a.roi.left] = qPremultiply(qRgba(idiv(r, count), idiv(g, count), idiv(b, count), idiv(al, count)));
                }
            blendResult(a, out);
        },
    },
    {
        id: "mandelbrot", name: "Fractal Mandelbrot", category: "render",
        params: [P.real("factor", "Factor", 1, 10, 1, 2), P.real("zoom", "Zum", 0, 100, 10, 2), P.angle("angle", "Ángulo", -180, 180, 0),
                 qualityParam(2), P.bool("invert", "Invertir colores", false), renderBlendParam(RB.Overwrite)],
        run(a) {
            const v = a.values, zoom = 1 + 20 * v.zoom, factor = v.factor, quality = Math.round(v.quality);
            const angleTheta = (v.angle * 2 * Math.PI) / 360, invert = !!v.invert;
            const maxV = 100000, invLogMax = 1 / Math.log(maxV), xOffset = -0.7, yOffset = -0.29;
            const w = a.src.w, h = a.src.h, invH = 1 / h, invZoom = 1 / zoom, invQuality = 1 / quality;
            const count = quality * quality + 1, invCount = 1 / count;
            const out = image(a.roi.width, a.y1 - a.y0 + 1, true);
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    let r = 0, g = 0, b = 0, al = 0;
                    for (let i = 0; i < count; ++i) {
                        const u = (2 * x - w + i * invCount) * invH, vv = (2 * y - h + ((i * invQuality) % 1)) * invH;
                        const radius = Math.sqrt(u * u + vv * vv), theta = Math.atan2(vv, u) + angleTheta;
                        const cr = radius * Math.cos(theta) * invZoom + xOffset, ci = radius * Math.sin(theta) * invZoom + yOffset;
                        let it = 0, zx = 0, zy = 0;
                        while (it * factor < 1024 && zx * zx + zy * zy < maxV) {
                            const t = zx;
                            zx = zx * zx - zy * zy + cr;
                            zy = 2 * t * zy + ci;
                            ++it;
                        }
                        const m = it - Math.log(zy * zy + zx * zx) * invLogMax, c = 64 + factor * m;
                        r += clamp255(Math.trunc(c - 768)); g += clamp255(Math.trunc(c - 512));
                        b += clamp255(Math.trunc(c - 256)); al += clamp255(Math.trunc(c));
                    }
                    let rr = idiv(r, count), gg = idiv(g, count), bb = idiv(b, count);
                    if (invert) {
                        rr = 255 - rr; gg = 255 - gg; bb = 255 - bb;
                    }
                    out.px[(y - a.y0) * out.w + x - a.roi.left] = qPremultiply(qRgba(rr, gg, bb, idiv(al, count)));
                }
            blendResult(a, out);
        },
    },
    {
        id: "turbulence", name: "Turbulencia", category: "render",
        params: [P.int("octaves", "Octavas", 1, 12, 4), P.real("period", "Período", 2, 1000, 100, 2), P.int("size", "Tamaño", 16, 4096, 4096),
                 P.choice("noise", "Ruido", ["Suma Fractal", "Turbulencia"], 1), P.seed(), renderBlendParam(RB.Over)],
        run(a) {
            const v = a.values, octaves = Math.round(v.octaves), period = v.period, roughness = 0.5;
            const size = Math.max(1, Math.round(v.size)), fractalSum = Math.round(v.noise) === 0, seed = Math.round(v.seed) & 255;
            const out = image(a.roi.width, a.y1 - a.y0 + 1, true), ch = [0, 0, 0];
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    for (let c = 0; c < 3; ++c) {
                        let sum = 0, amp = 1, freq = 1 / period, norm = 0;
                        for (let o = 0; o < octaves; ++o) {
                            const n = pdnPerlin((x % size) * freq + c * 137.1, (y % size) * freq - c * 71.7, 1, 1, (seed + c * 31) & 255);
                            sum += (fractalSum ? n : Math.abs(n)) * amp;
                            norm += amp;
                            amp *= roughness;
                            freq *= 2;
                        }
                        sum /= Math.max(1e-9, norm);
                        ch[c] = clamp255f((fractalSum ? (sum + 1) / 2 : sum * 1.8) * 255);
                    }
                    out.px[(y - a.y0) * out.w + x - a.roi.left] = qRgb(ch[0], ch[1], ch[2]);
                }
            blendResult(a, out);
        },
    },
];

// --- Estilizado ---------------------------------------------------------------------------

function directionalWeights(angleDeg, center) {
    const r = (angleDeg * 2 * Math.PI) / 360, dr = Math.PI / 4;
    return [
        [Math.cos(r + dr), Math.cos(r + 2 * dr), Math.cos(r + 3 * dr)],
        [Math.cos(r), center, Math.cos(r + 4 * dr)],
        [Math.cos(r - dr), Math.cos(r - 2 * dr), Math.cos(r - 3 * dr)],
    ];
}

// Convolución 3x3 sobre colores directos; en los bordes se omiten las filas y columnas que
// quedan fuera, como en Paint.NET. f(suma por canal, píxel central) → color directo.
function convolve3(a, wts, f) {
    const s = straightCopy(a.src), d = a.dst.px, w = s.w, h = s.h;
    for (let y = a.y0; y <= a.y1; ++y) {
        const fy0 = y === 0 ? 1 : 0, fy1 = y === h - 1 ? 2 : 3;
        for (let x = a.roi.left; x <= a.roi.right; ++x) {
            const fx0 = x === 0 ? 1 : 0, fx1 = x === w - 1 ? 2 : 3;
            d[y * w + x] = qPremultiply(f((fn) => {
                let sum = 0;
                for (let fy = fy0; fy < fy1; ++fy)
                    for (let fx = fx0; fx < fx1; ++fx)
                        sum += wts[fy][fx] * fn(s.px[(y - 1 + fy) * w + x - 1 + fx]);
                return sum;
            }, s.px[y * w + x]));
        }
    }
}

const stylize = [
    {
        id: "edgedetect", name: "Detección de bordes", category: "stylize",
        params: [P.real("intensity", "Intensidad", 0, 1, 0.5, 2), P.real("blur", "Desenfoque", 0, 10, 0, 2),
                 P.choice("algorithm", "Algoritmo", ["Sobel", "Prewitt", "Scharr"], 0), P.bool("overlay", "Bordes superpuestos", false)],
        run(a) {
            // Detección de bordes de Paint.NET 5: magnitud del gradiente (Sobel, Prewitt o Scharr)
            // de cada canal, opcionalmente sobre un desenfoque previo, en negro o sobre la imagen.
            const strength = a.values.intensity, blur = a.values.blur, algorithm = Math.round(a.values.algorithm);
            const overlay = !!a.values.overlay;
            const margin = 2 + Math.ceil(blur * 3);
            const big = intersected(adjusted(band(a), margin), imageRect(a.src));
            let work = crop(a.src, big);
            if (blur > 0)
                work = tentBlur(work, imageRect(work), Math.max(1, lround(blur * 2)));
            const s = straightCopy(work), S = a.src.px, d = a.dst.px, w = a.src.w;
            const center = algorithm === 2 ? 10 : algorithm === 1 ? 1 : 2, side = algorithm === 2 ? 3 : 1;
            const norm = 1 / ((center + 2 * side) * 255);
            const getters = [qRed, qGreen, qBlue], mag = [0, 0, 0];
            for (let y = a.y0; y <= a.y1; ++y) {
                const ly = y - big.top;
                for (let x = a.roi.left; x <= a.roi.right; ++x) {
                    const lx = x - big.left;
                    const px = (dx, dy) => clamped(s, lx + dx, ly + dy);
                    for (let c = 0; c < 3; ++c) {
                        const ch = (p) => getters[c](p);
                        const gx = side * (ch(px(1, -1)) - ch(px(-1, -1))) + center * (ch(px(1, 0)) - ch(px(-1, 0))) + side * (ch(px(1, 1)) - ch(px(-1, 1)));
                        const gy = side * (ch(px(-1, 1)) - ch(px(-1, -1))) + center * (ch(px(0, 1)) - ch(px(0, -1))) + side * (ch(px(1, 1)) - ch(px(1, -1)));
                        mag[c] = Math.min(1, Math.sqrt(gx * gx + gy * gy) * norm * strength * 4);
                    }
                    const o = qUnpremultiply(S[y * w + x]);
                    let result;
                    if (overlay) {
                        const screen = (base, e) => clamp255(lround(255 - (255 - base) * (1 - e)));
                        result = qRgba(screen(qRed(o), mag[0]), screen(qGreen(o), mag[1]), screen(qBlue(o), mag[2]), qAlpha(o));
                    } else {
                        result = qRgba(Math.trunc(mag[0] * 255), Math.trunc(mag[1] * 255), Math.trunc(mag[2] * 255), qAlpha(o));
                    }
                    d[y * w + x] = qPremultiply(result);
                }
            }
        },
    },
    {
        id: "emboss", name: "Relieve", category: "stylize", params: [P.angle("angle", "Ángulo", -180, 180, 45)],
        run(a) {
            const wts = directionalWeights(a.values.angle, 0);
            convolve3(a, wts, (sum, c) => {
                const v = clamp255(Math.trunc(sum(intensity)) + 128);
                return qRgba(v, v, v, qAlpha(c));
            });
        },
    },
    {
        id: "outline", name: "Contorno", category: "stylize",
        params: [P.int("thickness", "Grosor", 1, 200, 1), P.int("intensity", "Intensidad", 0, 100, 50), P.int("quality", "Calidad", 1, 9, 8)],
        run(a) {
            const strength = Math.round(a.values.intensity), bits = Math.round(a.values.quality);
            localHistogram(a, Math.round(a.values.thickness), (c, h) => {
                const min1 = idiv(h.area * (100 - strength), 200), min2 = idiv(h.area * (100 + strength), 200);
                const span = (hist) => {
                    let count = 0, v1 = 0;
                    while (v1 < 255 && hist[v1] === 0)
                        ++v1;
                    while (v1 < 255 && count < min1) {
                        count += hist[v1];
                        ++v1;
                    }
                    let v2 = v1;
                    while (v2 < 255 && count < min2) {
                        count += hist[v2];
                        ++v2;
                    }
                    return [v1, v2];
                };
                const [r1, r2] = span(h.r), [g1, g2] = span(h.g), [b1, b2] = span(h.b), [, a2] = span(h.a);
                return quantizeBits(qRgba(255 - (r2 - r1), 255 - (g2 - g1), 255 - (b2 - b1), a2), bits);
            });
        },
    },
    {
        id: "relief", name: "Realce", category: "stylize", params: [P.angle("angle", "Ángulo", -180, 180, 0)],
        run(a) {
            const wts = directionalWeights(a.values.angle, 1);
            convolve3(a, wts, (sum, c) => qRgba(clamp255(Math.trunc(sum(qRed))), clamp255(Math.trunc(sum(qGreen))), clamp255(Math.trunc(sum(qBlue))), qAlpha(c)));
        },
    },
];

// --- Capa: Giro y zum (menú Capas) --------------------------------------------------------

const layer = [
    {
        id: "rotatezoom", name: "Giro y zum", category: "layer", hidden: true, shortcut: "Ctrl+Shift+Z",
        params: [P.rollBall("rotation", "Rotación y giro"), P.offset("pan", "Desplazamiento"), P.real("zoom", "Zum", 0.05, 16, 1, 2),
                 qualityParam(2), P.choice("tiling", "Modo de mosaico", ["Ninguno", "Repetir", "Reflejar"], 0),
                 P.choice("sampling", "Muestreo", ["Bilineal", "Vecino más próximo"], 0)],
        run(a) {
            // Rotación y giro: ángulo, dirección de la inclinación e inclinación (de -90 a 90°; en
            // ±90 el plano se vería de canto, así que se limita).
            const rotation = a.values.rotation || { x: 0, y: 0, z: 0 }, pan = a.values.pan || { x: 0, y: 0 };
            const tilt = clamp(rotation.z, -89.5, 89.5), zoom = a.values.zoom;
            const tiling = Math.round(a.values.tiling), nearest = Math.round(a.values.sampling) === 1;
            const r = a.roi, cx = r.left + r.width / 2, cy = r.top + r.height / 2;
            const t = new Transform().translate(cx + (pan.x * r.width) / 2, cy + (pan.y * r.height) / 2)
                .rotate(-rotation.y).rotate(tilt, "x").rotate(rotation.y).rotate(rotation.x).scale(zoom, zoom).translate(-cx, -cy);
            const inv = t.inverted();
            if (!inv)
                return;
            const aa = rgssOffsets(Math.round(a.values.quality)), src = a.src, d = a.dst.px, w = src.w;
            const samples = new Uint32Array(aa.length);
            const fold = (v, lo, size, mirror) => {
                let rel = (v - lo) % (2 * size);
                if (rel < 0)
                    rel += 2 * size;
                if (!mirror)
                    return lo + (rel % size);
                return lo + (rel < size ? rel : 2 * size - rel);
            };
            for (let y = a.y0; y <= a.y1; ++y)
                for (let x = r.left; x <= r.right; ++x) {
                    for (let p = 0; p < aa.length; ++p) {
                        let [sx, sy] = inv.map(x + 0.5 + aa[p][0], y + 0.5 + aa[p][1]);
                        const inside = sx >= r.left && sy >= r.top && sx < r.right + 1 && sy < r.bottom + 1;
                        if (!inside) {
                            if (tiling === 0) {
                                samples[p] = 0;
                                continue;
                            }
                            sx = fold(sx, r.left, r.width, tiling === 2);
                            sy = fold(sy, r.top, r.height, tiling === 2);
                        }
                        samples[p] = nearest ? clamped(src, Math.trunc(sx), Math.trunc(sy)) : sampleBilinear(src, sx, sy);
                    }
                    d[y * w + x] = average(samples, aa.length);
                }
        },
    },
];

// --- Registro y ejecución -----------------------------------------------------------------

const resolveColor = (c, info) => {
    if (c && typeof c === "object")
        return { r: c.r, g: c.g, b: c.b, a: c.a ?? 255 };
    if (c === "secondary")
        return info.secondary ? { a: 255, ...info.secondary } : { r: 255, g: 255, b: 255, a: 255 };
    if (c === "white")
        return { r: 255, g: 255, b: 255, a: 255 };
    if (c === "black")
        return { r: 0, g: 0, b: 0, a: 255 };
    return info.primary ? { a: 255, ...info.primary } : { r: 0, g: 0, b: 0, a: 255 };
};

// Ejecuta un efecto integrado como Effects::apply de Kaint: los efectos espaciales calculan en
// premultiplicado y, al volver al alfa directo, donde no cambiaron nada recuperan el píxel
// original exacto. La selección la aplica quien llama (solo 'rect' se calcula). Con 'bands'
// ([[primera, última], ...]) calcula solo esas franjas de filas y entrega cada una a onBand(y0,
// y1, píxeles de todo el ancho): así varios Web Workers se reparten el trabajo como los hilos
// de Kaint.
function runBuiltin(effect, src, values, r, info = {}, bands = null, onBand = null) {
    const w = src.width, h = src.height;
    const v = { ...values };
    for (const p of effect.params) {
        if (v[p.key] === undefined)
            v[p.key] = cloneValue(p.def);
        if (p.type === "color")
            v[p.key] = resolveColor(v[p.key], info);
    }
    const toPacked = (c, fallback) => (c ? qRgba(c.r, c.g, c.b, c.a ?? 255) : fallback);
    const ctx = { primary: toPacked(info.primary, qRgb(0, 0, 0)), secondary: toPacked(info.secondary, qRgb(255, 255, 255)) };
    const orig = new Uint32Array(new Uint8ClampedArray(src.data).buffer);
    const roi = intersected(rect(r.x, r.y, r.w, r.h), rect(0, 0, w, h));
    const list = bands || [[0, h - 1]];
    const premul = !effect.straight;
    const work = isEmpty(roi) ? orig : premul ? orig.map(qPremultiply) : orig.slice();
    const srcImg = { w, h, px: work, premul }, dstImg = { w, h, px: work.slice(), premul };
    const state = effect.prepare && !isEmpty(roi) ? effect.prepare(srcImg, roi, v, ctx) : null;
    let full = null;
    for (const [b0, b1] of list) {
        const out = orig.slice(b0 * w, (b1 + 1) * w);
        const y0 = Math.max(roi.top, b0), y1 = Math.min(roi.bottom, b1);
        if (!isEmpty(roi) && y1 >= y0) {
            effect.run({ src: srcImg, dst: dstImg, roi, values: v, ctx, state, y0, y1 });
            for (let y = y0; y <= y1; ++y)
                for (let i = y * w + roi.left, end = y * w + roi.right, o = (y - b0) * w + roi.left; i <= end; ++i, ++o)
                    if (!premul)
                        out[o] = dstImg.px[i];
                    else if (dstImg.px[i] !== work[i])
                        out[o] = qUnpremultiply(dstImg.px[i]);
        }
        if (onBand)
            onBand(b0, b1, new Uint8ClampedArray(out.buffer));
        else
            full = out;
    }
    return full ? new ImageData(new Uint8ClampedArray(full.buffer), w, h) : null;
}

function cloneValue(v) {
    return typeof v === "object" && v !== null ? JSON.parse(JSON.stringify(v)) : v;
}

export const Effects = [...adjustments, ...artistic, ...blurs, ...color, ...distort, ...noise, ...object, ...photo, ...render, ...stylize, ...layer]
    .map((e) => ({
        ...e, builtin: true,
        render(src, values, r, info) { return runBuiltin(this, src, values, r, info); },
        renderBands(src, values, r, info, bands, onBand) { runBuiltin(this, src, values, r, info, bands, onBand); },
    }));

export function findEffect(id) {
    return Effects.find((e) => e.id === id) || null;
}

// Valores por omisión (como Effects::defaults de Kaint: la semilla, al azar).
export function defaultValues(effect) {
    const v = {};
    for (const p of effect.params)
        v[p.key] = p.type === "seed" ? Math.floor(Math.random() * 256) : cloneValue(p.def);
    return v;
}

// Operaciones de píxel de 32 bits que comparten otros módulos (Resample.js).
export const PixelOps = { qRed, qGreen, qBlue, qAlpha, qRgba, qPremultiply, qUnpremultiply, clamp255f, sampleBilinear };

// ¿Está desactivado el parámetro con estos valores? (readOnlyWhen de Kaint).
export function isReadOnly(p, values) {
    if (p.readOnly)
        return true;
    if (!p.readOnlyWhen?.length)
        return false;
    const match = p.readOnlyWhen.some((c) => c.values.some((w) => (typeof w === "boolean" ? !!values[c.key] === w : Number(values[c.key]) === Number(w))));
    return match !== !!p.readOnlyInverse;
}

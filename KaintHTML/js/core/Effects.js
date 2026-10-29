// Ajustes y efectos de Kaint (src/core/effects): los mismos nombres, categorías, atajos y
// parámetros, con implementaciones en JavaScript. Cada efecto recibe la capa (ImageData, alfa
// directo), sus valores y el rectángulo que debe calcular, y devuelve un ImageData nuevo.

export const Categories = {
    artistic: "Artístico", blurs: "Desenfoques", distort: "Distorsionar", noise: "Ruido", object: "Objeto",
    photo: "Fotografía", render: "Representación", stylize: "Estilizado",
};

const P = {
    int: (key, label, min, max, def) => ({ key, label, type: "int", min, max, def }),
    real: (key, label, min, max, def, decimals = 2) => ({ key, label, type: "real", min, max, def, decimals }),
    bool: (key, label, def) => ({ key, label, type: "bool", def }),
    choice: (key, label, choices, def = 0) => ({ key, label, type: "choice", choices, def }),
    angle: (key, label, min, max, def) => ({ key, label, type: "angle", min, max, def }),
    offset: (key, label) => ({ key, label, type: "offset", def: { x: 0, y: 0 } }),
    color: (key, label, def) => ({ key, label, type: "color", def }),
    seed: () => ({ key: "seed", label: "Reiniciar", type: "seed", def: 0 }),
    curve: (key) => ({ key, label: "", type: "curve", def: [[0, 0], [255, 255]] }),
};

// --- Utilidades ---------------------------------------------------------------------------

const clamp255 = (v) => (v < 0 ? 0 : v > 255 ? 255 : v);
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

function copy(src) {
    return new ImageData(new Uint8ClampedArray(src.data), src.width, src.height);
}

// Aplica f(r, g, b, a) → [r, g, b, a?] a cada píxel del rectángulo.
function perPixel(src, rect, f) {
    const out = copy(src), d = out.data, w = src.width;
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x, i = (y * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4) {
            const r = f(d[i], d[i + 1], d[i + 2], d[i + 3], x, y);
            d[i] = r[0]; d[i + 1] = r[1]; d[i + 2] = r[2];
            if (r.length > 3) d[i + 3] = r[3];
        }
    return out;
}

// Generador pseudoaleatorio con semilla (mulberry32).
function random(seed) {
    let t = (seed * 2654435761) >>> 0 || 1;
    return () => {
        t += 0x6d2b79f5;
        let r = Math.imul(t ^ (t >>> 15), 1 | t);
        r ^= r + Math.imul(r ^ (r >>> 7), 61 | r);
        return ((r ^ (r >>> 14)) >>> 0) / 4294967296;
    };
}

// Copia premultiplicada en Float32 (para promediar sin halos en los bordes transparentes).
function premul(src) {
    const n = src.width * src.height, d = src.data, p = new Float32Array(n * 4);
    for (let i = 0; i < n * 4; i += 4) {
        const a = d[i + 3] / 255;
        p[i] = d[i] * a; p[i + 1] = d[i + 1] * a; p[i + 2] = d[i + 2] * a; p[i + 3] = d[i + 3];
    }
    return p;
}

function writePremul(out, p, rect) {
    const d = out.data, w = out.width;
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x, i = (y * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4) {
            const a = p[i + 3];
            if (a <= 0.001) {
                d[i + 3] = 0;
                continue;
            }
            const k = 255 / a;
            d[i] = clamp255(p[i] * k); d[i + 1] = clamp255(p[i + 1] * k); d[i + 2] = clamp255(p[i + 2] * k);
            d[i + 3] = clamp255(a);
        }
}

// Desenfoque de caja (suma móvil) en una dirección sobre datos premultiplicados.
function boxPass(src, dst, w, h, r, horizontal) {
    const n = horizontal ? w : h, lines = horizontal ? h : w;
    const step = horizontal ? 4 : w * 4;
    const norm = 1 / (2 * r + 1);
    for (let line = 0; line < lines; ++line) {
        const base = horizontal ? line * w * 4 : line * 4;
        const acc = [0, 0, 0, 0];
        for (let k = -r; k <= r; ++k) {
            const j = base + clamp(k, 0, n - 1) * step;
            for (let c = 0; c < 4; ++c) acc[c] += src[j + c];
        }
        for (let i = 0; i < n; ++i) {
            const o = base + i * step;
            for (let c = 0; c < 4; ++c) dst[o + c] = acc[c] * norm;
            const add = base + clamp(i + r + 1, 0, n - 1) * step;
            const sub = base + clamp(i - r, 0, n - 1) * step;
            for (let c = 0; c < 4; ++c) acc[c] += src[add + c] - src[sub + c];
        }
    }
}

// Desenfoque gaussiano aproximado con tres pasadas de caja.
function gaussPremul(p, w, h, sigma) {
    if (sigma < 0.3)
        return p;
    const boxes = [];
    const wIdeal = Math.sqrt((12 * sigma * sigma) / 3 + 1);
    let wl = Math.floor(wIdeal);
    if (wl % 2 === 0) wl--;
    const m = Math.round((12 * sigma * sigma - 3 * wl * wl - 12 * wl - 9) / (-4 * wl - 4));
    for (let i = 0; i < 3; ++i) boxes.push(((i < m ? wl : wl + 2) - 1) / 2);
    let a = p, b = new Float32Array(p.length);
    for (const r of boxes) {
        boxPass(a, b, w, h, r, true);
        boxPass(b, a === p ? (a = new Float32Array(p.length)) : a, w, h, r, false);
    }
    return a;
}

export function gaussianBlur(src, rect, radius) {
    const out = copy(src);
    writePremul(out, gaussPremul(premul(src), src.width, src.height, radius / 2), rect);
    return out;
}

// Muestra bilineal (premultiplicada) con el borde repetido.
function sampler(src) {
    const w = src.width, h = src.height, d = src.data;
    return (x, y, out) => {
        x = clamp(x, 0, w - 1); y = clamp(y, 0, h - 1);
        const x0 = Math.floor(x), y0 = Math.floor(y), x1 = Math.min(x0 + 1, w - 1), y1 = Math.min(y0 + 1, h - 1);
        const fx = x - x0, fy = y - y0;
        const wts = [(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy];
        const idx = [(y0 * w + x0) * 4, (y0 * w + x1) * 4, (y1 * w + x0) * 4, (y1 * w + x1) * 4];
        let r = 0, g = 0, b = 0, a = 0;
        for (let k = 0; k < 4; ++k) {
            const i = idx[k], wa = wts[k] * d[i + 3];
            r += d[i] * wa; g += d[i + 1] * wa; b += d[i + 2] * wa; a += wa;
        }
        if (a > 0) { out[0] = r / a; out[1] = g / a; out[2] = b / a; } else out[0] = out[1] = out[2] = 0;
        out[3] = a;
        return out;
    };
}

// Distorsión por correspondencia inversa: map(x, y) → [sx, sy] (o null: transparente).
function distort(src, rect, map) {
    const out = copy(src), d = out.data, w = src.width, sample = sampler(src), px = [0, 0, 0, 0];
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x, i = (y * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4) {
            const s = map(x + 0.5, y + 0.5);
            if (!s) { d[i + 3] = 0; continue; }
            sample(s[0] - 0.5, s[1] - 0.5, px);
            d[i] = px[0]; d[i + 1] = px[1]; d[i + 2] = px[2]; d[i + 3] = px[3];
        }
    return out;
}

// Centro de un parámetro de desplazamiento (-1..1 de la mitad del lienzo).
function center(src, offset) {
    return [src.width / 2 * (1 + (offset?.x ?? 0)), src.height / 2 * (1 + (offset?.y ?? 0))];
}

function luma(r, g, b) {
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

// Ruido de Perlin 2D (para las nubes, las abolladuras y la turbulencia).
function perlin(seed) {
    const rnd = random(seed + 1), perm = new Uint8Array(512), p = [...Array(256).keys()];
    for (let i = 255; i > 0; --i) {
        const j = Math.floor(rnd() * (i + 1));
        [p[i], p[j]] = [p[j], p[i]];
    }
    for (let i = 0; i < 512; ++i) perm[i] = p[i & 255];
    const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10);
    const grad = (h, x, y) => ((h & 1) ? -x : x) + ((h & 2) ? -y : y);
    return (x, y) => {
        const X = Math.floor(x) & 255, Y = Math.floor(y) & 255;
        x -= Math.floor(x); y -= Math.floor(y);
        const u = fade(x), v = fade(y);
        const a = perm[X] + Y, b = perm[X + 1] + Y;
        const l1 = grad(perm[a], x, y) + u * (grad(perm[b], x - 1, y) - grad(perm[a], x, y));
        const l2 = grad(perm[a + 1], x, y - 1) + u * (grad(perm[b + 1], x - 1, y - 1) - grad(perm[a + 1], x, y - 1));
        return l1 + v * (l2 - l1);
    };
}

function fbm(noise, x, y, octaves, persistence, turbulence = false) {
    let sum = 0, amp = 1, freq = 1, norm = 0;
    for (let o = 0; o < octaves; ++o) {
        const n = noise(x * freq, y * freq);
        sum += (turbulence ? Math.abs(n) : n) * amp;
        norm += amp;
        amp *= persistence;
        freq *= 2;
    }
    return sum / norm;
}

// Histogramas por canal en una ventana que se desliza por cada fila (mediana, percentil).
function percentileFilter(src, rect, radius, percentile) {
    const out = copy(src), d = src.data, o = out.data, w = src.width, h = src.height;
    const hist = [new Uint32Array(256), new Uint32Array(256), new Uint32Array(256), new Uint32Array(256)];
    const target = (count) => Math.min(count - 1, Math.floor((count * percentile) / 100));
    for (let y = rect.y; y < rect.y + rect.h; ++y) {
        hist.forEach((hh) => hh.fill(0));
        let count = 0;
        const y0 = Math.max(0, y - radius), y1 = Math.min(h - 1, y + radius);
        const column = (x, sign) => {
            if (x < 0 || x >= w) return;
            for (let yy = y0; yy <= y1; ++yy) {
                const i = (yy * w + x) * 4;
                for (let c = 0; c < 4; ++c) hist[c][d[i + c]] += sign;
                count += sign;
            }
        };
        for (let x = rect.x - radius; x <= rect.x + radius; ++x) column(x, 1);
        for (let x = rect.x; x < rect.x + rect.w; ++x) {
            const t = target(count), i = (y * w + x) * 4;
            for (let c = 0; c < 4; ++c) {
                let acc = 0, v = 0;
                const hh = hist[c];
                for (; v < 256; ++v) { acc += hh[v]; if (acc > t) break; }
                o[i + c] = v;
            }
            column(x - radius, -1);
            column(x + radius + 1, 1);
        }
    }
    return out;
}

function morphology(src, rect, radius, dilate) {
    const out = copy(src), d = src.data, o = out.data, w = src.width, h = src.height;
    // Máximo/mínimo separable (cuadrado), por filas y luego por columnas.
    const tmp = new Uint8ClampedArray(d.length);
    const pick = dilate ? Math.max : Math.min;
    for (let y = 0; y < h; ++y)
        for (let x = rect.x; x < rect.x + rect.w; ++x) {
            const i = (y * w + x) * 4;
            for (let c = 0; c < 4; ++c) {
                let v = d[i + c];
                for (let k = -radius; k <= radius; ++k) v = pick(v, d[(y * w + clamp(x + k, 0, w - 1)) * 4 + c]);
                tmp[i + c] = v;
            }
        }
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x; x < rect.x + rect.w; ++x) {
            const i = (y * w + x) * 4;
            for (let c = 0; c < 4; ++c) {
                let v = tmp[i + c];
                for (let k = -radius; k <= radius; ++k) v = pick(v, tmp[(clamp(y + k, 0, h - 1) * w + x) * 4 + c]);
                o[i + c] = v;
            }
        }
    return out;
}

// Convolución 3×3 de la luminosidad (relieve, bordes).
function sobel(src, x, y, kx, ky) {
    const w = src.width, h = src.height, d = src.data;
    let gx = 0, gy = 0;
    for (let j = -1; j <= 1; ++j)
        for (let i = -1; i <= 1; ++i) {
            const k = (clamp(y + j, 0, h - 1) * w + clamp(x + i, 0, w - 1)) * 4;
            const l = luma(d[k], d[k + 1], d[k + 2]);
            gx += l * kx[(j + 1) * 3 + i + 1];
            gy += l * ky[(j + 1) * 3 + i + 1];
        }
    return [gx, gy];
}

function curveTable(points) {
    // Interpolación monótona (Fritsch–Carlson) por los puntos de control.
    const pts = [...points].sort((a, b) => a[0] - b[0]);
    const table = new Uint8Array(256);
    const n = pts.length;
    if (n < 2) {
        for (let i = 0; i < 256; ++i) table[i] = i;
        return table;
    }
    const dx = [], dy = [], m = [];
    for (let i = 0; i < n - 1; ++i) {
        dx.push(Math.max(1e-6, pts[i + 1][0] - pts[i][0]));
        dy.push(pts[i + 1][1] - pts[i][1]);
        m.push(dy[i] / dx[i]);
    }
    const t = [m[0]];
    for (let i = 1; i < n - 1; ++i) t.push(m[i - 1] * m[i] <= 0 ? 0 : (m[i - 1] + m[i]) / 2);
    t.push(m[n - 2]);
    for (let i = 0; i < n - 1; ++i) {
        if (m[i] === 0) { t[i] = t[i + 1] = 0; continue; }
        const a = t[i] / m[i], b = t[i + 1] / m[i], s = a * a + b * b;
        if (s > 9) { const k = 3 / Math.sqrt(s); t[i] = k * a * m[i]; t[i + 1] = k * b * m[i]; }
    }
    for (let x = 0; x < 256; ++x) {
        let i = 0;
        if (x <= pts[0][0]) { table[x] = clamp255(pts[0][1]); continue; }
        if (x >= pts[n - 1][0]) { table[x] = clamp255(pts[n - 1][1]); continue; }
        while (i < n - 2 && x > pts[i + 1][0]) ++i;
        const h = dx[i], u = (x - pts[i][0]) / h;
        const h00 = 2 * u ** 3 - 3 * u ** 2 + 1, h10 = u ** 3 - 2 * u ** 2 + u, h01 = -2 * u ** 3 + 3 * u ** 2, h11 = u ** 3 - u ** 2;
        table[x] = clamp255(Math.round(h00 * pts[i][1] + h10 * h * t[i] + h01 * pts[i + 1][1] + h11 * h * t[i + 1]));
    }
    return table;
}
export { curveTable };

function rgbToHsl(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2;
    let h = 0, s = 0;
    if (max !== min) {
        const d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        h = max === r ? (g - b) / d + (g < b ? 6 : 0) : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
        h *= 60;
    }
    return [h, s, l];
}

function hslToRgb(h, s, l) {
    const f = (n) => {
        const k = (n + h / 30) % 12, a = s * Math.min(l, 1 - l);
        return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
    };
    return [f(0) * 255, f(8) * 255, f(4) * 255];
}

// --- Ajustes ---------------------------------------------------------------------------

const adjustments = [
    {
        id: "autolevel", name: "Nivel automático", shortcut: "Ctrl+Shift+L", params: [],
        render(src, v, rect) {
            const lo = [255, 255, 255], hi = [0, 0, 0], d = src.data, w = src.width;
            const hist = [new Uint32Array(256), new Uint32Array(256), new Uint32Array(256)];
            let n = 0;
            for (let y = rect.y; y < rect.y + rect.h; ++y)
                for (let x = rect.x, i = (y * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4)
                    if (d[i + 3]) { ++n; for (let c = 0; c < 3; ++c) hist[c][d[i + c]]++; }
            const cut = n * 0.005;
            for (let c = 0; c < 3; ++c) {
                let acc = 0;
                for (let k = 0; k < 256; ++k) { acc += hist[c][k]; if (acc > cut) { lo[c] = k; break; } }
                acc = 0;
                for (let k = 255; k >= 0; --k) { acc += hist[c][k]; if (acc > cut) { hi[c] = k; break; } }
            }
            const lmin = Math.min(...lo), lmax = Math.max(...hi), span = Math.max(1, lmax - lmin);
            return perPixel(src, rect, (r, g, b) => [r, g, b].map((x) => clamp255(((x - lmin) * 255) / span)));
        },
    },
    {
        id: "blackwhite", name: "Blanco y negro", shortcut: "Ctrl+Shift+G", params: [],
        render: (src, v, rect) => perPixel(src, rect, (r, g, b) => { const l = luma(r, g, b); return [l, l, l]; }),
    },
    {
        id: "brightness", name: "Brillo y contraste", shortcut: "Ctrl+Shift+T",
        params: [P.int("brightness", "Brillo", -100, 100, 0), P.int("contrast", "Contraste", -100, 100, 0)],
        render(src, v, rect) {
            const k = v.contrast >= 0 ? 100 / Math.max(1, 100 - v.contrast) : (100 + v.contrast) / 100;
            const table = new Uint8Array(256);
            for (let i = 0; i < 256; ++i) table[i] = clamp255(Math.round((i - 127.5) * k + 127.5 + v.brightness * 1.275));
            return perPixel(src, rect, (r, g, b) => [table[r], table[g], table[b]]);
        },
    },
    {
        id: "curves", name: "Curvas", shortcut: "Ctrl+Shift+M", params: [P.curve("curve")],
        render(src, v, rect) {
            const t = curveTable(v.curve);
            return perPixel(src, rect, (r, g, b) => [t[r], t[g], t[b]]);
        },
    },
    {
        id: "exposure", name: "Exposición", params: [P.real("exposure", "Exposición", -5, 5, 0)],
        render(src, v, rect) {
            const k = 2 ** v.exposure;
            return perPixel(src, rect, (r, g, b) => [r * k, g * k, b * k]);
        },
    },
    {
        id: "highlightsshadows", name: "Iluminaciones y sombras",
        params: [P.int("highlights", "Iluminaciones", -100, 100, 0), P.int("shadows", "Sombras", -100, 100, 0),
                 P.int("clarity", "Claridad", -100, 100, 0), P.real("radius", "Radio", 0.25, 20, 1.25)],
        render(src, v, rect) {
            const blurred = v.clarity ? gaussianBlur(src, rect, v.radius * 6) : null;
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const l = luma(r, g, b) / 255;
                let k = 1 + (v.shadows / 100) * (1 - l) ** 2 * 1.5 - (v.highlights / 100) * l ** 2 * 0.6;
                let out = [r * k, g * k, b * k];
                if (blurred) {
                    const i = (y * src.width + x) * 4, c = v.clarity / 100;
                    out = out.map((o, ch) => o + (o - blurred.data[i + ch]) * c);
                }
                return out;
            });
        },
    },
    {
        id: "huesat", name: "Matiz y saturación", shortcut: "Ctrl+Shift+U",
        params: [P.int("hue", "Matiz", -180, 180, 0), P.int("saturation", "Saturación", 0, 200, 100),
                 P.int("lightness", "Luminosidad", -100, 100, 0)],
        render(src, v, rect) {
            return perPixel(src, rect, (r, g, b) => {
                let [h, s, l] = rgbToHsl(r, g, b);
                h = (h + v.hue + 360) % 360;
                s = clamp(s * v.saturation / 100, 0, 1);
                l = v.lightness > 0 ? l + (1 - l) * v.lightness / 100 : l * (1 + v.lightness / 100);
                return hslToRgb(h, s, l);
            });
        },
    },
    {
        id: "colorbalance", name: "Balance de color",
        params: [P.int("cyanred", "Cian ↔ rojo", -100, 100, 0), P.int("magentagreen", "Magenta ↔ verde", -100, 100, 0),
                 P.int("yellowblue", "Amarillo ↔ azul", -100, 100, 0)],
        render(src, v, rect) {
            return perPixel(src, rect, (r, g, b) => {
                const mid = 1 - Math.abs(luma(r, g, b) / 127.5 - 1) * 0.6;
                const k = 0.8 * mid;
                return [r + v.cyanred * k, g + v.magentagreen * k, b + v.yellowblue * k];
            });
        },
    },
    {
        id: "invertalpha", name: "Invertir alfa", shortcut: "Ctrl+Alt+I", params: [],
        render: (src, v, rect) => perPixel(src, rect, (r, g, b, a) => [r, g, b, 255 - a]),
    },
    {
        id: "invert", name: "Invertir colores", shortcut: "Ctrl+Shift+I", params: [],
        render: (src, v, rect) => perPixel(src, rect, (r, g, b) => [255 - r, 255 - g, 255 - b]),
    },
    {
        id: "levels", name: "Niveles", shortcut: "Ctrl+L",
        params: [P.int("inLow", "Entrada: negro", 0, 254, 0), P.int("inHigh", "Entrada: blanco", 1, 255, 255),
                 P.real("gamma", "Gamma", 0.1, 10, 1), P.int("outLow", "Salida: negro", 0, 255, 0),
                 P.int("outHigh", "Salida: blanco", 0, 255, 255)],
        render(src, v, rect) {
            const table = new Uint8Array(256), span = Math.max(1, v.inHigh - v.inLow);
            for (let i = 0; i < 256; ++i) {
                const t = clamp((i - v.inLow) / span, 0, 1) ** (1 / v.gamma);
                table[i] = clamp255(Math.round(v.outLow + t * (v.outHigh - v.outLow)));
            }
            return perPixel(src, rect, (r, g, b) => [table[r], table[g], table[b]]);
        },
    },
    {
        id: "posterize", name: "Posterización", shortcut: "Ctrl+Shift+P",
        params: [P.int("red", "Rojo", 2, 64, 16), P.int("green", "Verde", 2, 64, 16), P.int("blue", "Azul", 2, 64, 16)],
        render(src, v, rect) {
            const level = (x, n) => Math.round(Math.round((x / 255) * (n - 1)) * (255 / (n - 1)));
            return perPixel(src, rect, (r, g, b) => [level(r, v.red), level(g, v.green), level(b, v.blue)]);
        },
    },
    {
        id: "sepia", name: "Sepia", shortcut: "Ctrl+Shift+E", params: [P.int("intensity", "Intensidad", 0, 100, 50)],
        render(src, v, rect) {
            const k = 0.5 + v.intensity / 100;
            return perPixel(src, rect, (r, g, b) => {
                const l = luma(r, g, b);
                return [l + 40 * k, l + 20 * k, l - 20 * k];
            });
        },
    },
    {
        id: "temperature", name: "Temperatura y tinte",
        params: [P.int("temperature", "Temperatura", -100, 100, 0), P.int("tint", "Tinte", -100, 100, 0)],
        render(src, v, rect) {
            const t = v.temperature * 0.6, n = v.tint * 0.5;
            return perPixel(src, rect, (r, g, b) => [r + t, g - n, b - t]);
        },
    },
];

// --- Efectos -----------------------------------------------------------------------------

const effects = [
    // Artístico
    {
        id: "inksketch", name: "Boceto a tinta", category: "artistic",
        params: [P.int("ink", "Contorno en tinta", 0, 99, 50), P.int("coloring", "Color", 0, 100, 50)],
        render(src, v, rect) {
            const blurred = gaussianBlur(src, rect, 2);
            const kx = [-1, 0, 1, -2, 0, 2, -1, 0, 1], ky = [-1, -2, -1, 0, 0, 0, 1, 2, 1];
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const [gx, gy] = sobel(blurred, x, y, kx, ky);
                const edge = Math.min(1, Math.hypot(gx, gy) / 255 * (v.ink / 25));
                const l = luma(r, g, b), c = v.coloring / 100;
                const base = [l + (r - l) * c, l + (g - l) * c, l + (b - l) * c].map((x) => x + (255 - x) * 0.35);
                return base.map((x) => x * (1 - edge));
            });
        },
    },
    {
        id: "oilpainting", name: "Pintura al óleo", category: "artistic",
        params: [P.int("brush", "Tamaño del pincel", 1, 40, 10), P.real("granularity", "Granularidad", 0, 1, 0.2)],
        render(src, v, rect) {
            const out = copy(src), d = src.data, o = out.data, w = src.width, h = src.height;
            const r = Math.max(1, Math.round(v.brush / 2)), levels = Math.max(4, Math.round(8 + (1 - v.granularity) * 56));
            const count = new Uint32Array(levels), sr = new Float32Array(levels), sg = new Float32Array(levels), sb = new Float32Array(levels);
            for (let y = rect.y; y < rect.y + rect.h; ++y) {
                count.fill(0); sr.fill(0); sg.fill(0); sb.fill(0);
                const y0 = Math.max(0, y - r), y1 = Math.min(h - 1, y + r);
                const column = (x, sign) => {
                    if (x < 0 || x >= w) return;
                    for (let yy = y0; yy <= y1; ++yy) {
                        const i = (yy * w + x) * 4, bin = Math.min(levels - 1, (luma(d[i], d[i + 1], d[i + 2]) * levels / 256) | 0);
                        count[bin] += sign; sr[bin] += sign * d[i]; sg[bin] += sign * d[i + 1]; sb[bin] += sign * d[i + 2];
                    }
                };
                for (let x = rect.x - r; x <= rect.x + r; ++x) column(x, 1);
                for (let x = rect.x; x < rect.x + rect.w; ++x) {
                    let best = 0;
                    for (let k = 1; k < levels; ++k) if (count[k] > count[best]) best = k;
                    const i = (y * w + x) * 4, c = Math.max(1, count[best]);
                    o[i] = sr[best] / c; o[i + 1] = sg[best] / c; o[i + 2] = sb[best] / c;
                    column(x - r, -1);
                    column(x + r + 1, 1);
                }
            }
            return out;
        },
    },
    {
        id: "pencilsketch", name: "Boceto a lápiz", category: "artistic",
        params: [P.real("tip", "Espesor del lápiz", 1, 20, 2), P.real("range", "Intervalo", -20, 20, 0, 1)],
        render(src, v, rect) {
            const gray = perPixel(src, rect, (r, g, b) => { const l = luma(r, g, b); return [l, l, l]; });
            const inv = perPixel(gray, rect, (r) => [255 - r, 255 - r, 255 - r]);
            const blurred = gaussianBlur(inv, rect, v.tip * 2);
            return perPixel(gray, rect, (r, g, b, a, x, y) => {
                const bl = blurred.data[(y * src.width + x) * 4];
                const dodge = bl >= 255 ? 255 : Math.min(255, (r * 255) / (255 - bl));
                const out = dodge + v.range * 3;
                return [out, out, out];
            });
        },
    },
    // Desenfoques
    {
        id: "bokeh", name: "Bokeh", category: "blurs", params: [P.real("radius", "Radio", 1, 200, 25, 1)],
        render(src, v, rect) {
            // Disco: promedio de varias pasadas de caja en direcciones distintas.
            const r = Math.max(1, Math.round(v.radius / 2));
            const w = src.width, h = src.height, p = premul(src), tmp = new Float32Array(p.length);
            boxPass(p, tmp, w, h, r, true);
            boxPass(tmp, p, w, h, r, false);
            boxPass(p, tmp, w, h, Math.round(r * 0.7), true);
            boxPass(tmp, p, w, h, Math.round(r * 0.7), false);
            const out = copy(src);
            writePremul(out, p, rect);
            return out;
        },
    },
    {
        id: "fragment", name: "Fragmento", category: "blurs",
        params: [P.int("fragments", "Cantidad de fragmentos", 2, 50, 4), P.int("distance", "Distancia", 0, 100, 8),
                 P.angle("rotation", "Rotación", 0, 360, 0)],
        render(src, v, rect) {
            const offsets = [];
            for (let k = 0; k < v.fragments; ++k) {
                const a = (v.rotation + (k * 360) / v.fragments) * Math.PI / 180;
                offsets.push([Math.cos(a) * v.distance, Math.sin(a) * v.distance]);
            }
            return averageOffsets(src, rect, offsets);
        },
    },
    {
        id: "gaussianblur", name: "Desenfoque gaussiano", category: "blurs", params: [P.real("radius", "Radio", 0, 200, 2, 1)],
        render: (src, v, rect) => gaussianBlur(src, rect, v.radius),
    },
    {
        id: "median", name: "Desenfoque por mediana", category: "blurs",
        params: [P.int("radius", "Radio", 1, 200, 10), P.int("percentile", "Percentil", 0, 100, 50)],
        render: (src, v, rect) => percentileFilter(src, rect, v.radius, v.percentile),
    },
    {
        id: "motionblur", name: "Desenfoque de movimiento", category: "blurs",
        params: [P.angle("angle", "Ángulo", -180, 180, 25), P.real("distance", "Distancia", 1, 200, 10),
                 P.bool("centered", "Centrado", true)],
        render(src, v, rect) {
            const a = (v.angle * Math.PI) / 180, n = Math.max(2, Math.round(v.distance));
            const offsets = [];
            for (let k = 0; k < n; ++k) {
                const t = v.centered ? k / (n - 1) - 0.5 : -k / (n - 1);
                offsets.push([Math.cos(a) * v.distance * t, -Math.sin(a) * v.distance * t]);
            }
            return averageOffsets(src, rect, offsets);
        },
    },
    {
        id: "radialblur", name: "Desenfoque radial", category: "blurs",
        params: [P.angle("angle", "Ángulo", 0, 360, 4), P.offset("offset", "Centrar"), P.real("quality", "Calidad", 1, 5, 2, 1)],
        render(src, v, rect) {
            const [cx, cy] = center(src, v.offset), total = (v.angle * Math.PI) / 180;
            const n = Math.max(3, Math.round(4 + v.angle * v.quality));
            return averageMap(src, rect, n, (x, y, t) => {
                const a = total * (t - 0.5), c = Math.cos(a), s = Math.sin(a), dx = x - cx, dy = y - cy;
                return [cx + dx * c - dy * s, cy + dx * s + dy * c];
            });
        },
    },
    {
        id: "squareblur", name: "Desenfoque cuadrado", category: "blurs", params: [P.real("radius", "Radio", 1, 200, 6, 1)],
        render(src, v, rect) {
            const r = Math.max(1, Math.round(v.radius)), w = src.width, h = src.height;
            const p = premul(src), tmp = new Float32Array(p.length);
            boxPass(p, tmp, w, h, r, true);
            boxPass(tmp, p, w, h, r, false);
            const out = copy(src);
            writePremul(out, p, rect);
            return out;
        },
    },
    {
        id: "surfaceblur", name: "Desenfoque de superficie", category: "blurs",
        params: [P.int("radius", "Radio", 1, 100, 6), P.int("threshold", "Umbral", 1, 100, 15)],
        render(src, v, rect) {
            // Filtro bilateral: solo se promedian los vecinos de color parecido.
            const out = copy(src), d = src.data, o = out.data, w = src.width, h = src.height;
            const r = Math.min(v.radius, 12), t = v.threshold * 2.55;
            for (let y = rect.y; y < rect.y + rect.h; ++y)
                for (let x = rect.x; x < rect.x + rect.w; ++x) {
                    const i = (y * w + x) * 4, acc = [0, 0, 0], ws = [0, 0, 0];
                    for (let j = -r; j <= r; ++j)
                        for (let k = -r; k <= r; ++k) {
                            const q = (clamp(y + j, 0, h - 1) * w + clamp(x + k, 0, w - 1)) * 4;
                            for (let c = 0; c < 3; ++c) {
                                const wt = Math.max(0, 1 - Math.abs(d[q + c] - d[i + c]) / (2.5 * t));
                                acc[c] += d[q + c] * wt; ws[c] += wt;
                            }
                        }
                    for (let c = 0; c < 3; ++c) o[i + c] = acc[c] / Math.max(1e-6, ws[c]);
                }
            return out;
        },
    },
    {
        id: "zoomblur", name: "Desenfoque de zum", category: "blurs",
        params: [P.int("amount", "Cantidad", 0, 100, 10), P.offset("offset", "Centrar")],
        render(src, v, rect) {
            const [cx, cy] = center(src, v.offset), n = Math.max(3, 2 + v.amount);
            return averageMap(src, rect, n, (x, y, t) => {
                const k = 1 - (v.amount / 100) * 0.5 * t;
                return [cx + (x - cx) * k, cy + (y - cy) * k];
            });
        },
    },
    // Distorsionar
    {
        id: "bulge", name: "Abultar", category: "distort", params: [P.real("amount", "Abultar", -1, 1, 0.45), P.offset("offset", "Centrar")],
        render(src, v, rect) {
            const [cx, cy] = center(src, v.offset), R = Math.min(src.width, src.height) / 2;
            return distort(src, rect, (x, y) => {
                const dx = x - cx, dy = y - cy, r = Math.hypot(dx, dy) / R;
                if (r >= 1) return [x, y];
                const k = 1 - v.amount * (1 - r * r) * 0.8;
                return [cx + dx * k, cy + dy * k];
            });
        },
    },
    {
        id: "crystalize", name: "Cristalizar", category: "distort", params: [P.int("cell", "Tamaño de celda", 2, 250, 8), P.seed()],
        render(src, v, rect) {
            const cell = v.cell, rnd = random(v.seed + 7), w = src.width, h = src.height, d = src.data;
            const gw = Math.ceil(w / cell) + 1, gh = Math.ceil(h / cell) + 1, sites = new Float32Array(gw * gh * 2);
            for (let j = 0; j < gh; ++j)
                for (let i = 0; i < gw; ++i) {
                    sites[(j * gw + i) * 2] = (i + rnd()) * cell;
                    sites[(j * gw + i) * 2 + 1] = (j + rnd()) * cell;
                }
            const out = copy(src), o = out.data;
            for (let y = rect.y; y < rect.y + rect.h; ++y)
                for (let x = rect.x; x < rect.x + rect.w; ++x) {
                    const gi = Math.floor(x / cell), gj = Math.floor(y / cell);
                    let best = 1e18, sx = x, sy = y;
                    for (let j = gj - 1; j <= gj + 1; ++j)
                        for (let i = gi - 1; i <= gi + 1; ++i) {
                            if (i < 0 || j < 0 || i >= gw || j >= gh) continue;
                            const px = sites[(j * gw + i) * 2], py = sites[(j * gw + i) * 2 + 1];
                            const dd = (px - x) ** 2 + (py - y) ** 2;
                            if (dd < best) { best = dd; sx = px; sy = py; }
                        }
                    const q = (clamp(Math.floor(sy), 0, h - 1) * w + clamp(Math.floor(sx), 0, w - 1)) * 4, i = (y * w + x) * 4;
                    o[i] = d[q]; o[i + 1] = d[q + 1]; o[i + 2] = d[q + 2]; o[i + 3] = d[q + 3];
                }
            return out;
        },
    },
    {
        id: "dents", name: "Abolladuras", category: "distort",
        params: [P.real("scale", "Cambiar tamaño", 1, 200, 25), P.real("refraction", "Refracción", 0, 200, 50),
                 P.real("roughness", "Detalle", 0, 100, 10), P.seed()],
        render(src, v, rect) {
            const noise = perlin(v.seed), s = v.scale, oct = 1 + Math.round(v.roughness / 25);
            return distort(src, rect, (x, y) => [
                x + fbm(noise, x / s, y / s, oct, 0.5) * v.refraction * 0.4,
                y + fbm(noise, x / s + 37.1, y / s + 11.7, oct, 0.5) * v.refraction * 0.4,
            ]);
        },
    },
    {
        id: "frostedglass", name: "Cristal congelado", category: "distort",
        params: [P.real("max", "Radio de dispersión máximo", 0, 200, 3), P.seed()],
        render(src, v, rect) {
            const rnd = random(v.seed + 3);
            return distort(src, rect, (x, y) => [x + (rnd() * 2 - 1) * v.max, y + (rnd() * 2 - 1) * v.max]);
        },
    },
    {
        id: "morphology", name: "Morfología", category: "distort",
        params: [P.int("radius", "Radio", 1, 100, 5), P.choice("mode", "Modo", ["Dilatar", "Erosionar"], 0)],
        render: (src, v, rect) => morphology(src, rect, v.radius, v.mode === 0),
    },
    {
        id: "pixelate", name: "Pixelado", category: "distort", params: [P.int("cell", "Tamaño de celda", 1, 100, 2)],
        render(src, v, rect) {
            const out = copy(src), d = src.data, o = out.data, w = src.width, h = src.height, c = v.cell;
            for (let by = Math.floor(rect.y / c) * c; by < rect.y + rect.h; by += c)
                for (let bx = Math.floor(rect.x / c) * c; bx < rect.x + rect.w; bx += c) {
                    let r = 0, g = 0, b = 0, a = 0;
                    const y1 = Math.min(h, by + c), x1 = Math.min(w, bx + c);
                    for (let y = by; y < y1; ++y)
                        for (let x = bx; x < x1; ++x) {
                            const i = (y * w + x) * 4, al = d[i + 3];
                            r += d[i] * al; g += d[i + 1] * al; b += d[i + 2] * al; a += al;
                        }
                    const n = (y1 - by) * (x1 - bx), k = a > 0 ? 1 / a : 0;
                    for (let y = by; y < y1; ++y)
                        for (let x = bx; x < x1; ++x) {
                            const i = (y * w + x) * 4;
                            o[i] = r * k; o[i + 1] = g * k; o[i + 2] = b * k; o[i + 3] = a / n;
                        }
                }
            return out;
        },
    },
    {
        id: "polarinversion", name: "Inversión polar", category: "distort",
        params: [P.real("amount", "Escala", -4, 4, 1), P.offset("offset", "Desplazamiento")],
        render(src, v, rect) {
            const [cx, cy] = center(src, v.offset), R = Math.min(src.width, src.height) / 2;
            return distort(src, rect, (x, y) => {
                const dx = (x - cx) / R, dy = (y - cy) / R, r2 = dx * dx + dy * dy;
                if (r2 < 1e-6) return null;
                const k = v.amount / r2;
                const sx = cx + dx * k * R, sy = cy + dy * k * R;
                // Se repite en mosaico (reflejado) fuera de la imagen.
                const fold = (t, n) => { t = ((t % (2 * n)) + 2 * n) % (2 * n); return t < n ? t : 2 * n - t; };
                return [fold(sx, src.width), fold(sy, src.height)];
            });
        },
    },
    {
        id: "tilereflection", name: "Reflejo en mosaico", category: "distort",
        params: [P.angle("angle", "Ángulo", -180, 180, 30), P.real("size", "Tamaño del mosaico", 1, 800, 40),
                 P.real("curvature", "Curvatura", -100, 100, 8)],
        render(src, v, rect) {
            const a = (v.angle * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a), size = v.size, k = v.curvature / 100;
            return distort(src, rect, (x, y) => {
                let u = (x * c + y * s) / size, w = (-x * s + y * c) / size;
                const fu = u - Math.floor(u) - 0.5, fw = w - Math.floor(w) - 0.5;
                u += fu * k * Math.cos(fu * Math.PI);
                w += fw * k * Math.cos(fw * Math.PI);
                const bu = u * size, bw = w * size;
                return [bu * c - bw * s, bu * s + bw * c];
            });
        },
    },
    {
        id: "twist", name: "Giro", category: "distort",
        params: [P.int("amount", "Cantidad y dirección", -200, 200, 30), P.real("size", "Tamaño", 0.01, 2, 1),
                 P.offset("offset", "Centrar")],
        render(src, v, rect) {
            const [cx, cy] = center(src, v.offset), R = (Math.min(src.width, src.height) / 2) * v.size;
            return distort(src, rect, (x, y) => {
                const dx = x - cx, dy = y - cy, r = Math.hypot(dx, dy);
                if (r >= R) return [x, y];
                const t = 1 - r / R, a = (v.amount / 20) * t * t, cs = Math.cos(a), sn = Math.sin(a);
                return [cx + dx * cs - dy * sn, cy + dx * sn + dy * cs];
            });
        },
    },
    // Ruido
    {
        id: "addnoise", name: "Agregar ruido", category: "noise",
        params: [P.int("intensity", "Intensidad", 0, 100, 64), P.int("saturation", "Saturación de colores", 0, 400, 100),
                 P.real("coverage", "Cobertura", 0, 100, 100), P.seed()],
        render(src, v, rect) {
            const rnd = random(v.seed + 11);
            const gauss = () => (rnd() + rnd() + rnd() - 1.5) * 1.6;
            return perPixel(src, rect, (r, g, b) => {
                if (rnd() * 100 >= v.coverage) return [r, g, b];
                const n = gauss() * v.intensity, sat = v.saturation / 100;
                return [r + n + gauss() * v.intensity * sat * 0.5, g + n + gauss() * v.intensity * sat * 0.5,
                        b + n + gauss() * v.intensity * sat * 0.5];
            });
        },
    },
    {
        id: "reducenoise", name: "Reducción de ruido", category: "noise",
        params: [P.int("radius", "Radio", 0, 200, 10), P.real("strength", "Intensidad", 0, 1, 0.4)],
        render(src, v, rect) {
            if (v.radius === 0) return copy(src);
            const med = percentileFilter(src, rect, Math.min(v.radius, 30), 50);
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const i = (y * src.width + x) * 4, m = med.data, k = v.strength;
                return [r + (m[i] - r) * k, g + (m[i + 1] - g) * k, b + (m[i + 2] - b) * k];
            });
        },
    },
    // Objeto
    {
        id: "dropshadow", name: "Sombra paralela", category: "object",
        params: [P.real("radius", "Radio de sombra", 0, 100, 10, 1), P.real("distance", "Distancia", 0, 100, 10, 1),
                 P.angle("angle", "Ángulo", -180, 180, -45), P.real("opacity", "Opacidad", 0, 1, 0.75),
                 P.color("color", "Color", "black"), P.bool("only", "Solo proyectar sombra", false)],
        render(src, v, rect, info) {
            const w = src.width, h = src.height, d = src.data, a = (v.angle * Math.PI) / 180;
            const ox = Math.round(Math.cos(a) * v.distance), oy = Math.round(-Math.sin(a) * v.distance);
            const alpha = new Float32Array(w * h * 4);
            for (let y = 0; y < h; ++y)
                for (let x = 0; x < w; ++x) {
                    const sx = x - ox, sy = y - oy;
                    if (sx >= 0 && sy >= 0 && sx < w && sy < h) alpha[(y * w + x) * 4 + 3] = d[(sy * w + sx) * 4 + 3];
                }
            const blurred = gaussPremul(alpha, w, h, v.radius / 2);
            const col = v.color === "black" ? { r: 0, g: 0, b: 0 } : v.color;
            return perPixel(src, rect, (r, g, b, al, x, y) => {
                const sa = (blurred[(y * w + x) * 4 + 3] * v.opacity) / 255;
                if (v.only) return [col.r, col.g, col.b, sa * 255];
                const fa = al / 255, outA = fa + sa * (1 - fa);
                if (outA <= 0) return [0, 0, 0, 0];
                const mix = (c, s) => (c * fa + s * sa * (1 - fa)) / outA;
                return [mix(r, col.r), mix(g, col.g), mix(b, col.b), outA * 255];
            });
        },
    },
    // Fotografía
    {
        id: "glow", name: "Resplandor", category: "photo",
        params: [P.real("radius", "Radio", 1, 20, 6, 1), P.int("brightness", "Brillo", -100, 100, 10),
                 P.int("contrast", "Contraste", -100, 100, 10)],
        render(src, v, rect) {
            const blurred = gaussianBlur(src, rect, v.radius * 2);
            const k = 1 + v.contrast / 100, bb = v.brightness * 1.27;
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const i = (y * src.width + x) * 4, bl = blurred.data;
                const glow = [bl[i], bl[i + 1], bl[i + 2]].map((c) => clamp255((c - 128) * k + 128 + bb));
                return [r, g, b].map((c, ch) => c + glow[ch] - (c * glow[ch]) / 255);
            });
        },
    },
    {
        id: "redeye", name: "Eliminar ojos rojos", category: "photo", params: [P.int("force", "Fuerza", 1, 5, 3)],
        render(src, v, rect) {
            return perPixel(src, rect, (r, g, b) => {
                const other = (g + b) / 2;
                if (r < other * 1.4 || r < 60) return [r, g, b];
                const t = Math.min(1, (v.force / 5) * 1.2);
                return [r + (other * 0.9 - r) * t, g, b];
            });
        },
    },
    {
        id: "sharpen", name: "Nítido", category: "photo", params: [P.real("amount", "Cantidad", 1, 20, 2)],
        render(src, v, rect) {
            const blurred = gaussianBlur(src, rect, v.amount);
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const i = (y * src.width + x) * 4, bl = blurred.data;
                return [r + (r - bl[i]), g + (g - bl[i + 1]), b + (b - bl[i + 2])];
            });
        },
    },
    {
        id: "softenportrait", name: "Suavizado de retrato", category: "photo",
        params: [P.real("softness", "Suavizado", 0, 10, 5, 1), P.int("lighting", "Iluminación", -20, 20, 0),
                 P.int("warmth", "Temperatura", 0, 20, 10)],
        render(src, v, rect) {
            const blurred = gaussianBlur(src, rect, v.softness * 2);
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const i = (y * src.width + x) * 4, bl = blurred.data, k = 0.6;
                const light = v.lighting * 2;
                return [r + (bl[i] - r) * k + light + v.warmth, g + (bl[i + 1] - g) * k + light + v.warmth * 0.4,
                        b + (bl[i + 2] - b) * k + light - v.warmth * 0.5];
            });
        },
    },
    {
        id: "vignette", name: "Viñeta", category: "photo",
        params: [P.offset("offset", "Centrar"), P.real("radius", "Radio", 0.1, 4, 0.5), P.real("amount", "Intensidad", 0, 1, 1)],
        render(src, v, rect) {
            const [cx, cy] = center(src, v.offset), R = Math.hypot(src.width, src.height) / 2 * v.radius * 1.5;
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const t = Math.min(1, Math.hypot(x - cx, y - cy) / R), k = 1 - v.amount * t * t * (3 - 2 * t);
                return [r * k, g * k, b * k];
            });
        },
    },
    // Representación
    {
        id: "clouds", name: "Nubes", category: "render",
        params: [P.int("scale", "Escala", 2, 1000, 250), P.real("power", "Aspereza", 0, 1, 0.5), P.seed()],
        render(src, v, rect, info) {
            const noise = perlin(v.seed), c1 = info.primary, c2 = info.secondary;
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const t = clamp(fbm(noise, x / v.scale, y / v.scale, 8, v.power) * 0.9 + 0.5, 0, 1);
                return [c1.r + (c2.r - c1.r) * t, c1.g + (c2.g - c1.g) * t, c1.b + (c2.b - c1.b) * t, 255];
            });
        },
    },
    {
        id: "julia", name: "Fractal Julia", category: "render",
        params: [P.real("factor", "Factor", 1, 10, 4), P.real("zoom", "Zum", 0.1, 50, 1), P.angle("angle", "Ángulo", -180, 180, 0)],
        render(src, v, rect) {
            const w = src.width, h = src.height, a = (v.angle * Math.PI) / 180, cs = Math.cos(a), sn = Math.sin(a);
            return perPixel(src, rect, (r, g, b, al, x, y) => {
                let zx = ((x - w / 2) / Math.min(w, h)) * 3 / v.zoom, zy = ((y - h / 2) / Math.min(w, h)) * 3 / v.zoom;
                [zx, zy] = [zx * cs - zy * sn, zx * sn + zy * cs];
                let i = 0;
                for (; i < 64 && zx * zx + zy * zy < v.factor * v.factor; ++i)
                    [zx, zy] = [zx * zx - zy * zy - 0.70176, 2 * zx * zy - 0.3842];
                const t = i / 64;
                return [255 * t ** 0.5, 255 * t ** 1.5, 255 * Math.sin(t * Math.PI), i < 64 ? 255 : 0];
            });
        },
    },
    {
        id: "mandelbrot", name: "Fractal Mandelbrot", category: "render",
        params: [P.real("factor", "Factor", 1, 10, 1), P.real("zoom", "Zum", 0, 100, 10), P.angle("angle", "Ángulo", -180, 180, 0),
                 P.bool("invert", "Invertir colores", false)],
        render(src, v, rect) {
            const w = src.width, h = src.height, a = (v.angle * Math.PI) / 180, cs = Math.cos(a), sn = Math.sin(a);
            const scale = 3 / Math.min(w, h) / Math.max(0.1, v.zoom / 10);
            return perPixel(src, rect, (r, g, b, al, x, y) => {
                let px = (x - w / 2) * scale, py = (y - h / 2) * scale;
                [px, py] = [px * cs - py * sn - 0.5, px * sn + py * cs];
                let zx = 0, zy = 0, i = 0;
                for (; i < 128 && zx * zx + zy * zy < 4 * v.factor; ++i)
                    [zx, zy] = [zx * zx - zy * zy + px, 2 * zx * zy + py];
                const t = i / 128;
                let c = [255 * (1 - t) ** 3, 255 * Math.sin(t * Math.PI) ** 2, 255 * t ** 0.4];
                if (v.invert) c = c.map((q) => 255 - q);
                return [...c, i < 128 ? 255 : 0];
            });
        },
    },
    {
        id: "turbulence", name: "Turbulencia", category: "render",
        params: [P.int("octaves", "Octavas", 1, 12, 4), P.real("period", "Período", 2, 1000, 100), P.seed()],
        render(src, v, rect) {
            const noise = perlin(v.seed + 5);
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const t = clamp(fbm(noise, x / v.period, y / v.period, v.octaves, 0.5, true) * 1.6, 0, 1) * 255;
                return [t, t, t, 255];
            });
        },
    },
    // Estilizado
    {
        id: "edgedetect", name: "Detección de bordes", category: "stylize",
        params: [P.real("intensity", "Intensidad", 0, 1, 0.5), P.bool("overlay", "Bordes superpuestos", false)],
        render(src, v, rect) {
            const kx = [-1, 0, 1, -2, 0, 2, -1, 0, 1], ky = [-1, -2, -1, 0, 0, 0, 1, 2, 1];
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const [gx, gy] = sobel(src, x, y, kx, ky);
                const e = Math.min(255, Math.hypot(gx, gy) * v.intensity * 2);
                return v.overlay ? [r - e, g - e, b - e] : [255 - e, 255 - e, 255 - e];
            });
        },
    },
    {
        id: "emboss", name: "Relieve", category: "stylize", params: [P.angle("angle", "Ángulo", -180, 180, 45)],
        render(src, v, rect) {
            const a = (v.angle * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
            const kx = [-1, 0, 1, -2, 0, 2, -1, 0, 1], ky = [-1, -2, -1, 0, 0, 0, 1, 2, 1];
            return perPixel(src, rect, (r, g, b, al, x, y) => {
                const [gx, gy] = sobel(src, x, y, kx, ky);
                const e = clamp255(128 + (gx * c - gy * s) * 0.5);
                return [e, e, e];
            });
        },
    },
    {
        id: "outline", name: "Contorno", category: "stylize",
        params: [P.int("thickness", "Grosor", 1, 200, 1), P.int("intensity", "Intensidad", 0, 100, 50)],
        render(src, v, rect) {
            const dark = morphology(src, rect, Math.min(v.thickness, 50), false), k = v.intensity / 100;
            return perPixel(src, rect, (r, g, b, a, x, y) => {
                const i = (y * src.width + x) * 4, m = dark.data;
                return [r + (m[i] - r) * k, g + (m[i + 1] - g) * k, b + (m[i + 2] - b) * k];
            });
        },
    },
    {
        id: "relief", name: "Realce", category: "stylize", params: [P.angle("angle", "Ángulo", -180, 180, 0)],
        render(src, v, rect) {
            const a = (v.angle * Math.PI) / 180, c = Math.cos(a), s = Math.sin(a);
            const kx = [-1, 0, 1, -2, 0, 2, -1, 0, 1], ky = [-1, -2, -1, 0, 0, 0, 1, 2, 1];
            return perPixel(src, rect, (r, g, b, al, x, y) => {
                const [gx, gy] = sobel(src, x, y, kx, ky);
                const e = (gx * c - gy * s) * 0.25;
                return [r + e, g + e, b + e];
            });
        },
    },
];

// Promedio de la imagen desplazada (fragmento, movimiento).
function averageOffsets(src, rect, offsets) {
    const out = copy(src), o = out.data, w = src.width, sample = sampler(src), px = [0, 0, 0, 0];
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x; x < rect.x + rect.w; ++x) {
            let r = 0, g = 0, b = 0, a = 0;
            for (const [dx, dy] of offsets) {
                sample(x + dx, y + dy, px);
                r += px[0] * px[3]; g += px[1] * px[3]; b += px[2] * px[3]; a += px[3];
            }
            const i = (y * w + x) * 4, k = a > 0 ? 1 / a : 0;
            o[i] = r * k; o[i + 1] = g * k; o[i + 2] = b * k; o[i + 3] = a / offsets.length;
        }
    return out;
}

// Promedio a lo largo de una trayectoria t ∈ [0, 1] (radial, zum).
function averageMap(src, rect, n, map) {
    const out = copy(src), o = out.data, w = src.width, sample = sampler(src), px = [0, 0, 0, 0];
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x; x < rect.x + rect.w; ++x) {
            let r = 0, g = 0, b = 0, a = 0;
            for (let k = 0; k < n; ++k) {
                const [sx, sy] = map(x, y, k / (n - 1));
                sample(sx, sy, px);
                r += px[0] * px[3]; g += px[1] * px[3]; b += px[2] * px[3]; a += px[3];
            }
            const i = (y * w + x) * 4, k = a > 0 ? 1 / a : 0;
            o[i] = r * k; o[i + 1] = g * k; o[i + 2] = b * k; o[i + 3] = a / n;
        }
    return out;
}

export const Effects = [...adjustments, ...effects];

export function findEffect(id) {
    return Effects.find((e) => e.id === id) || null;
}

export function defaultValues(effect) {
    const v = {};
    for (const p of effect.params)
        v[p.key] = typeof p.def === "object" && p.def !== null ? JSON.parse(JSON.stringify(p.def)) : p.def;
    return v;
}

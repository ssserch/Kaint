// Cambio de tamaño de las capas (src/core/Resample.cpp de Kaint): los mismos métodos de remuestreo
// (bicúbico, bicúbico suave, bilineal, bilineal de baja calidad, adaptativo, Lanczos,
// supermuestreo y vecino más cercano) y la corrección gamma opcional (en luz lineal).
import { PixelOps } from "./Effects.js";

const { qRed, qGreen, qBlue, qAlpha, qRgba, qPremultiply, qUnpremultiply, clamp255f, sampleBilinear } = PixelOps;

// [id, nombre] en el orden del desplegable de Kaint.
export const ResampleMethods = [
    ["bicubic", "Bicúbico"], ["bicubicSmooth", "Bicúbico (suave)"], ["bilinear", "Bilineal"], ["bilinearLow", "Bilineal (baja calidad)"],
    ["adaptive", "Adaptativo (nítido)"], ["lanczos", "Lanczos"], ["supersampling", "Supermuestreo (Fant)"], ["nearest", "Vecino más cercano"],
];

// --- Núcleos ------------------------------------------------------------------------------

function mitchell(x, b, c) {
    x = Math.abs(x);
    if (x < 1)
        return ((12 - 9 * b - 6 * c) * x * x * x + (-18 + 12 * b + 6 * c) * x * x + (6 - 2 * b)) / 6;
    if (x < 2)
        return ((-b - 6 * c) * x * x * x + (6 * b + 30 * c) * x * x + (-12 * b - 48 * c) * x + (8 * b + 24 * c)) / 6;
    return 0;
}

const sinc = (x) => (x === 0 ? 1 : Math.sin(x * Math.PI) / (x * Math.PI));
const kTriangle = (x) => (Math.abs(x) < 1 ? 1 - Math.abs(x) : 0);
const kCatmullRom = (x) => mitchell(x, 0, 0.5);
const kBSpline = (x) => mitchell(x, 1, 0);
const kLanczos3 = (x) => (Math.abs(x) < 3 ? sinc(x) * sinc(x / 3) : 0);

// Pesos de cada píxel de destino: { first, weights }.
function kernelWeights(srcN, dstN, kernel, support, prefilter) {
    const out = [];
    const f = srcN / dstN, scale = prefilter ? Math.max(1, f) : 1, s = support * scale;
    for (let i = 0; i < dstN; ++i) {
        const center = (i + 0.5) * f - 0.5;
        const lo = Math.ceil(center - s), hi = Math.floor(center + s);
        const first = Math.min(srcN - 1, Math.max(0, lo)), last = Math.min(srcN - 1, Math.max(0, hi));
        const w = new Float32Array(last - first + 1);
        let sum = 0;
        for (let j = lo; j <= hi; ++j) {
            const v = kernel((j - center) / scale);
            w[Math.min(last, Math.max(first, j)) - first] += v;
            sum += v;
        }
        if (Math.abs(sum) > 1e-9)
            for (let k = 0; k < w.length; ++k)
                w[k] /= sum;
        out.push({ first, weights: w });
    }
    return out;
}

// Media exacta del área cubierta (Fant); al ampliar equivale a bilineal.
function areaWeights(srcN, dstN) {
    if (dstN >= srcN)
        return kernelWeights(srcN, dstN, kTriangle, 1, false);
    const out = [], f = srcN / dstN;
    for (let i = 0; i < dstN; ++i) {
        const a = i * f, b = (i + 1) * f;
        const first = Math.floor(a), last = Math.min(srcN - 1, Math.ceil(b) - 1);
        const w = [];
        for (let j = first; j <= last; ++j)
            w.push((Math.min(b, j + 1) - Math.max(a, j)) / f);
        out.push({ first, weights: Float32Array.from(w) });
    }
    return out;
}

// --- Gamma --------------------------------------------------------------------------------

const ONE = 16320;
let linearLut = null, srgbLut = null;
function luts() {
    if (!linearLut) {
        linearLut = new Int32Array(256);
        for (let i = 0; i < 256; ++i) {
            const c = i / 255;
            linearLut[i] = Math.round((c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)) * ONE);
        }
        srgbLut = new Uint8Array(ONE + 1);
        for (let i = 0; i <= ONE; ++i) {
            const lin = i / ONE;
            srgbLut[i] = clamp255f((lin <= 0.0031308 ? lin * 12.92 : 1.055 * Math.pow(lin, 1 / 2.4) - 0.055) * 255);
        }
    }
}

// Dos pasadas (horizontal a un búfer de 16 bits y vertical) sobre píxeles premultiplicados.
function separable(src, sw, sh, dw, dh, wx, wy, gamma) {
    luts();
    const mid = new Int16Array(dw * sh * 4), row = new Int32Array(sw * 4);
    for (let y = 0; y < sh; ++y) {
        for (let x = 0; x < sw; ++x) {
            const p = src[y * sw + x], a = qAlpha(p), c = x * 4;
            row[c + 3] = a * 64;
            if (!gamma) {
                row[c] = qRed(p) * 64; row[c + 1] = qGreen(p) * 64; row[c + 2] = qBlue(p) * 64;
            } else if (a === 0) {
                row[c] = row[c + 1] = row[c + 2] = 0;
            } else {
                const u = qUnpremultiply(p);
                row[c] = Math.trunc((linearLut[qRed(u)] * a) / 255);
                row[c + 1] = Math.trunc((linearLut[qGreen(u)] * a) / 255);
                row[c + 2] = Math.trunc((linearLut[qBlue(u)] * a) / 255);
            }
        }
        const base = y * dw * 4;
        for (let x = 0; x < dw; ++x) {
            const k = wx[x];
            let a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            for (let j = 0; j < k.weights.length; ++j) {
                const c = (k.first + j) * 4, w = k.weights[j];
                a0 += row[c] * w; a1 += row[c + 1] * w; a2 += row[c + 2] * w; a3 += row[c + 3] * w;
            }
            const o = base + x * 4, cl = (v) => Math.trunc(Math.min(32000, Math.max(-32000, v)));
            mid[o] = cl(a0); mid[o + 1] = cl(a1); mid[o + 2] = cl(a2); mid[o + 3] = cl(a3);
        }
    }
    const out = new Uint32Array(dw * dh), acc = new Float32Array(dw * 4);
    for (let y = 0; y < dh; ++y) {
        acc.fill(0);
        const k = wy[y];
        for (let j = 0; j < k.weights.length; ++j) {
            const m = (k.first + j) * dw * 4, w = k.weights[j];
            for (let i = 0; i < dw * 4; ++i)
                acc[i] += mid[m + i] * w;
        }
        for (let x = 0; x < dw; ++x) {
            const c = x * 4, a = clamp255f(acc[c + 3] / 64);
            if (a === 0)
                continue;
            if (!gamma) {
                out[y * dw + x] = qRgba(Math.min(a, clamp255f(acc[c] / 64)), Math.min(a, clamp255f(acc[c + 1] / 64)), Math.min(a, clamp255f(acc[c + 2] / 64)), a);
            } else {
                const ch = (v) => srgbLut[Math.min(ONE, Math.max(0, Math.trunc((v * 255) / a + 0.5)))];
                out[y * dw + x] = qPremultiply(qRgba(ch(acc[c]), ch(acc[c + 1]), ch(acc[c + 2]), a));
            }
        }
    }
    return out;
}

// Escala un ImageData (alfa directo) a w × h con el método elegido; devuelve otro ImageData.
export function scaleImage(image, w, h, method = "adaptive", gammaCorrect = false) {
    const sw = image.width, sh = image.height;
    const straight = new Uint32Array(new Uint8ClampedArray(image.data).buffer);
    const finish = (px, premul) => {
        const out = premul ? px.map(qUnpremultiply) : px;
        return new ImageData(new Uint8ClampedArray(out.buffer), w, h);
    };
    if (w === sw && h === sh)
        return finish(straight, false);
    if (method === "nearest") {
        const out = new Uint32Array(w * h), fx = sw / w, fy = sh / h;
        for (let y = 0; y < h; ++y) {
            const sy = Math.min(sh - 1, Math.trunc((y + 0.5) * fy));
            for (let x = 0; x < w; ++x)
                out[y * w + x] = straight[sy * sw + Math.min(sw - 1, Math.trunc((x + 0.5) * fx))];
        }
        return finish(out, false);
    }
    const src = straight.map(qPremultiply);
    if (method === "adaptive")
        method = Math.max(sw / w, sh / h) >= 2 ? "supersampling" : "bicubic";
    if (method === "bilinearLow") {
        const img = { w: sw, h: sh, px: src }, out = new Uint32Array(w * h), fx = Math.fround(sw / w), fy = Math.fround(sh / h);
        for (let y = 0; y < h; ++y)
            for (let x = 0; x < w; ++x)
                out[y * w + x] = sampleBilinear(img, Math.fround((x + 0.5) * fx), Math.fround((y + 0.5) * fy));
        return finish(out, true);
    }
    if (method === "supersampling")
        return finish(separable(src, sw, sh, w, h, areaWeights(sw, w), areaWeights(sh, h), gammaCorrect), true);
    let kernel = kCatmullRom, support = 2;
    if (method === "bilinear") {
        kernel = kTriangle;
        support = 1;
    } else if (method === "bicubicSmooth") {
        kernel = kBSpline;
    } else if (method === "lanczos") {
        kernel = kLanczos3;
        support = 3;
    }
    return finish(separable(src, sw, sh, w, h, kernelWeights(sw, w, kernel, support, true), kernelWeights(sh, h, kernel, support, true), gammaCorrect), true);
}

// --- Transformaciones (Mover píxeles seleccionados) ---------------------------------------

// Al reducir, antes se promedia por áreas (si no, sale pixelado), como Resample::prefiltered de
// Kaint: si la matriz (DOMMatrix, de los píxeles de 'image' al destino) encoge un eje por debajo
// de 0,85, devuelve la imagen reducida y la matriz ajustada. 'cache' (un objeto) guarda la última.
export function prefiltered(image, matrix, gammaCorrect, cache = {}) {
    const sx = Math.hypot(matrix.a, matrix.b), sy = Math.hypot(matrix.c, matrix.d);
    if (sx >= 0.85 && sy >= 0.85)
        return { image, matrix };
    const w = Math.max(1, Math.round(image.width * Math.min(1, sx))), h = Math.max(1, Math.round(image.height * Math.min(1, sy)));
    if (cache.source !== image || cache.w !== w || cache.h !== h || cache.gamma !== gammaCorrect)
        Object.assign(cache, { source: image, w, h, gamma: gammaCorrect, image: scaleImage(image, w, h, "supersampling", gammaCorrect) });
    return { image: cache.image, matrix: matrix.multiply(new DOMMatrix().scale(image.width / w, image.height / h)) };
}

// Píxeles en luz lineal (premultiplicados, de 0 a 1), para muestrear con la corrección gamma
// (LinearImage de Kaint).
function linearImage(px, w, h) {
    luts();
    const out = new Float32Array(w * h * 4);
    for (let i = 0; i < w * h; ++i) {
        const u = qUnpremultiply(px[i]), a = qAlpha(u) / 255, k = i * 4;
        out[k] = (linearLut[qRed(u)] / ONE) * a;
        out[k + 1] = (linearLut[qGreen(u)] / ONE) * a;
        out[k + 2] = (linearLut[qBlue(u)] / ONE) * a;
        out[k + 3] = a;
    }
    return out;
}

// De vuelta a sRGB premultiplicado de 8 bits.
function linearToPremul(c) {
    const a = Math.min(1, Math.max(0, c[3]));
    if (a <= 0.5 / 255)
        return 0;
    const ch = (v) => srgbLut[Math.round(Math.min(1, Math.max(0, v / a)) * ONE)];
    return qPremultiply(qRgba(ch(c[0]), ch(c[1]), ch(c[2]), Math.round(a * 255)));
}

// Dibuja 'source' (ImageData, alfa directo) transformada por 'matrix' (DOMMatrix: de sus píxeles
// a los de 'dst') sobre 'dst' (ImageData transparente, alfa directo), con el muestreo bicúbico o el
// bilineal con la corrección gamma (Resample::drawTransformed de Kaint).
export function drawTransformed(dst, source, matrix, interpolation, gammaCorrect) {
    const inv = matrix.inverse();
    if (!Number.isFinite(inv.a) || !Number.isFinite(inv.d))
        return;
    const sw = source.width, sh = source.height, dw = dst.width, dh = dst.height;
    const corners = [[0, 0], [sw, 0], [0, sh], [sw, sh]].map(([x, y]) => [matrix.a * x + matrix.c * y + matrix.e, matrix.b * x + matrix.d * y + matrix.f]);
    const x0 = Math.max(0, Math.floor(Math.min(...corners.map((p) => p[0]))) - 2), x1 = Math.min(dw - 1, Math.ceil(Math.max(...corners.map((p) => p[0]))) + 2);
    const y0 = Math.max(0, Math.floor(Math.min(...corners.map((p) => p[1]))) - 2), y1 = Math.min(dh - 1, Math.ceil(Math.max(...corners.map((p) => p[1]))) + 2);
    const px = new Uint32Array(new Uint8ClampedArray(source.data).buffer).map(qPremultiply);
    const out = new Uint32Array(dst.data.buffer, dst.data.byteOffset, dw * dh);
    const weights = (t) => [kCatmullRom(t + 1), kCatmullRom(t), kCatmullRom(t - 1), kCatmullRom(t - 2)];
    if (gammaCorrect && interpolation !== "nearest") {
        const lin = linearImage(px, sw, sh), zero = [0, 0, 0, 0];
        const at = (x, y) => (x < 0 || y < 0 || x >= sw || y >= sh ? null : (y * sw + x) * 4);
        const c = new Float32Array(4);
        for (let y = y0; y <= y1; ++y)
            for (let x = x0; x <= x1; ++x) {
                const X = x + 0.5, Y = y + 0.5;
                let sx = inv.a * X + inv.c * Y + inv.e - 0.5, sy = inv.b * X + inv.d * Y + inv.f - 0.5;
                const ix = Math.floor(sx), iy = Math.floor(sy);
                c.fill(0);
                if (interpolation === "bilinear") {
                    const tx = Math.fround(sx - ix), ty = Math.fround(sy - iy);
                    const p = [at(ix, iy), at(ix + 1, iy), at(ix, iy + 1), at(ix + 1, iy + 1)];
                    for (let k = 0; k < 4; ++k) {
                        const v = (q) => (q === null ? zero[k] : lin[q + k]);
                        c[k] = (v(p[0]) * (1 - tx) + v(p[1]) * tx) * (1 - ty) + (v(p[2]) * (1 - tx) + v(p[3]) * tx) * ty;
                    }
                } else {
                    const wx = weights(sx - ix), wy = weights(sy - iy);
                    for (let j = 0; j < 4; ++j)
                        for (let i = 0; i < 4; ++i) {
                            const q = at(ix + i - 1, iy + j - 1);
                            if (q === null)
                                continue;
                            const wgt = Math.fround(wx[i] * wy[j]);
                            for (let k = 0; k < 4; ++k)
                                c[k] += lin[q + k] * wgt;
                        }
                    c[3] = Math.min(1, Math.max(0, c[3]));
                    for (let k = 0; k < 3; ++k)
                        c[k] = Math.min(c[3], Math.max(0, c[k]));
                }
                const s = linearToPremul(c);
                if (qAlpha(s))
                    out[y * dw + x] = qUnpremultiply(s);
            }
        return;
    }
    const pixel = (x, y) => (x < 0 || y < 0 || x >= sw || y >= sh ? 0 : px[y * sw + x]);
    for (let y = y0; y <= y1; ++y)
        for (let x = x0; x <= x1; ++x) {
            const X = x + 0.5, Y = y + 0.5;
            const sx = inv.a * X + inv.c * Y + inv.e, sy = inv.b * X + inv.d * Y + inv.f;
            let s;
            if (interpolation === "nearest") {
                s = pixel(Math.floor(sx), Math.floor(sy));
            } else if (interpolation === "bilinear") {
                const fx = sx - 0.5, fy = sy - 0.5, ix = Math.floor(fx), iy = Math.floor(fy);
                const tx = Math.trunc((fx - ix) * 256), ty = Math.trunc((fy - iy) * 256);
                const p00 = pixel(ix, iy), p10 = pixel(ix + 1, iy), p01 = pixel(ix, iy + 1), p11 = pixel(ix + 1, iy + 1);
                const mix = (f) => {
                    const top = f(p00) * (256 - tx) + f(p10) * tx, bottom = f(p01) * (256 - tx) + f(p11) * tx;
                    return (top * (256 - ty) + bottom * ty + 32768) >> 16;
                };
                s = qRgba(mix(qRed), mix(qGreen), mix(qBlue), mix(qAlpha));
            } else {
                const fx = sx - 0.5, fy = sy - 0.5, ix = Math.floor(fx), iy = Math.floor(fy);
                const wx = weights(fx - ix), wy = weights(fy - iy), acc = [0, 0, 0, 0];
                for (let j = 0; j < 4; ++j)
                    for (let i = 0; i < 4; ++i) {
                        const p = pixel(ix + i - 1, iy + j - 1), w = wx[i] * wy[j];
                        acc[0] += qRed(p) * w; acc[1] += qGreen(p) * w; acc[2] += qBlue(p) * w; acc[3] += qAlpha(p) * w;
                    }
                const a = clamp255f(Math.fround(acc[3]));
                s = qRgba(Math.min(a, clamp255f(Math.fround(acc[0]))), Math.min(a, clamp255f(Math.fround(acc[1]))),
                          Math.min(a, clamp255f(Math.fround(acc[2]))), a);
            }
            if (qAlpha(s))
                out[y * dw + x] = qUnpremultiply(s);
        }
}

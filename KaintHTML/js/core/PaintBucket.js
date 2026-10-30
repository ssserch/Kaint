// Cubo de pintura de Kaint (src/core/FloodFill.cpp, paintBucket): la zona parecida al píxel
// pulsado, con el borde suavizado de Kaint (la fila del borde y hasta dos píxeles por fuera, con
// la cobertura estimada por el color), la "desmezcla" del color de la zona y la opción de acumular
// los rellenos.
import { PixelOps } from "./Effects.js";
import { patternBit } from "./FillPattern.js";
import { floodMask } from "./FloodFill.js";
import { blendPixels, context, createCanvas, lerpStraight, scaleAlpha } from "./Raster.js";
import { smoothContour } from "./Selection.js";

const { qRed, qGreen, qBlue, qAlpha, qRgba, qPremultiply, qUnpremultiply } = PixelOps;

// Distancia entre dos colores premultiplicados (también cuenta el alfa).
function colorDistance(a, b) {
    const dr = qRed(a) - qRed(b), dg = qGreen(a) - qGreen(b), db = qBlue(a) - qBlue(b), da = qAlpha(a) - qAlpha(b);
    return Math.sqrt(dr * dr + dg * dg + db * db + da * da);
}

const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));

function maskBounds(mask, w, h) {
    let x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (let y = 0; y < h; ++y)
        for (let x = 0, i = y * w; x < w; ++x, ++i)
            if (mask[i]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                y1 = y;
            }
    return x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
}

// Borde suavizado de 'mask' en 'sample' (Uint32, alfa directo): [{ x, y, coverage, interior }],
// con 'interior' premultiplicado (FloodFill::antialiasEdge).
export function antialiasEdge(sample, mask, w, h) {
    const out = [];
    const inMask = (x, y) => x >= 0 && y >= 0 && x < w && y < h && mask[y * w + x] !== 0;
    const color = (x, y) => qPremultiply(sample[y * w + x]);
    // Anillo 0: píxeles de la máscara junto al exterior; anillos 1 y 2: por fuera.
    const ring = new Uint8Array(w * h).fill(0xff);
    const interiorOf = new Uint32Array(w * h), coverageOf = new Int32Array(w * h);

    // Color del contorno: el más distinto de la zona en un entorno de 5x5 (fuera de la máscara).
    const outline = (x, y, interior) => {
        let best = -1, o = interior;
        for (let dy = -2; dy <= 2; ++dy)
            for (let dx = -2; dx <= 2; ++dx) {
                const xx = x + dx, yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= w || yy >= h || inMask(xx, yy))
                    continue;
                const c = color(xx, yy), d = colorDistance(c, interior);
                if (d > best) {
                    best = d;
                    o = c;
                }
            }
        return o;
    };
    const estimate = (x, y, interior) => {
        const p = color(x, y);
        const dOutline = colorDistance(outline(x, y, interior), interior);
        if (dOutline < 1)
            return 255;
        const t = 1 - colorDistance(p, interior) / dOutline;
        return Math.min(255, Math.max(0, Math.trunc(t * 255 + 0.5)));
    };

    // Anillo 0: el color de la zona es la media de los vecinos de la máscara.
    const mb = maskBounds(mask, w, h);
    if (!mb)
        return out;
    let previous = [];
    for (let y = mb.y; y < mb.y + mb.h; ++y)
        for (let x = mb.x; x < mb.x + mb.w; ++x) {
            if (!mask[y * w + x])
                continue;
            let border = false;
            for (let dy = -1; dy <= 1 && !border; ++dy)
                for (let dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h && !inMask(x + dx, y + dy)) {
                        border = true;
                        break;
                    }
            if (!border)
                continue;
            let r = 0, g = 0, b = 0, a = 0, n = 0;
            for (let dy = -1; dy <= 1; ++dy)
                for (let dx = -1; dx <= 1; ++dx)
                    if (inMask(x + dx, y + dy)) {
                        const c = color(x + dx, y + dy);
                        r += qRed(c); g += qGreen(c); b += qBlue(c); a += qAlpha(c);
                        ++n;
                    }
            const average = qRgba(Math.trunc(r / n), Math.trunc(g / n), Math.trunc(b / n), Math.trunc(a / n));
            const edgeColor = outline(x, y, average);
            let interior = average, farthest = -1;
            for (let dy = -2; dy <= 2; ++dy)
                for (let dx = -2; dx <= 2; ++dx)
                    if (inMask(x + dx, y + dy)) {
                        const c = color(x + dx, y + dy), d = colorDistance(c, edgeColor);
                        if (d > farthest) {
                            farthest = d;
                            interior = c;
                        }
                    }
            const i = y * w + x;
            ring[i] = 0;
            interiorOf[i] = interior;
            coverageOf[i] = Math.max(128, estimate(x, y, interior));
            out.push({ x, y, coverage: coverageOf[i], interior });
            previous.push(i);
        }

    // Anillos 1 y 2: vecinos de fuera del anillo anterior.
    for (let level = 1; level <= 2; ++level) {
        const current = [];
        for (const p of previous) {
            const px = p % w, py = (p - px) / w, parentCoverage = coverageOf[p];
            if (level > 1 && parentCoverage <= 0)
                continue;
            for (let dy = -1; dy <= 1; ++dy)
                for (let dx = -1; dx <= 1; ++dx) {
                    const x = px + dx, y = py + dy;
                    if ((!dx && !dy) || x < 0 || y < 0 || x >= w || y >= h || inMask(x, y))
                        continue;
                    const i = y * w + x;
                    if (ring[i] !== 0xff && ring[i] < level)
                        continue;
                    const interior = interiorOf[p];
                    const cov = Math.min(level === 1 ? 255 : parentCoverage, estimate(x, y, interior));
                    if (ring[i] === level) {
                        if (cov > coverageOf[i]) {
                            coverageOf[i] = cov;
                            interiorOf[i] = interior;
                        }
                        continue;
                    }
                    ring[i] = level;
                    coverageOf[i] = cov;
                    interiorOf[i] = interior;
                    current.push(i);
                }
        }
        for (const i of current)
            if (coverageOf[i] > 0)
                out.push({ x: i % w, y: Math.trunc(i / w), coverage: coverageOf[i], interior: interiorOf[i] });
        previous = current;
    }
    return out;
}

// "Desmezcla": sustituye en 'pixel' (alfa directo) la parte de color 'interior' (premultiplicado)
// por 'fill' (alfa directo), en la proporción 'coverage' (FloodFill::replaceInterior).
export function replaceInterior(pixel, interior, fill, coverage) {
    const p = qPremultiply(pixel), f = qPremultiply(fill), t = Math.min(255, Math.max(0, coverage)) / 255;
    const ch = (pc, fc, ic) => Math.min(255, Math.max(0, lround(pc + t * (fc - ic))));
    const a = ch(qAlpha(p), qAlpha(f), qAlpha(interior));
    if (a === 0)
        return 0;
    return qUnpremultiply(qRgba(Math.min(a, ch(qRed(p), qRed(f), qRed(interior))), Math.min(a, ch(qGreen(p), qGreen(f), qGreen(interior))),
                                Math.min(a, ch(qBlue(p), qBlue(f), qBlue(interior))), a));
}

// Cobertura de cada píxel de fuera de la máscara si esta se ensancha medio píxel con suavizado
// (halfPixelRing de Kaint). Con un borde enorme, cada cuarto del píxel cuenta si toca la máscara.
function halfPixelRing(mask, w, h, bounds) {
    const ring = new Uint8Array(bounds.w * bounds.h);
    const inside = (x, y) => x >= 0 && y >= 0 && x < w && y < h && mask[y * w + x] !== 0;
    let edge = 0;
    for (let y = bounds.y; y < bounds.y + bounds.h; ++y)
        for (let x = bounds.x; x < bounds.x + bounds.w; ++x)
            edge += !inside(x, y) && (inside(x - 1, y) || inside(x + 1, y) || inside(x, y - 1) || inside(x, y + 1)) ? 1 : 0;
    if (edge <= 60000) {
        const canvas = createCanvas(bounds.w, bounds.h), ctx = context(canvas);
        ctx.translate(-bounds.x, -bounds.y);
        ctx.beginPath();
        for (const loop of smoothContour(mask, w, h, bounds)) {
            ctx.moveTo(loop[0], loop[1]);
            for (let i = 2; i < loop.length; i += 2)
                ctx.lineTo(loop[i], loop[i + 1]);
            ctx.closePath();
        }
        ctx.fillStyle = ctx.strokeStyle = "#000";
        ctx.lineWidth = 1;
        ctx.lineCap = ctx.lineJoin = "round";
        ctx.fill();
        ctx.stroke();
        const d = ctx.getImageData(0, 0, bounds.w, bounds.h).data;
        for (let i = 0; i < ring.length; ++i)
            ring[i] = d[i * 4 + 3];
    }
    for (let y = bounds.y; y < bounds.y + bounds.h; ++y)
        for (let x = bounds.x; x < bounds.x + bounds.w; ++x) {
            const i = (y - bounds.y) * bounds.w + x - bounds.x;
            if (inside(x, y)) {
                ring[i] = 0;
            } else if (edge > 60000) {
                let quarters = 0;
                for (const sy of [-1, 1])
                    for (const sx of [-1, 1])
                        quarters += inside(x + sx, y) || inside(x, y + sy) || inside(x + sx, y + sy) ? 1 : 0;
                ring[i] = Math.min(255, quarters * 64);
            }
        }
    return ring;
}

// "Source over" con alfa directo (overStraight de Kaint) sobre el píxel RGBA de 'o' en 'j'.
function overStraight(o, j, r, g, b, sa) {
    if (sa === 0)
        return;
    const da = o[j + 3];
    if (sa === 255 || da === 0) {
        o[j] = r; o[j + 1] = g; o[j + 2] = b; o[j + 3] = sa;
        return;
    }
    const y = Math.trunc((da * (255 - sa) + 127) / 255), total = y + sa;
    const ch = (d, s) => Math.trunc((d * y + s * sa + Math.trunc(total / 2)) / total);
    o[j] = ch(o[j], r); o[j + 1] = ch(o[j + 1], g); o[j + 2] = ch(o[j + 2], b); o[j + 3] = total;
}

// Rellena desde 'seed' la zona de 'sample' (ImageData) sobre 'before' (ImageData de la capa, alfa
// directo), recortada por 'clip' (cobertura de la selección o null). Devuelve { rect, data } con
// los píxeles nuevos del rectángulo, o null si no hay zona. o: { tolerance (0..100), global,
// straight, antialias, accumulate, blend, color: {r, g, b, a}, background, pattern, sampleIsLayer }.
export function paintBucket(before, sample, seed, o, clip) {
    const w = before.width, h = before.height;
    const m = floodMask(sample, seed.x, seed.y, o.tolerance, o.global, o.straight);
    let bounds = maskBounds(m, w, h);
    if (!bounds)
        return null;
    const samplePx = new Uint32Array(sample.data.buffer, sample.data.byteOffset, w * h);
    const edge = o.antialias ? antialiasEdge(samplePx, m, w, h) : [];
    let x0 = bounds.x, y0 = bounds.y, x1 = bounds.x + bounds.w - 1, y1 = bounds.y + bounds.h - 1;
    for (const e of edge) {
        x0 = Math.min(x0, e.x); y0 = Math.min(y0, e.y); x1 = Math.max(x1, e.x); y1 = Math.max(y1, e.y);
    }
    // Acumular: como en Paint.NET, el borde suavizado sale medio píxel de la zona y se mezcla con
    // lo que había, así que cada relleno repetido engrosa el borde.
    const accumulate = o.antialias && o.accumulate;
    if (accumulate) {
        x0 = Math.max(0, x0 - 1); y0 = Math.max(0, y0 - 1); x1 = Math.min(w - 1, x1 + 1); y1 = Math.min(h - 1, y1 + 1);
    }
    bounds = { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
    const ring = accumulate ? halfPixelRing(m, w, h, bounds) : null;
    const bw = bounds.w, bh = bounds.h;
    // El color de cada píxel: con una trama, el principal donde tiene bit y el otro donde no.
    const colorAt = (x, y) => (!o.pattern || patternBit(o.pattern, x, y) ? o.color : o.background);

    // Cobertura de cada píxel.
    const coverage = new Uint8Array(bw * bh);
    for (let y = 0; y < bh; ++y)
        for (let x = 0; x < bw; ++x)
            coverage[y * bw + x] = m[(y + y0) * w + x + x0];
    const unmix = o.blend === "normal" && o.sampleIsLayer;
    if (!unmix) {
        for (const e of edge)
            if (!m[e.y * w + e.x])
                coverage[(e.y - y0) * bw + e.x - x0] = e.coverage;
        if (accumulate)
            for (let i = 0; i < coverage.length; ++i)
                coverage[i] = Math.max(coverage[i], ring[i]);
    }

    // compositeRegion: lo de antes y, encima, el relleno con su cobertura y la de la selección.
    const out = new ImageData(bw, bh), d = out.data, bd = before.data;
    for (let y = 0; y < bh; ++y) {
        const s = ((y + y0) * w + x0) * 4;
        d.set(bd.subarray(s, s + bw * 4), y * bw * 4);
    }
    const clipAt = (x, y) => (clip ? clip[y * w + x] : 255);
    const weight = (x, y) => Math.trunc((coverage[(y - y0) * bw + x - x0] * clipAt(x, y)) / 255);
    if (o.blend === "overwrite") {
        for (let y = y0; y <= y1; ++y)
            for (let x = x0; x <= x1; ++x) {
                const c = colorAt(x, y);
                lerpStraight(d, ((y - y0) * bw + x - x0) * 4, c.r, c.g, c.b, c.a ?? 255, weight(x, y));
            }
    } else {
        const top = new ImageData(bw, bh), t = top.data;
        for (let y = y0; y <= y1; ++y)
            for (let x = x0; x <= x1; ++x) {
                const j = ((y - y0) * bw + x - x0) * 4, c = colorAt(x, y);
                t[j] = c.r; t[j + 1] = c.g; t[j + 2] = c.b; t[j + 3] = scaleAlpha(c.a ?? 255, weight(x, y));
            }
        blendPixels(out, top, o.blend);
    }

    if (unmix) {
        for (const e of edge) {
            // Acumulando, lo que entra en la tolerancia queda relleno del todo (como en Paint.NET):
            // así el borde avanza parejo con cada clic.
            if (accumulate && m[e.y * w + e.x])
                continue;
            const k = (e.y * w + e.x) * 4, p = qRgba(bd[k], bd[k + 1], bd[k + 2], bd[k + 3]), c = colorAt(e.x, e.y);
            const u = replaceInterior(p, e.interior, qRgba(c.r, c.g, c.b, c.a ?? 255), e.coverage);
            const j = ((e.y - y0) * bw + e.x - x0) * 4;
            d[j] = bd[k]; d[j + 1] = bd[k + 1]; d[j + 2] = bd[k + 2]; d[j + 3] = bd[k + 3];
            lerpStraight(d, j, qRed(u), qGreen(u), qBlue(u), qAlpha(u), clipAt(e.x, e.y));
        }
        // Lo desmezclado no deja halo; encima, el medio píxel de más que acumula.
        if (accumulate)
            for (let y = y0; y <= y1; ++y)
                for (let x = x0; x <= x1; ++x) {
                    const cov = ring[(y - y0) * bw + x - x0], wgt = clipAt(x, y);
                    if (!cov || !wgt)
                        continue;
                    const c = colorAt(x, y);
                    overStraight(d, ((y - y0) * bw + x - x0) * 4, c.r, c.g, c.b, scaleAlpha(c.a ?? 255, Math.trunc((cov * wgt) / 255)));
                }
    }
    return { rect: bounds, data: out };
}

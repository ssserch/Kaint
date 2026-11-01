// Trazo de pincel (StrokeEngine de Kaint), como el de Paint.NET: cada toque se acumula en una
// máscara "encima" (donde se solapan, el borde suave se va llenando) y la capa se compone a partir
// de la copia del principio del trazo, así un color translúcido no se oscurece dentro del mismo
// trazo. El borde del toque es un disco desenfocado en luz lineal según la dureza; con la
// trayectoria suavizada, el recorrido pasa por los puntos del puntero con una curva (Catmull-Rom
// centrípeta) y el último tramo se vuelve a dibujar hasta que llega el siguiente punto. Lo usan el
// pincel, la goma, el lápiz, el tampón de clonar y volver a colorear.
import { AppState } from "../AppState.js";
import { blendPixels, clampRect, cloneCanvas, context, lerpStraight, scaleAlpha, unionRect } from "../core/Raster.js";
import { similar, threshold } from "../core/FloodFill.js";
import { patternBit } from "../core/FillPattern.js";

const linearToSrgb = (v) => (v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055);

// e^-x · I0(x), la función de Bessel modificada de orden 0 escalada (Abramowitz y Stegun 9.8.1 y
// 9.8.2): sin escalar se desborda con los desenfoques finos.
function besselI0Scaled(x) {
    if (x < 3.75) {
        const t = (x / 3.75) * (x / 3.75);
        return (1 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (0.0360768 + t * 0.0045813)))))) * Math.exp(-x);
    }
    const t = 3.75 / x;
    return (0.39894228 + t * (0.01328592 + t * (0.00225319 + t * (-0.00157565 + t * (0.00916281 + t * (-0.02057706
        + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))))) / Math.sqrt(x);
}

// Cobertura a la distancia 'u' del centro de un disco de radio 'edge' desenfocado con una
// gaussiana de desviación 'sigma' (en anillos, con la función de Bessel), por Simpson.
function blurredDisc(u, edge, sigma) {
    const from = Math.max(0, u - 7 * sigma), to = Math.min(edge, u + 7 * sigma);
    if (to <= from)
        return 0;
    const steps = 96, h = (to - from) / steps, s2 = sigma * sigma;
    const f = (r) => (r / s2) * Math.exp(-((r - u) * (r - u)) / (2 * s2)) * besselI0Scaled((r * u) / s2);
    let sum = f(from) + f(to);
    for (let i = 1; i < steps; ++i)
        sum += f(from + i * h) * (i % 2 ? 4 : 2);
    return Math.min(1, Math.max(0, (sum * h) / 3));
}

// Perfil del toque en radios del pincel (StrokeEngine::buildProfile de Kaint): con la dureza crece
// el disco (de un 59 % del radio al radio entero) y se afina el desenfoque (de un tercio del radio a
// nada), en luz lineal. Vale para cualquier tamaño; se guarda el de cada dureza.
const profiles = new Map();
function brushProfile(hardness) {
    const h = Math.min(1, Math.max(0, hardness / 100));
    let p = profiles.get(h);
    if (p)
        return p;
    const edge = 0.594375 + 0.405625 * h, sigma = (1 - h) / 3, hard = sigma < 0.004;
    const extent = edge + (hard ? 0 : 3.5 * sigma) + 0.01, entries = 1024, lut = new Float32Array(entries + 2);
    for (let i = 0; i <= entries; ++i) {
        const u = (i * extent) / entries, ink = hard ? (u <= edge ? 1 : 0) : blurredDisc(u, edge, sigma);
        lut[i] = Math.min(1, Math.max(0, 1 - linearToSrgb(1 - ink)));
    }
    p = { lut, extent, edge, sigma, at(u) {
        if (u >= extent)
            return 0;
        const f = (u / extent) * entries, i = f | 0;
        return lut[i] + (lut[i + 1] - lut[i]) * (f - i);
    } };
    profiles.set(h, p);
    return p;
}

export class Stroke {
    // mode: "paint" | "erase" | "clone" | "recolor"
    constructor(doc, layer, { mode = "paint", color = null, blend = "normal", antialias = true, hardness = 100, spacing = 15,
                             smoothing = false, cloneOffset = null, cloneSource = null, recolorFrom = null, recolorTolerance = 50,
                             recolorStraight = false, fill = 0, background = null } = {}) {
        this.doc = doc;
        this.layer = layer;
        this.mode = mode;
        this.color = color;
        this.blend = blend;
        this.antialias = antialias;
        this.hardness = hardness;
        this.profile = brushProfile(hardness);
        this.spacing = Math.max(1, spacing) / 100;
        this.smoothing = smoothing;
        this.cloneOffset = cloneOffset;
        this.recolorFrom = recolorFrom;
        this.recolorLimit = threshold(recolorTolerance);
        this.recolorStraight = recolorStraight;
        this.fill = fill;
        this.background = background;
        const w = doc.width, h = doc.height;
        this.width = w;
        this.height = h;
        this.mask = new Uint8Array(w * h);
        this.start = cloneCanvas(layer.canvas);
        this.startData = context(this.start).getImageData(0, 0, w, h);
        this.cloneData = cloneSource ? context(cloneSource).getImageData(0, 0, w, h) : this.startData;
        this.preview = cloneCanvas(layer.canvas);
        this.previewCtx = context(this.preview);
        this.dirty = null;
        // Puntos del puntero ({ x, y, size }), tramos ya definitivos, dónde va el próximo toque y el
        // tramo provisional (su zona de la máscara antes de dibujarlo).
        this.points = [];
        this.drySegments = 0;
        this.walker = null;
        this.wet = null;
        doc.setPreview({ layer, canvas: this.preview });
    }

    // Toque redondo de diámetro 'size' en (x, y), acumulado "encima" en la máscara.
    stamp(x, y, size) {
        // Como Paint.NET: por debajo de un píxel, un toque de un píxel más transparente.
        let opacity = 1;
        if (size < 1) {
            opacity = size * size;
            size = 1;
        }
        const r = size / 2, P = this.profile, aa = this.antialias, extent = aa ? r * P.extent : r;
        const x0 = Math.floor(x - extent - 1), y0 = Math.floor(y - extent - 1), x1 = Math.ceil(x + extent + 1), y1 = Math.ceil(y + extent + 1);
        const rect = clampRect({ x: x0, y: y0, w: x1 - x0, h: y1 - y0 }, this.width, this.height);
        const clip = this.doc.selection.clipMask(AppState.selectionAntialias());
        const edgePx = P.edge * r, sigmaPx = P.sigma * r, small = r < 3, k = opacity * 255, mask = this.mask;
        const cx = Math.floor(x), cy = Math.floor(y);
        for (let py = rect.y; py < rect.y + rect.h; ++py)
            for (let px = rect.x; px < rect.x + rect.w; ++px) {
                const dx = px + 0.5 - x, dy = py + 0.5 - y, d = Math.hypot(dx, dy);
                let v;
                if (!aa) {
                    v = d <= r || (px === cx && py === cy) ? 255 : 0;
                } else {
                    if (d >= extent + 1)
                        continue;
                    let c;
                    // Los píxeles del borde (con un pincel duro o pequeño) se muestrean 4×4 veces.
                    if (small || (sigmaPx < 1 && Math.abs(d - edgePx) < 1)) {
                        c = 0;
                        for (let j = 0; j < 4; ++j)
                            for (let i = 0; i < 4; ++i)
                                c += P.at(Math.hypot(dx + (i - 1.5) / 4, dy + (j - 1.5) / 4) / r);
                        c /= 16;
                    } else {
                        c = P.at(d / r);
                    }
                    v = Math.round(c * k);
                }
                if (v <= 0)
                    continue;
                const i = py * this.width + px;
                if (clip)
                    v = Math.round((v * clip[i]) / 255);
                if (v > 0)
                    mask[i] = aa ? v + (((255 - v) * mask[i] + 127) / 255 | 0) : Math.max(mask[i], v);
            }
        // Un toque fuera de la imagen (clic fuera del lienzo) no cambia nada.
        if (rect.w > 0 && rect.h > 0)
            this.dirty = unionRect(this.dirty, rect);
        return rect;
    }

    // Separación entre toques: una fracción del diámetro (15 % por defecto), nunca menos de un
    // píxel, como en Paint.NET.
    step(size) {
        return Math.max(1, Math.max(1, size) * this.spacing);
    }

    // Toques a lo largo de 'path' ([{ x, y, size }]) desde donde se quedó 'walker'.
    walk(walker, path) {
        let changed = null;
        for (const p of path) {
            if (!walker.last) {
                walker.last = p;
                changed = unionRect(changed, this.stamp(p.x, p.y, p.size));
                walker.next = this.step(p.size);
                continue;
            }
            const a = walker.last, len = Math.hypot(p.x - a.x, p.y - a.y);
            let at = walker.next;
            while (at <= len) {
                const t = len > 0 ? at / len : 1, size = a.size + (p.size - a.size) * t;
                changed = unionRect(changed, this.stamp(a.x + (p.x - a.x) * t, a.y + (p.y - a.y) * t, size));
                at += this.step(size);
            }
            walker.next = at - len;
            walker.last = p;
        }
        return changed;
    }

    // Puntos del tramo de points[i] a points[i + 1]: recto o, suavizado, la curva Catmull-Rom
    // centrípeta (en los extremos, con el punto reflejado).
    segment(i) {
        const pts = this.points, a = pts[i], b = pts[i + 1];
        if (!this.smoothing)
            return [b];
        const p0 = i > 0 ? pts[i - 1] : { x: 2 * a.x - b.x, y: 2 * a.y - b.y };
        const p3 = i + 2 < pts.length ? pts[i + 2] : { x: 2 * b.x - a.x, y: 2 * b.y - a.y };
        const knot = (u, v) => Math.max(1e-4, Math.sqrt(Math.hypot(v.x - u.x, v.y - u.y)));
        const t1 = knot(p0, a), t2 = t1 + knot(a, b), t3 = t2 + knot(b, p3);
        const steps = Math.max(1, Math.ceil(Math.hypot(b.x - a.x, b.y - a.y))), out = [];
        const lerp = (p, q, ta, tb, t) => ({ x: ((tb - t) * p.x + (t - ta) * q.x) / (tb - ta), y: ((tb - t) * p.y + (t - ta) * q.y) / (tb - ta) });
        for (let j = 1; j <= steps; ++j) {
            const t = t1 + ((t2 - t1) * j) / steps;
            const a1 = lerp(p0, a, 0, t1, t), a2 = lerp(a, b, t1, t2, t), a3 = lerp(b, p3, t2, t3, t);
            const b1 = lerp(a1, a2, 0, t2, t), b2 = lerp(a2, a3, t1, t3, t), c = lerp(b1, b2, t1, t2, t);
            out.push({ x: c.x, y: c.y, size: a.size + ((b.size - a.size) * j) / steps });
        }
        return out;
    }

    // Quita el tramo provisional (deja la máscara como estaba antes de dibujarlo).
    restoreWet() {
        const wet = this.wet;
        this.wet = null;
        if (!wet)
            return null;
        for (let y = 0; y < wet.rect.h; ++y)
            this.mask.set(wet.backup.subarray(y * wet.rect.w, (y + 1) * wet.rect.w), (wet.rect.y + y) * this.width + wet.rect.x);
        return wet.rect;
    }

    // El último tramo, hasta el punto actual: se guarda lo que había debajo para quitarlo cuando
    // llegue el siguiente punto y el tramo sea definitivo.
    drawWet() {
        const i = this.points.length - 2;
        if (i < this.drySegments || i < 0)
            return null;
        const path = this.segment(i), margin = (this.points[0].size / 2) * this.profile.extent + 2;
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        for (const p of [this.walker.last, ...path]) {
            const m = Math.max(margin, (p.size / 2) * this.profile.extent + 2);
            x0 = Math.min(x0, p.x - m); y0 = Math.min(y0, p.y - m); x1 = Math.max(x1, p.x + m); y1 = Math.max(y1, p.y + m);
        }
        const rect = clampRect({ x: Math.floor(x0), y: Math.floor(y0), w: Math.ceil(x1) - Math.floor(x0), h: Math.ceil(y1) - Math.floor(y0) },
                               this.width, this.height);
        if (rect.w <= 0 || rect.h <= 0)
            return null;
        const backup = new Uint8Array(rect.w * rect.h);
        for (let y = 0; y < rect.h; ++y)
            backup.set(this.mask.subarray((rect.y + y) * this.width + rect.x, (rect.y + y) * this.width + rect.x + rect.w), y * rect.w);
        this.wet = { rect, backup };
        this.walk({ ...this.walker }, path);
        return rect;
    }

    // Sigue el trazo hasta (x, y) con el diámetro 'size' (con la presión ya aplicada).
    lineTo(x, y, size) {
        if (!this.walker) {
            this.points = [{ x, y, size }];
            this.walker = {};
            this.refresh(this.walk(this.walker, this.points));
            return;
        }
        const last = this.points.at(-1);
        if (last.x === x && last.y === y)
            return;
        this.points.push({ x, y, size });
        let changed = this.restoreWet();
        // Sin suavizar, cada tramo es recto y definitivo; suavizado, un tramo es definitivo cuando
        // ya se conoce el punto que lo sigue, y el último se dibuja provisional.
        const lastDry = this.points.length - (this.smoothing ? 3 : 2);
        while (this.drySegments <= lastDry)
            changed = unionRect(changed, this.walk(this.walker, this.segment(this.drySegments++)));
        if (this.smoothing)
            changed = unionRect(changed, this.drawWet());
        if (changed)
            this.refresh(changed);
    }

    // El tramo provisional pasa a definitivo (con el final de la curva reflejado).
    finishPath() {
        if (!this.walker)
            return;
        let changed = this.restoreWet();
        while (this.drySegments <= this.points.length - 2)
            changed = unionRect(changed, this.walk(this.walker, this.segment(this.drySegments++)));
        if (changed)
            this.refresh(changed);
    }

    // Píxeles sueltos (lápiz): línea de Bresenham sin suavizado.
    pixelLine(x0, y0, x1, y1) {
        x0 = Math.floor(x0); y0 = Math.floor(y0); x1 = Math.floor(x1); y1 = Math.floor(y1);
        const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        let err = dx + dy;
        const clip = this.doc.selection.clipMask(AppState.selectionAntialias());
        let rect = null;
        for (;;) {
            if (x0 >= 0 && y0 >= 0 && x0 < this.width && y0 < this.height) {
                const i = y0 * this.width + x0;
                this.mask[i] = clip ? clip[i] : 255;
                rect = unionRect(rect, { x: x0, y: y0, w: 1, h: 1 });
            }
            if (x0 === x1 && y0 === y1)
                break;
            const e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
        if (rect) {
            this.dirty = unionRect(this.dirty, rect);
            this.refresh(rect);
        }
    }

    // Recompone la vista previa en 'rect' a partir de la copia inicial y la máscara.
    refresh(rect) {
        if (!rect || rect.w <= 0 || rect.h <= 0)
            return;
        const w = this.width, src = this.startData.data;
        const out = this.previewCtx.createImageData(rect.w, rect.h), o = out.data;
        for (let y = 0; y < rect.h; ++y) {
            const row = (rect.y + y) * w + rect.x;
            for (let x = 0; x < rect.w; ++x) {
                const i = (row + x) * 4, j = (y * rect.w + x) * 4;
                o[j] = src[i]; o[j + 1] = src[i + 1]; o[j + 2] = src[i + 2]; o[j + 3] = src[i + 3];
            }
        }
        if (this.mode === "paint") {
            // Como compositeRegion de Kaint: el color con el alfa por la cobertura y el modo de
            // fusión; "Sobrescribir" sustituye lo de debajo en la proporción de la cobertura.
            const top = new ImageData(rect.w, rect.h), t = top.data, overwrite = this.blend === "overwrite";
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    if (!m) continue;
                    const j = (y * rect.w + x) * 4;
                    // La trama de relleno: el color principal donde tiene bit y el otro donde no.
                    const c = !this.fill || patternBit(this.fill, rect.x + x, rect.y + y) ? this.color : this.background, ca = c.a ?? 255;
                    if (overwrite)
                        lerpStraight(o, j, c.r, c.g, c.b, ca, m);
                    else {
                        t[j] = c.r; t[j + 1] = c.g; t[j + 2] = c.b; t[j + 3] = scaleAlpha(ca, m);
                    }
                }
            if (!overwrite)
                blendPixels(out, top, this.blend);
        } else if (this.mode === "erase") {
            // La opacidad del color regula la fuerza de la goma.
            const k = (this.color?.a ?? 255) / 255;
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    if (m) o[(y * rect.w + x) * 4 + 3] *= 1 - (m / 255) * k;
                }
        } else if (this.mode === "clone") {
            // Lo de origen con la opacidad del color (Stroke::render de Kaint).
            const top = new ImageData(rect.w, rect.h), t = top.data, s = this.cloneData.data, off = this.cloneOffset;
            const opacity = this.color?.a ?? 255, overwrite = this.blend === "overwrite";
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    if (!m) continue;
                    const sx = rect.x + x + off.x, sy = rect.y + y + off.y, j = (y * rect.w + x) * 4;
                    const inside = sx >= 0 && sy >= 0 && sx < w && sy < this.height, i = (sy * w + sx) * 4;
                    const r = inside ? s[i] : 0, g = inside ? s[i + 1] : 0, b = inside ? s[i + 2] : 0;
                    const a = inside ? scaleAlpha(s[i + 3], opacity) : 0;
                    if (overwrite)
                        lerpStraight(o, j, r, g, b, a, m);
                    else {
                        t[j] = r; t[j + 1] = g; t[j + 2] = b; t[j + 3] = scaleAlpha(a, m);
                    }
                }
            if (!overwrite)
                blendPixels(out, top, this.blend);
        } else if (this.mode === "recolor") {
            // Como Stroke::render de Kaint: los píxeles parecidos al color de origen pasan al nuevo
            // (con su alfa), en la proporción de la cobertura y de la opacidad del color.
            const from = this.recolorFrom, c = this.color, ref = [from.r, from.g, from.b, from.a ?? 255], opacity = c.a ?? 255;
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    const j = (y * rect.w + x) * 4;
                    if (!m || !similar(o, j, ref, this.recolorLimit, this.recolorStraight))
                        continue;
                    lerpStraight(o, j, c.r, c.g, c.b, o[j + 3], Math.trunc((m * opacity) / 255));
                }
        }
        this.previewCtx.putImageData(out, rect.x, rect.y);
        this.doc.pixelsChanged(this.layer, rect);
    }

    // Aplica el trazo a la capa y lo deja en el historial.
    commit(name, icon) {
        this.finishPath();
        const doc = this.doc, layer = this.layer;
        doc.preview = null;
        const r = this.dirty;
        if (!r || r.w <= 0 || r.h <= 0) {
            doc.pixelsChanged(layer, null);
            return;
        }
        const before = context(this.start).getImageData(r.x, r.y, r.w, r.h);
        layer.ctx.putImageData(this.previewCtx.getImageData(r.x, r.y, r.w, r.h), r.x, r.y);
        doc.pixelsChanged(layer, r);
        doc.pushPixels(layer, r, before, name, icon);
    }

    cancel() {
        this.doc.setPreview(null);
    }
}

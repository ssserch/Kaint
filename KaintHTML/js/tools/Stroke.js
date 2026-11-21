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

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Un número al azar (0..1) fijo para cada toque y cada uso: el tramo provisional se vuelve a
// dibujar al llegar el siguiente punto y debe salir igual (stableRandom de Kaint).
function stableRandom(x, y, salt) {
    let h = (Math.imul(Math.round(x * 8), 73856093) ^ Math.imul(Math.round(y * 8), 19349663) ^ Math.imul(salt, 83492791)) >>> 0;
    h ^= h >>> 13;
    h = Math.imul(h, 0x5bd1e995) >>> 0;
    h ^= h >>> 15;
    return (h & 0xffffff) / 0xffffff;
}

export class Stroke {
    // mode: "paint" | "erase" | "clone" | "recolor"
    constructor(doc, layer, { mode = "paint", color = null, blend = "normal", antialias = true, hardness = 100, spacing = 15,
                             smoothing = false, cloneOffset = null, cloneSource = null, recolorFrom = null, recolorTolerance = 50,
                             recolorStraight = false, fill = 0, background = null, advanced = null, symmetry = 0, symmetryCount = 6 } = {}) {
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
        // Opciones avanzadas del pincel (Configuración > Herramientas), como en Ibis Paint y FireAlpaca:
        // { tipShape (0 redonda, 1 cuadrada), angle, roundness, flow, sizeJitter, opacityJitter,
        // angleJitter, scatter, pressureOpacity, stabilizer (0..20), taperIn, taperOut } o null.
        this.advanced = advanced;
        // Simetría alrededor del centro de la imagen: 0 ninguna, 1 a izquierda y derecha, 2 arriba y
        // abajo, 3 las dos, 4 radial ('symmetryCount' copias girando).
        this.symmetry = symmetry;
        this.symmetryCount = Math.min(32, Math.max(2, symmetryCount));
        this.lastPressure = 1;
        this.strokeLength = -1;
        this.dabPressure = 1;
        this.stable = null;
        this.raw = null;
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

    // Los sitios donde va un toque en (x, y) con la simetría (el primero, el mismo).
    symmetric(x, y) {
        const cx = this.width / 2, cy = this.height / 2;
        switch (this.symmetry) {
        case 1: return [[x, y], [2 * cx - x, y]];
        case 2: return [[x, y], [x, 2 * cy - y]];
        case 3: return [[x, y], [2 * cx - x, y], [x, 2 * cy - y], [2 * cx - x, 2 * cy - y]];
        case 4: {
            const out = [[x, y]], dx = x - cx, dy = y - cy;
            for (let i = 1; i < this.symmetryCount; ++i) {
                const a = (2 * Math.PI * i) / this.symmetryCount, ca = Math.cos(a), sa = Math.sin(a);
                out.push([cx + dx * ca - dy * sa, cy + dx * sa + dy * ca]);
            }
            return out;
        }
        default: return [[x, y]];
        }
    }

    // Toque redondo de diámetro 'size' en (x, y), acumulado "encima" en la máscara (con la simetría,
    // en cada sitio: en la misma máscara, donde se cruzan no se oscurece).
    stamp(x, y, size) {
        if (!this.symmetry)
            return this.stampAt(x, y, size);
        let rect = null;
        for (const [sx, sy] of this.symmetric(x, y))
            rect = unionRect(rect, this.stampAt(sx, sy, size));
        return rect;
    }

    stampAt(x, y, size) {
        if (this.advanced && this.antialias)
            return this.stampAdvanced(x, y, size);
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

    // El toque con las opciones avanzadas (StrokeEngine::dabAdvanced de Kaint): variación al azar de
    // tamaño, opacidad, ángulo y sitio; punta redonda o cuadrada, girada y aplastada; flujo y opacidad
    // por presión.
    stampAdvanced(x, y, size) {
        const A = this.advanced, kx = x, ky = y;
        size *= 1 - clamp(A.sizeJitter || 0, 0, 1) * stableRandom(kx, ky, 1);
        if (A.scatter > 0) {
            const a = stableRandom(kx, ky, 2) * 2 * Math.PI, dist = stableRandom(kx, ky, 3) * A.scatter * size;
            x += Math.cos(a) * dist;
            y += Math.sin(a) * dist;
        }
        let opacity = clamp(A.flow ?? 1, 0.01, 1) * (1 - clamp(A.opacityJitter || 0, 0, 1) * stableRandom(kx, ky, 4));
        if (A.pressureOpacity)
            opacity *= clamp(this.dabPressure, 0, 1);
        if (size < 1) {
            opacity *= size * size;
            size = 1;
        }
        const angle = (((A.angle || 0) + clamp(A.angleJitter || 0, 0, 1) * 180 * (stableRandom(kx, ky, 5) * 2 - 1)) * Math.PI) / 180;
        const roundness = clamp(A.roundness ?? 1, 0.05, 1), square = A.tipShape === 1, r = size / 2, P = this.profile;
        // Lo que puede abarcar (la diagonal de la punta cuadrada).
        const extent = r * P.extent * (square ? Math.SQRT2 : 1);
        const x0 = Math.floor(x - extent - 1), y0 = Math.floor(y - extent - 1);
        const rect = clampRect({ x: x0, y: y0, w: Math.ceil(x + extent + 1) - x0, h: Math.ceil(y + extent + 1) - y0 }, this.width, this.height);
        if (rect.w <= 0 || rect.h <= 0)
            return rect;
        const clip = this.doc.selection.clipMask(AppState.selectionAntialias()), mask = this.mask;
        const ca = Math.cos(angle), sa = Math.sin(angle), k = opacity * 255, samples = r < 3 ? 4 : 1, rw = r * roundness;
        for (let py = rect.y; py < rect.y + rect.h; ++py)
            for (let px = rect.x; px < rect.x + rect.w; ++px) {
                let c = 0;
                for (let j = 0; j < samples; ++j)
                    for (let i = 0; i < samples; ++i) {
                        const dx = px + (i + 0.5) / samples - x, dy = py + (j + 0.5) / samples - y;
                        // En los ejes de la punta: girada y aplastada.
                        const u = (dx * ca + dy * sa) / r, w = (-dx * sa + dy * ca) / rw;
                        c += P.at(square ? Math.max(Math.abs(u), Math.abs(w)) : Math.hypot(u, w));
                    }
                let v = Math.round((c / (samples * samples)) * k);
                if (v <= 0)
                    continue;
                const idx = py * this.width + px;
                if (clip)
                    v = Math.round((v * clip[idx]) / 255);
                if (v > 0)
                    mask[idx] = v + (((255 - v) * mask[idx] + 127) / 255 | 0);
            }
        this.dirty = unionRect(this.dirty, rect);
        return rect;
    }

    // Lo que encoge el toque a 'distance' del principio (entrada) y, con la longitud total conocida
    // (al terminar), del final (salida): las líneas en punta del manga.
    taperFactor(distance) {
        const A = this.advanced;
        if (!A)
            return 1;
        let f = 1;
        if (A.taperIn > 0)
            f = Math.min(f, 0.08 + 0.92 * clamp(distance / A.taperIn, 0, 1));
        if (A.taperOut > 0 && this.strokeLength >= 0)
            f = Math.min(f, 0.08 + 0.92 * clamp((this.strokeLength - distance) / A.taperOut, 0, 1));
        return f;
    }

    // Hasta dónde llega un toque de diámetro 'size' (con la punta cuadrada y la dispersión, más lejos).
    reach(size) {
        const A = this.advanced;
        if (!A)
            return (size / 2) * this.profile.extent + 2;
        return (size / 2) * this.profile.extent * (A.tipShape === 1 ? Math.SQRT2 : 1) + (A.scatter || 0) * size + 2;
    }

    // Formas rápidas: el trazo hecho hasta ahora se sustituye por esta línea quebrada ([{ x, y }]),
    // recta entre sus puntos y con la presión del último.
    replaceWith(path) {
        if (!this.walker || path.length < 2)
            return;
        this.restoreWet();
        const dirty = this.dirty;
        this.mask.fill(0);
        this.stable = this.raw = null;
        this.smoothing = false;
        const size = this.points.at(-1)?.size ?? 1, pressure = this.lastPressure;
        this.points = path.map((p) => ({ x: p.x, y: p.y, size, pressure }));
        this.walker = {};
        const changed = this.walk(this.walker, this.points);
        this.drySegments = this.points.length - 1;
        this.refresh(unionRect(dirty, changed));
    }

    // Lo mismo para el lápiz: líneas de píxeles entre los puntos.
    replacePixelsWith(path) {
        const dirty = this.dirty;
        this.mask.fill(0);
        for (let i = 0; i + 1 < path.length; ++i)
            this.pixelLine(path[i].x, path[i].y, path[i + 1].x, path[i + 1].y);
        if (dirty)
            this.refresh(unionRect(dirty, this.dirty));
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
                walker.distance = 0;
                this.dabPressure = p.pressure ?? 1;
                const size = p.size * this.taperFactor(0);
                changed = unionRect(changed, this.stamp(p.x, p.y, size));
                walker.next = this.step(size);
                continue;
            }
            const a = walker.last, len = Math.hypot(p.x - a.x, p.y - a.y), pa = a.pressure ?? 1, pb = p.pressure ?? 1;
            let at = walker.next;
            while (at <= len) {
                const t = len > 0 ? at / len : 1;
                this.dabPressure = pa + (pb - pa) * t;
                const size = (a.size + (p.size - a.size) * t) * this.taperFactor(walker.distance + at);
                changed = unionRect(changed, this.stamp(a.x + (p.x - a.x) * t, a.y + (p.y - a.y) * t, size));
                at += this.step(size);
            }
            walker.next = at - len;
            walker.distance = (walker.distance || 0) + len;
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
            out.push({ x: c.x, y: c.y, size: a.size + ((b.size - a.size) * j) / steps,
                       pressure: (a.pressure ?? 1) + (((b.pressure ?? 1) - (a.pressure ?? 1)) * j) / steps });
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
        const path = this.segment(i), margin = this.reach(this.points[0].size);
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        for (const p of [this.walker.last, ...path]) {
            const m = Math.max(margin, this.reach(p.size));
            x0 = Math.min(x0, p.x - m); y0 = Math.min(y0, p.y - m); x1 = Math.max(x1, p.x + m); y1 = Math.max(y1, p.y + m);
        }
        // Con simetría, también donde caen las copias.
        if (this.symmetry) {
            const corners = [[x0, y0], [x1, y0], [x0, y1], [x1, y1]].flatMap(([x, y]) => this.symmetric(x, y));
            x0 = Math.min(...corners.map((c) => c[0])); x1 = Math.max(...corners.map((c) => c[0]));
            y0 = Math.min(...corners.map((c) => c[1])); y1 = Math.max(...corners.map((c) => c[1]));
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

    // Sigue el trazo hasta (x, y) con el diámetro 'size' (con la presión ya aplicada); 'pressure'
    // (0..1) es para la opacidad por presión del pincel avanzado.
    lineTo(x, y, size, pressure = 1) {
        this.lastPressure = pressure;
        // Estabilizador (como la corrección de FireAlpaca): el trazo sigue al puntero con retraso;
        // cuanto más alto, más suave y más atrás se queda. Al terminar llega hasta el último punto.
        const level = this.advanced?.stabilizer || 0;
        if (level > 0 && this.walker) {
            if (!this.stable)
                this.stable = { ...this.points.at(-1), pressure: this.points.at(-1).pressure ?? 1 };
            this.raw = { x, y, size, pressure };
            const s = this.stable, k = 1 / (1 + level * 0.6);
            s.x += (x - s.x) * k;
            s.y += (y - s.y) * k;
            s.size += (size - s.size) * k;
            s.pressure += (pressure - s.pressure) * k;
            this.lineToPoint(s.x, s.y, s.size, s.pressure);
            return;
        }
        this.lineToPoint(x, y, size, pressure);
    }

    lineToPoint(x, y, size, pressure = 1) {
        if (!this.walker) {
            this.points = [{ x, y, size, pressure }];
            this.walker = {};
            this.refresh(this.walk(this.walker, this.points));
            return;
        }
        const last = this.points.at(-1);
        if (last.x === x && last.y === y)
            return;
        this.points.push({ x, y, size, pressure });
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
        // Con el estabilizador, el trazo acaba donde se soltó el puntero (unos pasos hasta allí).
        if (this.stable && this.raw) {
            const s = this.stable, r = this.raw;
            this.stable = this.raw = null;
            for (let i = 1; i <= 4; i++)
                this.lineToPoint(s.x + ((r.x - s.x) * i) / 4, s.y + ((r.y - s.y) * i) / 4, s.size + ((r.size - s.size) * i) / 4,
                                 s.pressure + ((r.pressure - s.pressure) * i) / 4);
        }
        let changed = this.restoreWet();
        while (this.drySegments <= this.points.length - 2)
            changed = unionRect(changed, this.walk(this.walker, this.segment(this.drySegments++)));
        // Salida (manga): ya se sabe dónde acaba; el trazo se vuelve a dibujar entero, afinándose
        // hacia el final.
        if (this.advanced?.taperOut > 0 && this.points.length > 1 && this.strokeLength < 0) {
            this.strokeLength = this.walker.distance || 0;
            changed = unionRect(changed, this.dirty);
            this.mask.fill(0);
            let all = [this.points[0]];
            for (let i = 0; i + 1 < this.points.length; i++)
                all = all.concat(this.segment(i));
            changed = unionRect(changed, this.walk({}, all));
        }
        if (changed)
            this.refresh(changed);
    }

    // Píxeles sueltos (lápiz): línea de Bresenham sin suavizado (con la simetría, en cada sitio, por los
    // centros de los píxeles).
    pixelLine(x0, y0, x1, y1) {
        if (!this.symmetry)
            return this.pixelLineAt(x0, y0, x1, y1);
        const a = this.symmetric(Math.floor(x0) + 0.5, Math.floor(y0) + 0.5), b = this.symmetric(Math.floor(x1) + 0.5, Math.floor(y1) + 0.5);
        a.forEach(([ax, ay], i) => this.pixelLineAt(ax, ay, b[i][0], b[i][1]));
    }

    pixelLineAt(x0, y0, x1, y1) {
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
        // Solo la cobertura (las herramientas de PhotoCraft que curan o quitan lo que se pinta
        // encima): la capa no cambia, la herramienta la enseña a su manera.
        if (this.mode === "mask") {
            this.doc.pixelsChanged(this.layer, rect);
            return;
        }
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
        // Alpha-Lock, también mientras se pinta.
        if (this.layer.alphaLock)
            for (let y = 0; y < rect.h; ++y) {
                const row = (rect.y + y) * w + rect.x;
                for (let x = 0; x < rect.w; ++x) {
                    const i = (row + x) * 4, j = (y * rect.w + x) * 4;
                    if (src[i + 3] === 0) {
                        o[j] = src[i]; o[j + 1] = src[i + 1]; o[j + 2] = src[i + 2]; o[j + 3] = 0;
                    } else {
                        o[j + 3] = src[i + 3];
                    }
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

    // Los píxeles RGBA 'pixels' (del tamaño de 'rect') sobre la capa de antes del trazo, a través de
    // la cobertura (con la selección) y el modo de fusión del trazo; 'opacity' escala su alfa.
    applyThroughCoverage(rect, pixels, opacity = 255) {
        const w = this.width, src = this.startData.data;
        const out = this.previewCtx.createImageData(rect.w, rect.h), o = out.data;
        const top = new ImageData(rect.w, rect.h), t = top.data, overwrite = this.blend === "overwrite";
        for (let y = 0; y < rect.h; ++y)
            for (let x = 0; x < rect.w; ++x) {
                const i = ((rect.y + y) * w + rect.x + x) * 4, j = (y * rect.w + x) * 4;
                o[j] = src[i]; o[j + 1] = src[i + 1]; o[j + 2] = src[i + 2]; o[j + 3] = src[i + 3];
                const m = this.mask[(rect.y + y) * w + rect.x + x];
                if (!m)
                    continue;
                const a = scaleAlpha(pixels[j + 3], opacity);
                if (overwrite)
                    lerpStraight(o, j, pixels[j], pixels[j + 1], pixels[j + 2], a, m);
                else {
                    t[j] = pixels[j]; t[j + 1] = pixels[j + 1]; t[j + 2] = pixels[j + 2]; t[j + 3] = scaleAlpha(a, m);
                }
            }
        if (!overwrite)
            blendPixels(out, top, this.blend);
        this.previewCtx.putImageData(out, rect.x, rect.y);
        this.dirty = unionRect(this.dirty, rect);
        this.doc.pixelsChanged(this.layer, rect);
    }
}

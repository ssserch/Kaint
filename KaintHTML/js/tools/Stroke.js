// Trazo de pincel: la cobertura se acumula en una máscara (máximo de cada sello, así el borde
// suave no se oscurece donde se solapan) y la capa se compone a partir de la copia del principio
// del trazo. Lo usan el pincel, la goma, el lápiz, el tampón de clonar y volver a colorear.
import { blendPixels, clampRect, cloneCanvas, context, unionRect } from "../core/Raster.js";
import { threshold } from "../core/FloodFill.js";

export class Stroke {
    // mode: "paint" | "erase" | "clone" | "recolor"
    constructor(doc, layer, { mode = "paint", color = null, blend = "normal", antialias = true, hardness = 100, spacing = 15,
                             cloneOffset = null, cloneSource = null, recolorFrom = null, recolorTolerance = 50 } = {}) {
        this.doc = doc;
        this.layer = layer;
        this.mode = mode;
        this.color = color;
        this.blend = blend;
        this.antialias = antialias;
        this.hardness = hardness;
        this.spacing = Math.max(1, spacing) / 100;
        this.cloneOffset = cloneOffset;
        this.recolorFrom = recolorFrom;
        this.recolorLimit = threshold(recolorTolerance);
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
        this.last = null;
        doc.setPreview({ layer, canvas: this.preview });
    }

    // Sello redondo de diámetro 'size' en (x, y).
    stamp(x, y, size) {
        const r = Math.max(0.5, size / 2);
        const x0 = Math.floor(x - r - 1), y0 = Math.floor(y - r - 1), x1 = Math.ceil(x + r + 1), y1 = Math.ceil(y + r + 1);
        const rect = clampRect({ x: x0, y: y0, w: x1 - x0, h: y1 - y0 }, this.width, this.height);
        const inner = (r * this.hardness) / 100;
        const sel = this.doc.selection;
        for (let py = rect.y; py < rect.y + rect.h; ++py)
            for (let px = rect.x; px < rect.x + rect.w; ++px) {
                const d = Math.hypot(px + 0.5 - x, py + 0.5 - y);
                let cov;
                if (!this.antialias)
                    cov = d <= r ? 1 : 0;
                else if (this.hardness >= 100 || d <= inner)
                    cov = Math.min(1, Math.max(0, r + 0.5 - d));
                else
                    cov = Math.max(0, 1 - (d - inner) / Math.max(0.5, r - inner));
                if (cov <= 0)
                    continue;
                let v = Math.round(cov * 255);
                if (sel.mask)
                    v = Math.round((v * sel.mask[py * this.width + px]) / 255);
                const i = py * this.width + px;
                if (v > this.mask[i])
                    this.mask[i] = v;
            }
        // Un sello fuera de la imagen (clic fuera del lienzo) no cambia nada.
        if (rect.w > 0 && rect.h > 0)
            this.dirty = unionRect(this.dirty, rect);
        return rect;
    }

    // Línea de sellos desde el último punto (separación: el espaciado, del 15 % del diámetro por
    // defecto, como Kaint).
    lineTo(x, y, size) {
        let changed = null;
        if (!this.last) {
            changed = this.stamp(x, y, size);
        } else {
            const dx = x - this.last.x, dy = y - this.last.y, dist = Math.hypot(dx, dy);
            const step = Math.max(0.5, size * this.spacing);
            let t = this.last.carry ?? step;
            while (t <= dist) {
                changed = unionRect(changed, this.stamp(this.last.x + (dx * t) / dist, this.last.y + (dy * t) / dist, size));
                t += step;
            }
            this.last = { x, y, carry: t - dist };
            if (changed)
                this.refresh(changed);
            return;
        }
        this.last = { x, y, carry: Math.max(0.5, size * this.spacing) };
        this.refresh(changed);
    }

    // Píxeles sueltos (lápiz): línea de Bresenham sin suavizado.
    pixelLine(x0, y0, x1, y1) {
        x0 = Math.floor(x0); y0 = Math.floor(y0); x1 = Math.floor(x1); y1 = Math.floor(y1);
        const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        let err = dx + dy;
        const sel = this.doc.selection;
        let rect = null;
        for (;;) {
            if (x0 >= 0 && y0 >= 0 && x0 < this.width && y0 < this.height) {
                const i = y0 * this.width + x0;
                this.mask[i] = sel.mask ? sel.mask[i] : 255;
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
            const top = new ImageData(rect.w, rect.h), t = top.data, c = this.color;
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    if (!m) continue;
                    const j = (y * rect.w + x) * 4;
                    t[j] = c.r; t[j + 1] = c.g; t[j + 2] = c.b; t[j + 3] = (m * (c.a ?? 255)) / 255;
                }
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
            const top = new ImageData(rect.w, rect.h), t = top.data, s = this.cloneData.data, off = this.cloneOffset;
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    if (!m) continue;
                    const sx = rect.x + x + off.x, sy = rect.y + y + off.y;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= this.height) continue;
                    const i = (sy * w + sx) * 4, j = (y * rect.w + x) * 4;
                    t[j] = s[i]; t[j + 1] = s[i + 1]; t[j + 2] = s[i + 2]; t[j + 3] = (s[i + 3] * m) / 255;
                }
            blendPixels(out, top, this.blend);
        } else if (this.mode === "recolor") {
            const from = this.recolorFrom, to = this.color;
            for (let y = 0; y < rect.h; ++y)
                for (let x = 0; x < rect.w; ++x) {
                    const m = this.mask[(rect.y + y) * w + rect.x + x];
                    if (!m) continue;
                    const j = (y * rect.w + x) * 4;
                    const dr = o[j] - from.r, dg = o[j + 1] - from.g, db = o[j + 2] - from.b;
                    if (dr * dr + dg * dg + db * db > this.recolorLimit) continue;
                    // Se conserva la diferencia con el color de origen (las sombras siguen ahí).
                    const k = m / 255;
                    o[j] += (Math.min(255, Math.max(0, to.r + dr)) - o[j]) * k;
                    o[j + 1] += (Math.min(255, Math.max(0, to.g + dg)) - o[j + 1]) * k;
                    o[j + 2] += (Math.min(255, Math.max(0, to.b + db)) - o[j + 2]) * k;
                }
        }
        this.previewCtx.putImageData(out, rect.x, rect.y);
        this.doc.pixelsChanged(this.layer, rect);
    }

    // Aplica el trazo a la capa y lo deja en el historial.
    commit(name, icon) {
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

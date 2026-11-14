// Las herramientas de PhotoCraft (como src/app/tools/RetouchTools.cpp de Kaint): Pincel corrector,
// Pincel corrector puntual, Quitar, Parche, Lazo magnético y Selección de objetos. Los algoritmos
// (core/retouch) son los de PhotoCraft (ArtCraft Team y los colaboradores de PhotoCraft, licencia
// MIT). Solo se ven con las funciones de PhotoCraft activadas (Configuración > Complementos) y llevan
// la pieza de puzle en el icono. Los cálculos largos van en un Web Worker (Esc los cancela).
import { StrokeTool } from "./PaintTools.js";
import { Stroke } from "./Stroke.js";
import { LassoSelectTool, SelectTool } from "./SelectionTools.js";
import { selectHint } from "./Tool.js";
import { AppState } from "../AppState.js";
import { Selection } from "../core/Selection.js";
import { context, createCanvas } from "../core/Raster.js";
import { runRetouch } from "../core/retouch/Runner.js";
import { MAX_PIXELS, box, boxArea, boxH, boxW, healCoarse, removeWindow } from "../core/retouch/Common.js";
import { patchPreviewStep, planPatch, spotWindow } from "../core/retouch/Heal.js";
import { MagneticTracer, magneticSettings, polylineLength } from "../core/retouch/Magnetic.js";
import { OBJECT_AT_WORK_PX, SUBJECT_WORK_PX } from "../core/retouch/Segment.js";
import { tr } from "../i18n.js";

const tooLarge = () => tr("La zona es demasiado grande (más de 16 millones de píxeles).");

// Los píxeles RGBA de 'rect' de la capa activa o de la imagen (fuera del lienzo, transparentes).
function pixelsOf(source, rect) {
    if (rect.w <= 0 || rect.h <= 0)
        return new Uint8ClampedArray(0);
    return context(source).getImageData(rect.x, rect.y, rect.w, rect.h).data;
}

function sourceCanvas(doc, image) {
    return image ? doc.flattened() : doc.activeLayer.canvas;
}

// La cobertura de 'mask' (del tamaño de la imagen) sobre 'rect'.
function coverageOf(mask, width, height, rect) {
    const out = new Uint8Array(Math.max(rect.w, 0) * Math.max(rect.h, 0));
    for (let y = Math.max(rect.y, 0); y < Math.min(rect.y + rect.h, height); ++y)
        for (let x = Math.max(rect.x, 0); x < Math.min(rect.x + rect.w, width); ++x)
            out[(y - rect.y) * rect.w + (x - rect.x)] = mask[y * width + x];
    return out;
}

// El rectángulo de lo cubierto de 'mask' dentro de 'area'.
function coveredBounds(mask, width, area) {
    let x0 = Infinity, y0 = Infinity, x1 = -1, y1 = -1;
    for (let y = area.y; y < area.y + area.h; ++y)
        for (let x = area.x; x < area.x + area.w; ++x)
            if (mask[y * width + x]) {
                x0 = Math.min(x0, x); x1 = Math.max(x1, x);
                y0 = Math.min(y0, y); y1 = Math.max(y1, y);
            }
    return x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
}

const toBox = (r) => box(r.x, r.y, r.x + r.w, r.y + r.h);
const toRect = (b) => ({ x: b.x0, y: b.y0, w: boxW(b), h: boxH(b) });

// Un tinte del color [r, g, b] con 'alpha' por la cobertura de 'mask', sobre 'rect'.
function tintCanvas(mask, width, rect, [r, g, b], alpha) {
    const canvas = createCanvas(rect.w, rect.h), ctx = context(canvas), img = ctx.createImageData(rect.w, rect.h), d = img.data;
    for (let y = 0; y < rect.h; ++y)
        for (let x = 0; x < rect.w; ++x) {
            const m = mask[(rect.y + y) * width + rect.x + x];
            if (!m)
                continue;
            const j = (y * rect.w + x) * 4;
            d[j] = r; d[j + 1] = g; d[j + 2] = b; d[j + 3] = Math.round((m * alpha) / 255);
        }
    ctx.putImageData(img, 0, 0);
    return canvas;
}

function drawOver(ctx, view, rect, canvas) {
    if (!canvas || !rect || rect.w <= 0 || rect.h <= 0)
        return;
    const a = view.imageToView(rect.x, rect.y), b = view.imageToView(rect.x + rect.w, rect.y + rect.h);
    ctx.save();
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(canvas, a.x, a.y, b.x - a.x, b.y - a.y);
    ctx.restore();
}

// Pone los píxeles 'pixels' (RGBA de 'rect') en la capa donde 'where(i)' y deja un paso en el
// historial; 'through' (cobertura 0..255) los mezcla en vez de reemplazarlos.
function writePixels(doc, layer, rect, pixels, name, icon, { where = null, through = null } = {}) {
    const before = layer.ctx.getImageData(rect.x, rect.y, rect.w, rect.h);
    const after = new ImageData(new Uint8ClampedArray(before.data), rect.w, rect.h), d = after.data;
    for (let i = 0; i < rect.w * rect.h; ++i) {
        const j = i * 4;
        if (through) {
            // Normal a través de la cobertura (alfa directo).
            const t = (through[i] / 255) * (pixels[j + 3] / 255);
            if (t <= 0)
                continue;
            const da = d[j + 3] / 255, oa = t + da * (1 - t);
            for (let c = 0; c < 3; ++c)
                d[j + c] = oa > 0 ? Math.round((pixels[j + c] * t + d[j + c] * da * (1 - t)) / oa) : 0;
            d[j + 3] = Math.round(oa * 255);
        } else if (!where || where(i)) {
            d[j] = pixels[j]; d[j + 1] = pixels[j + 1]; d[j + 2] = pixels[j + 2]; d[j + 3] = pixels[j + 3];
        }
    }
    layer.ctx.putImageData(after, rect.x, rect.y);
    doc.pixelsChanged(layer, rect);
    doc.pushPixels(layer, rect, before, name, icon);
}

// ---------------------------------------------------------------------------------------------
// Pinceles correctores: el trazo es el del pincel y al soltar se cura en segundo plano.

class HealStrokeTool extends StrokeTool {
    get extra() { return true; }
    get letter() { return "J"; }

    cursor() {
        return this.job ? "wait" : super.cursor();
    }

    setJob(job) {
        this.job = job;
        this.view.updateCursor();
    }

    onPointerDown(e) {
        if (this.job)
            return;
        super.onPointerDown(e);
    }

    onPointerMove(e) {
        if (this.job)
            return;
        super.onPointerMove(e);
    }

    onPointerUp(e) {
        if (!this.stroke || this.job)
            return;
        if (e)
            this.stroke.lineTo(e.x, e.y, this.size(e));
        this.release();
    }

    release() {
        const stroke = this.stroke;
        stroke.finishPath();
        const plan = this.plan(stroke);
        if (!plan) {
            stroke.cancel();
            this.stroke = null;
            this.repaint();
            return;
        }
        const job = runRetouch(plan.op, plan.args);
        this.setJob(job);
        job.promise.then((pixels) => {
            if (this.job !== job)
                return;
            this.setJob(null);
            this.stroke = null;
            if (!pixels) {
                stroke.cancel();
                this.repaint();
                return;
            }
            stroke.applyThroughCoverage(plan.rect, pixels, plan.opacity ?? 255);
            stroke.commit(this.name, this.icon);
            this.repaint();
        }).catch((err) => {
            console.error(err);
            this.setJob(null);
            this.stroke = null;
            stroke.cancel();
        });
    }

    isBusy() {
        return !!this.stroke || !!this.job;
    }

    // Lo pendiente se cancela (en la web no se puede esperar al hilo).
    finish() {
        if (this.job)
            this.cancelJob();
        else if (this.stroke)
            this.release();
    }

    cancelJob() {
        const job = this.job;
        this.setJob(null);
        job?.cancel();
        if (this.stroke) {
            this.stroke.cancel();
            this.stroke = null;
        }
        this.repaint();
    }

    onKeyDown(e) {
        if (e.key === "Escape" && (this.stroke || this.job)) {
            this.cancelJob();
            return true;
        }
        return false;
    }
}

export class HealingBrushTool extends HealStrokeTool {
    get id() { return "healingbrush"; }
    get name() { return tr("Pincel corrector"); }
    get icon() { return "healingbrush"; }
    get options() {
        return ["brushSize", "hardness", "spacing", "healAligned", "retouchSampling", "smoothing", "pressure", "antialias", "blend",
                "selectionQuality"];
    }

    hint() {
        return this.source
            ? tr("Pinta sobre lo que quieras arreglar: al soltar, la textura del origen toma el color y la luz de alrededor. Ctrl+clic: otro origen.")
            : tr("Ctrl+clic para elegir de dónde tomar la textura y luego pinta sobre lo que quieras arreglar.");
    }

    onPointerDown(e) {
        if (!this.doc || this.job)
            return;
        if (e.ctrl) {
            this.source = { x: Math.floor(e.x), y: Math.floor(e.y) };
            this.offset = null;
            this.setHint(this.hint());
            this.repaint();
            return;
        }
        if (!this.source) {
            // Sin origen no hay de dónde tomar la textura: se avisa junto al puntero (la barra de
            // estado sola pasaba desapercibida).
            this.notice(tr("Primero Ctrl+clic para elegir de dónde tomar la textura."));
            return;
        }
        if (e.button !== "left" && e.button !== "right")
            return;
        // Alineado: el desplazamiento se queda entre trazos; si no, cada trazo vuelve al origen.
        if (!this.offset || !this.settings.healAligned)
            this.offset = { x: this.source.x - Math.floor(e.x), y: this.source.y - Math.floor(e.y) };
        this.sample = this.settings.retouchSampleImage ? this.doc.flattened() : null;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, {
            mode: "clone", color: this.colorFor(e.button), blend: this.settings.blend, antialias: this.settings.antialias,
            hardness: this.settings.hardness, spacing: this.settings.spacing, smoothing: !!this.settings.smoothing,
            cloneOffset: this.offset, cloneSource: this.sample || this.doc.activeLayer.canvas,
        });
        this.stroke.lineTo(e.x, e.y, this.size(e));
    }

    plan(stroke) {
        const doc = this.doc, W = doc.width, H = doc.height;
        const covered = stroke.dirty && coveredBounds(stroke.mask, W, stroke.dirty);
        if (!covered)
            return null;
        // El trazo y dos píxeles alrededor (el borde de la solución), dentro de la capa.
        const x0 = Math.max(covered.x - 2, 0), y0 = Math.max(covered.y - 2, 0);
        const rect = { x: x0, y: y0, w: Math.min(covered.x + covered.w + 2, W) - x0, h: Math.min(covered.y + covered.h + 2, H) - y0 };
        if (rect.w * rect.h > MAX_PIXELS) {
            this.setHint(tooLarge());
            return null;
        }
        const from = this.sample || stroke.start;
        const src = pixelsOf(from, { ...rect, x: rect.x + this.offset.x, y: rect.y + this.offset.y });
        const dst = pixelsOf(this.sample || stroke.start, rect);
        const cov = coverageOf(stroke.mask, W, H, rect);
        return { op: "heal", rect, opacity: stroke.color?.a ?? 255, args: { w: rect.w, h: rect.h, src, dst, cov } };
    }

    paintOverlay(ctx, view) {
        if (!this.source || !this.doc)
            return;
        let p = { x: this.source.x + 0.5, y: this.source.y + 0.5 };
        if (this.offset && view.mouse)
            p = { x: view.mouse.x + this.offset.x, y: view.mouse.y + this.offset.y };
        const v = view.imageToView(p.x, p.y);
        for (const [color, width] of [["#fff", 3], ["#000", 1]]) {
            ctx.strokeStyle = color;
            ctx.lineWidth = width;
            ctx.beginPath();
            ctx.moveTo(v.x - 8, v.y); ctx.lineTo(v.x + 8, v.y); ctx.moveTo(v.x, v.y - 8); ctx.lineTo(v.x, v.y + 8);
            ctx.stroke();
        }
    }
}

export class SpotHealingTool extends HealStrokeTool {
    get id() { return "spothealing"; }
    get name() { return tr("Pincel corrector puntual"); }
    get icon() { return "spothealing"; }
    get options() {
        return ["brushSize", "hardness", "spacing", "spotType", "retouchSampling", "smoothing", "pressure", "antialias", "selectionQuality"];
    }

    hint() {
        return tr("Pinta sobre una mancha: al soltar se rellena con lo de alrededor y se funde con su color.");
    }

    strokeOptions(e) {
        return { ...super.strokeOptions(e), mode: "mask", blend: "normal" };
    }

    onPointerDown(e) {
        super.onPointerDown(e);
        this.repaint();
    }

    onPointerMove(e) {
        super.onPointerMove(e);
        if (this.stroke)
            this.repaint();
    }

    plan(stroke) {
        const doc = this.doc, W = doc.width, H = doc.height;
        const covered = stroke.dirty && coveredBounds(stroke.mask, W, stroke.dirty);
        if (!covered)
            return null;
        const win = spotWindow(toBox(covered), box(0, 0, W, H));
        if (boxArea(win) > MAX_PIXELS) {
            this.setHint(tooLarge());
            return null;
        }
        const rect = toRect(win);
        const img = pixelsOf(this.settings.retouchSampleImage ? doc.flattened() : stroke.start, rect);
        return { op: "spotHeal", rect, args: { w: rect.w, h: rect.h, img, cov: coverageOf(stroke.mask, W, H, rect),
                                                kind: this.settings.spotType, size: this.settings.brushSize } };
    }

    // El trazo se ve como un rastro negro al 50 % (lo que se va a curar).
    paintOverlay(ctx, view) {
        const s = this.stroke;
        if (!s?.dirty)
            return;
        drawOver(ctx, view, s.dirty, tintCanvas(s.mask, s.width, s.dirty, [0, 0, 0], 128));
    }
}

// ---------------------------------------------------------------------------------------------
// Quitar.

export class RemoveTool extends StrokeTool {
    get id() { return "remove"; }
    get name() { return tr("Quitar"); }
    get icon() { return "remove"; }
    get letter() { return "J"; }
    get extra() { return true; }
    get options() {
        return ["brushSize", "hardness", "spacing", "removeEach", "retouchSampling", "smoothing", "pressure", "antialias"];
    }

    hint() {
        return this.settings.removeEachStroke
            ? tr("Pinta encima o alrededor de lo que quieras quitar: al soltar se rellena con lo de alrededor.")
            : tr("Pinta encima o alrededor de lo que quieras quitar; Intro lo quita (Esc descarta los trazos).");
    }

    cursor() {
        return this.job ? "wait" : super.cursor();
    }

    strokeOptions(e) {
        return { ...super.strokeOptions(e), mode: "mask", blend: "normal" };
    }

    onPointerDown(e) {
        if (this.job)
            return;
        super.onPointerDown(e);
        this.repaint();
    }

    onPointerMove(e) {
        super.onPointerMove(e);
        if (this.stroke)
            this.repaint();
    }

    // El trazo terminado pasa a los reunidos.
    collect() {
        const s = this.stroke;
        if (!s)
            return;
        this.stroke = null;
        s.finishPath();
        s.cancel();
        const doc = this.doc;
        if (!this.pending || this.pendingDoc !== doc || this.pending.length !== doc.width * doc.height) {
            this.pending = new Uint8Array(doc.width * doc.height);
            this.pendingRect = null;
            this.pendingDoc = doc;
        }
        const r = s.dirty && coveredBounds(s.mask, doc.width, s.dirty);
        if (!r)
            return;
        for (let y = r.y; y < r.y + r.h; ++y)
            for (let x = r.x; x < r.x + r.w; ++x) {
                const i = y * doc.width + x;
                this.pending[i] = Math.max(this.pending[i], s.mask[i]);
            }
        this.pendingRect = this.pendingRect
            ? toRect({ x0: Math.min(this.pendingRect.x, r.x), y0: Math.min(this.pendingRect.y, r.y),
                       x1: Math.max(this.pendingRect.x + this.pendingRect.w, r.x + r.w), y1: Math.max(this.pendingRect.y + this.pendingRect.h, r.y + r.h) })
            : r;
    }

    onPointerUp(e) {
        if (!this.stroke)
            return;
        if (e)
            this.stroke.lineTo(e.x, e.y, this.size(e));
        this.collect();
        if (this.settings.removeEachStroke)
            this.run();
        this.repaint();
    }

    drop() {
        this.pending = null;
        this.pendingRect = null;
        this.pendingDoc = null;
        this.repaint();
    }

    run() {
        const doc = this.pendingDoc, rect0 = this.pendingRect;
        if (!doc || !rect0 || this.job || doc !== this.doc) {
            this.drop();
            return;
        }
        if (rect0.w * rect0.h > MAX_PIXELS) {
            this.setHint(tr("La zona es demasiado grande para quitarla."));
            this.drop();
            return;
        }
        const win = removeWindow(toBox(rect0), box(0, 0, doc.width, doc.height));
        if (boxArea(win) <= 0) {
            this.drop();
            return;
        }
        const rect = toRect(win), image = !!this.settings.retouchSampleImage;
        const img = pixelsOf(sourceCanvas(doc, image), rect);
        const strokes = coverageOf(this.pending, doc.width, doc.height, rect);
        const clip = doc.selection.isEmpty() ? null : coverageOf(doc.selection.mask, doc.width, doc.height, rect);
        const layer = doc.activeLayer;
        this.drop();
        const job = runRetouch("remove", { w: rect.w, h: rect.h, img, strokes, clip, image });
        this.job = job;
        this.view.updateCursor();
        job.promise.then((out) => {
            if (this.job !== job)
                return;
            this.job = null;
            this.view.updateCursor();
            if (!out || !doc.layers.includes(layer))
                return;
            writePixels(doc, layer, rect, out.pixels, this.name, this.icon, { where: (i) => out.hole[i] });
        }).catch((err) => {
            console.error(err);
            this.job = null;
            this.view.updateCursor();
        });
    }

    isBusy() {
        return !!this.stroke || !!this.pending || !!this.job;
    }

    finish() {
        this.collect();
        if (!this.job && this.pending)
            this.run();
    }

    onKeyDown(e) {
        if (e.key === "Escape") {
            if (this.job) {
                const job = this.job;
                this.job = null;
                job.cancel();
                this.view.updateCursor();
                return true;
            }
            if (this.stroke) {
                this.stroke.cancel();
                this.stroke = null;
                this.repaint();
                return true;
            }
            if (this.pending) {
                this.drop();
                return true;
            }
            return false;
        }
        if (e.key === "Enter" && this.pending) {
            this.run();
            return true;
        }
        return false;
    }

    // Los trazos se ven con un tinte rojo al 50 %.
    paintOverlay(ctx, view) {
        if (this.pending && this.pendingRect)
            drawOver(ctx, view, this.pendingRect, tintCanvas(this.pending, this.doc.width, this.pendingRect, [255, 0, 0], 128));
        const s = this.stroke;
        if (s?.dirty)
            drawOver(ctx, view, s.dirty, tintCanvas(s.mask, s.width, s.dirty, [255, 0, 0], 128));
    }
}

// ---------------------------------------------------------------------------------------------
// Parche: se selecciona como con el lazo y se arrastra la selección.

export class PatchTool extends LassoSelectTool {
    get id() { return "patch"; }
    get name() { return tr("Parche"); }
    get icon() { return "patch"; }
    get letter() { return "J"; }
    get extra() { return true; }
    get options() {
        return ["selectionMode", "patch", "retouchSampling", "selectionQuality"];
    }

    hint() {
        if (this.settings.patchMode === "contentAware")
            return tr("Selecciona una zona y arrástrala hasta lo que la rellenará según el contenido.");
        return this.settings.patchDestination ? tr("Selecciona una zona y arrástrala hasta lo que quieras reparar con ella.")
            : tr("Selecciona lo que quieras reparar y arrástralo hasta la textura que lo sustituirá.");
    }

    cursor() {
        return this.job ? "wait" : this.dragging ? "move" : super.cursor();
    }

    offset() {
        return [Math.round(this.pointer.x - this.grab.x), Math.round(this.pointer.y - this.grab.y)];
    }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || this.job)
            return;
        const sel = doc.selection, px = Math.floor(e.x), py = Math.floor(e.y);
        // Un arrastre con el botón izquierdo y sin teclas dentro de la selección la parchea.
        if (e.button === "left" && !e.ctrl && !e.alt && !e.shift && !sel.isEmpty() && px >= 0 && py >= 0 && px < doc.width
            && py < doc.height && sel.value(px, py) > 0) {
            this.dragging = true;
            this.grab = { x: e.x, y: e.y };
            this.pointer = { x: e.x, y: e.y };
            this.preview = null;
            this.view.updateCursor();
            return;
        }
        super.onPointerDown(e);
    }

    input(offset) {
        const doc = this.doc, s = this.settings, sel = doc.selection;
        if (sel.isEmpty())
            return { refusal: "empty" };
        const mode = s.patchMode === "contentAware" ? "contentAware" : s.patchDestination ? "destination" : "source";
        const b = sel.bounds();
        if (!b)
            return { refusal: "empty" };
        const res = planPatch(box(b.x, b.y, b.x + b.w, b.y + b.h), box(0, 0, doc.width, doc.height), offset, mode);
        if (!res.plan)
            return res;
        const plan = res.plan, rect = toRect(plan.rect);
        const src = sourceCanvas(doc, !!s.retouchSampleImage);
        return {
            mode, plan, rect, structure: s.patchStructure, color: s.patchColor,
            here: pixelsOf(src, rect),
            texture: pixelsOf(src, { ...rect, x: rect.x + plan.srcShift[0], y: rect.y + plan.srcShift[1] }),
            coverage: coverageOf(sel.mask, doc.width, doc.height, { ...rect, x: rect.x - plan.dstShift[0], y: rect.y - plan.dstShift[1] }),
        };
    }

    onPointerMove(e) {
        if (this.dragging) {
            this.pointer = { x: e.x, y: e.y };
            clearTimeout(this.previewTimer);
            this.previewTimer = setTimeout(() => this.updatePreview(), 40);
            this.repaint();
            return;
        }
        super.onPointerMove(e);
    }

    updatePreview() {
        this.preview = null;
        if (!this.dragging || !this.doc)
            return;
        const input = this.input(this.offset());
        if (!input.plan)
            return;
        const { rect } = input;
        let pixels;
        if (input.mode === "contentAware") {
            pixels = new Uint8ClampedArray(input.texture);
        } else {
            // La curación aproximada (la membrana más gruesa).
            pixels = healCoarse(rect.w, rect.h, input.texture, input.here, input.coverage, patchPreviewStep(input.plan.rect, 4096));
        }
        for (let i = 0; i < input.coverage.length; ++i)
            pixels[i * 4 + 3] = Math.round((pixels[i * 4 + 3] * input.coverage[i]) / 255);
        const canvas = createCanvas(rect.w, rect.h);
        context(canvas).putImageData(new ImageData(pixels, rect.w, rect.h), 0, 0);
        this.preview = { rect, canvas };
        this.repaint();
    }

    cancelDrag() {
        this.dragging = false;
        this.preview = null;
        clearTimeout(this.previewTimer);
        this.view.updateCursor();
        this.repaint();
    }

    onPointerUp(e) {
        if (!this.dragging) {
            super.onPointerUp(e);
            return;
        }
        if (e)
            this.pointer = { x: e.x, y: e.y };
        const offset = this.offset();
        this.cancelDrag();
        const doc = this.doc;
        if (!doc)
            return;
        const input = this.input(offset);
        if (!input.plan) {
            const messages = {
                tooLarge: tr("La selección es demasiado grande para el parche."),
                noOffset: tr("Arrastra la selección hasta la zona de la que tomar."),
                offCanvas: tr("El parche arrastrado tiene que quedar dentro de la imagen."),
            };
            if (messages[input.refusal])
                this.setHint(messages[input.refusal]);
            return;
        }
        const { rect } = input, layer = doc.activeLayer;
        const job = runRetouch("patch", { mode: input.mode, w: rect.w, h: rect.h, here: input.here, texture: input.texture,
                                          coverage: input.coverage, structure: input.structure, color: input.color });
        this.job = job;
        this.view.updateCursor();
        job.promise.then((pixels) => {
            if (this.job !== job)
                return;
            this.job = null;
            this.view.updateCursor();
            if (!pixels || !doc.layers.includes(layer))
                return;
            if (input.mode === "contentAware")
                writePixels(doc, layer, rect, pixels, this.name, this.icon, { where: (i) => input.coverage[i] > 0 });
            else
                writePixels(doc, layer, rect, pixels, this.name, this.icon, { through: input.coverage });
        }).catch((err) => {
            console.error(err);
            this.job = null;
            this.view.updateCursor();
        });
    }

    isBusy() {
        return !!this.dragging || !!this.job || super.isBusy();
    }

    finish() {
        if (this.dragging)
            this.cancelDrag();
        super.finish();
    }

    onKeyDown(e) {
        if (e.key === "Escape") {
            if (this.job) {
                const job = this.job;
                this.job = null;
                job.cancel();
                this.view.updateCursor();
                return true;
            }
            if (this.dragging) {
                this.cancelDrag();
                return true;
            }
        }
        return false;
    }

    paintOverlay(ctx, view) {
        if (!this.dragging || !this.preview)
            return;
        drawOver(ctx, view, this.preview.rect, this.preview.canvas);
    }
}

// ---------------------------------------------------------------------------------------------
// Lazo magnético: un borde de selección que se pega a los bordes de la imagen.

const DOUBLE_CLICK = [500, 4];
const CLOSE_RADIUS = 8;
const STEPS_PER_FRAME = 4;
const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);

// La polilínea 'p' hasta 'len' a lo largo y el punto, si queda más de un píxel después.
function cutAt(p, len) {
    if (polylineLength(p) < len + 1)
        return null;
    const head = [];
    let acc = 0;
    for (let i = 0; i + 1 < p.length; ++i) {
        const a = p[i], b = p[i + 1];
        head.push(a);
        const d = dist(a, b);
        if (acc + d >= len && d > 0) {
            const t = (len - acc) / d, point = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];
            head.push(point);
            return [head, point];
        }
        acc += d;
    }
    return null;
}

export class MagneticLassoTool extends SelectTool {
    get id() { return "magneticlasso"; }
    get name() { return tr("Lazo magnético"); }
    get icon() { return "magneticlasso"; }
    get letter() { return "S"; }
    get extra() { return true; }
    get options() {
        return ["selectionMode", "magnetic", "afterSelect", "selectionQuality"];
    }

    constructor(app) {
        super(app);
        this.reset();
    }

    reset() {
        this.path = [];
        this.anchors = [];
        this.live = [];
        this.trail = [];
        this.pending = [];
        this.freehand = false;
        this.pressed = null;
        this.opening = false;
        this.pressDragged = false;
        this.tracer = null;
        cancelAnimationFrame(this.frame);
        this.frame = 0;
    }

    hint() {
        if (!this.path.length)
            return tr("Haz clic en un borde y sigue el contorno: el borde de la selección se pega a él. [ y ]: anchura de detección.");
        return tr("Clic: fijar un punto · Alt+clic: tramo recto · Retroceso: quitar el último punto · doble clic, clic en el inicio o Intro: cerrar · Esc: cancelar.");
    }

    get magnetic() {
        return magneticSettings(this.settings.magneticWidth, this.settings.magneticContrast / 100);
    }

    spacing() {
        const f = Math.min(Math.max(Number(this.settings.magneticFrequency) || 0, 0), 100);
        return (8 + (100 - f) * 0.9) / Math.max(this.view.scale, 1e-6);
    }

    inside(p) {
        return [Math.min(Math.max(p[0], 0), this.doc.width), Math.min(Math.max(p[1], 0), this.doc.height)];
    }

    snap(p) {
        return this.tracer ? this.tracer.snap(p, this.magnetic) || p : p;
    }

    trace(from, to, guide) {
        const t = this.tracer ? this.tracer.trace(from, to, guide, this.magnetic) : [];
        return t.length ? t : [from, to];
    }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || (e.button !== "left" && e.button !== "right") || this.pressed)
            return;
        const p = [e.x, e.y];
        this.pressed = { p, alt: e.alt };
        this.pressDragged = false;
        this.pointer = p;
        this.catchUp(Infinity);
        const now = performance.now();
        if (!this.path.length) {
            // El primer punto de anclaje; las teclas del lazo eligen cómo se combina.
            this.begin(e);
            const layer = doc.activeLayer, W = doc.width, H = doc.height;
            const data = layer.ctx.getImageData(0, 0, W, H).data;
            this.tracer = new MagneticTracer(box(0, 0, W, H), (r) => {
                const w = boxW(r), out = new Uint8ClampedArray(w * boxH(r) * 4);
                for (let y = r.y0; y < r.y1; ++y)
                    out.set(data.subarray((y * W + r.x0) * 4, (y * W + r.x1) * 4), (y - r.y0) * w * 4);
                return out;
            });
            this.path = [this.snap(p)];
            this.anchors = [0];
            this.trail = [this.inside(p)];
            this.live = [];
            this.lastPress = { t: now, p };
            this.opening = true;
            this.setHint(this.hint());
            this.show();
            return;
        }
        const scale = Math.max(this.view.scale, 1e-6);
        const dbl = this.lastPress && now - this.lastPress.t <= DOUBLE_CLICK[0] && dist(p, this.lastPress.p) * scale <= DOUBLE_CLICK[1];
        this.lastPress = { t: now, p };
        if (dbl || this.nearStart(p)) {
            this.close(e.alt, !dbl);
            return;
        }
        if (e.alt) {
            // Alt+clic: un tramo recto hasta aquí; si se arrastra, a mano alzada.
            if (!this.path.length || dist(this.path[this.path.length - 1], p) > 1e-9) {
                this.path.push(p);
                this.anchors.push(this.path.length - 1);
            }
            this.live = [];
            this.trail = [this.inside(p)];
            this.freehand = true;
            this.show();
            return;
        }
        // Un clic fija un punto en el borde junto al puntero.
        this.follow(p, false);
        this.fastenLive();
        this.show();
    }

    queue(p) {
        const last = this.pointer;
        this.pointer = p;
        if (last[0] === p[0] && last[1] === p[1])
            return;
        const step = Math.max(this.spacing() / 2, 1);
        // Un salto largo se recorre a pasos, así se siguen fijando puntos por el camino.
        const n = Math.min(Math.max(Math.ceil(dist(last, p) / step), 1), 256);
        for (let k = 1; k <= n; ++k) {
            const t = k / n;
            this.pending.push([last[0] + (p[0] - last[0]) * t, last[1] + (p[1] - last[1]) * t]);
        }
        if (!this.frame) {
            const tick = () => {
                this.frame = 0;
                if (!this.pending.length || !this.path.length)
                    return;
                this.catchUp(STEPS_PER_FRAME);
                this.show();
                if (this.pending.length)
                    this.frame = requestAnimationFrame(tick);
            };
            this.frame = requestAnimationFrame(tick);
        }
    }

    catchUp(budget) {
        while (budget > 0 && this.pending.length) {
            const q = this.pending.shift();
            if (!this.freehand) {
                this.follow(q, true);
                --budget;
                continue;
            }
            // Alt+arrastre: el camino del puntero es el borde.
            const last = this.path[this.path.length - 1];
            if (!last || dist(last, q) >= 0.5)
                this.path.push(q);
            this.trail = [this.inside(q)];
        }
    }

    extendTrail(q) {
        const gap = Math.min(Math.max(this.magnetic.width / 4, 1), 8), t = this.trail, n = t.length;
        if (n >= 2 && dist(t[n - 2], q) < gap)
            t[n - 1] = q;
        else
            t.push(q);
    }

    follow(q0, fasten) {
        const q = this.inside(q0);
        this.extendTrail(q);
        const target = this.snap(q), every = this.spacing();
        for (let i = 0; i < 64; ++i) {
            if (!this.path.length)
                return;
            const from = this.path[this.path.length - 1];
            const live = this.trace(from, target, this.trail.slice());
            // La parte del borde a 'every' de su inicio ya no cambia al mover el puntero: se fija.
            const cut = fasten ? cutAt(live, every) : null;
            if (!cut) {
                this.live = live;
                return;
            }
            this.path.push(...cut[0].slice(1));
            this.anchors.push(this.path.length - 1);
            // Los bordes se buscan por el camino del puntero desde donde pasó junto al punto nuevo.
            let nearest = 0, best = Infinity;
            this.trail.forEach((t, k) => {
                const d = dist(t, cut[1]);
                if (d < best) {
                    best = d;
                    nearest = k;
                }
            });
            this.trail = [cut[1], ...this.trail.slice(nearest + 1)];
        }
    }

    fastenLive() {
        const before = this.path.length;
        for (const q of this.live.slice(1)) {
            const last = this.path[this.path.length - 1];
            if (!last || dist(last, q) > 1e-9)
                this.path.push(q);
        }
        this.live = [];
        if (this.path.length > before)
            this.anchors.push(this.path.length - 1);
        this.trail = this.path.length ? [this.path[this.path.length - 1]] : [];
    }

    endFreehand() {
        const was = this.freehand;
        this.freehand = false;
        if (was && this.anchors.length && this.anchors[this.anchors.length - 1] + 1 < this.path.length)
            this.anchors.push(this.path.length - 1);
    }

    nearStart(p) {
        const r = CLOSE_RADIUS / Math.max(this.view.scale, 1e-6);
        return this.path.length && dist(this.path[0], p) <= r && polylineLength(this.path) + polylineLength(this.live) > 2 * r;
    }

    close(straight, atStart) {
        this.catchUp(Infinity);
        this.endFreehand();
        let guide = [];
        if (atStart) {
            this.live = [];
            guide = this.trail;
            this.trail = [];
        } else {
            this.fastenLive();
        }
        if (!straight && this.path.length) {
            const closing = this.trace(this.path[this.path.length - 1], this.path[0], guide);
            closing.pop();
            this.path.push(...closing.slice(1));
        }
        this.commitBorder();
    }

    polygon() {
        return [...this.path, ...this.live.slice(1), ...this.pending];
    }

    shapeOf(pts) {
        return Selection.fromPath(this.doc.width, this.doc.height, (c) => {
            pts.forEach(([x, y], i) => (i ? c.lineTo(x, y) : c.moveTo(x, y)));
            c.closePath();
        }, this.settings.selectionAntialias);
    }

    show() {
        if (!this.doc || !this.path.length)
            return;
        const pts = this.polygon();
        if (pts.length >= 3)
            this.preview(this.shapeOf(pts));
        this.repaint();
    }

    commitBorder() {
        const doc = this.doc, pts = this.polygon(), mode = this.mode, plain = this.plain;
        this.reset();
        if (!doc || !this.active)
            return;
        this.active = false;
        if (pts.length < 3) {
            doc.setSelection(this.before);
            this.repaint();
            return;
        }
        const shape = this.shapeOf(pts);
        doc.setSelection(mode === "replace" ? shape : this.before.combined(shape, mode));
        doc.pushSelection(this.before, this.name, this.icon);
        this.app.updateStatus();
        this.setHint(this.hint());
        if (mode === "replace" && plain)
            this.selectionFinished();
        this.repaint();
    }

    cancel() {
        const was = this.path.length > 0;
        this.reset();
        if (was && this.active && this.doc)
            this.doc.setSelection(this.before);
        this.active = false;
        this.setHint(this.hint());
        this.repaint();
    }

    removeLastPoint() {
        this.endFreehand();
        if (this.anchors.length <= 1) {
            this.cancel();
            return;
        }
        this.anchors.pop();
        const keep = this.anchors.length ? this.anchors[this.anchors.length - 1] + 1 : 1;
        this.path.length = Math.min(this.path.length, keep);
        this.live = [];
        this.trail = this.path.length ? [this.path[this.path.length - 1]] : [];
        // El tramo vivo vuelve al puntero; solo se fija al moverlo.
        this.follow(this.pointer, false);
    }

    onPointerMove(e) {
        if (!this.path.length)
            return;
        const p = [e.x, e.y];
        const far = DOUBLE_CLICK[1] / Math.max(this.view.scale, 1e-6);
        if (this.pressed && dist(this.pressed.p, p) > far)
            this.pressDragged = true;
        this.queue(p);
        this.show();
    }

    onPointerUp(e) {
        if (!this.pressed)
            return;
        if (e)
            this.onPointerMove(e);
        const alt = this.pressed.alt;
        this.pressed = null;
        this.opening = false;
        const dragged = this.pressDragged;
        this.pressDragged = false;
        if (dragged && this.path.length) {
            // Tras arrastrar, al soltar se cierra el borde, como con el lazo (recto con Alt, si no se
            // pulsó ya al empezar).
            this.close(!!e?.alt && !alt, false);
            return;
        }
        this.catchUp(Infinity);
        this.endFreehand();
        this.show();
    }

    onKeyDown(e) {
        if (e.ctrlKey || e.metaKey)
            return false;
        if (e.key === "[" || e.key === "]") {
            const step = e.key === "[" ? -1 : 1;
            AppState.updateTools({ magneticWidth: Math.min(Math.max(Math.round(this.settings.magneticWidth) + step, 1), 256) });
            return true;
        }
        if (!this.path.length)
            return false;
        switch (e.key) {
        case "Enter": this.close(e.altKey, false); return true;
        case "Escape": this.cancel(); return true;
        case "Backspace":
        case "Delete":
            this.catchUp(Infinity);
            this.removeLastPoint();
            this.show();
            return true;
        default: return false;
        }
    }

    isBusy() {
        return this.path.length > 0;
    }

    finish() {
        this.cancel();
    }

    // El borde abierto (lo fijado y el tramo vivo) con las mismas hormigas que la selección: antes,
    // un punteado azul fijo encima de ellas, y el contorno se veía doble y cortado.
    paintOverlay(ctx, view) {
        if (!this.path.length)
            return;
        const path = new Path2D();
        this.polygon().forEach(([x, y], i) => {
            const v = view.imageToView(x, y);
            i ? path.lineTo(v.x, v.y) : path.moveTo(v.x, v.y);
        });
        view.strokeAnts(ctx, path);
    }
}

// ---------------------------------------------------------------------------------------------
// Selección de objetos: un recuadro alrededor de un objeto, o el objeto bajo el puntero.

export class ObjectSelectionTool extends SelectTool {
    get id() { return "objectselection"; }
    get name() { return tr("Selección de objetos"); }
    get icon() { return "objectselection"; }
    get letter() { return "S"; }
    get extra() { return true; }
    get options() {
        return ["selectionMode", "objectFinder", "retouchSampling", "afterSelect", "selectionQuality"];
    }

    hint() {
        return tr("Rodea un objeto con un recuadro, o haz clic en el que se resalta, para seleccionarlo.") + " " + selectHint();
    }

    cursor() {
        return this.job ? "wait" : super.cursor();
    }

    // La imagen que se analiza (la capa o la imagen), en bytes RGBA.
    pixels() {
        const doc = this.doc, src = sourceCanvas(doc, !!this.settings.retouchSampleImage);
        return context(src).getImageData(0, 0, doc.width, doc.height).data;
    }

    start(op, args, onDone, finder = false) {
        const job = runRetouch(op, { ...args, pixels: this.pixels(), width: this.doc.width, height: this.doc.height });
        if (finder) {
            this.finder?.cancel();
            this.finder = job;
        } else {
            this.job = job;
            this.view.updateCursor();
        }
        job.promise.then((region) => {
            if (finder ? this.finder !== job : this.job !== job)
                return;
            if (finder)
                this.finder = null;
            else {
                this.job = null;
                this.view.updateCursor();
            }
            onDone(region);
        }).catch((err) => {
            console.error(err);
            if (!finder) {
                this.job = null;
                this.view.updateCursor();
            }
        });
    }

    apply(region, mode, before, plain) {
        const doc = this.doc;
        if (!doc)
            return;
        // La cobertura de 128 o más, con bordes de píxel.
        const W = doc.width, H = doc.height, mask = new Uint8Array(W * H), w = boxW(region.box);
        for (let y = Math.max(region.box.y0, 0); y < Math.min(region.box.y1, H); ++y)
            for (let x = Math.max(region.box.x0, 0); x < Math.min(region.box.x1, W); ++x)
                if (region.mask[(y - region.box.y0) * w + (x - region.box.x0)] >= 128)
                    mask[y * W + x] = 255;
        const shape = Selection.fromMask(W, H, mask);
        doc.setSelection(mode === "replace" ? shape : before.combined(shape, mode));
        doc.pushSelection(before, this.name, this.icon);
        this.app.updateStatus();
        if (mode === "replace" && plain)
            this.selectionFinished();
        this.repaint();
    }

    onPointerDown(e) {
        if (!this.doc || this.job || (e.button !== "left" && e.button !== "right"))
            return;
        this.pressed = { x: e.x, y: e.y, vx: e.vx, vy: e.vy, mode: this.modeFor(e), plain: !e.ctrl && !e.alt };
        this.end = { x: e.x, y: e.y };
        this.boxDragged = false;
        clearTimeout(this.hoverTimer);
    }

    onPointerMove(e) {
        if (this.pressed) {
            this.end = { x: e.x, y: e.y };
            if (Math.hypot(e.vx - this.pressed.vx, e.vy - this.pressed.vy) > 2)
                this.boxDragged = true;
            this.repaint();
            return;
        }
        this.hover = { x: e.x, y: e.y };
        clearTimeout(this.hoverTimer);
        if (this.settings.objectFinder && !this.job)
            this.hoverTimer = setTimeout(() => this.lookupHover(), 150);
        else if (this.found) {
            this.found = null;
            this.repaint();
        }
    }

    onPointerUp(e) {
        const press = this.pressed;
        if (!press)
            return;
        this.pressed = null;
        const doc = this.doc;
        if (!doc)
            return;
        if (e)
            this.end = { x: e.x, y: e.y };
        const before = doc.selection;
        const done = (region) => {
            if (!region) {
                this.setHint(tr("No se encontró ningún objeto."));
                return;
            }
            this.apply(region, press.mode, before, press.plain);
        };
        if (this.boxDragged) {
            // El recuadro, con los píxeles de los dos extremos dentro.
            const a = press, b = this.end;
            const rect = [Math.floor(Math.min(a.x, b.x)), Math.floor(Math.min(a.y, b.y)), Math.floor(Math.max(a.x, b.x)) + 1,
                          Math.floor(Math.max(a.y, b.y)) + 1];
            this.found = null;
            this.start("objectInRect", { rect, workPx: OBJECT_AT_WORK_PX }, done);
            return;
        }
        const px = Math.floor(this.end.x), py = Math.floor(this.end.y), f = this.found;
        // Un clic sobre el objeto resaltado lo selecciona.
        if (f && px >= f.box.x0 && px < f.box.x1 && py >= f.box.y0 && py < f.box.y1) {
            this.found = null;
            this.apply(f, press.mode, before, press.plain);
            return;
        }
        this.found = null;
        this.start("objectAt", { x: this.end.x, y: this.end.y, workPx: OBJECT_AT_WORK_PX }, done);
    }

    lookupHover() {
        const doc = this.doc, h = this.hover;
        if (!doc || this.pressed || this.job || !this.settings.objectFinder || !h)
            return;
        const px = Math.floor(h.x), py = Math.floor(h.y);
        if (px < 0 || py < 0 || px >= doc.width || py >= doc.height) {
            if (this.found) {
                this.found = null;
                this.repaint();
            }
            return;
        }
        // El objeto resaltado sigue siéndolo mientras el puntero esté dentro.
        const f = this.found;
        if (f && px >= f.box.x0 && px < f.box.x1 && py >= f.box.y0 && py < f.box.y1
            && f.mask[(py - f.box.y0) * boxW(f.box) + (px - f.box.x0)] >= 128)
            return;
        this.start("objectAt", { x: h.x, y: h.y, workPx: OBJECT_AT_WORK_PX }, (region) => {
            if (this.pressed)
                return;
            this.found = region;
            this.foundCanvas = null;
            if (region) {
                // Relleno azul al 50 % con su contorno opaco.
                const w = boxW(region.box), hh = boxH(region.box), canvas = createCanvas(w, hh), ctx = context(canvas);
                const img = ctx.createImageData(w, hh), d = img.data, on = (x, y) => x >= 0 && y >= 0 && x < w && y < hh
                    && region.mask[y * w + x] >= 128;
                for (let y = 0; y < hh; ++y)
                    for (let x = 0; x < w; ++x) {
                        if (!on(x, y))
                            continue;
                        const edge = !on(x - 1, y) || !on(x + 1, y) || !on(x, y - 1) || !on(x, y + 1), j = (y * w + x) * 4;
                        d[j] = 40; d[j + 1] = 110; d[j + 2] = 230; d[j + 3] = edge ? 255 : 128;
                    }
                ctx.putImageData(img, 0, 0);
                this.foundCanvas = canvas;
            }
            this.repaint();
        }, true);
    }

    selectSubject() {
        const doc = this.doc;
        if (!doc || this.job)
            return;
        const mode = this.settings.selectionMode, before = doc.selection;
        this.found = null;
        this.start("selectSubject", { workPx: SUBJECT_WORK_PX }, (region) => {
            if (!region) {
                this.setHint(tr("No se encontró un sujeto destacado."));
                return;
            }
            this.apply(region, mode, before, true);
        });
    }

    isBusy() {
        return !!this.pressed || !!this.job;
    }

    deactivate() {
        clearTimeout(this.hoverTimer);
        this.finder?.cancel();
        this.finder = null;
        this.found = null;
        super.deactivate();
    }

    finish() {}

    onKeyDown(e) {
        if (e.key !== "Escape")
            return false;
        if (this.job) {
            const job = this.job;
            this.job = null;
            job.cancel();
            this.view.updateCursor();
            return true;
        }
        if (this.pressed || this.found) {
            this.pressed = null;
            this.found = null;
            this.repaint();
            return true;
        }
        return false;
    }

    paintOverlay(ctx, view) {
        const f = this.found;
        if (f && this.foundCanvas && !this.pressed)
            drawOver(ctx, view, toRect(f.box), this.foundCanvas);
        if (this.pressed && this.boxDragged) {
            const a = view.imageToView(this.pressed.x, this.pressed.y), b = view.imageToView(this.end.x, this.end.y);
            ctx.save();
            for (const [color, dash] of [["#fff", []], ["rgb(30,80,160)", [3, 3]]]) {
                ctx.strokeStyle = color;
                ctx.lineWidth = 1;
                ctx.setLineDash(dash);
                ctx.strokeRect(Math.min(a.x, b.x) + 0.5, Math.min(a.y, b.y) + 0.5, Math.abs(b.x - a.x), Math.abs(b.y - a.y));
            }
            ctx.restore();
        }
    }
}

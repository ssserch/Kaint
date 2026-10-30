// Selección rectangular, elíptica, de lazo y varita mágica (src/app/tools/SelectionTools.cpp),
// con las combinaciones de Paint.NET: Ctrl + izq.: unión · Alt + izq.: restar · Ctrl + der.: XOR ·
// Alt + der.: intersección · sin teclas: el modo de la barra.
import { Tool, selectHint } from "./Tool.js";
import { drawMoveHandle, nearPoint } from "./EditableSteps.js";
import { AppState } from "../AppState.js";
import { Selection } from "../core/Selection.js";
import { floodMask } from "../core/FloodFill.js";
import { context } from "../core/Raster.js";
import { tr } from "../i18n.js";

class SelectTool extends Tool {
    get options() {
        return ["selectionMode", "afterSelect", "selectionQuality"];
    }

    get highlightSelection() {
        return true;
    }

    modeFor(e) {
        const right = e.button === "right";
        if (e.ctrl && !right) return "union";
        if (e.alt && !right) return "exclude";
        if (e.ctrl && right) return "xor";
        if (e.alt && right) return "intersect";
        return this.settings.selectionMode;
    }

    begin(e) {
        this.before = this.doc.selection;
        this.mode = this.modeFor(e);
        this.plain = !e.ctrl && !e.alt;
        this.active = true;
        this.dragged = false;
        this.startTime = performance.now();
    }

    preview(shape) {
        this.doc.setSelection(this.mode === "replace" ? shape : this.before.combined(shape, this.mode));
    }

    // Como Paint.NET: un clic (o un arrastre de menos de 50 ms) en modo reemplazar anula la
    // selección; en los otros modos no cambia nada.
    done(name, icon) {
        this.active = false;
        const quick = performance.now() - this.startTime <= 50;
        if (this.mode !== "replace") {
            if (!this.dragged)
                this.doc.setSelection(this.before);
        } else if (!this.dragged || quick) {
            this.doc.setSelection(Selection.none(this.doc.width, this.doc.height));
            this.doc.pushSelection(this.before, tr("Anular la selección"), "edit-select-none");
            return;
        }
        this.doc.pushSelection(this.before, name, icon);
        this.app.updateStatus();
        if (this.mode === "replace" && this.plain)
            this.selectionFinished();
    }

    // Como Kaint (Después de seleccionar): tras una selección nueva, cambia a una herramienta Mover.
    selectionFinished() {
        const after = this.settings.afterSelect;
        if (!after || after === "none" || this.doc.selection.isEmpty())
            return;
        setTimeout(() => {
            if (this.app.tools.current === this)
                this.app.tools.select(after);
        }, 0);
    }

    showStatus(r) {
        this.setHint(tr("Selección: %1, %2 · %3 × %4", Math.round(r.x), Math.round(r.y), Math.round(r.w), Math.round(r.h)));
    }

    isBusy() {
        return !!this.active;
    }

    finish() {
        if (this.active)
            this.done(this.name, this.icon);
    }
}

class ShapeSelectTool extends SelectTool {
    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.begin(e);
        this.start = { x: Math.round(e.x), y: Math.round(e.y) };
    }

    currentRect(e) {
        const s = this.settings;
        let end = { x: Math.round(e.x), y: Math.round(e.y) };
        if (!this.ellipse && s.rectMode === "fixedsize") {
            const w = Math.max(1, s.fixedWidth), h = Math.max(1, s.fixedHeight);
            return { x: Math.round(end.x - w / 2), y: Math.round(end.y - h / 2), w, h };
        }
        let ratio = 0;
        if (e.shift) ratio = 1;
        else if (!this.ellipse && s.rectMode === "fixedratio" && s.fixedHeight > 0) ratio = s.fixedWidth / s.fixedHeight;
        if (ratio > 0) {
            const dx = end.x - this.start.x, dy = end.y - this.start.y;
            const w = Math.max(Math.abs(dx), Math.abs(dy) * ratio), h = w / ratio;
            end = { x: Math.round(this.start.x + (dx >= 0 ? w : -w)), y: Math.round(this.start.y + (dy >= 0 ? h : -h)) };
        }
        return { x: Math.min(this.start.x, end.x), y: Math.min(this.start.y, end.y), w: Math.abs(end.x - this.start.x),
                 h: Math.abs(end.y - this.start.y) };
    }

    onPointerMove(e) {
        if (!this.active)
            return;
        const r = this.currentRect(e);
        if (r.w < 1 && r.h < 1)
            return;
        this.dragged = true;
        const W = this.doc.width, H = this.doc.height;
        const shape = this.ellipse
            ? Selection.fromPath(W, H, (c) => c.ellipse(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, r.h / 2, 0, 0, Math.PI * 2),
                                 this.settings.selectionAntialias)
            : Selection.rect(W, H, r.x, r.y, r.w, r.h);
        this.preview(shape);
        this.showStatus(r);
    }

    onPointerUp() {
        if (this.active)
            this.done(this.name, this.icon);
        this.setHint(this.hint());
    }
}

export class RectSelectTool extends ShapeSelectTool {
    get id() { return "rectangleselect"; }
    get name() { return tr("Selección rectangular"); }
    get icon() { return "rectangleselect"; }
    get letter() { return "S"; }
    get ellipse() { return false; }
    get options() { return ["selectionMode", "rectMode", "afterSelect", "selectionQuality"]; }
    hint() { return tr("Arrastra para seleccionar un rectángulo. Mayús: cuadrado. ") + selectHint(); }
}

export class EllipseSelectTool extends ShapeSelectTool {
    get id() { return "ellipseselect"; }
    get name() { return tr("Selección elíptica"); }
    get icon() { return "ellipseselect"; }
    get letter() { return "S"; }
    get ellipse() { return true; }
    hint() { return tr("Arrastra para seleccionar una elipse. Mayús: círculo. ") + selectHint(); }
}

export class LassoSelectTool extends SelectTool {
    get id() { return "lassoselect"; }
    get name() { return tr("Selección de lazo"); }
    get icon() { return "lassoselect"; }
    get letter() { return "S"; }
    hint() { return tr("Arrastra para dibujar el contorno de la selección. ") + selectHint(); }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.begin(e);
        this.points = [[e.x, e.y]];
    }

    onPointerMove(e) {
        if (!this.active)
            return;
        const last = this.points[this.points.length - 1];
        if (Math.hypot(e.x - last[0], e.y - last[1]) < 0.5)
            return;
        this.points.push([e.x, e.y]);
        if (this.points.length < 3)
            return;
        this.dragged = true;
        const pts = this.points;
        this.preview(Selection.fromPath(this.doc.width, this.doc.height, (c) => {
            pts.forEach(([x, y], i) => (i ? c.lineTo(x, y) : c.moveTo(x, y)));
            c.closePath();
        }, this.settings.selectionAntialias));
    }

    onPointerUp() {
        if (this.active)
            this.done(this.name, this.icon);
    }
}

export class MagicWandTool extends SelectTool {
    get id() { return "magicwand"; }
    get name() { return tr("Varita mágica"); }
    get icon() { return "magicwand"; }
    get letter() { return "S"; }
    get options() { return ["selectionMode", "flood", "tolerance", "tolAlpha", "sampling", "afterSelect", "selectionQuality", "finish"]; }

    hint() {
        return this.session
            ? tr("Ajusta la tolerancia o arrastra el punto de origen. Intro: terminar.")
            : tr("Haz clic para seleccionar una zona de color parecido. Mayús: modo global. ") + selectHint();
    }

    activate() {
        // Como en Kaint, la selección sigue a la tolerancia y al modo mientras dura la sesión.
        this.onSettings = () => this.session && this.recompute();
        AppState.addEventListener("tools", this.onSettings);
    }

    deactivate() {
        AppState.removeEventListener("tools", this.onSettings);
        this.finish();
    }

    seedHandle() {
        const v = this.view.imageToView(this.seed.x + 0.5, this.seed.y + 0.5);
        return { x: v.x + 16, y: v.y + 16 };
    }

    recompute() {
        const doc = this.doc, s = this.settings;
        const mask = floodMask(this.sample, this.seed.x, this.seed.y, s.tolerance, s.floodGlobal !== this.shiftGlobal, !!s.toleranceStraight);
        this.preview(Selection.fromMask(doc.width, doc.height, mask));
        this.repaint();
    }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || (e.button !== "left" && e.button !== "right"))
            return;
        if (this.session && e.button === "left" && nearPoint(e, this.seedHandle(), 11)) {
            this.draggingSeed = { x: this.seed.x + 0.5 - e.x, y: this.seed.y + 0.5 - e.y };
            return;
        }
        this.finish();
        const x = Math.floor(e.x), y = Math.floor(e.y);
        if (x < 0 || y < 0 || x >= doc.width || y >= doc.height)
            return;
        const s = this.settings;
        this.sample = context(s.sampleImage ? doc.flattened() : doc.activeLayer.canvas).getImageData(0, 0, doc.width, doc.height);
        this.begin(e);
        this.session = true;
        this.shiftGlobal = !!e.shift;
        this.seed = { x, y };
        this.dragged = true;
        this.recompute();
        this.setHint(this.hint());
    }

    onPointerMove(e) {
        if (!this.draggingSeed) {
            if (this.session)
                this.view.setCursor(nearPoint(e, this.seedHandle(), 11) ? "move" : this.cursor());
            return;
        }
        const doc = this.doc;
        const x = Math.floor(e.x + this.draggingSeed.x), y = Math.floor(e.y + this.draggingSeed.y);
        if ((x !== this.seed.x || y !== this.seed.y) && x >= 0 && y >= 0 && x < doc.width && y < doc.height) {
            this.seed = { x, y };
            this.recompute();
        }
    }

    onPointerUp() {
        if (this.draggingSeed) {
            this.draggingSeed = null;
            return;
        }
        if (this.session && this.mode === "replace" && this.plain)
            this.selectionFinished();
    }

    onKeyDown(e) {
        if (this.session && (e.key === "Enter" || e.key === "Escape")) {
            this.finish();
            return true;
        }
        return false;
    }

    // Solo mientras se arrastra el origen (deshacer durante la sesión aplica lo seleccionado).
    isBusy() {
        return !!this.draggingSeed;
    }

    finish() {
        if (!this.session)
            return;
        this.session = false;
        this.draggingSeed = null;
        this.sample = null;
        this.active = false;
        this.doc.pushSelection(this.before, this.name, this.icon);
        this.app.updateStatus();
        this.setHint(this.hint());
        this.repaint();
    }

    cancel() {
        this.draggingSeed = null;
    }

    paintOverlay(ctx, view) {
        if (!this.session)
            return;
        const v = view.imageToView(this.seed.x + 0.5, this.seed.y + 0.5);
        ctx.fillStyle = "#fff";
        ctx.strokeStyle = "#000";
        ctx.fillRect(Math.round(v.x) - 2, Math.round(v.y) - 2, 4, 4);
        ctx.strokeRect(Math.round(v.x) - 2.5, Math.round(v.y) - 2.5, 5, 5);
        drawMoveHandle(ctx, this.seedHandle());
    }
}

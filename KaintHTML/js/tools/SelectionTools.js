// Selección rectangular, elíptica, de lazo y varita mágica (src/app/tools/SelectionTools.cpp),
// con las combinaciones de Paint.NET: Ctrl + izq.: unión · Alt + izq.: restar · Ctrl + der.: XOR ·
// Alt + der.: intersección · sin teclas: el modo de la barra.
import { Tool, selectHint } from "./Tool.js";
import { Selection } from "../core/Selection.js";
import { floodMask } from "../core/FloodFill.js";
import { context } from "../core/Raster.js";
import { tr } from "../i18n.js";

class SelectTool extends Tool {
    get options() {
        return ["selectionMode", "selectionQuality"];
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
    get options() { return ["selectionMode", "rectMode"]; }
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
    get options() { return ["selectionMode", "flood", "tolerance", "sampling"]; }
    hint() { return tr("Haz clic para seleccionar una zona de color parecido. Mayús: modo global. ") + selectHint(); }
    cursor() { return this.iconCursor(4, 20); }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.begin(e);
        const doc = this.doc, s = this.settings;
        const source = s.sampleImage ? doc.flattened() : doc.activeLayer.canvas;
        const image = context(source).getImageData(0, 0, doc.width, doc.height);
        const mask = floodMask(image, Math.floor(e.x), Math.floor(e.y), s.tolerance, s.floodGlobal || e.shift);
        this.dragged = true;
        this.startTime = 0;
        this.preview(Selection.fromMask(doc.width, doc.height, mask));
        this.done(this.name, this.icon);
    }
}

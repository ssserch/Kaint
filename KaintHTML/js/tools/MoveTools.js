// Mover píxeles seleccionados y Mover selección (src/app/tools/MoveTools.cpp y TransformBox):
// arrastrar mueve, los tiradores cambian el tamaño (Mayús: proporción) y el botón derecho o
// arrastrar fuera del borde gira. Se aplica con Intro, al cambiar de herramienta o con otra orden.
import { Tool } from "./Tool.js";
import { Selection } from "../core/Selection.js";
import { cloneCanvas, context, createCanvas } from "../core/Raster.js";
import { iconCursor } from "../ui/Icons.js";
import { tr } from "../i18n.js";

// Caja de transformación: rectángulo base, desplazamiento, escala y giro alrededor del centro.
class TransformBox {
    constructor(rect) {
        this.base = rect;
        this.tx = 0;
        this.ty = 0;
        this.sx = 1;
        this.sy = 1;
        this.angle = 0;
    }

    center() {
        return { x: this.base.x + this.base.w / 2 + this.tx, y: this.base.y + this.base.h / 2 + this.ty };
    }

    matrix() {
        const c0 = { x: this.base.x + this.base.w / 2, y: this.base.y + this.base.h / 2 }, c = this.center();
        return new DOMMatrix().translate(c.x, c.y).rotate((this.angle * 180) / Math.PI).scale(this.sx, this.sy).translate(-c0.x, -c0.y);
    }

    isIdentity() {
        return !this.tx && !this.ty && this.sx === 1 && this.sy === 1 && !this.angle;
    }

    // Tiradores: esquinas y centros de los lados (en la imagen).
    handles() {
        const m = this.matrix(), b = this.base;
        const pts = [[0, 0], [0.5, 0], [1, 0], [1, 0.5], [1, 1], [0.5, 1], [0, 1], [0, 0.5]];
        return pts.map(([fx, fy]) => m.transformPoint({ x: b.x + fx * b.w, y: b.y + fy * b.h }));
    }

    // Punto de la imagen en las coordenadas de la caja (sin girar ni mover; centro en 0, 0).
    local(p) {
        const c = this.center(), dx = p.x - c.x, dy = p.y - c.y, cs = Math.cos(-this.angle), sn = Math.sin(-this.angle);
        return { x: dx * cs - dy * sn, y: dx * sn + dy * cs };
    }

    inside(p) {
        const l = this.local(p);
        return Math.abs(l.x) <= Math.abs((this.base.w * this.sx) / 2) && Math.abs(l.y) <= Math.abs((this.base.h * this.sy) / 2);
    }

    // Arrastrar un tirador: el lado contrario se queda quieto.
    resize(handle, p, keepRatio) {
        const l = this.local(p), hw = (this.base.w * this.sx) / 2, hh = (this.base.h * this.sy) / 2;
        let left = -hw, right = hw, top = -hh, bottom = hh;
        if ([0, 6, 7].includes(handle)) left = l.x;
        if ([2, 3, 4].includes(handle)) right = l.x;
        if ([0, 1, 2].includes(handle)) top = l.y;
        if ([4, 5, 6].includes(handle)) bottom = l.y;
        let sx = (right - left) / this.base.w, sy = (bottom - top) / this.base.h;
        if (keepRatio && handle % 2 === 0) {
            const k = Math.max(Math.abs(sx / this.sx), Math.abs(sy / this.sy));
            sx = Math.sign(sx || 1) * Math.abs(this.sx) * k;
            sy = Math.sign(sy || 1) * Math.abs(this.sy) * k;
            if ([0, 6].includes(handle)) left = right - sx * this.base.w;
            else right = left + sx * this.base.w;
            if ([0, 2].includes(handle)) top = bottom - sy * this.base.h;
            else bottom = top + sy * this.base.h;
        }
        if (Math.abs(sx) < 1e-3 || Math.abs(sy) < 1e-3)
            return;
        const mid = { x: (left + right) / 2, y: (top + bottom) / 2 }, cs = Math.cos(this.angle), sn = Math.sin(this.angle);
        this.tx += mid.x * cs - mid.y * sn;
        this.ty += mid.x * sn + mid.y * cs;
        this.sx = sx;
        this.sy = sy;
    }
}

class MoveToolBase extends Tool {
    get options() {
        return ["finish"];
    }

    get highlightSelection() {
        return true;
    }

    hint() {
        return tr("Arrastra la selección para moverla. Arrastre los controles para cambiar el tamaño. Arrastra con el botón derecho del ratón, o justo fuera del borde, para rotar.");
    }

    cursor() {
        return iconCursor(this.icon, 2, 2, "move");
    }

    handleAt(e) {
        if (!this.box)
            return -1;
        for (const [i, p] of this.box.handles().entries()) {
            const v = this.view.imageToView(p.x, p.y);
            if (Math.hypot(v.x - e.vx, v.y - e.vy) <= 7)
                return i;
        }
        return -1;
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        if (!this.box && !this.start())
            return;
        const p = { x: e.x, y: e.y }, c = this.box.center();
        const handle = e.button === "left" ? this.handleAt(e) : -1;
        if (handle >= 0)
            this.drag = { kind: "resize", handle };
        else if (e.button === "right" || !this.box.inside(p))
            this.drag = { kind: "rotate", from: Math.atan2(p.y - c.y, p.x - c.x), angle: this.box.angle };
        else
            this.drag = { kind: "move", from: p, tx: this.box.tx, ty: this.box.ty };
    }

    onPointerMove(e) {
        if (!this.drag)
            return;
        const p = { x: e.x, y: e.y }, d = this.drag, box = this.box;
        if (d.kind === "move") {
            box.tx = Math.round(d.tx + p.x - d.from.x);
            box.ty = Math.round(d.ty + p.y - d.from.y);
        } else if (d.kind === "resize") {
            box.resize(d.handle, p, e.shift);
        } else {
            const c = box.center();
            let a = d.angle + Math.atan2(p.y - c.y, p.x - c.x) - d.from;
            if (e.shift)
                a = Math.round(a / (Math.PI / 12)) * (Math.PI / 12);
            box.angle = a;
        }
        this.update();
    }

    onPointerUp() {
        this.drag = null;
    }

    isBusy() {
        return !!this.box;
    }

    onKeyDown(e) {
        if (!this.box)
            return false;
        if (e.key === "Enter") { this.finish(); return true; }
        const step = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
        if (step) {
            this.box.tx += step[0];
            this.box.ty += step[1];
            this.update();
            return true;
        }
        return false;
    }

    paintOverlay(ctx, view) {
        if (!this.box || this.drag)
            return;
        const pts = this.box.handles().map((p) => view.imageToView(p.x, p.y));
        ctx.setLineDash([3, 3]);
        ctx.strokeStyle = "rgba(0,0,0,0.55)";
        ctx.lineWidth = 1;
        ctx.beginPath();
        [0, 2, 4, 6].forEach((i, k) => (k ? ctx.lineTo(pts[i].x, pts[i].y) : ctx.moveTo(pts[i].x, pts[i].y)));
        ctx.closePath();
        ctx.stroke();
        ctx.setLineDash([]);
        for (const v of pts) {
            ctx.fillStyle = "#fff";
            ctx.fillRect(v.x - 3.5, v.y - 3.5, 7, 7);
            ctx.strokeStyle = "#000";
            ctx.strokeRect(v.x - 3.5, v.y - 3.5, 7, 7);
        }
    }

    hidesSelectionOutline() {
        return !!this.box;
    }
}

export class MoveSelectedTool extends MoveToolBase {
    get id() { return "moveselectedpixels"; }
    get name() { return tr("Mover píxeles seleccionados"); }
    get icon() { return "moveselectedpixels"; }
    get letter() { return "M"; }

    // Levanta los píxeles seleccionados (o toda la capa) y deja el hueco.
    start() {
        const doc = this.doc, sel = doc.selection;
        this.layer = doc.activeLayer;
        this.before = cloneCanvas(this.layer.canvas);
        this.beforeSelection = sel;
        const b = sel.bounds() || { x: 0, y: 0, w: doc.width, h: doc.height };
        this.floating = createCanvas(b.w, b.h);
        const f = context(this.floating);
        f.drawImage(this.layer.canvas, -b.x, -b.y);
        f.globalCompositeOperation = "destination-in";
        f.drawImage(sel.maskCanvas(), -b.x, -b.y);
        this.hole = cloneCanvas(this.layer.canvas);
        const h = context(this.hole);
        h.globalCompositeOperation = "destination-out";
        h.drawImage(sel.maskCanvas(), 0, 0);
        this.preview = createCanvas(doc.width, doc.height);
        this.box = new TransformBox(b);
        this.update();
        return true;
    }

    // Píxeles pegados: flotan sobre la capa (sin hueco debajo) hasta aplicarlos.
    startFloating(image, x, y, name) {
        const doc = this.doc;
        this.finish();
        this.layer = doc.activeLayer;
        this.before = cloneCanvas(this.layer.canvas);
        this.hole = cloneCanvas(this.layer.canvas);
        this.floating = createCanvas(image.width, image.height);
        context(this.floating).drawImage(image, 0, 0);
        const rect = { x, y, w: image.width, h: image.height };
        this.selectionBeforePaste = doc.selection;
        this.beforeSelection = Selection.rect(doc.width, doc.height, x, y, image.width, image.height);
        this.preview = createCanvas(doc.width, doc.height);
        this.box = new TransformBox(rect);
        this.forceCommit = true;
        this.commitName = name;
        this.update();
    }

    update() {
        const doc = this.doc, p = context(this.preview), m = this.box.matrix();
        p.setTransform(1, 0, 0, 1, 0, 0);
        p.clearRect(0, 0, doc.width, doc.height);
        p.drawImage(this.hole, 0, 0);
        p.setTransform(m.multiply(new DOMMatrix().translate(this.box.base.x, this.box.base.y)));
        p.imageSmoothingEnabled = this.box.sx !== 1 || this.box.sy !== 1 || !!this.box.angle;
        p.imageSmoothingQuality = "high";
        p.drawImage(this.floating, 0, 0);
        p.setTransform(1, 0, 0, 1, 0, 0);
        doc.setPreview({ layer: this.layer, canvas: this.preview });
    }

    finish() {
        if (!this.box)
            return;
        const doc = this.doc, layer = this.layer, box = this.box;
        this.box = null;
        this.drag = null;
        doc.preview = null;
        const name = this.commitName || this.name, forced = this.forceCommit;
        this.forceCommit = false;
        this.commitName = null;
        if (box.isIdentity() && !forced) {
            doc.setSelection(this.beforeSelection);
            doc.pixelsChanged(layer, null);
            return;
        }
        layer.ctx.clearRect(0, 0, doc.width, doc.height);
        layer.ctx.drawImage(this.preview, 0, 0);
        const before = context(this.before).getImageData(0, 0, doc.width, doc.height);
        const after = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        const selBefore = this.beforeSelection;
        const selAfter = selBefore.isEmpty() ? selBefore : selBefore.transformed(box.matrix());
        doc.selection = selAfter;
        const apply = (data, sel) => {
            layer.ctx.putImageData(data, 0, 0);
            doc.setSelection(sel);
            doc.pixelsChanged(layer, null);
        };
        // Al pegar, deshacer quita también la selección que se creó.
        const selUndo = forced ? this.selectionBeforePaste : selBefore;
        doc.push({ name, icon: forced ? "edit-paste" : this.icon, undo: () => apply(before, selUndo), redo: () => apply(after, selAfter) });
        doc.pixelsChanged(layer, null);
        doc.emit("selection");
    }

    cancel() {
        if (!this.box)
            return;
        this.box = null;
        this.drag = null;
        this.doc.setPreview(null);
        this.doc.emit("selection");
    }

    paintOverlay(ctx, view) {
        if (this.box && !this.beforeSelection.isEmpty())
            view.strokeSegments(ctx, this.beforeSelection.outline(), this.box.matrix());
        super.paintOverlay(ctx, view);
    }
}

export class MoveSelectionTool extends MoveToolBase {
    get id() { return "moveselection"; }
    get name() { return tr("Mover selección"); }
    get icon() { return "moveselection"; }
    get letter() { return "M"; }

    start() {
        const sel = this.doc.selection;
        if (sel.isEmpty())
            return false;
        this.beforeSelection = sel;
        this.box = new TransformBox(sel.bounds());
        return true;
    }

    update() {
        this.view.update();
    }

    finish() {
        if (!this.box)
            return;
        const box = this.box;
        this.box = null;
        this.drag = null;
        if (box.isIdentity())
            return;
        const doc = this.doc;
        doc.setSelection(this.beforeSelection.transformed(box.matrix(), this.settings.selectionAntialias));
        doc.pushSelection(this.beforeSelection, this.name, this.icon);
    }

    cancel() {
        this.box = null;
        this.drag = null;
        this.view.update();
    }

    paintOverlay(ctx, view) {
        if (this.box)
            view.strokeSegments(ctx, this.beforeSelection.outline(), this.box.matrix());
        super.paintOverlay(ctx, view);
    }
}


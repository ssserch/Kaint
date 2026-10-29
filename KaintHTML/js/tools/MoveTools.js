// Mover píxeles seleccionados y Mover selección (src/app/tools/MoveTools.cpp y TransformBox):
// arrastrar mueve, los tiradores cambian el tamaño (Mayús: proporción) y el botón derecho o
// arrastrar fuera del borde gira. Como en Kaint, cada arrastre es un paso del historial y los
// píxeles siguen "flotando" mientras ese paso sea el último (moverlos otra vez no los degrada).
// Sin selección, un clic con Mover píxeles seleccionados selecciona todo el lienzo.
import { Tool } from "./Tool.js";
import { Selection } from "../core/Selection.js";
import { cloneCanvas, context, createCanvas } from "../core/Raster.js";
import { iconCursor, rotateCursor } from "../ui/Icons.js";
import { Settings } from "../Settings.js";
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

    save() {
        return { tx: this.tx, ty: this.ty, sx: this.sx, sy: this.sy, angle: this.angle };
    }

    restore(state) {
        Object.assign(this, state);
    }

    same(state) {
        return this.tx === state.tx && this.ty === state.ty && this.sx === state.sx && this.sy === state.sy && this.angle === state.angle;
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
        return iconCursor(this.icon, null, null, "move");
    }

    // La sesión (píxeles o selección levantados) sigue viva mientras su paso sea el último del
    // historial de la imagen activa.
    session() {
        const s = this.current;
        if (!s || s.doc !== this.doc)
            return null;
        if (this.drag?.session === s)
            return s;
        const h = s.doc.history;
        if (!s.step || h.steps[h.index] !== s.step || (s.layer && s.layer !== s.doc.activeLayer))
            return null;
        return s;
    }

    // Caja que se ve y se agarra: la de la sesión o la de la selección.
    restBox() {
        const s = this.session();
        if (s)
            return s.box;
        const b = this.doc?.selection.bounds();
        return b ? new TransformBox(b) : null;
    }

    handleAt(e, box) {
        if (!box)
            return -1;
        for (const [i, p] of box.handles().entries()) {
            const v = this.view.imageToView(p.x, p.y);
            if (Math.hypot(v.x - e.vx, v.y - e.vy) <= 7)
                return i;
        }
        return -1;
    }

    // Qué hace el botón en ese punto: tirador, mover (dentro) o girar (fuera o botón derecho).
    hit(e, box) {
        const handle = e.button !== "right" ? this.handleAt(e, box) : -1;
        if (handle >= 0)
            return { kind: "resize", handle };
        if (e.button === "right" || (box && !box.inside({ x: e.x, y: e.y })))
            return { kind: "rotate" };
        return { kind: "move" };
    }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || (e.button !== "left" && e.button !== "right"))
            return;
        let hit = this.hit(e, this.restBox());
        const session = this.begin(e);
        if (!session)
            return;
        if (this.selectedAll)
            hit = { kind: "move" };
        const box = session.box, p = { x: e.x, y: e.y }, c = box.center();
        this.drag = { ...hit, session, start: box.save(), from: p, fromAngle: Math.atan2(p.y - c.y, p.x - c.x) };
    }

    onPointerMove(e) {
        if (!this.drag) {
            this.hoverCursor(e);
            return;
        }
        const p = { x: e.x, y: e.y }, d = this.drag, box = d.session.box;
        if (d.kind === "move") {
            box.tx = Math.round(d.start.tx + p.x - d.from.x);
            box.ty = Math.round(d.start.ty + p.y - d.from.y);
        } else if (d.kind === "resize") {
            box.resize(d.handle, p, e.shift);
        } else {
            const c = box.center();
            let a = d.start.angle + Math.atan2(p.y - c.y, p.x - c.x) - d.fromAngle;
            if (e.shift)
                a = Math.round(a / (Math.PI / 12)) * (Math.PI / 12);
            box.angle = a;
            this.setHint(tr("Ángulo: %1°", ((-a * 180) / Math.PI).toFixed(2)));
            this.view.canvas.style.cursor = rotateCursor(this.angleTo(box, e));
        }
        this.render(d.session);
    }

    onPointerUp() {
        const d = this.drag;
        if (!d)
            return;
        this.drag = null;
        this.selectedAll = false;
        this.setHint(this.hint());
        if (d.session.box.same(d.start)) {
            this.unmoved(d.session);
            return;
        }
        this.commit(d.session, this.name, this.icon);
    }

    hoverCursor(e) {
        const box = this.restBox();
        const canvas = this.view.canvas;
        if (!box || this.doc.selection.isEmpty()) {
            canvas.style.cursor = this.cursor();
            return;
        }
        const hit = this.hit(e, box);
        canvas.style.cursor = hit.kind === "resize" ? "pointer" : hit.kind === "rotate" ? rotateCursor(this.angleTo(box, e)) : this.cursor();
    }

    // Dirección (en la vista) desde el puntero hacia el centro de la caja: la de la flecha de girar.
    angleTo(box, e) {
        const c = box.center(), v = this.view.imageToView(c.x, c.y);
        return Math.atan2(v.y - e.vy, v.x - e.vx);
    }

    isBusy() {
        return !!this.drag;
    }

    // Se deja de arrastrar sin aplicar nada (Esc, deshacer durante el arrastre).
    cancel() {
        const d = this.drag;
        if (!d)
            return;
        this.drag = null;
        this.selectedAll = false;
        d.session.box.restore(d.start);
        this.unmoved(d.session);
        this.view.update();
    }

    finish() {
        this.cancel();
        this.current = null;
    }

    deactivate() {
        this.finish();
        this.view.update();
    }

    onKeyDown(e) {
        // Flechas: mover un píxel, como un arrastre corto.
        const step = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
        if (!step || this.drag || !this.doc || this.doc.selection.isEmpty())
            return false;
        const session = this.begin({ button: "left", ctrl: false });
        if (!session)
            return false;
        session.box.tx += step[0];
        session.box.ty += step[1];
        this.render(session);
        this.commit(session, this.name, this.icon);
        return true;
    }

    paintOverlay(ctx, view) {
        const s = this.session();
        const box = this.drag ? this.drag.session.box : this.restBox();
        if (!box || (!s && this.doc.selection.isEmpty()))
            return;
        // La selección se ve donde quedará; los tiradores, solo en reposo (como Kaint).
        if (s && this.drag)
            view.strokeSegments(ctx, s.selBase.outline(), box.matrix());
        if (this.drag)
            return;
        const pts = box.handles().map((p) => view.imageToView(p.x, p.y));
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
        return !!this.drag;
    }
}

export class MoveSelectedTool extends MoveToolBase {
    get id() { return "moveselectedpixels"; }
    get name() { return tr("Mover píxeles seleccionados"); }
    get icon() { return "moveselectedpixels"; }
    get letter() { return "M"; }

    // Sesión para arrastrar: la que sigue viva o, si no, los píxeles de la selección levantados.
    // Sin selección, como en Paint.NET, el clic selecciona todo el lienzo (ya se ve al pulsar).
    begin(e) {
        const doc = this.doc;
        if (!doc.activeLayer)
            return null;
        let session = e.ctrl ? null : this.session();
        if (!session && doc.selection.isEmpty()) {
            const before = doc.selection;
            doc.setSelection(Selection.rect(doc.width, doc.height, 0, 0, doc.width, doc.height));
            doc.pushSelection(before, tr("Seleccionar todo"), "edit-select-all");
            this.selectedAll = true;
        }
        if (!session) {
            session = this.lift(!!e.ctrl);
            this.current = session;
        }
        return session;
    }

    // Levanta los píxeles seleccionados y deja el hueco (con Ctrl, una copia donde estaban).
    lift(keepCopy) {
        const doc = this.doc, sel = doc.selection, layer = doc.activeLayer;
        const b = sel.bounds();
        const floating = createCanvas(b.w, b.h);
        const f = context(floating);
        f.drawImage(layer.canvas, -b.x, -b.y);
        f.globalCompositeOperation = "destination-in";
        f.drawImage(sel.maskCanvas(), -b.x, -b.y);
        const base = cloneCanvas(layer.canvas);
        if (!keepCopy) {
            const h = context(base);
            h.globalCompositeOperation = "destination-out";
            h.drawImage(sel.maskCanvas(), 0, 0);
        }
        return { doc, layer, base, floating, selBase: sel, box: new TransformBox(b), preview: createCanvas(doc.width, doc.height), step: null };
    }

    // La capa con los píxeles en su sitio nuevo. Como en Paint.NET (Configuración), los movidos
    // reemplazan a los de debajo, también a los transparentes.
    render(s) {
        const doc = s.doc, p = context(s.preview), m = s.box.matrix();
        p.setTransform(1, 0, 0, 1, 0, 0);
        p.globalCompositeOperation = "source-over";
        p.clearRect(0, 0, doc.width, doc.height);
        p.drawImage(s.base, 0, 0);
        const smooth = s.box.sx !== 1 || s.box.sy !== 1 || !!s.box.angle;
        if (Settings.get("replacePixels")) {
            p.setTransform(m);
            p.globalCompositeOperation = "destination-out";
            p.imageSmoothingEnabled = smooth;
            p.drawImage(s.selBase.maskCanvas(), 0, 0);
            p.globalCompositeOperation = "source-over";
        }
        p.setTransform(m.multiply(new DOMMatrix().translate(s.box.base.x, s.box.base.y)));
        p.imageSmoothingEnabled = smooth;
        p.imageSmoothingQuality = "high";
        p.drawImage(s.floating, 0, 0);
        p.setTransform(1, 0, 0, 1, 0, 0);
        doc.setPreview({ layer: s.layer, canvas: s.preview });
    }

    unmoved(s) {
        // Se soltó sin mover: la capa no cambia (la selección de todo, si se hizo, se queda).
        if (s.doc.preview?.canvas === s.preview)
            s.doc.setPreview(null);
        if (!s.step)
            this.current = null;
    }

    // Un paso del historial con la capa y la selección de antes y de después.
    commit(s, name, icon, selectionBefore = s.doc.selection) {
        const doc = s.doc, layer = s.layer;
        const before = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        doc.preview = null;
        layer.ctx.clearRect(0, 0, doc.width, doc.height);
        layer.ctx.drawImage(s.preview, 0, 0);
        const after = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        const selAfter = s.selBase.transformed(s.box.matrix(), this.settings.selectionAntialias);
        const apply = (data, sel) => {
            layer.ctx.putImageData(data, 0, 0);
            doc.setSelection(sel);
            doc.pixelsChanged(layer, null);
        };
        s.step = { name, icon, undo: () => apply(before, selectionBefore), redo: () => apply(after, selAfter) };
        doc.selection = selAfter;
        doc.push(s.step);
        doc.pixelsChanged(layer, null);
        doc.emit("selection");
        this.current = s;
    }

    // Píxeles pegados: se colocan como un paso "Pegar" y quedan flotando (sin hueco debajo) para
    // moverlos después.
    startFloating(image, x, y, name) {
        const doc = this.doc;
        this.finish();
        const floating = createCanvas(image.width, image.height);
        context(floating).drawImage(image, 0, 0);
        const rect = { x, y, w: image.width, h: image.height };
        const s = {
            doc, layer: doc.activeLayer, base: cloneCanvas(doc.activeLayer.canvas), floating,
            selBase: Selection.rect(doc.width, doc.height, x, y, image.width, image.height), box: new TransformBox(rect),
            preview: createCanvas(doc.width, doc.height), step: null,
        };
        this.render(s);
        this.commit(s, name, "edit-paste");
    }
}

export class MoveSelectionTool extends MoveToolBase {
    get id() { return "moveselection"; }
    get name() { return tr("Mover selección"); }
    get icon() { return "moveselection"; }
    get letter() { return "M"; }

    begin(e) {
        const doc = this.doc;
        let session = e.ctrl ? null : this.session();
        if (!session) {
            if (doc.selection.isEmpty())
                return null;
            session = { doc, selBase: doc.selection, box: new TransformBox(doc.selection.bounds()), step: null };
            this.current = session;
        }
        return session;
    }

    render() {
        this.view.update();
    }

    unmoved(s) {
        if (!s.step)
            this.current = null;
        this.view.update();
    }

    commit(s, name, icon) {
        const doc = s.doc, before = doc.selection;
        const after = s.selBase.transformed(s.box.matrix(), this.settings.selectionAntialias);
        s.step = { name, icon, undo: () => doc.setSelection(before), redo: () => doc.setSelection(after) };
        doc.setSelection(after);
        doc.push(s.step);
        this.current = s;
    }

    paintOverlay(ctx, view) {
        if (this.drag)
            view.strokeSegments(ctx, this.drag.session.selBase.outline(), this.drag.session.box.matrix());
        super.paintOverlay(ctx, view);
    }
}

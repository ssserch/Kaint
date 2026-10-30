// Zum, mano y selector de color (src/app/tools/ViewTools.cpp).
import { Tool } from "./Tool.js";
import { AppState } from "../AppState.js";
import { context } from "../core/Raster.js";
import { handCursor, pdnCursors } from "../ui/Icons.js";
import { tr } from "../i18n.js";

export class ZoomTool extends Tool {
    get id() { return "zoom"; }
    get name() { return tr("Zum"); }
    get icon() { return "zoom"; }
    get letter() { return "Z"; }
    hint() { return tr("Clic izquierdo: acercar · clic derecho: alejar · arrastrar: acercar a un rectángulo."); }

    onPointerDown(e) {
        this.pressed = true;
        this.dragging = false;
        this.start = { x: e.x, y: e.y };
        this.current = { x: e.x, y: e.y };
        this.startView = { x: e.vx, y: e.vy };
    }

    // El recuadro va de borde a borde de píxel y se queda dentro del lienzo, como la selección
    // rectangular.
    dragRect() {
        if (!this.dragging || !this.doc)
            return null;
        const ax = Math.round(this.start.x), ay = Math.round(this.start.y);
        const bx = Math.round(this.current.x), by = Math.round(this.current.y);
        const x0 = Math.max(0, Math.min(ax, bx)), y0 = Math.max(0, Math.min(ay, by));
        const x1 = Math.min(this.doc.width, Math.max(ax, bx)), y1 = Math.min(this.doc.height, Math.max(ay, by));
        return x1 > x0 && y1 > y0 ? { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } : null;
    }

    onPointerMove(e) {
        if (!this.pressed)
            return;
        this.current = { x: e.x, y: e.y };
        if (!this.dragging && Math.hypot(e.vx - this.startView.x, e.vy - this.startView.y) > 4) {
            this.dragging = true;
            this.view.setOverlayAnts(true);
        }
        const r = this.dragRect();
        if (r)
            this.setHint(tr("Zum a: %1, %2 · %3 × %4", r.x, r.y, r.w, r.h));
    }

    onPointerUp(e) {
        if (!this.pressed)
            return;
        this.pressed = false;
        const r = this.dragRect(), dragged = this.dragging;
        this.stop();
        const anchor = { x: e.vx, y: e.vy };
        if (dragged && e.button === "left" && r)
            this.view.zoomToRect(r);
        else if (e.button === "left")
            this.view.zoomIn(anchor);
        else if (e.button === "right")
            this.view.zoomOut(anchor);
    }

    stop() {
        if (this.dragging) {
            this.dragging = false;
            this.view.setOverlayAnts(false);
            this.setHint(this.hint());
        }
    }

    deactivate() {
        this.pressed = false;
        this.stop();
    }

    paintOverlay(ctx, view) {
        const r = this.dragRect();
        if (r)
            view.drawMarchingRect(ctx, r, true);
    }
}

export class PanTool extends Tool {
    get id() { return "pan"; }
    get name() { return tr("Mano"); }
    get icon() { return "pan"; }
    get letter() { return "H"; }
    hint() { return tr("Arrastra para desplazar la vista. También: botón central o barra espaciadora."); }
    // El lienzo desplaza la vista al arrastrar con esta herramienta (y pone la mano agarrando).
    cursor() { return handCursor(false); }
}

export class ColorPickerTool extends Tool {
    get id() { return "colorpicker"; }
    get name() { return tr("Selector de color"); }
    get icon() { return "colorpicker"; }
    get letter() { return "K"; }
    get options() { return ["picker", "selectionQuality"]; }
    hint() {
        return tr("Clic izquierdo: tomar el color principal · clic derecho: el secundario. "
                  + "Ctrl: muestrear la imagen en lugar de la capa (o al revés).");
    }

    pick(e) {
        const doc = this.doc;
        const image = this.settings.pickerSampleImage !== e.ctrl;
        const source = image ? doc.flattened() : doc.activeLayer.canvas;
        const half = Math.floor(this.settings.pickerSize / 2);
        const x0 = Math.max(0, Math.floor(e.x) - half), y0 = Math.max(0, Math.floor(e.y) - half);
        const x1 = Math.min(doc.width, Math.floor(e.x) + half + 1), y1 = Math.min(doc.height, Math.floor(e.y) + half + 1);
        if (x1 <= x0 || y1 <= y0)
            return;
        const d = context(source).getImageData(x0, y0, x1 - x0, y1 - y0).data;
        let r = 0, g = 0, b = 0, a = 0;
        for (let i = 0; i < d.length; i += 4) {
            r += d[i] * d[i + 3]; g += d[i + 1] * d[i + 3]; b += d[i + 2] * d[i + 3]; a += d[i + 3];
        }
        const n = d.length / 4, k = a > 0 ? 1 / a : 0;
        const color = { r: Math.round(r * k), g: Math.round(g * k), b: Math.round(b * k), a: Math.round(a / n) };
        this.button === "right" ? AppState.setSecondary(color) : AppState.setPrimary(color);
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.button = e.button;
        this.pick(e);
    }

    onPointerMove(e) {
        if (pdnCursors() && (Math.floor(e.x) !== this.hover?.x || Math.floor(e.y) !== this.hover?.y))
            this.view.update();
        this.hover = { x: Math.floor(e.x), y: Math.floor(e.y) };
        if (this.button)
            this.pick(e);
    }

    // Como en Paint.NET (con sus cursores): un cuadrado sigue al puntero píxel a píxel y marca lo
    // que se toma (el píxel, o el cuadro de N × N de la barra).
    paintOverlay(ctx, view) {
        if (!pdnCursors() || !this.hover || !view.mouse)
            return;
        const half = Math.floor(this.settings.pickerSize / 2);
        const a = view.imageToView(this.hover.x - half, this.hover.y - half);
        const b = view.imageToView(this.hover.x + half + 1, this.hover.y + half + 1);
        const x = Math.round(a.x) + 0.5, y = Math.round(a.y) + 0.5, w = Math.round(b.x) - Math.round(a.x), h = Math.round(b.y) - Math.round(a.y);
        ctx.lineWidth = 1;
        ctx.strokeStyle = "#000";
        ctx.strokeRect(x - 1, y - 1, w + 1, h + 1);
        ctx.strokeStyle = "#fff";
        ctx.strokeRect(x, y, w - 1, h - 1);
    }

    onPointerUp() {
        if (!this.button)
            return;
        this.button = null;
        const after = this.settings.pickerAfter;
        if (after === "previous" && this.app.tools.previous)
            this.app.tools.select(this.app.tools.previous.id);
        else if (after === "pencil")
            this.app.tools.select("pencil");
    }
}

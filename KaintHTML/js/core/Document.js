// Documento: capas, selección e historial (como src/core/Document.cpp y Commands.cpp de Kaint).
// Eventos: "layers" (lista o propiedades), "pixels" ({layer, rect}), "selection", "active",
// "size", "history" y "modified".
import { cloneCanvas, context, createCanvas, drawBlended } from "./Raster.js";
import { Selection } from "./Selection.js";
import { tr } from "../i18n.js";

let nextLayerId = 1;

export class Layer {
    constructor(width, height, name) {
        this.canvas = createCanvas(width, height);
        this.ctx = context(this.canvas);
        this.name = name;
        this.visible = true;
        this.opacity = 255;
        this.blend = "normal";
        this.id = nextLayerId++;
    }

    props() {
        return { name: this.name, visible: this.visible, opacity: this.opacity, blend: this.blend };
    }

    setProps(p) {
        Object.assign(this, p);
    }

    duplicate(name) {
        const copy = new Layer(this.canvas.width, this.canvas.height, name);
        copy.ctx.drawImage(this.canvas, 0, 0);
        copy.setProps({ visible: this.visible, opacity: this.opacity, blend: this.blend });
        return copy;
    }

    // Sustituye el lienzo (cambiar el tamaño, girar...).
    replaceCanvas(canvas) {
        this.canvas = canvas;
        this.ctx = context(canvas);
    }
}

export class History {
    constructor(name, icon) {
        this.steps = [{ name, icon, undo() {}, redo() {} }];
        this.index = 0;
    }

    canUndo() {
        return this.index > 0;
    }

    canRedo() {
        return this.index < this.steps.length - 1;
    }
}

export class Document extends EventTarget {
    constructor(width, height, { name = "", background = "#ffffff", historyName, historyIcon = "document-new" } = {}) {
        super();
        this.width = width;
        this.height = height;
        this.name = name;
        this.fileHandle = null;
        this.fileName = "";
        this.layers = [];
        this.active = 0;
        this.selection = Selection.none(width, height);
        // Vista previa de la capa activa mientras se pinta, se mueve o se ajusta un efecto.
        this.preview = null;
        this.history = new History(historyName ?? tr("Imagen nueva"), historyIcon);
        this.cleanIndex = 0;
        const bg = new Layer(width, height, tr("Fondo"));
        if (background) {
            bg.ctx.fillStyle = background;
            bg.ctx.fillRect(0, 0, width, height);
        }
        this.layers.push(bg);
    }

    static fromImage(image, name) {
        const doc = new Document(image.width, image.height, { name, background: null, historyName: tr("Abrir imagen"),
                                                               historyIcon: "document-open" });
        doc.layers[0].ctx.drawImage(image, 0, 0);
        return doc;
    }

    emit(type, detail = {}) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }

    get rect() {
        return { x: 0, y: 0, w: this.width, h: this.height };
    }

    get activeLayer() {
        return this.layers[this.active];
    }

    isModified() {
        return this.history.index !== this.cleanIndex;
    }

    markSaved() {
        this.cleanIndex = this.history.index;
        this.emit("modified");
    }

    displayName() {
        return this.fileName || this.name || tr("Sin título");
    }

    setActiveIndex(i) {
        i = Math.max(0, Math.min(this.layers.length - 1, i));
        if (i === this.active)
            return;
        this.active = i;
        this.emit("active");
    }

    setSelection(selection) {
        this.selection = selection;
        this.emit("selection");
    }

    pixelsChanged(layer, rect = null) {
        this.emit("pixels", { layer, rect: rect || this.rect });
    }

    layersChanged() {
        this.emit("layers");
        this.emit("pixels", { layer: null, rect: this.rect });
    }

    setPreview(preview, rect = null) {
        this.preview = preview;
        this.emit("pixels", { layer: preview?.layer ?? this.activeLayer, rect: rect || this.rect });
    }

    // La imagen compuesta (capas visibles) de un rectángulo, en 'ctx'.
    composite(ctx, rect = this.rect, { includeHidden = false } = {}) {
        ctx.clearRect(rect.x, rect.y, rect.w, rect.h);
        for (const layer of this.layers) {
            if (!layer.visible && !includeHidden)
                continue;
            const source = this.preview && this.preview.layer === layer ? this.preview.canvas : layer.canvas;
            drawBlended(ctx, source, layer.blend, layer.opacity, rect);
        }
    }

    flattened() {
        const canvas = createCanvas(this.width, this.height);
        this.composite(context(canvas));
        return canvas;
    }

    // --- Historial ------------------------------------------------------------------------

    push(step) {
        const h = this.history;
        h.steps.splice(h.index + 1);
        if (this.cleanIndex > h.index)
            this.cleanIndex = -1;
        h.steps.push(step);
        h.index = h.steps.length - 1;
        this.emit("history");
        this.emit("modified");
    }

    undo() {
        const h = this.history;
        if (!h.canUndo())
            return;
        h.steps[h.index].undo();
        h.index--;
        this.emit("history");
        this.emit("modified");
    }

    redo() {
        const h = this.history;
        if (!h.canRedo())
            return;
        h.index++;
        h.steps[h.index].redo();
        this.emit("history");
        this.emit("modified");
    }

    goTo(index) {
        while (this.history.index > index)
            this.undo();
        while (this.history.index < index)
            this.redo();
    }

    // Píxeles de una capa: 'before' es el ImageData del rectángulo antes del cambio.
    pushPixels(layer, rect, before, name, icon) {
        if (!rect || rect.w <= 0 || rect.h <= 0)
            return;
        const after = layer.ctx.getImageData(rect.x, rect.y, rect.w, rect.h);
        const apply = (data) => {
            layer.ctx.putImageData(data, rect.x, rect.y);
            this.pixelsChanged(layer, rect);
        };
        this.push({ name, icon, undo: () => apply(before), redo: () => apply(after) });
    }

    pushSelection(before, name = tr("Selección"), icon = "rectangleselect") {
        const after = this.selection;
        if (before.equals(after))
            return;
        this.push({ name, icon, undo: () => this.setSelection(before), redo: () => this.setSelection(after) });
    }

    // Lista de capas y capa activa (añadir, borrar, mover...): estado antes y después.
    layerState() {
        return { layers: [...this.layers], active: this.active, props: this.layers.map((l) => l.props()) };
    }

    restoreLayerState(state) {
        this.layers = [...state.layers];
        state.layers.forEach((l, i) => l.setProps(state.props[i]));
        this.active = Math.min(state.active, this.layers.length - 1);
        this.layersChanged();
        this.emit("active");
    }

    pushLayers(before, name, icon) {
        const after = this.layerState();
        this.push({ name, icon, undo: () => this.restoreLayerState(before), redo: () => this.restoreLayerState(after) });
    }

    // Todo el documento (tamaño, capas con sus píxeles y selección): girar, recortar, acoplar...
    snapshot() {
        return {
            width: this.width, height: this.height, active: this.active, selection: this.selection,
            layers: this.layers.map((l) => ({ layer: l, canvas: cloneCanvas(l.canvas), props: l.props() })),
        };
    }

    restore(snap) {
        const sizeChanged = snap.width !== this.width || snap.height !== this.height;
        this.width = snap.width;
        this.height = snap.height;
        this.layers = snap.layers.map(({ layer, canvas, props }) => {
            layer.replaceCanvas(cloneCanvas(canvas));
            layer.setProps(props);
            return layer;
        });
        this.active = Math.min(snap.active, this.layers.length - 1);
        this.selection = snap.selection;
        if (sizeChanged)
            this.emit("size");
        this.emit("selection");
        this.layersChanged();
        this.emit("active");
    }

    pushSnapshot(before, name, icon) {
        const after = this.snapshot();
        this.push({ name, icon, undo: () => this.restore(before), redo: () => this.restore(after) });
    }
}

// Lienzo (src/app/Canvas.cpp de Kaint): muestra la imagen compuesta con el zum (100 % = un
// píxel de la imagen por píxel físico de la pantalla), el damero, la sombra, la cuadrícula de
// píxeles, la selección con hormigas y lo que dibuje la herramienta; reparte el ratón a la
// herramienta y se desplaza con la rueda, el botón central o la barra espaciadora.
import { checkerPattern, clampRect, context, createCanvas, unionRect } from "../core/Raster.js";
import { handCursor } from "./Icons.js";
import { Settings } from "../Settings.js";

const kMargin = 24;

export const ZoomPresets = [0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.12, 0.16, 0.2, 0.25, 1 / 3, 0.5, 2 / 3, 1, 1.5, 2, 3,
                            4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64];

export class CanvasView extends EventTarget {
    constructor(canvas, workspace) {
        super();
        this.canvas = canvas;
        this.workspace = workspace;
        this.ctx = canvas.getContext("2d");
        this.doc = null;
        this.tool = null;
        this.zoom = 1;
        this.origin = { x: 0, y: 0 };
        this.fitOnResize = true;
        this.pixelGrid = false;
        this.mouse = null;
        this.panning = null;
        this.spaceDown = false;
        this.antsOffset = 0;
        this.overlayAnts = false;
        this.pending = false;
        this.listeners = [];
        this.hScroll = workspace.querySelector(".scrollbar.h");
        this.vScroll = workspace.querySelector(".scrollbar.v");
        new ResizeObserver(() => this.resized()).observe(workspace);
        this.installEvents();
        this.installScrollbars();
        setInterval(() => this.stepAnts(), 120);
        window.addEventListener("kaint-settings", () => this.update());
    }

    get dpr() {
        return window.devicePixelRatio || 1;
    }

    // Píxeles CSS por píxel de la imagen.
    get scale() {
        return this.zoom / this.dpr;
    }

    viewSize() {
        return { w: this.canvas.clientWidth, h: this.canvas.clientHeight };
    }

    imageToView(x, y) {
        return { x: this.origin.x + x * this.scale, y: this.origin.y + y * this.scale };
    }

    viewToImage(x, y) {
        return { x: (x - this.origin.x) / this.scale, y: (y - this.origin.y) / this.scale };
    }

    // --- Documento -------------------------------------------------------------------------

    setDocument(doc) {
        if (this.doc) {
            this.doc.viewState = { zoom: this.zoom, origin: { ...this.origin }, fit: this.fitOnResize };
            for (const [type, fn] of this.listeners)
                this.doc.removeEventListener(type, fn);
        }
        this.listeners = [];
        this.doc = doc;
        if (!doc) {
            this.update();
            return;
        }
        const on = (type, fn) => {
            doc.addEventListener(type, fn);
            this.listeners.push([type, fn]);
        };
        on("pixels", (e) => this.invalidate(e.detail.rect));
        on("layers", () => this.invalidate(null));
        on("selection", () => this.update());
        on("size", () => {
            doc.compositeCanvas = null;
            this.clampOrigin();
            this.update();
            this.emitZoom();
        });
        if (!doc.compositeCanvas)
            this.invalidate(null);
        if (doc.viewState) {
            this.zoom = doc.viewState.zoom;
            this.origin = { ...doc.viewState.origin };
            this.fitOnResize = doc.viewState.fit;
            this.clampOrigin();
        } else {
            this.zoomToWindow();
        }
        this.update();
        this.emitZoom();
    }

    invalidate(rect) {
        const doc = this.doc;
        if (!doc)
            return;
        const full = { x: 0, y: 0, w: doc.width, h: doc.height };
        doc.compositeDirty = rect ? unionRect(doc.compositeDirty, clampRect(rect, doc.width, doc.height)) : full;
        this.update();
    }

    updateComposite() {
        const doc = this.doc;
        if (!doc.compositeCanvas || doc.compositeCanvas.width !== doc.width || doc.compositeCanvas.height !== doc.height) {
            doc.compositeCanvas = createCanvas(doc.width, doc.height);
            doc.compositeDirty = { x: 0, y: 0, w: doc.width, h: doc.height };
        }
        const dirty = doc.compositeDirty;
        if (dirty && dirty.w > 0 && dirty.h > 0)
            doc.composite(context(doc.compositeCanvas), dirty);
        doc.compositeDirty = null;
    }

    // --- Zum y desplazamiento --------------------------------------------------------------

    emitZoom() {
        this.dispatchEvent(new Event("zoom"));
    }

    clampOrigin() {
        if (!this.doc)
            return;
        const v = this.viewSize();
        const clampAxis = (pos, content, view) => {
            if (content + 2 * kMargin <= view)
                return (view - content) / 2;
            return Math.min(kMargin, Math.max(view - content - kMargin, pos));
        };
        this.origin.x = Math.round(clampAxis(this.origin.x, this.doc.width * this.scale, v.w));
        this.origin.y = Math.round(clampAxis(this.origin.y, this.doc.height * this.scale, v.h));
        this.updateScrollbars();
    }

    setZoom(zoom, anchor = null) {
        if (!this.doc)
            return;
        zoom = Math.min(64, Math.max(0.01, zoom));
        const v = this.viewSize();
        const a = anchor || { x: v.w / 2, y: v.h / 2 };
        const img = this.viewToImage(a.x, a.y);
        this.zoom = zoom;
        this.origin = { x: a.x - img.x * this.scale, y: a.y - img.y * this.scale };
        this.fitOnResize = false;
        this.clampOrigin();
        this.update();
        this.emitZoom();
    }

    zoomIn(anchor = null) {
        const next = ZoomPresets.find((z) => z > this.zoom * 1.0001);
        this.setZoom(next ?? 64, anchor);
    }

    zoomOut(anchor = null) {
        const prev = [...ZoomPresets].reverse().find((z) => z < this.zoom * 0.9999);
        this.setZoom(prev ?? 0.01, anchor);
    }

    fitZoom() {
        const v = this.viewSize();
        if (!this.doc || v.w <= 0 || v.h <= 0)
            return 1;
        const z = Math.min((v.w - 2 * kMargin) / this.doc.width, (v.h - 2 * kMargin) / this.doc.height) * this.dpr;
        return Math.min(1, Math.max(0.01, z));
    }

    zoomToWindow() {
        if (!this.doc)
            return;
        this.zoom = this.fitZoom();
        this.origin = { x: -1e9, y: -1e9 };
        this.clampOrigin();
        const v = this.viewSize();
        this.origin.x = Math.round((v.w - this.doc.width * this.scale) / 2);
        this.origin.y = Math.round((v.h - this.doc.height * this.scale) / 2);
        this.clampOrigin();
        this.fitOnResize = true;
        this.update();
        this.emitZoom();
    }

    zoomToRect(r) {
        if (!this.doc || r.w <= 0 || r.h <= 0)
            return;
        const v = this.viewSize();
        this.setZoom(Math.min((v.w - 2 * kMargin) / r.w, (v.h - 2 * kMargin) / r.h) * this.dpr);
        this.centerOn(r.x + r.w / 2, r.y + r.h / 2);
    }

    centerOn(x, y) {
        const v = this.viewSize();
        this.origin = { x: v.w / 2 - x * this.scale, y: v.h / 2 - y * this.scale };
        this.clampOrigin();
        this.update();
    }

    scrollBy(dx, dy) {
        this.origin.x -= dx;
        this.origin.y -= dy;
        this.fitOnResize = false;
        this.clampOrigin();
        this.update();
    }

    resized() {
        const dpr = this.dpr, v = this.viewSize();
        this.canvas.width = Math.max(1, Math.round(v.w * dpr));
        this.canvas.height = Math.max(1, Math.round(v.h * dpr));
        if (this.doc && this.fitOnResize)
            this.zoomToWindow();
        else
            this.clampOrigin();
        this.render();
    }

    // --- Barras de desplazamiento ----------------------------------------------------------

    installScrollbars() {
        for (const [bar, horizontal] of [[this.hScroll, true], [this.vScroll, false]]) {
            const thumb = bar.querySelector(".thumb");
            let start = null;
            thumb.addEventListener("pointerdown", (e) => {
                e.preventDefault();
                thumb.setPointerCapture(e.pointerId);
                start = { pos: horizontal ? e.clientX : e.clientY, origin: { ...this.origin } };
            });
            thumb.addEventListener("pointermove", (e) => {
                if (!start || !this.doc)
                    return;
                const v = this.viewSize();
                const content = (horizontal ? this.doc.width : this.doc.height) * this.scale + 2 * kMargin;
                const track = horizontal ? bar.clientWidth : bar.clientHeight;
                const delta = ((horizontal ? e.clientX : e.clientY) - start.pos) * (content / track);
                void v;
                if (horizontal) this.origin.x = start.origin.x - delta;
                else this.origin.y = start.origin.y - delta;
                this.fitOnResize = false;
                this.clampOrigin();
                this.update();
            });
            thumb.addEventListener("pointerup", () => (start = null));
            bar.addEventListener("pointerdown", (e) => {
                if (e.target !== bar || !this.doc)
                    return;
                const r = thumb.getBoundingClientRect();
                const before = horizontal ? e.clientX < r.left : e.clientY < r.top;
                const page = (horizontal ? this.viewSize().w : this.viewSize().h) * 0.9;
                horizontal ? this.scrollBy(before ? -page : page, 0) : this.scrollBy(0, before ? -page : page);
            });
        }
    }

    updateScrollbars() {
        const v = this.viewSize();
        for (const [bar, horizontal] of [[this.hScroll, true], [this.vScroll, false]]) {
            const thumb = bar.querySelector(".thumb");
            if (!this.doc) {
                thumb.style.display = "none";
                continue;
            }
            const view = horizontal ? v.w : v.h;
            const content = (horizontal ? this.doc.width : this.doc.height) * this.scale + 2 * kMargin;
            const track = horizontal ? bar.clientWidth : bar.clientHeight;
            if (content <= view + 1) {
                thumb.style.display = "none";
                continue;
            }
            const pos = kMargin - (horizontal ? this.origin.x : this.origin.y);
            const size = Math.max(24, (track * view) / content);
            const at = Math.min(track - size, Math.max(0, (pos / (content - view)) * (track - size)));
            thumb.style.display = "block";
            if (horizontal) { thumb.style.left = `${at}px`; thumb.style.width = `${size}px`; }
            else { thumb.style.top = `${at}px`; thumb.style.height = `${size}px`; }
        }
    }

    // --- Dibujo ----------------------------------------------------------------------------

    update() {
        if (this.pending)
            return;
        this.pending = true;
        requestAnimationFrame(() => {
            this.pending = false;
            this.render();
        });
    }

    stepAnts() {
        const selection = this.doc && !this.doc.selection.isEmpty();
        if (!selection && !this.overlayAnts)
            return;
        this.antsOffset = (this.antsOffset + 1) % 8;
        this.update();
    }

    render() {
        const ctx = this.ctx, dpr = this.dpr, v = this.viewSize();
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        const styles = getComputedStyle(document.documentElement);
        ctx.fillStyle = styles.getPropertyValue("--workspace").trim() || "#cfcfcf";
        ctx.fillRect(0, 0, v.w, v.h);
        const doc = this.doc;
        if (!doc) {
            this.updateScrollbars();
            return;
        }
        this.updateComposite();
        const x = this.origin.x, y = this.origin.y, w = doc.width * this.scale, h = doc.height * this.scale;
        // Sombra bajo el lienzo, como Paint.NET.
        if (Settings.get("dropShadow")) {
            ctx.save();
            ctx.shadowColor = "rgba(0,0,0,0.35)";
            ctx.shadowBlur = 8;
            ctx.shadowOffsetX = 1;
            ctx.shadowOffsetY = 2;
            ctx.fillStyle = "#808080";
            ctx.fillRect(x, y, w, h);
            ctx.restore();
        }
        ctx.save();
        ctx.beginPath();
        ctx.rect(x, y, w, h);
        ctx.clip();
        ctx.fillStyle = checkerPattern(ctx, Settings.get("checkerBrightness"));
        ctx.save();
        ctx.translate(Math.round(x), Math.round(y));
        ctx.fillRect(0, 0, w + 1, h + 1);
        ctx.restore();
        ctx.imageSmoothingEnabled = this.zoom < 1;
        ctx.imageSmoothingQuality = "high";
        ctx.drawImage(doc.compositeCanvas, x, y, w, h);
        ctx.restore();
        if (this.pixelGrid && this.zoom >= 2)
            this.drawPixelGrid(ctx, v);
        this.drawSelection(ctx);
        if (this.tool) {
            ctx.save();
            this.tool.paintOverlay?.(ctx, this);
            ctx.restore();
            const diameter = this.tool.brushDiameter?.() ?? 0;
            if (diameter > 0 && this.mouse && !this.panning) {
                const r = (diameter * this.scale) / 2;
                if (r >= 2) {
                    ctx.beginPath();
                    ctx.arc(this.mouse.vx, this.mouse.vy, r + 1, 0, Math.PI * 2);
                    ctx.strokeStyle = "rgba(255,255,255,0.75)";
                    ctx.lineWidth = 1;
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.arc(this.mouse.vx, this.mouse.vy, r, 0, Math.PI * 2);
                    ctx.strokeStyle = "rgba(0,0,0,0.75)";
                    ctx.stroke();
                }
            }
        }
        this.updateScrollbars();
    }

    drawPixelGrid(ctx, v) {
        const doc = this.doc, s = this.scale;
        const x0 = Math.max(0, Math.floor(-this.origin.x / s)), y0 = Math.max(0, Math.floor(-this.origin.y / s));
        const x1 = Math.min(doc.width, Math.ceil((v.w - this.origin.x) / s)), y1 = Math.min(doc.height, Math.ceil((v.h - this.origin.y) / s));
        ctx.save();
        ctx.strokeStyle = "#bbbbbb";
        ctx.lineWidth = 1 / this.dpr;
        ctx.setLineDash([1, 1]);
        ctx.beginPath();
        for (let x = x0; x <= x1; ++x) {
            const vx = Math.round((this.origin.x + x * s) * this.dpr) / this.dpr + 0.5 / this.dpr;
            ctx.moveTo(vx, this.origin.y + y0 * s);
            ctx.lineTo(vx, this.origin.y + y1 * s);
        }
        for (let y = y0; y <= y1; ++y) {
            const vy = Math.round((this.origin.y + y * s) * this.dpr) / this.dpr + 0.5 / this.dpr;
            ctx.moveTo(this.origin.x + x0 * s, vy);
            ctx.lineTo(this.origin.x + x1 * s, vy);
        }
        ctx.stroke();
        ctx.restore();
    }

    // Interior azul translúcido (con las herramientas de selección) y las hormigas en movimiento.
    drawSelection(ctx) {
        const sel = this.doc.selection;
        if (sel.isEmpty() || this.tool?.hidesSelectionOutline?.())
            return;
        if (this.tool?.highlightSelection) {
            if (sel.tintCanvas === undefined) {
                const b = sel.bounds();
                const tint = createCanvas(b.w, b.h), tc = context(tint), img = tc.createImageData(b.w, b.h);
                for (let y = 0; y < b.h; ++y)
                    for (let x = 0; x < b.w; ++x) {
                        const m = sel.mask[(b.y + y) * sel.width + b.x + x], i = (y * b.w + x) * 4;
                        img.data[i + 1] = 120; img.data[i + 2] = 215; img.data[i + 3] = (m * 45) / 255;
                    }
                tc.putImageData(img, 0, 0);
                sel.tintCanvas = tint;
            }
            const b = sel.bounds(), p = this.imageToView(b.x, b.y);
            ctx.save();
            ctx.imageSmoothingEnabled = this.zoom < 1;
            ctx.drawImage(sel.tintCanvas, p.x, p.y, b.w * this.scale, b.h * this.scale);
            ctx.restore();
        }
        this.strokeSegments(ctx, sel.outline());
    }

    // Tramos de contorno [x1, y1, x2, y2, ...] en coordenadas de la imagen, con hormigas.
    strokeSegments(ctx, segs, transform = null) {
        ctx.save();
        const px = (n) => Math.round(n * this.dpr) / this.dpr + 0.5 / this.dpr;
        ctx.beginPath();
        for (let i = 0; i < segs.length; i += 4) {
            let [x1, y1, x2, y2] = [segs[i], segs[i + 1], segs[i + 2], segs[i + 3]];
            if (transform) {
                const a = transform.transformPoint({ x: x1, y: y1 }), b = transform.transformPoint({ x: x2, y: y2 });
                [x1, y1, x2, y2] = [a.x, a.y, b.x, b.y];
            }
            const a = this.imageToView(x1, y1), b = this.imageToView(x2, y2);
            ctx.moveTo(transform ? a.x : px(a.x), transform ? a.y : px(a.y));
            ctx.lineTo(transform ? b.x : px(b.x), transform ? b.y : px(b.y));
        }
        ctx.lineWidth = 1 / this.dpr;
        ctx.strokeStyle = "#fff";
        ctx.stroke();
        ctx.setLineDash([4 / this.dpr, 4 / this.dpr]);
        ctx.lineDashOffset = -this.antsOffset / this.dpr;
        ctx.strokeStyle = "#000";
        ctx.stroke();
        ctx.restore();
    }

    // Rectángulo de la imagen dibujado como una selección rectangular (la herramienta Zum).
    drawMarchingRect(ctx, r, tint = true) {
        const a = this.imageToView(r.x, r.y), b = this.imageToView(r.x + r.w, r.y + r.h);
        if (tint) {
            ctx.fillStyle = "rgba(0,120,215,0.18)";
            ctx.fillRect(a.x, a.y, b.x - a.x, b.y - a.y);
        }
        this.strokeSegments(ctx, [r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x + r.w, r.y + r.h,
                                  r.x, r.y + r.h, r.x, r.y + r.h, r.x, r.y]);
    }

    setOverlayAnts(on) {
        this.overlayAnts = on;
    }

    // --- Ratón -----------------------------------------------------------------------------

    toolEvent(e) {
        const rect = this.canvas.getBoundingClientRect();
        const vx = e.clientX - rect.left, vy = e.clientY - rect.top;
        const p = this.viewToImage(vx, vy);
        return {
            x: p.x, y: p.y, vx, vy, button: e.button === 2 ? "right" : e.button === 1 ? "middle" : "left",
            shift: e.shiftKey, ctrl: e.ctrlKey || e.metaKey, alt: e.altKey, pressure: e.pressure || 0.5,
            pointerType: e.pointerType, original: e,
        };
    }

    updateCursor() {
        if (this.panning)
            this.canvas.style.cursor = handCursor(true);
        else if (this.spaceDown)
            this.canvas.style.cursor = handCursor(false);
        else
            this.canvas.style.cursor = this.doc && this.tool ? this.tool.cursor?.() || "default" : "default";
    }

    installEvents() {
        const c = this.canvas;
        c.addEventListener("contextmenu", (e) => e.preventDefault());
        c.addEventListener("pointerdown", (e) => {
            c.focus({ preventScroll: true });
            if (!this.doc)
                return;
            c.setPointerCapture(e.pointerId);
            const te = this.toolEvent(e);
            if (e.button === 1 || (this.spaceDown && e.button === 0) || (e.button === 0 && this.tool?.id === "pan")) {
                this.panning = { x: e.clientX, y: e.clientY };
                this.updateCursor();
                e.preventDefault();
                return;
            }
            if (e.button === 3 || e.button === 4) {
                this.tool?.extraButton?.(te, true);
                return;
            }
            this.tool?.onPointerDown?.(te);
            this.update();
        });
        c.addEventListener("pointermove", (e) => {
            if (!this.doc)
                return;
            const te = this.toolEvent(e);
            this.mouse = te;
            this.dispatchEvent(new CustomEvent("cursor", { detail: te }));
            if (this.panning) {
                this.scrollBy(this.panning.x - e.clientX, this.panning.y - e.clientY);
                this.panning = { x: e.clientX, y: e.clientY };
                return;
            }
            // Eventos agrupados (trazos más finos con lápices y ratones rápidos).
            const events = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
            for (const ce of events.length ? events : [e])
                this.tool?.onPointerMove?.(this.toolEvent(ce));
            this.update();
        });
        const up = (e) => {
            if (!this.doc)
                return;
            if (this.panning) {
                this.panning = null;
                this.updateCursor();
                return;
            }
            if (e.button === 3 || e.button === 4) {
                this.tool?.extraButton?.(this.toolEvent(e), false);
                return;
            }
            this.tool?.onPointerUp?.(this.toolEvent(e));
            this.update();
        };
        c.addEventListener("pointerup", up);
        c.addEventListener("pointercancel", up);
        c.addEventListener("pointerleave", () => {
            this.mouse = null;
            this.dispatchEvent(new CustomEvent("cursor", { detail: null }));
            this.update();
        });
        c.addEventListener("dblclick", (e) => this.tool?.onDoubleClick?.(this.toolEvent(e)));
        c.addEventListener("wheel", (e) => {
            if (!this.doc)
                return;
            e.preventDefault();
            const rect = c.getBoundingClientRect();
            const anchor = { x: e.clientX - rect.left, y: e.clientY - rect.top };
            const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1;
            if (e.ctrlKey || e.metaKey) {
                const factor = Math.exp((-e.deltaY * unit) / 300);
                if (Math.abs(e.deltaY * unit) >= 50)
                    e.deltaY < 0 ? this.zoomIn(anchor) : this.zoomOut(anchor);
                else
                    this.setZoom(this.zoom * factor, anchor);
            } else if (e.shiftKey) {
                this.scrollBy(e.deltaY * unit, 0);
            } else {
                this.scrollBy(e.deltaX * unit, e.deltaY * unit);
            }
        }, { passive: false });
    }
}

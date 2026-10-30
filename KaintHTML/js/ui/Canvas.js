// Lienzo (src/app/Canvas.cpp de Kaint): muestra la imagen compuesta con el zum (100 % = un
// píxel de la imagen por píxel físico de la pantalla), el damero, la sombra, la cuadrícula de
// píxeles, la selección con hormigas y lo que dibuje la herramienta; reparte el ratón a la
// herramienta y se desplaza con la rueda, el botón central o la barra espaciadora.
import { checkerPattern, clampRect, context, createCanvas, unionRect } from "../core/Raster.js";
import { handCursor, outlineCursor, paintMini, pdnCursors } from "./Icons.js";
import { Settings } from "../Settings.js";
import { pixelsPerUnit } from "./Units.js";
import { isSrgb, transformPixels } from "../core/ColorProfiles.js";

const kMargin = 24;
// Grosor de las reglas (Ver > Reglas).
export const kRuler = 20;

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
        this.hRuler = workspace.querySelector("canvas.ruler.h");
        this.vRuler = workspace.querySelector("canvas.ruler.v");
        // Miniatura que acompaña al puntero del sistema (Icons.miniCursor).
        this.miniIcon = document.createElement("canvas");
        this.miniIcon.className = "mini-pointer";
        workspace.appendChild(this.miniIcon);
        this.rulersVisible = !!Settings.get("rulers");
        workspace.classList.toggle("rulers", this.rulersVisible);
        new ResizeObserver(() => this.resized()).observe(workspace);
        this.installEvents();
        this.installScrollbars();
        setInterval(() => this.stepAnts(), 120);
        window.addEventListener("kaint-settings", () => {
            this.clampOrigin();
            this.update();
        });
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
        if (dirty && dirty.w > 0 && dirty.h > 0) {
            const ctx = context(doc.compositeCanvas);
            doc.composite(ctx, dirty);
            // Con otro perfil de color, la imagen se ve convertida a sRGB (como Kaint).
            if (doc.colorProfile && !isSrgb(doc.colorProfile)) {
                const part = ctx.getImageData(dirty.x, dirty.y, dirty.w, dirty.h);
                transformPixels(part.data, doc.colorProfile, null);
                ctx.putImageData(part, dirty.x, dirty.y);
            }
        }
        doc.compositeDirty = null;
    }

    // --- Zum y desplazamiento --------------------------------------------------------------

    emitZoom() {
        this.updateCursor();
        this.dispatchEvent(new Event("zoom"));
    }

    // Límites del origen como en Kaint: con overscroll (Configuración), la imagen se puede llevar
    // hasta que su borde llega al centro de la vista; si no, centrada si cabe o con un margen.
    originLimits(content, view) {
        if (Settings.get("overscroll") !== false)
            return [view / 2 - content, view / 2];
        if (content + 2 * kMargin <= view)
            return [(view - content) / 2, (view - content) / 2];
        return [view - content - kMargin, kMargin];
    }

    clampOrigin() {
        if (!this.doc)
            return;
        const v = this.viewSize();
        const clampAxis = (pos, content, view) => {
            const [lo, hi] = this.originLimits(content, view);
            return Math.min(hi, Math.max(lo, pos));
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
        // Justo ese rectángulo, sin margen: llena la vista a lo ancho o a lo alto y queda centrado.
        const v = this.viewSize();
        this.setZoom(Math.min(v.w / r.w, v.h / r.h) * this.dpr);
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
                const v = this.viewSize(), view = horizontal ? v.w : v.h;
                const [lo, hi] = this.originLimits((horizontal ? this.doc.width : this.doc.height) * this.scale, view);
                const track = horizontal ? bar.clientWidth : bar.clientHeight;
                const delta = ((horizontal ? e.clientX : e.clientY) - start.pos) * ((hi - lo + view) / track);
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
            // Como las barras de Kaint: recorren todo lo que se puede desplazar el origen.
            const view = horizontal ? v.w : v.h;
            const [lo, hi] = this.originLimits((horizontal ? this.doc.width : this.doc.height) * this.scale, view);
            const track = horizontal ? bar.clientWidth : bar.clientHeight;
            if (hi - lo < 1) {
                thumb.style.display = "none";
                continue;
            }
            const total = hi - lo + view;
            const pos = hi - (horizontal ? this.origin.x : this.origin.y);
            const size = Math.max(24, (track * view) / total);
            const at = Math.min(track - size, Math.max(0, (pos / (hi - lo)) * (track - size)));
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
            this.renderRulers();
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
        this.renderRulers();
        if (this.tool) {
            ctx.save();
            this.tool.paintOverlay?.(ctx, this);
            ctx.restore();
            const diameter = this.tool.brushDiameter?.() ?? 0;
            if (diameter > 0 && this.mouse && !this.panning && !this.outlineInCursor) {
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

    // --- Desplazamiento automático ------------------------------------------------------------

    // Como en Kaint: al arrastrar con una herramienta junto al borde (o fuera) de la vista, se
    // desplaza más deprisa cuanto más lejos esté el puntero.
    updateAutoScroll(te, buttons) {
        const tool = this.tool;
        if (!buttons || !Settings.get("autoScroll") || !tool || tool.id === "pan" || tool.id === "zoom" || this.panning) {
            this.stopAutoScroll();
            return;
        }
        const v = this.viewSize(), margin = 20;
        const axis = (p, size) => (p < margin ? -(margin - p) : p > size - margin ? p - (size - margin) : 0);
        const clamp = (d) => Math.max(-40, Math.min(40, d * 0.6));
        this.autoScroll = { dx: clamp(axis(te.vx, v.w)), dy: clamp(axis(te.vy, v.h)), event: te };
        if (!this.autoScroll.dx && !this.autoScroll.dy) {
            this.stopAutoScroll();
            return;
        }
        if (!this.autoScrollTimer)
            this.autoScrollTimer = setInterval(() => this.autoScrollStep(), 16);
    }

    autoScrollStep() {
        const a = this.autoScroll;
        if (!a || !this.doc || !this.tool) {
            this.stopAutoScroll();
            return;
        }
        const before = { ...this.origin };
        this.scrollBy(a.dx, a.dy);
        if (before.x === this.origin.x && before.y === this.origin.y)
            return;
        const p = this.viewToImage(a.event.vx, a.event.vy);
        this.tool.onPointerMove?.({ ...a.event, x: p.x, y: p.y });
    }

    // La vista se movió (rueda) bajo el puntero mientras se arrastra con una herramienta: le
    // llega la nueva posición en la imagen, como si se hubiera movido el ratón.
    followPointer(view) {
        const te = this.dragEvent;
        if (!te || !this.tool || this.panning)
            return;
        const p = this.viewToImage(view.x, view.y);
        this.dragEvent = { ...te, vx: view.x, vy: view.y, x: p.x, y: p.y };
        this.tool.onPointerMove?.(this.dragEvent);
        this.update();
    }

    stopAutoScroll() {
        clearInterval(this.autoScrollTimer);
        this.autoScrollTimer = null;
        this.autoScroll = null;
    }

    // --- Reglas ------------------------------------------------------------------------------

    setRulersVisible(on) {
        this.rulersVisible = on;
        Settings.set("rulers", on);
        this.workspace.classList.toggle("rulers", on);
        this.resized();
        this.dispatchEvent(new Event("rulers"));
    }

    // Como las de paint.js (Configuración > Lienzo): oscuras, con una marca corta y el número cada
    // 1, 2, 5, 10, 20, 40... píxeles (al menos 35 px entre marcas), o del color de la barra de
    // herramientas con marcas intermedias. Resaltan la selección y la posición del cursor.
    renderRulers() {
        if (!this.rulersVisible)
            return;
        // 0: según el tema (oscura con los temas oscuros); 1: del color de la barra; 2: oscura.
        const style = Settings.get("rulerStyle");
        const paintJs = style === 2 || (style !== 1 && document.documentElement.classList.contains("dark"));
        this.workspace.classList.toggle("ruler-classic", !paintJs);
        const styles = getComputedStyle(document.documentElement);
        const theme = (name, fallback) => styles.getPropertyValue(name).trim() || fallback;
        const colors = paintJs ? { bg: "#202020", tick: "#a8a8a8", text: "#e5e5e5", edge: "#575757" }
                               : { bg: theme("--toolbar", "#fff"), tick: theme("--text", "#000"), text: theme("--text", "#000"), edge: theme("--line", "#999") };
        const accent = theme("--accent", "#3399ff");
        for (const vertical of [false, true]) {
            const canvas = vertical ? this.vRuler : this.hRuler;
            if (!canvas)
                continue;
            const dpr = this.dpr, w = canvas.clientWidth, h = canvas.clientHeight;
            if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
                canvas.width = Math.round(w * dpr);
                canvas.height = Math.round(h * dpr);
            }
            const ctx = canvas.getContext("2d");
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            // Azul: en degradado hacia el lienzo, como en Paint.NET.
            if (!paintJs && document.documentElement.classList.contains("blue")) {
                const g = vertical ? ctx.createLinearGradient(0, 0, w, 0) : ctx.createLinearGradient(0, 0, 0, h);
                g.addColorStop(0, "#fbfdff");
                g.addColorStop(1, "#f2f6fc");
                ctx.fillStyle = g;
            } else {
                ctx.fillStyle = colors.bg;
            }
            ctx.fillRect(0, 0, w, h);
            const doc = this.doc;
            if (doc) {
                // En la unidad de medida elegida (Ver > Píxeles, Pulgadas o Centímetros).
                const unit = Settings.get("unit") ?? 0, ppu = pixelsPerUnit(unit, doc.dpi);
                const t = kRuler, length = vertical ? h : w, origin = vertical ? this.origin.y : this.origin.x, s = this.scale * ppu;
                const b = doc.selection.bounds();
                if (b) {
                    ctx.globalAlpha = paintJs ? 0.45 : 0.35;
                    ctx.fillStyle = accent;
                    const a = origin + (vertical ? b.y : b.x) * this.scale, size = (vertical ? b.h : b.w) * this.scale;
                    vertical ? ctx.fillRect(0, a, t, size) : ctx.fillRect(a, 0, size, t);
                    ctx.globalAlpha = 1;
                }
                // paint.js: en píxeles 1, 2, 5, 10, 20, 40, 80... con al menos 35 px entre marcas; si
                // no (o en pulgadas y centímetros), 1, 2, 5 × 10^n con al menos ~60 px.
                let step = 1;
                if (paintJs && unit === 0) {
                    while (step * s < 35 && step < 1e7)
                        step *= step === 2 ? 2.5 : 2;
                } else {
                    const minGap = paintJs ? 35 : 60;
                    outer: for (let mag = 1e-3; mag < 1e7; mag *= 10)
                        for (const k of [1, 2, 5])
                            if (k * mag * s >= minGap) {
                                step = k * mag;
                                break outer;
                            }
                }
                const minor = paintJs ? step : step / 5;
                const first = Math.ceil(-origin / s / minor) * minor - minor, last = (length - origin) / s;
                ctx.font = `10px ${theme("--font", "sans-serif")}`;
                ctx.lineWidth = 1;
                for (let value = first; value <= last; value += minor) {
                    const pos = Math.round(origin + value * s) + 0.5;
                    const major = Math.abs(value / step - Math.round(value / step)) < 1e-6;
                    const tick = paintJs ? 6 : major ? t : t / 3;
                    ctx.strokeStyle = colors.tick;
                    ctx.beginPath();
                    if (vertical) {
                        ctx.moveTo(t - tick, pos);
                        ctx.lineTo(t, pos);
                    } else {
                        ctx.moveTo(pos, t - tick);
                        ctx.lineTo(pos, t);
                    }
                    ctx.stroke();
                    if (!major)
                        continue;
                    ctx.fillStyle = colors.text;
                    const label = String(Math.abs(value) < step * 1e-6 ? 0 : Number(value.toPrecision(6)));
                    if (vertical) {
                        // Como paint.js, de abajo arriba sobre la marca.
                        ctx.save();
                        ctx.translate(t - 9, pos + 2);
                        ctx.rotate(-Math.PI / 2);
                        ctx.fillText(label, 0, 0);
                        ctx.restore();
                    } else {
                        ctx.fillText(label, pos + 2, 12);
                    }
                }
                // Como en Paint.NET, la marca se queda en la última posición del puntero aunque salga.
                if (this.lastMouse) {
                    const m = this.imageToView(this.lastMouse.x, this.lastMouse.y);
                    const c = Math.round(vertical ? m.y : m.x) + 0.5;
                    ctx.strokeStyle = accent;
                    ctx.beginPath();
                    vertical ? (ctx.moveTo(0, c), ctx.lineTo(t, c)) : (ctx.moveTo(c, 0), ctx.lineTo(c, t));
                    ctx.stroke();
                }
            }
            ctx.strokeStyle = colors.edge;
            ctx.beginPath();
            vertical ? (ctx.moveTo(w - 0.5, 0), ctx.lineTo(w - 0.5, h)) : (ctx.moveTo(0, h - 0.5), ctx.lineTo(w, h - 0.5));
            ctx.stroke();
        }
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
        this.strokeSelection(ctx, sel);
    }

    // Contorno de una selección con hormigas ('transform': donde quedará al moverla): el trazado de
    // las que salen de uno (elipse, lazo...), como en Kaint, o los bordes de los píxeles.
    strokeSelection(ctx, sel, transform = null) {
        if (sel.path)
            this.strokePath(ctx, sel.path, transform);
        else
            this.strokeOutline(ctx, sel.outline(), transform);
    }

    // Blanco y, encima, negro a trazos que se mueven, de un píxel de la pantalla.
    strokeAnts(ctx, path) {
        ctx.save();
        ctx.lineWidth = 1 / this.dpr;
        ctx.strokeStyle = "#fff";
        ctx.stroke(path);
        ctx.setLineDash([4 / this.dpr, 4 / this.dpr]);
        ctx.lineDashOffset = -this.antsOffset / this.dpr;
        ctx.strokeStyle = "#000";
        ctx.stroke(path);
        ctx.restore();
    }

    // Un trazado en coordenadas de la imagen, suavizado como en Kaint.
    strokePath(ctx, path, transform = null) {
        let m = new DOMMatrix([this.scale, 0, 0, this.scale, this.origin.x, this.origin.y]);
        if (transform)
            m = m.multiply(transform);
        const view = new Path2D();
        view.addPath(path, m);
        this.strokeAnts(ctx, view);
    }

    // Contornos cerrados [[x, y, x, y, ...], ...] en coordenadas de la imagen; sin transformar, en
    // píxeles enteros de la pantalla para que se vean nítidos.
    strokeOutline(ctx, loops, transform = null) {
        const px = (n) => Math.round(n * this.dpr) / this.dpr + 0.5 / this.dpr;
        const path = new Path2D();
        for (const loop of loops) {
            for (let i = 0; i < loop.length; i += 2) {
                let p = { x: loop[i], y: loop[i + 1] };
                if (transform)
                    p = transform.transformPoint(p);
                const v = this.imageToView(p.x, p.y), x = transform ? v.x : px(v.x), y = transform ? v.y : px(v.y);
                i ? path.lineTo(x, y) : path.moveTo(x, y);
            }
            path.closePath();
        }
        this.strokeAnts(ctx, path);
    }

    // Rectángulo de la imagen dibujado como una selección rectangular (la herramienta Zum).
    drawMarchingRect(ctx, r, tint = true) {
        const a = this.imageToView(r.x, r.y), b = this.imageToView(r.x + r.w, r.y + r.h);
        if (tint) {
            ctx.fillStyle = "rgba(0,120,215,0.18)";
            ctx.fillRect(a.x, a.y, b.x - a.x, b.y - a.y);
        }
        this.strokeOutline(ctx, [[r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h]]);
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
        this.outlineInCursor = false;
        if (this.panning) {
            this.setCursor(handCursor(true));
        } else if (this.spaceDown) {
            this.setCursor(handCursor(false));
        } else {
            const outline = this.brushCursor();
            this.outlineInCursor = !!outline && !this.outlineNearEdge;
            this.setCursor(this.outlineInCursor ? outline.css : this.doc && this.tool ? this.tool.cursor?.() || "default" : "default");
        }
    }

    // Cursor del lienzo; uno con miniatura sobre el puntero del sistema (miniCursor) lleva al
    // lado la miniatura, que sigue al ratón.
    setCursor(css) {
        this.canvas.style.cursor = css;
        const mini = css?.mini || null;
        // Se repinta siempre: algunas llevan los colores activos (el degradado).
        if (mini)
            paintMini(this.miniIcon, mini.name);
        this.mini = mini;
        this.placeMini();
    }

    // Abajo a la derecha de la flecha (o de la I), como en los cursores de Kaint.
    placeMini() {
        const show = !!(this.mini && this.mouse && !this.panning && !this.spaceDown);
        this.miniIcon.style.display = show ? "block" : "none";
        if (!show)
            return;
        const [dx, dy] = this.mini.ibeam ? [5, 3] : [12, 13];
        this.miniIcon.style.transform = `translate(${Math.round(this.canvas.offsetLeft + this.mouse.vx + dx)}px, ${Math.round(this.canvas.offsetTop + this.mouse.vy + dy)}px)`;
    }

    // Con los pinceles, el contorno va en el propio cursor si cabe: así no se queda atrás del
    // puntero (dibujado en el lienzo, llega un fotograma o más tarde).
    brushCursor() {
        // Con los cursores de Paint.NET el cursor es la cruz y el contorno se dibuja en el lienzo.
        if (pdnCursors())
            return null;
        const diameter = this.doc ? this.tool?.brushDiameter?.() ?? 0 : 0;
        const r = (diameter * this.scale) / 2;
        return r >= 2 ? outlineCursor(this.tool.icon, r) : null;
    }

    // Los navegadores no dejan que un cursor de más de 32 px tape su propia interfaz: junto al
    // borde de la ventana el contorno vuelve a dibujarse en el lienzo.
    checkOutlineEdge(e) {
        const half = this.brushCursor()?.half ?? 0;
        const near = half > 16 && (e.clientX < half || e.clientY < half || window.innerWidth - e.clientX < half
                                   || window.innerHeight - e.clientY < half);
        if (near !== !!this.outlineNearEdge) {
            this.outlineNearEdge = near;
            this.updateCursor();
        }
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
            // Como en Kaint: el extremo borrador del lápiz usa la Goma mientras se pinta.
            if (e.pointerType === "pen" && (e.button === 5 || (e.buttons & 32)) && Settings.get("tabletEraser") !== false && this.tool?.id !== "eraser") {
                this.dispatchEvent(new CustomEvent("pen-eraser", { detail: true }));
                te.button = "left";
            }
            this.dragEvent = te;
            this.tool?.onPointerDown?.(te);
            this.update();
        });
        c.addEventListener("pointermove", (e) => {
            if (!this.doc)
                return;
            const te = this.toolEvent(e);
            this.mouse = te;
            this.lastMouse = { x: te.x, y: te.y };
            this.placeMini();
            this.checkOutlineEdge(e);
            this.dispatchEvent(new CustomEvent("cursor", { detail: te }));
            if (this.panning) {
                this.scrollBy(this.panning.x - e.clientX, this.panning.y - e.clientY);
                this.panning = { x: e.clientX, y: e.clientY };
                return;
            }
            this.updateAutoScroll(te, e.buttons);
            if (this.dragEvent)
                this.dragEvent = te;
            // Eventos agrupados (trazos más finos con lápices y ratones rápidos).
            const events = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
            for (const ce of events.length ? events : [e])
                this.tool?.onPointerMove?.(this.toolEvent(ce));
            this.update();
        });
        const up = (e) => {
            this.stopAutoScroll();
            if (!this.doc)
                return;
            if (e.button === 0 || e.button === 2 || e.type === "pointercancel")
                this.dragEvent = null;
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
            if (this.penEraser)
                this.dispatchEvent(new CustomEvent("pen-eraser", { detail: false }));
            this.update();
        };
        c.addEventListener("pointerup", up);
        c.addEventListener("pointercancel", up);
        c.addEventListener("pointerleave", () => {
            this.mouse = null;
            this.placeMini();
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
            // Como en Kaint: Ctrl+rueda acerca o aleja hacia el puntero con pasos de 1,12 por
            // muesca (parando en el 100 %), Mayús+rueda desplaza en horizontal y la rueda sola, en
            // vertical.
            if (e.ctrlKey || e.metaKey) {
                const notches = e.deltaMode === 1 ? e.deltaY / 3 : e.deltaMode === 2 ? e.deltaY : e.deltaY / 100;
                let z = this.zoom * Math.pow(1.12, -notches);
                if ((this.zoom < 1 && z > 1) || (this.zoom > 1 && z < 1))
                    z = 1;
                this.setZoom(z, anchor);
            } else if (e.shiftKey && !e.deltaX) {
                this.scrollBy(e.deltaY * unit, 0);
            } else {
                this.scrollBy(e.deltaX * unit, e.deltaY * unit);
            }
            this.followPointer(anchor);
        }, { passive: false });
    }
}

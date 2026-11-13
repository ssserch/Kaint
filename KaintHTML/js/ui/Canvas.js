// Lienzo (src/app/Canvas.cpp de Kaint): muestra la imagen compuesta con el zum (100 % = un
// píxel de la imagen por píxel físico de la pantalla), el damero, la sombra, la cuadrícula de
// píxeles, la selección con hormigas y lo que dibuje la herramienta; reparte el ratón a la
// herramienta y se desplaza con la rueda, el botón central o la barra espaciadora.
import { checkerPattern, clampRect, context, createCanvas, unionRect } from "../core/Raster.js";
import { handCursor, icon, outlineCursor, paintMini, pdnCursors } from "./Icons.js";
import { Settings } from "../Settings.js";
import { pixelsPerUnit } from "./Units.js";
import { isSrgb, transformPixels } from "../core/ColorProfiles.js";

const kMargin = 24;
// Dedos: lo que espera el primero antes de usar la herramienta (por si llega el segundo), lo que
// puede moverse mientras tanto sin empezar el trazo y hasta cuándo el segundo deshace el trazo.
const kTouchDelay = 90;
const kTouchSlop = 8;
const kTouchUndoWindow = 400;
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
        // Deshacer y rehacer cambian lo que hay bajo el puntero (el cuadro de la selección, la forma
        // que se edita): la herramienta vuelve a elegir su cursor sin esperar a que se mueva el ratón.
        on("history", () => setTimeout(() => this.refreshToolCursor(), 0));
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
            const at = (e) => (horizontal ? e.clientX : e.clientY);
            // Lo que se mueve el origen por cada píxel que se mueve el pulgar (lo inverso de
            // updateScrollbars): así el pulgar se queda justo bajo el ratón o el dedo.
            const ratio = () => {
                const v = this.viewSize(), view = horizontal ? v.w : v.h;
                const [lo, hi] = this.originLimits((horizontal ? this.doc.width : this.doc.height) * this.scale, view);
                const track = horizontal ? bar.clientWidth : bar.clientHeight;
                const size = horizontal ? thumb.offsetWidth : thumb.offsetHeight;
                return (hi - lo) / Math.max(1, track - size);
            };
            const moveTo = (origin, delta) => {
                if (horizontal) this.origin.x = origin.x - delta;
                else this.origin.y = origin.y - delta;
                this.fitOnResize = false;
                this.clampOrigin();
                this.update();
            };
            const begin = (e) => {
                e.preventDefault();
                this.stopInertia();
                bar.setPointerCapture(e.pointerId);
                start = { pos: at(e), origin: { ...this.origin }, id: e.pointerId };
                bar.classList.add("dragging");
            };
            const end = () => {
                start = null;
                bar.classList.remove("dragging");
            };
            bar.addEventListener("pointerdown", (e) => {
                if (!this.doc || e.button !== 0)
                    return;
                if (e.target === thumb) {
                    begin(e);
                    return;
                }
                const r = thumb.getBoundingClientRect();
                // Con el dedo, el pulgar salta bajo él y se sigue arrastrando (acertar en una barra
                // estrecha con el dedo cuesta); con el ratón, como siempre, una página.
                if (e.pointerType !== "mouse" && thumb.style.display !== "none") {
                    const center = horizontal ? (r.left + r.right) / 2 : (r.top + r.bottom) / 2;
                    moveTo({ ...this.origin }, (at(e) - center) * ratio());
                    begin(e);
                    return;
                }
                const before = horizontal ? e.clientX < r.left : e.clientY < r.top;
                const page = (horizontal ? this.viewSize().w : this.viewSize().h) * 0.9;
                horizontal ? this.scrollBy(before ? -page : page, 0) : this.scrollBy(0, before ? -page : page);
            });
            bar.addEventListener("pointermove", (e) => {
                if (!start || e.pointerId !== start.id || !this.doc)
                    return;
                moveTo(start.origin, (at(e) - start.pos) * ratio());
            });
            bar.addEventListener("pointerup", end);
            bar.addEventListener("pointercancel", end);
            bar.addEventListener("lostpointercapture", end);
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

    // La imagen tal como se ve, recortada a la vista y en coordenadas del área de trabajo: las
    // ventanas flotantes miran si la tapan (translucidez solo sobre la imagen). Solo se avisa si
    // cambió.
    reportImageRect(r, v) {
        let rect = null;
        if (r) {
            const x0 = Math.max(0, r.x), y0 = Math.max(0, r.y), x1 = Math.min(v.w, r.x + r.w), y1 = Math.min(v.h, r.y + r.h);
            if (x1 > x0 && y1 > y0)
                rect = { x: x0 + this.canvas.offsetLeft, y: y0 + this.canvas.offsetTop, w: x1 - x0, h: y1 - y0 };
        }
        const key = rect ? `${Math.round(rect.x)},${Math.round(rect.y)},${Math.round(rect.w)},${Math.round(rect.h)}` : "";
        if (key === this.imageRectKey)
            return;
        this.imageRectKey = key;
        this.onImageRect?.(rect);
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
        // Con una imagen o un vídeo de fondo (tema personalizado), alrededor de la imagen se ve la
        // capa de detrás (Background.js).
        if (document.documentElement.classList.contains("workspace-media")) {
            ctx.clearRect(0, 0, v.w, v.h);
        } else {
            ctx.fillStyle = styles.getPropertyValue("--workspace").trim() || "#cfcfcf";
            ctx.fillRect(0, 0, v.w, v.h);
        }
        const doc = this.doc;
        if (!doc) {
            this.updateScrollbars();
            this.renderRulers();
            this.reportImageRect(null, v);
            return;
        }
        this.updateComposite();
        const x = this.origin.x, y = this.origin.y, w = doc.width * this.scale, h = doc.height * this.scale;
        this.reportImageRect({ x, y, w, h }, v);
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
        this.drawRemotePointers(ctx);
        this.updateScrollbars();
    }

    // Punteros de los demás participantes de la sesión colaborativa: un triángulo de su color y, al
    // lado, su nombre con el icono de su herramienta.
    drawRemotePointers(ctx) {
        const list = this.remotePointers;
        if (!list?.length)
            return;
        this.pointerIcons ??= new Map();
        const iconFor = (name) => {
            if (!name)
                return null;
            if (!this.pointerIcons.has(name)) {
                const element = icon(name, 16);
                if (element.tagName === "IMG" && !element.complete)
                    element.addEventListener("load", () => this.update(), { once: true });
                this.pointerIcons.set(name, element);
            }
            const element = this.pointerIcons.get(name);
            return element.tagName === "SPAN" || (element.tagName === "IMG" && !element.complete) ? null : element;
        };
        ctx.save();
        const font = getComputedStyle(document.documentElement).getPropertyValue("--font").trim() || "sans-serif";
        ctx.font = `12px ${font}`;
        ctx.textBaseline = "middle";
        ctx.lineJoin = "round";
        for (const p of list) {
            const v = this.imageToView(p.x, p.y);
            const arrow = new Path2D();
            arrow.moveTo(v.x, v.y);
            arrow.lineTo(v.x, v.y + 16);
            arrow.lineTo(v.x + 4.4, v.y + 12.2);
            arrow.lineTo(v.x + 11.5, v.y + 11.5);
            arrow.closePath();
            ctx.save();
            ctx.translate(1, 1);
            ctx.strokeStyle = "rgba(0,0,0,0.25)";
            ctx.lineWidth = 3;
            ctx.stroke(arrow);
            ctx.restore();
            ctx.fillStyle = p.color;
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 1.5;
            ctx.fill(arrow);
            ctx.stroke(arrow);
            const image = iconFor(p.tool);
            const h = 18, w = 4 + (image ? 19 : 0) + ctx.measureText(p.name).width + 6;
            const x = v.x + 10, y = v.y + 16;
            ctx.beginPath();
            if (ctx.roundRect) ctx.roundRect(x, y, w, h, 4); else ctx.rect(x, y, w, h);
            ctx.fillStyle = p.color;
            ctx.fill();
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 1;
            ctx.stroke();
            let tx = x + 4;
            if (image) {
                ctx.fillStyle = "rgba(255,255,255,0.92)";
                ctx.beginPath();
                if (ctx.roundRect) ctx.roundRect(tx - 1, y, 18, 18, 3); else ctx.rect(tx - 1, y, 18, 18);
                ctx.fill();
                ctx.drawImage(image, tx, y + 1, 16, 16);
                tx += 19;
            }
            const c = p.color.replace("#", "");
            const gray = (parseInt(c.slice(0, 2), 16) * 11 + parseInt(c.slice(2, 4), 16) * 16 + parseInt(c.slice(4, 6), 16) * 5) / 32;
            ctx.fillStyle = gray > 165 ? "#000000" : "#ffffff";
            ctx.fillText(p.name, tx, y + h / 2 + 0.5);
        }
        ctx.restore();
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
                // Con zum, la marca ocupa lo que mide en pantalla el píxel bajo el puntero (a 100 %,
                // una línea): una franja algo transparente, para que se lean los números, con sus bordes.
                if (this.lastMouse) {
                    const px = Math.floor(vertical ? this.lastMouse.y : this.lastMouse.x);
                    const p0 = this.imageToView(px, px), p1 = this.imageToView(px + 1, px + 1);
                    const a = vertical ? p0.y : p0.x, b = vertical ? p1.y : p1.x;
                    const from = Math.min(a, b), size = Math.abs(b - a);
                    ctx.strokeStyle = accent;
                    if (size <= 1.5) {
                        const m = this.imageToView(this.lastMouse.x, this.lastMouse.y);
                        const c = Math.round(vertical ? m.y : m.x) + 0.5;
                        ctx.beginPath();
                        vertical ? (ctx.moveTo(0, c), ctx.lineTo(t, c)) : (ctx.moveTo(c, 0), ctx.lineTo(c, t));
                        ctx.stroke();
                    } else {
                        ctx.save();
                        ctx.globalAlpha = 0.43;
                        ctx.fillStyle = accent;
                        vertical ? ctx.fillRect(0, from, t, size) : ctx.fillRect(from, 0, size, t);
                        ctx.restore();
                        const e0 = Math.round(from) + 0.5, e1 = Math.round(from + size) - 0.5;
                        ctx.beginPath();
                        for (const e of [e0, e1])
                            vertical ? (ctx.moveTo(0, e), ctx.lineTo(t, e)) : (ctx.moveTo(e, 0), ctx.lineTo(e, t));
                        ctx.stroke();
                    }
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
                        // El lavanda translúcido de Paint.NET 5 (sobre blanco, #e4eaf7).
                        img.data[i] = 30; img.data[i + 1] = 80; img.data[i + 2] = 188; img.data[i + 3] = (m * 31) / 255;
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

    // Casi blanco y, encima, gris a trazos que se mueven (las hormigas de Paint.NET 5), de un píxel
    // de la pantalla.
    strokeAnts(ctx, path) {
        ctx.save();
        ctx.lineWidth = 1 / this.dpr;
        ctx.strokeStyle = "#f3f4f8";
        ctx.stroke(path);
        ctx.setLineDash([4 / this.dpr, 4 / this.dpr]);
        ctx.lineDashOffset = -this.antsOffset / this.dpr;
        ctx.strokeStyle = "#7f8289";
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
            ctx.fillStyle = "rgba(30,80,188,0.12)";
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

    // --- Dedos ------------------------------------------------------------------------------
    // En las pantallas táctiles, como en las aplicaciones de dibujo: un dedo usa la herramienta y
    // dos mueven la imagen (arrastrando) y la acercan o alejan (pellizcando), con inercia al
    // soltar. El primer dedo espera un instante antes de pintar: si llega el segundo, no deja
    // marca; si llega con el trazo recién empezado, el trazo se deshace.

    // true: el evento táctil es del gesto y la herramienta no lo ve.
    touchDown(e) {
        this.touches.set(e.pointerId, { x: e.clientX, y: e.clientY });
        this.stopInertia();
        if (this.touches.size === 1) {
            this.touchBlocked = false;
            // La Mano ya mueve la imagen con un dedo (y ahora con inercia).
            if (this.tool?.id === "pan")
                return false;
            this.touchPending = { down: e, moves: [], id: e.pointerId, x: e.clientX, y: e.clientY,
                                  timer: setTimeout(() => this.flushTouch(), kTouchDelay) };
            return true;
        }
        if (this.touchPending) {
            clearTimeout(this.touchPending.timer);
            this.touchPending = null;
        } else if (this.dragEvent?.pointerType === "touch") {
            this.abortTouchStroke();
        }
        this.panning = null;
        if (this.touches.size === 2)
            this.startGesture();
        return true;
    }

    touchMove(e) {
        if (!this.touches.has(e.pointerId))
            return false;
        this.touches.set(e.pointerId, { x: e.clientX, y: e.clientY });
        if (this.gesture) {
            this.updateGesture();
            return true;
        }
        if (this.touchBlocked)
            return true;
        const p = this.touchPending;
        if (p && p.id === e.pointerId) {
            if (Math.hypot(e.clientX - p.x, e.clientY - p.y) < kTouchSlop) {
                p.moves.push(e);
                return true;
            }
            // Se movió lo bastante: era un trazo; sigue como con el ratón.
            this.flushTouch();
        }
        return false;
    }

    touchUp(e) {
        if (!this.touches.delete(e.pointerId))
            return false;
        if (this.gesture) {
            if (this.touches.size < 2)
                this.endGesture();
            this.touchBlocked = this.touches.size > 0;
            return true;
        }
        if (this.touchBlocked) {
            this.touchBlocked = this.touches.size > 0;
            return true;
        }
        if (this.touchPending?.id === e.pointerId) {
            // El navegador se quedó el toque: no llegó a empezar nada.
            if (e.type === "pointercancel") {
                clearTimeout(this.touchPending.timer);
                this.touchPending = null;
                return true;
            }
            // Un toque corto (sin moverse): la herramienta recibe el toque entero.
            this.flushTouch();
        }
        return false;
    }

    // El primer dedo, que esperaba, empieza de verdad con lo que se movió mientras tanto.
    flushTouch() {
        const p = this.touchPending;
        if (!p)
            return;
        clearTimeout(p.timer);
        this.touchPending = null;
        this.pointerDown(p.down);
        this.strokeStart = performance.now();
        for (const m of p.moves)
            this.pointerMove(m);
    }

    // El segundo dedo llegó con el trazo ya empezado: se termina y, si acababa de empezar, se
    // deshace sin dejar nada que rehacer (era el principio del gesto, no un trazo).
    abortTouchStroke() {
        const doc = this.doc, te = this.dragEvent;
        this.dragEvent = null;
        this.stopAutoScroll();
        if (!doc || !te)
            return;
        const recent = performance.now() - (this.strokeStart || 0) < kTouchUndoWindow;
        const before = doc.history.index;
        this.tool?.onPointerUp?.(te);
        if (!recent)
            return;
        if (doc.history.index === before + 1) {
            doc.undo();
            doc.history.steps.splice(doc.history.index + 1);
            doc.emit("history");
        } else if (this.tool?.isBusy?.()) {
            this.tool.cancel?.();
        }
        this.update();
    }

    touchPoints() {
        const rect = this.canvas.getBoundingClientRect();
        const [a, b] = [...this.touches.values()];
        return {
            mid: { x: (a.x + b.x) / 2 - rect.left, y: (a.y + b.y) / 2 - rect.top },
            dist: Math.max(1, Math.hypot(a.x - b.x, a.y - b.y)),
        };
    }

    startGesture() {
        if (!this.doc)
            return;
        const { mid, dist } = this.touchPoints();
        this.gesture = { dist, zoom: this.zoom, image: this.viewToImage(mid.x, mid.y), samples: [] };
        this.fitOnResize = false;
        this.touchBlocked = true;
        this.stopAutoScroll();
        this.updateCursor();
    }

    // Pellizcar acerca o aleja con el punto de la imagen que estaba entre los dedos siempre entre
    // ellos (también al arrastrar a la vez); cerca del 100 % se queda en él.
    updateGesture() {
        const g = this.gesture;
        if (!g || this.touches.size < 2 || !this.doc)
            return;
        const { mid, dist } = this.touchPoints();
        let zoom = Math.min(64, Math.max(0.01, (g.zoom * dist) / g.dist));
        if (Math.abs(zoom - 1) < 0.04)
            zoom = 1;
        const changed = zoom !== this.zoom;
        this.zoom = zoom;
        this.origin = { x: mid.x - g.image.x * this.scale, y: mid.y - g.image.y * this.scale };
        this.clampOrigin();
        this.update();
        if (changed)
            this.emitZoom();
        const now = performance.now();
        g.samples.push({ t: now, x: mid.x, y: mid.y });
        while (g.samples.length > 2 && now - g.samples[0].t > 100)
            g.samples.shift();
    }

    endGesture() {
        const g = this.gesture;
        this.gesture = null;
        if (!g)
            return;
        this.flingFrom(g.samples);
    }

    // Inercia: la imagen sigue con la velocidad de los últimos 100 ms y se frena poco a poco.
    flingFrom(samples) {
        if (!samples || samples.length < 2)
            return;
        const a = samples[0], b = samples[samples.length - 1], dt = b.t - a.t;
        if (dt <= 0 || performance.now() - b.t > 80)
            return;
        let vx = (b.x - a.x) / dt, vy = (b.y - a.y) / dt;
        if (Math.hypot(vx, vy) < 0.25)
            return;
        let last = performance.now();
        const step = (now) => {
            const ms = Math.min(48, now - last);
            last = now;
            const before = { ...this.origin };
            this.scrollBy(-vx * ms, -vy * ms);
            const decay = Math.pow(0.996, ms);
            vx *= decay;
            vy *= decay;
            // Se para al frenar o al llegar al límite.
            if (Math.hypot(vx, vy) < 0.03 || (before.x === this.origin.x && before.y === this.origin.y)) {
                this.inertia = 0;
                return;
            }
            this.inertia = requestAnimationFrame(step);
        };
        this.inertia = requestAnimationFrame(step);
    }

    stopInertia() {
        if (this.inertia) {
            cancelAnimationFrame(this.inertia);
            this.inertia = 0;
        }
    }

    installEvents() {
        const c = this.canvas;
        this.touches = new Map();
        this.touchPending = null;
        this.touchBlocked = false;
        this.gesture = null;
        this.inertia = 0;
        c.addEventListener("contextmenu", (e) => e.preventDefault());
        c.addEventListener("pointerdown", (e) => {
            // Con el dedo y una herramienta que escribe (Texto), el foco se queda en su campo: si el
            // lienzo se lo quitaba, el teclado en pantalla se cerraba al tocar (y se volvía a abrir
            // al dárselo otra vez, al mover el texto), y la «A» no sabía si estaba abierto.
            if (!(e.pointerType === "touch" && this.tool?.wantsKeyboard?.()))
                c.focus({ preventScroll: true });
            if (!this.doc)
                return;
            c.setPointerCapture(e.pointerId);
            // Sin los eventos de ratón que el navegador emula tras soltar el dedo: su mousedown
            // devolvía el foco al lienzo y el Texto se quedaba sin teclado (las letras cambiaban de
            // herramienta).
            if (e.pointerType === "touch")
                e.preventDefault();
            if (e.pointerType === "touch" && this.touchDown(e))
                return;
            this.stopInertia();
            this.pointerDown(e);
        });
        c.addEventListener("pointermove", (e) => {
            if (!this.doc)
                return;
            if (e.pointerType === "touch" && this.touchMove(e))
                return;
            this.pointerMove(e);
        });
        const up = (e) => {
            if (e.pointerType === "touch" && this.touchUp(e))
                return;
            this.pointerUp(e);
        };
        c.addEventListener("pointerup", up);
        c.addEventListener("pointercancel", up);
        c.addEventListener("pointerleave", (e) => {
            if (e.pointerType === "touch" && (this.gesture || this.touchBlocked || this.touchPending))
                return;
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
            this.stopInertia();
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

    pointerDown(e) {
        const te = this.toolEvent(e);
        if (e.button === 1 || (this.spaceDown && e.button === 0) || (e.button === 0 && this.tool?.id === "pan")) {
            this.panning = { x: e.clientX, y: e.clientY, samples: [] };
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
    }

    refreshToolCursor() {
        if (!this.doc || !this.tool || !this.mouse || this.dragEvent || this.panning)
            return;
        this.setCursor(this.tool.cursor?.() ?? "default");
        this.tool.onPointerMove?.({ ...this.mouse, buttons: 0 });
    }

    pointerMove(e) {
        const te = this.toolEvent(e);
        this.mouse = te;
        this.lastMouse = { x: te.x, y: te.y };
        this.placeMini();
        this.checkOutlineEdge(e);
        this.dispatchEvent(new CustomEvent("cursor", { detail: te }));
        if (this.panning) {
            this.scrollBy(this.panning.x - e.clientX, this.panning.y - e.clientY);
            this.panning.x = e.clientX;
            this.panning.y = e.clientY;
            // Con el dedo, la Mano deja la imagen con inercia al soltar.
            if (e.pointerType === "touch") {
                const now = performance.now(), samples = this.panning.samples;
                samples.push({ t: now, x: e.clientX, y: e.clientY });
                while (samples.length > 2 && now - samples[0].t > 100)
                    samples.shift();
            }
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
    }

    pointerUp(e) {
        this.stopAutoScroll();
        if (!this.doc)
            return;
        if (e.button === 0 || e.button === 2 || e.type === "pointercancel")
            this.dragEvent = null;
        if (this.panning) {
            const samples = this.panning.samples;
            this.panning = null;
            this.updateCursor();
            if (e.pointerType === "touch" && e.type === "pointerup")
                this.flingFrom(samples);
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
    }
}

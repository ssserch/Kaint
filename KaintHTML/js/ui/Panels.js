// Ventanas flotantes (src/app/Workspace.cpp y panels/): Herramientas, Historial, Capas y Colores.
// Se anclan a una esquina del área de trabajo, se arrastran por la cabecera, cambian de tamaño
// por los bordes y se vuelven translúcidas sin el puntero encima.
import { AppState, defaultPalette, parsePalette, rainbowPalette, samePalette, serializePalette, smallPalette } from "../AppState.js";
import { hsvToRgb, hex, parseHex, rgbToHsv, thumbnail } from "../core/Raster.js";
import { kRuler } from "./Canvas.js";
import { el, promptText } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { popupMenu } from "./Menu.js";
import { kIndicator, outCubic, run, slideRows } from "./Animations.js";
import { remember, Settings } from "../Settings.js";
import { plain, tr, trc } from "../i18n.js";

export class FloatingPanel {
    constructor(workspace, id, title, content, { corner = "tl", offset = { x: 8, y: 8 }, size = null, onClose = null } = {}) {
        this.workspace = workspace;
        this.id = id;
        this.defaults = { corner, offset, size };
        this.onClose = onClose;
        // Contenido que decide su tamaño (PanelContent de Kaint): las columnas de Herramientas o
        // la escala de Colores. Se guarda eso y la ventana mide lo que su contenido.
        this.fit = content.fit || null;
        this.element = el("div", { className: "floating-panel", dataset: { panel: id } });
        const head = el("div", { className: "title" }, [el("span", { textContent: title })]);
        const close = el("button", { className: "close", title: tr("Cerrar"), style: { border: "none", padding: 0 } });
        close.addEventListener("click", () => {
            this.setVisible(false);
            this.onClose?.();
        });
        head.appendChild(close);
        this.element.appendChild(head);
        this.content = el("div", { className: "content" }, [content]);
        this.element.appendChild(this.content);
        for (const edge of ["n", "s", "e", "w", "ne", "nw", "se", "sw"])
            this.element.appendChild(el("div", { className: `resize ${edge}`, dataset: { edge } }));
        workspace.appendChild(this.element);
        const saved = remember(`panel.${id}`) || {};
        this.corner = saved.corner || corner;
        this.offset = saved.offset || offset;
        if (this.fit) {
            this.fit.apply(saved.layout ?? this.fit.initial);
        } else if (saved.size || size) {
            const s = saved.size || size;
            this.element.style.width = `${s.w}px`;
            // Sin alto, la ventana mide lo que su contenido (Colores, como en Kaint).
            this.element.style.height = s.h ? `${s.h}px` : "";
        }
        this.visible = saved.visible ?? true;
        this.element.style.display = this.visible ? "" : "none";
        this.installDrag(head);
        this.installResize();
        this.reposition();
    }

    save() {
        remember(`panel.${this.id}`, {
            corner: this.corner, offset: this.offset, visible: this.visible,
            size: this.element.style.width ? { w: this.element.offsetWidth, h: this.element.style.height ? this.element.offsetHeight : 0 } : null,
            layout: this.fit ? this.fit.state() : undefined,
        });
    }

    // Zona para las ventanas: el área de trabajo sin las barras de desplazamiento ni, como en
    // paint.js, las reglas.
    area() {
        const r = this.workspace.classList.contains("rulers") ? kRuler : 0;
        return { x: r, y: r, w: this.workspace.clientWidth - 14 - r, h: this.workspace.clientHeight - 14 - r };
    }

    reposition() {
        const a = this.area(), W = a.w, H = a.h;
        const w = this.element.offsetWidth, h = this.element.offsetHeight;
        let x = this.corner.includes("l") ? this.offset.x : W - w - this.offset.x;
        let y = this.corner.includes("t") ? this.offset.y : H - h - this.offset.y;
        x = Math.max(0, Math.min(W - w, x));
        y = Math.max(0, Math.min(H - h, y));
        this.element.style.left = `${x + a.x}px`;
        this.element.style.top = `${y + a.y}px`;
    }

    // Esquina más cercana y distancia a ella (así se quedan en su sitio al cambiar el tamaño).
    anchorFromGeometry() {
        const a = this.area(), W = a.w, H = a.h;
        const x = this.element.offsetLeft - a.x, y = this.element.offsetTop - a.y, w = this.element.offsetWidth, h = this.element.offsetHeight;
        const left = x + w / 2 < W / 2, top = y + h / 2 < H / 2;
        this.corner = (top ? "t" : "b") + (left ? "l" : "r");
        this.offset = { x: left ? x : W - x - w, y: top ? y : H - y - h };
        this.save();
    }

    installDrag(head) {
        let start = null;
        head.addEventListener("pointerdown", (e) => {
            if (e.target.closest("button"))
                return;
            start = { x: e.clientX, y: e.clientY, left: this.element.offsetLeft, top: this.element.offsetTop };
            head.setPointerCapture(e.pointerId);
            this.element.classList.add("dragging");
            this.raise();
        });
        head.addEventListener("pointermove", (e) => {
            if (!start)
                return;
            const a = this.area();
            this.element.style.left = `${Math.max(a.x, Math.min(a.x + a.w - this.element.offsetWidth, start.left + e.clientX - start.x))}px`;
            this.element.style.top = `${Math.max(a.y, Math.min(a.y + a.h - this.element.offsetHeight, start.top + e.clientY - start.y))}px`;
        });
        head.addEventListener("pointerup", () => {
            if (!start)
                return;
            start = null;
            this.element.classList.remove("dragging");
            this.anchorFromGeometry();
        });
        this.element.addEventListener("pointerdown", () => this.raise());
    }

    installResize() {
        let start = null;
        for (const grip of this.element.querySelectorAll(".resize")) {
            grip.addEventListener("pointerdown", (e) => {
                e.stopPropagation();
                grip.setPointerCapture(e.pointerId);
                start = { x: e.clientX, y: e.clientY, l: this.element.offsetLeft, t: this.element.offsetTop,
                          w: this.element.offsetWidth, h: this.element.offsetHeight, edge: grip.dataset.edge };
                this.element.classList.add("dragging");
            });
            grip.addEventListener("pointermove", (e) => {
                if (!start)
                    return;
                const dx = e.clientX - start.x, dy = e.clientY - start.y;
                let { l, t, w, h } = start;
                if (start.edge.includes("e")) w += dx;
                if (start.edge.includes("s")) h += dy;
                if (start.edge.includes("w")) { w -= dx; l += dx; }
                if (start.edge.includes("n")) { h -= dy; t += dy; }
                if (this.fit) {
                    // Como en Kaint: el contenido elige su tamaño (columnas enteras, la misma
                    // proporción) y el borde contrario al que se arrastra se queda quieto.
                    const content = this.content.firstElementChild;
                    const chromeW = this.element.offsetWidth - content.offsetWidth, chromeH = this.element.offsetHeight - content.offsetHeight;
                    const horizontal = /[ew]/.test(start.edge), vertical = /[ns]/.test(start.edge);
                    this.fit.apply(this.fit.fromSize(w - chromeW, h - chromeH, horizontal, vertical));
                    const a = this.area();
                    const fw = this.element.offsetWidth, fh = this.element.offsetHeight;
                    l = start.edge.includes("w") ? start.l + start.w - fw : start.l;
                    t = start.edge.includes("n") ? start.t + start.h - fh : start.t;
                    this.element.style.left = `${Math.max(a.x, Math.min(a.x + a.w - fw, l))}px`;
                    this.element.style.top = `${Math.max(a.y, Math.min(a.y + a.h - fh, t))}px`;
                    return;
                }
                w = Math.max(80, w);
                h = Math.max(60, h);
                Object.assign(this.element.style, { left: `${l}px`, top: `${t}px`, width: `${w}px`, height: `${h}px` });
                this.onResize?.();
            });
            grip.addEventListener("pointerup", () => {
                if (!start)
                    return;
                start = null;
                this.element.classList.remove("dragging");
                this.anchorFromGeometry();
            });
        }
    }

    raise() {
        for (const p of this.workspace.querySelectorAll(".floating-panel"))
            p.style.zIndex = p === this.element ? "20" : "10";
    }

    setVisible(on, animated = false) {
        this.visible = on;
        this.element.style.display = on ? "" : "none";
        if (on) {
            this.raise();
            this.reposition();
            if (animated) {
                this.element.classList.remove("appearing");
                void this.element.offsetWidth;
                this.element.classList.add("appearing");
            }
        }
        this.save();
    }

    reset() {
        this.corner = this.defaults.corner;
        this.offset = { ...this.defaults.offset };
        if (this.fit) {
            this.fit.apply(this.fit.initial);
        } else {
            this.element.style.width = this.defaults.size ? `${this.defaults.size.w}px` : "";
            this.element.style.height = this.defaults.size?.h ? `${this.defaults.size.h}px` : "";
        }
        this.setVisible(true);
    }
}

// --- Herramientas -------------------------------------------------------------------------

// Rejilla de botones como la de Paint.NET; al cambiar el tamaño de la ventana se queda con las
// columnas enteras que quepan (ToolsPanel::fitSize de Kaint). Empieza con dos.
export function toolsPanel(app) {
    const grid = el("div", { className: "tools-grid" });
    const buttons = new Map();
    for (const tool of app.tools.tools) {
        const b = el("button", { className: "tbutton", title: `${tool.name} (${tool.letter})` }, [icon(tool.icon, 22)]);
        b.addEventListener("click", () => app.tools.select(tool.id));
        buttons.set(tool.id, b);
        grid.appendChild(b);
    }
    const sync = () => buttons.forEach((b, id) => b.classList.toggle("checked", app.tools.current?.id === id));
    app.tools.addEventListener("changed", sync);
    sync();
    // Lado de los botones (30 px; 24 con el aspecto de Paint.NET) y su separación.
    const count = buttons.size, gap = 1, cell = () => grid.firstElementChild?.offsetWidth || 30;
    let columns = 2;
    grid.fit = {
        initial: 2,
        state: () => columns,
        apply(c) {
            columns = Math.min(count, Math.max(1, Math.round(Number(c) || 2)));
            grid.style.setProperty("--columns", columns);
            // Como en Paint.NET, el último botón (Formas) ocupa lo que quede de su fila.
            grid.lastElementChild.style.gridColumn = `span ${columns - ((count - 1) % columns)}`;
        },
        fromSize(w, h, horizontal, vertical) {
            if (vertical && !horizontal) {
                const rows = Math.min(count, Math.max(1, Math.floor((h + gap) / (cell() + gap))));
                return Math.ceil(count / rows);
            }
            return Math.floor((w + gap) / (cell() + gap));
        },
    };
    return grid;
}

// --- Historial ----------------------------------------------------------------------------

export function historyPanel(app) {
    const list = el("div", { className: "list history-list" });
    const refresh = () => {
        list.replaceChildren();
        const doc = app.doc;
        if (!doc)
            return;
        doc.history.steps.forEach((step, i) => {
            const row = el("div", { className: "row" + (i === doc.history.index ? " selected" : "") + (i > doc.history.index ? " undone" : "") },
                           [icon(step.icon || "document-new", 16), el("span", { textContent: step.name })]);
            row.addEventListener("click", () => app.jumpHistory(i));
            list.appendChild(row);
        });
        // Solo se desplaza la lista (scrollIntoView movería también el área de trabajo).
        const row = list.querySelector(".selected");
        if (row) {
            if (row.offsetTop < list.scrollTop)
                list.scrollTop = row.offsetTop;
            else if (row.offsetTop + row.offsetHeight > list.scrollTop + list.clientHeight)
                list.scrollTop = row.offsetTop + row.offsetHeight - list.clientHeight;
        }
    };
    app.addEventListener("history", refresh);
    app.addEventListener("document", refresh);
    const buttons = el("div", { className: "panel-buttons" });
    const jump = (text, iconName, fn, tip) => {
        // Ir al principio y al final no están con el aspecto de Paint.NET (solo deshacer y rehacer).
        const b = el("button", { className: iconName ? "tbutton" : "tbutton history-jump", title: tip },
                     [iconName ? icon(iconName, 16) : el("span", { textContent: text })]);
        b.addEventListener("click", fn);
        buttons.appendChild(b);
    };
    jump("⏮", null, () => app.jumpHistory(0), tr("Volver al principio"));
    jump("", "edit-undo", () => app.undo(), plain(tr("&Deshacer")));
    jump("", "edit-redo", () => app.redo(), plain(tr("&Rehacer")));
    jump("⏭", null, () => app.jumpHistory(Infinity), tr("Avanzar hasta el final"));
    return el("div", { style: { display: "flex", flexDirection: "column", flex: "1", minHeight: "0" } }, [list, buttons]);
}

// --- Capas --------------------------------------------------------------------------------

export function layersPanel(app, actionIds) {
    const list = el("div", { className: "list layer-list" });
    const dropLine = el("div", { className: "drop-line", style: { display: "none" } });
    let lastDoc = null;
    const thumbs = new Map();
    const refresh = () => {
        const doc = app.doc;
        // Posiciones anteriores (para deslizar las filas que cambian de sitio).
        const before = new Map();
        if (doc && doc === lastDoc)
            for (const row of list.querySelectorAll(".row"))
                before.set(row.dataset.key, row.offsetTop);
        lastDoc = doc;
        list.replaceChildren(dropLine);
        if (!doc)
            return;
        for (let i = doc.layers.length - 1; i >= 0; --i) {
            const layer = doc.layers[i];
            const thumbBox = el("div", { className: "thumb" });
            const check = el("input", { type: "checkbox", checked: layer.visible, title: tr("Visible") });
            check.addEventListener("pointerdown", (e) => e.stopPropagation());
            check.addEventListener("change", (e) => app.toggleLayerVisibility(i, e.ctrlKey));
            const row = el("div", { className: "row" + (i === doc.active ? " selected" : ""), dataset: { key: String(layer.id) },
                                    title: tr("%1\nOpacidad: %2 · Fusión: %3", layer.name, layer.opacity, layer.blend) },
                           [thumbBox, el("span", { className: "name", textContent: layer.name }), check]);
            row.layer = layer;
            row.index = i;
            row.addEventListener("dblclick", () => app.layerProperties());
            list.appendChild(row);
            thumbs.set(layer, thumbBox);
            updateThumb(layer);
        }
        slideRows(list.querySelectorAll(".row"), before);
    };
    const updateThumb = (layer) => {
        const box = thumbs.get(layer);
        if (box)
            box.replaceChildren(thumbnail(layer.canvas, 40, 40));
    };
    let pending = new Set(), timer = 0;
    app.addEventListener("pixels", (e) => {
        const layer = e.detail.layer;
        if (layer) pending.add(layer);
        else app.doc?.layers.forEach((l) => pending.add(l));
        clearTimeout(timer);
        timer = setTimeout(() => {
            pending.forEach(updateThumb);
            pending = new Set();
        }, 250);
    });
    app.addEventListener("layers", () => {
        endDrag();
        refresh();
    });
    app.addEventListener("document", () => {
        endDrag();
        refresh();
    });
    // Clic: capa activa · arrastrar: mover la capa. Como en Kaint, la fila sigue al ratón y las
    // demás le van dejando sitio; con la opción de Configuración, se queda en su sitio y una línea
    // marca dónde quedará.
    let drag = null;
    const rows = () => [...list.querySelectorAll(".row")];
    // Fila (de arriba abajo) bajo la posición y de la ventana; fuera de la lista, la primera o la última.
    const slotAt = (y) => {
        const all = rows();
        const i = all.findIndex((r) => {
            const b = r.getBoundingClientRect(), shift = Number(r.dataset.shift || 0);
            return y >= b.top - shift && y < b.bottom - shift;
        });
        if (i >= 0)
            return i;
        return all.length && y < all[0].getBoundingClientRect().top - Number(all[0].dataset.shift || 0) ? 0 : all.length - 1;
    };
    const setShift = (row, px) => {
        row.dataset.shift = px;
        row.style.transform = px ? `translateY(${px}px)` : "";
    };
    const endDrag = () => {
        if (!drag)
            return;
        clearInterval(drag.scroll);
        for (const r of rows()) {
            r.classList.remove("dragging", "live", "making-room");
            setShift(r, 0);
        }
        dropLine.style.display = "none";
        drag = null;
    };
    list.addEventListener("pointerdown", (e) => {
        const row = e.target.closest(".row");
        if (!row || e.button !== 0)
            return;
        app.doc.setActiveIndex(row.index);
        const all = rows();
        drag = { row, y: e.clientY, from: all.indexOf(row), slot: all.indexOf(row), moving: false, live: !Settings.get("layerDropLine") };
        list.setPointerCapture(e.pointerId);
    });
    const dragTo = (y) => {
        const all = rows(), h = drag.row.offsetHeight;
        if (drag.live) {
            // La fila sigue al ratón (sin salirse de la lista) y las demás se apartan.
            const top = drag.row.offsetTop, scroll = list.scrollTop;
            const dy = Math.max(-top + scroll - h / 2, Math.min(list.clientHeight + scroll - top - h / 2, y - drag.y));
            setShift(drag.row, dy);
            const slot = Math.min(all.length - 1, Math.max(0, drag.from + Math.round(dy / h)));
            if (slot === drag.slot)
                return;
            drag.slot = slot;
            all.forEach((r, i) => {
                if (r === drag.row)
                    return;
                setShift(r, drag.from < slot && i > drag.from && i <= slot ? -h : drag.from > slot && i >= slot && i < drag.from ? h : 0);
            });
            return;
        }
        const slot = slotAt(y);
        drag.slot = slot;
        if (slot === drag.from) {
            dropLine.style.display = "none";
            return;
        }
        const target = all[slot];
        dropLine.style.display = "block";
        dropLine.style.top = `${target.offsetTop + (slot > drag.from ? target.offsetHeight - 2 : -1)}px`;
    };
    list.addEventListener("pointermove", (e) => {
        if (!drag)
            return;
        if (!drag.moving && Math.abs(e.clientY - drag.y) > 4) {
            drag.moving = true;
            drag.row.classList.add(drag.live ? "live" : "dragging");
            if (drag.live)
                rows().forEach((r) => r !== drag.row && r.classList.add("making-room"));
        }
        if (!drag.moving)
            return;
        drag.lastY = e.clientY;
        dragTo(e.clientY);
        // Por encima o por debajo de la lista, se desplaza mientras se arrastra.
        const b = list.getBoundingClientRect(), step = e.clientY < b.top ? -10 : e.clientY > b.bottom ? 10 : 0;
        clearInterval(drag.scroll);
        if (step)
            drag.scroll = setInterval(() => {
                const before = list.scrollTop;
                list.scrollTop += step;
                if (list.scrollTop !== before && drag) {
                    if (drag.live)
                        drag.y -= list.scrollTop - before;
                    dragTo(drag.lastY);
                }
            }, 50);
    });
    list.addEventListener("pointerup", () => {
        if (!drag)
            return;
        const { row, from, slot, moving, live } = drag;
        endDrag();
        if (!moving || slot === from)
            return;
        // Las filas ya se ven en su sitio nuevo: al rehacer la lista no se deslizan.
        if (live)
            lastDoc = null;
        const layers = app.doc.layers.length;
        app.moveLayerTo(row.index, layers - 1 - slot);
    });
    const buttons = el("div", { className: "panel-buttons" });
    for (const id of actionIds)
        buttons.appendChild(app.actionButton(id));
    return el("div", { style: { display: "flex", flexDirection: "column", flex: "1", minHeight: "0" } }, [list, buttons]);
}

// --- Colores ------------------------------------------------------------------------------

// Damero de Kaint (paintCheckerboard): grises 191 y 143.
function paintChecker(p, x, y, w, h, cell) {
    p.save();
    p.beginPath();
    p.rect(x, y, w, h);
    p.clip();
    p.fillStyle = "#bfbfbf";
    p.fillRect(x, y, w, h);
    p.fillStyle = "#8f8f8f";
    for (let yy = 0; yy * cell < h; ++yy)
        for (let xx = yy % 2; xx * cell < w; xx += 2)
            p.fillRect(x + xx * cell, y + yy * cell, cell, cell);
    p.restore();
}

const cssColor = (c) => `rgba(${c.r},${c.g},${c.b},${(c.a ?? 255) / 255})`;
const themeColor = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

// Ranuras principal y secundaria como las de Paint.NET: la activa lleva marco grueso y un pico
// abajo; al cambiar, las dos se animan a la vez (una adelgaza y pierde el pico, la otra crece).
class ColorSwatches {
    constructor() {
        this.canvas = el("canvas", { className: "swatches", width: 64, height: 64, style: { width: "64px", height: "64px" },
                                     title: tr("Color principal (arriba) y secundario (abajo). La muesca marca la ranura activa (C).\nDoble clic para editarlos. X intercambia.") });
        this.level = AppState.secondaryActive ? 0 : 1;
        this.anim = null;
        this.canvas.addEventListener("pointerdown", (e) => {
            const r = this.canvas.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
            if (x >= 40 && x <= 54 && y >= 3 && y <= 16) AppState.swapColors();
            else if (x <= 17 && y >= 39) AppState.resetColors();
            else if (x >= 1 && x <= 36 && y >= 1 && y <= 36) AppState.setSecondaryActive(false);
            else if (x >= 19 && x <= 54 && y >= 19 && y <= 54) AppState.setSecondaryActive(true);
        });
        this.swap = new Image();
        this.swap.src = "assets/icons/swap_icon.png";
        this.swap.onload = () => this.paint();
        AppState.addEventListener("colors", () => this.sync());
        this.sync();
    }

    sync() {
        const target = AppState.secondaryActive ? 0 : 1;
        if (target !== this.target) {
            this.target = target;
            this.anim?.stop();
            this.anim = run(this.level, target, kIndicator, outCubic, (v) => {
                this.level = v;
                this.paint();
            });
        }
        this.paint();
    }

    paint() {
        const dpr = window.devicePixelRatio || 1, c = this.canvas;
        c.width = c.height = Math.round(64 * dpr);
        const p = c.getContext("2d");
        p.setTransform(dpr, 0, 0, dpr, 0, 0);
        p.clearRect(0, 0, 64, 64);
        const swatch = (x, y, color, level, framed) => {
            const s = 35, ring = framed ? 1 + level : 2 * level, line = framed ? 1 : level;
            p.fillStyle = "#000";
            p.fillRect(x, y, s, s);
            if (ring > 0) {
                p.fillStyle = "#fff";
                p.fillRect(x + 1, y + 1, s - 2, s - 2);
            }
            const ix = x + 1 + ring, iy = y + 1 + ring, iw = s - 2 - 2 * ring;
            const cx = ix + iw / 2, bottom = iy + iw;
            // Recorta el pico (que sale un poco por debajo del marco) como subtracted() en Kaint:
            // solo dentro del rectángulo, sin pintar nada fuera de él.
            const cutNotch = (half, depth) => {
                if (level <= 0)
                    return;
                p.beginPath();
                p.rect(0, 0, 64, 64);
                p.moveTo(cx - half, bottom + 1);
                p.lineTo(cx, bottom + 1 - depth * level);
                p.lineTo(cx + half, bottom + 1);
                p.closePath();
                p.clip("evenodd");
            };
            // Marco negro de dentro con el pico de la ranura activa recortado abajo.
            p.save();
            cutNotch(5, 7);
            p.fillStyle = "#000";
            p.fillRect(ix, iy, iw, iw);
            p.restore();
            p.save();
            p.beginPath();
            p.rect(ix + line, iy + line, iw - 2 * line, iw - 2 * line);
            p.clip();
            cutNotch(6.5, 8.5);
            paintChecker(p, ix, iy, iw, iw, 5);
            p.fillStyle = cssColor(color);
            p.fillRect(ix, iy, iw, iw);
            p.restore();
        };
        swatch(19, 19, AppState.secondary, 1 - this.level, false);
        swatch(1, 1, AppState.primary, this.level, true);
        if (this.swap.complete && this.swap.naturalWidth) {
            p.imageSmoothingQuality = "high";
            p.drawImage(this.swap, 40, 3, 14, 13);
        }
        // Mini muestras negro y blanco para restablecer, con borde gris.
        p.strokeStyle = "#787878";
        p.fillStyle = "#000";
        p.fillRect(1.5, 39.5, 10, 10);
        p.strokeRect(1.5, 39.5, 10, 10);
        p.fillStyle = "#fff";
        p.fillRect(6.5, 44.5, 10, 10);
        p.strokeRect(6.5, 44.5, 10, 10);
    }
}

// Deslizador de un canal (ColorSlider de Kaint): la barra con el degradado del canal y un
// triángulo a cada lado que marca el valor. Se arrastra o se mueve con la rueda.
export class ChannelSlider {
    constructor(channel, vertical, onChange) {
        this.channel = channel;
        this.vertical = vertical;
        this.onChange = onChange;
        this.color = { r: 0, g: 0, b: 0, a: 255 };
        this.hsv = { h: 0, s: 0, v: 0 };
        this.canvas = el("canvas", { className: `channel-slider${vertical ? " vertical" : ""}` });
        const pick = (e) => {
            const r = this.canvas.getBoundingClientRect();
            const t = vertical ? 1 - (e.clientY - r.top - 4) / Math.max(1, r.height - 8)
                               : (e.clientX - r.left - 4) / Math.max(1, r.width - 8);
            this.onChange(Math.round(Math.min(1, Math.max(0, t)) * this.maximum));
        };
        this.canvas.addEventListener("pointerdown", (e) => {
            if (e.button !== 0)
                return;
            this.canvas.setPointerCapture(e.pointerId);
            pick(e);
        });
        this.canvas.addEventListener("pointermove", (e) => {
            if (this.canvas.hasPointerCapture(e.pointerId))
                pick(e);
        });
        this.canvas.addEventListener("wheel", (e) => {
            e.preventDefault();
            const step = e.deltaY < 0 ? 1 : e.deltaY > 0 ? -1 : 0;
            if (step)
                this.onChange(Math.min(this.maximum, Math.max(0, this.value() + step)));
        }, { passive: false });
        new ResizeObserver(() => this.paint()).observe(this.canvas);
    }

    get maximum() {
        return this.channel === "h" ? 359 : this.channel === "s" || this.channel === "v" ? 100 : 255;
    }

    value() {
        switch (this.channel) {
        case "h": return Math.min(359, Math.round(this.hsv.h));
        case "s": return Math.round(this.hsv.s * 100);
        case "v": return Math.round(this.hsv.v * 100);
        default: return this.color[this.channel];
        }
    }

    set(color, hsv) {
        this.color = color;
        this.hsv = hsv;
        this.paint();
    }

    colorAt(t) {
        const c = this.color, hsv = this.hsv;
        switch (this.channel) {
        case "h": return hsvToRgb(t * 0.999 * 360, 1, 1);
        case "s": return hsvToRgb(hsv.h, t, Math.max(0.25, hsv.v));
        case "v": return hsvToRgb(hsv.h, hsv.s, t);
        case "a": return { ...c, a: Math.round(t * 255) };
        default: return { ...c, [this.channel]: Math.round(t * 255), a: 255 };
        }
    }

    paint() {
        const c = this.canvas, dpr = window.devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
        if (!w || !h)
            return;
        c.width = Math.round(w * dpr);
        c.height = Math.round(h * dpr);
        const p = c.getContext("2d");
        p.setTransform(dpr, 0, 0, dpr, 0, 0);
        const v = this.vertical;
        const bar = v ? { x: 3, y: 4, w: w - 6, h: h - 8 } : { x: 4, y: 3, w: w - 8, h: h - 6 };
        if (this.channel === "a")
            paintChecker(p, bar.x, bar.y, bar.w, bar.h, 4);
        const g = v ? p.createLinearGradient(0, bar.y + bar.h, 0, bar.y) : p.createLinearGradient(bar.x, 0, bar.x + bar.w, 0);
        const stops = this.channel === "h" ? 12 : 2;
        for (let i = 0; i <= stops; ++i)
            g.addColorStop(i / stops, cssColor(this.colorAt(i / stops)));
        p.fillStyle = g;
        p.fillRect(bar.x, bar.y, bar.w, bar.h);
        p.strokeStyle = themeColor("--border");
        p.lineWidth = 1;
        p.strokeRect(bar.x - 0.5, bar.y - 0.5, bar.w + 1, bar.h + 1);
        const t = this.value() / this.maximum;
        p.fillStyle = themeColor("--panel");
        p.strokeStyle = themeColor("--text");
        const triangle = (pts) => {
            p.beginPath();
            pts.forEach(([x, y], i) => (i ? p.lineTo(x, y) : p.moveTo(x, y)));
            p.closePath();
            p.fill();
            p.stroke();
        };
        if (v) {
            const y = bar.y + bar.h - t * bar.h;
            triangle([[0, y - 4], [0, y + 4], [bar.x + 2, y]]);
            triangle([[w, y - 4], [w, y + 4], [bar.x + bar.w - 2, y]]);
        } else {
            const x = bar.x + t * bar.w;
            triangle([[x - 4, h], [x + 4, h], [x, bar.y + bar.h - 2]]);
            triangle([[x - 4, 0], [x + 4, 0], [x, bar.y + 2]]);
        }
    }
}

// Rueda de color (ColorWheel de Kaint): el tono alrededor y la saturación hacia fuera, siempre
// con el brillo máximo; ocupa el cuadrado más grande que cabe en su caja.
export class ColorWheel {
    constructor(onPick) {
        this.onPick = onPick;
        this.hsv = { h: 0, s: 0, v: 0 };
        this.box = el("div", { className: "wheel-box" });
        this.canvas = el("canvas", { className: "wheel",
                                     title: tr("Clic izquierdo: ranura activa · derecho: inactiva\nCtrl: solo tono · Alt: solo saturación · Mayús: radios de 15°") });
        this.box.appendChild(this.canvas);
        this.image = null;
        this.canvas.addEventListener("contextmenu", (e) => e.preventDefault());
        this.canvas.addEventListener("pointerdown", (e) => {
            if (e.button !== 0 && e.button !== 2)
                return;
            this.canvas.setPointerCapture(e.pointerId);
            this.press = { button: e.button, hsv: { ...this.hsv } };
            this.pick(e);
        });
        this.canvas.addEventListener("pointermove", (e) => {
            if (this.press && this.canvas.hasPointerCapture(e.pointerId))
                this.pick(e);
        });
        this.canvas.addEventListener("pointerup", () => (this.press = null));
        new ResizeObserver(() => this.resized()).observe(this.box);
    }

    // Lado de la rueda, centrada en la caja con 3 px de margen.
    rect() {
        const w = this.box.clientWidth, h = this.box.clientHeight, side = Math.max(1, Math.min(w, h) - 6);
        return { x: (w - side) / 2, y: (h - side) / 2, side };
    }

    // Con el aspecto de Paint.NET, el tono va en el sentido de las agujas del reloj (el amarillo
    // abajo), como la rueda de Paint.NET 5.
    get clockwise() {
        return Settings.get("uiStyle") === 1;
    }

    resized() {
        const dpr = window.devicePixelRatio || 1, side = Math.round(this.rect().side * dpr), clockwise = this.clockwise;
        if (side > 0 && (!this.image || this.image.width !== side || this.imageClockwise !== clockwise)) {
            this.imageClockwise = clockwise;
            this.image = el("canvas", { width: side, height: side });
            const ctx = this.image.getContext("2d"), img = ctx.createImageData(side, side), c = side / 2;
            for (let y = 0; y < side; ++y)
                for (let x = 0; x < side; ++x) {
                    const dx = x + 0.5 - c, dy = y + 0.5 - c, d = Math.hypot(dx, dy), alpha = Math.min(1, Math.max(0, c - d));
                    if (alpha <= 0)
                        continue;
                    const col = hsvToRgb(((Math.atan2(clockwise ? dy : -dy, dx) * 180) / Math.PI + 360) % 360, Math.min(1, d / c), 1);
                    const i = (y * side + x) * 4;
                    img.data[i] = col.r; img.data[i + 1] = col.g; img.data[i + 2] = col.b; img.data[i + 3] = alpha * 255;
                }
            ctx.putImageData(img, 0, 0);
        }
        this.paint();
    }

    set(hsv) {
        this.hsv = hsv;
        this.paint();
    }

    paint() {
        const dpr = window.devicePixelRatio || 1, w = this.box.clientWidth, h = this.box.clientHeight;
        if (!w || !h || !this.image)
            return;
        const c = this.canvas;
        c.width = Math.round(w * dpr);
        c.height = Math.round(h * dpr);
        const p = c.getContext("2d");
        p.setTransform(dpr, 0, 0, dpr, 0, 0);
        const r = this.rect();
        p.drawImage(this.image, r.x, r.y, r.side, r.side);
        const radius = r.side / 2, a = (this.hsv.h * Math.PI) / 180, d = this.hsv.s * radius;
        const mx = r.x + radius + Math.cos(a) * d, my = r.y + radius + (this.clockwise ? 1 : -1) * Math.sin(a) * d;
        p.lineWidth = 1.5;
        p.strokeStyle = "#000";
        p.beginPath();
        p.arc(mx, my, 5, 0, Math.PI * 2);
        p.stroke();
        p.strokeStyle = "#fff";
        p.beginPath();
        p.arc(mx, my, 3.5, 0, Math.PI * 2);
        p.stroke();
    }

    pick(e) {
        const b = this.canvas.getBoundingClientRect(), r = this.rect(), radius = r.side / 2;
        const dx = e.clientX - b.left - (r.x + radius), dy = e.clientY - b.top - (r.y + radius);
        let h = ((Math.atan2(this.clockwise ? dy : -dy, dx) * 180) / Math.PI + 360) % 360, s = Math.min(1, Math.hypot(dx, dy) / radius);
        // Ctrl: solo el tono · Alt: solo la saturación · Mayús: pasos de 15°.
        if (e.ctrlKey)
            s = this.press.hsv.s;
        if (e.altKey && !e.ctrlKey)
            h = this.press.hsv.h;
        if (e.shiftKey)
            h = (Math.round(h / 15) * 15) % 360;
        // Con negro elegido, la rueda da el color con todo su brillo.
        const v = this.hsv.v < 0.02 ? 1 : this.hsv.v;
        this.hsv = { h, s, v };
        this.paint();
        this.onPick(this.hsv, this.press.button === 2);
    }
}

// Ventana Colores (ColorsPanel de Kaint). Con «Más» crece a lo ancho como la de Paint.NET: las
// ranuras suben junto al botón y a la derecha van RGB, MSV, la opacidad y el código hexadecimal.
// Al cambiar el tamaño de la ventana todo se escala a la vez (fit).
export function colorsPanel(app) {
    const panel = el("div", { className: "colors-panel" });
    const left = el("div", { className: "left" });
    const extended = el("div", { className: "extended", style: { display: "none" } });
    panel.append(left, extended);
    const active = () => AppState.activeColor();
    const setActive = (c) => AppState.setActiveColor(c);
    const setInactive = (c) => (AppState.secondaryActive ? AppState.setPrimary(c) : AppState.setSecondary(c));
    let hsv = rgbToHsv(active());
    const setHsv = (h) => {
        hsv = h;
        setActive({ ...hsvToRgb(h.h, h.s, h.v), a: active().a });
    };

    // Cabecera: ranura que se edita y más opciones.
    const target = el("select", { title: tr("Ranura de color que se edita (C)") },
                      [el("option", { value: "0", textContent: tr("Principal") }), el("option", { value: "1", textContent: tr("Secundario") })]);
    target.addEventListener("change", () => AppState.setSecondaryActive(target.value === "1"));
    const more = el("button", { className: "pushbutton more-button", textContent: tr("Más »") });
    const topRow = el("div", { className: "top" }, [target, el("span", { style: { flex: 1 } }), more]);
    // Ranuras, rueda y brillo.
    const swatches = new ColorSwatches();
    const swatchColumn = el("div", { className: "swatch-column" }, [swatches.canvas]);
    const wheel = new ColorWheel((h, inactive) => {
        if (inactive) {
            setInactive({ ...hsvToRgb(h.h, h.s, h.v), a: (AppState.secondaryActive ? AppState.primary : AppState.secondary).a });
            return;
        }
        setHsv(h);
    });
    const value = new ChannelSlider("v", true, (v) => setHsv({ ...hsv, v: v / 100 }));
    value.canvas.title = tr("Brillo");
    // La rueda (y encima, con el aspecto de Paint.NET, el botón «Más >>»).
    const wheelColumn = el("div", { className: "wheel-column" }, [wheel.box]);
    const middle = el("div", { className: "middle" }, [swatchColumn, wheelColumn, value.canvas]);
    left.append(topRow, middle);

    // Paleta: clic izquierdo = ranura activa, derecho = inactiva.
    const addColor = el("button", { className: "tbutton", title: tr("Añadir el color activo a la paleta: haz clic después en la muestra que quieras sustituir") });
    const addIcon = el("canvas", { width: 16, height: 16, style: { width: "16px", height: "16px" } });
    addColor.appendChild(addIcon);
    const paletteMenu = el("button", { className: "tbutton palette-menu", title: tr("Paletas") }, [icon("palette", 16), el("span", { className: "arrow", textContent: "▾" })]);
    left.appendChild(el("div", { className: "palette-row" }, [addColor, paletteMenu]));
    const palette = el("div", { className: "palette", title: tr("Clic izquierdo: ranura activa · clic derecho: ranura inactiva") });
    left.appendChild(palette);
    let expanded = false;
    let addMode = false;
    addColor.addEventListener("click", () => {
        addMode = !addMode;
        addColor.classList.toggle("checked", addMode);
        palette.classList.toggle("add-mode", addMode);
    });
    // Como el menú de paletas de Kaint: las guardadas, guardar y abrir, la predeterminada y las de
    // muestra; la que se está usando lleva la marca.
    paletteMenu.addEventListener("click", () => {
        const r = paletteMenu.getBoundingClientRect(), current = AppState.palette;
        const saved = remember("palettes") || {};
        const entry = (text, iconName, colors, run) => ({ text, icon: colors && samePalette(colors, current) ? "dialog-ok-apply" : iconName, run });
        const entries = Object.keys(saved).sort((a, b) => a.localeCompare(b))
            .map((name) => entry(name, "palette", saved[name], () => AppState.setPalette(saved[name])));
        if (entries.length)
            entries.push("-");
        entries.push(entry(tr("Guardar la paleta actual como…"), "document-save-as", null, async () => {
            const name = (await promptText(tr("Guardar la paleta"), tr("Nombre de la paleta:"), ""))?.trim();
            if (name)
                remember("palettes", { ...(remember("palettes") || {}), [name]: AppState.palette });
        }));
        entries.push(entry(tr("Abrir paleta desde archivo…"), "document-open", null, () => {
            const input = el("input", { type: "file", accept: ".txt,text/plain" });
            input.addEventListener("change", async () => {
                const f = input.files?.[0];
                const colors = f ? parsePalette(await f.text()) : [];
                if (!colors.length)
                    return;
                remember("palettes", { ...(remember("palettes") || {}), [f.name.replace(/\.txt$/i, "")]: colors });
                AppState.setPalette(colors);
            });
            input.click();
        }));
        entries.push(entry(tr("Guardar la paleta en un archivo…"), "document-save", null, () => {
            const a = el("a", { href: URL.createObjectURL(new Blob([serializePalette(AppState.palette)], { type: "text/plain" })), download: "Kaint.txt" });
            a.click();
            setTimeout(() => URL.revokeObjectURL(a.href), 1000);
        }));
        entries.push("-");
        entries.push(entry(tr("Restablecer la paleta predeterminada"), "edit-reset", defaultPalette(), () => AppState.resetPalette()));
        entries.push("-");
        entries.push(entry(tr("Arcoíris"), "palette", rainbowPalette(), () => AppState.setPalette(rainbowPalette())));
        entries.push(entry(tr("Pequeña"), "palette", smallPalette(), () => AppState.setPalette(smallPalette())));
        popupMenu(entries, r.left, r.bottom);
    });
    // Como en Kaint: dos filas y, con «Más», los 96 colores.
    const drawPalette = () => {
        palette.replaceChildren();
        AppState.palette.slice(0, expanded ? 96 : 32).forEach((c, i) => {
            const sw = el("div", { className: "swatch", title: `#${hex(c, (c.a ?? 255) < 255)}\n${tr("Clic izquierdo: ranura activa · clic derecho: ranura inactiva")}` });
            sw.style.setProperty("--c", cssColor(c));
            sw.addEventListener("pointerdown", (e) => {
                if (addMode) {
                    const colors = [...AppState.palette];
                    colors[i] = { ...active() };
                    AppState.setPalette(colors);
                    addMode = false;
                    addColor.classList.remove("checked");
                    palette.classList.remove("add-mode");
                    return;
                }
                if (e.button === 2) setInactive(c);
                else setActive(c);
            });
            sw.addEventListener("contextmenu", (e) => e.preventDefault());
            palette.appendChild(sw);
        });
    };
    AppState.addEventListener("palette", drawPalette);
    drawPalette();

    // Vista ampliada: secciones de Paint.NET con un deslizador y una caja numérica por canal.
    const channels = [];
    const section = (text) => extended.appendChild(el("div", { className: "section", textContent: text }));
    const hexInput = el("input", { type: "text", className: "hex", maxLength: 9, title: tr("RRGGBB o RRGGBBAA") });
    const row = (channel, label) => {
        const apply = (v) => {
            const c = { ...active() };
            if (channel === "h") setHsv({ ...hsv, h: v });
            else if (channel === "s") setHsv({ ...hsv, s: v / 100 });
            else if (channel === "v") setHsv({ ...hsv, v: v / 100 });
            else setActive({ ...c, [channel]: v });
        };
        const slider = new ChannelSlider(channel, false, apply);
        const spin = el("input", { type: "number", min: 0, max: slider.maximum, step: 1 });
        spin.addEventListener("input", () => {
            const v = Number(spin.value);
            if (spin.value !== "" && Number.isFinite(v))
                apply(Math.min(slider.maximum, Math.max(0, Math.round(v))));
        });
        spin.addEventListener("change", () => (spin.value = slider.value()));
        extended.append(el("span", { textContent: label }), slider.canvas, spin);
        channels.push({ slider, spin });
        if (channel === "b")
            extended.append(el("span", { textContent: tr("Hex:") }), hexInput);
    };
    section("RGB");
    row("r", tr("R:")); row("g", tr("V:")); row("b", tr("A:"));
    section(tr("MSV (HSV)"));
    row("h", tr("M:")); row("s", tr("S:")); row("v", trc("valor (MSV)", "V:"));
    section(tr("Opacidad (canal alfa)"));
    row("a", "");
    // Solo si se escribió algo: al salir del campo no se vuelve a poner un color de antes.
    let hexEdited = false;
    hexInput.addEventListener("input", () => {
        hexEdited = true;
        hexInput.value = hexInput.value.replace(/[^#0-9a-f]/gi, "");
    });
    const applyHex = () => {
        if (!hexEdited)
            return;
        hexEdited = false;
        const t = hexInput.value.replace(/#/g, "");
        const c = t.length === 6 || t.length === 8 ? parseHex(t) : null;
        if (c)
            setActive(t.length === 6 ? { ...c, a: active().a } : c);
        sync();
    };
    hexInput.addEventListener("change", applyHex);
    hexInput.addEventListener("keydown", (e) => e.key === "Enter" && applyHex());

    // Escala (como el ajuste de Kaint al cambiar el tamaño de la ventana) y ancho de cada vista.
    let scale = 1;
    const baseWidth = () => (expanded ? 466 : 220);
    const applyScale = (s) => {
        scale = Math.min(3, Math.max(0.8, Number(s) || 1));
        panel.style.setProperty("--s", scale);
        panel.style.width = `${Math.round(baseWidth() * scale)}px`;
    };
    panel.fit = {
        initial: 1,
        state: () => scale,
        apply: applyScale,
        fromSize(w, h, horizontal, vertical) {
            const sx = w / baseWidth(), sy = h / Math.max(1, panel.offsetHeight / scale);
            return vertical && !horizontal ? sy : horizontal && !vertical ? sx : Math.max(sx, sy);
        },
    };
    applyScale(1);

    const setExpanded = (on) => {
        expanded = on;
        target.style.display = on ? "none" : "";
        value.canvas.style.display = on ? "none" : "";
        if (on)
            topRow.prepend(swatches.canvas);
        else
            swatchColumn.prepend(swatches.canvas);
        extended.style.display = on ? "" : "none";
        panel.classList.toggle("expanded", on);
        moreText();
        drawPalette();
        applyScale(scale);
        app.panels.colors.reposition();
        app.panels.colors.save();
    };
    more.addEventListener("click", () => setExpanded(!expanded));
    // Con el aspecto de Paint.NET, «Más >>» y «<< Menos».
    const moreText = () => {
        const pdn = Settings.get("uiStyle") === 1;
        more.textContent = pdn ? (expanded ? tr("<< Menos") : tr("Más >>")) : expanded ? tr("« Menos") : tr("Más »");
        // Paint.NET compacto: «Más >>» a la derecha de las muestras, encima de la rueda.
        if (pdn && !expanded)
            wheelColumn.prepend(more);
        else if (more.parentElement !== topRow)
            topRow.appendChild(more);
    };
    moreText();

    const sync = () => {
        const c = active(), fromHsv = hsvToRgb(hsv.h, hsv.s, hsv.v);
        if (fromHsv.r !== c.r || fromHsv.g !== c.g || fromHsv.b !== c.b) {
            const n = rgbToHsv(c);
            // Sin saturación o sin brillo, el tono y la saturación de antes se conservan.
            hsv = { h: n.s > 0 && n.v > 0 ? n.h : hsv.h, s: n.v > 0 ? n.s : hsv.s, v: n.v };
        }
        target.value = AppState.secondaryActive ? "1" : "0";
        wheel.set(hsv);
        value.set(c, hsv);
        for (const { slider, spin } of channels) {
            slider.set(c, hsv);
            if (document.activeElement !== spin)
                spin.value = slider.value();
        }
        if (document.activeElement !== hexInput || !hexEdited)
            hexInput.value = hex(c, (c.a ?? 255) < 255);
        // Como en Paint.NET: el color activo con un «+».
        const ctx = addIcon.getContext("2d"), dpr = window.devicePixelRatio || 1;
        addIcon.width = addIcon.height = 16 * dpr;
        ctx.setTransform(dpr / 2, 0, 0, dpr / 2, 0, 0);
        ctx.fillStyle = `rgb(${c.r},${c.g},${c.b})`;
        ctx.strokeStyle = "rgb(60,60,64)";
        ctx.lineWidth = 2;
        ctx.fillRect(2, 2, 24, 24);
        ctx.strokeRect(2, 2, 24, 24);
        ctx.beginPath();
        for (const [x, y, w, h] of [[20, 13, 5, 18], [13.5, 19.5, 18, 5]])
            ctx.rect(x, y, w, h);
        ctx.fillStyle = "#fff";
        ctx.fill("nonzero");
        ctx.beginPath();
        ctx.moveTo(20, 13); ctx.lineTo(25, 13); ctx.lineTo(25, 19.5); ctx.lineTo(31.5, 19.5); ctx.lineTo(31.5, 24.5);
        ctx.lineTo(25, 24.5); ctx.lineTo(25, 31); ctx.lineTo(20, 31); ctx.lineTo(20, 24.5); ctx.lineTo(13.5, 24.5);
        ctx.lineTo(13.5, 19.5); ctx.lineTo(20, 19.5); ctx.closePath();
        ctx.strokeStyle = "rgb(50,110,100)";
        ctx.lineWidth = 1.6;
        ctx.stroke();
    };
    AppState.addEventListener("colors", sync);
    window.addEventListener("kaint-settings", () => {
        swatches.paint();
        moreText();
        wheel.resized();
        sync();
        // El selector de ranura y el brillo aparecen o desaparecen: el tamaño de la ventana cambia.
        app.panels?.colors.reposition();
    });
    sync();
    return panel;
}

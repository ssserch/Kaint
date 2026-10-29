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
import { remember } from "../Settings.js";
import { plain, tr } from "../i18n.js";

export class FloatingPanel {
    constructor(workspace, id, title, content, { corner = "tl", offset = { x: 8, y: 8 }, size = null, onClose = null } = {}) {
        this.workspace = workspace;
        this.id = id;
        this.defaults = { corner, offset, size };
        this.onClose = onClose;
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
        if (saved.size || size) {
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
        this.element.style.width = this.defaults.size ? `${this.defaults.size.w}px` : "";
        this.element.style.height = this.defaults.size?.h ? `${this.defaults.size.h}px` : "";
        this.setVisible(true);
    }
}

// --- Herramientas -------------------------------------------------------------------------

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
        const b = el("button", { className: "tbutton", title: tip }, [iconName ? icon(iconName, 16) : el("span", { textContent: text })]);
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
    app.addEventListener("layers", refresh);
    app.addEventListener("document", refresh);
    // Clic: capa activa · arrastrar: mover la capa (una línea marca dónde quedará).
    let drag = null;
    list.addEventListener("pointerdown", (e) => {
        const row = e.target.closest(".row");
        if (!row || e.button !== 0)
            return;
        app.doc.setActiveIndex(row.index);
        drag = { row, y: e.clientY, moving: false };
        list.setPointerCapture(e.pointerId);
    });
    list.addEventListener("pointermove", (e) => {
        if (!drag)
            return;
        if (!drag.moving && Math.abs(e.clientY - drag.y) > 4) {
            drag.moving = true;
            drag.row.classList.add("dragging");
        }
        if (!drag.moving)
            return;
        const target = rowAt(e.clientY);
        if (!target || target === drag.row) {
            dropLine.style.display = "none";
            return;
        }
        const below = target.index < drag.row.index;
        dropLine.style.display = "block";
        dropLine.style.top = `${target.offsetTop + (below ? target.offsetHeight - 2 : -1)}px`;
        drag.target = target;
    });
    list.addEventListener("pointerup", () => {
        if (drag?.moving) {
            drag.row.classList.remove("dragging");
            dropLine.style.display = "none";
            if (drag.target && drag.target !== drag.row)
                app.moveLayerTo(drag.row.index, drag.target.index);
        }
        drag = null;
    });
    const rowAt = (y) => [...list.querySelectorAll(".row")].find((r) => {
        const b = r.getBoundingClientRect();
        return y >= b.top && y < b.bottom;
    });
    const buttons = el("div", { className: "panel-buttons" });
    for (const id of actionIds)
        buttons.appendChild(app.actionButton(id));
    return el("div", { style: { display: "flex", flexDirection: "column", flex: "1", minHeight: "0" } }, [list, buttons]);
}

// --- Colores ------------------------------------------------------------------------------

// Ranuras principal y secundaria como las de Paint.NET: la activa lleva marco grueso y un pico
// abajo; al cambiar, las dos se animan a la vez (una adelgaza y pierde el pico, la otra crece).
class ColorSwatches {
    constructor() {
        this.canvas = el("canvas", { width: 64, height: 64, style: { width: "64px", height: "64px" } });
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
        const checker = (x, y, w, h) => {
            for (let yy = 0; yy < h; yy += 5)
                for (let xx = 0; xx < w; xx += 5) {
                    p.fillStyle = (Math.floor(xx / 5) + Math.floor(yy / 5)) % 2 ? "#bfbfbf" : "#fff";
                    p.fillRect(x + xx, y + yy, Math.min(5, w - xx), Math.min(5, h - yy));
                }
        };
        const swatch = (x, y, color, level, framed) => {
            const s = 35, ring = framed ? 1 + level : 2 * level, line = framed ? 1 : level;
            p.fillStyle = "#000";
            p.fillRect(x, y, s, s);
            if (ring > 0) {
                p.fillStyle = "#fff";
                p.fillRect(x + 1, y + 1, s - 2, s - 2);
            }
            const ix = x + 1 + ring, iy = y + 1 + ring, iw = s - 2 - 2 * ring;
            // Marco negro de dentro con el pico de la ranura activa recortado abajo.
            p.save();
            p.beginPath();
            p.rect(ix, iy, iw, iw);
            const cx = ix + iw / 2, bottom = iy + iw;
            if (level > 0) {
                p.moveTo(cx - 5, bottom + 1);
                p.lineTo(cx, bottom + 1 - 7 * level);
                p.lineTo(cx + 5, bottom + 1);
                p.closePath();
            }
            p.fillStyle = "#000";
            p.fill("evenodd");
            p.restore();
            p.save();
            p.beginPath();
            p.rect(ix + line, iy + line, iw - 2 * line, iw - 2 * line);
            if (level > 0) {
                p.moveTo(cx - 6.5, bottom + 1);
                p.lineTo(cx, bottom + 1 - 8.5 * level);
                p.lineTo(cx + 6.5, bottom + 1);
                p.closePath();
            }
            p.clip("evenodd");
            checker(ix, iy, iw, iw);
            p.fillStyle = `rgba(${color.r},${color.g},${color.b},${color.a / 255})`;
            p.fillRect(ix, iy, iw, iw);
            p.restore();
        };
        swatch(19, 19, AppState.secondary, 1 - this.level, false);
        swatch(1, 1, AppState.primary, this.level, true);
        if (this.swap.complete)
            p.drawImage(this.swap, 40, 3, 14, 13);
        p.strokeStyle = "#787878";
        p.fillStyle = "#000";
        p.fillRect(1.5, 39.5, 10, 10);
        p.strokeRect(1.5, 39.5, 10, 10);
        p.fillStyle = "#fff";
        p.fillRect(6.5, 44.5, 10, 10);
        p.strokeRect(6.5, 44.5, 10, 10);
    }
}

export function colorsPanel(app) {
    const panel = el("div", { className: "colors-panel" });
    // Cabecera: ranura activa y más opciones
    const target = el("select", {}, [el("option", { value: "0", textContent: tr("Principal") }),
                                     el("option", { value: "1", textContent: tr("Secundario") })]);
    target.addEventListener("change", () => AppState.setSecondaryActive(target.value === "1"));
    const more = el("button", { className: "pushbutton", textContent: tr("Más »"), style: { minWidth: "0" } });
    panel.appendChild(el("div", { className: "top" }, [target, el("span", { style: { flex: 1 } }), more]));
    // Ranuras, rueda de color y brillo
    const swatches = new ColorSwatches();
    const wheel = el("canvas", { className: "wheel", width: 116, height: 116, style: { width: "116px", height: "116px" } });
    const value = el("canvas", { className: "value-slider", width: 14, height: 116, style: { width: "14px", height: "116px" } });
    panel.appendChild(el("div", { className: "middle" }, [swatches.canvas, wheel, value]));
    let hsv = rgbToHsv(AppState.activeColor());
    const setActive = (c) => AppState.setActiveColor({ ...c, a: AppState.activeColor().a });
    // La rueda va siempre con el brillo máximo (el brillo es el deslizador de al lado).
    const drawWheel = () => {
        const dpr = window.devicePixelRatio || 1, size = 116;
        wheel.width = wheel.height = Math.round(size * dpr);
        const ctx = wheel.getContext("2d"), img = ctx.createImageData(wheel.width, wheel.height), R = wheel.width / 2;
        for (let y = 0; y < wheel.height; ++y)
            for (let x = 0; x < wheel.width; ++x) {
                const dx = x + 0.5 - R, dy = y + 0.5 - R, d = Math.hypot(dx, dy) / (R - 1);
                if (d > 1.01) continue;
                const c = hsvToRgb(((Math.atan2(-dy, dx) * 180) / Math.PI + 360) % 360, Math.min(1, d), 1);
                const i = (y * wheel.width + x) * 4;
                img.data[i] = c.r; img.data[i + 1] = c.g; img.data[i + 2] = c.b; img.data[i + 3] = d > 1 ? (1.01 - d) * 2550 : 255;
            }
        ctx.putImageData(img, 0, 0);
        const a = (hsv.h * Math.PI) / 180, mx = R + Math.cos(a) * hsv.s * (R - 1), my = R - Math.sin(a) * hsv.s * (R - 1);
        ctx.lineWidth = dpr;
        ctx.strokeStyle = "#fff";
        ctx.beginPath(); ctx.arc(mx, my, 5 * dpr, 0, Math.PI * 2); ctx.stroke();
        ctx.strokeStyle = "#000";
        ctx.beginPath(); ctx.arc(mx, my, 4 * dpr, 0, Math.PI * 2); ctx.stroke();
        // Brillo
        value.width = Math.round(14 * dpr);
        value.height = Math.round(116 * dpr);
        const v = value.getContext("2d"), g = v.createLinearGradient(0, 0, 0, value.height);
        const top = hsvToRgb(hsv.h, hsv.s, 1);
        g.addColorStop(0, `rgb(${top.r},${top.g},${top.b})`);
        g.addColorStop(1, "#000");
        v.fillStyle = g;
        v.fillRect(2 * dpr, 0, value.width - 4 * dpr, value.height);
        const y = (1 - hsv.v) * (value.height - 1);
        v.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--text");
        v.beginPath(); v.moveTo(0, y - 4 * dpr); v.lineTo(3 * dpr, y); v.lineTo(0, y + 4 * dpr); v.fill();
        v.beginPath(); v.moveTo(value.width, y - 4 * dpr); v.lineTo(value.width - 3 * dpr, y); v.lineTo(value.width, y + 4 * dpr); v.fill();
    };
    const dragOn = (canvasEl, fn) => {
        canvasEl.addEventListener("pointerdown", (e) => {
            canvasEl.setPointerCapture(e.pointerId);
            fn(e);
            const move = (ev) => fn(ev);
            const up = () => {
                canvasEl.removeEventListener("pointermove", move);
                canvasEl.removeEventListener("pointerup", up);
            };
            canvasEl.addEventListener("pointermove", move);
            canvasEl.addEventListener("pointerup", up);
        });
    };
    dragOn(wheel, (e) => {
        const r = wheel.getBoundingClientRect(), R = r.width / 2;
        const dx = e.clientX - r.left - R, dy = e.clientY - r.top - R;
        let h = ((Math.atan2(-dy, dx) * 180) / Math.PI + 360) % 360;
        if (e.shiftKey) h = Math.round(h / 15) * 15;
        hsv = { ...hsv, h, s: Math.min(1, Math.hypot(dx, dy) / (R - 1)) };
        setActive(hsvToRgb(hsv.h, hsv.s, hsv.v));
    });
    dragOn(value, (e) => {
        const r = value.getBoundingClientRect();
        hsv = { ...hsv, v: Math.min(1, Math.max(0, 1 - (e.clientY - r.top) / r.height)) };
        setActive(hsvToRgb(hsv.h, hsv.s, hsv.v));
    });
    // Paleta: clic izquierdo = ranura activa, derecho = inactiva.
    const paletteRow = el("div", { style: { display: "flex", gap: "2px", alignItems: "center" } });
    const addColor = el("button", { className: "tbutton", title: tr("Añadir el color activo a la paleta: haz clic después en la muestra que quieras sustituir") });
    const addIcon = el("canvas", { width: 16, height: 16, style: { width: "16px", height: "16px" } });
    addColor.appendChild(addIcon);
    const paletteMenu = el("button", { className: "tbutton palette-menu", title: tr("Paletas") }, [icon("palette", 16), el("span", { className: "arrow", textContent: "▾" })]);
    paletteRow.append(addColor, paletteMenu);
    panel.appendChild(paletteRow);
    const palette = el("div", { className: "palette" });
    panel.appendChild(palette);
    let allColors = false;
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
        AppState.palette.slice(0, allColors ? 96 : 32).forEach((c, i) => {
            const sw = el("div", { className: "swatch", title: `#${hex(c, true)}`, style: { "--c": `rgba(${c.r},${c.g},${c.b},${c.a / 255})` } });
            sw.style.setProperty("--c", `rgba(${c.r},${c.g},${c.b},${c.a / 255})`);
            sw.addEventListener("pointerdown", (e) => {
                if (addMode) {
                    const colors = [...AppState.palette];
                    colors[i] = { ...AppState.activeColor() };
                    AppState.setPalette(colors);
                    addMode = false;
                    addColor.classList.remove("checked");
                    palette.classList.remove("add-mode");
                    return;
                }
                const active = e.button !== 2;
                if (active === !AppState.secondaryActive) AppState.setPrimary(c);
                else AppState.setSecondary(c);
            });
            sw.addEventListener("contextmenu", (e) => e.preventDefault());
            palette.appendChild(sw);
        });
    };
    AppState.addEventListener("palette", drawPalette);
    drawPalette();
    // Más: RGB, HSV, alfa y hexadecimal
    const moreBox = el("div", { className: "more", style: { display: "none" } });
    const fields = {};
    const field = (key, label, max) => {
        const range = el("input", { type: "range", min: 0, max, step: 1 });
        const num = el("input", { type: "number", min: 0, max, step: 1 });
        const set = (v) => {
            const c = { ...AppState.activeColor() }, h = { ...hsv };
            if ("rgba".includes(key)) { c[key] = v; AppState.setActiveColor(c); return; }
            if (key === "h") h.h = v; else if (key === "s") h.s = v / 100; else h.v = v / 100;
            hsv = h;
            AppState.setActiveColor({ ...hsvToRgb(h.h, h.s, h.v), a: c.a });
        };
        range.addEventListener("input", () => set(Number(range.value)));
        num.addEventListener("change", () => set(Math.min(max, Math.max(0, Number(num.value) || 0))));
        moreBox.append(el("span", { textContent: label }), range, num);
        fields[key] = (v) => { range.value = num.value = Math.round(v); };
    };
    field("r", tr("Rojo"), 255); field("g", tr("Verde"), 255); field("b", tr("Azul"), 255);
    field("h", tr("Matiz"), 360); field("s", tr("Saturación"), 100); field("v", tr("Brillo"), 100); field("a", tr("Alfa"), 255);
    const hexInput = el("input", { type: "text", style: { width: "100%" } });
    hexInput.addEventListener("change", () => {
        const c = parseHex(hexInput.value);
        if (c) AppState.setActiveColor(c);
    });
    moreBox.append(el("span", { textContent: tr("Hex:") }), hexInput, el("span"));
    panel.appendChild(moreBox);
    more.addEventListener("click", () => {
        const open = moreBox.style.display === "none";
        moreBox.style.display = open ? "" : "none";
        more.textContent = open ? tr("« Menos") : tr("Más »");
        allColors = open;
        drawPalette();
        app.panels.colors.reposition();
    });
    const sync = () => {
        const c = AppState.activeColor(), fromHsv = hsvToRgb(hsv.h, hsv.s, hsv.v);
        if (fromHsv.r !== c.r || fromHsv.g !== c.g || fromHsv.b !== c.b) {
            const n = rgbToHsv(c);
            // Sin saturación o sin brillo, el tono y la saturación de antes se conservan.
            hsv = { h: n.s > 0 && n.v > 0 ? n.h : hsv.h, s: n.v > 0 ? n.s : hsv.s, v: n.v };
        }
        target.value = AppState.secondaryActive ? "1" : "0";
        drawWheel();
        for (const k of ["r", "g", "b", "a"]) fields[k](c[k]);
        fields.h(hsv.h); fields.s(hsv.s * 100); fields.v(hsv.v * 100);
        if (document.activeElement !== hexInput) hexInput.value = hex(c, c.a < 255);
        const ctx = addIcon.getContext("2d"), dpr = window.devicePixelRatio || 1;
        addIcon.width = addIcon.height = 16 * dpr;
        ctx.scale(dpr, dpr);
        ctx.fillStyle = `rgb(${c.r},${c.g},${c.b})`;
        ctx.fillRect(1, 1, 12, 12);
        ctx.strokeStyle = "#3c3c40";
        ctx.strokeRect(1.5, 1.5, 11, 11);
        ctx.fillStyle = "#fff";
        ctx.strokeStyle = "#326e64";
        ctx.fillRect(10, 6.5, 2.5, 9); ctx.fillRect(6.75, 9.75, 9, 2.5);
    };
    AppState.addEventListener("colors", sync);
    sync();
    return panel;
}

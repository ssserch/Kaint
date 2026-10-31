// Diálogos de Kaint: la base modal (con el parpadeo de atención de paint.js al pulsar fuera),
// las tareas con opciones (TaskDialog) y los de Imagen y Capas, Configuración y Acerca de.
import { icon } from "./Icons.js";
import { shortcutLabel } from "./Actions.js";
import { BlendModes, BlendNames, checkerPattern, thumbnail } from "../core/Raster.js";
import { delayFromName, MIN_DELAY, nameWithDelay } from "../core/Codecs.js";
import { MAX_WORKERS } from "../core/EffectRunner.js";
import { remember, Settings } from "../Settings.js";
import { AppState, defaultTools, LocalToolSettings, setToolDefaults, toolDefaults } from "../AppState.js";
import { ToolOptionsPanel } from "./ToolOptions.js";
import { CustomShapes } from "../core/CustomShapes.js";
import { DEFAULT_PACK, IconPacks } from "../IconPacks.js";
import { Plugins } from "../plugins/Plugins.js";
import { UserFonts } from "../UserFonts.js";
import { REPOSITORY, VERSION } from "../version.js";
import { ColorRoles, Presets } from "./Themes.js";
import { currentLanguage, plain, tr } from "../i18n.js";
import { ResampleMethods } from "../core/Resample.js";

let openDialogs = 0;

export function dialogOpen() {
    return openDialogs > 0;
}

export function el(tag, props = {}, children = []) {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(props)) {
        if (k === "style" && typeof v === "object") Object.assign(e.style, v);
        else if (k === "dataset") Object.assign(e.dataset, v);
        else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
        else if (k in e) {
            // Las propiedades de solo lectura (como "list") van como atributo.
            try { e[k] = v; } catch { e.setAttribute(k, v); }
        } else e.setAttribute(k, v);
    }
    for (const c of [].concat(children))
        if (c != null) e.append(c);
    return e;
}

export class Dialog {
    constructor(title, { iconName = null } = {}) {
        this.backdrop = el("div", { className: "backdrop" });
        this.element = el("div", { className: "dialog", role: "dialog" });
        // Cabecera (se arrastra)
        const head = el("div", { className: "title" });
        if (iconName)
            head.appendChild(icon(iconName, 16));
        head.appendChild(el("span", { textContent: plain(title) }));
        const close = el("button", { className: "close", title: tr("Cerrar") });
        close.addEventListener("click", () => this.close(null));
        head.appendChild(close);
        this.element.appendChild(head);
        this.body = el("div", { className: "body" });
        this.element.appendChild(this.body);
        this.buttons = el("div", { className: "buttons" });
        this.element.appendChild(this.buttons);
        this.backdrop.appendChild(this.element);
        this.defaultValue = undefined;
        this.makeDraggable(head);
        this.lastAttention = 0;
        // Pulsar fuera: el diálogo llama la atención (sombra y título parpadean cinco veces).
        this.backdrop.addEventListener("pointerdown", (e) => {
            if (e.target !== this.backdrop)
                return;
            const now = performance.now();
            if (now - this.lastAttention < 250)
                return;
            this.lastAttention = now;
            this.element.classList.remove("attention");
            void this.element.offsetWidth;
            this.element.classList.add("attention");
            clearTimeout(this.attentionTimer);
            this.attentionTimer = setTimeout(() => this.element.classList.remove("attention"), 850);
        });
        this.backdrop.addEventListener("keydown", (e) => {
            e.stopPropagation();
            if (e.key === "Escape") {
                e.preventDefault();
                this.close(null);
            } else if (e.key === "Enter" && e.target.tagName !== "TEXTAREA" && e.target.tagName !== "BUTTON" && this.defaultValue !== undefined) {
                e.preventDefault();
                this.accept(this.defaultValue);
            }
        });
    }

    makeDraggable(handle) {
        let start = null;
        handle.addEventListener("pointerdown", (e) => {
            if (e.target.closest("button"))
                return;
            const r = this.element.getBoundingClientRect();
            start = { x: e.clientX, y: e.clientY, left: r.left, top: r.top };
            handle.setPointerCapture(e.pointerId);
        });
        handle.addEventListener("pointermove", (e) => {
            if (!start)
                return;
            this.element.style.position = "fixed";
            this.element.style.margin = "0";
            this.element.style.left = `${start.left + e.clientX - start.x}px`;
            this.element.style.top = `${Math.max(0, start.top + e.clientY - start.y)}px`;
        });
        handle.addEventListener("pointerup", () => (start = null));
    }

    addButton(text, value, { isDefault = false } = {}) {
        const b = el("button", { className: "pushbutton" + (isDefault ? " default" : ""), textContent: plain(text) });
        b.addEventListener("click", () => this.accept(value));
        this.buttons.appendChild(b);
        if (isDefault)
            this.defaultValue = value;
        return b;
    }

    // Antes de aceptar se puede comprobar algo (validate devuelve false para seguir abierto).
    accept(value) {
        if (value !== null && this.validate && this.validate(value) === false)
            return;
        this.close(value);
    }

    exec() {
        openDialogs++;
        // Al cerrarse, el foco vuelve a donde estaba (por ejemplo, al diálogo de un efecto después
        // de elegir un color).
        this.returnFocus = document.activeElement;
        document.body.appendChild(this.backdrop);
        // El foco entra ya (si no, un Esc o un Intro pulsado justo al abrirse se perdía); se repite
        // tras el primer pintado por si el navegador aún no lo aceptaba.
        const focus = this.element.querySelector("input, select, textarea") || this.element.querySelector(".pushbutton.default")
            || this.element.querySelector(".title .close");
        focus?.focus({ preventScroll: true });
        setTimeout(() => this.backdrop.contains(document.activeElement) || focus?.focus({ preventScroll: true }), 0);
        return new Promise((resolve) => (this.resolve = resolve));
    }

    close(value) {
        if (!this.resolve)
            return;
        openDialogs--;
        this.backdrop.remove();
        if (this.returnFocus?.isConnected)
            this.returnFocus.focus({ preventScroll: true });
        const resolve = this.resolve;
        this.resolve = null;
        this.onClose?.(value);
        resolve(value);
    }
}

// --- Controles ---------------------------------------------------------------------------

export function numberInput(value, min, max, step = 1, decimals = 0) {
    const input = el("input", { type: "number", value: String(value), min: String(min), max: String(max), step: String(step) });
    input.read = () => {
        const v = parseFloat(input.value);
        return Number.isFinite(v) ? Math.min(max, Math.max(min, decimals ? v : Math.round(v))) : min;
    };
    return input;
}

export function checkbox(text, checked) {
    const input = el("input", { type: "checkbox", checked });
    const label = el("label", {}, [input, plain(text)]);
    label.input = input;
    return label;
}

export function select(options, value) {
    const s = el("select");
    for (const [v, text] of options)
        s.appendChild(el("option", { value: v, textContent: plain(text) }));
    s.value = value;
    return s;
}

// Deslizador con su número al lado.
export function slider(value, min, max, step = 1, onChange = null) {
    const range = el("input", { type: "range", min, max, step, value });
    const number = numberInput(value, min, max, step, step < 1 ? 2 : 0);
    const row = el("div", { style: { display: "flex", gap: "6px", alignItems: "center" } }, [range, number]);
    range.style.flex = "1";
    range.addEventListener("input", () => {
        number.value = range.value;
        onChange?.(parseFloat(range.value));
    });
    number.addEventListener("input", () => {
        range.value = number.value;
        onChange?.(number.read());
    });
    row.value = () => number.read();
    row.set = (v) => {
        range.value = number.value = v;
    };
    return row;
}

// --- Tareas ------------------------------------------------------------------------------

// Pregunta con opciones grandes (icono, título y explicación), como TaskDialog de Kaint.
export class TaskDialog extends Dialog {
    constructor(title, iconName, question, image = null) {
        super(title, { iconName });
        this.task = el("div", { className: "task" });
        const q = el("div", { className: "question" });
        if (image)
            q.appendChild(image);
        else if (iconName)
            q.appendChild(icon(iconName, 32));
        q.appendChild(el("div", { textContent: question, style: { whiteSpace: "pre-wrap", maxWidth: "420px" } }));
        this.task.appendChild(q);
        this.body.appendChild(this.task);
        this.buttons.remove();
        this.count = 0;
    }

    addChoice(iconName, title, description, isDefault = false) {
        const value = this.count++;
        const b = el("button", { className: "choice-button" }, [icon(iconName, 24), el("b", { textContent: plain(title) }),
                                                                 el("small", { textContent: description })]);
        b.addEventListener("click", () => this.close(value));
        this.task.appendChild(b);
        if (isDefault) {
            this.defaultValue = value;
            setTimeout(() => b.focus(), 0);
        }
        return value;
    }
}

export async function message(title, text, iconName = "dialog-warning") {
    const dlg = new Dialog(title, { iconName });
    dlg.body.appendChild(el("div", { className: "task" }, [
        el("div", { className: "question" }, [icon(iconName, 32), el("div", { textContent: text, style: { whiteSpace: "pre-wrap", maxWidth: "420px" } })]),
    ]));
    dlg.addButton(tr("Aceptar"), true, { isDefault: true });
    return dlg.exec();
}

// --- Imagen ------------------------------------------------------------------------------

// Tamaño en memoria de una capa (o de varias), como en los diálogos de Kaint.
function memoryText(w, h, layers = 1) {
    const mb = (w * h * 4 * layers) / (1024 * 1024), lang = currentLanguage();
    const n = (v, d) => v.toLocaleString(lang, { minimumFractionDigits: d, maximumFractionDigits: d });
    return mb >= 1024 ? tr("%1 GB", n(mb / 1024, 2)) : tr("%1 MB", n(mb, 1));
}

const MAX_SIDE = 65535;

// Recuadro con título (QGroupBox).
function groupBox(title, children) {
    return el("fieldset", { className: "group" }, [el("legend", { textContent: title }), ...children]);
}

// Tamaño como en los diálogos de Paint.NET (SizeEditor de Kaint): por porcentaje o absoluto,
// "Mantener la relación de aspecto", tamaño en píxeles y tamaño de impresión (resolución en
// píxeles/pulgada o píxeles/cm y ancho/alto en pulgadas o centímetros).
function sizeEditor(original, dpi, allowPercent, onChange) {
    const aspect = original.h > 0 ? original.w / original.h : 1;
    let currentDpi = dpi, updating = false;
    const element = el("div", { className: "size-editor" });
    let byPercent = null, percent = null, percentRow = null;
    if (allowPercent) {
        const name = `size-${Math.random().toString(36).slice(2)}`;
        byPercent = el("input", { type: "radio", name, checked: true });
        const byAbsolute = el("input", { type: "radio", name });
        percent = numberInput(100, 1, 10000);
        percentRow = el("span", { className: "with-unit" }, [percent, el("span", { textContent: "%" })]);
        element.append(el("div", { className: "row" }, [el("label", {}, [byPercent, tr("Por porcentaje:")]), percentRow]),
                       el("label", { className: "row" }, [byAbsolute, tr("Por tamaño absoluto")]));
        byPercent.addEventListener("change", () => updateMode());
        byAbsolute.addEventListener("change", () => updateMode());
        percent.addEventListener("input", () => onPercent());
    }
    const keep = checkbox(tr("Mantener la relación de aspecto"), true);
    element.appendChild(keep);

    // Como en Paint.NET: cada número en su campo y la unidad a la derecha.
    const width = numberInput(original.w, 1, MAX_SIDE), height = numberInput(original.h, 1, MAX_SIDE);
    const resolution = numberInput(0, 0.01, 99999, 0.01, 2);
    // Píxeles/centímetro por defecto; se recuerda la última unidad elegida.
    const unit = remember("resolutionUnit") === 0 ? 0 : 1;
    const resolutionUnit = select([["0", tr("píxeles/pulgada")], ["1", tr("píxeles/cm")]], String(unit));
    const printWidth = numberInput(0, 0.01, 999999, 0.01, 2), printHeight = numberInput(0, 0.01, 999999, 0.01, 2);
    const printUnit = select([["0", tr("pulgadas")], ["1", tr("centímetros")]], String(unit));
    const printHeightUnit = el("span");
    const label = (text) => el("label", { textContent: text });
    element.append(
        groupBox(tr("Tamaño en píxeles"), [el("div", { className: "size-grid" }, [
            label(tr("Anchura:")), width, el("span", { textContent: tr("píxeles") }),
            label(tr("Altura:")), height, el("span", { textContent: tr("píxeles") }),
            label(tr("Resolución:")), resolution, resolutionUnit,
        ])]),
        groupBox(tr("Tamaño de la impresión"), [el("div", { className: "size-grid" }, [
            label(tr("Anchura:")), printWidth, printUnit,
            label(tr("Altura:")), printHeight, printHeightUnit,
        ])]),
    );
    const fixed = (input, v) => (input.value = v.toFixed(2));
    const unitPixels = () => (resolutionUnit.value === "1" ? currentDpi / 2.54 : currentDpi);
    const clampSide = (v) => Math.min(MAX_SIDE, Math.max(1, Math.round(v)));
    const changed = () => onChange?.({ w: width.read(), h: height.read() });
    const syncPrint = () => {
        printHeightUnit.textContent = printUnit.selectedOptions[0]?.textContent || "";
        fixed(printWidth, width.read() / unitPixels());
        fixed(printHeight, height.read() / unitPixels());
    };
    const onPixels = (widthChanged) => {
        if (updating)
            return;
        if (keep.input.checked) {
            if (widthChanged)
                height.value = clampSide(width.read() / aspect);
            else
                width.value = clampSide(height.read() * aspect);
        }
        syncPrint();
        changed();
    };
    const setValue = (w, h) => {
        width.value = w;
        height.value = h;
        syncPrint();
        changed();
    };
    function onPercent() {
        if (!percent || !byPercent.checked)
            return;
        const f = percent.read() / 100;
        setValue(clampSide(original.w * f), clampSide(original.h * f));
    }
    function updateMode() {
        const p = byPercent?.checked ?? false;
        if (percent)
            percent.disabled = !p;
        for (const w of [width, height, printWidth, printHeight, keep.input])
            w.disabled = p;
        keep.classList.toggle("disabled", p);
        if (p)
            onPercent();
    }
    const onPrint = (widthChanged) => {
        if (widthChanged)
            width.value = clampSide(printWidth.read() * unitPixels());
        else
            height.value = clampSide(printHeight.read() * unitPixels());
        onPixels(widthChanged);
    };
    width.addEventListener("input", () => onPixels(true));
    height.addEventListener("input", () => onPixels(false));
    printWidth.addEventListener("input", () => onPrint(true));
    printHeight.addEventListener("input", () => onPrint(false));
    resolution.addEventListener("input", () => {
        // La resolución exacta solo cambia cuando se escribe otra (el campo la muestra redondeada).
        currentDpi = resolutionUnit.value === "1" ? resolution.read() * 2.54 : resolution.read();
        if (keep.input.checked && !byPercent?.checked) {
            width.value = clampSide(printWidth.read() * unitPixels());
            onPixels(true);
        } else {
            syncPrint();
            changed();
        }
    });
    resolutionUnit.addEventListener("change", () => {
        const u = Number(resolutionUnit.value);
        fixed(resolution, u === 1 ? currentDpi / 2.54 : currentDpi);
        remember("resolutionUnit", u);
        printUnit.value = String(u);
        syncPrint();
    });
    printUnit.addEventListener("change", () => {
        resolutionUnit.value = printUnit.value;
        resolutionUnit.dispatchEvent(new Event("change"));
    });
    keep.input.addEventListener("change", () => keep.input.checked && onPixels(true));
    fixed(resolution, unit === 1 ? dpi / 2.54 : dpi);
    syncPrint();
    updateMode();
    return {
        element,
        value: () => ({ w: width.read(), h: height.read() }),
        dpi: () => currentDpi,
        setKeepAspect(on) { keep.input.checked = on; },
    };
}

export async function newImageDialog(width, height, dpi = 96) {
    const dlg = new Dialog(tr("Imagen nueva"), { iconName: "document-new" });
    const memory = el("div");
    const size = sizeEditor({ w: width, h: height }, dpi, false, (s) => (memory.textContent = tr("Nuevo tamaño: %1", memoryText(s.w, s.h))));
    size.setKeepAspect(false);
    const bg = select([["white", tr("Blanco")], ["secondary", tr("Color secundario")], ["transparent", tr("Transparente")]], "white");
    memory.textContent = tr("Nuevo tamaño: %1", memoryText(width, height));
    // Como en Paint.NET: el tamaño en memoria arriba; el fondo, abajo.
    dlg.body.appendChild(el("div", { className: "size-dialog" }, [
        memory, size.element, el("div", { className: "row" }, [el("label", { textContent: tr("Fondo:") }), bg]),
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { width: size.value().w, height: size.value().h, background: bg.value, dpi: size.dpi() } : null;
}

export async function resizeDialog(width, height, dpi = 96) {
    const dlg = new Dialog(tr("Cambiar el tamaño"), { iconName: "transform-scale" });
    const method = select(ResampleMethods.map(([id, name]) => [id, tr(name)]), "adaptive");
    const gamma = checkbox(tr("Corrección gamma (en luz lineal)"), false);
    gamma.title = tr("Calcula el remuestreo en luz lineal: los detalles finos conservan su brillo");
    const memory = el("span");
    const update = (s) => (memory.textContent = tr("%1 × %2 (%3 por capa)", s.w, s.h, memoryText(s.w, s.h)));
    const size = sizeEditor({ w: width, h: height }, dpi, true, update);
    update(size.value());
    dlg.body.appendChild(el("div", { className: "size-dialog" }, [
        el("div", { className: "row" }, [el("label", { textContent: tr("Remuestreo:") }), method]),
        el("div", { className: "row indent" }, [gamma]),
        size.element,
        el("div", { className: "row" }, [el("span", { textContent: tr("Tamaño actual: %1 × %2 · nuevo:", width, height) }), memory]),
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { width: size.value().w, height: size.value().h, method: method.value, gamma: gamma.input.checked, dpi: size.dpi() } : null;
}

export async function canvasSizeDialog(width, height, dpi = 96) {
    const dlg = new Dialog(tr("Tamaño del lienzo"), { iconName: "transform-crop-and-resize" });
    const memory = el("span", { textContent: memoryText(width, height) });
    const size = sizeEditor({ w: width, h: height }, dpi, true, (s) => (memory.textContent = memoryText(s.w, s.h)));
    size.setKeepAspect(false);
    // Anclaje: el ancla en la casilla elegida y flechas desde ella hacia donde crece el lienzo.
    let anchor = [1, 1];
    const cells = [];
    const arrows = [["↖", "↑", "↗"], ["←", "●", "→"], ["↙", "↓", "↘"]];
    const updateArrows = () => cells.forEach((b, i) => {
        const r = Math.floor(i / 3), c = i % 3, dr = r - anchor[1], dc = c - anchor[0];
        b.classList.toggle("checked", dr === 0 && dc === 0);
        b.replaceChildren();
        if (dr === 0 && dc === 0)
            b.appendChild(icon("anchor", 24));
        else if (Math.abs(dr) <= 1 && Math.abs(dc) <= 1)
            b.textContent = arrows[dr + 1][dc + 1];
    });
    const grid = el("div", { className: "anchor-grid" });
    for (let y = 0; y < 3; ++y)
        for (let x = 0; x < 3; ++x) {
            const b = el("button", { className: "tbutton anchor-cell", type: "button" });
            b.addEventListener("click", () => {
                anchor = [x, y];
                updateArrows();
            });
            cells.push(b);
            grid.appendChild(b);
        }
    updateArrows();
    dlg.body.appendChild(el("div", { className: "size-dialog" }, [
        el("div", { textContent: tr("Tamaño actual: %1 × %2 píxeles", width, height) }),
        size.element,
        groupBox(tr("Anclaje"), [grid]),
        el("div", { className: "row" }, [el("span", { textContent: tr("Memoria por capa:") }), memory]),
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { width: size.value().w, height: size.value().h, anchor, dpi: size.dpi() } : null;
}

// Propiedades de la capa con vista previa al momento (onPreview recibe las propiedades). Como
// en Paint.NET 5: Nombre, Opacidad, Modo de fusión y Visible.
export async function layerPropertiesDialog(props, onPreview) {
    const dlg = new Dialog(tr("Propiedades de la capa"), { iconName: "layer-properties" });
    const name = el("input", { type: "text", value: props.name });
    const visible = checkbox(tr("Visible"), props.visible);
    const blend = select(BlendModes.map((m) => [m, tr(BlendNames[m][0])]), props.blend);
    const opacity = slider(props.opacity, 0, 255, 1);
    const current = () => ({ name: name.value || props.name, visible: visible.input.checked, blend: blend.value, opacity: opacity.value() });
    for (const c of [name, visible.input, blend, ...opacity.querySelectorAll("input")])
        c.addEventListener("input", () => onPreview(current()));
    dlg.body.appendChild(el("div", { className: "layer-props" }, [
        el("label", { textContent: tr("Nombre") }), name,
        el("label", { textContent: tr("Opacidad") }), opacity,
        el("div", { className: "row" }, [el("label", { textContent: tr("Modo de fusión:") }), blend]),
        visible,
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? current() : null;
}

// Imagen > Perfil de colores (ColorProfileDialog de Kaint, como Paint.NET 5): el perfil actual y
// otro de la lista o de un archivo .icc para convertir los colores (los píxeles cambian y la
// imagen se ve igual) o asignarlo (solo cambia cómo se interpretan). Devuelve null o
// { result: "convert" | "assign", profile }.
export async function colorProfileDialog(current) {
    const { NamedProfiles, describe, readIcc, sameProfile, SRGB } = await import("../core/ColorProfiles.js");
    current ||= SRGB;
    const dlg = new Dialog(tr("Perfil de colores"), { iconName: "color-profile" });
    const profiles = [];
    const list = el("select", { style: { minWidth: "220px" } });
    const add = (p, selectIt) => {
        let index = profiles.findIndex((q) => sameProfile(q, p));
        if (index < 0) {
            profiles.push(p);
            list.appendChild(el("option", { value: profiles.length - 1, textContent: describe(p) }));
            index = profiles.length - 1;
        }
        if (selectIt)
            list.value = index;
    };
    // Los mismos perfiles que ofrece Paint.NET.
    NamedProfiles.forEach((p) => add(p, false));
    add(current, true);
    const load = el("button", { className: "tbutton", type: "button", title: tr("Cargar un perfil de color (.icc, .icm)") }, [icon("document-open", 16)]);
    const save = el("button", { className: "tbutton", type: "button", title: tr("Guardar el perfil elegido como archivo .icc") }, [icon("document-save", 16)]);
    load.addEventListener("click", () => {
        const input = el("input", { type: "file", accept: ".icc,.icm" });
        input.addEventListener("change", async () => {
            const f = input.files?.[0];
            const p = f ? readIcc(new Uint8Array(await f.arrayBuffer())) : null;
            if (!p) {
                await message(tr("Perfil de colores"), tr("El archivo no contiene un perfil de color RGB válido."));
                return;
            }
            add(p, true);
            update();
        });
        input.click();
    });
    save.addEventListener("click", () => {
        const p = profiles[Number(list.value)];
        const a = el("a", { href: URL.createObjectURL(new Blob([p.icc], { type: "application/vnd.iccprofile" })), download: `${describe(p)}.icc` });
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    });
    dlg.body.appendChild(el("div", { className: "profile-grid" }, [
        el("span", { textContent: tr("Actual:") }), el("b", { textContent: describe(current) }),
        el("span", { textContent: tr("Nuevo:") }), el("span", { className: "row" }, [list, load, save]),
    ]));
    const convert = dlg.addButton(tr("Convertir"), "convert");
    const assign = dlg.addButton(tr("Asignar"), "assign");
    dlg.addButton(tr("Cancelar"), null);
    convert.title = tr("Convierte los colores al perfil nuevo: los píxeles cambian y la imagen se ve igual.");
    assign.title = tr("Asigna el perfil nuevo sin tocar los píxeles: cambia cómo se interpretan sus colores.");
    // Igual que Paint.NET: con el mismo perfil no hay nada que hacer.
    const update = () => (convert.disabled = assign.disabled = sameProfile(profiles[Number(list.value)], current));
    list.addEventListener("change", update);
    update();
    const r = await dlg.exec();
    return r ? { result: r, profile: profiles[Number(list.value)] } : null;
}

// Imagen > Animación (AnimationDialog de Kaint): la duración de cada fotograma de un GIF (cada
// capa visible, la de abajo primero) con una vista previa que se reproduce. Devuelve null o un
// Map índice de capa → nombre nuevo (la duración va en el nombre: "Fotograma 1 (100ms)").
export async function animationDialog(layers, defaultDelay) {
    const dlg = new Dialog(tr("Animación"), { iconName: "media-playback-start" });
    const frames = [];
    const list = el("div", { className: "anim-list" }, [el("div", { className: "anim-head" }, [
        el("span", { textContent: tr("Fotograma") }), el("span", { textContent: tr("Capa") }), el("span", { textContent: tr("Duración (ms)") }),
    ])]);
    const preview = el("canvas", { className: "anim-preview", width: 216, height: 161 });
    let current = 0, timer = 0;
    const showFrame = (i) => {
        if (i < 0 || i >= frames.length)
            return;
        current = i;
        frames.forEach((f, k) => f.row.classList.toggle("current", k === i));
        const layer = frames[i].layer, p = preview.getContext("2d");
        p.clearRect(0, 0, preview.width, preview.height);
        const s = Math.min(preview.width / layer.canvas.width, preview.height / layer.canvas.height);
        const w = layer.canvas.width * s, h = layer.canvas.height * s, x = (preview.width - w) / 2, y = (preview.height - h) / 2;
        p.fillStyle = checkerPattern(p);
        p.fillRect(x, y, w, h);
        p.imageSmoothingQuality = "high";
        p.drawImage(layer.canvas, x, y, w, h);
    };
    layers.forEach((layer, index) => {
        if (!layer.visible)
            return;
        const delay = numberInput(delayFromName(layer.name, defaultDelay), MIN_DELAY, 60000, 10);
        const thumb = thumbnail(layer.canvas, 48, 36);
        const row = el("div", { className: "anim-row" }, [
            el("span", { textContent: String(frames.length + 1) }),
            el("span", { className: "anim-layer" }, [thumb, el("span", { textContent: nameWithDelay(layer.name, 0).replace(/ \(0ms\)$/, "") })]),
            delay,
        ]);
        const frame = frames.length;
        row.addEventListener("pointerdown", () => !timer && showFrame(frame));
        frames.push({ index, layer, delay, row });
        list.appendChild(row);
    });
    const play = el("button", { className: "pushbutton anim-play", type: "button" });
    const setPlay = (on) => {
        clearTimeout(timer);
        timer = 0;
        play.replaceChildren(icon(on ? "media-playback-pause" : "media-playback-start", 16), on ? tr("Pausa") : tr("Reproducir"));
        if (on)
            step();
    };
    const step = () => {
        if (!frames.length)
            return;
        showFrame(current);
        const delay = frames[current].delay.read();
        current = (current + 1) % frames.length;
        timer = setTimeout(step, delay);
    };
    play.addEventListener("click", () => setPlay(!timer));
    // Misma duración para todos.
    const all = numberInput(defaultDelay, MIN_DELAY, 60000, 10);
    const apply = el("button", { className: "pushbutton", type: "button", textContent: tr("Aplicar a todos") });
    apply.addEventListener("click", () => frames.forEach((f) => (f.delay.value = all.read())));
    dlg.body.appendChild(el("div", { className: "anim-dialog" }, [
        el("div", { className: "note", textContent: tr("Al guardar como GIF, cada capa visible es un fotograma (la de abajo es el primero). La duración se guarda en el nombre de la capa.") }),
        el("div", { className: "anim-top" }, [list, el("div", { className: "anim-side" }, [preview, play])]),
        el("div", { className: "row" }, [el("span", { textContent: tr("Duración de todos los fotogramas:") }), all, el("span", { textContent: tr("ms") }), apply]),
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    setPlay(false);
    showFrame(0);
    const r = await dlg.exec();
    clearTimeout(timer);
    if (!r)
        return null;
    const renamed = new Map();
    for (const f of frames) {
        const name = f.layer.name, updated = nameWithDelay(name, f.delay.read());
        if (updated !== name && delayFromName(name, -1) !== f.delay.read())
            renamed.set(f.index, updated);
    }
    return renamed;
}

// "Guardar como" sin el diálogo de archivos del navegador: el nombre y el tipo (el archivo se
// descarga). Devuelve el nombre con su extensión o null.
export async function saveAsDialog(formats, suggested) {
    const dlg = new Dialog(tr("Guardar como"), { iconName: "document-save-as" });
    const ext = (n) => (n.split(".").pop() || "").toLowerCase();
    const base = (n) => n.replace(/\.[^.]+$/, "");
    const find = (n) => formats.find((f) => f.ext === ext(n) || f.aliases?.includes(ext(n))) || formats.find((f) => f.ext === "png");
    const name = el("input", { type: "text", value: suggested, style: { width: "260px" } });
    const type = select(formats.map((f) => [f.ext, `${tr(f.name)} (*.${f.ext})`]), find(suggested).ext);
    // Al cambiar el tipo cambia la extensión, como en el diálogo de Kaint.
    type.addEventListener("change", () => (name.value = `${base(name.value)}.${type.value}`));
    name.addEventListener("input", () => {
        const f = formats.find((x) => x.ext === ext(name.value) || x.aliases?.includes(ext(name.value)));
        if (f)
            type.value = f.ext;
    });
    dlg.body.appendChild(el("div", { className: "form", style: { minWidth: "360px" } }, [
        el("label", { textContent: tr("Nombre:") }), name, el("label", { textContent: tr("Tipo:") }), type,
    ]));
    dlg.addButton(tr("Guardar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    setTimeout(() => name.setSelectionRange(0, base(name.value).length), 0);
    if ((await dlg.exec()) !== "ok")
        return null;
    const chosen = formats.find((f) => f.ext === type.value);
    const n = name.value.trim() || tr("Sin título");
    return chosen.ext === ext(n) || chosen.aliases?.includes(ext(n)) ? n : `${base(n)}.${chosen.ext}`;
}

// Pide un número entero (QInputDialog::getInt): devuelve el número o null si se cancela.
export async function promptNumber(title, label, value, min, max) {
    const dlg = new Dialog(title);
    const input = numberInput(value, min, max);
    dlg.body.appendChild(el("label", { style: { display: "flex", gap: "8px", alignItems: "center" } }, [plain(label), input]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    setTimeout(() => input.select(), 0);
    return (await dlg.exec()) === "ok" ? input.read() : null;
}

// Pide un texto (nombre de una paleta...): devuelve el texto o null si se cancela.
export async function promptText(title, label, value = "") {
    const dlg = new Dialog(title, { iconName: "palette" });
    const input = el("input", { type: "text", value, style: { width: "260px" } });
    dlg.body.appendChild(el("label", { style: { display: "flex", gap: "8px", alignItems: "center" } }, [plain(label), input]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    setTimeout(() => input.select(), 0);
    return (await dlg.exec()) === "ok" ? input.value : null;
}

// --- Configuración -----------------------------------------------------------------------

export async function settingsDialog(app = null, startPage = "interface") {
    const dlg = new Dialog(tr("Configuración"), { iconName: "configure" });
    const sections = el("div", { className: "sections" });
    const page = el("div", { className: "page" });
    const check = (text, key, tip = "") => {
        const c = checkbox(text, Settings.get(key) !== false && Settings.get(key) !== undefined ? !!Settings.get(key) : false);
        c.title = tip;
        c.input.addEventListener("change", () => Settings.set(key, c.input.checked));
        return c;
    };
    const heading = (text) => el("h4", { className: "section", textContent: text });
    const note = (text) => el("small", { textContent: text, style: { opacity: 0.75 } });
    const button = (text, tip, run) => {
        const b = el("button", { className: "pushbutton", textContent: plain(text), title: tip || "" });
        b.addEventListener("click", run);
        return b;
    };
    let show = null;
    const pages = [
        ["interface", "settings-interface", tr("Interfaz de usuario"), async () => {
            const scheme = select([[0, tr("Seguir al sistema")], [1, tr("Claro")], [2, tr("Oscuro")], [6, tr("Azul")], [4, tr("Windows 98")], [5, tr("Personalizado")]],
                                  Settings.get("colorScheme"));
            scheme.addEventListener("change", () => Settings.set("colorScheme", Number(scheme.value)));
            const cursors = select([[0, "Kaint"], [1, "Paint.NET"]], Settings.get("cursorStyle"));
            cursors.title = tr("Los de Paint.NET, en pixel art: la cruz con el dibujo de la herramienta.");
            cursors.addEventListener("change", () => Settings.set("cursorStyle", Number(cursors.value)));
            const look = select([[0, "Kaint"], [1, "Paint.NET"]], Settings.get("uiStyle"));
            look.title = tr("Como Paint.NET 5: más compacto, con las ventanas sin borde y los botones y menús más juntos.");
            look.addEventListener("change", () => {
                Settings.set("uiStyle", Number(look.value));
                paintNetCompat();
            });
            const customize = button(tr("Personalizar…"), tr("Editar el tema personalizado."), async () => {
                Settings.set("colorScheme", 5);
                scheme.value = "5";
                await customThemeDialog();
            });
            const language = select([["en", "English"], ["es", "Español"]], Settings.get("language"));
            const reload = el("button", { className: "pushbutton", textContent: tr("Recargar"), style: { visibility: "hidden" } });
            reload.addEventListener("click", () => location.reload());
            language.addEventListener("change", () => {
                Settings.set("language", language.value);
                reload.style.visibility = "visible";
            });
            const packs = await IconPacks.list();
            const pack = select(packs.map((p) => [p.id, p.name]), IconPacks.activeId());
            pack.addEventListener("change", () => IconPacks.activate(pack.value));
            const panelStyle = select([[0, tr("Windows 10")], [1, tr("Compacto")]], Settings.get("panelStyle"));
            // Con el estilo de Paint.NET las ventanas no tienen borde: estilo y borde se ven desactivados.
            const paintNetCompat = () => {
                const off = Settings.get("uiStyle") === 1;
                for (const field of [panelStyle, border]) {
                    field.disabled = off;
                    field.title = off ? tr("No se usa con el estilo de interfaz Paint.NET: sus ventanas no tienen borde.") : "";
                    field.closest("label")?.classList.toggle("disabled", off);
                }
            };
            panelStyle.addEventListener("change", () => Settings.set("panelStyle", Number(panelStyle.value)));
            const border = numberInput(Settings.get("panelBorder"), 1, 12);
            border.style.width = "60px";
            border.title = tr("Grosor del borde de las ventanas Herramientas, Historial, Capas y Colores.");
            border.addEventListener("input", () => Settings.set("panelBorder", border.read()));
            const autosave = numberInput(Settings.get("autosaveMinutes"), 0, 120);
            autosave.style.width = "60px";
            autosave.addEventListener("input", () => Settings.set("autosaveMinutes", autosave.read()));
            queueMicrotask(paintNetCompat);
            return [
                check(tr("Animaciones"), "animations",
                      tr("Menús y ventanas que aparecen con un fundido, capas que se deslizan al reordenarlas y otras transiciones.")),
                check(tr("Ventanas translúcidas"), "translucentPanels",
                      tr("Las ventanas flotantes se vuelven translúcidas cuando el puntero no está encima.")),
                check(tr("Desplazamiento más allá del borde de la imagen (overscroll)"), "overscroll",
                      tr("Permite ver un margen alrededor de la imagen; así el zum hacia el cursor funciona también en las esquinas.")),
                check(tr("Desplazamiento automático al dibujar en el borde de la ventana"), "autoScroll"),
                check(tr("Seleccionar automáticamente la capa visible más cercana tras ocultar una capa"), "autoSelectVisibleLayer"),
                check(tr("Iconos de herramientas con los colores activos"), "coloredToolIcons",
                      tr("El cubo de pintura, el pincel, cambiar color y el degradado muestran el color principal y el secundario.")),
                check(tr("Subrayar las letras de acceso de los menús"), "menuMnemonics",
                      tr("Las letras que abren cada menú y opción con Alt. Desactivado, como en Windows, solo se subrayan al pulsar Alt.")),
                check(tr("Usar el puntero del sistema en los cursores con la herramienta en miniatura"), "systemPointer",
                      tr("La selección, el degradado, la línea, el texto y las formas muestran la flecha (o la I) del tema de cursores del escritorio con su icono al lado. Desactivado, la flecha de Kaint.")),
                check(tr("Al arrastrar una capa, marcar con una línea dónde quedará"), "layerDropLine",
                      tr("Desactivado, la capa se mueve en la lista con el ratón mientras se arrastra.")),
                check(tr("Mostrar los botones de ir al principio y al final del historial"), "historyJumpButtons",
                      tr("En la ventana Historial, junto a deshacer y rehacer: vuelven a la imagen original o al último cambio.")),
                check(tr("Mostrar los colores semitransparentes de la paleta en la vista ampliada de Colores"), "paletteTranslucent",
                      tr("Las filas de abajo de la paleta de fábrica, como en Paint.NET. El blanco con que se rellenan las paletas de menos colores no se muestra nunca.")),
                el("label", {}, [tr("Tema:"), scheme, customize]),
                el("label", {}, [tr("Estilo de la interfaz:"), look]),
                el("label", {}, [tr("Cursores:"), cursors]),
                el("label", {}, [tr("Idioma:"), language, reload]),
                note(tr("Los cambios de idioma se aplican al recargar la página.")),
                el("label", {}, [tr("Paquete de iconos:"), pack, button(tr("Administrar…"), tr("Añadir o quitar paquetes de iconos."), () => show("iconpacks"))]),
                heading(tr("Ventanas")),
                el("label", {}, [tr("Estilo de las ventanas:"), panelStyle]),
                el("label", {}, [tr("Grosor del borde de las ventanas:"), border, tr("px")]),
                heading(tr("Al iniciar")),
                check(tr("Volver a abrir las imágenes de la última sesión"), "reopenLast",
                      window.showOpenFilePicker ? "" : tr("Este navegador no deja volver a abrir los archivos: solo funciona en los que tienen acceso a los archivos del sistema.")),
                heading(tr("Al cerrar")),
                check(tr("Preguntar una sola vez por todas las imágenes sin guardar"), "askOnceOnClose",
                      tr("Como en Paint.NET: «No guardar» descarta todas a la vez. Desactivado, se pregunta imagen por imagen.")),
                // Copias de recuperación por si la página se cierra de golpe.
                heading(tr("Recuperación")),
                el("label", {}, [tr("Guardar una copia de recuperación de las imágenes sin guardar cada:"), autosave, tr("min"), note(tr("(0: nunca)"))]),
            ];
        }],
        ["canvas", "settings-canvas", tr("Lienzo"), () => {
            // Fondo alrededor de la imagen, uno para los temas claros y otro para los oscuros.
            const borderRow = (text, key, fallback) => {
                const c = checkbox(text, !!Settings.get(key));
                const color = el("input", { type: "color", value: Settings.get(key) || fallback });
                color.disabled = !c.input.checked;
                const apply = () => {
                    color.disabled = !c.input.checked;
                    Settings.set(key, c.input.checked ? color.value : null);
                };
                c.input.addEventListener("change", apply);
                color.addEventListener("input", apply);
                return el("div", { className: "row-buttons" }, [c, color]);
            };
            // Brillo del tablero de transparencia de 0,25 a 1, como en Kaint (191 / 255 por defecto).
            const value = Math.max(0.25, Math.min(1, (Settings.get("checkerBrightness") ?? 191) / 255));
            const brightness = slider(Number(value.toFixed(2)), 0.25, 1, 0.01, (v) => Settings.set("checkerBrightness", Math.round(v * 255)));
            const reset = el("button", { className: "tbutton", title: tr("Restablecer") }, [icon("edit-reset", 16)]);
            reset.addEventListener("click", () => {
                brightness.set(0.75);
                Settings.set("checkerBrightness", 191);
            });
            const ruler = select([[0, tr("Según el tema")], [1, tr("Del color de la barra de herramientas")], [2, tr("Oscura")]], Settings.get("rulerStyle"));
            ruler.addEventListener("change", () => Settings.set("rulerStyle", Number(ruler.value)));
            return [check(tr("Proyectar una sombra bajo el lienzo"), "dropShadow"),
                    borderRow(tr("Usar un color personalizado para el fondo del lienzo con los temas claros"), "canvasBorderLight", "#cfcfcf"),
                    borderRow(tr("Usar un color personalizado para el fondo del lienzo con los temas oscuros"), "canvasBorderDark", "#303030"),
                    el("label", {}, [tr("Brillo de tablero de ajedrez de transparencia")]),
                    el("div", { className: "row-buttons" }, [brightness, reset]),
                    el("label", {}, [tr("Reglas:"), ruler])];
        }],
        ["tools", "settings-tools", tr("Herramientas"), () => {
            const tools = app ? app.tools.tools : [];
            const start = select([["", tr("La última que se usó")], ...tools.map((t) => [t.id, t.name])], Settings.get("startTool") || "");
            start.addEventListener("change", () => Settings.set("startTool", start.value));
            // Opciones predeterminadas de la barra de herramientas (con ellas empieza cada sesión).
            const defaults = new LocalToolSettings(toolDefaults());
            defaults.addEventListener("tools", () => setToolDefaults(defaults.tools));
            const load = button(tr("Cargar desde barra de htas."), tr("Usa como predeterminadas las opciones que tiene ahora la barra de herramientas."),
                                () => defaults.set(AppState.tools));
            const reset = button(tr("Restablecer"), tr("Vuelve a los valores de fábrica de todas las herramientas."), () => defaults.set(defaultTools()));
            return [
                el("p", { textContent: tr("Puede elegir la herramienta que está activa cuando inicie Kaint, y qué configuración predeterminada se debe aplicar a los elementos de la barra de herramientas.") }),
                el("div", { className: "row-buttons" }, [load, reset]),
                el("label", {}, [tr("Herramienta predeterminada:"), start]),
                heading(tr("Mover y pegar")),
                check(tr("Ocultar el contorno y los tiradores mientras se arrastra"), "hideMoveBox",
                      tr("Con Mover píxeles seleccionados solo queda el icono de mover; con Mover selección desaparecen los tiradores.")),
                check(tr("Los píxeles pegados o movidos reemplazan a los de debajo, también los transparentes"), "replacePixels",
                      tr("Como en Paint.NET: pegar una imagen con transparencia sobre otra en la misma capa deja ese rectángulo transparente. Desactivado, se mezclan con lo que hay debajo.")),
                ...(app ? [new ToolOptionsPanel(app, defaults).element] : []),
            ];
        }],
        ["tablet", "settings-tablet", tr("Lápiz y tableta"), () => [
            check(tr("La presión del lápiz de la tableta controla el tamaño del pincel"), "penPressure"),
            check(tr("Usar la Goma con el extremo borrador del lápiz"), "tabletEraser"),
            note(tr("Al dar la vuelta al lápiz se activa la herramienta Goma de borrar, y al volver a la punta, la herramienta de antes.")),
            note("PointerEvent" in window ? tr("El navegador admite lápices y tabletas.") : tr("No se detectó ningún lápiz ni tableta de dibujo.")),
        ]],
        ["performance", "settings-performance", tr("Rendimiento"), () => {
            const limit = numberInput(Settings.get("historyLimitMB"), 0, 1024 * 1024, 256);
            limit.style.width = "90px";
            limit.addEventListener("input", () => Settings.set("historyLimitMB", limit.read()));
            return [el("p", { textContent: tr("Kaint dibuja y aplica los efectos con el procesador, repartiendo el trabajo entre %1 hilos.", MAX_WORKERS) }),
                    heading(tr("Historial")),
                    el("label", {}, [tr("Memoria máxima del historial de cada imagen:"), limit, tr("MB"), note(tr("(0: sin límite)"))]),
                    note(tr("Si el historial de una imagen ocupa más, se descartan los pasos más antiguos (y se avisa en la barra de estado)."))];
        }],
        ["plugins", "settings-plugins", tr("Complementos"), () => pluginsPage(() => show("plugins"))],
        ["updates", "settings-updates", tr("Actualizaciones"), () => {
            const status = el("p", { textContent: "" });
            const now = button(tr("Buscar ahora"), "", async () => {
                status.textContent = tr("Buscando…");
                try {
                    const r = await fetch("https://raw.githubusercontent.com/ssserch/Kaint/main/version.json", { cache: "no-store" });
                    const latest = (await r.json()).version;
                    status.textContent = latest && latest !== VERSION ? tr("Hay una versión nueva: %1. Recarga la página para usarla.", latest)
                        : tr("Kaint está al día.");
                } catch {
                    status.textContent = tr("No se pudo comprobar. Inténtalo más tarde.");
                }
            });
            return [el("p", { textContent: tr("Versión instalada: %1", VERSION) }),
                    el("p", { textContent: tr("La versión web se actualiza sola: al recargar la página se usa la última publicada.") }),
                    el("div", { className: "row-buttons" }, [now]), status];
        }],
        ["diagnostics", "settings-diagnostics", tr("Diagnósticos"), () => {
            const rows = [
                ["Kaint", `${tr("Versión web de Kaint")} ${VERSION}`], [tr("Navegador"), navigator.userAgent],
                [tr("Plataforma"), navigator.platform || "?"], [tr("Idioma"), navigator.language || "?"],
                [tr("Procesadores lógicos"), String(navigator.hardwareConcurrency || "?")],
                [tr("Memoria"), navigator.deviceMemory ? `${navigator.deviceMemory} GB` : "?"],
                [tr("Pantalla"), `${screen.width} × ${screen.height} · ${window.devicePixelRatio || 1}×`],
                [tr("Lápiz y tableta"), "PointerEvent" in window ? tr("Sí") : tr("No")],
                [tr("Archivos del sistema"), window.showOpenFilePicker ? tr("Sí") : tr("No")],
            ];
            const copy = button(tr("Copiar en el portapapeles"), "", () => navigator.clipboard?.writeText(rows.map((r) => r.join(": ")).join("\n")));
            return [el("div", { className: "diagnostics" }, rows.flatMap(([k, v]) => [el("span", { textContent: k }), el("span", { textContent: v })])),
                    el("div", { className: "row-buttons" }, [copy])];
        }],
        ["iconpacks", "document-open", tr("Paquetes de iconos"), () => iconPacksPage(() => show("iconpacks"))],
        ["fonts", "text", tr("Fuentes"), () => fontsPage(() => show("fonts"))],
        ["shapes", "shapes", tr("Formas"), () => shapesPage(() => show("shapes"))],
        ["keyboard", "configure", tr("Atajos de teclado"), () => shortcutsPage(app)],
    ];
    show = async (id) => {
        const i = Math.max(0, typeof id === "number" ? id : pages.findIndex((p) => p[0] === id));
        [...sections.children].forEach((row, k) => row.classList.toggle("selected", k === i));
        page.replaceChildren(...(await pages[i][3]()));
    };
    pages.forEach(([id, iconName, name]) => {
        const row = el("div", { className: "row" }, [icon(iconName, 24), name]);
        row.addEventListener("click", () => show(id));
        sections.appendChild(row);
    });
    dlg.body.appendChild(el("div", { className: "settings" }, [sections, page]));
    show(startPage);
    dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
    return dlg.exec();
}

// Formas de Paint.NET (.xaml) para la herramienta Formas (SettingsDialog::createShapesPage).
function shapesPage(refresh) {
    const list = el("div", { className: "list shape-list" });
    const selected = new Set();
    for (const s of CustomShapes.all()) {
        const row = el("div", { className: "row", dataset: { id: s.id } }, [icon(`opt-shape:custom:${s.id}`, 24), el("span", { textContent: s.name }),
                                                                            el("small", { textContent: s.file, style: { opacity: 0.7, marginLeft: "auto" } })]);
        row.addEventListener("click", (e) => {
            if (!e.ctrlKey && !e.shiftKey)
                selected.clear();
            selected.has(s.id) ? selected.delete(s.id) : selected.add(s.id);
            for (const r of list.children)
                r.classList.toggle("selected", selected.has(r.dataset.id));
            remove.disabled = !selected.size;
        });
        list.appendChild(row);
    }
    if (!list.children.length)
        list.appendChild(el("p", { textContent: tr("No hay formas personalizadas."), style: { opacity: 0.7, margin: "6px" } }));
    const status = el("p", { style: { whiteSpace: "pre-line" } });
    const file = el("input", { type: "file", accept: ".xaml", multiple: true, hidden: true });
    file.addEventListener("change", async () => {
        const errors = await CustomShapes.add([...file.files]);
        refresh();
        if (errors.length)
            await message(tr("Añadir formas"), tr("No se pudieron añadir:\n\n%1", errors.join("\n")));
    });
    const add = el("button", { className: "pushbutton", textContent: plain(tr("Añadir formas…")) });
    add.addEventListener("click", () => file.click());
    const remove = el("button", { className: "pushbutton", textContent: tr("Quitar"), disabled: true });
    remove.addEventListener("click", () => {
        CustomShapes.remove([...selected]);
        refresh();
    });
    return [
        el("p", { textContent: tr("Formas de Paint.NET (.xaml) para la herramienta Formas: aparecen en el grupo «Formas personalizadas». Se guardan en este navegador.") }),
        list, el("div", { className: "row-buttons" }, [add, remove, file]), status,
    ];
}

// Tema personalizado: parte de claro, oscuro o Windows 98 y cambia los colores que se quiera.
async function customThemeDialog() {
    const dlg = new Dialog(tr("Tema personalizado"), { iconName: "configure" });
    const current = Settings.get("customTheme") || { base: "dark", colors: {} };
    let theme = { base: current.base || "dark", colors: { ...(current.colors || {}) } };
    const save = () => Settings.set("customTheme", { base: theme.base, colors: { ...theme.colors } });
    const base = select([["light", tr("Claro")], ["dark", tr("Oscuro")], ["win98", tr("Windows 98")]], theme.base);
    const grid = el("div", { className: "theme-colors" });
    const fill = () => {
        grid.replaceChildren();
        for (const [key, , label] of ColorRoles) {
            const input = el("input", { type: "color", value: theme.colors[key] || Presets[theme.base][key] });
            input.addEventListener("input", () => {
                theme.colors[key] = input.value;
                save();
            });
            grid.append(el("span", { textContent: tr(label) }), input);
        }
    };
    base.addEventListener("change", () => {
        theme = { base: base.value, colors: {} };
        save();
        fill();
    });
    const reset = el("button", { className: "pushbutton", textContent: tr("Restablecer") });
    reset.addEventListener("click", () => {
        theme.colors = {};
        save();
        fill();
    });
    fill();
    dlg.body.append(el("label", { style: { display: "flex", gap: "6px", alignItems: "center", marginBottom: "8px" } }, [tr("Basado en:"), base, reset]), grid);
    dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
    save();
    return dlg.exec();
}

// Complementos (.paintjs): activar, instalar desde un archivo o desde el catálogo, y quitar.
function pluginsPage(refresh) {
    const list = el("div", { className: "list plugin-list" });
    let selected = Plugins.records[0]?.id || null;
    const details = el("p", { className: "plugin-details" });
    const sync = () => {
        for (const row of list.children)
            row.classList.toggle("selected", row.dataset.id === selected);
        const r = Plugins.records.find((x) => x.id === selected);
        const error = Plugins.errors.find((e) => e.id === selected);
        details.textContent = r ? [r.description, `${tr("Versión")}: ${r.version}`, `${tr("Autor")}: ${r.author}`,
                                   tr("%1 efectos · %2 formatos", r.effectCount, r.fileTypeCount), error ? `${tr("Problemas:")} ${error.message}` : ""]
            .filter(Boolean).join("\n") : tr("No hay complementos instalados.");
        remove.disabled = !r;
    };
    for (const r of Plugins.records) {
        const enabled = el("input", { type: "checkbox", checked: r.enabled, title: tr("Activado") });
        enabled.addEventListener("change", async () => {
            try {
                await Plugins.setEnabled(r.id, enabled.checked);
            } catch (e) {
                enabled.checked = !enabled.checked;
                status.textContent = e.message;
            }
        });
        const row = el("div", { className: "row", dataset: { id: r.id } }, [enabled, icon("plugins", 16), el("span", { textContent: `${r.name} ${r.version}` })]);
        row.addEventListener("click", (e) => {
            if (e.target === enabled)
                return;
            selected = r.id;
            sync();
        });
        list.appendChild(row);
    }
    const status = el("p", { className: "plugin-status", style: { minHeight: "1.2em" } });
    const file = el("input", { type: "file", accept: ".paintjs", hidden: true });
    file.addEventListener("change", async () => {
        const f = file.files?.[0];
        if (!f)
            return;
        try {
            const r = await Plugins.install(f);
            selected = r.id;
            refresh();
        } catch (e) {
            status.textContent = e.message;
        }
    });
    const install = el("button", { className: "pushbutton", textContent: plain(tr("Instalar complemento…")), title: tr("Instalar un complemento (.paintjs)") });
    install.addEventListener("click", () => file.click());
    const browse = el("button", { className: "pushbutton", textContent: plain(tr("Explorar complementos…")), title: tr("El catálogo de complementos") });
    browse.addEventListener("click", async () => {
        await pluginCatalogDialog();
        refresh();
    });
    const remove = el("button", { className: "pushbutton", textContent: tr("Quitar"), title: tr("Desinstalar el complemento elegido") });
    remove.addEventListener("click", async () => {
        if (!selected || !confirm(tr("¿Quieres quitar este complemento?")))
            return;
        await Plugins.remove(selected);
        refresh();
    });
    sync();
    return [
        el("p", { textContent: tr("Complementos de efectos y formatos de archivo (.paintjs). Se guardan en este navegador. Tienen el mismo acceso que Kaint: instala solo los que sean de confianza.") }),
        list, details, el("div", { className: "row-buttons" }, [install, browse, remove, file]), status,
    ];
}

// El catálogo de complementos: cada uno con su botón de instalar.
async function pluginCatalogDialog() {
    const dlg = new Dialog(tr("Explorar complementos"), { iconName: "plugins" });
    const list = el("div", { className: "list catalog-list" }, [el("p", { textContent: tr("Cargando el catálogo…") })]);
    dlg.body.appendChild(list);
    dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
    const pending = dlg.exec();
    try {
        const entries = await Plugins.catalog();
        list.replaceChildren();
        for (const entry of entries) {
            const installed = Plugins.records.find((r) => r.id === entry.id);
            const b = el("button", { className: "pushbutton", textContent: installed?.version === entry.version ? tr("Instalado") : tr("Instalar") });
            b.disabled = installed?.version === entry.version;
            const status = el("small");
            b.addEventListener("click", async () => {
                b.disabled = true;
                try {
                    await Plugins.installFromCatalog(entry);
                    b.textContent = tr("Instalado");
                } catch (e) {
                    status.textContent = e.message;
                    b.disabled = false;
                }
            });
            list.appendChild(el("div", { className: "catalog-row" }, [
                el("div", {}, [el("b", { textContent: `${entry.name} ${entry.version}` }), el("div", { textContent: `${entry.author} · ${entry.license}` }),
                               el("div", { textContent: entry.description, style: { opacity: 0.8 } }), status]), b]));
        }
        if (!entries.length)
            list.replaceChildren(el("p", { textContent: tr("El catálogo está vacío.") }));
    } catch (e) {
        list.replaceChildren(el("p", { textContent: tr("No se pudo cargar el catálogo: %1", e.message) }));
    }
    return pending;
}

// Fuentes para la herramienta Texto, guardadas en el navegador.
function fontsPage(refresh) {
    const list = el("div", { className: "list font-list" });
    let selected = null;
    for (const f of UserFonts.loaded) {
        const row = el("div", { className: "row", dataset: { file: f.file }, style: { fontFamily: `"${f.family}"` } },
                       [el("span", { textContent: `${f.family} (${f.file})` })]);
        row.addEventListener("click", () => {
            selected = f.file;
            for (const r of list.children) r.classList.toggle("selected", r.dataset.file === selected);
        });
        list.appendChild(row);
    }
    if (!UserFonts.loaded.length)
        list.appendChild(el("p", { textContent: tr("Todavía no has añadido ninguna fuente."), style: { margin: "4px" } }));
    const status = el("p", { style: { minHeight: "1.2em" } });
    const file = el("input", { type: "file", accept: ".ttf,.otf,.woff,.woff2", multiple: true, hidden: true });
    file.addEventListener("change", async () => {
        const errors = await UserFonts.add([...file.files]);
        if (errors.length)
            alert(tr("No se pudieron añadir:\n\n%1", errors.join("\n")));
        refresh();
    });
    const add = el("button", { className: "pushbutton", textContent: plain(tr("Añadir fuentes…")) });
    add.addEventListener("click", () => file.click());
    const remove = el("button", { className: "pushbutton", textContent: tr("Quitar") });
    remove.addEventListener("click", async () => {
        if (selected) {
            await UserFonts.remove(selected);
            refresh();
        }
    });
    const clear = el("button", { className: "pushbutton", textContent: tr("Quitar todas") });
    clear.addEventListener("click", async () => {
        if (UserFonts.loaded.length && confirm(tr("¿Quieres quitar todas las fuentes añadidas?"))) {
            await UserFonts.removeAll();
            refresh();
        }
    });
    return [el("p", { textContent: tr("Añade fuentes para la herramienta Texto (.ttf, .otf, .woff). Se guardan en este navegador y solo las usa Kaint.") }),
            list, el("div", { className: "row-buttons" }, [add, remove, clear, file]), status];
}

// Vista previa de un paquete: algunos de sus iconos de los menús y de las herramientas (los de
// Kaint si no los trae).
function packPreview(pack) {
    const files = ["menu_file_new_icon", "menu_file_open_icon", "menu_file_save_icon", "menu_edit_cut_icon", "menu_edit_copy_icon",
                   "menu_edit_paste_icon", "menu_edit_undo_icon", "menu_layers_add_new_layer_icon", "menu_image_crop_icon",
                   "menu_utilities_settings_icon"];
    const strip = el("span", { className: "pack-preview" });
    for (const f of files) {
        const own = pack.id !== DEFAULT_PACK ? pack.urls?.[`icons/${f}`] : null;
        strip.appendChild(el("img", { src: own || `assets/icons/${f}.png`, width: 18, height: 18, alt: "" }));
    }
    for (const tool of ["paintbrush", "zoom"])
        strip.appendChild(icon(tool, 18));
    return strip;
}

// Paquetes de iconos (.zip con asset-pack.json): instalar, usar y quitar.
async function iconPacksPage(refresh) {
    const packs = await IconPacks.list();
    const active = IconPacks.activeId();
    const list = el("div", { className: "list pack-list" });
    let selected = active;
    const details = el("p", { className: "pack-details" });
    const use = el("button", { className: "pushbutton", textContent: tr("Usar"), title: tr("Usar el paquete elegido") });
    const add = el("button", { className: "pushbutton", textContent: plain(tr("Añadir…")), title: tr("Instalar un paquete de iconos (.zip)") });
    const remove = el("button", { className: "pushbutton", textContent: tr("Quitar"), title: tr("Desinstalar el paquete elegido") });
    const file = el("input", { type: "file", accept: ".zip,application/zip", hidden: true });
    const status = el("p", { className: "pack-status", style: { minHeight: "1.2em" } });
    const sync = () => {
        for (const row of list.children)
            row.classList.toggle("selected", row.dataset.id === selected);
        const p = packs.find((x) => x.id === selected);
        details.textContent = p ? [p.description, p.id !== DEFAULT_PACK ? tr("Imágenes: %1", p.images) : ""].filter(Boolean).join(" · ") : "";
        use.disabled = selected === active;
        remove.disabled = selected === DEFAULT_PACK;
    };
    for (const p of packs) {
        const extra = [p.author, p.version].filter(Boolean).join(", ");
        const text = `${p.name}${extra ? ` (${extra})` : ""}${p.id === active ? ` · ${tr("en uso")}` : ""}`;
        const row = el("div", { className: "row pack-row", dataset: { id: p.id }, title: p.description }, [packPreview(p), el("span", { textContent: text })]);
        if (p.id === active)
            row.style.fontWeight = "bold";
        row.addEventListener("click", () => {
            selected = p.id;
            sync();
        });
        row.addEventListener("dblclick", () => use.click());
        list.appendChild(row);
    }
    use.addEventListener("click", async () => {
        await IconPacks.activate(selected);
        refresh();
    });
    remove.addEventListener("click", async () => {
        if (selected === DEFAULT_PACK || !confirm(tr("¿Quieres quitar este paquete de iconos?")))
            return;
        await IconPacks.remove(selected);
        refresh();
    });
    add.addEventListener("click", () => file.click());
    file.addEventListener("change", async () => {
        const f = file.files?.[0];
        if (!f)
            return;
        try {
            const { id, images } = await IconPacks.install(f);
            // Como en paint.js, el paquete instalado pasa a usarse.
            await IconPacks.activate(id);
            await refresh();
            if (!images)
                alert(tr("El paquete no trae ninguna imagen que Kaint pueda usar."));
        } catch (error) {
            status.textContent = error.message;
        }
    });
    sync();
    return [
        el("p", { textContent: tr("Los paquetes de iconos (archivos .zip con asset-pack.json) cambian los iconos de Kaint, también los de las herramientas. Los iconos que no traiga un paquete siguen siendo los de Kaint. Se guardan en este equipo.") }),
        list, details, el("div", { className: "row-buttons" }, [use, add, remove, file]), status,
    ];
}

// Atajos de teclado editables: pulsa en un atajo y escribe la combinación nueva.
function shortcutsPage(app) {
    const actions = app ? [...app.actions.values()].filter((a) => a.text && a.id) : [];
    const collator = new Intl.Collator();
    const nameOf = (a) => plain(a.text).replace(/(\.\.\.|…)$/, "").trim();
    actions.sort((a, b) => collator.compare(nameOf(a), nameOf(b)));
    const search = el("input", { type: "search", placeholder: tr("Buscar acciones") });
    const table = el("div", { className: "list shortcut-list" });
    const status = el("p", { style: { minHeight: "1.2em" } });
    const rows = [];
    const describe = (keys) => (keys ? shortcutLabel(keys) : tr("Sin atajo"));
    for (const action of actions) {
        const field = el("input", { type: "text", readOnly: true, className: "shortcut-field", value: describe(action.shortcut) });
        const reset = el("button", { className: "tbutton", title: tr("Restablecer") }, [icon("edit-reset", 16)]);
        const clear = el("button", { className: "tbutton", title: tr("Quitar el atajo"), textContent: "×" });
        const row = el("div", { className: "row shortcut-row" }, [icon(action.icon || "configure", 16), el("span", { className: "name", textContent: nameOf(action) }), field, clear, reset]);
        const sync = () => {
            field.value = describe(action.shortcut);
            reset.disabled = action.shortcuts.length === action.defaultShortcuts.length
                && action.shortcuts.every((k, i) => k === action.defaultShortcuts[i]);
        };
        const assign = (keys) => {
            const taken = app.setShortcut(action.id, keys);
            for (const r of rows)
                r.sync();
            status.textContent = taken ? tr("Se quitó el atajo de «%1».", nameOf(taken)) : "";
        };
        field.addEventListener("focus", () => (field.value = tr("Pulsa un atajo…")));
        field.addEventListener("blur", sync);
        field.addEventListener("keydown", (e) => {
            e.preventDefault();
            e.stopPropagation();
            if (["Control", "Shift", "Alt", "Meta"].includes(e.key))
                return;
            if (e.key === "Escape") {
                field.blur();
                return;
            }
            assign(app.comboFromEvent(e));
            field.blur();
        });
        clear.addEventListener("click", () => assign(""));
        reset.addEventListener("click", () => assign(action.defaultShortcut));
        row.sync = sync;
        sync();
        rows.push(row);
        table.appendChild(row);
    }
    search.addEventListener("input", () => {
        const q = search.value.trim().toLowerCase();
        for (const row of rows)
            row.hidden = !!q && !row.textContent.toLowerCase().includes(q) && !row.querySelector("input").value.toLowerCase().includes(q);
    });
    const resetAll = el("button", { className: "pushbutton", textContent: tr("Restablecer todos"), title: tr("Volver a los atajos de fábrica de todas las acciones") });
    resetAll.addEventListener("click", () => {
        app.resetShortcuts();
        for (const row of rows)
            row.sync();
        status.textContent = tr("Se restablecieron todos los atajos.");
    });
    return [
        el("p", { textContent: tr("Haz clic en un atajo y pulsa la combinación de teclas. Con la «x» se queda sin atajo; si otra acción lo usaba, se le quita.") }),
        search, table, el("div", { className: "row-buttons" }, [status, resetAll]),
    ];
}

// Logo de Kaint como el de Acerca de del programa: el icono y "kaint.html" con la letra fina del
// logo de Paint.NET; se aclara desde muy desenfocado al abrirse.
export function kaintLogo() {
    return el("div", { className: "kaint-logo" }, [el("img", { src: "assets/kaint-64.png", alt: "" }), el("span", { textContent: "kaint.html" })]);
}

// Texto con una ola suave en bucle (WaveLabel de Kaint): cada letra sube y baja un poco, desfasada
// de la anterior; la segunda parte es un enlace. Sin animaciones, quieto.
export function waveLabel(text, link, url) {
    const box = el("span", { className: "wave" });
    let i = 0;
    const letter = (ch) => {
        const span = el("span", { textContent: ch });
        span.style.setProperty("--i", i++);
        return span;
    };
    for (const ch of text)
        box.appendChild(letter(ch));
    const a = el("a", { href: url, target: "_blank", rel: "noopener", title: url });
    for (const ch of link)
        a.appendChild(letter(ch));
    box.appendChild(a);
    return box;
}

// Bibliotecas y contribuciones de la versión web (no usa ninguna biblioteca: todo es JavaScript).
const credits = [
    ["Kaint", "SSSerch", "https://github.com/ssserch/Kaint"],
    ["Paint.NET", "dotPDN LLC, Rick Brewster", "https://www.getpaint.net/"],
    ["Paint.NET API", "dotPDN LLC", "https://paintdotnet.github.io/apidocs/"],
    ["Breeze Icons", "KDE", "https://invent.kde.org/frameworks/breeze-icons"],
];

// Como el Acerca de de Kaint: el logo, la versión, los derechos, el enlace al repositorio, la firma
// con la ola, la traducción y las contribuciones.
export async function aboutDialog(version) {
    const dlg = new Dialog(tr("Acerca de Kaint"), { iconName: "help-about" });
    const list = el("div", { className: "credits" }, credits.map(([name, author, url]) =>
        el("div", {}, [`• ${name} – ${author} `, el("a", { href: url, target: "_blank", rel: "noopener", textContent: url })])));
    dlg.body.appendChild(el("div", { className: "about" }, [
        kaintLogo(),
        el("div", { textContent: `Kaint v${version}`, style: { marginTop: "4px" } }),
        el("div", {}, [tr("Copyright © 2026 SSSerch (aka Y2Kerchy) y colaboradores."), el("br"), tr("Software libre con licencia GPL-3.0.")]),
        el("a", { href: REPOSITORY, target: "_blank", rel: "noopener", textContent: REPOSITORY }),
        el("div", {}, [waveLabel("By ", "Y2Kerchy", "https://ssserch.com")]),
        el("div", { textContent: tr("Traducido al español por SSSerch"), style: { marginTop: "6px" } }),
        el("div", { textContent: tr("Bibliotecas de software y contribuciones:"), style: { marginTop: "6px" } }),
        list,
        el("small", { textContent: "Paint.NET is a trademark of dotPDN LLC.", style: { opacity: 0.7 } }),
    ]));
    dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
    return dlg.exec();
}

// Bienvenida de la primera vez (como la de Kaint): el logo, la versión y el autor (con un botón
// discreto para donar) y lo básico de la configuración: tema, estilo de la interfaz e idioma. El
// tema y el estilo se ven al momento; el idioma, al recargar la página.
export async function welcomeDialog() {
    const dlg = new Dialog(tr("Te damos la bienvenida a Kaint"));
    const startLanguage = Settings.get("language");
    const scheme = select([[0, tr("Seguir al sistema")], [1, tr("Claro")], [2, tr("Oscuro")], [6, tr("Azul")], [4, tr("Windows 98")], [5, tr("Personalizado")]],
                          Settings.get("colorScheme"));
    scheme.addEventListener("change", () => Settings.set("colorScheme", Number(scheme.value)));
    const look = select([[0, "Kaint"], [1, "Paint.NET"]], Settings.get("uiStyle"));
    look.title = tr("Como Paint.NET 5: más compacto, con las ventanas sin borde y los botones y menús más juntos.");
    look.addEventListener("change", () => Settings.set("uiStyle", Number(look.value)));
    const cursors = select([[0, "Kaint"], [1, "Paint.NET"]], Settings.get("cursorStyle"));
    cursors.title = tr("Los de Paint.NET, en pixel art: la cruz con el dibujo de la herramienta.");
    cursors.addEventListener("change", () => Settings.set("cursorStyle", Number(cursors.value)));
    const language = select([["en", "English"], ["es", "Español"]], startLanguage);
    const note = el("small", { textContent: tr("El idioma se aplica al recargar la página."), style: { opacity: 0.7, display: "none" } });
    language.addEventListener("change", () => {
        Settings.set("language", language.value);
        note.style.display = language.value !== startLanguage ? "" : "none";
    });
    const donate = el("a", { className: "donate", href: "https://ko-fi.com/opposition", target: "_blank", rel: "noopener",
                             textContent: tr("♥ Donar"), title: tr("Apoyar el desarrollo de Kaint en Ko-fi") });
    dlg.body.appendChild(el("div", { className: "about welcome" }, [
        kaintLogo(),
        el("div", { textContent: `Kaint v${VERSION}`, style: { marginTop: "4px" } }),
        el("div", { className: "author" }, [waveLabel(tr("Por "), "SSSerch", "https://ssserch.com"), donate]),
        el("hr"),
        el("div", { textContent: tr("Elige cómo quieres que se vea Kaint. Todo se puede cambiar más tarde en Configuración.") }),
        el("div", { className: "form" }, [
            el("span", { textContent: tr("Tema:") }), scheme,
            el("span", { textContent: tr("Estilo de la interfaz:") }), look,
            el("span", { textContent: tr("Cursores:") }), cursors,
            el("span", { textContent: tr("Idioma:") }), language,
        ]),
        note,
    ]));
    dlg.addButton(tr("Empezar"), "ok", { isDefault: true });
    await dlg.exec();
    Settings.set("welcomed", true);
    if (Settings.get("language") !== startLanguage)
        location.reload();
}

// Diálogos de Kaint: la base modal (con el parpadeo de atención de paint.js al pulsar fuera),
// las tareas con opciones (TaskDialog) y los de Imagen y Capas, Configuración y Acerca de.
import { icon } from "./Icons.js";
import { shortcutLabel } from "./Actions.js";
import { BlendModes, BlendNames } from "../core/Raster.js";
import { Settings } from "../Settings.js";
import { DEFAULT_PACK, IconPacks } from "../IconPacks.js";
import { Plugins } from "../plugins/Plugins.js";
import { UserFonts } from "../UserFonts.js";
import { VERSION } from "../version.js";
import { ColorRoles, Presets } from "./Themes.js";
import { plain, tr } from "../i18n.js";

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
        document.body.appendChild(this.backdrop);
        const focus = this.element.querySelector("input, select, textarea") || this.element.querySelector(".pushbutton.default");
        setTimeout(() => focus?.focus(), 0);
        return new Promise((resolve) => (this.resolve = resolve));
    }

    close(value) {
        if (!this.resolve)
            return;
        openDialogs--;
        this.backdrop.remove();
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

export async function newImageDialog(width, height) {
    const dlg = new Dialog(tr("Imagen nueva"), { iconName: "document-new" });
    const w = numberInput(width, 1, 16384), h = numberInput(height, 1, 16384);
    const keep = checkbox(tr("Mantener la relación de aspecto"), false);
    const bg = select([["white", tr("Blanco")], ["secondary", tr("Color secundario")], ["transparent", tr("Transparente")]], "white");
    const ratio = width / height;
    w.addEventListener("input", () => { if (keep.input.checked) h.value = Math.max(1, Math.round(w.read() / ratio)); });
    h.addEventListener("input", () => { if (keep.input.checked) w.value = Math.max(1, Math.round(h.read() * ratio)); });
    dlg.body.appendChild(el("div", { className: "form" }, [
        el("label", { textContent: tr("Anchura:") }), w, el("label", { textContent: tr("Altura:") }), h,
        el("div", { className: "wide" }, [keep]),
        el("label", { textContent: tr("Fondo:") }), bg,
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { width: w.read(), height: h.read(), background: bg.value } : null;
}

export async function resizeDialog(width, height) {
    const dlg = new Dialog(tr("Cambiar el tamaño"), { iconName: "transform-scale" });
    const byPercent = el("input", { type: "radio", name: "mode", checked: false });
    const byAbsolute = el("input", { type: "radio", name: "mode", checked: true });
    const percent = numberInput(100, 1, 1000);
    const w = numberInput(width, 1, 16384), h = numberInput(height, 1, 16384);
    const keep = checkbox(tr("Mantener la relación de aspecto"), true);
    const method = select([["high", tr("Bicúbico")], ["bilinear", tr("Bilineal")], ["nearest", tr("Vecino más cercano")]], "high");
    const ratio = width / height;
    const sync = () => {
        percent.disabled = !byPercent.checked;
        w.disabled = h.disabled = byPercent.checked;
    };
    byPercent.addEventListener("change", sync);
    byAbsolute.addEventListener("change", sync);
    percent.addEventListener("input", () => {
        w.value = Math.max(1, Math.round((width * percent.read()) / 100));
        h.value = Math.max(1, Math.round((height * percent.read()) / 100));
    });
    w.addEventListener("input", () => { if (keep.input.checked) h.value = Math.max(1, Math.round(w.read() / ratio)); });
    h.addEventListener("input", () => { if (keep.input.checked) w.value = Math.max(1, Math.round(h.read() * ratio)); });
    dlg.body.appendChild(el("div", { className: "form" }, [
        el("label", { textContent: tr("Remuestreo:") }), method,
        el("label", { className: "wide" }, [byPercent, " ", tr("Por porcentaje:")]), el("span"), percent,
        el("label", { className: "wide" }, [byAbsolute, " ", tr("Por tamaño absoluto")]),
        el("div", { className: "wide" }, [keep]),
        el("label", { textContent: tr("Anchura:") }), w, el("label", { textContent: tr("Altura:") }), h,
    ]));
    sync();
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { width: w.read(), height: h.read(), method: method.value } : null;
}

export async function canvasSizeDialog(width, height) {
    const dlg = new Dialog(tr("Tamaño del lienzo"), { iconName: "transform-crop-and-resize" });
    const w = numberInput(width, 1, 16384), h = numberInput(height, 1, 16384);
    let anchor = [1, 1];
    const grid = el("div", { style: { display: "grid", gridTemplateColumns: "repeat(3, 28px)", gap: "2px" } });
    const cells = [];
    for (let y = 0; y < 3; ++y)
        for (let x = 0; x < 3; ++x) {
            const b = el("button", { className: "tbutton", style: { width: "28px", height: "28px" } });
            b.addEventListener("click", () => {
                anchor = [x, y];
                cells.forEach((c, i) => c.classList.toggle("checked", i === y * 3 + x));
            });
            cells.push(b);
            grid.appendChild(b);
        }
    cells[4].classList.add("checked");
    dlg.body.appendChild(el("div", { className: "form" }, [
        el("label", { textContent: tr("Anchura:") }), w, el("label", { textContent: tr("Altura:") }), h,
        el("label", { textContent: tr("Anclaje") }), grid,
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { width: w.read(), height: h.read(), anchor } : null;
}

// Propiedades de la capa con vista previa al momento (onPreview recibe las propiedades).
export async function layerPropertiesDialog(props, onPreview) {
    const dlg = new Dialog(tr("Propiedades de la capa"), { iconName: "layer-properties" });
    const name = el("input", { type: "text", value: props.name });
    const visible = checkbox(tr("Visible"), props.visible);
    const blend = select(BlendModes.map((m) => [m, tr(BlendNames[m][0])]), props.blend);
    const opacity = slider(props.opacity, 0, 255, 1);
    const current = () => ({ name: name.value, visible: visible.input.checked, blend: blend.value, opacity: opacity.value() });
    for (const c of [name, visible.input, blend, ...opacity.querySelectorAll("input")])
        c.addEventListener("input", () => onPreview(current()));
    dlg.body.appendChild(el("div", { className: "form" }, [
        el("label", { textContent: tr("Nombre") }), name,
        el("div", { className: "wide" }, [visible]),
        el("label", { textContent: tr("Modo de fusión") }), blend,
        el("label", { textContent: tr("Opacidad") }), opacity,
    ]));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? current() : null;
}

export async function saveOptionsDialog(formats, chosen, quality) {
    const dlg = new Dialog(tr("Guardar"), { iconName: "document-save" });
    const format = select(formats.map((f) => [f.ext, `${f.name} (.${f.ext})`]), chosen.ext);
    const q = slider(quality, 1, 100, 1);
    const qualityRow = [el("label", { textContent: tr("Calidad:") }), q];
    const sync = () => {
        const f = formats.find((x) => x.ext === format.value);
        qualityRow.forEach((e) => (e.style.visibility = f.quality ? "visible" : "hidden"));
    };
    format.addEventListener("change", sync);
    dlg.body.appendChild(el("div", { className: "form", style: { minWidth: "320px" } }, [
        el("label", { textContent: tr("Formato:") }), format, ...qualityRow,
    ]));
    sync();
    dlg.addButton(tr("Guardar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    const r = await dlg.exec();
    return r ? { format: formats.find((x) => x.ext === format.value), quality: q.value() } : null;
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
            const scheme = select([[0, tr("Seguir al sistema")], [1, tr("Claro")], [2, tr("Oscuro")], [4, tr("Windows 98")], [5, tr("Personalizado")]],
                                  Settings.get("colorScheme"));
            scheme.addEventListener("change", () => Settings.set("colorScheme", Number(scheme.value)));
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
            panelStyle.addEventListener("change", () => Settings.set("panelStyle", Number(panelStyle.value)));
            const border = numberInput(Settings.get("panelBorder"), 1, 12);
            border.style.width = "60px";
            border.title = tr("Grosor del borde de las ventanas Herramientas, Historial, Capas y Colores.");
            border.addEventListener("input", () => Settings.set("panelBorder", border.read()));
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
                el("label", {}, [tr("Tema:"), scheme, customize]),
                el("label", {}, [tr("Idioma:"), language, reload]),
                note(tr("Los cambios de idioma se aplican al recargar la página.")),
                el("label", {}, [tr("Paquete de iconos:"), pack, button(tr("Administrar…"), tr("Añadir o quitar paquetes de iconos."), () => show("iconpacks"))]),
                heading(tr("Ventanas")),
                el("label", {}, [tr("Estilo de las ventanas:"), panelStyle]),
                el("label", {}, [tr("Grosor del borde de las ventanas:"), border, tr("px")]),
                heading(tr("Al cerrar")),
                check(tr("Preguntar una sola vez por todas las imágenes sin guardar"), "askOnceOnClose",
                      tr("Como en Paint.NET: «No guardar» descarta todas a la vez. Desactivado, se pregunta imagen por imagen.")),
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
            const ruler = select([[0, tr("Oscura")], [1, tr("Del color de la barra de herramientas")]], Settings.get("rulerStyle"));
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
            return [
                el("label", {}, [tr("Herramienta predeterminada:"), start]),
                heading(tr("Mover y pegar")),
                check(tr("Los píxeles pegados o movidos reemplazan a los de debajo, también los transparentes"), "replacePixels",
                      tr("Como en Paint.NET: pegar una imagen con transparencia sobre otra en la misma capa deja ese rectángulo transparente. Desactivado, se mezclan con lo que hay debajo.")),
            ];
        }],
        ["tablet", "settings-tablet", tr("Lápiz y tableta"), () => [
            check(tr("La presión del lápiz de la tableta controla el tamaño del pincel"), "penPressure"),
            check(tr("Usar la Goma con el extremo borrador del lápiz"), "tabletEraser"),
            note(tr("Al dar la vuelta al lápiz se activa la herramienta Goma de borrar, y al volver a la punta, la herramienta de antes.")),
            note("PointerEvent" in window ? tr("El navegador admite lápices y tabletas.") : tr("No se detectó ningún lápiz ni tableta de dibujo.")),
        ]],
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

export async function aboutDialog(version) {
    const dlg = new Dialog(tr("Acerca de Kaint"), { iconName: "help-about" });
    dlg.body.appendChild(el("div", { className: "about" }, [
        el("div", { className: "head" }, [
            el("img", { className: "logo", src: "assets/kaint-256.png", alt: "" }),
            el("div", {}, [el("h2", { textContent: "Kaint" }), el("div", { textContent: `${tr("Versión web de Kaint")} ${version}` })]),
        ]),
        el("p", { textContent: tr("Kaint para la web: el editor de imágenes al estilo Paint.NET, en el navegador.") }),
        el("p", {}, [el("a", { href: "https://github.com/ssserch/Kaint", target: "_blank", rel: "noopener", textContent: "github.com/ssserch/Kaint" })]),
        el("small", { textContent: "© SSSerch · GPL-3.0. Paint.NET is a trademark of dotPDN LLC.", style: { opacity: 0.75 } }),
    ]));
    dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
    return dlg.exec();
}

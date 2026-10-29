// Segunda fila de la barra de herramientas (src/app/ToolOptionsBar.cpp): "Herramienta: [icono]"
// y las opciones que use la herramienta actual. Los iconos son los dibujados por código de Kaint.
import { AppState } from "../AppState.js";
import { BlendModes, BlendNames } from "../core/Raster.js";
import { el } from "./Dialogs.js";
import { icon, ShapeTypes } from "./Icons.js";
import { popupMenu } from "./Menu.js";
import { tr } from "../i18n.js";
import { UserFonts } from "../UserFonts.js";

const brushPresets = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85,
                      90, 95, 100, 125, 150, 175, 200, 225, 250, 275, 300, 325, 350, 375, 400, 425, 450, 475, 500];
const fontSizes = [8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 96, 144, 192, 288];
const fonts = ["Noto Sans, Segoe UI, sans-serif", "Arial", "Times New Roman", "Courier New", "Georgia", "Verdana", "Trebuchet MS",
               "Comic Sans MS", "Impact", "serif", "sans-serif", "monospace", "cursive"];

const set = (changes) => AppState.updateTools(changes);
const tools = () => AppState.tools;

function label(text) {
    return el("label", { className: "title", textContent: text });
}

function button(content, tip, onClick) {
    const b = el("button", { className: "tbutton", title: tip }, content);
    b.addEventListener("click", onClick);
    return b;
}

// Botón con menú de opciones (ChoiceButton de Kaint): choices = [[valor, icono, texto], ...].
function choice(tip, key, choices, { showText = false } = {}) {
    const b = el("button", { className: "tbutton choice", title: tip });
    const refresh = () => {
        const current = choices.find(([v]) => v === tools()[key]) || choices[0];
        b.replaceChildren(...[current[1] ? icon(current[1], 16) : null, showText ? el("span", { textContent: current[2] }) : null,
                               el("span", { className: "arrow", textContent: "▼" })].filter(Boolean));
    };
    b.addEventListener("click", () => {
        const r = b.getBoundingClientRect();
        popupMenu(choices.map(([v, iconName, text]) => ({ text, icon: iconName, checked: tools()[key] === v, run: () => set({ [key]: v }) })),
                  r.left, r.bottom);
    });
    refresh();
    b.refresh = refresh;
    return b;
}

// Conjunto de botones de los que uno está marcado.
function toggles(key, items) {
    return items.map(([v, iconName, tip]) => {
        const b = button([icon(iconName, 16)], tip, () => set({ [key]: v }));
        b.refresh = () => b.classList.toggle("checked", tools()[key] === v);
        b.refresh();
        return b;
    });
}

// Barra con el valor dentro (BarSlider de Kaint), de min a max, con el texto que devuelve format.
function barSlider(key, min, max, format) {
    const bar = el("div", { className: "barslider" });
    const fill = el("div", { className: "fill" });
    const text = el("div", { className: "label" });
    bar.append(fill, text);
    const update = () => {
        const v = tools()[key], f = (v - min) / (max - min);
        fill.style.width = `${f * 100}%`;
        text.textContent = format(v);
        text.classList.toggle("inside", f > 0.25);
    };
    const setFrom = (e) => {
        const r = bar.getBoundingClientRect();
        set({ [key]: Math.round(min + Math.min(1, Math.max(0, (e.clientX - r.left) / r.width)) * (max - min)) });
    };
    bar.addEventListener("pointerdown", (e) => {
        bar.setPointerCapture(e.pointerId);
        setFrom(e);
        const move = (ev) => setFrom(ev);
        bar.addEventListener("pointermove", move);
        bar.addEventListener("pointerup", () => bar.removeEventListener("pointermove", move), { once: true });
    });
    bar.addEventListener("wheel", (e) => {
        e.preventDefault();
        set({ [key]: Math.min(max, Math.max(min, tools()[key] + (e.deltaY < 0 ? 1 : -1))) });
    }, { passive: false });
    bar.refresh = update;
    update();
    return bar;
}

function stepper(plus, onStep, tip) {
    const b = button([icon(`opt-stepper:${plus ? "plus" : "minus"}`, 12)], tip, () => onStep(plus ? 1 : -1));
    b.style.minWidth = b.style.minHeight = "18px";
    b.style.padding = "1px";
    return b;
}

function editableCombo(key, presets, width = 64) {
    const input = el("input", { type: "number", min: 1, max: 2000, style: { width: `${width}px` } });
    const list = el("datalist", { id: `list-${key}` }, presets.map((v) => el("option", { value: v })));
    input.setAttribute("list", list.id);
    input.addEventListener("change", () => set({ [key]: Math.max(1, Math.min(2000, Number(input.value) || 1)) }));
    const box = el("span", {}, [input, list]);
    box.refresh = () => { if (document.activeElement !== input) input.value = tools()[key]; };
    box.refresh();
    return box;
}

// Paso al siguiente o anterior valor de la lista de tamaños.
function stepPreset(key, presets, dir) {
    const v = tools()[key];
    const next = dir > 0 ? presets.find((p) => p > v) : [...presets].reverse().find((p) => p < v);
    set({ [key]: next ?? v });
}

export class ToolOptions {
    constructor(element, app) {
        this.element = element;
        this.app = app;
        this.controls = [];
        app.tools.addEventListener("changed", () => this.rebuild());
        window.addEventListener("kaint-fonts", () => this.rebuild());
        AppState.addEventListener("tools", () => this.refresh());
        // La rueda desplaza la barra cuando no cabe.
        element.addEventListener("wheel", (e) => {
            if (element.scrollWidth > element.clientWidth && !e.target.closest(".barslider")) {
                element.scrollLeft += e.deltaY || e.deltaX;
                e.preventDefault();
            }
        }, { passive: false });
        this.rebuild();
    }

    refresh() {
        for (const c of this.controls)
            c.refresh?.();
    }

    group(...children) {
        const flat = children.flat().filter(Boolean);
        this.controls.push(...flat);
        this.element.appendChild(el("span", { className: "sep" }));
        this.element.appendChild(el("span", { className: "group" }, flat));
    }

    rebuild() {
        const tool = this.app.tools.current;
        this.element.replaceChildren();
        this.controls = [];
        if (!tool)
            return;
        // Herramienta: [icono ▾]
        const pick = button([icon(tool.icon, 16), el("span", { className: "arrow", textContent: "▼" })], tool.name, () => {
            const r = pick.getBoundingClientRect();
            popupMenu(this.app.tools.tools.map((t) => ({ text: t.name, icon: t.icon, shortcut: t.letter, checked: t === tool,
                                                         run: () => this.app.tools.select(t.id) })), r.left, r.bottom);
        });
        this.element.append(label(tr("Herramienta:")), pick);
        for (const option of tool.options)
            this.addOption(option, tool);
    }

    addOption(option, tool) {
        switch (option) {
        case "brushSize":
            this.group(label(tr("Tamaño de pincel:")), stepper(false, (d) => stepPreset("brushSize", brushPresets, d), tr("Reducir")),
                       editableCombo("brushSize", brushPresets), stepper(true, (d) => stepPreset("brushSize", brushPresets, d), tr("Aumentar")));
            break;
        case "hardness":
            this.group(label(tr("Dureza:")), stepper(false, () => set({ hardness: Math.max(0, tools().hardness - 1) }), tr("Reducir")),
                       barSlider("hardness", 0, 100, (v) => `${v}%`),
                       stepper(true, () => set({ hardness: Math.min(100, tools().hardness + 1) }), tr("Aumentar")));
            break;
        case "spacing":
            this.group(label(tr("Espaciado:")), stepper(false, () => set({ spacing: Math.max(1, tools().spacing - 1) }), tr("Reducir")),
                       barSlider("spacing", 1, 100, (v) => `${v}%`),
                       stepper(true, () => set({ spacing: Math.min(100, tools().spacing + 1) }), tr("Aumentar")));
            break;
        case "smoothing":
            this.group(choice(tr("Trayectoria"), "smoothing", [[false, "opt-smoothing:0", tr("Trayectoria no suavizada")],
                                                                [true, "opt-smoothing:1", tr("Trayectoria suavizada")]]));
            break;
        case "pressure": {
            const b = button([icon("opt-pressure", 16)], tr("La presión del lápiz de la tableta controla el tamaño del pincel"),
                             () => set({ pressure: !tools().pressure }));
            b.refresh = () => {
                b.classList.toggle("checked", tools().pressure !== false);
                b.replaceChildren(icon(tools().pressure !== false ? "opt-pressure:1" : "opt-pressure:0", 16));
            };
            b.refresh();
            this.group(b);
            break;
        }
        case "tolerance":
            this.group(label(tr("Tolerancia:")), barSlider("tolerance", 0, 100, (v) => `${v}%`));
            break;
        case "antialias":
            this.group(choice(tr("Suavizado"), "antialias", [[true, "opt-aa:1", tr("Suavizado activado")],
                                                             [false, "opt-aa:0", tr("Suavizado desactivado")]]));
            break;
        case "blend":
            this.group(choice(tr("Modo de fusión"), "blend", BlendModes.map((m) => [m, "opt-blend", tr(BlendNames[m][1])]), { showText: true }));
            break;
        case "flood":
            this.group(label(tr("Modo de saturación")), choice(tr("Modo de saturación"), "floodGlobal",
                                                              [[false, "opt-flood:0", tr("Contiguo")], [true, "opt-flood:1", tr("Global")]]));
            break;
        case "sampling":
            this.group(label(tr("Toma de muestras:")), choice(tr("Toma de muestras"), "sampleImage",
                                                               [[false, null, tr("Capa")], [true, null, tr("Imagen")]], { showText: true }));
            break;
        case "selectionMode":
            this.group(toggles("selectionMode", [
                ["replace", "opt-selmode:replace", tr("Modo de selección: %1", tr("Reemplazar"))],
                ["union", "opt-selmode:union", tr("Modo de selección: %1", tr("Sumar (unión) · Ctrl + clic izquierdo"))],
                ["exclude", "opt-selmode:exclude", tr("Modo de selección: %1", tr("Excluir · Alt + clic izquierdo"))],
                ["intersect", "opt-selmode:intersect", tr("Modo de selección: %1", tr("Intersecar · Alt + clic derecho"))],
                ["xor", "opt-selmode:xor", tr("Modo de selección: %1", tr("Invertir («xor») · Ctrl + clic derecho"))]]));
            break;
        case "afterSelect":
            this.group(label(tr("Después de seleccionar:")), choice(tr("Después de seleccionar"), "afterSelect", [
                ["none", "rectangleselect", tr("No cambiar de herramienta")],
                ["moveselection", "moveselection", tr("Cambiar a Mover selección")],
                ["moveselectedpixels", "moveselectedpixels", tr("Cambiar a Mover píxeles seleccionados")]], { showText: true }));
            break;
        case "selectionQuality":
            this.group(choice(tr("Calidad de selección"), "selectionAntialias", [
                [false, "opt-selquality:0", tr("Calidad de selección pixelada")],
                [true, "opt-selquality:1", tr("Calidad de selección con antidentado")]]));
            break;
        case "rectMode": {
            const w = el("input", { type: "number", min: 1, style: { width: "52px" } });
            const h = el("input", { type: "number", min: 1, style: { width: "52px" } });
            w.addEventListener("change", () => set({ fixedWidth: Math.max(1, Number(w.value) || 1) }));
            h.addEventListener("change", () => set({ fixedHeight: Math.max(1, Number(h.value) || 1) }));
            const sizes = el("span", { className: "group" }, [label(tr("Ancho:")), w, label(tr("Alto:")), h]);
            sizes.refresh = () => {
                sizes.style.display = tools().rectMode === "normal" ? "none" : "";
                w.value = tools().fixedWidth;
                h.value = tools().fixedHeight;
            };
            this.group(choice(tr("Modo de selección rectangular"), "rectMode", [["normal", "opt-rectmode:normal", tr("Cualquier tamaño")],
                                                       ["fixedratio", "opt-rectmode:ratio", tr("Proporción")],
                                                       ["fixedsize", "opt-rectmode:size", tr("Tamaño fijo")]]), sizes);
            sizes.refresh();
            break;
        }
        case "gradient":
            this.group(toggles("gradient", [
                ["linear", "opt-gradient:linear", tr("Lineal")], ["reflected", "opt-gradient:reflected", tr("Lineal (reflejado)")],
                ["diamond", "opt-gradient:diamond", tr("Rombo")], ["radial", "opt-gradient:radial", tr("Radial")],
                ["conical", "opt-gradient:conical", tr("Cónico")]]));
            this.group(choice(tr("Modo"), "gradientTransparency", [[false, null, tr("Modo de color")],
                                                                   [true, null, tr("Modo de transparencia")]], { showText: true }));
            this.group(choice(tr("Repetición"), "gradientRepeat", [["none", null, tr("Sin repetición")],
                                                                   ["wrapped", null, tr("Repetir envuelto")],
                                                                   ["reflected", null, tr("Repetición reflejada")]], { showText: true }));
            break;
        case "shape":
            this.group(choice(tr("Forma (A / Mayús+A)"), "shape", ShapeTypes.map(([id, name]) => [id, `opt-shape:${id}`, tr(name)])));
            break;
        case "shapeStyle":
            this.group(choice(tr("Tipo de dibujo"), "shapeStyle", [
                ["outline", "opt-style:outline", tr("Dibujar el contorno de la forma")],
                ["fill", "opt-style:fill", tr("Dibujar forma rellena")],
                ["both", "opt-style:both", tr("Dibuja una forma rellena con contorno")]]));
            break;
        case "dash":
            this.group(label(tr("Estilo:")), choice(tr("Estilo de guion ( . )"), "dash", [
                ["solid", "opt-dash:solid", tr("Sólido")], ["dash", "opt-dash:dash", tr("Guiones")], ["dot", "opt-dash:dot", tr("Punteado")],
                ["dashdot", "opt-dash:dashdot", tr("Guion, punto")], ["dashdotdot", "opt-dash:dashdotdot", tr("Guion, punto, punto")]]));
            break;
        case "caps": {
            const caps = (end) => [["flat", `opt-cap:flat:${end}`, tr("Línea")], ["round", `opt-cap:round:${end}`, tr("Redondeado")],
                                   ["arrow", `opt-cap:arrow:${end}`, tr("Flecha")], ["filled", `opt-cap:filled:${end}`, tr("Flecha rellena")]];
            this.group(choice(tr("Inicio de la línea ( , )"), "startCap", caps("start")), choice(tr("Final de la línea ( / )"), "endCap", caps("end")));
            break;
        }
        case "curve":
            this.group(toggles("curveType", [["straight", "opt-curve:straight", tr("Directo")], ["spline", "opt-curve:spline", tr("Curva segmentaria")],
                                             ["bezier", "opt-curve:bezier", tr("Bézier")]]));
            break;
        case "text": {
            // Las fuentes del navegador y las que se añadieron en Configuración > Fuentes.
            const family = el("select", { style: { width: "150px" } },
                              [...fonts, ...UserFonts.families().map((f) => `"${f}"`)].map((f) => el("option", { value: f, textContent: f.split(",")[0].replace(/"/g, "") })));
            family.addEventListener("change", () => set({ fontFamily: family.value }));
            family.refresh = () => { family.value = tools().fontFamily; };
            const style = [["bold", "B", tr("Negrita")], ["italic", "I", tr("Cursiva")], ["underline", "U", tr("Subrayado")]].map(([key, t, tip]) => {
                const b = button([el(key === "bold" ? "b" : key === "italic" ? "i" : "u", { textContent: t })], tip, () => set({ [key]: !tools()[key] }));
                b.refresh = () => b.classList.toggle("checked", tools()[key]);
                return b;
            });
            this.group(label(tr("Tipo de letra:")), family, editableCombo("fontSize", fontSizes, 56));
            this.group(...style);
            this.group(toggles("textAlign", [["left", "opt-align:left", tr("Alinear a la izquierda")],
                                             ["center", "opt-align:center", tr("Centrar")], ["right", "opt-align:right", tr("Alinear a la derecha")]]));
            break;
        }
        case "picker":
            this.group(label(tr("Toma de muestras:")),
                       choice(tr("Tamaño de la muestra"), "pickerSize", [1, 3, 5, 11, 31, 51].map((n) => [n, `opt-sample:${n}`, n === 1 ? tr("Píxel") : tr("%1 × %1 píxeles", n)]), { showText: true }),
                       choice(tr("Toma de muestras"), "pickerSampleImage", [[false, null, tr("Capa")], [true, null, tr("Imagen")]], { showText: true }));
            this.group(label(tr("Después de pulsar:")), choice(tr("Después de pulsar"), "pickerAfter", [
                ["stay", null, tr("No cambiar de herramienta")], ["previous", null, tr("Cambiar a la herramienta anterior")],
                ["pencil", null, tr("Cambiar a la herramienta Lápiz")]], { showText: true }));
            break;
        case "recolor":
            this.group(label(tr("Muestreo:")), choice(tr("Muestreo"), "recolorSampling", [
                ["once", "opt-recolor:once", tr("Muestreo: una vez")], ["secondary", "opt-recolor:secondary", tr("Muestreo: Color secundario")]]));
            break;
        case "finish": {
            const b = button([el("span", { textContent: "✓" }), el("span", { textContent: tr("Acabado") })],
                             tr("Aplicar lo que se está editando (Intro)"), () => this.app.tools.current?.finish());
            this.group(b);
            break;
        }
        }
        void tool;
    }
}

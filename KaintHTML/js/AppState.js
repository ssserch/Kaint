// Estado compartido (como AppState de Kaint): colores principal y secundario, paleta y opciones
// de las herramientas, que son las mismas para todas (el ancho del pincel vale para el pincel,
// la goma, las formas...). Eventos: "colors", "tools", "palette".
import { remember } from "./Settings.js";

export const defaultTools = () => ({
    brushSize: 10,
    hardness: 75,
    // Separación entre sellos (% del diámetro), suavizado del trazo y presión del lápiz (como Kaint).
    spacing: 15,
    smoothing: true,
    pressure: true,
    antialias: true,
    selectionAntialias: false,
    blend: "normal",
    tolerance: 50,
    floodGlobal: false,
    sampleImage: false,
    pickerSampleImage: false,
    selectionMode: "replace",
    // Después de seleccionar: "none", "moveselection" o "moveselectedpixels" (como Kaint).
    afterSelect: "none",
    rectMode: "normal",
    fixedWidth: 4,
    fixedHeight: 3,
    gradient: "linear",
    gradientTransparency: false,
    gradientRepeat: "none",
    shape: "rectangle",
    shapeStyle: "outline",
    cornerRadius: 20,
    dash: "solid",
    curveType: "spline",
    startCap: "flat",
    endCap: "flat",
    fontFamily: "Noto Sans, Segoe UI, sans-serif",
    fontSize: 12,
    bold: false,
    italic: false,
    underline: false,
    strikeout: false,
    // Unidad del tamaño de letra ("points": según los PPP de la imagen; "fixed96") y modo de
    // representación ("smooth", "sharpModern", "sharpClassic").
    fontMetric: "points",
    textRendering: "smooth",
    textAlign: "left",
    pickerSize: 1,
    pickerAfter: "stay",
    recolorSampling: "secondary",
    // Mover selección: píxel a píxel por defecto. Mover píxeles seleccionados: muestreo y gamma.
    moveSelectionAntialias: false,
    moveInterpolation: "bicubic",
    moveGamma: true,
    // Modo alfa de la tolerancia (directo o premultiplicado) y acumular los rellenos del cubo.
    toleranceStraight: false,
    fillAccumulate: true,
    // Trama de relleno del pincel, el cubo, la línea y las formas (0: color sólido).
    fill: 0,
});

// Opciones predeterminadas de las herramientas (Configuración > Herramientas), como
// AppState::toolDefaults de Kaint: con ellas empieza cada sesión; lo último usado no se guarda.
export function toolDefaults() {
    return { ...defaultTools(), ...(remember("toolDefaults") || {}) };
}

export function setToolDefaults(tools) {
    remember("toolDefaults", { ...tools });
}

// Unas opciones de herramienta propias (LocalToolSettings de Kaint): las de Configuración >
// Herramientas. Eventos: "tools".
export class LocalToolSettings extends EventTarget {
    constructor(initial) {
        super();
        this.tools = { ...initial };
    }

    updateTools(changes) {
        Object.assign(this.tools, changes);
        this.dispatchEvent(new Event("tools"));
    }

    set(tools) {
        this.tools = { ...tools };
        this.dispatchEvent(new Event("tools"));
    }
}

// Paleta de Paint.NET (la de Kaint, src/core/Palette.cpp): 64 colores y los 32 primeros otra
// vez con la mitad de opacidad.
const paletteRgb = [
    0x000000, 0x404040, 0xff0000, 0xff6a00, 0xffd800, 0xb6ff00, 0x4cff00, 0x00ff21, 0x00ff90, 0x00ffff, 0x0094ff,
    0x0026ff, 0x4800ff, 0xb200ff, 0xff00dc, 0xff006e, 0xffffff, 0x808080, 0x7f0000, 0x7f3300, 0x7f6a00, 0x5b7f00,
    0x267f00, 0x007f0e, 0x007f46, 0x007f7f, 0x004a7f, 0x00137f, 0x21007f, 0x57007f, 0x7f006e, 0x7f0037, 0xa0a0a0,
    0x303030, 0xff7f7f, 0xffb27f, 0xffe97f, 0xdaff7f, 0xa5ff7f, 0x7fff8e, 0x7fffc5, 0x7fffff, 0x7fc9ff, 0x7f92ff,
    0xa17fff, 0xd67fff, 0xff7fed, 0xff7fb6, 0xc0c0c0, 0x606060, 0x7f3f3f, 0x7f593f, 0x7f743f, 0x6d7f3f, 0x527f3f,
    0x3f7f47, 0x3f7f62, 0x3f7f7f, 0x3f647f, 0x3f497f, 0x503f7f, 0x6b3f7f, 0x7f3f76, 0x7f3f5b,
];

// Paletas de muestra del menú de paletas (como Kaint): el arcoíris (16 tonos con 6 luminosidades)
// y la pequeña (los 32 primeros colores de la predeterminada y el resto en blanco).
export function rainbowPalette() {
    const colors = [];
    for (const l of [0.2, 0.32, 0.44, 0.56, 0.68, 0.8])
        for (let i = 0; i < 16; ++i) {
            const h = i / 16, q = l < 0.5 ? l * 2 : 1, p = 2 * l - q;
            const channel = (t) => {
                t = (t + 1) % 1;
                const v = t < 1 / 6 ? p + (q - p) * 6 * t : t < 1 / 2 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p;
                return Math.round(v * 255);
            };
            colors.push({ r: channel(h + 1 / 3), g: channel(h), b: channel(h - 1 / 3), a: 255 });
        }
    return colors;
}

export function smallPalette() {
    const colors = defaultPalette().slice(0, 32);
    while (colors.length < 96)
        colors.push({ r: 255, g: 255, b: 255, a: 255 });
    return colors;
}

// Paleta en el formato de texto de Paint.NET (aarrggbb por línea, ';' para comentarios).
export function parsePalette(text) {
    const colors = [];
    for (const line of text.split(/\r?\n/)) {
        const m = /^\s*([0-9a-f]{8}|[0-9a-f]{6})\s*$/i.exec(line.split(";")[0]);
        if (!m)
            continue;
        const v = m[1].length === 6 ? `ff${m[1]}` : m[1];
        colors.push({ a: parseInt(v.slice(0, 2), 16), r: parseInt(v.slice(2, 4), 16), g: parseInt(v.slice(4, 6), 16), b: parseInt(v.slice(6, 8), 16) });
        if (colors.length === 96)
            break;
    }
    while (colors.length && colors.length < 96)
        colors.push({ r: 255, g: 255, b: 255, a: 255 });
    return colors;
}

export function serializePalette(colors) {
    const h = (n) => n.toString(16).padStart(2, "0").toUpperCase();
    return "; Paint.NET Palette File\n" + colors.map((c) => h(c.a ?? 255) + h(c.r) + h(c.g) + h(c.b)).join("\n") + "\n";
}

export function samePalette(a, b) {
    return a.length === b.length && a.every((c, i) => c.r === b[i].r && c.g === b[i].g && c.b === b[i].b && (c.a ?? 255) === (b[i].a ?? 255));
}

export function defaultPalette() {
    const color = (v, a) => ({ r: (v >> 16) & 255, g: (v >> 8) & 255, b: v & 255, a });
    return [...paletteRgb.map((v) => color(v, 255)), ...paletteRgb.slice(0, 32).map((v) => color(v, 128))];
}

class State extends EventTarget {
    constructor() {
        super();
        // Cada inicio empieza como Paint.NET: negro y blanco.
        this.primary = { r: 0, g: 0, b: 0, a: 255 };
        this.secondary = { r: 255, g: 255, b: 255, a: 255 };
        this.secondaryActive = false;
        // Ni los colores ni las opciones de las herramientas se guardan al salir (como en Kaint);
        // las últimas usadas que guardaban las versiones anteriores se descartan.
        this.tools = toolDefaults();
        try {
            localStorage.removeItem("kaint.tools");
        } catch {
            // Sin almacenamiento.
        }
        this.palette = remember("palette") || defaultPalette();
    }

    emit(type) {
        this.dispatchEvent(new Event(type));
    }

    setPrimary(c) {
        this.primary = { ...c, a: c.a ?? 255 };
        this.emit("colors");
    }

    setSecondary(c) {
        this.secondary = { ...c, a: c.a ?? 255 };
        this.emit("colors");
    }

    swapColors() {
        [this.primary, this.secondary] = [this.secondary, this.primary];
        this.emit("colors");
    }

    resetColors() {
        this.primary = { r: 0, g: 0, b: 0, a: 255 };
        this.secondary = { r: 255, g: 255, b: 255, a: 255 };
        this.emit("colors");
    }

    setSecondaryActive(on) {
        this.secondaryActive = on;
        this.emit("colors");
    }

    activeColor() {
        return this.secondaryActive ? this.secondary : this.primary;
    }

    setActiveColor(c) {
        this.secondaryActive ? this.setSecondary(c) : this.setPrimary(c);
    }

    updateTools(changes) {
        Object.assign(this.tools, changes);
        this.emit("tools");
    }

    // Calidad de selección con la que recorta la herramienta actual (Raster::selectionAntialias de
    // Kaint): Mover selección tiene la suya.
    selectionAntialias() {
        return !!(this.currentTool === "moveselection" ? this.tools.moveSelectionAntialias : this.tools.selectionAntialias);
    }

    setPalette(colors) {
        this.palette = colors.slice(0, 96);
        remember("palette", this.palette);
        this.emit("palette");
    }

    resetPalette() {
        this.setPalette(defaultPalette());
    }
}

export const AppState = new State();

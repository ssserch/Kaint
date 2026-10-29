// Estado compartido (como AppState de Kaint): colores principal y secundario, paleta y opciones
// de las herramientas, que son las mismas para todas (el ancho del pincel vale para el pincel,
// la goma, las formas...). Eventos: "colors", "tools", "palette".
import { remember } from "./Settings.js";

export const defaultTools = () => ({
    brushSize: 10,
    hardness: 75,
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
    textAlign: "left",
    pickerSize: 1,
    pickerAfter: "stay",
    recolorSampling: "secondary",
});

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
        this.tools = { ...defaultTools(), ...(remember("tools") || {}) };
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
        remember("tools", this.tools);
        this.emit("tools");
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

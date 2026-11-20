// Temas de Kaint (src/app/Theme.cpp): claro, oscuro y azul de Paint.NET, Windows 7 y Windows 7
// oscuro (con los de 7.css), Windows 7 Basic (sin cristal), Windows 8 (el marco plano y ancho del color
// de las ventanas), Windows XP, Windows 98 (con los colores de 98.css y botones en relieve), los de
// los escritorios de Linux (Ubuntu, GNOME, Linux Mint y elementary OS, claros y oscuros) y el personalizado, que parte de uno de ellos (también de Windows 7, claro u oscuro, o de XP, con
// su estilo), cambia sus colores y puede llevar una imagen, un GIF o un vídeo de fondo.
import { Settings } from "../Settings.js";

// Colores editables: clave, variable de CSS y nombre (los mismos que en el tema personalizado de
// Kaint).
export const ColorRoles = [
    ["accent", "--accent", "Acento"], ["window", "--window", "Ventana y diálogos"], ["text", "--text", "Texto"],
    ["base", "--base", "Campos y listas"], ["button", "--button", "Botones"], ["border", "--border", "Bordes"],
    ["menuBar", "--menubar", "Fila de menús"], ["menuBarText", "--menubar-text", "Texto de los menús"],
    ["toolBar", "--toolbar", "Barras de herramientas"], ["statusBar", "--statusbar", "Barra de estado"],
    ["panel", "--panel", "Ventanas flotantes"], ["panelTitle", "--panel-title", "Cabecera de las ventanas"],
    ["panelTitleEnd", "--panel-title-end", "Cabecera (final del degradado)"], ["panelTitleText", "--panel-title-text", "Título de las ventanas"],
    ["panelClose", "--panel-close", "Botón de cerrar"], ["selection", "--selection", "Selección"],
    ["selectionBorder", "--selection-border", "Borde de la selección"], ["hover", "--hover", "Bajo el puntero"],
    ["workspace", "--workspace", "Fondo del lienzo"],
    // El cristal de las ventanas con el estilo de Windows 7 (su «Color de la ventana»).
    ["glass", "--glass", "Color de las ventanas (cristal)"],
];

export const Presets = {
    light: { accent: "#0078d7", window: "#f0f0f0", text: "#000000", base: "#ffffff", button: "#e1e1e1", border: "#adadad",
             menuBar: "#ffffff", menuBarText: "#000000", toolBar: "#ffffff", statusBar: "#f3f3f3", panel: "#f0f0f0",
             panelTitle: "#f0f0f0", panelTitleEnd: "#f0f0f0", panelTitleText: "#3c3c3c", panelClose: "#c75050", selection: "#cce4f7",
             selectionBorder: "#3c7fb1", hover: "#e5f3ff", workspace: "#cfcfcf" },
    dark: { accent: "#0078d4", window: "#2b2b2b", text: "#f0f0f0", base: "#1e1e1e", button: "#3a3a3a", border: "#5f5f5f",
            menuBar: "#000000", menuBarText: "#f0f0f0", toolBar: "#202020", statusBar: "#202020", panel: "#262626",
            panelTitle: "#262626", panelTitleEnd: "#262626", panelTitleText: "#d6d6d6", panelClose: "#a84646", selection: "#1c476d",
            selectionBorder: "#2a6fb0", hover: "#3a3a3a", workspace: "#303030" },
    win98: { accent: "#000080", window: "#c0c0c0", text: "#000000", base: "#ffffff", button: "#c0c0c0", border: "#808080",
             menuBar: "#c0c0c0", menuBarText: "#000000", toolBar: "#c0c0c0", statusBar: "#c0c0c0", panel: "#c0c0c0",
             panelTitle: "#000080", panelTitleEnd: "#1084d0", panelTitleText: "#ffffff", panelClose: "#c0c0c0", selection: "#000080",
             selectionBorder: "#000080", hover: "#c0c0c0", workspace: "#008080" },
};

// El Azul de Paint.NET (Theme::blue de Kaint): el claro con los menús casi blancos, las barras y
// las reglas en degradado azulado (en la hoja de estilo, con la clase "blue") y la barra de estado
// azul claro.
const BlueColors = { toolBar: "#fbfdff", statusBar: "#ebf2fa", border: "#8fa6c3", hover: "#e5f1fb" };

// Windows 7 (Theme::windows7 de Kaint), con los colores de 7.css (https://khang-nd.github.io/7.css/,
// licencia MIT, © 2021 Khang Nguyen Duy): el claro con la barra de arriba y las cabeceras en el azul
// de Aero Basic y la selección celeste (los degradados, en la hoja de estilo con la clase "win7").
const Win7Colors = { accent: "#3399ff", window: "#f0f0f0", text: "#222222", base: "#ffffff", button: "#ebebeb", border: "#8e8f8f",
                     menuBar: "#b9d1ea", menuBarText: "#000000", toolBar: "#f1f4fa", statusBar: "#f0f0f0", panel: "#f0f0f0",
                     panelTitle: "#c4d8ee", panelTitleEnd: "#a9c4e6", panelTitleText: "#000000", panelClose: "#c9503a",
                     selection: "#d5e6fb", selectionBorder: "#7da2ce", hover: "#e8f2fd", workspace: "#c8d4e3", glass: "#7ea7d6" };

// Windows 7 oscuro (Theme::windows7Dark de Kaint): el de Windows 7 con el cristal ahumado (como los
// temas oscuros de Aero) y los controles en grises oscuros (en la hoja de estilo, "win7" con "dark").
const Win7DarkColors = { accent: "#3399ff", window: "#2b2d30", text: "#e8e8e8", base: "#1e1f22", button: "#3a3d41", border: "#5a5f66",
                         menuBar: "#2a3542", menuBarText: "#f0f0f0", toolBar: "#30343a", statusBar: "#2b2d30", panel: "#2b2d30",
                         panelTitle: "#3a4a5e", panelTitleEnd: "#2c3a4c", panelTitleText: "#ffffff", panelClose: "#c9503a",
                         selection: "#26476b", selectionBorder: "#4f86c6", hover: "#2f3f52", workspace: "#1b1d20", glass: "#3a4452" };

// Windows XP (Theme::windowsXP de Kaint), con los colores de XP.css (https://botoxparty.github.io/XP.css/,
// licencia MIT, © 2020 Adam Hammad y Jordan Scales): Luna azul, ventanas beige y la selección azul.
const XpColors = { accent: "#316ac5", window: "#ece9d8", text: "#000000", base: "#ffffff", button: "#ece9d8", border: "#7f9db9",
                   menuBar: "#ece9d8", menuBarText: "#000000", toolBar: "#ece9d8", statusBar: "#ece9d8", panel: "#ece9d8",
                   panelTitle: "#0058e6", panelTitleEnd: "#3a8cf5", panelTitleText: "#ffffff", panelClose: "#e0461d",
                   selection: "#316ac5", selectionBorder: "#316ac5", hover: "#c1d2ee", workspace: "#a7a6aa" };

// Windows 98 ALT: el clásico de Windows 2000 y XP (Paint.NET 2 en Windows XP con el estilo clásico):
// gris #d4d0c8, cabeceras de azul oscuro a celeste y el lienzo sobre gris.
const Win98AltColors = { accent: "#0a246a", window: "#d4d0c8", text: "#000000", base: "#ffffff", button: "#d4d0c8", border: "#808080",
                         menuBar: "#d4d0c8", menuBarText: "#000000", toolBar: "#d4d0c8", statusBar: "#d4d0c8", panel: "#d4d0c8",
                         panelTitle: "#0a246a", panelTitleEnd: "#a6caf0", panelTitleText: "#ffffff", panelClose: "#d4d0c8",
                         selection: "#0a246a", selectionBorder: "#0a246a", hover: "#d4d0c8", workspace: "#808080" };

// Windows 7 Basic (Theme::windows7Basic de Kaint): sin Aero, el marco opaco en el celeste de Windows 7
// Basic (o el gris azulado oscuro), con los mismos controles que el de cristal.
const Win7BasicColors = { ...Win7Colors, glass: "#b9d1ea", menuBar: "#c9dcf2", panelTitle: "#c4d8ee", panelTitleEnd: "#b2cbe8" };
const Win7BasicDarkColors = { ...Win7DarkColors, glass: "#46505e", menuBar: "#323b47", panelTitle: "#46505e", panelTitleEnd: "#39424e" };

// Windows 8 (Theme::windows8 de Kaint): el marco ancho y la barra de título planos del color de las
// ventanas (de fábrica, el celeste de la primera captura), el título centrado y Segoe UI.
export const Win8DefaultColor = "#6badf6";
// Los de «Color y apariencia» de Windows 8, sin negro (el negro, solo con «Personalizado»).
export const Win8WindowColors = [
    ["Gris", "#ababab"], ["Celeste", "#8acfff"], ["Rosa", "#f598d6"], ["Amarillo", "#f3d240"], ["Lima", "#add85f"],
    ["Turquesa", "#78d9d9"], ["Naranja", "#ffaf51"], ["Coral", "#ff6f6f"], ["Fucsia", "#f359a8"], ["Verde", "#47cf74"],
    ["Lila", "#c48aff"], ["Azul", "#58b1fc"], ["Lavanda", "#9898ff"], ["Topo", "#c3b5a8"], ["Blanco", "#fcfcfc"],
];
// Los de la pantalla Inicio de Windows 8.
export const Win8StartColors = [
    ["Verde azulado", "#008299"], ["Turquesa intenso", "#00a0b1"], ["Azul cobalto", "#2672ec"], ["Azul cielo", "#2e8def"],
    ["Morado", "#8c0095"], ["Magenta", "#a700ae"], ["Índigo", "#5133ab"], ["Violeta", "#643ebf"], ["Granate", "#ac193d"],
    ["Carmesí", "#bf1e4b"], ["Teja", "#d24726"], ["Mandarina", "#dc572e"], ["Verde bosque", "#008a00"], ["Verde hoja", "#00a600"],
    ["Azul marino", "#094ab2"], ["Azul real", "#0a5bc4"],
];

const lightness = (hex) => {
    const [r, g, b] = rgbOf(hex);
    return (Math.max(r, g, b) + Math.min(r, g, b)) / 2;
};
const mixHex = (a, b, t) => {
    const x = rgbOf(a), y = rgbOf(b);
    return hexOf(x.map((v, i) => v * t + y[i] * (1 - t)));
};

function win8Colors(dark, color) {
    const window = /^#[0-9a-f]{6}$/i.test(color || "") ? color.toLowerCase() : Win8DefaultColor;
    const out = { accent: "#3399ff", menuBar: window, panelTitle: window, panelTitleEnd: window,
                  panelTitleText: lightness(window) < 150 ? "#ffffff" : "#000000", menuBarText: dark ? Presets.dark.menuBarText : "#000000",
                  win8: window, panelClose: "#c75050" };
    if (!dark)
        Object.assign(out, { selection: "#cce8ff", selectionBorder: "#99d1ff", hover: "#e5f3ff", button: "#e1e1e1", border: "#acacac",
                             toolBar: "#f5f6f7", statusBar: "#f0f0f0" });
    return out;
}

// Los escritorios de Linux (Theme::linuxDesktop de Kaint): sus colores de ventana, de la barra de
// título (headerbar), de la vista y del texto, su color de acento, su fuente y su redondeo.
const LinuxDesktops = {
    yaru: { light: ["#e95420", "#fafafa", "#ebebeb", "#ffffff", "#3d3d3d", "#f2f2f2", "#cdc7c2", "#c8c8c8"],
            dark: ["#e95420", "#2c2c2c", "#303030", "#232323", "#f7f7f7", "#3c3c3c", "#4a4a4a", "#1e1e1e"], font: "Ubuntu", radius: 6 },
    adwaita: { light: ["#3584e4", "#fafafa", "#ebebeb", "#ffffff", "#2e3436", "#ebebeb", "#d8d4d0", "#c0bfbc"],
               dark: ["#3584e4", "#242424", "#303030", "#1e1e1e", "#ffffff", "#3a3a3a", "#444444", "#1a1a1a"], font: "Adwaita Sans", radius: 6 },
    mint: { light: ["#35a854", "#f2f2f2", "#e8e8e8", "#ffffff", "#303030", "#f7f7f7", "#cfcfcf", "#c4c4c4"],
            dark: ["#35a854", "#383838", "#2f2f2f", "#404040", "#dadada", "#444444", "#525252", "#262626"], font: "Ubuntu", radius: 4 },
    elementary: { light: ["#3689e6", "#fafafa", "#f0f0f0", "#ffffff", "#333333", "#fafafa", "#c3c3c3", "#cfcfcf"],
                  dark: ["#3689e6", "#333333", "#2b2b2b", "#292929", "#fafafa", "#404040", "#4d4d4d", "#1f1f1f"], font: "Inter", radius: 4 },
};

function linuxColors(desktop, dark) {
    const d = LinuxDesktops[desktop] || LinuxDesktops.adwaita;
    const [accent, window, header, base, text, button, border, workspace] = dark ? d.dark : d.light;
    return { accent, window, text, base, button, border, menuBar: header, menuBarText: text, toolBar: window, statusBar: window, panel: window,
             panelTitle: header, panelTitleEnd: header, panelTitleText: text, panelClose: "#c01c28",
             // La selección con el acento suavizado (el texto sigue siendo el normal) y su borde con el acento.
             selection: mixHex(accent, base, dark ? 0.45 : 0.25), selectionBorder: accent, hover: mixHex(accent, window, dark ? 0.18 : 0.12), workspace };
}

// Los temas 11 a 22 (los mismos números que en Kaint): Windows 8, Windows 7 Basic y los de Linux.
const LinuxSchemes = { 15: "yaru", 16: "yaru", 17: "adwaita", 18: "adwaita", 19: "mint", 20: "mint", 21: "elementary", 22: "elementary" };

// La lista de temas de Configuración y de la bienvenida (como la de Kaint), agrupados.
export function themeChoices(tr) {
    return [[0, tr("Seguir al sistema")], [1, tr("Claro")], [2, tr("Oscuro")], [6, tr("Azul")],
            [7, tr("Windows 7")], [10, tr("Windows 7 oscuro")], [13, tr("Windows 7 Basic")], [14, tr("Windows 7 Basic oscuro")],
            [11, tr("Windows 8")], [12, tr("Windows 8 oscuro")], [8, tr("Windows XP")], [4, tr("Windows 98")], [9, tr("Windows 98 ALT")],
            [15, "Ubuntu"], [16, tr("Ubuntu oscuro")], [17, "GNOME"], [18, tr("GNOME oscuro")], [19, "Linux Mint"], [20, tr("Linux Mint oscuro")],
            [21, "elementary OS"], [22, tr("elementary OS oscuro")], [5, tr("Personalizado")]];
}

// De qué parte el tema personalizado: el modo de la hoja de estilo, los colores y el estilo de los
// controles (Windows 7, XP o el clásico de 98).
export const CustomBases = {
    light: { base: "light", colors: {} },
    dark: { base: "dark", colors: {} },
    win7: { base: "light", colors: Win7Colors, win7: true },
    win7dark: { base: "dark", colors: Win7DarkColors, win7: true },
    xp: { base: "light", colors: XpColors, xp: true },
    win98: { base: "win98", colors: {} },
    win98alt: { base: "win98", colors: Win98AltColors, alt: true },
};

// Los colores de una base del personalizado (los que tiene cada color sin cambiar).
export function baseColors(name) {
    const b = CustomBases[name] || CustomBases.dark;
    return { ...Presets[b.base], ...b.colors };
}

const rgbOf = (hex) => {
    const n = parseInt(String(hex).slice(1, 7), 16);
    return [n >> 16 & 255, n >> 8 & 255, n & 255];
};
const hexOf = (c) => `#${c.map((v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0")).join("")}`;

// Como followGlass de Kaint: 'color' de la base llevado al color de las ventanas 'glass', aclarado
// (hacia el blanco) u oscurecido (hacia el negro) en la proporción en que 'color' lo está respecto
// al cristal de la base; así toma el tono del cristal propio y no le queda el azul de la base.
function followGlass(color, baseGlass, glass) {
    const c = rgbOf(color), g = rgbOf(baseGlass), to = rgbOf(glass);
    const lighter = c[0] + c[1] + c[2] >= g[0] + g[1] + g[2];
    let t = 0;
    for (let i = 0; i < 3; i++)
        t += lighter ? (g[i] >= 255 ? 0 : (c[i] - g[i]) / (255 - g[i])) : (g[i] <= 0 ? 0 : (g[i] - c[i]) / g[i]);
    t = Math.max(0, Math.min(1, t / 3));
    return hexOf(to.map((v) => lighter ? v + t * (255 - v) : v * (1 - t)));
}

// Los colores del personalizado con base 'name' y los propios 'colors': con Windows 7 y un color de
// las ventanas propio, lo que en Windows 7 es de ese color (la franja de los menús y las cabeceras
// de las ventanas flotantes) lo toma, salvo lo que tenga su propio color (como Theme::resolved).
export function customColors(name, colors = {}) {
    const b = CustomBases[name] || CustomBases.dark;
    const defaults = baseColors(name), out = { ...defaults, ...colors };
    const glass = colors.glass;
    if (b.win7 && /^#[0-9a-f]{6}$/i.test(glass || "") && glass.toLowerCase() !== defaults.glass)
        for (const key of ["menuBar", "panelTitle", "panelTitleEnd"])
            if (!colors[key])
                out[key] = followGlass(defaults[key], defaults.glass, glass);
    // Con una cabecera propia sin final propio, de un solo color.
    if (colors.panelTitle && !colors.panelTitleEnd)
        out.panelTitleEnd = colors.panelTitle;
    return out;
}

// Los colores de ventana de Windows 7 (aproximados), como Theme::win7Colors de Kaint.
export const Win7WindowColors = [
    ["Cielo", "#74b8fc"], ["Crepúsculo", "#2d5fa8"], ["Mar", "#32b4b4"], ["Hoja", "#3c9a2e"], ["Lima", "#9ccf3c"],
    ["Sol", "#f2cf2a"], ["Calabaza", "#f39a1e"], ["Rubor", "#ee7fae"], ["Rubí", "#c8282c"], ["Fucsia", "#d63ca8"],
    ["Violeta", "#8a4fc4"], ["Lavanda", "#b1a5e2"], ["Topo", "#9a8a72"], ["Chocolate", "#6e4026"], ["Pizarra", "#6d747c"],
    ["Escarcha", "#f4f6f8"],
];

// Los temas Windows 7 con el color de las ventanas 'glass' (como withWin7Glass de Kaint): el
// cristal y lo que en Windows 7 es de ese color; con el oscuro, el color se ahúma como el cristal
// oscuro respecto al claro.
function withWin7Glass(colors, glass, dark) {
    if (!/^#[0-9a-f]{6}$/i.test(glass || ""))
        return colors;
    if (dark)
        glass = followGlass(Win7DarkColors.glass, Win7Colors.glass, glass);
    const out = { ...colors, glass };
    for (const key of ["menuBar", "panelTitle", "panelTitleEnd"])
        out[key] = followGlass(colors[key], colors.glass, glass);
    return out;
}

// El cristal de los marcos de las ventanas (como Theme::glassColor de Kaint): el de siempre,
// llevado al tono del color de las ventanas.
function glassFrame(glass, dark) {
    const base = dark ? Win7DarkColors.glass : Win7Colors.glass;
    const variant = dark ? "#1c2430" : "#4580c4";
    return followGlass(variant, base, glass);
}

// Esquema guardado → { base: "light" | "dark" | "win98", colors, estilo... }.
export function resolveScheme(scheme, systemDark) {
    if (scheme === 11 || scheme === 12)
        return { base: scheme === 12 ? "dark" : "light", colors: win8Colors(scheme === 12, Settings.get("win8Color")), win8: true };
    if (scheme === 13 || scheme === 14)
        return { base: scheme === 14 ? "dark" : "light", colors: scheme === 14 ? Win7BasicDarkColors : Win7BasicColors, win7: true, basic: true };
    if (LinuxSchemes[scheme]) {
        const dark = scheme % 2 === 0, desktop = LinuxSchemes[scheme];
        return { base: dark ? "dark" : "light", colors: linuxColors(desktop, dark), linux: desktop };
    }
    if (scheme === 4)
        return { base: "win98" };
    if (scheme === 10)
        return { base: "dark", colors: withWin7Glass(Win7DarkColors, Settings.get("win7Glass"), true), win7: true };
    if (scheme === 9)
        return { base: "win98", colors: Win98AltColors, alt: true };
    if (scheme === 8)
        return { base: "light", colors: XpColors, xp: true };
    if (scheme === 7)
        return { base: "light", colors: withWin7Glass(Win7Colors, Settings.get("win7Glass"), false), win7: true };
    if (scheme === 6)
        return { base: "light", colors: BlueColors, blue: true };
    if (scheme === 5) {
        const custom = Settings.get("customTheme") || {};
        const b = CustomBases[custom.base] || CustomBases.dark;
        // Con Windows 7 y sin cristal propio, el «Color de las ventanas» de Configuración (como Kaint).
        let own = custom.colors || {};
        const glass = Settings.get("win7Glass");
        if (b.win7 && !own.glass && /^#[0-9a-f]{6}$/i.test(glass || ""))
            own = { ...own, glass: b.base === "dark" ? followGlass(Win7DarkColors.glass, Win7Colors.glass, glass) : glass };
        const colors = b.win7 ? customColors(CustomBases[custom.base] ? custom.base : "dark", own)
                              : { ...b.colors, ...own };
        return { base: b.base, colors, win7: !!b.win7, xp: !!b.xp, alt: !!b.alt, custom: true };
    }
    return { base: scheme === 2 || (scheme === 0 && systemDark) ? "dark" : "light" };
}

// Botones de las ventanas (Configuración > Interfaz de usuario), como los de la barra de título de
// Kaint: Windows 10 (los predeterminados), 11, macOS, 7, XP o 98, a la derecha o a la izquierda. Con
// los temas Windows 98, 7 y XP, «Windows 10» son los suyos.
// (Los mismos números que WindowFrame::Buttons de Kaint: Windows 8 y GNOME van después de «Auto»,
// el 6, que aquí es «Windows 10».)
export const WindowButtons = ["win10", "win11", "macos", "win7", "xp", "win98", "win10", "win8", "gnome"];
export function applyWindowButtons(root) {
    let style = WindowButtons[Math.max(0, Math.min(WindowButtons.length - 1, Number(Settings.get("titleButtons")) || 0))];
    if (style === "win10")
        style = root.classList.contains("classic") ? "win98" : root.classList.contains("win7") ? "win7" : root.classList.contains("xp") ? "xp"
              : root.classList.contains("win8") ? "win8" : root.classList.contains("linux") ? "gnome" : style;
    for (const name of WindowButtons)
        root.classList.toggle(`btn-${name}`, name === style);
    root.classList.toggle("btn-left", !!Settings.get("titleButtonsLeft"));
    // Windows 7: la X de las ventanas unida al borde de arriba, en un rectángulo o en un cuadrado.
    const close7 = Number(Settings.get("win7PanelClose") ?? 1);
    root.classList.toggle("btn-win7-hanging", style === "win7" && close7 === 0);
    root.classList.toggle("btn-win7-square", style === "win7" && close7 === 2);
}

// Aplica el tema: clases del documento (oscuro, clásico) y, con Windows 98 y el personalizado, los
// colores en las variables de CSS.
export function applyThemeColors(root, scheme, systemDark) {
    const { base, colors, blue, win7, xp, alt, win8, basic, linux } = resolveScheme(scheme, systemDark);
    const classic = base === "win98";
    root.classList.toggle("dark", base === "dark");
    root.classList.toggle("classic", classic);
    root.classList.toggle("blue", !!blue);
    root.classList.toggle("win7", !!win7);
    root.classList.toggle("win7basic", !!basic);
    root.classList.toggle("win8", !!win8);
    root.classList.toggle("linux", !!linux);
    for (const desktop of Object.keys(LinuxDesktops))
        root.classList.toggle(`desk-${desktop}`, linux === desktop);
    root.classList.toggle("xp", !!xp);
    root.classList.toggle("alt", !!alt);
    // Windows 8: el color de las ventanas (el marco y las cabeceras); los de Linux, su letra y su redondeo.
    if (win8)
        root.style.setProperty("--win8", colors.win8);
    else
        root.style.removeProperty("--win8");
    if (linux) {
        root.style.setProperty("--linux-font", `"${LinuxDesktops[linux].font}"`);
        root.style.setProperty("--linux-radius", `${LinuxDesktops[linux].radius}px`);
    } else {
        root.style.removeProperty("--linux-font");
        root.style.removeProperty("--linux-radius");
    }
    applyDesign(root, scheme === 5 ? Settings.get("customTheme")?.design || null : null);
    applyWindowButtons(root);
    for (const [key, cssVar] of ColorRoles) {
        const value = colors?.[key] || (classic ? Presets.win98[key] : null);
        if (value)
            root.style.setProperty(cssVar, value);
        else
            root.style.removeProperty(cssVar);
    }
    // Windows 7: el cristal de los marcos, del tono del color de las ventanas (Basic: opaco, el suyo).
    if (win7 && colors?.glass && !basic)
        root.style.setProperty("--glass-frame", glassFrame(colors.glass, base === "dark"));
    else
        root.style.removeProperty("--glass-frame");
    // Como el color de énfasis en las barras de título de Windows (y el color propio de Kaint en
    // Linux): con los temas de Paint.NET, la barra superior y las cabeceras de las ventanas lo llevan
    // (Windows 98, XP y 7 tienen las suyas: con Windows 7, el título salía blanco sobre el cristal).
    const title = Settings.get("titleBarColor");
    if (title && /^#[0-9a-f]{6}$/i.test(title) && (scheme === 0 || scheme === 1 || scheme === 2 || scheme === 6)) {
        const n = parseInt(title.slice(1), 16), light = Math.max(n >> 16, (n >> 8) & 255, n & 255) + Math.min(n >> 16, (n >> 8) & 255, n & 255);
        for (const cssVar of ["--menubar", "--panel-title", "--panel-title-end", "--win-title"])
            root.style.setProperty(cssVar, title);
        for (const cssVar of ["--panel-title-text", "--win-title-text"])
            root.style.setProperty(cssVar, light / 2 < 128 ? "#ffffff" : "#000000");
    } else {
        root.style.removeProperty("--win-title");
        root.style.removeProperty("--win-title-text");
    }
    // Color de selección propio (temas Claro, Oscuro y Seguir al sistema, como en Kaint): el fondo
    // de lo elegido; el borde, más oscuro (claro) o más claro (oscuro), y lo de bajo el puntero, a
    // medio camino del fondo de las ventanas.
    const own = Settings.get(base === "dark" ? "selectionColorDark" : "selectionColorLight");
    const ownSelection = (scheme === 0 || scheme === 1 || scheme === 2) && /^#[0-9a-f]{6}$/i.test(own || "");
    root.classList.toggle("own-selection", ownSelection);
    if (ownSelection) {
        root.style.setProperty("--selection", own);
        root.style.setProperty("--selection-border", base === "dark" ? `color-mix(in srgb, ${own} 55%, white)` : `color-mix(in srgb, ${own} 70%, black)`);
        for (const cssVar of ["--hover", "--button-hover"])
            root.style.setProperty(cssVar, `color-mix(in srgb, ${own} 45%, var(--panel))`);
    } else {
        // (--selection, --selection-border y --hover los vuelve a poner o quitar el bucle de arriba.)
        root.style.removeProperty("--button-hover");
    }
    // Como en Kaint (Configuración > Lienzo): el fondo alrededor de la imagen puede tener su color.
    const border = Settings.get(base === "dark" ? "canvasBorderDark" : "canvasBorderLight");
    if (border)
        root.style.setProperty("--workspace", border);
    return base;
}

// --- Exportar e importar el personalizado (el mismo formato que Kaint: Theme::exportTheme) -----
// theme.json con las marcas de Kaint (dark, win7, xp, classic), los colores cambiados y los ajustes
// del fondo, más la base de la web; con fondo, en un .zip junto al archivo (background.mp4...).

// El tema de la web (Settings.customTheme) → el JSON común.
// El diseño del tema personalizado (como el de Kaint: Theme::Spec): botones, espaciado, redondeo,
// opacidad de las ventanas translúcidas, iconos, fuente, bordes clásicos y texto sin suavizado.
export const DesignDefaults = { buttonStyle: 0, density: 1, radius: 0, panelOpacity: 75, iconSize: 16, toolIconSize: 22,
                                fontFamily: "", fontSize: 0, classic: false, pixelFont: false };

export function readDesign(json) {
    const num = (v, fallback, lo, hi) => (Number.isFinite(Number(v)) ? Math.max(lo, Math.min(hi, Math.round(Number(v)))) : fallback);
    return {
        buttonStyle: num(json?.buttonStyle, 0, 0, 2), density: num(json?.density, 1, 0, 2), radius: num(json?.radius, 0, 0, 16),
        panelOpacity: num(json?.panelOpacity, 75, 20, 100), iconSize: num(json?.iconSize, 16, 12, 48), toolIconSize: num(json?.toolIconSize, 22, 12, 48),
        fontFamily: typeof json?.fontFamily === "string" ? json.fontFamily.replace(/["\\<>]/g, "").slice(0, 80) : "",
        fontSize: num(json?.fontSize, 0, 0, 32), classic: !!json?.classic, pixelFont: !!json?.pixelFont,
    };
}

// Aplica el diseño del tema personalizado (o lo quita con otro tema).
export function applyDesign(root, design) {
    const d = design ? readDesign(design) : null;
    const set = (name, value) => (value === null ? root.style.removeProperty(name) : root.style.setProperty(name, value));
    set("--radius", d && d.radius ? `${d.radius}px` : null);
    set("--panel-opacity", d ? String(d.panelOpacity / 100) : null);
    set("--bar-icon", d && d.iconSize !== 16 ? `${d.iconSize}px` : null);
    set("--tool-icon", d && d.toolIconSize !== 22 ? `${d.toolIconSize}px` : null);
    set("--theme-font", d && d.fontFamily ? `"${d.fontFamily}"` : null);
    set("--theme-font-size", d && d.fontSize ? `${Math.round((d.fontSize * 96) / 72)}px` : null);
    root.classList.toggle("theme-btn-framed", !!d && d.buttonStyle === 1);
    root.classList.toggle("theme-btn-rounded", !!d && d.buttonStyle === 2);
    root.classList.toggle("density-compact", !!d && d.density === 0);
    root.classList.toggle("density-comfortable", !!d && d.density === 2);
    root.classList.toggle("theme-bar-icons", !!d && d.iconSize !== 16);
    root.classList.toggle("theme-tool-icons", !!d && d.toolIconSize !== 22);
    root.classList.toggle("pixel-font", !!d && d.pixelFont);
    root.classList.toggle("theme-classic", !!d && d.classic);
}

export function themeToJson(theme, backgroundName = "") {
    const b = CustomBases[theme.base] || CustomBases.dark;
    // Con Windows 98, Kaint no tiene base para los colores sin cambiar: van todos.
    const colors = b.base === "win98" ? { ...baseColors(theme.base), ...(theme.colors || {}) } : { ...(theme.colors || {}) };
    const img = theme.image || {};
    return {
        format: "kaint-theme", version: 1, base: CustomBases[theme.base] ? theme.base : "dark",
        dark: b.base === "dark", win7: !!b.win7, xp: !!b.xp, classic: b.base === "win98", colors,
        workspaceImage: backgroundName,
        imageMode: Number(img.mode ?? 0), imageOpacity: Number(img.opacity ?? 100), imageScale: Number(img.scale ?? 100),
        imageAnchor: Number(img.anchor ?? 4), imageX: Number(img.x ?? -1), imageY: Number(img.y ?? -1),
        imageBlur: Number(img.blur ?? 0), imageGrayscale: !!img.grayscale,
        imageTint: img.tint && Number(img.tintStrength) > 0 ? img.tint : "", imageTintStrength: Number(img.tintStrength ?? 0),
        // El diseño (los mismos campos que Kaint para el escritorio).
        ...(() => {
            const d = readDesign(theme.design || {});
            return { ...d, classic: b.base === "win98" || d.classic };
        })(),
    };
}

// El JSON común (de Kaint o de la web) → { base, colors, image (los ajustes, sin el archivo) }.
export function themeFromJson(json) {
    let base = CustomBases[json?.base] ? json.base : null;
    if (!base) {
        const window = String(json?.colors?.window || "").toLowerCase();
        base = json?.classic ? (window.endsWith("d4d0c8") ? "win98alt" : "win98") : json?.xp ? "xp"
            : json?.win7 ? (json?.dark === false ? "win7" : "win7dark") : json?.dark === false ? "light" : "dark";
    }
    // Solo los que cambian respecto a la base (los demás la siguen), en #rrggbb.
    const defaults = baseColors(base), colors = {};
    for (const [key] of ColorRoles) {
        let v = String(json?.colors?.[key] || "").toLowerCase();
        if (/^#[0-9a-f]{8}$/.test(v))
            v = `#${v.slice(3)}`;
        if (/^#[0-9a-f]{6}$/.test(v) && v !== String(defaults[key] || "").toLowerCase())
            colors[key] = v;
    }
    const num = (v, fallback, lo, hi) => (Number.isFinite(Number(v)) ? Math.max(lo, Math.min(hi, Number(v))) : fallback);
    const tint = /^#[0-9a-f]{6}$/i.test(json?.imageTint || "") ? json.imageTint : "#000000";
    const image = { mode: num(json?.imageMode, 0, 0, 4), anchor: num(json?.imageAnchor, 4, 0, 8),
                    // (Los temas de antes no llevan la alineación propia: -1, la de su posición.)
                    x: num(json?.imageX ?? -1, -1, -1, 100), y: num(json?.imageY ?? -1, -1, -1, 100), scale: num(json?.imageScale, 100, 10, 400),
                    opacity: num(json?.imageOpacity, 100, 0, 100), blur: num(json?.imageBlur, 0, 0, 40), grayscale: !!json?.imageGrayscale,
                    tint, tintStrength: num(json?.imageTintStrength, 0, 0, 100) };
    const design = readDesign(json);
    // Con la base Windows 98, «classic» es la suya: no cuenta como opción del diseño.
    if (CustomBases[base]?.base === "win98")
        design.classic = false;
    return { base, colors, image, design };
}

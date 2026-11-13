// Temas de Kaint (src/app/Theme.cpp): claro, oscuro y azul de Paint.NET, Windows 7 y Windows 7
// oscuro (con los de 7.css), Windows XP, Windows 98 (con los colores de 98.css y botones en relieve)
// y el personalizado, que parte de uno de ellos (también de Windows 7, claro u oscuro, o de XP, con
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

// Esquema guardado → { base: "light" | "dark" | "win98", colors, estilo... }.
export function resolveScheme(scheme, systemDark) {
    if (scheme === 4)
        return { base: "win98" };
    if (scheme === 10)
        return { base: "dark", colors: Win7DarkColors, win7: true };
    if (scheme === 9)
        return { base: "win98", colors: Win98AltColors, alt: true };
    if (scheme === 8)
        return { base: "light", colors: XpColors, xp: true };
    if (scheme === 7)
        return { base: "light", colors: Win7Colors, win7: true };
    if (scheme === 6)
        return { base: "light", colors: BlueColors, blue: true };
    if (scheme === 5) {
        const custom = Settings.get("customTheme") || {};
        const b = CustomBases[custom.base] || CustomBases.dark;
        return { base: b.base, colors: { ...b.colors, ...(custom.colors || {}) }, win7: !!b.win7, xp: !!b.xp, alt: !!b.alt, custom: true };
    }
    return { base: scheme === 2 || (scheme === 0 && systemDark) ? "dark" : "light" };
}

// Botones de las ventanas (Configuración > Interfaz de usuario), como los de la barra de título de
// Kaint: Windows 10 (los predeterminados), 11, macOS, 7, XP o 98, a la derecha o a la izquierda. Con
// los temas Windows 98, 7 y XP, «Windows 10» son los suyos.
export const WindowButtons = ["win10", "win11", "macos", "win7", "xp", "win98"];
export function applyWindowButtons(root) {
    let style = WindowButtons[Math.max(0, Math.min(5, Number(Settings.get("titleButtons")) || 0))];
    if (style === "win10")
        style = root.classList.contains("classic") ? "win98" : root.classList.contains("win7") ? "win7" : root.classList.contains("xp") ? "xp" : style;
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
    const { base, colors, blue, win7, xp, alt } = resolveScheme(scheme, systemDark);
    const classic = base === "win98";
    root.classList.toggle("dark", base === "dark");
    root.classList.toggle("classic", classic);
    root.classList.toggle("blue", !!blue);
    root.classList.toggle("win7", !!win7);
    root.classList.toggle("xp", !!xp);
    root.classList.toggle("alt", !!alt);
    applyWindowButtons(root);
    for (const [key, cssVar] of ColorRoles) {
        const value = colors?.[key] || (classic ? Presets.win98[key] : null);
        if (value)
            root.style.setProperty(cssVar, value);
        else
            root.style.removeProperty(cssVar);
    }
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
        imageAnchor: Number(img.anchor ?? 4), imageBlur: Number(img.blur ?? 0), imageGrayscale: !!img.grayscale,
        imageTint: img.tint && Number(img.tintStrength) > 0 ? img.tint : "", imageTintStrength: Number(img.tintStrength ?? 0),
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
    const image = { mode: num(json?.imageMode, 0, 0, 4), anchor: num(json?.imageAnchor, 4, 0, 8), scale: num(json?.imageScale, 100, 10, 400),
                    opacity: num(json?.imageOpacity, 100, 0, 100), blur: num(json?.imageBlur, 0, 0, 40), grayscale: !!json?.imageGrayscale,
                    tint, tintStrength: num(json?.imageTintStrength, 0, 0, 100) };
    return { base, colors, image };
}

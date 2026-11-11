// Temas de Kaint (src/app/Theme.cpp): claro, oscuro y azul de Paint.NET, Windows 7 (con los de
// 7.css), Windows 98 (con los colores de 98.css y botones en relieve) y el personalizado, que parte de uno de ellos y cambia sus
// colores.
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
                     selection: "#d5e6fb", selectionBorder: "#7da2ce", hover: "#e8f2fd", workspace: "#c8d4e3" };

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

// Esquema guardado → { base: "light" | "dark" | "win98", custom }.
export function resolveScheme(scheme, systemDark) {
    if (scheme === 4)
        return { base: "win98" };
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
        return { base: Presets[custom.base] ? custom.base : "dark", colors: custom.colors || {} };
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
    // Linux): con los temas de Paint.NET, la barra superior y las cabeceras de las ventanas lo llevan.
    const title = Settings.get("titleBarColor");
    if (title && /^#[0-9a-f]{6}$/i.test(title) && (scheme === 0 || scheme === 1 || scheme === 2 || scheme === 6 || scheme === 7)) {
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

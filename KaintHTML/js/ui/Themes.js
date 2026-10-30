// Temas de Kaint (src/app/Theme.cpp): claro, oscuro y azul de Paint.NET, Windows 98 (con los
// colores de 98.css y botones en relieve) y el personalizado, que parte de uno de ellos y cambia sus
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

// Esquema guardado → { base: "light" | "dark" | "win98", custom }.
export function resolveScheme(scheme, systemDark) {
    if (scheme === 4)
        return { base: "win98" };
    if (scheme === 6)
        return { base: "light", colors: BlueColors, blue: true };
    if (scheme === 5) {
        const custom = Settings.get("customTheme") || {};
        return { base: Presets[custom.base] ? custom.base : "dark", colors: custom.colors || {} };
    }
    return { base: scheme === 2 || (scheme === 0 && systemDark) ? "dark" : "light" };
}

// Aplica el tema: clases del documento (oscuro, clásico) y, con Windows 98 y el personalizado, los
// colores en las variables de CSS.
export function applyThemeColors(root, scheme, systemDark) {
    const { base, colors, blue } = resolveScheme(scheme, systemDark);
    const classic = base === "win98";
    root.classList.toggle("dark", base === "dark");
    root.classList.toggle("classic", classic);
    root.classList.toggle("blue", !!blue);
    for (const [key, cssVar] of ColorRoles) {
        const value = colors?.[key] || (classic ? Presets.win98[key] : null);
        if (value)
            root.style.setProperty(cssVar, value);
        else
            root.style.removeProperty(cssVar);
    }
    // Como en Kaint (Configuración > Lienzo): el fondo alrededor de la imagen puede tener su color.
    const border = Settings.get(base === "dark" ? "canvasBorderDark" : "canvasBorderLight");
    if (border)
        root.style.setProperty("--workspace", border);
    return base;
}

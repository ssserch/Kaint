// Preferencias (como AppSettings de Kaint), guardadas en el navegador.
const KEY = "kaint.settings";

const defaults = {
    animations: true,
    translucentPanels: true,
    // Desplazamiento más allá del borde de la imagen, desplazamiento automático al dibujar en el
    // borde y elegir la capa visible más cercana al ocultar una (como en Kaint).
    overscroll: true,
    autoScroll: true,
    autoSelectVisibleLayer: false,
    coloredToolIcons: true,
    colorScheme: 0, // 0: seguir al sistema · 1: claro · 2: oscuro
    language: navigator.language?.toLowerCase().startsWith("es") ? "es" : "en",
    dropShadow: true,
    checkerBrightness: 191,
    askOnceOnClose: true,
    // Los píxeles pegados o movidos reemplazan a los de debajo, también los transparentes.
    replacePixels: true,
    // Ventanas flotantes y diálogos: 0, como los de paint.js (Windows 10); 1, compactos. Grosor
    // del borde en píxeles.
    panelStyle: 0,
    panelBorder: 1,
    // Ver > Reglas y su estilo: 0, oscura como la de paint.js; 1, del color de la barra.
    rulers: false,
    rulerStyle: 0,
    // Atajos cambiados en Configuración > Atajos de teclado (id de la acción → combinación).
    shortcuts: {},
    // Herramienta al abrir Kaint ("" = la última que se usó).
    startTool: "",
    lastTool: "paintbrush",
    // Lápiz y tableta: la presión cambia el tamaño; el extremo borrador activa la Goma.
    penPressure: true,
    tabletEraser: true,
    // Color propio del fondo alrededor de la imagen con los temas claros y con los oscuros.
    canvasBorderLight: null,
    canvasBorderDark: null,
    // Tema personalizado: { base: "light" | "dark" | "win98", colors: { rol: "#rrggbb" } }.
    customTheme: null,
};

function load() {
    try {
        return { ...defaults, ...JSON.parse(localStorage.getItem(KEY) || "{}") };
    } catch {
        return { ...defaults };
    }
}

export const Settings = {
    values: load(),

    get(key) {
        return this.values[key];
    },

    set(key, value) {
        this.values[key] = value;
        try {
            localStorage.setItem(KEY, JSON.stringify(this.values));
        } catch {
            // Sin almacenamiento (modo privado): la preferencia dura hasta cerrar la página.
        }
        window.dispatchEvent(new CustomEvent("kaint-settings", { detail: { key, value } }));
    },
};

// Datos pequeños que se recuerdan entre sesiones (posición de las ventanas, paleta...).
export function remember(key, value) {
    try {
        if (value === undefined)
            return JSON.parse(localStorage.getItem(`kaint.${key}`) ?? "null");
        localStorage.setItem(`kaint.${key}`, JSON.stringify(value));
    } catch {
        // Sin almacenamiento.
    }
    return value;
}

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
    // Capas: al arrastrar una, se mueve en la lista con el ratón; con esto, se queda en su sitio
    // y una línea marca dónde quedará.
    layerDropLine: false,
    // Cursores con la herramienta en miniatura al lado: el puntero del sistema o el de Kaint.
    systemPointer: true,
    // Aspecto de la interfaz: 0, el de Kaint; 1, la recreación de Paint.NET (solo lo visual).
    uiStyle: 0,
    // Se mostró la bienvenida de la primera vez.
    welcomed: false,
    // Subrayar siempre las letras de acceso de los menús (si no, como en Windows, solo con Alt).
    menuMnemonics: false,
    colorScheme: 0, // 0: seguir al sistema · 1: claro · 2: oscuro · 4: Windows 98 · 5: personalizado · 6: azul
    language: navigator.language?.toLowerCase().startsWith("es") ? "es" : "en",
    dropShadow: true,
    checkerBrightness: 191,
    askOnceOnClose: true,
    // Al iniciar, volver a abrir los archivos de la última sesión; copias de recuperación de las
    // imágenes sin guardar cada tantos minutos (0: nunca); memoria máxima del historial de cada
    // imagen en MB (0: sin límite).
    reopenLast: false,
    autosaveMinutes: 5,
    historyLimitMB: 4096,
    // Los píxeles pegados o movidos reemplazan a los de debajo, también los transparentes.
    replacePixels: true,
    // Herramientas Mover: ocultar el contorno y los tiradores mientras se arrastra.
    hideMoveBox: true,
    // Ventanas flotantes y diálogos: 0, como los de paint.js (Windows 10); 1, compactos. Grosor
    // del borde en píxeles.
    panelStyle: 0,
    panelBorder: 1,
    // Ver > Reglas y su estilo: 0, oscura como la de paint.js; 1, del color de la barra.
    rulers: false,
    rulerStyle: 0,
    historyJumpButtons: true,
    paletteTranslucent: true,
    cursorStyle: 0,
    // Unidades de medida (Ver): 0, píxeles; 1, pulgadas; 2, centímetros.
    unit: 0,
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

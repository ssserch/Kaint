// Preferencias (como AppSettings de Kaint), guardadas en el navegador.
const KEY = "kaint.settings";

const defaults = {
    animations: true,
    translucentPanels: true,
    coloredToolIcons: true,
    colorScheme: 0, // 0: seguir al sistema · 1: claro · 2: oscuro
    language: navigator.language?.toLowerCase().startsWith("es") ? "es" : "en",
    dropShadow: true,
    checkerBrightness: 191,
    askOnceOnClose: true,
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

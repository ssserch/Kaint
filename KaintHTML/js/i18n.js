// Traducciones: los textos están en español, como en el código de Kaint, y el inglés (el idioma
// predeterminado) sale de translations/kaint_en.ts (js/i18n/en.js) más los textos propios de la
// web (extra).
import en from "./i18n/en.js";

const extra = {
    "Arrastra aquí una imagen o ábrela desde Archivo.": "Drop an image here or open it from File.",
    "Descargar": "Download",
    "Sin título": "Untitled",
    "Imagen nueva": "New Image",
    "Abrir imagen": "Open Image",
    "No se pudo abrir «%1».": "Could not open “%1”.",
    "No se pudo leer el portapapeles. Pega con Ctrl+V.": "Could not read the clipboard. Paste with Ctrl+V.",
    "Formato:": "Format:",
    "Calidad:": "Quality:",
    "El navegador reserva este atajo; usa el menú.": "The browser reserves this shortcut; use the menu.",
    "Kaint para la web: el editor de imágenes al estilo Paint.NET, en el navegador.":
        "Kaint for the web: the Paint.NET-style image editor, in your browser.",
    "Versión web de Kaint": "Kaint web version",
    "Pantalla completa": "Full Screen",
    "Zum a: %1, %2 · %3 × %4": "Zoom to: %1, %2 · %3 × %4",
    "Pegar desde el portapapeles": "Paste from Clipboard",
    "Los cambios de idioma se aplican al recargar la página.": "Language changes apply when the page reloads.",
    "Recargar": "Reload",
    "Ctrl": "Ctrl",
    "Mayús": "Shift",
    "Supr": "Del",
    "RePág": "PgUp",
    "AvPág": "PgDn",
    "Entrada: negro": "Input: black",
    "Entrada: blanco": "Input: white",
    "Salida: negro": "Output: black",
    "Salida: blanco": "Output: white",
    "Reiniciar": "Reseed",
    "Semilla aleatoria:": "Random seed:",
    "Imágenes: %1": "Images: %1",
    "Pulsa un atajo…": "Press a shortcut…",
    "Quitar el atajo": "Remove the shortcut",
};

let language = "en";

export function setLanguage(lang) {
    language = lang === "es" ? "es" : "en";
    document.documentElement.lang = language;
}

export function currentLanguage() {
    return language;
}

// Texto traducido, con los marcadores %1, %2... sustituidos por los argumentos.
export function tr(text, ...args) {
    let out = text;
    if (language === "en")
        out = en[text] ?? extra[text] ?? text;
    args.forEach((a, i) => {
        out = out.split(`%${i + 1}`).join(String(a));
    });
    return out;
}

// Sin el "&" de las teclas de acceso de los menús (para títulos y ayudas).
export function plain(text) {
    return text.replace(/&(.)/g, "$1");
}

// El texto de un menú con la tecla de acceso subrayada (HTML seguro).
export function mnemonicHtml(text) {
    const escape = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" })[c]);
    let html = "";
    for (let i = 0; i < text.length; ++i) {
        if (text[i] === "&" && i + 1 < text.length) {
            html += `<u>${escape(text[i + 1])}</u>`;
            ++i;
        } else {
            html += escape(text[i]);
        }
    }
    return html;
}

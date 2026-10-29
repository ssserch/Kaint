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
    "El idioma se aplica al recargar la página.": "The language is applied when the page reloads.",
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
    "%1 debe llamar a paintDotJs.registerPlugin() una vez.": "%1 must call paintDotJs.registerPlugin() once.",
    "%1 efectos · %2 formatos": "%1 effects · %2 file types",
    "%1 no es válido.": "%1 is not valid.",
    "%1: render() debe ser síncrono.": "%1: render() must be synchronous.",
    "%1: render() devolvió un valor que no vale.": "%1: render() returned an unsupported value.",
    "%1: renderPixel() debe devolver cuatro canales.": "%1: renderPixel() must return four channels.",
    "Activado": "Enabled",
    "Al dar la vuelta al lápiz se activa la herramienta Goma de borrar, y al volver a la punta, la herramienta de antes.": "Turning the pen over selects the Eraser, and turning it back selects the previous tool.",
    "Almacenamiento": "Storage",
    "Archivo no válido en el complemento: %1": "Invalid file in the plugin: %1",
    "Archivos del sistema": "System files",
    "Añade fuentes para la herramienta Texto (.ttf, .otf, .woff). Se guardan en este navegador y solo las usa Kaint.": "Add fonts for the Text tool (.ttf, .otf, .woff). They are stored in this browser and only Kaint uses them.",
    "Basado en:": "Based on:",
    "Buscando…": "Checking…",
    "Cargando el catálogo…": "Loading the catalog…",
    "Complementos de efectos y formatos de archivo (.paintjs). Se guardan en este navegador. Tienen el mismo acceso que Kaint: instala solo los que sean de confianza.": "Effect and file type plugins (.paintjs). They are stored in this browser. They have the same access as Kaint: only install plugins you trust.",
    "Control desconocido: %1": "Unknown control: %1",
    "Desconocido": "Unknown",
    "Desinstalar el complemento elegido": "Uninstall the selected plugin",
    "El catálogo de complementos": "The plugin catalog",
    "El catálogo está vacío.": "The catalog is empty.",
    "El catálogo o el complemento pasa del tamaño máximo.": "The catalog or the plugin exceeds the size limit.",
    "El complemento debe tener plugin.json y plugin.js en la raíz.": "The plugin must have plugin.json and plugin.js at its root.",
    "El complemento descargado no coincide con el del catálogo: no se instaló nada.": "The downloaded plugin doesn't match the catalog: nothing was installed.",
    "El complemento no coincide con el del catálogo.": "The plugin doesn't match the catalog.",
    "El efecto %1 debe definir render() o renderPixel().": "Effect %1 must define render() or renderPixel().",
    "El formato %1 debe definir decode() o encode().": "File type %1 must define decode() or encode().",
    "El formato %1 no tiene extensiones válidas.": "File type %1 has no valid extensions.",
    "El formato del catálogo no es compatible.": "The catalog format is not supported.",
    "El identificador del complemento": "The plugin ID",
    "El identificador del efecto": "The effect ID",
    "El identificador del formato": "The file type ID",
    "El navegador admite lápices y tabletas.": "The browser supports pens and tablets.",
    "Esta versión de Kaint admite la versión %1 de la API de complementos.": "This version of Kaint supports version %1 of the plugin API.",
    "Explorar complementos": "Browse Plugins",
    "Explorar complementos…": "Browse plugins…",
    "Falta la imagen del complemento: %1": "The plugin asset is missing: %1",
    "Guardar la paleta en un archivo…": "Save the palette to a file…",
    "Hay una versión nueva: %1. Recarga la página para usarla.": "There is a new version: %1. Reload the page to use it.",
    "Idioma": "Language",
    "Instalado": "Installed",
    "Instalar": "Install",
    "Instalar complemento…": "Install plugin…",
    "Instalar un complemento (.paintjs)": "Install a plugin (.paintjs)",
    "Kaint está al día.": "Kaint is up to date.",
    "La versión web se actualiza sola: al recargar la página se usa la última publicada.": "The web version updates itself: reloading the page uses the latest published version.",
    "Memoria": "Memory",
    "Navegador": "Browser",
    "No": "No",
    "No se pudo cargar el catálogo: %1": "Could not load the catalog: %1",
    "No se pudo comprobar. Inténtalo más tarde.": "Could not check. Try again later.",
    "No se pudo descargar (HTTP %1).": "Download failed (HTTP %1).",
    "No se pudo ejecutar %1.": "%1 could not be run.",
    "Otro complemento ya abre los archivos %1.": "Another plugin already opens %1 files.",
    "Plataforma": "Platform",
    "Procesadores lógicos": "Logical processors",
    "Se instaló «%1».": "“%1” was installed.",
    "Sí": "Yes",
    "Un complemento debe añadir al menos un efecto o un formato.": "A plugin must add at least one effect or file type.",
    "Un complemento no puede ocupar más de 5 MB.": "A plugin can't be larger than 5 MB.",
    "Ya se está cargando otro complemento.": "Another plugin is already loading.",
    "plugin.json no es válido: %1": "plugin.json is not valid: %1",
    "¿Quieres quitar este complemento?": "Do you want to remove this plugin?",
    "Pantalla": "Screen",
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

// Como tr() para un texto que se repite con otro sentido ('comment', el de tr(texto, comentario)
// en Kaint): "V:" es el verde en RGB y el valor en MSV.
export function trc(comment, text, ...args) {
    const key = `${comment}\u0004${text}`;
    if (language !== "en" || !(key in en))
        return tr(text, ...args);
    let out = en[key];
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

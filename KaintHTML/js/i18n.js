// Traducciones: los textos están en español, como en el código de Kaint, y el inglés (el idioma
// predeterminado) sale de translations/kaint_en.ts (js/i18n/en.js) más los textos propios de la
// web (extra).
import en from "./i18n/en.js";

const extra = {
    "Archivo": "File",
    "Los colores de la base, sin cambios.": "The base colors, unchanged.",
    "No se pudo guardar el fondo: %1": "The background couldn't be saved: %1",
    "Vídeo: se reproduce en bucle y sin sonido.": "Video: it plays in a loop, without sound.",
    "Con fondo propio, un .zip con el tema y el fondo; si no, un .json. Se abre también en Kaint para el escritorio.":
        "With your own background, a .zip with the theme and the background; otherwise, a .json. It also opens in Kaint for the desktop.",
    "No se pudo exportar el tema: %1": "The theme couldn't be exported: %1",
    "Un tema de Kaint o de Kaint.HTML (.zip con fondo o .json).": "A Kaint or Kaint.HTML theme (.zip with a background, or .json).",
    "La imagen es demasiado grande (%1 × %2 píxeles).": "The image is too large (%1 × %2 pixels).",
    "La capa «%1» usa un modo de fusión que Photoshop no tiene. Cámbialo en sus propiedades o guarda la imagen como PDN u ORA.":
        "The “%1” layer uses a blend mode that Photoshop does not have. Change it in its properties or save the image as PDN or ORA.",
    "Este navegador no puede abrir imágenes %1. Prueba con Safari o con Kaint para el escritorio.":
        "This browser can't open %1 images. Try Safari or Kaint for the desktop.",
    "Opciones de guardado": "Save options",
    "Tipo de zalgo": "Zalgo type",
    "Botones de las ventanas:": "Window buttons:",
    "El botón de cerrar de las ventanas y los diálogos. Con los temas Windows 7, Windows XP y Windows 98, «Windows 10» usa el del tema.":
        "The close button of the windows and dialogs. With the Windows 7, Windows XP and Windows 98 themes, “Windows 10” uses the theme's own.",
    "Windows 11": "Windows 11",
    "%1 de %2 efectos": "%1 of %2 effects",
    "%1 efectos": "%1 effects",
    "%1 imágenes en el GIF": "%1 images in the GIF",
    "Como capas: cada fotograma es una capa de una sola imagen. Como imágenes: cada fotograma se abre como una imagen aparte. Como una sola imagen: solo el primer fotograma. Elegir los fotogramas: cada vez, qué fotogramas abrir y cómo.":
        "As layers: each frame is a layer of a single image. As images: each frame opens as a separate image. As a single image: only the first frame. Choose the frames: each time, which frames to open and how.",
    "Capas de una imagen: un fotograma en cada capa (se puede guardar como GIF animado). Imágenes aparte: cada fotograma se abre como una imagen.":
        "Layers of one image: one frame on each layer (it can be saved as an animated GIF). Separate images: each frame opens as an image.",
    "Mostrar u ocultar sus efectos": "Show or hide its effects",
    "Opciones de la herramienta": "Tool options",
    "Al sacar una ventana, esconder las que tape": "When a window slides out, hide the ones it covers",
    "Con las ventanas en las esquinas: al sacar una, las que estaban abiertas debajo de ella se vuelven a esconder. Sin la casilla, se quedan abiertas y la última que sacaste queda delante, con su asa.": "With the windows in the corners: when one slides out, the open ones underneath it hide again. Unchecked, they stay open and the last one you slid out stays in front, with its handle.",
    "Ocultarse en las esquinas": "Hide in the corners",
    "Ocultarse en las esquinas: cada ventana se esconde en su esquina con un asa; al tocarla sale y se vuelve a esconder al tocar el lienzo o elegir una herramienta. Ventanas: las flotantes de siempre, con el tamaño que elijas.": "Hide in the corners: each window hides in its corner behind a handle; tap it to slide the window out, and it hides again when you tap the canvas or pick a tool. Windows: the usual floating windows, at the size you choose.",
    "Tamaño de cada ventana en el modo Ventanas:": "Size of each window in Windows mode:",
    "Teléfono": "Phone",
    "Ventanas en el teléfono:": "Windows on the phone:",
    "«%1» tiene %2 fotogramas. ¿Cómo quieres abrirlo?": "“%1” has %2 frames. How do you want to open it?",
    "Arrastra aquí una imagen o ábrela desde Archivo.": "Drop an image here or open it from File.",
    "Descargar": "Download",
    "Sistema de complementos basado en el de ": "Plugin system based on the one of ",
    "Ajuste de revelado con %1 valores.": "Develop preset with %1 settings.",
    "El archivo pasa de 32 MB.": "The file is larger than 32 MB.",
    "El complemento se detuvo con un error.": "The plugin stopped with an error.",
    "Este complemento solo se puede usar desde el menú Efectos.": "This plugin can only be used from the Effects menu.",
    "Instalar un complemento (.paintjs, .wasm, .cube, .3dl o .xmp)": "Install a plugin (.paintjs, .wasm, .cube, .3dl or .xmp)",
    "Usar un color personalizado para el fondo del lienzo con los temas claros": "Use a custom canvas background color with light themes",
    "Usar un color personalizado para el fondo del lienzo con los temas oscuros": "Use a custom canvas background color with dark themes",
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
    "Color anterior (izquierda) y nuevo (derecha)": "Previous color (left) and new one (right)",
    "%1 bytes": "%1 bytes",
    "Nombre:": "Name:",
    "Tipo:": "Type:",
    "Ctrl: solo tono · Alt: solo saturación · Mayús: radios de 15°": "Ctrl: hue only · Alt: saturation only · Shift: 15° steps",
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
    "%1, %2, %3, %4 y %5": "%1, %2, %3, %4 and %5",
    "Activado": "Enabled",
    "Al dar la vuelta al lápiz se activa la herramienta Goma de borrar, y al volver a la punta, la herramienta de antes.": "Turning the pen over selects the Eraser, and turning it back selects the previous tool.",
    "Almacenamiento": "Storage",
    "Archivo no válido en el complemento: %1": "Invalid file in the plugin: %1",
    "Archivos del sistema": "System files",
    "Añade fuentes para la herramienta Texto (.ttf, .otf, .woff). Se guardan en este navegador y solo las usa Kaint.": "Add fonts for the Text tool (.ttf, .otf, .woff). They are stored in this browser and only Kaint uses them.",
    "Basado en:": "Based on:",
    "Buscando…": "Checking…",
    "Cargando el catálogo…": "Loading the catalog…",
    "Complementos de efectos y formatos de archivo (%1). Se guardan en este navegador. Los .paintjs tienen el mismo acceso que Kaint: instala solo los que sean de confianza. Aparecen en Efectos > Complementos.": "Effect and file type plugins (%1). They are stored in this browser. .paintjs plugins have the same access as Kaint: only install plugins you trust. They appear in Effects > Plugins.",
    "Control desconocido: %1": "Unknown control: %1",
    "De paint.js": "From paint.js",
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
    "Filtros de PhotoCraft (sin acceso a nada de fuera)": "PhotoCraft filters (with no access to anything outside)",
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
    "Ahora no": "Not now",
    "El navegador pide permiso para volver a abrir las imágenes de la última sesión:\n%1": "The browser asks for permission to reopen the images from the last session:\n%1",
    "Empezar con una imagen nueva.": "Start with a new image.",
    "Este navegador no deja volver a abrir los archivos: solo funciona en los que tienen acceso a los archivos del sistema.": "This browser can't reopen files: it only works in browsers with access to the system's files.",
    "Pide permiso al navegador para abrirlas.": "Ask the browser for permission to open them.",
    "Volver a abrir": "Reopen",
    "Volver a abrir las imágenes": "Reopen images",
    "Formas de Paint.NET (.xaml) para la herramienta Formas: aparecen en el grupo «Formas personalizadas». Se guardan en este navegador.":
        "Paint.NET shapes (.xaml) for the Shapes tool: they appear in the “Custom shapes” group. They're saved in this browser.",
    "No hay formas personalizadas.": "There are no custom shapes.",
    "El paquete tiene cambios sin guardar. ¿Quieres descartarlos?": "The pack has unsaved changes. Do you want to discard them?",
    "Paquete guardado.": "Pack saved.",
    "la imagen": "the image",
    // Modo colaborativo (js/collab/).
    "&Hospedar la imagen actual...":
        "&Host the current image...",
    "Abriendo la sesión…":
        "Opening the session…",
    "Acceso: %1 · Nombre: %2 (se cambia en Configuración > Colaboración)":
        "Access: %1 · Name: %2 (changed in Settings > Collaboration)",
    "Agregar":
        "Add",
    "Clave para hospedar:":
        "Hosting key:",
    "Con los equipos de tu misma red (casa, oficina, clase): te encuentran en la lista de sesiones cercanas o por la dirección IP.":
        "With the computers on your own network (home, office, classroom): they find you in the list of nearby sessions or by the IP address.",
    "Copia exportada como «%1». La imagen de la sesión la guarda el anfitrión.":
        "Copy exported as “%1”. The host saves the session's image.",
    "Edita la imagen actual con otras personas: tú la hospedas y los demás se unen desde Kaint o desde Kaint para la web. Solo el anfitrión guarda la imagen; los invitados pueden exportar una copia.":
        "Edit the current image with other people: you host it and the others join from Kaint or Kaint for the web. Only the host saves the image; guests can export a copy.",
    "Ej.: 192.168.1.20 o 192.168.1.20:47913":
        "E.g. 192.168.1.20 or 192.168.1.20:47913",
    "El anfitrión guardó la imagen.":
        "The host saved the image.",
    "El anfitrión tiene cambios sin guardar.":
        "The host has unsaved changes.",
    "El servidor la muestra al arrancar; no hace falta en el mismo equipo.":
        "The server shows it when it starts; it isn't needed on the same computer.",
    "Elige una sesión de la lista o escribe la dirección del equipo que la hospeda (lo dice su ventana de la sesión).":
        "Choose a session from the list or type the address of the computer that hosts it (its session window shows it).",
    "En tiempo real también entre Kaint y Kaint para la web. Con confirmaciones, el invitado edita un borrador con su propio historial y lo sugiere; al aceptarlo, el cambio aparece para todos y en el historial del anfitrión como «Cambio hecho por…».":
        "In real time also between Kaint and Kaint for the web. With confirmations, the guest edits a draft with its own history and suggests it; when it's accepted, the change appears for everyone and in the host's history as “Change made by…”.",
    "Escribe la dirección del anfitrión.":
        "Type the host's address.",
    "Ese servidor ya tiene un anfitrión.":
        "That server already has a host.",
    "Esta página la sirve la sesión «%1» de %2 (%3).":
        "This page is served by the session “%1” of %2 (%3).",
    "Esta página se abrió con https: el navegador no deja conectarse a una sesión de la red local. Abre la dirección http:// que muestra el anfitrión, o usa P2P.":
        "This page was opened with https: the browser doesn't allow connecting to a session on the local network. Open the http:// address the host shows, or use P2P.",
    "Este navegador hospedará la imagen actual a través del servidor de Kaint para la web que abrió esta página. Los demás se unen abriendo esta misma dirección o desde Kaint.":
        "This browser will host the current image through the Kaint for the web server that opened this page. The others join by opening this same address or from Kaint.",
    "Este navegador no admite conexiones WebRTC: usa la red local (LAN).":
        "This browser doesn't support WebRTC connections: use the local network (LAN).",
    "Hospedando «%1» en la red local · %2":
        "Hosting “%1” on the local network · %2",
    "Hospedar":
        "Host",
    "Hospedar en la red local":
        "Host on the local network",
    "Hospedar la imagen actual…":
        "Host the current image…",
    "Imagen de la sesión colaborativa":
        "Collaborative session image",
    "Kaint (escritorio) la encuentra al buscar sesiones cercanas.":
        "Kaint (desktop) finds it when looking for nearby sessions.",
    "La clave para hospedar no es correcta.":
        "The hosting key is not correct.",
    "La lista la da el equipo que sirvió esta página (las sesiones que se anuncian en su red).":
        "The list comes from the computer that served this page (the sessions announced on its network).",
    "Ningún equipo todavía.":
        "No computers yet.",
    "No se pudo conectar con %1.":
        "Couldn't connect to %1.",
    "No se pudo conectar con ningún servidor de señalización.":
        "Couldn't connect to any signaling server.",
    "Nombre (opcional)":
        "Name (optional)",
    "Para hospedar en la red local desde el navegador hace falta el servidor de Kaint para la web (node tools/collab-server.mjs); por P2P no.":
        "Hosting on the local network from the browser needs the Kaint for the web server (node tools/collab-server.mjs); P2P doesn't.",
    "Para unirse: desde Kaint, Archivo > Colaboración > Red local > Unirse a una sesión, o abriendo en un navegador de la red la dirección para el navegador.":
        "To join: from Kaint, File > Collaboration > Local network > Join a session, or by opening the browser address in a browser on the network.",
    "Servidor:":
        "Server:",
    "Si dice «localhost», los demás usan la IP de este equipo en su lugar.":
        "If it says “localhost”, the others use this computer's IP instead.",
    "Sin sugerencias por ahora.":
        "No suggestions for now.",
    "Solo desde otro equipo":
        "Only from another computer",
    "Todavía no exportaste una copia a tu equipo.":
        "You haven't exported a copy to your computer yet.",
    "Todavía no hay ninguna sesión en ese servidor.":
        "That server has no session yet.",
    "Tu copia exportada está al día con la imagen del anfitrión.":
        "Your exported copy is up to date with the host's image.",
    "Tu copia exportada no está al día con la imagen del anfitrión.":
        "Your exported copy isn't up to date with the host's image.",
    "Un navegador no puede abrir un servidor en la red local. Para hospedar así, ejecuta en este equipo el servidor de Kaint para la web (con Node.js): «node tools/collab-server.mjs» en la carpeta de Kaint para la web, y abre la dirección que muestra (http://localhost:47913/). Por P2P no hace falta: el navegador hospeda solo.":
        "A browser can't open a server on the local network. To host that way, run the Kaint for the web server on this computer (with Node.js): “node tools/collab-server.mjs” in the Kaint for the web folder, and open the address it shows (http://localhost:47913/). P2P doesn't need it: the browser hosts on its own.",
    "Un navegador no puede buscar en la red por sí solo: la lista aparece al abrir Kaint para la web desde la dirección de una sesión o con tools/collab-server.mjs en este equipo. Abajo, las que ya usaste.":
        "A browser can't search the network on its own: the list appears when Kaint for the web is opened from a session's address or with tools/collab-server.mjs on this computer. Below, the ones you already used.",
    "Usada antes":
        "Used before",
    "no respondió a tiempo":
        "it didn't answer in time",
    "zona %1×%2 en (%3, %4)":
        "area %1×%2 at (%3, %4)",
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

// Como tr() para un texto con plural: %n es el número (en inglés, con la forma que toca).
export function trn(text, n, ...args) {
    const forms = language === "en" ? en[text] ?? extra[text] : null;
    let out = Array.isArray(forms) ? forms[n === 1 ? 0 : Math.min(1, forms.length - 1)] : text;
    out = out.split("%n").join(String(n));
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

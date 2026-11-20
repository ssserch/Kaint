// Preferencias (como AppSettings de Kaint), guardadas en el navegador.
const KEY = "kaint.settings";

const defaults = {
    animations: true,
    // Las funciones de PhotoCraft (desactivadas por defecto) y los efectos que siguen complementos
    // de BoltBait activados desde Explorar complementos (sus identificadores).
    photocraftExtras: false,
    // La fila de arriba como la de Kaint para el escritorio: el icono y el título de la imagen (sin
    // los botones de la ventana), y la tira de imágenes a toda la altura de la fila.
    fakeTitleBar: true,
    stripFullHeight: true,
    boltBaitEffects: [],
    // Modo colaborativo (Configuración > Colaboración; js/collab/CollabUI.js).
    collabName: "",
    collabColor: "",
    collabSessionName: "",
    collabAccess: "approve",
    collabPassword: "",
    collabAllowedIds: [],
    collabRealtime: true,
    collabAutoAccept: false,
    collabAnnounce: true,
    collabShowCursors: true,
    collabServer: "",
    // P2P (la predeterminada) o la red local; servidores de señalización y STUN/TURN (vacío: los
    // predeterminados); la vista compacta de su página de Configuración.
    collabMode: "p2p",
    collabSignalServers: null,
    collabIceServers: null,
    collabCompactView: false,
    translucentPanels: true,
    // Como Paint.NET: las ventanas translúcidas solo lo son mientras tapan la imagen (al cruzar su
    // borde o encima de ella); fuera de la imagen, opacas.
    translucentOverImage: true,
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
    uiStyle: 1,
    // Se mostró la bienvenida de la primera vez.
    welcomed: false,
    // Subrayar siempre las letras de acceso de los menús (si no, como en Windows, solo con Alt).
    menuMnemonics: false,
    colorScheme: 0, // 0: sistema · 1: claro · 2: oscuro · 4: Windows 98 · 5: personalizado · 6: azul · 7: Windows 7 · 8: XP · 9: 98 ALT · 10: Windows 7 oscuro
                    // · 11, 12: Windows 8 (oscuro) · 13, 14: Windows 7 Basic (oscuro) · 15-22: Ubuntu, GNOME, Linux Mint y elementary OS (claro y oscuro)
    // Inglés por defecto (como Kaint); el español se elige en la bienvenida o en Configuración.
    language: "en",
    dropShadow: true,
    checkerBrightness: 191,
    askOnceOnClose: true,
    // Al abrir un GIF animado: 0, preguntar; 1, como capas; 2, como imágenes (un fotograma por imagen).
    gifOpenMode: 0,
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
    // La rueda del ratón cambia el valor de los deslizadores (ui/Wheel.js) y su paso: 0, el 1 % del
    // recorrido; 1, una unidad; 2, el 5 %; 3, el 10 %.
    wheelChangesValues: true,
    wheelStep: 0,
    // Pantalla táctil: girar la imagen con dos dedos, y las barras de desplazamiento (null: según el
    // equipo, ocultas en el teléfono).
    touchRotate: true,
    scrollbars: null,
    // Ventanas flotantes y diálogos: 0, como los de paint.js (Windows 10); 1, compactos. Grosor
    // del borde en píxeles.
    panelStyle: 0,
    // Bordes de las ventanas: 0 rectos, 1 redondeados (los temas de Windows 7 los ponen al elegirlos).
    windowCorners: 0,
    panelBorder: 1,
    // Las ventanas flotantes se acoplan a los bordes del área de trabajo y a las demás ventanas
    // al arrastrarlas cerca.
    snapPanels: true,
    // Fuente de la interfaz ("" o 0: la del navegador / del tema) y color propio de la barra
    // superior y de las cabeceras de las ventanas (null: el del tema), como el de Kaint en Linux.
    uiFontFamily: "",
    uiFontSize: 0,
    titleBarColor: null,
    // Color de selección propio de los temas Claro y Oscuro (null: el azul de siempre).
    selectionColorLight: null,
    selectionColorDark: null,
    // Ver > Reglas y su estilo: 0, oscura como la de paint.js; 1, del color de la barra.
    rulers: false,
    rulerStyle: 3,
    historyJumpButtons: false,
    // En Ajustes y Efectos, los efectos de los complementos con el icono de los complementos (la
    // pieza de puzle) a la derecha, como en Kaint.
    pluginMenuIcons: true,
    paletteTranslucent: true,
    cursorStyle: 1,
    // Unidades de medida (Ver): 0, píxeles; 1, pulgadas; 2, centímetros.
    unit: 0,
    // Atajos cambiados en Configuración > Atajos de teclado (id de la acción → combinación).
    shortcuts: {},
    // Herramienta al abrir Kaint ("": la última que se usó); por defecto, el pincel. Con otro nombre
    // desde la 1.1.7 (antes la predeterminada era la última usada).
    startupTool: "paintbrush",
    lastTool: "paintbrush",
    // Lápiz y tableta: la presión cambia el tamaño; el extremo borrador activa la Goma.
    penPressure: true,
    tabletEraser: true,
    // Color propio del fondo alrededor de la imagen con los temas claros y con los oscuros.
    canvasBorderLight: null,
    canvasBorderDark: null,
    // Tema personalizado: { base: "light" | "dark" | "win98", colors: { rol: "#rrggbb" } }.
    customTheme: null,
    // Personalización: efectos movidos a otro menú (id → ubicación), efectos ocultos y el orden y
    // las herramientas ocultas de la ventana Herramientas (Customization.js).
    // Botones de las ventanas y diálogos (como los de la barra de título de Kaint): 0 Windows 10,
    // 1 Windows 11, 2 macOS, 3 Windows 7, 4 Windows XP, 5 Windows 98; y a la izquierda.
    titleButtons: 0,
    titleButtonsLeft: false,
    // La X de las ventanas con los botones de Windows 7: 0 unida al borde de arriba, 1 rectángulo,
    // 2 cuadrado.
    win7PanelClose: 1,
    // El color de las ventanas de los temas Windows 7 (#rrggbb; vacío, el de siempre).
    win7Glass: "",
    // El color de las ventanas de los temas Windows 8 (#rrggbb; vacío, el celeste de siempre).
    win8Color: "",
    // Teléfono: las ventanas escondidas en las esquinas ("autohide") o flotantes ("windows"), si al
    // sacar una se esconden las que tape (en las esquinas) y el tamaño de cada una en el modo
    // Ventanas (en %).
    mobilePanels: "autohide",
    mobileHideCovered: true,
    mobilePanelScale: { tools: 100, history: 100, layers: 100, colors: 100 },
    effectLocations: {},
    hiddenEffects: [],
    toolOrder: [],
    hiddenTools: [],
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

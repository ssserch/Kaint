// Fondo del lienzo del tema personalizado (como el de Kaint, Theme::paintBackgroundImage): una
// imagen, un GIF (o WebP, PNG animado) o un vídeo (MP4, WebM...) detrás de la imagen, con su
// colocación, escala, opacidad, desenfoque, escala de grises y tinte. El archivo se guarda en el
// navegador (IndexedDB); sus ajustes, en el tema (Settings.customTheme.image).
import { Settings } from "../Settings.js";

const DB = "kaint-background", STORE = "files", KEY = "custom";

function openDb() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB, 1);
        req.onupgradeneeded = () => req.result.createObjectStore(STORE);
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    });
}

async function request(mode, fn) {
    const db = await openDb();
    return new Promise((resolve, reject) => {
        const t = db.transaction(STORE, mode);
        const r = fn(t.objectStore(STORE));
        t.oncomplete = () => resolve(r?.result);
        t.onerror = () => reject(t.error);
    });
}

const VIDEO = /\.(mp4|m4v|mov|webm|mkv|ogv|avi|wmv|3gp)$/i;

// Si el archivo es un vídeo (por su tipo o su extensión).
export function isVideoFile(file) {
    return /^video\//.test(file?.type || "") || VIDEO.test(file?.name || "");
}

// Lo que acepta el diálogo de abrir.
export const BackgroundAccept = "image/*,video/*,.gif,.webp,.apng,.avif,.jxl,.mp4,.m4v,.mov,.webm,.mkv,.ogv";

// Ajustes del fondo de siempre (los de Kaint).
export const BackgroundDefaults = { mode: 0, anchor: 4, scale: 100, opacity: 100, blur: 0, grayscale: false, tint: "#000000", tintStrength: 0 };

// Guarda el archivo elegido y devuelve los datos para el tema ({ name, video, stamp, ... }).
export async function storeBackground(file) {
    await request("readwrite", (s) => s.put(file, KEY));
    return { name: file.name, video: isVideoFile(file), stamp: Date.now() };
}

// El archivo del fondo guardado (para exportar el tema), o null.
export async function loadBackgroundFile() {
    try {
        return (await request("readonly", (s) => s.get(KEY))) || null;
    } catch {
        return null;
    }
}

// El tipo de un fondo por su nombre (al importarlo de un .zip, que no lo guarda).
export function backgroundType(name) {
    const ext = String(name).toLowerCase().split(".").pop();
    const types = { png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", apng: "image/apng",
                    avif: "image/avif", bmp: "image/bmp", jxl: "image/jxl", mp4: "video/mp4", m4v: "video/mp4", mov: "video/quicktime",
                    webm: "video/webm", mkv: "video/x-matroska", ogv: "video/ogg", avi: "video/x-msvideo", wmv: "video/x-ms-wmv",
                    "3gp": "video/3gpp" };
    return types[ext] || "application/octet-stream";
}

export async function clearBackground() {
    try {
        await request("readwrite", (s) => s.delete(KEY));
    } catch {
        // (Sin almacenamiento: no había nada que borrar.)
    }
}

// --- Capa detrás del lienzo -------------------------------------------------------------------

let layer = null, media = null, tint = null, url = null, loadedStamp = null;

function ensureLayer(workspace) {
    if (layer)
        return layer;
    layer = document.createElement("div");
    layer.className = "workspace-background";
    layer.setAttribute("aria-hidden", "true");
    tint = document.createElement("div");
    tint.className = "tint";
    layer.appendChild(tint);
    // Detrás del lienzo: el primero del área de trabajo.
    workspace.insertBefore(layer, workspace.firstChild);
    return layer;
}

function removeLayer() {
    layer?.remove();
    layer = media = tint = null;
    if (url)
        URL.revokeObjectURL(url);
    url = loadedStamp = null;
    document.documentElement.classList.remove("workspace-media");
}

const number = (v, fallback) => (Number.isFinite(Number(v)) ? Number(v) : fallback);

// El tinte, como color con su intensidad.
function tintColor(settings) {
    const hex = /^#[0-9a-f]{6}$/i.test(settings.tint || "") ? settings.tint : "#000000";
    const n = parseInt(hex.slice(1), 16), a = Math.max(0, Math.min(100, number(settings.tintStrength, 0))) / 100;
    return `rgba(${n >> 16}, ${(n >> 8) & 255}, ${n & 255}, ${a})`;
}

const anchors = ["left top", "center top", "right top", "left center", "center center", "right center", "left bottom", "center bottom", "right bottom"];

// Coloca la imagen o el vídeo según los ajustes (mosaico, tamaño original, estirada, ajustada o
// rellenar; los vídeos no van en mosaico: se quedan a tamaño original). En las imágenes, el tinte
// es una capa más del fondo con su mismo tamaño y posición: tiñe la imagen y no lo de alrededor.
function place(image) {
    if (!media)
        return;
    const anchor = anchors[Math.max(0, Math.min(8, number(image.anchor, 4)))];
    const scale = Math.max(10, Math.min(400, number(image.scale, 100))) / 100;
    const mode = number(image.mode, 0);
    if (media.tagName === "VIDEO") {
        media.style.objectPosition = anchor;
        media.style.objectFit = mode === 2 ? "fill" : mode === 3 ? "contain" : mode === 4 ? "cover" : "none";
        // Tamaño original (y mosaico), con su escala.
        media.style.transform = mode <= 1 && scale !== 1 ? `scale(${scale})` : "";
        media.style.transformOrigin = anchor;
        tint.style.display = "";
        tint.style.background = tintColor(image);
        return;
    }
    tint.style.display = "none";
    const natural = media.naturalSize;
    const size = mode === 2 ? "100% 100%" : mode === 3 ? "contain" : mode === 4 ? "cover"
        : natural ? `${Math.max(1, natural.w * scale)}px ${Math.max(1, natural.h * scale)}px` : "auto";
    const repeat = mode === 0 ? "repeat" : "no-repeat";
    const color = tintColor(image);
    media.style.backgroundImage = `linear-gradient(${color}, ${color}), url("${url}")`;
    media.style.backgroundPosition = `${anchor}, ${anchor}`;
    media.style.backgroundRepeat = `${repeat}, ${repeat}`;
    media.style.backgroundSize = `${size}, ${size}`;
}

// Pone o quita el fondo según el tema activo (lo llama App.applyTheme).
export async function applyBackground(workspace, scheme) {
    const custom = Settings.get("customTheme") || {};
    const image = scheme === 5 ? custom.image : null;
    if (!image?.name) {
        removeLayer();
        return;
    }
    const settings = { ...BackgroundDefaults, ...image };
    ensureLayer(workspace);
    document.documentElement.classList.add("workspace-media");
    if (loadedStamp !== image.stamp) {
        loadedStamp = image.stamp;
        let blob = null;
        try {
            blob = await request("readonly", (s) => s.get(KEY));
        } catch {
            blob = null;
        }
        // Cambió mientras se leía.
        if (loadedStamp !== image.stamp || !layer)
            return;
        media?.remove();
        media = null;
        if (url)
            URL.revokeObjectURL(url);
        url = blob ? URL.createObjectURL(blob) : null;
        if (url) {
            if (image.video) {
                media = document.createElement("video");
                Object.assign(media, { src: url, muted: true, loop: true, autoplay: true, playsInline: true });
                media.setAttribute("muted", "");
                media.className = "media";
                media.addEventListener("canplay", () => media?.play?.().catch(() => {}), { once: true });
            } else {
                media = document.createElement("div");
                media.className = "media";
                // El tamaño natural, para el mosaico y el tamaño original con su escala.
                const probe = new Image();
                const target = media;
                probe.onload = () => {
                    target.naturalSize = { w: probe.naturalWidth, h: probe.naturalHeight };
                    place({ ...BackgroundDefaults, ...((Settings.get("customTheme") || {}).image || {}) });
                };
                probe.src = url;
            }
            layer.insertBefore(media, tint);
        }
    }
    if (!media)
        return;
    const blur = Math.max(0, Math.min(40, number(settings.blur, 0)));
    // Con desenfoque, la capa algo más grande: sus bordes no se ven transparentes.
    layer.style.inset = blur ? `${-blur * 2}px` : "";
    media.style.filter = [blur ? `blur(${blur}px)` : "", settings.grayscale ? "grayscale(1)" : ""].filter(Boolean).join(" ");
    layer.style.opacity = String(Math.max(0, Math.min(100, number(settings.opacity, 100))) / 100);
    place(settings);
}

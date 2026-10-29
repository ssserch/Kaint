// Complementos de la versión web: paquetes .paintjs (un ZIP con plugin.json y plugin.js, y una
// carpeta assets/ opcional) que añaden ajustes, efectos y formatos de archivo con la API de
// JavaScript que usan esos paquetes (paintDotJs.registerPlugin, versión 1). Se guardan en el
// navegador (IndexedDB) y se cargan al abrir Kaint; se pueden instalar desde un archivo o desde el
// catálogo de complementos.
import { Effects, defaultValues } from "../core/Effects.js";
import { SaveFormats } from "../core/ImageIO.js";
import { unzip } from "../core/Zip.js";
import { setCustomIcon } from "../ui/Icons.js";
import { tr } from "../i18n.js";

export const API_VERSION = 1;
const DB = "kaint-plugins", STORE = "plugins";
const CATALOG_URL = "https://labystudio.github.io/paintdotjs-plugins/catalog.json";
// Categorías de los paquetes que ya existen en Kaint.
const knownCategories = { artistic: "artistic", blurs: "blurs", distort: "distort", noise: "noise", object: "object",
                          photo: "photo", render: "render", stylize: "stylize" };

// --- Almacenamiento ----------------------------------------------------------------------------

function openDb() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB, 1);
        req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: "id" });
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

// --- Validación ----------------------------------------------------------------------------------

function validId(value, label) {
    if (typeof value !== "string" || !/^[a-z0-9][a-z0-9._-]{0,79}$/i.test(value))
        throw new Error(tr("%1 no es válido.", label));
    return value;
}

function text(value, fallback, max) {
    return typeof value === "string" && value.trim() ? value.trim().slice(0, max) : fallback;
}

// --- Ayudas de píxeles de la API -----------------------------------------------------------------

const pixels = Object.freeze({
    selectionBounds(mask, width, height) {
        if (!mask)
            return { x: 0, y: 0, width, height };
        let left = width, top = height, right = 0, bottom = 0;
        for (let i = 0; i < mask.length; ++i)
            if (mask[i]) {
                const x = i % width, y = Math.floor(i / width);
                left = Math.min(left, x); top = Math.min(top, y);
                right = Math.max(right, x + 1); bottom = Math.max(bottom, y + 1);
            }
        return { x: left, y: top, width: Math.max(0, right - left), height: Math.max(0, bottom - top) };
    },
    sample(source, width, height, x, y) {
        const o = (Math.max(0, Math.min(height - 1, Math.round(y))) * width + Math.max(0, Math.min(width - 1, Math.round(x)))) * 4;
        return Array.from(source.subarray(o, o + 4));
    },
    rgbToHsv(r, g, b) {
        r /= 255; g /= 255; b /= 255;
        const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
        const h = d === 0 ? 0 : max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
        return [(h * 60 + 360) % 360, max === 0 ? 0 : d / max, max];
    },
    hsvToRgb(h, s, v) {
        const hue = ((((h % 360) + 360) % 360) / 60), c = v * s, x = c * (1 - Math.abs((hue % 2) - 1)), m = v - c;
        const rgb = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][Math.floor(hue) % 6];
        return rgb.map((n) => Math.round((n + m) * 255));
    },
    convolve(source, width, height, kernel, { divisor = 1, bias = 0, preserveAlpha = true } = {}) {
        const size = Math.sqrt(kernel.length);
        if (!Number.isInteger(size) || size % 2 !== 1 || size > 31 || !Number.isFinite(divisor) || divisor === 0)
            throw new Error("Convolution requires an odd square kernel (up to 31×31) and a nonzero divisor.");
        const out = new Uint8ClampedArray(source), r = Math.floor(size / 2);
        for (let y = 0; y < height; ++y)
            for (let x = 0; x < width; ++x)
                for (let c = 0; c < (preserveAlpha ? 3 : 4); ++c) {
                    let sum = 0;
                    for (let ky = 0; ky < size; ++ky)
                        for (let kx = 0; kx < size; ++kx) {
                            const sx = Math.max(0, Math.min(width - 1, x + kx - r)), sy = Math.max(0, Math.min(height - 1, y + ky - r));
                            sum += source[(sy * width + sx) * 4 + c] * kernel[ky * size + kx];
                        }
                    out[(y * width + x) * 4 + c] = sum / divisor + bias;
                }
        return out;
    },
});

// --- Conversión de los efectos de un paquete a los de Kaint --------------------------------------

const hexOf = (c) => `#${[c.r, c.g, c.b].map((n) => n.toString(16).padStart(2, "0")).join("")}`;
const colorOf = (hex) => {
    const m = /^#?([0-9a-f]{6})$/i.exec(String(hex || "")) || ["", "000000"];
    return { r: parseInt(m[1].slice(0, 2), 16), g: parseInt(m[1].slice(2, 4), 16), b: parseInt(m[1].slice(4, 6), 16), a: 255 };
};

// Controles del paquete → parámetros del diálogo de efectos de Kaint.
function paramsFor(controls) {
    return (controls || []).slice(0, 32).map((c) => {
        const key = String(c.key), label = String(c.label ?? "");
        switch (c.type) {
        case "range": {
            const step = Number(c.step) || 1, integer = Number.isInteger(step) && Number.isInteger(Number(c.min)) && Number.isInteger(Number(c.max));
            const decimals = integer ? 0 : Math.min(4, Math.max(0, Math.ceil(-Math.log10(step))));
            return { key, label, type: integer ? "int" : "real", min: Number(c.min), max: Number(c.max), def: Number(c.defaultValue), decimals, step };
        }
        case "checkbox": return { key, label, type: "bool", def: !!c.defaultValue };
        case "select": {
            const values = (c.choices || []).map((x) => String(x[0]));
            return { key, label, type: "choice", choices: (c.choices || []).map((x) => String(x[1])), def: Math.max(0, values.indexOf(String(c.defaultValue))), values };
        }
        case "color": return { key, label, type: "color", def: colorOf(c.defaultValue) };
        case "text": return { key, label, type: "text", def: String(c.defaultValue ?? "") };
        default: throw new Error(tr("Control desconocido: %1", String(c.type)));
        }
    });
}

// Valores de Kaint → los que espera el paquete (cadenas para las listas, #rrggbb para colores).
function pluginValues(params, values) {
    const out = {};
    for (const p of params) {
        const v = values[p.key];
        out[p.key] = p.type === "choice" ? p.values[v] ?? p.values[p.def] : p.type === "color" ? hexOf(v || p.def) : v;
    }
    return out;
}

function kaintValues(params, values) {
    const out = {};
    for (const p of params) {
        const v = values?.[p.key];
        if (v === undefined)
            continue;
        out[p.key] = p.type === "choice" ? Math.max(0, p.values.indexOf(String(v))) : p.type === "color" ? colorOf(v) : v;
    }
    return out;
}

function toEffect(plugin, def) {
    const id = validId(def.id, tr("El identificador del efecto"));
    const name = text(def.name, id, 80);
    if ((typeof def.render === "function") === (typeof def.renderPixel === "function"))
        throw new Error(tr("El efecto %1 debe definir render() o renderPixel().", name));
    const menu = def.menu === "Adjustments" ? "adjustments" : "effects";
    const catName = typeof def.category === "object" && def.category ? text(def.category.name, "Plugins", 48) : text(def.category, "Plugins", 48);
    const catIcon = typeof def.category === "object" && def.category?.icon ? String(def.category.icon) : null;
    const params = paramsFor(def.controls);
    const effect = {
        id: `plugin.${plugin.id}.${id}`, name, plugin: plugin.name, pluginId: plugin.id, params, shortcut: "",
        // Ajustes: sin categoría; efectos: la de Kaint con ese nombre, una nueva o, con "Root", en el menú.
        category: menu === "adjustments" || catName === "Root" ? null : knownCategories[catName.toLowerCase()] || `plugin:${catName}`,
        categoryName: catName, categoryIcon: catIcon, menu, configure: typeof def.configure === "function" ? def.configure : null,
        dialogTitle: text(def.dialogTitle, plugin.dialogTitle || name, 120),
        render(src, values, rect, info) {
            const w = src.width, h = src.height, vals = this.configure ? values : pluginValues(params, values);
            if (def.renderPixel) {
                const out = new ImageData(new Uint8ClampedArray(src.data), w, h), d = out.data;
                for (let y = rect.y; y < rect.y + rect.h; ++y)
                    for (let x = rect.x, i = (y * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4) {
                        const r = def.renderPixel({ r: d[i], g: d[i + 1], b: d[i + 2], a: d[i + 3], x, y, width: w, height: h, values: vals });
                        if (!r || r.length !== 4)
                            throw new Error(tr("%1: renderPixel() debe devolver cuatro canales.", name));
                        d[i] = r[0]; d[i + 1] = r[1]; d[i + 2] = r[2]; d[i + 3] = r[3];
                    }
                return out;
            }
            const source = new Uint8ClampedArray(src.data), destination = new ImageData(new Uint8ClampedArray(source), w, h);
            const selection = info?.selection ? new Uint8Array(info.selection) : null;
            const result = def.render({ source, destination: destination.data, width: w, height: h, values: vals, selection,
                                        selectionBounds: pixels.selectionBounds(selection, w, h) });
            if (result instanceof Promise)
                throw new Error(tr("%1: render() debe ser síncrono.", name));
            if (result === undefined || result === destination || result === destination.data)
                return destination;
            if (result instanceof ImageData && result.width === w && result.height === h)
                return result;
            if (result instanceof Uint8ClampedArray && result.length === destination.data.length)
                return new ImageData(result, w, h);
            throw new Error(tr("%1: render() devolvió un valor que no vale.", name));
        },
    };
    if (def.icon)
        setCustomIcon(`effect-${effect.id}`, String(def.icon));
    else
        setCustomIcon(`effect-${effect.id}`, "assets/icons/menu_help_plugins_icon.png");
    return effect;
}

function toFileType(plugin, def) {
    const id = validId(def.id, tr("El identificador del formato"));
    if (typeof def.decode !== "function" && typeof def.encode !== "function")
        throw new Error(tr("El formato %1 debe definir decode() o encode().", id));
    const extensions = (def.extensions || []).map((e) => String(e).toLowerCase()).filter((e) => /^\.[a-z0-9]{1,12}$/.test(e));
    const reserved = [".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".ora", ".pdn", ".zip", ".paintjs"];
    if (!extensions.length || extensions.some((e) => reserved.includes(e)))
        throw new Error(tr("El formato %1 no tiene extensiones válidas.", id));
    return { id: `plugin.${plugin.id}.${id}`, pluginId: plugin.id, name: text(def.name, id, 80), extensions, ext: extensions[0].slice(1),
             mime: def.mime || "application/octet-stream", supportsLayers: def.supportsLayers === true, decode: def.decode, encode: def.encode,
             saveParams: paramsFor(def.saveControls), plugin: true };
}

// --- El gestor -----------------------------------------------------------------------------------

export const Plugins = {
    records: [],
    active: new Map(),
    errors: [],
    target: null,
    metadata: null,

    installApi(app) {
        this.app = app;
        if (window.paintDotJs)
            return;
        const controls = Object.freeze({
            range: (key, label, min, max, defaultValue, step = 1, options = {}) => ({ type: "range", key, label, min, max, defaultValue, step, ...options }),
            checkbox: (key, label, defaultValue = false) => ({ type: "checkbox", key, label, defaultValue }),
            select: (key, label, defaultValue, choices) => ({ type: "select", key, label, defaultValue, choices }),
            color: (key, label, defaultValue = "#000000") => ({ type: "color", key, label, defaultValue }),
            text: (key, label, defaultValue = "") => ({ type: "text", key, label, defaultValue }),
        });
        const utils = Object.freeze({
            clamp: (v, lo, hi) => Math.max(lo, Math.min(hi, v)),
            clampByte: (v) => Math.max(0, Math.min(255, Math.round(v))),
        });
        const effects = Object.freeze({
            list: () => Effects.map((e) => ({ id: e.id, name: tr(e.name), menu: e.category ? "Effects" : "Adjustments",
                                              category: e.categoryName || e.category || "", values: defaultValues(e) })),
            apply: (id, source, width, height, values = null, selection = null) => {
                const effect = Effects.find((e) => e.id === id);
                if (!effect)
                    throw new Error(`Unknown effect: ${id}`);
                const src = new ImageData(new Uint8ClampedArray(source), width, height);
                const vals = { ...defaultValues(effect), ...(effect.pluginId && !effect.configure ? kaintValues(effect.params, values) : values || {}) };
                const out = effect.render(src, vals, { x: 0, y: 0, w: width, h: height }, { selection }).data;
                if (selection)
                    for (let i = 0; i < selection.length; ++i)
                        for (let c = 0; c < 4; ++c)
                            out[i * 4 + c] = source[i * 4 + c] + ((out[i * 4 + c] - source[i * 4 + c]) * selection[i]) / 255;
                return out;
            },
            configure: async (id) => {
                const effect = Effects.find((e) => e.id === id);
                return effect ? this.app.configureEffect(effect) : null;
            },
        });
        const dialogs = Object.freeze({ create: (options = {}) => this.app.createPluginDialog(options) });
        window.paintDotJs = Object.freeze({
            apiVersion: API_VERSION, controls, dialogs, effects, utils, pixels,
            // Sustituido al instalar por la imagen del paquete (data:).
            asset: (name) => name,
            registerPlugin: (definition) => {
                if (!this.target)
                    throw new Error("paintDotJs.registerPlugin() may only be called while a plugin is loading.");
                this.target.push(this.metadata ? { ...definition, ...this.metadata } : definition);
            },
        });
    },

    // Lee el paquete: plugin.json, plugin.js y sus imágenes (que pasan a ir dentro del código).
    async readPackage(file) {
        if (file.size > 5 * 1024 * 1024)
            throw new Error(tr("Un complemento no puede ocupar más de 5 MB."));
        let files;
        try {
            files = await unzip(await file.arrayBuffer());
        } catch {
            throw new Error(tr("No es un archivo ZIP válido."));
        }
        for (const name of files.keys())
            if (!name.endsWith("/") && name !== "plugin.json" && name !== "plugin.js" && !/^assets\/[A-Za-z0-9_.-]+$/.test(name))
                throw new Error(tr("Archivo no válido en el complemento: %1", name));
        if (!files.has("plugin.json") || !files.has("plugin.js"))
            throw new Error(tr("El complemento debe tener plugin.json y plugin.js en la raíz."));
        const dec = new TextDecoder();
        let manifest;
        try {
            manifest = JSON.parse(dec.decode(files.get("plugin.json")));
        } catch (e) {
            throw new Error(tr("plugin.json no es válido: %1", e.message));
        }
        if (manifest.apiVersion !== API_VERSION)
            throw new Error(tr("Esta versión de Kaint admite la versión %1 de la API de complementos.", API_VERSION));
        const meta = {
            apiVersion: API_VERSION, id: validId(manifest.id, tr("El identificador del complemento")), name: text(manifest.name, manifest.id, 80),
            version: text(manifest.version, "1.0.0", 32), author: text(manifest.author, tr("Desconocido"), 80),
            copyright: text(manifest.copyright, "", 120), description: text(manifest.description, "", 500),
            website: text(manifest.website, "", 500), license: text(manifest.license, "", 120), dialogTitle: text(manifest.dialogTitle, "", 120),
        };
        const mimes = { gif: "image/gif", jpeg: "image/jpeg", jpg: "image/jpeg", json: "application/json", png: "image/png",
                        svg: "image/svg+xml", txt: "text/plain", webp: "image/webp" };
        let source = dec.decode(files.get("plugin.js"));
        source = source.replace(/paintDotJs\.asset\(\s*(["'])([A-Za-z0-9_.-]+)\1\s*\)/g, (match, quote, asset) => {
            const data = files.get(`assets/${asset}`), mime = mimes[asset.split(".").pop().toLowerCase()];
            if (!data || !mime)
                throw new Error(tr("Falta la imagen del complemento: %1", asset));
            let binary = "";
            for (let o = 0; o < data.length; o += 0x8000)
                binary += String.fromCharCode(...data.subarray(o, o + 0x8000));
            return JSON.stringify(`data:${mime};base64,${btoa(binary)}`);
        });
        return { source, manifest: meta };
    },

    // Ejecuta plugin.js y recoge lo que registra.
    async evaluate(source, fileName, manifest) {
        if (this.target)
            throw new Error(tr("Ya se está cargando otro complemento."));
        const registrations = [];
        this.target = registrations;
        this.metadata = manifest;
        const url = URL.createObjectURL(new Blob([source], { type: "text/javascript" }));
        const script = document.createElement("script");
        script.src = url;
        let failure = null;
        const onError = (e) => {
            failure = e.error || new Error(e.message);
        };
        window.addEventListener("error", onError);
        try {
            await new Promise((resolve, reject) => {
                script.onload = resolve;
                script.onerror = () => reject(new Error(tr("No se pudo ejecutar %1.", fileName)));
                document.head.appendChild(script);
            });
        } finally {
            window.removeEventListener("error", onError);
            script.remove();
            URL.revokeObjectURL(url);
            this.target = null;
            this.metadata = null;
        }
        if (failure)
            throw failure;
        if (registrations.length !== 1)
            throw new Error(tr("%1 debe llamar a paintDotJs.registerPlugin() una vez.", fileName));
        const def = registrations[0];
        const plugin = { id: def.id, name: def.name, version: def.version, author: def.author, description: def.description,
                         dialogTitle: def.dialogTitle || "" };
        plugin.effects = (Array.isArray(def.effects) ? def.effects : []).map((e) => toEffect(plugin, e));
        plugin.fileTypes = (Array.isArray(def.fileTypes) ? def.fileTypes : []).map((t) => toFileType(plugin, t));
        if (!plugin.effects.length && !plugin.fileTypes.length)
            throw new Error(tr("Un complemento debe añadir al menos un efecto o un formato."));
        return plugin;
    },

    activate(plugin) {
        this.deactivate(plugin.id);
        Effects.push(...plugin.effects);
        SaveFormats.push(...plugin.fileTypes.filter((t) => t.encode));
        this.active.set(plugin.id, plugin);
    },

    deactivate(id) {
        const plugin = this.active.get(id);
        if (!plugin)
            return;
        for (const e of plugin.effects) {
            const i = Effects.indexOf(e);
            if (i >= 0)
                Effects.splice(i, 1);
            setCustomIcon(`effect-${e.id}`, null);
        }
        for (const t of plugin.fileTypes) {
            const i = SaveFormats.indexOf(t);
            if (i >= 0)
                SaveFormats.splice(i, 1);
        }
        this.active.delete(id);
    },

    changed() {
        window.dispatchEvent(new Event("kaint-plugins"));
    },

    // Carga los complementos activados (al abrir Kaint).
    async load() {
        try {
            this.records = (await request("readonly", (s) => s.getAll())) || [];
        } catch (e) {
            this.errors.push({ id: "storage", name: tr("Almacenamiento"), message: e.message });
            this.records = [];
        }
        this.records.sort((a, b) => a.name.localeCompare(b.name));
        for (const record of this.records) {
            if (!record.enabled)
                continue;
            try {
                this.activate(await this.evaluate(record.source, record.fileName, record.manifest));
            } catch (e) {
                this.errors.push({ id: record.id, name: record.name, message: e.message });
            }
        }
        this.changed();
    },

    async install(file, expected = null) {
        const { source, manifest } = await this.readPackage(file);
        const plugin = await this.evaluate(source, file.name, manifest);
        if (expected && (plugin.id !== expected.id || plugin.version !== expected.version))
            throw new Error(tr("El complemento no coincide con el del catálogo."));
        for (const t of plugin.fileTypes)
            for (const other of this.active.values())
                if (other.id !== plugin.id && other.fileTypes.some((o) => o.extensions.some((e) => t.extensions.includes(e))))
                    throw new Error(tr("Otro complemento ya abre los archivos %1.", t.extensions.join(", ")));
        const record = { id: plugin.id, name: plugin.name, author: plugin.author, version: plugin.version, description: plugin.description,
                         fileName: file.name, source, manifest, enabled: true, effectCount: plugin.effects.length,
                         fileTypeCount: plugin.fileTypes.length, updatedAt: new Date().toISOString() };
        await request("readwrite", (s) => s.put(record));
        this.records = [...this.records.filter((r) => r.id !== record.id), record].sort((a, b) => a.name.localeCompare(b.name));
        this.errors = this.errors.filter((e) => e.id !== record.id);
        this.activate(plugin);
        this.changed();
        return record;
    },

    async setEnabled(id, enabled) {
        const record = this.records.find((r) => r.id === id);
        if (!record || record.enabled === enabled)
            return;
        if (enabled)
            this.activate(await this.evaluate(record.source, record.fileName, record.manifest));
        else
            this.deactivate(id);
        record.enabled = enabled;
        await request("readwrite", (s) => s.put(record));
        this.changed();
    },

    async remove(id) {
        this.deactivate(id);
        await request("readwrite", (s) => s.delete(id));
        this.records = this.records.filter((r) => r.id !== id);
        this.errors = this.errors.filter((e) => e.id !== id);
        this.changed();
    },

    anyEffects() {
        return Effects.some((e) => e.pluginId);
    },

    // Formato de un complemento que abre ese archivo, o null.
    fileTypeFor(name) {
        const lower = String(name || "").toLowerCase();
        for (const plugin of this.active.values())
            for (const t of plugin.fileTypes)
                if (t.decode && t.extensions.some((e) => lower.endsWith(e)))
                    return t;
        return null;
    },

    openExtensions() {
        return [...this.active.values()].flatMap((p) => p.fileTypes.filter((t) => t.decode).flatMap((t) => t.extensions));
    },

    // --- Catálogo ------------------------------------------------------------------------------

    async download(url, limit) {
        const response = await fetch(url, { credentials: "omit", redirect: "error", signal: AbortSignal.timeout(15000) });
        if (!response.ok)
            throw new Error(tr("No se pudo descargar (HTTP %1).", response.status));
        const bytes = new Uint8Array(await response.arrayBuffer());
        if (bytes.length > limit)
            throw new Error(tr("El catálogo o el complemento pasa del tamaño máximo."));
        return bytes;
    },

    async catalog() {
        const catalog = JSON.parse(new TextDecoder().decode(await this.download(`${CATALOG_URL}?refresh=${Date.now().toString(36)}`, 1024 * 1024)));
        if (catalog.schemaVersion !== 1 || !Array.isArray(catalog.plugins))
            throw new Error(tr("El formato del catálogo no es compatible."));
        const base = new URL("./", CATALOG_URL);
        return catalog.plugins.filter((e) => e.apiVersion === API_VERSION && /^[a-f0-9]{64}$/.test(e.sha256)
                                       && typeof e.file === "string" && /^[a-zA-Z0-9_./-]+\.paintjs$/.test(e.file) && !e.file.includes(".."))
            .map((e) => ({ id: e.id, name: text(e.name, e.id, 80), version: text(e.version, "", 32), author: text(e.author, tr("Desconocido"), 80),
                           description: text(e.description, "", 500), license: text(e.license, "", 120), sha256: e.sha256,
                           url: new URL(e.file, base).href }));
    },

    async installFromCatalog(entry) {
        const bytes = await this.download(entry.url, 5 * 1024 * 1024);
        const hash = [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))].map((b) => b.toString(16).padStart(2, "0")).join("");
        if (hash !== entry.sha256)
            throw new Error(tr("El complemento descargado no coincide con el del catálogo: no se instaló nada."));
        return this.install(new File([bytes], `${entry.id}.paintjs`), entry);
    },
};

export { kaintValues, pluginValues };

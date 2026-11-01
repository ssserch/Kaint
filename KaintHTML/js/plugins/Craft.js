// Complementos de PhotoCraft y LightCraft en la web (src/core/plugins/CraftPlugins.cpp de Kaint):
// · Filtros .wasm con la interfaz de complementos v1 de PhotoCraft (pc_abi_version, pc_manifest,
//   pc_alloc y pc_filter). Se ejecutan en un Web Worker (CraftWorker.js) sin nada importado; uno
//   que tarda más de 5 minutos o que se cancela se detiene. Su diálogo sale de los parámetros que
//   declaran.
// · "Looks" .cube y .3dl (tablas de color 3D, o 1D en .cube), con una Cantidad.
// · Ajustes de revelado .xmp (Camera Raw / Lightroom): Kaint aplica los suyos, con una Cantidad.
import { tr } from "../i18n.js";

export const CRAFT_EXTENSIONS = [".wasm", ".cube", ".3dl", ".xmp"];
const ABI_VERSION = 1;
const MAX_OVERLAP = 256;
const TIMEOUT_MS = 5 * 60 * 1000;
// Hasta unos 16 millones de píxeles van de una vez (algunos filtros se colocan según el tamaño de
// lo que reciben); más grandes, en cuadros con el margen que pidan.
const BAND_PIXELS = 16 * 1024 * 1024;

export function isCraftFile(name) {
    const lower = String(name || "").toLowerCase();
    return CRAFT_EXTENSIONS.some((e) => lower.endsWith(e));
}

const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const toByte = (v) => Math.round(clamp01(v) * 255);
const srgbToLinear = (v) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
const linearToSrgb = (v) => (v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055);

// "keepLight" o "edge_width" → "Keep light", "Edge width": los parámetros no traen etiqueta.
function humanize(key) {
    const spaced = key.replace(/[_-]+/g, " ").replace(/([a-z])([A-Z])/g, "$1 $2").toLowerCase().trim();
    return spaced.charAt(0).toUpperCase() + spaced.slice(1);
}

async function shortHash(text) {
    const digest = await crypto.subtle.digest("SHA-1", new TextEncoder().encode(text));
    return [...new Uint8Array(digest)].slice(0, 5).map((b) => b.toString(16).padStart(2, "0")).join("");
}

const pluginCategory = () => ({ category: "plugin:craft", categoryName: tr("Complementos"), categoryIcon: null });

// ===========================================================================================
// Filtros WebAssembly
// ===========================================================================================

// Una tarea en un Worker nuevo, con tiempo límite; cancel() lo termina.
function inWorker(message, transfer = []) {
    const worker = new Worker(new URL("./CraftWorker.js", import.meta.url));
    let timer = 0, settle = null;
    const promise = new Promise((resolve, reject) => {
        settle = (value, error) => {
            clearTimeout(timer);
            worker.terminate();
            settle = null;
            error ? reject(error) : resolve(value);
        };
        worker.onmessage = ({ data }) => settle?.(data.error ? null : data, data.error ? new Error(data.error) : null);
        worker.onerror = (e) => {
            e.preventDefault?.();
            settle?.(null, new Error(e.message || tr("El complemento se detuvo con un error.")));
        };
        timer = setTimeout(() => settle?.(null, new Error(tr("El complemento tardó demasiado."))), TIMEOUT_MS);
        worker.postMessage(message, transfer);
    });
    return { promise, cancel: () => settle?.(null) };
}

function parseManifest(text) {
    if (text.length > 64 * 1024)
        throw new Error(tr("El manifiesto del complemento es demasiado grande."));
    let o;
    try {
        o = JSON.parse(text);
    } catch (e) {
        throw new Error(tr("El manifiesto del complemento no es JSON válido: %1", e.message));
    }
    const m = {
        id: String(o.id ?? ""), name: String(o.name ?? "").trim(), version: String(o.version ?? "").slice(0, 32),
        author: String(o.author ?? "").slice(0, 128), description: String(o.description ?? "").slice(0, 1024),
        overlap: Number(o.overlap ?? 0) | 0, canvasArea: o.area === "canvas", params: [],
    };
    if (!/^[A-Za-z0-9._-]{1,64}$/.test(m.id))
        throw new Error(tr("El identificador «%1» del complemento no vale: usa de 1 a 64 letras, números, puntos, guiones o guiones bajos.", m.id));
    if (!m.name || m.name.length > 64)
        throw new Error(tr("El nombre del complemento debe tener de 1 a 64 caracteres."));
    if (o.kind !== "filter")
        throw new Error(tr("Solo se admiten complementos de tipo «filter»."));
    if (m.overlap < 0 || m.overlap > MAX_OVERLAP)
        throw new Error(tr("El margen («overlap») del complemento pasa de %1 píxeles.", MAX_OVERLAP));
    // En el orden en que se declaran (el de las claves del objeto).
    const params = o.params && typeof o.params === "object" ? o.params : {};
    if (Object.keys(params).length > 64)
        throw new Error(tr("El complemento declara demasiados parámetros."));
    for (const [key, p] of Object.entries(params)) {
        const spec = { key, type: p?.type, pixels: p?.unit === "px" };
        if (p?.type === "number" || p?.type === "int") {
            const big = p.type === "int" ? 1e6 : 0;
            spec.min = Number(p.min ?? -big);
            spec.max = Number(p.max ?? big);
            spec.def = Number(p.default ?? 0);
            if (!Number.isFinite(spec.min) || !Number.isFinite(spec.max) || spec.min > spec.max)
                throw new Error(tr("El parámetro «%1» del complemento tiene un rango que no vale.", key));
            spec.def = Math.min(spec.max, Math.max(spec.min, Number.isFinite(spec.def) ? spec.def : 0));
        } else if (p?.type === "bool") {
            spec.def = !!p.default;
        } else if (p?.type === "choice") {
            spec.options = Array.isArray(p.options) ? p.options.map(String) : [];
            if (!spec.options.length || spec.options.length > 64 || spec.options.includes(""))
                throw new Error(tr("El parámetro «%1» del complemento no tiene opciones válidas.", key));
            spec.def = Math.max(0, spec.options.indexOf(String(p.default)));
        } else {
            throw new Error(tr("El parámetro «%1» del complemento es de un tipo desconocido.", key));
        }
        m.params.push(spec);
    }
    return m;
}

// Lo que se escribe: el contenido de la capa (lo no transparente, con el margen) o, con
// "area": "canvas", todo; dentro de 'rect' (la selección).
function outputArea(src, rect, m) {
    if (m.canvasArea)
        return rect;
    const w = src.width, h = src.height, d = src.data;
    let x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x)
            if (d[(y * w + x) * 4 + 3]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0)
        return null;
    const ov = m.overlap;
    const l = Math.max(rect.x, x0 - ov, 0), t = Math.max(rect.y, y0 - ov, 0);
    const r = Math.min(rect.x + rect.w, x1 + 1 + ov, w), b = Math.min(rect.y + rect.h, y1 + 1 + ov, h);
    return r > l && b > t ? { x: l, y: t, w: r - l, h: b - t } : null;
}

function wasmEffect(module, m, id) {
    const params = m.params.map((p) => {
        const label = humanize(p.key) + (p.pixels ? " (px)" : "");
        if (p.type === "number") {
            const span = p.max - p.min;
            return { key: p.key, label, type: "real", min: p.min, max: p.max, def: p.def, decimals: span >= 100 ? 1 : span >= 10 ? 2 : 3 };
        }
        if (p.type === "int")
            return { key: p.key, label, type: "int", min: Math.round(p.min), max: Math.round(p.max), def: Math.round(p.def), decimals: 0 };
        if (p.type === "bool")
            return { key: p.key, label, type: "bool", def: p.def };
        return { key: p.key, label, type: "choice", choices: p.options, min: 0, max: p.options.length - 1, def: p.def };
    });
    const jsonValues = (values) => {
        const o = {};
        for (const p of m.params) {
            const v = values?.[p.key];
            if (p.type === "number")
                o[p.key] = Math.min(p.max, Math.max(p.min, Number(v ?? p.def)));
            else if (p.type === "int")
                o[p.key] = Math.round(Math.min(p.max, Math.max(p.min, Number(v ?? p.def))));
            else if (p.type === "bool")
                o[p.key] = v === undefined ? p.def : !!v;
            else
                o[p.key] = p.options[Math.min(p.options.length - 1, Math.max(0, Number(v ?? p.def) | 0))];
        }
        return o;
    };
    return {
        id, name: m.name, plugin: m.name, pluginId: id, params, shortcut: "", menu: "effects", help: m.description, ...pluginCategory(),
        render() {
            throw new Error(tr("Este complemento solo se puede usar desde el menú Efectos."));
        },
        // Cada cuadro, con su margen (los píxeles de fuera repiten el borde), en un Worker nuevo.
        start(src, values, rect, info, onProgress) {
            let cancelled = false, job = null;
            const promise = (async () => {
                const w = src.width, h = src.height, out = new Uint8ClampedArray(src.data);
                const area = outputArea(src, rect, m);
                if (!area)
                    return new ImageData(out, w, h);
                const ov = m.overlap, side = Math.max(16, Math.floor(Math.sqrt(BAND_PIXELS)) - 2 * ov), base = jsonValues(values);
                const tiles = [];
                for (let y = area.y; y < area.y + area.h; y += side)
                    for (let x = area.x; x < area.x + area.w; x += side)
                        tiles.push({ x, y, w: Math.min(side, area.x + area.w - x), h: Math.min(side, area.y + area.h - y) });
                let done = 0;
                for (const tile of tiles) {
                    if (cancelled)
                        return null;
                    const ix = tile.x - ov, iy = tile.y - ov, iw = tile.w + 2 * ov, ih = tile.h + 2 * ov;
                    const input = new Float32Array(iw * ih * 4), sd = src.data;
                    for (let y = 0; y < ih; ++y) {
                        const sy = Math.min(h - 1, Math.max(0, iy + y));
                        for (let x = 0; x < iw; ++x) {
                            const sx = Math.min(w - 1, Math.max(0, ix + x)), s = (sy * w + sx) * 4, o = (y * iw + x) * 4;
                            input[o] = sd[s] / 255; input[o + 1] = sd[s + 1] / 255; input[o + 2] = sd[s + 2] / 255; input[o + 3] = sd[s + 3] / 255;
                        }
                    }
                    const original = input.slice();
                    const json = JSON.stringify({ ...base, _image: { x: ix, y: iy, width: iw, height: ih, overlap: ov, canvasWidth: w,
                                                                       canvasHeight: h, mode: "rgb", depth: 8, alpha: true, channels: 4 } });
                    job = inWorker({ op: "filter", module, pixels: input, width: iw, height: ih, params: json }, [input.buffer]);
                    const result = await job.promise;
                    job = null;
                    if (!result || cancelled)
                        return null;
                    const px = result.pixels;
                    for (let y = 0; y < tile.h; ++y)
                        for (let x = 0; x < tile.w; ++x) {
                            const i = ((y + ov) * iw + x + ov) * 4, o = ((tile.y + y) * w + tile.x + x) * 4;
                            for (let c = 0; c < 4; ++c)
                                out[o + c] = toByte(Number.isFinite(px[i + c]) ? px[i + c] : original[i + c]);
                        }
                    onProgress?.(++done / tiles.length);
                }
                return new ImageData(out, w, h);
            })();
            return { promise, cancel() {
                cancelled = true;
                job?.cancel();
            } };
        },
    };
}

async function loadWasm(record) {
    let module;
    try {
        module = await WebAssembly.compile(record.data);
    } catch (e) {
        throw new Error(tr("El módulo WebAssembly no es válido (%1).", e.message));
    }
    const imports = WebAssembly.Module.imports(module);
    if (imports.length)
        throw new Error(tr("El módulo importa %1, pero los complementos no tienen acceso a nada de fuera (ni archivos, ni red, ni reloj).",
                           `${imports[0].module}.${imports[0].name}`));
    const exports = new Set(WebAssembly.Module.exports(module).map((e) => e.name));
    for (const name of ["pc_abi_version", "pc_manifest", "pc_alloc", "pc_filter"])
        if (!exports.has(name))
            throw new Error(tr("No es un complemento de filtros de PhotoCraft: falta la función %1.", name));
    const job = inWorker({ op: "manifest", module });
    const reply = await job.promise;
    if (reply.abi !== ABI_VERSION)
        throw new Error(tr("Usa la versión %1 de la interfaz de complementos; Kaint admite la %2.", reply.abi, ABI_VERSION));
    const m = parseManifest(reply.manifest);
    const id = `craft.wasm.${m.id}`;
    return { id, kind: "PhotoCraft", name: m.name, version: m.version, author: m.author, description: m.description,
             effects: [wasmEffect(module, m, id)], fileTypes: [] };
}

// ===========================================================================================
// Looks: tablas de color .cube y .3dl
// ===========================================================================================

function parseCube(text) {
    const lut = { size: 0, is3d: true, data: null, min: [0, 0, 0], max: [1, 1, 1], title: "" };
    const values = [];
    for (let line of text.split(/\r?\n/)) {
        line = line.trim();
        if (!line || line.startsWith("#"))
            continue;
        const t = line.split(/\s+/), key = t[0].toUpperCase();
        if (key === "TITLE")
            lut.title = line.slice(5).trim().replace(/"/g, "");
        else if (key === "LUT_3D_SIZE" || key === "LUT_1D_SIZE") {
            lut.is3d = key === "LUT_3D_SIZE";
            lut.size = Number(t[1]) | 0;
        } else if (key === "DOMAIN_MIN" || key === "DOMAIN_MAX")
            lut[key === "DOMAIN_MIN" ? "min" : "max"] = [Number(t[1]), Number(t[2]), Number(t[3])];
        else if (key === "LUT_1D_INPUT_RANGE" || key === "LUT_3D_INPUT_RANGE") {
            lut.min = [Number(t[1]), Number(t[1]), Number(t[1])];
            lut.max = [Number(t[2]), Number(t[2]), Number(t[2])];
        } else if (t.length >= 3) {
            const row = t.slice(0, 3).map(Number);
            if (row.some((v) => !Number.isFinite(v)))
                throw new Error(tr("Línea de la tabla que no se entiende: %1", line.slice(0, 60)));
            values.push(...row);
        }
    }
    const expected = lut.is3d ? lut.size ** 3 * 3 : lut.size * 3;
    if (lut.size < 2 || lut.size > 256 || values.length !== expected)
        throw new Error(tr("La tabla de color no tiene el tamaño que dice (%1).", lut.size));
    lut.data = Float32Array.from(values);
    return lut;
}

function parse3dl(text) {
    // Una línea con la malla y luego R G B enteros, con el azul cambiando más rápido.
    let mesh = null;
    const values = [];
    for (let line of text.split(/\r?\n/)) {
        line = line.trim();
        if (!line || line.startsWith("#"))
            continue;
        const row = line.split(/\s+/).map(Number);
        if (row.some((v) => !Number.isFinite(v)))
            continue;
        if (row.length > 3 && !mesh && !values.length)
            mesh = row;
        else if (row.length === 3)
            values.push(...row);
    }
    const count = values.length / 3, n = mesh ? mesh.length : Math.round(Math.cbrt(count));
    if (n < 2 || n > 256 || n ** 3 !== count)
        throw new Error(tr("La tabla .3dl no tiene el tamaño de su malla."));
    let top = 0;
    for (const v of values)
        top = v > top ? v : top;
    const scale = top <= 1 ? 1 : top <= 1023 ? 1023 : top <= 4095 ? 4095 : 65535;
    const data = new Float32Array(values.length);
    for (let r = 0; r < n; ++r)
        for (let g = 0; g < n; ++g)
            for (let b = 0; b < n; ++b) {
                const from = ((r * n + g) * n + b) * 3, to = ((b * n + g) * n + r) * 3;
                for (let c = 0; c < 3; ++c)
                    data[to + c] = values[from + c] / scale;
            }
    return { size: n, is3d: true, data, min: [0, 0, 0], max: [1, 1, 1], title: "" };
}

// Interpolación tetraédrica entre los 8 vecinos (la habitual en las tablas de color).
function applyLut(lut, r, g, b, out) {
    const n = lut.size, d = lut.data, t = [r, g, b];
    for (let c = 0; c < 3; ++c) {
        const span = lut.max[c] - lut.min[c];
        t[c] = clamp01(span > 0 ? (t[c] - lut.min[c]) / span : t[c]) * (n - 1);
    }
    if (!lut.is3d) {
        for (let c = 0; c < 3; ++c) {
            const i = Math.min(Math.floor(t[c]), n - 2), f = t[c] - i;
            out[c] = d[i * 3 + c] * (1 - f) + d[(i + 1) * 3 + c] * f;
        }
        return;
    }
    const ix = Math.min(Math.floor(t[0]), n - 2), iy = Math.min(Math.floor(t[1]), n - 2), iz = Math.min(Math.floor(t[2]), n - 2);
    const fx = t[0] - ix, fy = t[1] - iy, fz = t[2] - iz;
    const at = (x, y, z) => ((iz + z) * n * n + (iy + y) * n + ix + x) * 3;
    let a, bb, w0, wa, wb, w1;
    if (fx >= fy) {
        if (fy >= fz) { a = at(1, 0, 0); bb = at(1, 1, 0); w0 = 1 - fx; wa = fx - fy; wb = fy - fz; w1 = fz; }
        else if (fx >= fz) { a = at(1, 0, 0); bb = at(1, 0, 1); w0 = 1 - fx; wa = fx - fz; wb = fz - fy; w1 = fy; }
        else { a = at(0, 0, 1); bb = at(1, 0, 1); w0 = 1 - fz; wa = fz - fx; wb = fx - fy; w1 = fy; }
    } else if (fz >= fy) { a = at(0, 0, 1); bb = at(0, 1, 1); w0 = 1 - fz; wa = fz - fy; wb = fy - fx; w1 = fx; }
    else if (fz >= fx) { a = at(0, 1, 0); bb = at(0, 1, 1); w0 = 1 - fy; wa = fy - fz; wb = fz - fx; w1 = fx; }
    else { a = at(0, 1, 0); bb = at(1, 1, 0); w0 = 1 - fy; wa = fy - fx; wb = fx - fz; w1 = fz; }
    const c0 = at(0, 0, 0), c1 = at(1, 1, 1);
    for (let k = 0; k < 3; ++k)
        out[k] = w0 * d[c0 + k] + wa * d[a + k] + wb * d[bb + k] + w1 * d[c1 + k];
}

const amountParam = () => ({ key: "amount", label: "Cantidad", type: "int", min: 0, max: 100, def: 100, decimals: 0 });

// Recorre 'rect' de 'src' (ImageData) y escribe en una copia lo que devuelve f(r, g, b, x, y) (0-1).
function mapPixels(src, rect, amount, f) {
    const w = src.width, s = src.data, out = new Uint8ClampedArray(s), c = [0, 0, 0];
    for (let y = rect.y; y < rect.y + rect.h; ++y)
        for (let x = rect.x; x < rect.x + rect.w; ++x) {
            const i = (y * w + x) * 4, r = s[i] / 255, g = s[i + 1] / 255, b = s[i + 2] / 255;
            f(r, g, b, x, y, c);
            out[i] = toByte(r + (c[0] - r) * amount);
            out[i + 1] = toByte(g + (c[1] - g) * amount);
            out[i + 2] = toByte(b + (c[2] - b) * amount);
        }
    return new ImageData(out, w, src.height);
}

async function loadLut(record) {
    const text = new TextDecoder().decode(record.data);
    const lut = /\.3dl$/i.test(record.fileName) ? parse3dl(text) : parseCube(text);
    const name = (lut.title || record.fileName.replace(/\.[^.]+$/, "")).slice(0, 64);
    const id = `craft.look.${await shortHash(record.fileName)}`;
    const effect = {
        id, name, plugin: name, pluginId: id, params: [amountParam()], shortcut: "", menu: "effects", ...pluginCategory(),
        render(src, values, rect) {
            const k = Math.min(100, Math.max(0, Number(values?.amount ?? 100))) / 100;
            return mapPixels(src, rect, k, (r, g, b, x, y, out) => applyLut(lut, r, g, b, out));
        },
    };
    return { id, kind: "LightCraft (look)", name, version: "", author: "",
             description: tr("Tabla de color %1 de %2 puntos por canal.", lut.is3d ? "3D" : "1D", lut.size), effects: [effect], fileTypes: [] };
}

// ===========================================================================================
// Ajustes de revelado .xmp
// ===========================================================================================

const CRS = "http://ns.adobe.com/camera-raw-settings/1.0/";
const HUE_BANDS = ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"];
const HUE_CENTERS = [0, 30, 60, 120, 180, 240, 270, 300];
const CURVES = ["ToneCurvePV2012", "ToneCurvePV2012Red", "ToneCurvePV2012Green", "ToneCurvePV2012Blue"];

function parseXmp(text) {
    // Los ajustes van como atributos crs:Nombre="valor" o como elementos <crs:Nombre>valor</...>;
    // las curvas, como una lista de "x, y".
    const doc = new DOMParser().parseFromString(text, "application/xml");
    if (doc.getElementsByTagName("parsererror").length)
        throw new Error(tr("El archivo .xmp no es XML válido: %1", doc.getElementsByTagName("parsererror")[0].textContent.slice(0, 80)));
    const values = new Map(), curves = [[], [], [], []];
    let name = "";
    const store = (key, value) => {
        if (key === "ConvertToGrayscale")
            return values.set(key, /^true$/i.test(value.trim()) ? 1 : 0);
        if (key === "Name" || key === "PresetName") {
            name = name || value.trim();
            return;
        }
        const v = Number(value.replace("+", ""));
        if (Number.isFinite(v) && value.trim() !== "")
            values.set(key, v);
    };
    for (const node of doc.getElementsByTagName("*")) {
        for (const attr of node.attributes)
            if (attr.namespaceURI === CRS)
                store(attr.localName, attr.value);
        if (node.namespaceURI !== CRS)
            continue;
        const c = CURVES.indexOf(node.localName);
        if (c >= 0) {
            for (const li of node.getElementsByTagNameNS("*", "li")) {
                const xy = li.textContent.split(",");
                if (xy.length === 2)
                    curves[c].push([Number(xy[0]), Number(xy[1])]);
            }
        } else if (node.localName === "Name") {
            store("Name", node.textContent);
        } else if (!node.children.length) {
            store(node.localName, node.textContent);
        }
    }
    if (!values.size && curves.every((c) => !c.length))
        throw new Error(tr("El archivo .xmp no tiene ajustes de revelado (crs:)."));
    return { name, values, curves };
}

// Interpolación cúbica monótona (Fritsch-Carlson) de los puntos de una curva, en 256 valores.
function curveLut(points) {
    const pts = [...points].sort((a, b) => a[0] - b[0]), lut = new Float32Array(256), n = pts.length;
    if (n < 2) {
        for (let i = 0; i < 256; ++i)
            lut[i] = i / 255;
        return lut;
    }
    const d = [], m = new Array(n);
    for (let i = 0; i < n - 1; ++i)
        d.push((pts[i + 1][1] - pts[i][1]) / Math.max(1e-6, pts[i + 1][0] - pts[i][0]));
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (let i = 1; i < n - 1; ++i)
        m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
    for (let i = 0; i < n - 1; ++i) {
        if (d[i] === 0) {
            m[i] = m[i + 1] = 0;
            continue;
        }
        const a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b;
        if (s > 9) {
            const t = 3 / Math.sqrt(s);
            m[i] = t * a * d[i];
            m[i + 1] = t * b * d[i];
        }
    }
    for (let x = 0; x < 256; ++x) {
        let y;
        if (x <= pts[0][0])
            y = pts[0][1];
        else if (x >= pts[n - 1][0])
            y = pts[n - 1][1];
        else {
            let i = 0;
            while (i < n - 2 && x > pts[i + 1][0])
                ++i;
            const h = pts[i + 1][0] - pts[i][0], t = (x - pts[i][0]) / h, t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * pts[i][1] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * pts[i + 1][1] + (t3 - t2) * h * m[i + 1];
        }
        lut[x] = clamp01(y / 255);
    }
    return lut;
}

function rgbToHsv(c) {
    const mx = Math.max(c[0], c[1], c[2]), mn = Math.min(c[0], c[1], c[2]), d = mx - mn;
    let h = 0;
    if (d > 1e-6)
        h = mx === c[0] ? 60 * (((c[1] - c[2]) / d + 6) % 6) : mx === c[1] ? 60 * ((c[2] - c[0]) / d + 2) : 60 * ((c[0] - c[1]) / d + 4);
    return [h, mx > 0 ? d / mx : 0, mx];
}

function hsvToRgb(h, s, v, out) {
    h = (((h % 360) + 360) % 360) / 60;
    const i = Math.floor(h) % 6, f = h - Math.floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    const table = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i];
    out[0] = table[0]; out[1] = table[1]; out[2] = table[2];
}

const luma = (c) => 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
const smoothstep = (a, b, x) => (b <= a ? (x >= b ? 1 : 0) : ((t) => t * t * (3 - 2 * t))(clamp01((x - a) / (b - a))));

// Peso de cada banda de color de HSL para un tono (las dos más cercanas, en proporción).
function hueWeights(hue, w) {
    w.fill(0);
    hue = ((hue % 360) + 360) % 360;
    for (let i = 0; i < 8; ++i) {
        const a = HUE_CENTERS[i], b = i === 7 ? 360 : HUE_CENTERS[i + 1];
        if (hue >= a && hue < b) {
            const t = (hue - a) / (b - a);
            w[i] = 1 - t;
            w[(i + 1) % 8] += t;
            return;
        }
    }
}

// Desenfoque de caja (tres pasadas, casi gaussiano) de un canal de coma flotante.
function blurPlane(p, w, h, radius) {
    let src = p, dst = new Float32Array(p.length);
    for (let pass = 0; pass < 3; ++pass)
        for (const horizontal of [true, false]) {
            const n = horizontal ? w : h, lines = horizontal ? h : w;
            for (let line = 0; line < lines; ++line) {
                const at = (i) => (horizontal ? line * w + i : i * w + line);
                let sum = 0, count = 0;
                for (let i = 0; i <= Math.min(n - 1, radius); ++i, ++count)
                    sum += src[at(i)];
                for (let i = 0; i < n; ++i) {
                    dst[at(i)] = sum / Math.max(1, count);
                    const leave = i - radius, enter = i + radius + 1;
                    if (leave >= 0) { sum -= src[at(leave)]; --count; }
                    if (enter < n) { sum += src[at(enter)]; ++count; }
                }
            }
            [src, dst] = [dst, src];
        }
    return src;
}

function developRender(dev, src, rect, amount) {
    const v = (key, def = 0) => (dev.values.has(key) ? dev.values.get(key) : def), has = (key) => dev.values.has(key);
    const w = rect.w, h = rect.h, sw = src.width, sd = src.data;
    // Balance de blancos: en las fotos ya reveladas, el relativo; si solo está la temperatura en
    // kelvin, la diferencia con la luz de día.
    let temp = v("IncrementalTemperature") / 100, tint = v("IncrementalTint") / 100;
    if (!has("IncrementalTemperature") && has("Temperature"))
        temp = Math.min(1, Math.max(-1, (v("Temperature") - 5500) / 4500)) * 0.6;
    if (!has("IncrementalTint") && has("Tint"))
        tint = Math.min(1, Math.max(-1, v("Tint") / 150));
    const wb = [1 + 0.25 * temp + 0.08 * tint, 1 - 0.18 * tint, 1 - 0.25 * temp + 0.08 * tint];
    const exposure = Math.pow(2, v("Exposure2012", v("Exposure")));
    const dehaze = v("Dehaze") / 100;
    const contrast = v("Contrast2012", v("Contrast")) / 100 + 0.3 * dehaze;
    const highlights = v("Highlights2012", -v("HighlightRecovery")) / 100, shadows = v("Shadows2012", v("FillLight")) / 100;
    const whites = v("Whites2012") / 100, blacks = v("Blacks2012", -v("Shadows") / 2) / 100;
    const clarity = v("Clarity2012", v("Clarity")) / 100, texture = v("Texture") / 100;
    const vibrance = v("Vibrance") / 100, saturation = v("Saturation") / 100 + 0.2 * dehaze;
    const split = [v("ParametricShadowSplit", 25) / 100, v("ParametricMidtoneSplit", 50) / 100, v("ParametricHighlightSplit", 75) / 100, 1];
    const parametric = [v("ParametricShadows") / 100, v("ParametricDarks") / 100, v("ParametricLights") / 100, v("ParametricHighlights") / 100];
    const hueAdj = HUE_BANDS.map((b) => v(`HueAdjustment${b}`) / 100), satAdj = HUE_BANDS.map((b) => v(`SaturationAdjustment${b}`) / 100);
    const lumAdj = HUE_BANDS.map((b) => v(`LuminanceAdjustment${b}`) / 100), grayAdj = HUE_BANDS.map((b) => v(`GrayMixer${b}`) / 100);
    const gray = v("ConvertToGrayscale") > 0.5;
    const tone = (hue, sat, lum) => {
        const t = [0, 0, 0];
        hsvToRgb(hue, 1, 1, t);
        const y = Math.max(0.05, luma(t));
        return { tint: t.map((c) => c / y), strength: (sat / 100) * 0.35, lum: (lum / 100) * 0.2 };
    };
    const tones = [
        [tone(v("ColorGradeShadowHue", v("SplitToningShadowHue")), Math.max(v("ColorGradeShadowSat"), v("SplitToningShadowSaturation")), v("ColorGradeShadowLum")), "s"],
        [tone(v("ColorGradeHighlightHue", v("SplitToningHighlightHue")), Math.max(v("ColorGradeHighlightSat"), v("SplitToningHighlightSaturation")), v("ColorGradeHighlightLum")), "h"],
        [tone(v("ColorGradeMidtoneHue"), v("ColorGradeMidtoneSat"), v("ColorGradeMidtoneLum")), "m"],
        [tone(v("ColorGradeGlobalHue"), v("ColorGradeGlobalSat"), v("ColorGradeGlobalLum")), "g"],
    ];
    const balance = Math.max(-100, Math.min(100, v("ColorGradeBalance", v("SplitToningBalance")))) / 100;
    const vignette = v("PostCropVignetteAmount") / 100, vMid = 0.4 + 0.8 * (v("PostCropVignetteMidpoint", 50) / 100);
    const vFeather = 0.05 + 0.9 * (v("PostCropVignetteFeather", 50) / 100);
    const grain = v("GrainAmount") / 100, grainCell = 1 + Math.floor(v("GrainSize", 25) / 25);
    const curves = dev.curves.map((c) => (c.length && c.some(([x, y]) => Math.abs(x - y) > 0.5) ? curveLut(c) : null));

    // Curva de tonos de las luminancias: contraste, luces y sombras, blancos y negros y la
    // paramétrica (las de Kaint, a la manera de Camera Raw), en una tabla.
    const toneLut = new Float32Array(1024);
    for (let i = 0; i < 1024; ++i) {
        let t = i / 1023;
        if (dehaze > 0)
            t = clamp01((t - 0.04 * dehaze) / (1 - 0.04 * dehaze));
        if (contrast !== 0) {
            const e = contrast > 0 ? 1 + 1.5 * contrast : 1 / (1 - 1.5 * Math.max(contrast, -0.66));
            t = t < 0.5 ? 0.5 * Math.pow(2 * t, e) : 1 - 0.5 * Math.pow(2 * (1 - t), e);
        }
        const ws = 6.75 * t * (1 - t) * (1 - t), wh = 6.75 * t * t * (1 - t);
        t += 0.25 * (shadows * ws + highlights * wh);
        t += 0.25 * (whites * t * t * t + blacks * (1 - t) ** 3);
        let lo = 0;
        for (let z = 0; z < 4; ++z) {
            const hi = split[z], c = (lo + hi) / 2, width = Math.max(0.12, hi - lo), b = Math.max(0, 1 - Math.abs(t - c) / width);
            t += 0.2 * parametric[z] * b * b;
            lo = hi;
        }
        toneLut[i] = clamp01(t);
    }
    const toneAt = (t) => {
        const f = clamp01(t) * 1023, i = Math.min(Math.floor(f), 1022);
        return toneLut[i] + (toneLut[i + 1] - toneLut[i]) * (f - i);
    };
    const toLinear = new Float32Array(256).map((_, i) => srgbToLinear(i / 255));
    // Primera pasada: balance, exposición y tonos (y la luminancia, para la claridad).
    const rgb = new Float32Array(w * h * 3), lum = new Float32Array(w * h), c = [0, 0, 0];
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const s = ((rect.y + y) * sw + rect.x + x) * 4, i = y * w + x;
            for (let k = 0; k < 3; ++k)
                c[k] = linearToSrgb(Math.min(4, toLinear[sd[s + k]] * wb[k] * exposure));
            const l = luma(c), l2 = toneAt(l);
            for (let k = 0; k < 3; ++k)
                rgb[i * 3 + k] = l > 1e-5 ? (c[k] * l2) / l : l2;
            lum[i] = l2;
        }
    const wide = clarity ? blurPlane(lum.slice(), w, h, Math.max(2, Math.floor(Math.min(w, h) / 60))) : null;
    const fine = texture ? blurPlane(lum.slice(), w, h, 2) : null;
    const out = new Uint8ClampedArray(sd), bw = new Float32Array(8), cx = (src.width - 1) / 2, cy = (src.height - 1) / 2;
    const apply = (lut, t) => {
        const f = clamp01(t) * 255, j = Math.min(Math.floor(f), 254);
        return lut[j] + (lut[j + 1] - lut[j]) * (f - j);
    };
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const i = y * w + x, s = ((rect.y + y) * sw + rect.x + x) * 4;
            c[0] = rgb[i * 3]; c[1] = rgb[i * 3 + 1]; c[2] = rgb[i * 3 + 2];
            let l = lum[i], l2 = l;
            // Claridad (contraste local amplio, más en los medios tonos) y textura (fino).
            if (wide)
                l2 += clarity * 0.8 * (l - wide[i]) * (1 - Math.abs(2 * l - 1));
            if (fine)
                l2 += texture * 0.6 * (l - fine[i]);
            if (l2 !== l) {
                l2 = clamp01(l2);
                for (let k = 0; k < 3; ++k)
                    c[k] = l > 1e-5 ? (c[k] * l2) / l : l2;
                l = l2;
            }
            // Curvas de puntos: la general en los tres canales y luego la de cada uno.
            for (let k = 0; k < 3; ++k) {
                if (curves[0])
                    c[k] = apply(curves[0], c[k]);
                if (curves[k + 1])
                    c[k] = apply(curves[k + 1], c[k]);
            }
            // HSL por bandas de color, intensidad y saturación (o blanco y negro con su mezcla).
            let [hh, ss, vv] = rgbToHsv(c);
            hueWeights(hh, bw);
            let dh = 0, ds = 0, dl = 0, dg = 0;
            for (let b = 0; b < 8; ++b) {
                dh += bw[b] * hueAdj[b]; ds += bw[b] * satAdj[b]; dl += bw[b] * lumAdj[b]; dg += bw[b] * grayAdj[b];
            }
            if (gray) {
                c[0] = c[1] = c[2] = clamp01(luma(c) * (1 + 0.7 * dg * ss));
            } else {
                hh += dh * 30;
                ss *= 1 + ds;
                vv *= 1 + 0.5 * dl * ss;
                ss *= 1 + vibrance * (1 - ss);
                ss *= 1 + saturation;
                hsvToRgb(hh, clamp01(ss), clamp01(vv), c);
            }
            // Gradación: sombras, luces, medios y global.
            const yv = clamp01(luma(c) - 0.25 * balance), zs = (1 - yv) * (1 - yv), zh = yv * yv, zones = { s: zs, h: zh, m: Math.max(0, 1 - zs - zh), g: 1 };
            for (const [t, z] of tones) {
                const k = t.strength * zones[z];
                for (let ch = 0; ch < 3; ++ch)
                    c[ch] = c[ch] + (c[ch] * t.tint[ch] - c[ch]) * k + t.lum * zones[z];
            }
            // Viñeta tras el recorte (sobre la imagen entera) y grano.
            if (vignette) {
                const dx = (rect.x + x - cx) / Math.max(1, cx), dy = (rect.y + y - cy) / Math.max(1, cy);
                const f = smoothstep(vMid - vFeather / 2, vMid + vFeather / 2, Math.sqrt(dx * dx + dy * dy));
                for (let k = 0; k < 3; ++k)
                    c[k] = vignette < 0 ? c[k] * (1 + vignette * f) : c[k] + (1 - c[k]) * vignette * f;
            }
            if (grain > 0) {
                let hsh = (Math.imul(Math.floor((rect.x + x) / grainCell), 73856093) ^ Math.imul(Math.floor((rect.y + y) / grainCell), 19349663)) >>> 0;
                hsh ^= hsh >>> 13;
                hsh = Math.imul(hsh, 0x5bd1e995) >>> 0;
                hsh ^= hsh >>> 15;
                const noise = (((hsh & 0xffff) / 65535) - 0.5) * 0.12 * grain;
                for (let k = 0; k < 3; ++k)
                    c[k] += noise;
            }
            for (let k = 0; k < 3; ++k) {
                const o = sd[s + k] / 255;
                out[s + k] = toByte(o + (c[k] - o) * amount);
            }
        }
    return new ImageData(out, src.width, src.height);
}

async function loadXmp(record) {
    const dev = parseXmp(new TextDecoder().decode(record.data));
    const name = (dev.name || record.fileName.replace(/\.[^.]+$/, "")).slice(0, 64);
    const id = `craft.preset.${await shortHash(record.fileName)}`;
    const effect = {
        id, name, plugin: name, pluginId: id, params: [amountParam()], shortcut: "", menu: "effects", ...pluginCategory(),
        render(src, values, rect) {
            return developRender(dev, src, rect, Math.min(100, Math.max(0, Number(values?.amount ?? 100))) / 100);
        },
    };
    return { id, kind: "LightCraft (preset)", name, version: "", author: "",
             description: tr("Ajuste de revelado con %1 valores.", dev.values.size), effects: [effect], fileTypes: [] };
}

// Crea el complemento de un registro guardado ({ fileName, data: bytes }).
export async function loadCraft(record) {
    const lower = record.fileName.toLowerCase();
    if (lower.endsWith(".wasm"))
        return loadWasm(record);
    if (lower.endsWith(".xmp"))
        return loadXmp(record);
    return loadLut(record);
}

// Solo para las pruebas.
export const testing = { parseCube, parse3dl, applyLut, parseXmp, parseManifest };

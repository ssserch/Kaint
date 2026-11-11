// Paquetes de iconos con el formato de los de paint.js (src/app/IconPacks.cpp de Kaint): un ZIP con
// asset-pack.json y las imágenes que sustituye (icons/<archivo>.png, cursors/...). Se guardan en el
// navegador (IndexedDB) y el activo reemplaza los iconos que trae; los demás siguen siendo los de
// Kaint.
import { unzip, zip } from "./core/Zip.js";
import { Settings } from "./Settings.js";
import { tr } from "./i18n.js";

const DB = "kaint", STORE = "iconPacks";
const FOLDERS = ["icons", "cursors", "images"];
const MANIFEST = "asset-pack.json";
export const DEFAULT_PACK = "kaint-default";

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

export const validId = (id) => typeof id === "string" && /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(id) && !id.includes("..");

function builtin() {
    return { id: DEFAULT_PACK, name: tr("Kaint (predeterminado)"), author: "SSSerch", version: "",
             description: tr("Los iconos de siempre de Kaint."), images: 0 };
}

export const IconPacks = {
    // "icons/archivo" → URL de la imagen del paquete activo.
    urls: new Map(),
    generation: 0,

    activeId() {
        return Settings.get("iconPack") || DEFAULT_PACK;
    },

    // El predeterminado primero y después los instalados, por nombre.
    async list() {
        let packs = [];
        try {
            packs = (await request("readonly", (s) => s.getAll())) || [];
        } catch {
            // Sin IndexedDB (modo privado de algunos navegadores): solo el predeterminado.
        }
        packs.sort((a, b) => a.name.localeCompare(b.name));
        // Para la vista previa, las imágenes de los iconos de los menús que trae cada paquete.
        const urls = (p) => Object.fromEntries(Object.entries(p.files).filter(([k]) => k.startsWith("icons/menu_"))
            .map(([k, blob]) => [k, URL.createObjectURL(blob)]));
        return [builtin(), ...packs.map((p) => ({ id: p.id, name: p.name, author: p.author, version: p.version,
                                                  description: p.description, images: Object.keys(p.files).length, urls: urls(p) }))];
    },

    // Instala un ZIP (sustituye al que tenga el mismo id) y devuelve su id; si no vale, lanza un
    // Error con el motivo.
    async install(file) {
        let entries;
        try {
            entries = await unzip(await file.arrayBuffer());
        } catch {
            throw new Error(tr("No es un archivo ZIP válido."));
        }
        // asset-pack.json en la raíz o dentro de una sola carpeta, como los de paint.js.
        const manifestName = [...entries.keys()].find((n) => n === MANIFEST || (n.endsWith(`/${MANIFEST}`) && n.split("/").length === 2));
        if (!manifestName)
            throw new Error(tr("Falta asset-pack.json: no es un paquete de iconos."));
        const prefix = manifestName.slice(0, -MANIFEST.length);
        let manifest;
        try {
            manifest = JSON.parse(new TextDecoder().decode(entries.get(manifestName)));
        } catch {
            throw new Error(tr("Falta asset-pack.json: no es un paquete de iconos."));
        }
        if ((manifest.formatVersion ?? 1) !== 1)
            throw new Error(tr("El paquete usa una versión del formato que Kaint no conoce."));
        if (!validId(manifest.id))
            throw new Error(tr("El identificador del paquete no es válido."));
        if (manifest.id === DEFAULT_PACK)
            throw new Error(tr("Ese es el identificador del paquete predeterminado."));
        const files = {};
        for (const [name, bytes] of entries) {
            if (!name.startsWith(prefix))
                continue;
            const parts = name.slice(prefix.length).split("/");
            const match = parts.length === 2 && FOLDERS.includes(parts[0]) && /^([A-Za-z0-9._-]+)\.(png|webp)$/i.exec(parts[1]);
            if (!match || !bytes.length)
                continue;
            files[`${parts[0]}/${match[1]}`] = new Blob([bytes], { type: match[2].toLowerCase() === "webp" ? "image/webp" : "image/png" });
        }
        const pack = { id: manifest.id, name: String(manifest.name || manifest.id), author: String(manifest.author || ""),
                       version: String(manifest.version || ""), description: String(manifest.description || ""), files };
        await request("readwrite", (s) => s.put(pack));
        if (this.activeId() === pack.id)
            await this.load();
        return { id: pack.id, images: Object.keys(files).length };
    },

    async remove(id) {
        if (id === DEFAULT_PACK)
            return;
        if (this.activeId() === id)
            await this.activate(DEFAULT_PACK);
        await request("readwrite", (s) => s.delete(id));
    },

    async activate(id) {
        Settings.set("iconPack", id);
        await this.load();
    },

    // Lee el paquete activo y avisa para que se vuelvan a pintar los iconos.
    async load() {
        for (const url of this.urls.values())
            URL.revokeObjectURL(url);
        this.urls = new Map();
        const id = this.activeId();
        if (id !== DEFAULT_PACK) {
            try {
                const pack = await request("readonly", (s) => s.get(id));
                if (pack)
                    for (const [key, blob] of Object.entries(pack.files))
                        this.urls.set(key, URL.createObjectURL(blob));
                else
                    Settings.set("iconPack", DEFAULT_PACK);
            } catch {
                // Sin IndexedDB: se quedan los de Kaint.
            }
        }
        ++this.generation;
        window.dispatchEvent(new Event("kaint-iconpack"));
    },

    file(folder, name) {
        return this.urls.get(`${folder}/${name}`) || null;
    },

    // --- Editor de paquetes (Configuración > Paquetes de iconos > Editor) ---

    // Un identificador a partir del nombre ("Mis iconos" → "mis-iconos").
    idFromName(name) {
        const id = String(name || "").normalize("NFKD").replace(/[\u0300-\u036f]/g, "").toLowerCase()
            .replace(/[^a-z0-9._-]+/g, "-").replace(/^[-._]+|[-._]+$/g, "").slice(0, 64);
        return validId(id) && id !== DEFAULT_PACK ? id : "mis-iconos";
    },

    // Un paquete instalado con sus imágenes ("icons/archivo" → Blob), o null.
    async get(id) {
        if (!validId(id) || id === DEFAULT_PACK)
            return null;
        try {
            return (await request("readonly", (s) => s.get(id))) || null;
        } catch {
            return null;
        }
    },

    // Guarda (instala) un paquete hecho con el editor. Si 'previousId' es otro (se cambió el
    // identificador), el anterior se quita. Si el paquete estaba en uso, se vuelve a cargar.
    async save(info, files, previousId = "") {
        if (!validId(info.id) || info.id === DEFAULT_PACK)
            throw new Error(tr("El identificador del paquete no es válido."));
        const clean = {};
        for (const [key, blob] of Object.entries(files))
            if (/^(icons|cursors|images)\/[A-Za-z0-9._-]+$/.test(key) && blob?.size)
                clean[key] = blob;
        const pack = { id: info.id, name: String(info.name || info.id), author: String(info.author || ""),
                       version: String(info.version || ""), description: String(info.description || ""), files: clean };
        await request("readwrite", (s) => {
            s.put(pack);
            if (previousId && previousId !== info.id && validId(previousId) && previousId !== DEFAULT_PACK)
                s.delete(previousId);
        });
        const active = this.activeId();
        if (active === info.id || (previousId && active === previousId))
            await this.activate(info.id);
    },

    // El ZIP del paquete (asset-pack.json y las imágenes), para compartirlo.
    async zip(info, files) {
        const manifest = { formatVersion: 1, id: info.id, name: info.name || info.id };
        for (const k of ["author", "version", "description"])
            if (info[k])
                manifest[k] = info[k];
        const entries = [{ name: MANIFEST, data: new TextEncoder().encode(JSON.stringify(manifest, null, 4)) }];
        for (const key of Object.keys(files).sort()) {
            const blob = files[key];
            const ext = blob.type === "image/webp" ? "webp" : "png";
            entries.push({ name: `${key}.${ext}`, data: new Uint8Array(await blob.arrayBuffer()) });
        }
        return zip(entries);
    },
};

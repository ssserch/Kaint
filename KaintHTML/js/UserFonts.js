// Fuentes que el usuario añade para la herramienta Texto (src/app/UserFonts.cpp de Kaint): se
// guardan en el navegador (IndexedDB) y se cargan con FontFace al abrir Kaint.
import { tr } from "./i18n.js";

const DB = "kaint-fonts", STORE = "fonts";

function openDb() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB, 1);
        req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: "file" });
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

export const UserFonts = {
    // Fuentes cargadas: [{ file, family, face }].
    loaded: [],

    families() {
        return this.loaded.map((f) => f.family);
    },

    async load() {
        let records = [];
        try {
            records = (await request("readonly", (s) => s.getAll())) || [];
        } catch {
            return;
        }
        for (const r of records)
            await this.addFace(r);
        window.dispatchEvent(new Event("kaint-fonts"));
    },

    async addFace(record) {
        try {
            const face = new FontFace(record.family, record.data);
            await face.load();
            document.fonts.add(face);
            this.loaded = [...this.loaded.filter((f) => f.file !== record.file), { file: record.file, family: record.family, face }];
            return true;
        } catch {
            return false;
        }
    },

    // Añade los archivos (.ttf, .otf, .woff, .woff2); devuelve los que no se pudieron leer.
    async add(files) {
        const errors = [];
        for (const file of files) {
            const record = { file: file.name, family: file.name.replace(/\.[^.]+$/, ""), data: await file.arrayBuffer() };
            if (!(await this.addFace(record))) {
                errors.push(file.name);
                continue;
            }
            await request("readwrite", (s) => s.put(record));
        }
        window.dispatchEvent(new Event("kaint-fonts"));
        return errors;
    },

    async remove(file) {
        const f = this.loaded.find((x) => x.file === file);
        if (f)
            document.fonts.delete(f.face);
        this.loaded = this.loaded.filter((x) => x.file !== file);
        await request("readwrite", (s) => s.delete(file));
        window.dispatchEvent(new Event("kaint-fonts"));
    },

    async removeAll() {
        for (const f of [...this.loaded])
            await this.remove(f.file);
    },

    describe() {
        return this.loaded.length ? "" : tr("Todavía no has añadido ninguna fuente.");
    },
};

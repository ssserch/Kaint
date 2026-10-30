// Copias de recuperación, como src/app/Recovery.cpp de Kaint: cada pocos minutos, las imágenes con
// cambios sin guardar se copian en este navegador (en OpenRaster, en IndexedDB). Si la página se
// cierra de golpe (el navegador falla, el equipo se apaga...), al volver a abrir Kaint se ofrecen.
// Cada página abierta es una sesión que se mantiene bloqueada mientras vive (Web Locks, como el
// QLockFile de Kaint); las copias de las sesiones sin bloqueo que no se cerraron bien son las que
// se pueden recuperar. También se recuerdan los archivos abiertos, para "Volver a abrir las
// imágenes de la última sesión".
import { encode, openFile, SaveFormats } from "./core/ImageIO.js";
import { readIcc } from "./core/ColorProfiles.js";

const DB = "kaint-recovery", COPIES = "copies", SESSION = "session";
// Sesiones que se cerraron bien (sus copias sobran) y, sin Web Locks, el último latido de cada una.
const CLOSED = "kaint.recovery.closed", BEATS = "kaint.recovery.beats";
const BEAT = 20000, STALE = 90000;
const session = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
let nextId = 1;

function openDb() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB, 1);
        req.onupgradeneeded = () => {
            req.result.createObjectStore(COPIES, { keyPath: "key" });
            req.result.createObjectStore(SESSION);
        };
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    });
}

async function request(store, mode, fn) {
    const db = await openDb();
    return new Promise((resolve, reject) => {
        const t = db.transaction(store, mode);
        const r = fn(t.objectStore(store));
        t.oncomplete = () => resolve(r?.result);
        t.onerror = () => reject(t.error);
    });
}

function readJson(key, fallback) {
    try {
        return JSON.parse(localStorage.getItem(key) || "null") ?? fallback;
    } catch {
        return fallback;
    }
}

function writeJson(key, value) {
    try {
        localStorage.setItem(key, JSON.stringify(value));
    } catch {
        // Sin almacenamiento.
    }
}

// Las sesiones que siguen abiertas (en esta pestaña o en otras).
async function aliveSessions() {
    const alive = new Set([session]);
    if (navigator.locks?.query) {
        const { held = [] } = await navigator.locks.query();
        for (const lock of held)
            if (lock.name.startsWith("kaint-session-"))
                alive.add(lock.name.slice("kaint-session-".length));
    } else {
        const now = Date.now();
        for (const [id, beat] of Object.entries(readJson(BEATS, {})))
            if (now - beat < STALE)
                alive.add(id);
    }
    return alive;
}

function beat() {
    const beats = readJson(BEATS, {}), now = Date.now();
    for (const [id, t] of Object.entries(beats))
        if (now - t > STALE * 10)
            delete beats[id];
    beats[session] = now;
    writeJson(BEATS, beats);
}

export const Recovery = {
    // Bloquea la sesión de esta página y, al cerrarla bien, la marca como cerrada.
    start() {
        if (navigator.locks?.request) {
            navigator.locks.request(`kaint-session-${session}`, () => new Promise(() => {})).catch(() => {});
        } else {
            beat();
            setInterval(beat, BEAT);
        }
        window.addEventListener("pagehide", () => this.clear());
        // Vuelta desde la caché del navegador (atrás y adelante): la sesión sigue.
        window.addEventListener("pageshow", (e) => {
            if (e.persisted)
                writeJson(CLOSED, readJson(CLOSED, []).filter((id) => id !== session));
        });
    },

    // Copia las imágenes con cambios sin guardar y borra las demás copias de esta sesión.
    async save(docs) {
        try {
            const kept = new Set();
            const ora = SaveFormats.find((f) => f.layered);
            for (const doc of docs) {
                if (!doc.isModified())
                    continue;
                // Número fijo por documento aunque cambie el orden de las pestañas.
                doc.recoveryId ||= nextId++;
                const key = `${session}/${doc.recoveryId}`;
                try {
                    const data = await (await encode(doc, ora)).arrayBuffer();
                    // Sin el FileSystemFileHandle: Chromium cierra la pestaña al leerlo de IndexedDB
                    // en una ventana de incógnito, y las copias se leen al abrir Kaint.
                    await request(COPIES, "readwrite", (s) => s.put({ key, session, name: doc.displayName(), docName: doc.name, fileName: doc.fileName,
                                                                     dpi: doc.dpi, icc: doc.colorProfile?.icc || null, data, time: Date.now() }));
                    kept.add(key);
                } catch {
                    // Sin espacio: sin copia de esta imagen.
                }
            }
            const keys = await request(COPIES, "readonly", (s) => s.getAllKeys());
            const stale = keys.filter((k) => k.startsWith(`${session}/`) && !kept.has(k));
            if (stale.length)
                await request(COPIES, "readwrite", (s) => stale.forEach((k) => s.delete(k)));
        } catch {
            // Sin IndexedDB (modo privado de algunos navegadores): sin copias.
        }
    },

    // Kaint se cerró bien: las copias de esta sesión sobran. Se marca al momento (localStorage) por
    // si la página se va antes de que se borren.
    clear() {
        const closed = readJson(CLOSED, []);
        if (!closed.includes(session))
            writeJson(CLOSED, [...closed, session]);
        if (!navigator.locks?.request) {
            const beats = readJson(BEATS, {});
            delete beats[session];
            writeJson(BEATS, beats);
        }
        request(COPIES, "readonly", (s) => s.getAllKeys())
            .then((keys) => request(COPIES, "readwrite", (s) => keys.filter((k) => k.startsWith(`${session}/`)).forEach((k) => s.delete(k))))
            .catch(() => {});
    },

    // Las copias de las sesiones que se cerraron de golpe: [{ key, name, ... }]. Las de las que se
    // cerraron bien se borran.
    async orphaned() {
        try {
            const copies = await request(COPIES, "readonly", (s) => s.getAll());
            const alive = await aliveSessions(), closed = new Set(readJson(CLOSED, []));
            const out = [], leftovers = [];
            for (const c of copies) {
                if (alive.has(c.session))
                    continue;
                (closed.has(c.session) ? leftovers : out).push(c);
            }
            if (leftovers.length)
                await this.discard(leftovers);
            writeJson(CLOSED, [...closed].filter((id) => alive.has(id)));
            return out.sort((a, b) => a.time - b.time);
        } catch {
            return [];
        }
    },

    async discard(entries) {
        try {
            await request(COPIES, "readwrite", (s) => entries.forEach((e) => s.delete(e.key)));
        } catch {
            // Sin IndexedDB.
        }
    },

    // El documento de una copia, con sus cambios sin guardar.
    async restore(entry) {
        const doc = await openFile(new File([entry.data], "recuperacion.ora"));
        doc.name = entry.docName || "";
        doc.fileName = entry.fileName || "";
        doc.dpi = entry.dpi || 96;
        doc.colorProfile = entry.icc ? readIcc(entry.icc) : null;
        doc.cleanIndex = -1;
        return doc;
    },

    // Los archivos abiertos (los que el navegador deja volver a abrir: FileSystemFileHandle). Solo
    // se guardan y se leen con "Volver a abrir las imágenes de la última sesión" activado.
    async rememberSession(docs) {
        try {
            const handles = docs.map((d) => d.fileHandle).filter(Boolean);
            await request(SESSION, "readwrite", (s) => s.put(handles, "files"));
        } catch {
            // Sin IndexedDB o sin archivos del sistema.
        }
    },

    async lastSession() {
        try {
            return (await request(SESSION, "readonly", (s) => s.get("files"))) || [];
        } catch {
            return [];
        }
    },
};

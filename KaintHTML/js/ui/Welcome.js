// Kaint.APK: la pantalla de bienvenida al abrir la app (como la de ibis Paint). De fondo, la última
// imagen editada muy difuminada; en el centro, Abrir (las imágenes recientes y un botón para
// importar), Nueva (un lienzo blanco, el de siempre de Kaint) y Colaboración (su menú); abajo, las
// recientes; arriba, la configuración de esta pantalla. Al entrar, el difuminado se va y aparece el
// editor. Las imágenes recientes se guardan en este dispositivo (IndexedDB) al salir de la app o
// guardarlas: en Android no se puede volver a abrir un archivo por su ruta.
import { Settings } from "../Settings.js";
import { Document } from "../core/Document.js";
import { encode, openFile, SaveFormats } from "../core/ImageIO.js";
import { tr } from "../i18n.js";
import { VERSION } from "../version.js";
import { el } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { popupMenu } from "./Menu.js";

// Dos almacenes: las miniaturas y el nombre (lo que lee la bienvenida al abrirse) y, aparte, la
// imagen entera (solo al abrirla). Antes iba todo junto y abrir Kaint.APK leía todas las imágenes
// recientes con sus capas: el arranque se hacía lento.
const DB = "kaint-recents", STORE = "meta", DATA = "data", MAX = 12;

function openDb() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB, 2);
        req.onupgradeneeded = () => {
            const db = req.result;
            if (db.objectStoreNames.contains("recents"))
                db.deleteObjectStore("recents");
            for (const name of [STORE, DATA])
                if (!db.objectStoreNames.contains(name))
                    db.createObjectStore(name, { keyPath: "key" });
        };
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    });
}

async function request(mode, fn, store = STORE) {
    const db = await openDb();
    return new Promise((resolve, reject) => {
        const t = db.transaction(store, mode);
        const r = fn(t.objectStore(store));
        t.oncomplete = () => resolve(r?.result);
        t.onerror = () => reject(t.error);
    });
}

// La imagen reducida (para la lista y, más grande, para el fondo), como JPEG.
function preview(doc, side) {
    const source = doc.flattened();
    const scale = Math.min(1, side / Math.max(source.width, source.height));
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(source.width * scale));
    canvas.height = Math.max(1, Math.round(source.height * scale));
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(source, 0, 0, canvas.width, canvas.height);
    return canvas.toDataURL("image/jpeg", 0.82);
}

export const Recents = {
    available() {
        return !!window.KaintAndroid && typeof indexedDB !== "undefined";
    },

    // Guarda (o pone al principio) la imagen con sus capas.
    async remember(doc) {
        if (!this.available() || !doc || doc.width * doc.height > 64e6)
            return;
        // Solo si cambió desde la última vez (si no, cada vez que se sale de la app se volvía a
        // guardar entera).
        const stamp = `${doc.history.index}/${doc.history.steps.length}`;
        if (doc.recentStamp === stamp)
            return;
        doc.recentStamp = stamp;
        try {
            doc.recentKey ||= `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
            const ora = SaveFormats.find((f) => f.ext === "ora");
            const data = await (await encode(doc, ora)).arrayBuffer();
            const entry = { key: doc.recentKey, name: doc.displayName(), thumb: preview(doc, 220), background: preview(doc, 720),
                            time: Date.now() };
            await request("readwrite", (s) => s.put({ key: doc.recentKey, data }), DATA);
            await request("readwrite", (s) => s.put(entry));
            const all = await this.list();
            for (const old of all.slice(MAX)) {
                await request("readwrite", (s) => s.delete(old.key));
                await request("readwrite", (s) => s.delete(old.key), DATA);
            }
        } catch {
            // Sin espacio o sin IndexedDB: la lista se queda como estaba.
        }
    },

    async list() {
        try {
            const all = (await request("readonly", (s) => s.getAll())) || [];
            return all.sort((a, b) => b.time - a.time);
        } catch {
            return [];
        }
    },

    async open(app, entry) {
        const stored = await request("readonly", (s) => s.get(entry.key), DATA);
        if (!stored?.data)
            throw new Error("missing");
        const doc = await openFile(new File([stored.data], `${entry.name || "kaint"}.ora`));
        doc.recentKey = entry.key;
        doc.name = entry.name || doc.name;
        doc.markSaved?.();
        doc.recentStamp = `${doc.history.index}/${doc.history.steps.length}`;
        app.addDocument(doc);
    },
};

// Las opciones de esta pantalla (el engranaje de arriba).
function settingsMenu(anchor, rerender) {
    const r = anchor.getBoundingClientRect();
    const item = (text, key) => ({ text, checked: Settings.get(key) !== false, run: () => { Settings.set(key, Settings.get(key) === false); rerender(); } });
    popupMenu([
        item(tr("Mostrar las imágenes recientes"), "welcomeRecents"),
        item(tr("Fondo con la última imagen editada"), "welcomeBackground"),
        "-",
        item(tr("Mostrar esta pantalla al abrir Kaint"), "androidWelcome"),
    ], r.left, r.bottom + 4);
}

// El fondo: la última imagen reducida a una miniatura diminuta (32 px) que el navegador amplía, ya
// difuminada. Antes se le aplicaba un filtro de desenfoque del tamaño de la pantalla y se animaba:
// era lo que más trababa la entrada en los teléfonos.
function blurredBackground(url) {
    return new Promise((resolve) => {
        const img = new Image();
        img.onload = () => {
            const k = 32 / Math.max(img.width, img.height);
            const canvas = document.createElement("canvas");
            canvas.width = Math.max(1, Math.round(img.width * k));
            canvas.height = Math.max(1, Math.round(img.height * k));
            const g = canvas.getContext("2d");
            g.imageSmoothingQuality = "high";
            g.drawImage(img, 0, 0, canvas.width, canvas.height);
            resolve(canvas.toDataURL("image/png"));
        };
        img.onerror = () => resolve("");
        img.src = url;
    });
}

// La pantalla. Se cierra al elegir algo (lo de encima se va y aparece el editor).
export async function showWelcome(app) {
    const recents = await Recents.list();
    const backdrop = el("div", { className: "welcome-backdrop" });
    const shade = el("div", { className: "welcome-shade" });
    const page = el("div", { className: "welcome-page" });
    const root = el("div", { id: "kaint-welcome", className: "kaint-welcome" }, [backdrop, shade, page]);
    let closing = false;
    const enter = (then) => {
        if (closing)
            return;
        closing = true;
        root.classList.add("leaving");
        setTimeout(() => {
            root.remove();
            then?.();
        }, document.documentElement.classList.contains("no-animations") ? 0 : 420);
    };
    const blankDoc = () => {
        // El lienzo en blanco del arranque, si sigue sin tocar; si no, uno nuevo como el de Kaint.
        const doc = app.doc;
        if (doc && app.docs?.length === 1 && !doc.isModified() && !doc.fileName)
            return;
        app.addDocument(new Document(800, 600));
    };
    const openRecent = async (entry) => {
        try {
            await Recents.open(app, entry);
        } catch {
            return;
        }
        enter();
    };
    // Cada parte entra una tras otra (--d: su turno).
    const step = (node, d) => {
        node.classList.add("wl-in");
        node.style.setProperty("--d", d);
        return node;
    };
    const thumbs = (entries, className) => el("div", { className }, entries.map((entry) => {
        const b = el("button", { className: "welcome-recent", title: entry.name },
                     [el("img", { src: entry.thumb, alt: "", loading: "lazy", decoding: "async" }), el("span", { textContent: entry.name })]);
        b.addEventListener("click", () => openRecent(entry));
        return b;
    }));
    let background = "";
    const render = async () => {
        const showBackground = Settings.get("welcomeBackground") !== false && recents.length > 0;
        if (showBackground && !background)
            background = await blurredBackground(recents[0].background);
        backdrop.style.backgroundImage = showBackground && background ? `url("${background}")` : "";
        root.classList.toggle("plain", !showBackground || !background);
        const gear = el("button", { className: "welcome-gear", title: tr("Configurar esta pantalla") }, [icon("configure", 22)]);
        gear.addEventListener("click", () => settingsMenu(gear, render));
        const big = (iconName, text, run, primary = false) => {
            const b = el("button", { className: `welcome-action${primary ? " primary" : ""}` },
                         [el("span", { className: "welcome-circle" }, [icon(iconName, 32)]), el("span", { textContent: text })]);
            b.addEventListener("click", run);
            return b;
        };
        const openPage = () => {
            // Abrir: las recientes en grande y el botón para importar del dispositivo.
            const back = el("button", { className: "welcome-back", textContent: "‹ " + tr("Volver") });
            back.addEventListener("click", render);
            const importButton = el("button", { className: "welcome-import" }, [icon("document-open", 20), tr("Importar del dispositivo…")]);
            importButton.addEventListener("click", async () => {
                const files = await app.pickFiles();
                if (!files.length)
                    return;
                for (const f of files)
                    await app.openFile(f);
                enter();
            });
            content.replaceChildren(el("div", { className: "welcome-open" }, [
                step(el("div", { className: "welcome-open-head" }, [back, el("b", { textContent: tr("Abrir") })]), 0),
                step(recents.length ? thumbs(recents, "welcome-open-grid")
                                    : el("p", { className: "welcome-empty", textContent: tr("Todavía no hay imágenes recientes.") }), 1),
                step(importButton, 2),
            ]));
        };
        const actions = el("div", { className: "welcome-actions" }, [
            step(big("document-new", tr("Nueva"), () => { blankDoc(); enter(); }, true), 3),
            step(big("document-open", tr("Abrir"), openPage), 4),
            step(big("collab", tr("Colaboración"), () => enter(() => {
                const ids = ["collabHost", "collabJoin", "-", "collabHostLan", "collabJoinLan", "-", "collabSettings"];
                popupMenu(ids.map((id) => (id === "-" ? "-" : app.actions.get(id))).filter(Boolean),
                          Math.round(innerWidth / 2 - 120), Math.round(innerHeight / 3));
            })), 5),
        ]);
        const content = el("div", { className: "welcome-content" }, [
            step(el("img", { className: "welcome-logo", src: "assets/kaint-256.png", alt: "" }), 0),
            step(el("div", { className: "welcome-title", textContent: "Kaint" }), 1),
            step(el("div", { className: "welcome-version", textContent: `v${VERSION}` }), 2),
            actions,
        ]);
        const bottom = Settings.get("welcomeRecents") !== false && recents.length
            ? step(el("div", { className: "welcome-bottom" }, [el("b", { textContent: tr("Recientes") }), thumbs(recents, "welcome-strip")]), 6)
            : null;
        const top = el("div", { className: "welcome-top" }, [el("span", { className: "welcome-brand" }), gear]);
        page.replaceChildren(top, content, bottom || "");
    };
    await render();
    document.body.append(root);
    // Dos fotogramas: el primero con todo invisible y, después, la entrada animada.
    requestAnimationFrame(() => requestAnimationFrame(() => root.classList.add("shown")));
}

// Kaint.APK: guarda la imagen actual en las recientes al salir de la app (pasa a segundo plano) o
// al guardarla.
// Guarda en las recientes las imágenes abiertas que tengan algo (al salir de la app y al volver a la
// bienvenida desde el editor).
export async function rememberOpen(app) {
    if (!Recents.available())
        return;
    const docs = (app.docs || []).filter((doc) => doc.isModified() || doc.recentKey || doc.fileName);
    await Promise.all(docs.map((doc) => Recents.remember(doc).catch(() => {})));
}

export function installRecents(app) {
    if (!Recents.available())
        return;
    const keep = () => rememberOpen(app);
    // (Al volver a la app, el lienzo se vuelve a dibujar por si Android lo dejó congelado: ver Canvas.js.)
    document.addEventListener("visibilitychange", () => document.visibilityState === "hidden" && keep());
    window.addEventListener("pagehide", keep);
}

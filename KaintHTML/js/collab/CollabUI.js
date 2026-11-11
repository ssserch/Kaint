// El modo colaborativo en Kaint para la web (src/app/collab/CollabController.cpp y CollabDialogs.cpp
// de Kaint): hospedar y unirse por P2P (con un código; el navegador hospeda solo) o en la red local
// (a través de tools/collab-server.mjs), la ventana de la sesión, los punteros de los demás y la
// sección Colaboración de Configuración.
import { Settings } from "../Settings.js";
import { checkbox, Dialog, el, select, TaskDialog } from "../ui/Dialogs.js";
import { icon } from "../ui/Icons.js";
import { updateActions } from "../ui/Actions.js";
import { plain, tr } from "../i18n.js";
import { DEFAULT_PORT, DefaultIceServers, DefaultSignalServers, normalizedCode, normalizedSessionCode, prettyCode } from "./Protocol.js";
import { p2pSupported } from "./P2P.js";
import { CollabSession, deviceCode, regenerateDeviceCode } from "./Session.js";

const lines = (value, fallback) => (Array.isArray(value) ? value.map((v) => String(v).trim()).filter(Boolean) : [...fallback]);

export function collabSettings() {
    const s = Settings.values;
    return {
        name: (s.collabName || "").trim().slice(0, 40) || tr("Invitado"),
        color: /^#[0-9a-f]{6}$/i.test(s.collabColor || "") ? s.collabColor : "",
        sessionName: (s.collabSessionName || "").trim().slice(0, 60),
        access: s.collabAccess || "approve",
        password: s.collabPassword || "",
        allowedIds: Array.isArray(s.collabAllowedIds) ? s.collabAllowedIds : [],
        realtime: s.collabRealtime !== false,
        autoAccept: !!s.collabAutoAccept,
        announce: s.collabAnnounce !== false,
        showCursors: s.collabShowCursors !== false,
        mode: s.collabMode === "lan" ? "lan" : "p2p",
        signalServers: lines(s.collabSignalServers, DefaultSignalServers),
        iceServers: lines(s.collabIceServers, DefaultIceServers),
    };
}

// ws://… a partir de lo que se escriba: «192.168.1.20», «192.168.1.20:47913», «http://…».
export function sessionUrl(text) {
    let t = String(text || "").trim();
    if (!t)
        return "";
    t = t.replace(/^(wss?|https?):\/\//i, "").replace(/\/.*$/, "");
    if (!/:\d+$/.test(t) || (t.includes("]") && !/\]:\d+$/.test(t)))
        t += `:${DEFAULT_PORT}`;
    return `ws://${t}/`;
}

const accessText = (a) => ({ free: tr("Libre"), approve: tr("Con permiso del anfitrión"), password: tr("Con contraseña"), ids: tr("Equipos autorizados") })[a] || a;

function swatch(color) {
    return el("span", { className: "collab-swatch", style: { background: color || "#888" } });
}

const RECENT_KEY = "kaint.collab.recent";

function recentAddresses() {
    try {
        const list = JSON.parse(localStorage.getItem(RECENT_KEY) || "[]");
        return Array.isArray(list) ? list.filter((a) => typeof a === "string").slice(0, 8) : [];
    } catch {
        return [];
    }
}

function rememberAddress(address) {
    try {
        localStorage.setItem(RECENT_KEY, JSON.stringify([address, ...recentAddresses().filter((a) => a !== address)].slice(0, 8)));
    } catch {
        // Sin almacenamiento: no se recuerda.
    }
}

// Una dirección o un código: difuminado para que no se vea en capturas ni transmisiones; un clic en
// él (o en el ojo) lo muestra o lo oculta, y al lado un botón lo copia.
export function secretView(text, kind = "address") {
    const code = kind === "code";
    const value = el("span", { className: "collab-secret collab-code hidden", textContent: text || "", tabIndex: 0 });
    const eye = el("button", { className: "tbutton collab-eye" }, [icon("eye-open", 16)]);
    const copy = el("button", { className: "pushbutton" }, [icon("edit-copy", 16), el("span", { textContent: tr("Copiar") })]);
    const sync = () => {
        const hidden = value.classList.contains("hidden");
        const tip = code ? (hidden ? tr("Haz clic para mostrar el código.") : tr("Haz clic para ocultar el código."))
                         : (hidden ? tr("Haz clic para mostrar la dirección.") : tr("Haz clic para ocultar la dirección."));
        value.title = tip;
        eye.title = code ? (hidden ? tr("Mostrar el código") : tr("Ocultar el código")) : (hidden ? tr("Mostrar la dirección") : tr("Ocultar la dirección"));
        eye.replaceChildren(icon(hidden ? "eye-open" : "eye-closed", 16));
    };
    const toggle = () => {
        value.classList.toggle("hidden");
        sync();
    };
    value.addEventListener("click", toggle);
    value.addEventListener("keydown", (e) => (e.key === "Enter" || e.key === " ") && (e.preventDefault(), toggle()));
    eye.addEventListener("click", toggle);
    copy.title = code ? tr("Copiar el código al portapapeles.") : tr("Copiar la dirección al portapapeles.");
    copy.addEventListener("click", () => {
        navigator.clipboard?.writeText(value.textContent);
        copy.querySelector("span").textContent = code ? tr("Copiado") : tr("Copiada");
        setTimeout(() => (copy.querySelector("span").textContent = tr("Copiar")), 1500);
    });
    sync();
    const row = el("span", { className: "collab-secret-row" }, [value, eye, copy]);
    row.setText = (t) => (value.textContent = t);
    return row;
}

const secretHint = () => tr("Las direcciones y los códigos están difuminados para que no se vean en capturas ni transmisiones: haz clic en ellos (o en el ojo) para mostrarlos u ocultarlos.");

export class CollabController extends EventTarget {
    constructor(app) {
        super();
        this.app = app;
        this.session = null;
        this.served = null;
        this.wasInside = false;
        this.requests = new Map();
        this.createStatus();
        app.view.addEventListener("cursor", (e) => this.sendCursor(e.detail));
        app.tools.addEventListener("changed", () => this.sendCursor(app.view.mouse));
        app.addEventListener("document", () => this.refreshPointers());
        window.addEventListener("kaint-settings", (e) => {
            if (String(e.detail?.key || "").startsWith("collab"))
                this.settingsChanged();
        });
        // ¿Esta página la sirve una sesión (Kaint o el servidor de la web)? Entonces se ofrece unirse.
        this.checkServed();
    }

    get active() {
        return !!this.session && this.session.state !== "ended";
    }

    get hosting() {
        return this.active && this.session.isHost;
    }

    isGuestDocument(doc) {
        return !!doc && this.active && !this.session.isHost && this.session.doc === doc;
    }

    isSessionDocument(doc) {
        return !!doc && this.active && this.session.doc === doc;
    }

    // Lo que se muestra en la pestaña de una imagen de la sesión: amarilla, con el icono de la
    // colaboración, la corona (la imagen del anfitrión: guardada o no) y, en los invitados, el equipo
    // (su copia propia: "none", "behind" o "current").
    tabState(doc) {
        const s = this.session;
        if (!doc || !this.active || s.doc !== doc || s.state !== "joined")
            return { collab: false };
        return { collab: true, hostSaved: s.savedByHost(), guest: !s.isHost, copy: s.isHost ? "" : s.localCopy() };
    }

    // Invitado: se exportó una copia de la imagen de la sesión.
    markExported(doc) {
        if (this.isGuestDocument(doc))
            this.session.markExported();
    }

    get applyingRemote() {
        return !!this.session?.applying;
    }

    async checkServed() {
        // Solo si la página la sirve una sesión (Kaint o tools/collab-server.mjs marcan index.html).
        if (!/^https?:$/.test(location.protocol) || !document.querySelector('meta[name="kaint-collab"]'))
            return;
        try {
            const r = await fetch("/kaint-collab.json", { cache: "no-store" });
            if (!r.ok)
                return;
            const info = await r.json();
            if (info["kaint-collab"] !== 1)
                return;
            this.served = info;
            this.emit("changed");
            if (info.hosted !== false && info.session)
                setTimeout(() => this.joinDialog("lan"), 600);
        } catch {
            // No es una sesión: la página se abrió como siempre.
        }
    }

    emit(type, detail = {}) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }

    statusText() {
        if (!this.active)
            return tr("No estás en ninguna sesión.");
        const s = this.session;
        if (s.state === "connecting" && s.isHost)
            return tr("Abriendo la sesión…");
        if (s.state === "connecting")
            return s.connection === "p2p" ? s.progress || tr("Buscando la sesión…") : tr("Conectando con %1…", s.url);
        if (s.state === "waiting")
            return tr("Esperando a que el anfitrión te deje entrar…");
        const n = s.peers.size;
        const people = n === 1 ? tr("1 participante") : tr("%1 participantes", n);
        if (s.isHost && s.connection === "p2p") {
            // Sin servidores de señalización nadie más puede encontrar la sesión.
            if (s.p2p && !s.p2p.signalingReady)
                return tr("Hospedando «%1» por P2P · %2 · sin conexión con los servidores de señalización (se sigue intentando)", s.sessionName, people);
            return tr("Hospedando «%1» por P2P · %2", s.sessionName, people);
        }
        if (s.isHost)
            return tr("Hospedando «%1» en la red local · %2", s.sessionName, people);
        return tr("En «%1» de %2 · %3 · %4", s.sessionName, s.peer(0)?.name || "?", s.isLive ? tr("en tiempo real") : tr("con confirmaciones"), people);
    }

    // --- Barra de estado -------------------------------------------------------------------------

    createStatus() {
        this.statusButton = el("button", { className: "tbutton collab-status", title: tr("Sesión colaborativa: participantes y sugerencias") },
                               [icon("collab", 16), el("span")]);
        this.statusButton.addEventListener("click", () => this.sessionDialog());
        this.proposeButton = el("button", { className: "tbutton collab-propose", title: tr("Envía al anfitrión lo que has cambiado para que lo acepte.") },
                                [icon("collab", 16), el("span", { textContent: tr("Sugerir cambios") })]);
        this.proposeButton.addEventListener("click", () => this.session?.propose());
        this.discardButton = el("button", { className: "tbutton", title: tr("Descartar el borrador (vuelve a la imagen de la sesión)") },
                                [icon("edit-undo", 16)]);
        this.discardButton.addEventListener("click", () => this.discardDraft());
        this.statusElement = el("span", { className: "collab-bar" }, [this.statusButton, this.proposeButton, this.discardButton]);
        this.statusElement.hidden = true;
        const bar = document.getElementById("status-bar");
        const anchor = bar?.querySelector(".effect-progress");
        if (anchor)
            anchor.after(this.statusElement);
        else
            bar?.appendChild(this.statusElement);
    }

    updateStatus() {
        const s = this.session;
        this.statusElement.hidden = !this.active;
        if (!this.active)
            return;
        let text = tr("Conectando…");
        if (s.state === "joined") {
            const n = s.peers.size, p = s.proposals.length;
            text = n === 1 ? tr("1 participante") : tr("%1 participantes", n);
            if (s.isHost && p)
                text += ` · ${p === 1 ? tr("1 sugerencia") : tr("%1 sugerencias", p)}`;
        }
        this.statusButton.querySelector("span").textContent = text;
        const confirmGuest = !s.isHost && s.state === "joined" && !s.isLive;
        this.proposeButton.hidden = this.discardButton.hidden = !confirmGuest;
        this.proposeButton.disabled = !s.hasDraft || !!s.pendingPid;
        this.proposeButton.querySelector("span").textContent = s.pendingPid ? tr("Esperando al anfitrión…") : tr("Sugerir cambios");
        this.discardButton.disabled = !s.hasDraft;
    }

    // --- Sesión ----------------------------------------------------------------------------------

    attach(session) {
        if (this.session && this.session !== session && this.session.state !== "ended")
            this.session.leave();
        this.session = session;
        session.busyCheck = () => {
            const doc = session.doc, app = this.app;
            if (!doc || app.doc !== doc)
                return { busy: false, layer: null };
            if (doc.preview)
                return { busy: true, layer: doc.preview.layer || doc.activeLayer };
            const tool = app.tools.current;
            let layer = null;
            if (tool?.steps?.steps > 0)
                layer = tool.steps.last?.layer || doc.activeLayer;
            if (tool?.isBusy())
                layer = layer || doc.activeLayer;
            return { busy: !!layer, layer };
        };
        const refresh = () => {
            this.updateStatus();
            this.refreshPointers();
            this.emit("changed");
            updateActions();
        };
        for (const type of ["state", "peers", "proposals", "draft"])
            session.addEventListener(type, refresh);
        session.addEventListener("cursors", () => this.refreshPointers());
        // La pestaña de la imagen (corona, copia propia) y el historial del anfitrión.
        const tab = () => {
            if (session.doc && this.app.docs.includes(session.doc))
                this.app.updateTab(session.doc, true);
            this.emit("history");
        };
        for (const type of ["indicators", "hosthistory", "state", "peers"])
            session.addEventListener(type, tab);
        session.addEventListener("revert", (e) => {
            const doc = session.doc, app = this.app;
            if (!doc || !app.docs.includes(doc))
                return;
            if (app.doc === doc)
                app.prepareHistoryJump();
            doc.goTo(Math.min(e.detail.index, doc.history.steps.length - 1));
        });
        session.addEventListener("notice", (e) => this.app.showMessage(e.detail.text));
        session.addEventListener("request", (e) => this.askAdmission(e.detail.peer));
        session.addEventListener("document", (e) => {
            this.app.addDocument(e.detail.doc);
            this.app.showMessage(tr("Te uniste a «%1».", session.sessionName));
        });
        session.addEventListener("ended", (e) => {
            for (const toast of this.requests.values())
                toast.remove();
            this.requests.clear();
            const reason = e.detail.reason;
            this.app.showMessage(reason || tr("La sesión colaborativa terminó."));
            tab();
            if (reason && session.doc && !session.isHost && session.state === "ended" && this.app.docs.includes(session.doc))
                this.notify(`${reason}\n\n${tr("La imagen sigue abierta: puedes guardarla como una copia.")}`);
            refresh();
        });
        refresh();
    }

    host(doc, url, hostKey = "") {
        if (!doc)
            return null;
        const session = CollabSession.host(this.app, doc, url, collabSettings(), hostKey);
        this.attach(session);
        return session;
    }

    join(url, password) {
        const session = CollabSession.join(this.app, url, collabSettings(), password);
        this.attach(session);
        return session;
    }

    hostP2P(doc) {
        if (!doc)
            return null;
        const session = CollabSession.hostP2P(this.app, doc, collabSettings());
        this.attach(session);
        return session;
    }

    joinP2P(code, password) {
        const session = CollabSession.joinP2P(this.app, code, collabSettings(), password);
        this.attach(session);
        return session;
    }

    // Hospedar la imagen actual por P2P (sin más: el navegador hospeda) o en la red local.
    async hostCurrent(mode = collabSettings().mode) {
        const app = this.app;
        if (!app.doc)
            return;
        if (mode === "lan")
            return this.hostDialog();
        if (this.active && !(await this.leave(true)))
            return;
        if (!p2pSupported())
            return this.notify(tr("Este navegador no admite conexiones WebRTC: usa la red local (LAN)."));
        const session = this.hostP2P(app.doc);
        const opened = () => {
            if (session.state !== "joined")
                return;
            session.removeEventListener("state", opened);
            app.showMessage(tr("Sesión colaborativa abierta: %1", this.statusText()));
        };
        session.addEventListener("state", opened);
        this.sessionDialog();
        return session;
    }

    async leave(ask = true) {
        if (!this.active)
            return true;
        if (ask) {
            const host = this.session.isHost;
            const dlg = new TaskDialog(host ? tr("Terminar la sesión") : tr("Salir de la sesión"), "collab",
                                       host ? tr("Los invitados dejarán de estar conectados a tu imagen.")
                                           : tr("Dejarás de estar conectado. Puedes exportar una copia de la imagen antes de salir; lo que no hayas sugerido se pierde."));
            const yes = dlg.addChoice("application-exit", host ? tr("Terminar") : tr("Salir"), host ? tr("Cerrar la sesión para todos.") : tr("Salir de la sesión."));
            dlg.addChoice("dialog-cancel", tr("Cancelar"), tr("Seguir en la sesión."), true);
            if ((await dlg.exec()) !== yes)
                return false;
        }
        this.session?.leave();
        return true;
    }

    async discardDraft() {
        const dlg = new TaskDialog(tr("Descartar el borrador"), "edit-delete", tr("Se pierden los cambios que no has sugerido y su historial."));
        const yes = dlg.addChoice("edit-delete", tr("Descartar"), tr("Volver a la imagen de la sesión."));
        dlg.addChoice("dialog-cancel", tr("Cancelar"), tr("Seguir con el borrador."), true);
        if ((await dlg.exec()) === yes)
            this.session?.discardDraft();
    }

    settingsChanged() {
        if (!this.active)
            return;
        const s = collabSettings();
        if (this.session.isHost)
            this.session.updateSettings(s);
        else if (s.name !== this.session.settings.name)
            this.session.rename(s.name);
        this.refreshPointers();
    }

    // Aviso que no bloquea (encima de la barra de estado).
    notify(text, buttons = []) {
        const toast = el("div", { className: "collab-toast" }, [el("div", { textContent: text, style: { whiteSpace: "pre-wrap" } })]);
        const row = el("div", { className: "row-buttons" });
        for (const [label, run] of buttons.length ? buttons : [[tr("Aceptar"), () => {}]]) {
            const b = el("button", { className: "pushbutton", textContent: plain(label) });
            b.addEventListener("click", () => {
                toast.remove();
                run();
            });
            row.appendChild(b);
        }
        toast.appendChild(row);
        document.body.appendChild(toast);
        return toast;
    }

    askAdmission(peer) {
        const toast = this.notify(tr("%1 quiere unirse a tu sesión (%2).", peer.name, peer.app || peer.platform), [
            [tr("Dejar entrar"), () => this.session?.admit(peer.id, true)],
            [tr("Rechazar"), () => this.session?.admit(peer.id, false)],
        ]);
        this.requests.set(peer.id, toast);
        const watch = () => {
            if (!this.session?.waiting.has(peer.id)) {
                toast.remove();
                this.requests.delete(peer.id);
                this.session?.removeEventListener("peers", watch);
            }
        };
        this.session.addEventListener("peers", watch);
        this.app.showMessage(tr("%1 quiere unirse a tu sesión.", peer.name));
    }

    // --- Punteros --------------------------------------------------------------------------------

    sendCursor(e) {
        const s = this.session;
        if (!this.active || s.state !== "joined")
            return;
        const inside = !!e && this.app.doc === s.doc;
        if (!inside && !this.wasInside)
            return;
        this.wasInside = inside;
        const p = e || this.app.view.lastMouse || { x: 0, y: 0 };
        s.setCursor(p.x, p.y, inside, this.app.tools.current?.id || "");
    }

    refreshPointers() {
        const view = this.app.view, s = this.session, list = [];
        if (this.active && collabSettings().showCursors && this.app.doc && this.app.doc === s.doc) {
            for (const [id, c] of s.cursors) {
                const p = s.peer(id);
                if (!c.inside || id === s.myId || !p)
                    continue;
                list.push({ x: c.x, y: c.y, color: p.color, name: p.name, tool: c.tool });
            }
        }
        view.remotePointers = list;
        view.update();
    }

    // --- Diálogos --------------------------------------------------------------------------------

    async joinDialog(mode = collabSettings().mode) {
        // Una sola a la vez (la que se abre sola al cargar la página servida por una sesión y la del menú).
        if (this.joining)
            return;
        this.joining = true;
        try {
            if (mode === "p2p")
                await this.showP2PJoinDialog();
            else
                await this.showJoinDialog();
        } finally {
            this.joining = false;
        }
    }

    // Unirse por P2P: con el código de la sesión.
    async showP2PJoinDialog() {
        if (this.active && !(await this.leave(true)))
            return;
        const settings = collabSettings();
        const dlg = new Dialog(tr("Unirse a una sesión P2P"), { iconName: "collab" });
        const code = el("input", { type: "text", className: "collab-code", placeholder: tr("Ej.: ABCD-EFGH-JKLM-NPQR"), maxLength: 32, style: { width: "240px" } });
        const paste = el("button", { className: "tbutton", title: tr("Pegar") }, [icon("edit-paste", 16)]);
        paste.addEventListener("click", async () => {
            try {
                code.value = (await navigator.clipboard.readText()).trim();
            } catch {
                code.focus();
            }
        });
        const password = el("input", { type: "password", placeholder: tr("Solo si la sesión pide contraseña"), style: { width: "280px" } });
        const name = el("input", { type: "text", value: Settings.get("collabName") || "", placeholder: settings.name, maxLength: 40, style: { width: "280px" } });
        const status = el("p", { className: "collab-status-text" });
        dlg.body.append(el("div", { className: "collab-form" }, [
            el("p", { textContent: tr("Escribe el código que te dio el anfitrión. Los dos equipos se encuentran por Internet y la imagen va directa de uno a otro.") }),
            el("label", {}, [tr("Código de la sesión:"), el("span", { className: "row-buttons" }, [code, paste])]),
            el("label", {}, [tr("Contraseña:"), password]),
            el("label", {}, [tr("Tu nombre:"), name]),
            el("label", {}, [tr("ID de este equipo:"), this.deviceIdRow()]),
            el("small", { className: "collab-note", textContent: tr("Si la sesión solo admite equipos autorizados, dale este ID al anfitrión.") }),
            status,
        ]));
        const joinButton = dlg.addButton(tr("Unirse"), "join", { isDefault: true });
        dlg.addButton(tr("Cancelar"), null);
        if (!p2pSupported()) {
            status.textContent = tr("Este navegador no admite conexiones WebRTC: usa la red local (LAN).");
            status.classList.add("error");
            joinButton.disabled = true;
        }
        let session = null;
        dlg.validate = () => {
            const normalized = normalizedSessionCode(code.value);
            if (!normalized) {
                status.textContent = tr("El código tiene 16 letras y números, como ABCD-EFGH-JKLM-NPQR.");
                status.classList.add("error");
                return false;
            }
            if ((name.value.trim() || "") !== (Settings.get("collabName") || ""))
                Settings.set("collabName", name.value.trim());
            status.classList.remove("error");
            status.textContent = tr("Buscando la sesión…");
            joinButton.disabled = true;
            session = this.joinP2P(normalized, password.value);
            session.addEventListener("state", () => {
                if (session.state === "waiting")
                    status.textContent = tr("Esperando a que el anfitrión te deje entrar…");
                else if (session.state === "connecting" && session.progress)
                    status.textContent = session.progress;
            });
            session.addEventListener("document", () => dlg.close("joined"));
            session.addEventListener("ended", (e) => {
                if (!dlg.resolve)
                    return;
                status.textContent = e.detail.reason || tr("No se pudo entrar.");
                status.classList.add("error");
                joinButton.disabled = false;
            });
            return false;
        };
        setTimeout(() => code.focus(), 50);
        const result = await dlg.exec();
        if (result !== "joined" && session && session.state !== "joined")
            session.leave();
    }

    // Sesiones de la red local que se pueden ver desde el navegador: las que conoce el servidor que
    // sirvió la página (Kaint o tools/collab-server.mjs) o uno en este mismo equipo, y las recientes.
    async nearbySessions() {
        const found = new Map();
        const sources = [];
        if (this.served)
            sources.push("/kaint-collab-sessions.json");
        if (location.protocol === "http:" || /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname))
            sources.push(`http://localhost:${DEFAULT_PORT}/kaint-collab-sessions.json`);
        await Promise.all(sources.map(async (url) => {
            try {
                const r = await fetch(url, { cache: "no-store", signal: AbortSignal.timeout?.(1500) });
                for (const s of r.ok ? await r.json() : [])
                    if (s?.address && s.port)
                        found.set(`${s.address}:${s.port}`, s);
            } catch {
                // Sin servidor ahí.
            }
        }));
        return [...found.values()];
    }

    // Unirse en la red local: la lista de sesiones cercanas o una dirección escrita a mano.

    async showJoinDialog() {
        if (this.active && !(await this.leave(true)))
            return;
        const settings = collabSettings();
        const dlg = new Dialog(tr("Unirse a una sesión de la red local"), { iconName: "collab" });
        const served = this.served;
        const address = el("input", { type: "text", placeholder: tr("Ej.: 192.168.1.20 o 192.168.1.20:47913"), style: { width: "280px" } });
        if (served?.session)
            address.value = location.host;
        const password = el("input", { type: "password", placeholder: tr("Solo si la sesión pide contraseña"), style: { width: "280px" } });
        const name = el("input", { type: "text", value: Settings.get("collabName") || "", placeholder: settings.name, maxLength: 40, style: { width: "280px" } });
        const status = el("p", { className: "collab-status-text" });
        const intro = served?.session
            ? tr("Esta página la sirve la sesión «%1» de %2 (%3).", served.session, served.host, accessText(served.auth))
            : tr("Elige una sesión de la lista o escribe la dirección del equipo que la hospeda (lo dice su ventana de la sesión).");
        const httpsNote = location.protocol === "https:"
            ? el("small", { className: "collab-note", textContent: tr("Esta página se abrió con https: el navegador no deja conectarse a una sesión de la red local. Abre la dirección http:// que muestra el anfitrión, o usa P2P.") })
            : null;
        // Sesiones en la red local (como la lista de Kaint).
        const table = el("table", { className: "collab-found" });
        const head = el("tr", {}, [tr("Sesión"), tr("Anfitrión"), tr("Dirección"), tr("Acceso"), tr("Personas")].map((t) => el("th", { textContent: t })));
        const rows = el("tbody");
        table.append(el("thead", {}, [head]), rows);
        const foundNote = el("small", { className: "collab-note" });
        let selected = "";
        const pick = (addressText, auth) => {
            selected = addressText;
            address.value = addressText;
            for (const r of rows.children)
                r.classList.toggle("selected", r.dataset.address === addressText);
            if (auth === "password")
                password.focus();
        };
        const fill = (sessions) => {
            const recent = recentAddresses().filter((a) => !sessions.some((s) => `${s.address}:${s.port}` === a));
            const items = [
                ...sessions.map((s) => ({ address: `${s.address}:${s.port}`, cells: [s.name, s.host, `${s.address}:${s.port}`, accessText(s.auth), String(s.peers ?? "")], auth: s.auth })),
                ...recent.map((a) => ({ address: a, cells: [tr("Usada antes"), "", a, "", ""], auth: "" })),
            ];
            rows.replaceChildren(...items.map((item) => {
                const row = el("tr", { dataset: { address: item.address }, className: item.address === selected ? "selected" : "" },
                               item.cells.map((c, i) => el("td", {}, i === 0 ? [icon("collab", 16), " ", c] : [c])));
                row.addEventListener("click", () => pick(item.address, item.auth));
                row.addEventListener("dblclick", () => {
                    pick(item.address, item.auth);
                    joinButton.click();
                });
                return row;
            }));
            if (!items.length)
                rows.appendChild(el("tr", { className: "empty" }, [el("td", { colSpan: 5, textContent: tr("Buscando sesiones…") })]));
            foundNote.textContent = this.served
                ? tr("La lista la da el equipo que sirvió esta página (las sesiones que se anuncian en su red).")
                : tr("Un navegador no puede buscar en la red por sí solo: la lista aparece al abrir Kaint para la web desde la dirección de una sesión o con tools/collab-server.mjs en este equipo. Abajo, las que ya usaste.");
        };
        fill([]);
        let alive = true;
        const poll = async () => {
            if (!alive)
                return;
            fill(await this.nearbySessions());
            setTimeout(poll, 2000);
        };
        poll();
        dlg.body.append(el("div", { className: "collab-form collab-join" }, [
            el("p", { textContent: intro }),
            el("span", { textContent: tr("Sesiones en la red local:") }),
            el("div", { className: "collab-found-box" }, [table]),
            foundNote,
            el("label", {}, [tr("Dirección IP:"), address]),
            el("label", {}, [tr("Contraseña:"), password]),
            el("label", {}, [tr("Tu nombre:"), name]),
            el("label", {}, [tr("ID de este equipo:"), this.deviceIdRow()]),
            el("small", { className: "collab-note", textContent: tr("Si la sesión solo admite equipos autorizados, dale este ID al anfitrión.") }),
            httpsNote, status,
        ]));
        const joinButton = dlg.addButton(tr("Unirse"), "join", { isDefault: true });
        dlg.addButton(tr("Cancelar"), null);
        let session = null;
        dlg.validate = () => {
            const url = sessionUrl(address.value);
            if (!url) {
                status.textContent = tr("Escribe la dirección del anfitrión.");
                status.classList.add("error");
                return false;
            }
            if ((name.value.trim() || "") !== (Settings.get("collabName") || ""))
                Settings.set("collabName", name.value.trim());
            status.classList.remove("error");
            status.textContent = tr("Conectando con %1…", url);
            joinButton.disabled = true;
            rememberAddress(url.replace(/^ws:\/\//, "").replace(/\/$/, ""));
            session = this.join(url, password.value);
            session.addEventListener("state", () => {
                if (session.state === "waiting")
                    status.textContent = tr("Esperando a que el anfitrión te deje entrar…");
            });
            session.addEventListener("document", () => dlg.close("joined"));
            session.addEventListener("ended", (e) => {
                if (!dlg.resolve)
                    return;
                status.textContent = e.detail.reason || tr("No se pudo entrar.");
                status.classList.add("error");
                joinButton.disabled = false;
            });
            return false;
        };
        const result = await dlg.exec();
        alive = false;
        if (result !== "joined" && session && session.state !== "joined")
            session.leave();
    }

    deviceIdRow() {
        const code = el("b", { className: "collab-code", textContent: deviceCode() });
        const copy = el("button", { className: "tbutton", title: tr("Copiar") }, [icon("edit-copy", 16)]);
        copy.addEventListener("click", () => navigator.clipboard?.writeText(code.textContent));
        return el("span", { className: "row-buttons" }, [code, copy]);
    }

    // Hospedar desde el navegador: hace falta el servidor de Kaint para la web.
    async hostDialog() {
        const app = this.app;
        if (!app.doc)
            return;
        if (this.active && !(await this.leave(true)))
            return;
        const dlg = new Dialog(tr("Hospedar en la red local"), { iconName: "collab" });
        const relay = this.served?.server === "relay";
        const localhost = /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname);
        const address = el("input", { type: "text", value: relay ? location.host : Settings.get("collabServer") || `localhost:${DEFAULT_PORT}`, style: { width: "260px" } });
        const key = el("input", { type: "text", placeholder: tr("Solo desde otro equipo"), style: { width: "260px" } });
        const status = el("p", { className: "collab-status-text" });
        const settings = collabSettings();
        dlg.body.append(el("div", { className: "collab-form" }, [
            el("p", { textContent: relay
                ? tr("Este navegador hospedará la imagen actual a través del servidor de Kaint para la web que abrió esta página. Los demás se unen abriendo esta misma dirección o desde Kaint.")
                : tr("Un navegador no puede abrir un servidor en la red local. Para hospedar así, ejecuta en este equipo el servidor de Kaint para la web (con Node.js): «node tools/collab-server.mjs» en la carpeta de Kaint para la web, y abre la dirección que muestra (http://localhost:47913/). Por P2P no hace falta: el navegador hospeda solo.") }),
            el("label", {}, [tr("Servidor:"), address]),
            relay && localhost ? null : el("label", {}, [tr("Clave para hospedar:"), key]),
            relay && localhost ? null : el("small", { className: "collab-note", textContent: tr("El servidor la muestra al arrancar; no hace falta en el mismo equipo.") }),
            el("p", { textContent: tr("Acceso: %1 · Nombre: %2 (se cambia en Configuración > Colaboración)", accessText(settings.access), settings.name) }),
            status,
        ]));
        const go = dlg.addButton(tr("Hospedar"), "host", { isDefault: true });
        dlg.addButton(tr("Cancelar"), null);
        let session = null;
        dlg.validate = () => {
            const url = sessionUrl(address.value);
            if (!relay)
                Settings.set("collabServer", address.value.trim());
            status.classList.remove("error");
            status.textContent = tr("Conectando con %1…", url);
            go.disabled = true;
            session = this.host(app.doc, url, key.value.trim());
            session.addEventListener("state", () => {
                if (session.state === "joined")
                    dlg.close("hosting");
            });
            session.addEventListener("ended", (e) => {
                if (!dlg.resolve)
                    return;
                status.textContent = e.detail.reason || tr("No se pudo conectar.");
                status.classList.add("error");
                go.disabled = false;
            });
            return false;
        };
        const result = await dlg.exec();
        if (result === "hosting") {
            app.showMessage(tr("Sesión colaborativa abierta: %1", this.statusText()));
            this.sessionDialog();
        } else if (session && session.state !== "joined") {
            session.leave();
        }
    }

    async sessionDialog() {
        const s = this.session;
        const dlg = new Dialog(tr("Sesión colaborativa"), { iconName: "collab" });
        const body = el("div", { className: "collab-session" });
        dlg.body.appendChild(body);
        // Se conservan entre repintados (así no se vuelven a difuminar solos).
        const codeView = secretView("", "code"), addressView = secretView(""), webView = secretView("");
        const render = async () => {
            const items = [el("p", { className: "collab-title", textContent: this.statusText() })];
            if (this.active && s.isHost && s.state === "joined" && s.connection === "p2p") {
                items.push(el("p", { textContent: tr("Para unirse: Archivo > Colaboración > P2P > Unirse con un código, y este código (en Kaint o en Kaint para la web, desde cualquier lugar).") }),
                           el("div", { className: "row-buttons" }, [el("span", { textContent: tr("Código de la sesión:") }), codeView]),
                           el("small", { className: "collab-note", textContent: secretHint() }));
                codeView.setText(s.code);
            } else if (this.active && s.isHost && s.state === "joined") {
                const where = this.served?.server === "relay" ? location.host : sessionUrl(s.url).replace(/^ws:\/\//, "").replace(/\/$/, "");
                const lan = (this.served?.addresses || [])[0];
                const shown = lan ? `${lan}:${(where.split(":")[1] || DEFAULT_PORT)}` : where;
                addressView.setText(shown);
                webView.setText(`http://${shown}/`);
                items.push(el("p", { textContent: tr("Para unirse: desde Kaint, Archivo > Colaboración > Red local > Unirse a una sesión, o abriendo en un navegador de la red la dirección para el navegador.") }),
                           el("div", { className: "row-buttons" }, [el("span", { textContent: tr("Dirección:") }), addressView]),
                           el("div", { className: "row-buttons" }, [el("span", { textContent: tr("Desde un navegador:") }), webView]),
                           el("small", { className: "collab-note", textContent: secretHint() }));
                if (!lan)
                    items.push(el("small", { className: "collab-note", textContent: tr("Si dice «localhost», los demás usan la IP de este equipo en su lugar.") }));
            } else if (this.active && !s.isHost) {
                items.push(el("small", { className: "collab-note", textContent: tr("Lo que exportes es una copia: la imagen la guarda el anfitrión.") }));
            }
            if (this.active) {
                // Participantes y solicitudes.
                const list = el("div", { className: "list collab-peers" });
                for (const p of s.peerList()) {
                    const row = el("div", { className: "row" }, [swatch(p.color),
                        el("span", { textContent: p.name + (p.id === s.myId ? ` ${tr("(tú)")}` : "") + (p.host ? ` · ${tr("anfitrión")}` : "") }),
                        el("small", { textContent: (p.platform === "web" ? tr("Kaint para la web") : p.app || "Kaint")
                                      + (p.host ? "" : ` · ${p.mode === "live" ? tr("En tiempo real") : tr("Con confirmaciones")}`) })]);
                    if (s.isHost && !p.host) {
                        const kick = el("button", { className: "pushbutton", textContent: tr("Expulsar") });
                        kick.addEventListener("click", () => s.kick(p.id));
                        row.appendChild(kick);
                    }
                    list.appendChild(row);
                }
                for (const p of s.waiting.values()) {
                    const yes = el("button", { className: "pushbutton", textContent: tr("Dejar entrar") });
                    const no = el("button", { className: "pushbutton", textContent: tr("Rechazar") });
                    yes.addEventListener("click", () => s.admit(p.id, true));
                    no.addEventListener("click", () => s.admit(p.id, false));
                    list.appendChild(el("div", { className: "row" }, [icon("dialog-question", 16), el("span", { textContent: p.name }),
                                                                      el("small", { textContent: tr("Espera tu permiso") }), yes, no]));
                }
                items.push(el("h4", { className: "section", textContent: tr("Participantes") }), list);
                // Sugerencias.
                if (s.isHost) {
                    const props = el("div", { className: "list collab-proposals" });
                    for (const p of s.proposals) {
                        const thumb = el("span", { className: "collab-thumb" });
                        s.preview(p, 96).then(({ canvas, area }) => {
                            thumb.appendChild(canvas);
                            info.textContent += ` · ${tr("zona %1×%2 en (%3, %4)", area.w, area.h, area.x, area.y)}`;
                        }).catch(() => {});
                        const info = el("small", { textContent: p.time.toLocaleTimeString() });
                        const accept = el("button", { className: "pushbutton", textContent: tr("Aceptar") });
                        const reject = el("button", { className: "pushbutton", textContent: tr("Rechazar") });
                        accept.addEventListener("click", () => s.acceptProposal(p.from));
                        reject.addEventListener("click", () => s.rejectProposal(p.from));
                        props.appendChild(el("div", { className: "row" }, [thumb, el("span", {}, [el("b", { textContent: p.name }), ` — ${p.label || tr("Cambios")}`, el("br"), info]),
                                                                           accept, reject]));
                    }
                    if (!s.proposals.length)
                        props.appendChild(el("div", { className: "row", textContent: tr("Sin sugerencias por ahora.") }));
                    const auto = checkbox(tr("Aceptar automáticamente"), collabSettings().autoAccept);
                    auto.input.addEventListener("change", () => Settings.set("collabAutoAccept", auto.input.checked));
                    const all = el("button", { className: "pushbutton", textContent: tr("Aceptar todos"), disabled: !s.proposals.length });
                    all.addEventListener("click", () => s.acceptAll());
                    items.push(el("h4", { className: "section", textContent: tr("Cambios sugeridos") }), props, el("div", { className: "row-buttons" }, [all, auto]));
                } else if (s.state === "joined" && !s.isLive) {
                    const text = s.pendingPid ? tr("Enviados; esperando la respuesta del anfitrión.")
                        : s.hasDraft ? tr("Tienes cambios sin sugerir. Su historial es solo tuyo hasta que se aceptan.")
                        : tr("Sin cambios. Edita la imagen y sugiere los cambios al anfitrión.");
                    const propose = el("button", { className: "pushbutton", textContent: tr("Sugerir cambios"), disabled: !s.hasDraft || !!s.pendingPid });
                    propose.addEventListener("click", () => s.propose());
                    const discard = el("button", { className: "pushbutton", textContent: tr("Descartar"), disabled: !s.hasDraft });
                    discard.addEventListener("click", () => this.discardDraft());
                    items.push(el("h4", { className: "section", textContent: tr("Tus cambios") }), el("p", { textContent: text }),
                               el("div", { className: "row-buttons" }, [propose, discard]));
                }
                const leave = el("button", { className: "pushbutton", textContent: s.isHost ? tr("Terminar la sesión") : tr("Salir de la sesión") });
                leave.addEventListener("click", async () => {
                    if (await this.leave(true))
                        dlg.close(null);
                });
                items.push(el("div", { className: "row-buttons" }, [leave]));
            }
            body.replaceChildren(...items);
        };
        const refresh = () => render();
        this.addEventListener("changed", refresh);
        await render();
        dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
        await dlg.exec();
        this.removeEventListener("changed", refresh);
    }

    // El historial del anfitrión (Historial > el botón de la colaboración): sus pasos, el actual
    // resaltado, y volver a uno para todos (el anfitrión y los invitados con su permiso). El anfitrión
    // elige aquí quién más puede.
    async historyDialog() {
        if (!this.active)
            return;
        const dlg = new Dialog(tr("Historial de la sesión"), { iconName: "collab" });
        const body = el("div", { className: "collab-history" });
        dlg.body.appendChild(body);
        let selected = -1, followCurrent = true;
        const info = el("p");
        const list = el("div", { className: "list collab-history-steps", tabIndex: 0 });
        const revert = el("button", { className: "pushbutton" }, [icon("edit-undo", 16), el("span", { textContent: tr("Volver a esta versión") })]);
        revert.title = tr("La imagen vuelve a como estaba en ese paso, para todos. Se puede rehacer desde el mismo historial.");
        const note = el("small", { className: "collab-note" });
        const permissions = el("div", { className: "collab-history-permissions" });
        body.append(info, list, el("div", { className: "row-buttons" }, [revert, note]), permissions);
        const go = () => {
            const s = this.session;
            if (s && s.canRevert && selected >= 0 && selected !== s.historyIndex())
                s.revertTo(selected);
        };
        revert.addEventListener("click", go);
        const render = () => {
            const s = this.session;
            if (!this.active || s.state !== "joined") {
                info.textContent = tr("No estás en ninguna sesión.");
                list.replaceChildren();
                revert.disabled = true;
                permissions.replaceChildren();
                return;
            }
            info.textContent = s.isHost ? tr("Tu historial, el de la imagen de la sesión. Volver a un paso cambia la imagen para todos.")
                                        : tr("El historial de %1, el anfitrión. Volver a un paso cambia la imagen para todos.", s.peer(0)?.name || "?");
            const names = [tr("Imagen original"), ...s.historySteps()];
            const current = s.historyIndex();
            if (followCurrent || selected >= names.length)
                selected = current;
            followCurrent = selected === current;
            list.replaceChildren(...names.map((name, i) => {
                const row = el("div", { className: `row${i === current ? " current" : ""}${i > current ? " undone" : ""}${i === selected ? " selected" : ""}`,
                                        textContent: name });
                row.addEventListener("click", () => {
                    selected = i;
                    followCurrent = i === s.historyIndex();
                    render();
                });
                row.addEventListener("dblclick", go);
                return row;
            }));
            list.querySelector(".selected")?.scrollIntoView({ block: "nearest" });
            revert.disabled = !s.canRevert || selected === current;
            note.textContent = s.canRevert ? "" : tr("Solo el anfitrión y quienes tengan su permiso pueden volver a una versión anterior.");
            permissions.replaceChildren();
            if (s.isHost) {
                const guests = s.peerList().filter((p) => !p.host);
                const box = el("div", { className: "list collab-permission-list" });
                for (const p of guests) {
                    // El nombre tal cual (checkbox() quitaría los «&»).
                    const input = el("input", { type: "checkbox", checked: !!p.history });
                    input.addEventListener("change", () => s.setHistoryPermission(p.id, input.checked));
                    box.appendChild(el("div", { className: "row" }, [el("label", {}, [input, el("span", { textContent: p.name })]), swatch(p.color)]));
                }
                if (!guests.length)
                    box.appendChild(el("div", { className: "row", textContent: tr("Todavía no hay invitados.") }));
                permissions.append(el("h4", { className: "section", textContent: tr("Pueden volver a versiones anteriores") }), box);
            }
        };
        list.addEventListener("keydown", (e) => {
            const n = list.children.length;
            if (e.key === "ArrowUp" || e.key === "ArrowDown") {
                e.preventDefault();
                selected = Math.max(0, Math.min(n - 1, selected + (e.key === "ArrowUp" ? -1 : 1)));
                followCurrent = selected === this.session?.historyIndex();
                render();
            } else if (e.key === "Enter") {
                go();
            }
        });
        const refresh = () => render();
        this.addEventListener("history", refresh);
        this.addEventListener("changed", refresh);
        render();
        dlg.addButton(tr("Cerrar"), "ok", { isDefault: true });
        await dlg.exec();
        this.removeEventListener("history", refresh);
        this.removeEventListener("changed", refresh);
    }
}

// --- Configuración > Colaboración ----------------------------------------------------------------
// Dos vistas con las mismas opciones: la simplificada (por secciones, con las explicaciones a la
// vista y lo que menos se cambia en «Más opciones») y la compacta (todo junto; las explicaciones,
// en las descripciones emergentes).

export function collabSettingsPage(app, refresh) {
    const collab = app?.collab;
    const set = (key, value) => Settings.set(key, value);
    const s = collabSettings();
    const compact = !!Settings.get("collabCompactView");
    const page = el("div", { className: `collab-page ${compact ? "compact" : "simple"}` });

    // Vista.
    const viewButton = (text, tip, value) => {
        const b = el("button", { className: `pushbutton${compact === value ? " checked" : ""}`, textContent: text, title: tip });
        b.setAttribute("aria-pressed", String(compact === value));
        b.addEventListener("click", () => {
            if (compact === value)
                return;
            set("collabCompactView", value);
            refresh();
        });
        return b;
    };
    page.append(el("div", { className: "row-buttons collab-view" }, [
        el("span", { textContent: tr("Vista:") }),
        viewButton(tr("Simplificada"), tr("Por secciones, con las explicaciones a la vista y lo que menos se cambia en «Más opciones»."), false),
        viewButton(tr("Compacta"), tr("Todas las opciones juntas en formularios; las explicaciones, en las descripciones emergentes."), true),
    ]));

    // Una sección: título con línea en la compacta; recuadro en la simplificada.
    const section = (title, parent = page, id = "") => {
        if (compact) {
            parent.append(el("h4", { className: "section", textContent: title, id }));
            return parent;
        }
        const box = el("fieldset", { className: "collab-card", id }, [el("legend", { textContent: title })]);
        parent.append(box);
        return box;
    };
    const note = (text) => el("small", { className: "collab-explain", textContent: text });
    // En la simplificada: el nombre encima, el campo y su explicación; en la compacta, una fila.
    const field = (box, label, control, help = "") => {
        if (compact) {
            if (help)
                control.title = help;
            box.append(el("label", { className: "collab-row" }, [label, control]));
        } else {
            box.append(el("div", { className: "collab-field" }, [el("b", { textContent: label.replace(/:$/, "") }), control, help ? note(help) : null]));
        }
    };
    const check = (box, text, key, help, onChange = null) => {
        const c = checkbox(text, key === "collabRealtime" || key === "collabAnnounce" || key === "collabShowCursors" ? Settings.get(key) !== false : !!Settings.get(key));
        c.input.addEventListener("change", () => {
            set(key, c.input.checked);
            onChange?.();
        });
        if (compact) {
            c.title = help;
            box.append(c);
        } else {
            box.append(el("div", { className: "collab-option" }, [c, note(help)]));
        }
        return c;
    };
    const button = (text, run, enabled = true, iconName = "") => {
        const b = el("button", { className: "pushbutton", disabled: !enabled }, [iconName ? icon(iconName, 16) : null, plain(text)]);
        b.addEventListener("click", async () => {
            await run();
            refresh();
        });
        return b;
    };

    // --- Sesión --------------------------------------------------------------------------------
    const session = section(tr("Sesión"));
    if (!compact)
        session.append(note(tr("Edita la imagen actual con otras personas: tú la hospedas y los demás se unen desde Kaint o desde Kaint para la web. Solo el anfitrión guarda la imagen; los invitados pueden exportar una copia.")));
    const active = !!collab?.active;
    const radio = (value, text, help) => {
        const input = el("input", { type: "radio", name: "collab-mode", value, checked: s.mode === value, disabled: active });
        input.addEventListener("change", () => {
            set("collabMode", value);
            refresh();
        });
        const label = el("label", { title: help }, [input, plain(text)]);
        return compact ? label : el("div", { className: "collab-option" }, [label, note(help)]);
    };
    const p2pHelp = tr("Con cualquiera, esté donde esté: le das el código de la sesión y la imagen va directa de un equipo a otro.");
    const lanHelp = tr("Con los equipos de tu misma red (casa, oficina, clase): te encuentran en la lista de sesiones cercanas o por la dirección IP.");
    if (compact)
        session.append(el("div", { className: "row-buttons" }, [el("span", { textContent: tr("Conexión:") }), radio("p2p", tr("P2P (por Internet)"), p2pHelp), radio("lan", tr("Red local (LAN)"), lanHelp)]));
    else
        session.append(el("b", { textContent: tr("Conexión") }), radio("p2p", tr("P2P (por Internet)"), p2pHelp), radio("lan", tr("Red local (LAN)"), lanHelp));
    session.append(el("p", { className: "collab-title", textContent: collab ? collab.statusText() : "" }));
    if (collab) {
        session.append(el("div", { className: `row-buttons${compact ? "" : " collab-big"}` }, [
            button(s.mode === "lan" ? tr("Hospedar la imagen actual…") : tr("Hospedar la imagen actual"), () => collab.hostCurrent(s.mode), !active && !!app.doc, "collab"),
            button(s.mode === "lan" ? tr("Unirse a una sesión…") : tr("Unirse con un código…"), () => collab.joinDialog(s.mode), !active, "network-connect"),
            active ? button(tr("Ventana de la sesión"), () => collab.sessionDialog()) : null,
            active ? button(collab.hosting ? tr("Terminar la sesión") : tr("Salir de la sesión"), () => collab.leave(true)) : null,
        ]));
        const hs = collab.session;
        if (collab.hosting && hs.connection === "p2p" && hs.code) {
            const code = secretView(hs.code, "code");
            session.append(compact ? el("div", { className: "row-buttons" }, [el("span", { textContent: tr("Código de la sesión:") }), code])
                                   : el("div", { className: "collab-field" }, [el("b", { textContent: tr("Código de la sesión") }), code,
                                                                                note(tr("Dáselo a quien quieras invitar: lo escribe en Archivo > Colaboración > P2P > Unirse con un código."))]),
                           el("small", { className: "collab-note", textContent: secretHint() }));
        }
    }

    // --- Tú ------------------------------------------------------------------------------------
    const me = section(tr("Tú"));
    const name = el("input", { type: "text", value: Settings.get("collabName") || "", placeholder: tr("Invitado"), maxLength: 40 });
    name.addEventListener("change", () => set("collabName", name.value.trim()));
    field(me, tr("Nombre en pantalla:"), name, tr("Así te ven los demás: junto a tu puntero y en la lista de participantes."));
    const own = checkbox(tr("Color propio"), !!s.color);
    const color = el("input", { type: "color", value: s.color || "#1e88e5", disabled: !s.color });
    const applyColor = () => {
        color.disabled = !own.input.checked;
        set("collabColor", own.input.checked ? color.value : "");
    };
    own.input.addEventListener("change", applyColor);
    color.addEventListener("input", applyColor);
    field(me, tr("Color del puntero:"), el("span", { className: "row-buttons" }, [own, color]), tr("Sin marcar, el anfitrión te da uno que no use nadie más."));
    const code = el("b", { className: "collab-code", textContent: deviceCode() });
    const copy = el("button", { className: "pushbutton", textContent: tr("Copiar") });
    copy.addEventListener("click", () => {
        navigator.clipboard?.writeText(code.textContent);
        copy.textContent = tr("Copiado");
        setTimeout(() => (copy.textContent = tr("Copiar")), 1500);
    });
    const regenerate = el("button", { className: "pushbutton", textContent: tr("Generar otro") });
    regenerate.addEventListener("click", async () => {
        const dlg = new TaskDialog(tr("Generar otro ID"), "view-refresh", tr("Los anfitriones que tengan el ID actual en su lista dejarán de reconocer este equipo."));
        const yes = dlg.addChoice("view-refresh", tr("Generar otro"), tr("Cambiar el ID de este equipo."));
        dlg.addChoice("dialog-cancel", tr("Cancelar"), tr("Conservar el ID actual."), true);
        if ((await dlg.exec()) === yes)
            code.textContent = regenerateDeviceCode();
    });
    const idHelp = tr("Para entrar en una sesión que solo admite equipos autorizados, dale este ID al anfitrión. El ID no viaja por la red: el equipo demuestra que lo tiene sin enviarlo.");
    field(me, tr("ID de este equipo:"), el("span", { className: "row-buttons" }, [code, copy, regenerate]), idHelp);
    if (compact)
        me.append(el("small", { className: "collab-note", textContent: idHelp }));

    // --- Al hospedar ---------------------------------------------------------------------------
    const hosting = section(tr("Al hospedar"));
    const sessionName = el("input", { type: "text", value: s.sessionName, placeholder: tr("Sesión de %1", s.name), maxLength: 60 });
    sessionName.addEventListener("change", () => set("collabSessionName", sessionName.value.trim()));
    field(hosting, tr("Nombre de la sesión:"), sessionName, tr("El que ven los demás al unirse y en la lista de sesiones cercanas."));
    const accessChoices = [["free", tr("Libre"), tr("Cualquiera que tenga el código o esté en tu red puede entrar.")],
                           ["approve", tr("Con permiso"), tr("Te avisa cuando alguien quiere entrar y tú decides si pasa.")],
                           ["password", tr("Con contraseña"), tr("Solo entra quien sepa la contraseña.")],
                           ["ids", tr("Solo equipos autorizados"), tr("Solo entran los equipos cuyo ID agregues a la lista.")]];
    const password = el("input", { type: "password", value: s.password, placeholder: tr("Contraseña de la sesión") });
    password.addEventListener("change", () => set("collabPassword", password.value));
    const ids = el("div", { className: "list collab-ids" });
    const fillIds = () => {
        ids.replaceChildren(...collabSettings().allowedIds.map((line, i) => {
            const remove = el("button", { className: "pushbutton", textContent: tr("Quitar") });
            remove.addEventListener("click", () => {
                const list = [...collabSettings().allowedIds];
                list.splice(i, 1);
                set("collabAllowedIds", list);
                fillIds();
            });
            return el("div", { className: "row" }, [el("b", { className: "collab-code", textContent: line.split(" ")[0] }),
                                                    el("span", { textContent: line.split(" ").slice(1).join(" ") }), remove]);
        }));
        if (!ids.children.length)
            ids.appendChild(el("div", { className: "row", textContent: tr("Ningún equipo todavía.") }));
    };
    fillIds();
    const newId = el("input", { type: "text", placeholder: "ABCD-EFGH-JKLM", style: { width: "140px" } });
    const newName = el("input", { type: "text", placeholder: tr("Nombre (opcional)"), style: { width: "140px" } });
    const add = el("button", { className: "pushbutton", textContent: tr("Agregar") });
    add.addEventListener("click", () => {
        const n = normalizedCode(newId.value);
        if (n.length !== 12) {
            newId.setCustomValidity(tr("El ID tiene 12 letras y números, como ABCD-EFGH-JKLM."));
            newId.reportValidity();
            return;
        }
        newId.setCustomValidity("");
        set("collabAllowedIds", [...collabSettings().allowedIds, `${prettyCode(n)}${newName.value.trim() ? ` ${newName.value.trim()}` : ""}`]);
        newId.value = newName.value = "";
        fillIds();
    });
    const idsNote = el("small", { className: "collab-explain" });
    const idsBox = el("div", { className: "collab-ids-box" }, [
        compact ? el("span", { textContent: tr("Equipos autorizados (también entran sin esperar con «Con permiso»):") }) : el("b", { textContent: tr("Equipos autorizados") }),
        compact ? null : idsNote, ids, el("div", { className: "row-buttons" }, [newId, newName, add])]);
    const passwordBox = el("div", { className: "collab-indent" }, [password]);
    const syncAccess = (value) => {
        const listUsed = value === "ids" || value === "approve";
        if (compact) {
            password.disabled = value !== "password";
            idsBox.classList.toggle("disabled", !listUsed);
        } else {
            passwordBox.hidden = value !== "password";
            idsBox.hidden = !listUsed;
            idsNote.textContent = value === "ids" ? tr("Solo entran estos equipos. Cada persona ve el ID de su equipo en Configuración > Colaboración.")
                : tr("Estos equipos entran sin esperar a que los apruebes.");
        }
    };
    if (compact) {
        const access = select(accessChoices.map(([v, t]) => [v, t]), s.access);
        access.addEventListener("change", () => {
            set("collabAccess", access.value);
            syncAccess(access.value);
        });
        field(hosting, tr("Quién puede entrar:"), access);
        field(hosting, tr("Contraseña:"), password);
        hosting.append(idsBox);
    } else {
        hosting.append(el("b", { textContent: tr("¿Quién puede entrar?") }));
        for (const [value, text, help] of accessChoices) {
            const input = el("input", { type: "radio", name: "collab-access", value, checked: s.access === value });
            input.addEventListener("change", () => {
                set("collabAccess", value);
                syncAccess(value);
            });
            hosting.append(el("div", { className: "collab-option" }, [el("label", {}, [input, plain(text)]), note(help)]));
            if (value === "password")
                hosting.append(passwordBox);
        }
        idsBox.classList.add("collab-indent");
        hosting.append(idsBox);
    }
    syncAccess(s.access);

    const changes = compact ? hosting : section(tr("Cambios de los invitados"));
    check(changes, tr("Edición en tiempo real entre copias de Kaint para la web"), "collabRealtime",
          tr("Los invitados con Kaint para la web cambian la imagen al momento. Sin marcar, también ellos sugieren sus cambios para que los aceptes."));
    check(changes, tr("Aceptar automáticamente los cambios sugeridos"), "collabAutoAccept", tr("Lo que sugieran los invitados se aplica a la imagen sin preguntar."));
    changes.append(el("small", { className: "collab-note", textContent: tr("Entre Kaint y Kaint para la web la edición siempre es con confirmaciones: el invitado edita un borrador con su propio historial y lo sugiere; al aceptarlo, el cambio aparece para todos y en el historial del anfitrión como «Cambio hecho por…».") }));

    // --- Más opciones: P2P, red local y la vista ---------------------------------------------------
    let more = page;
    if (!compact) {
        const reveal = !!collab?.revealSection;
        const toggle = el("button", { className: "tbutton collab-more", "aria-expanded": String(reveal) }, [el("span", { className: "collab-arrow", textContent: reveal ? "▾" : "▸" }), tr("Más opciones")]);
        more = el("div", { className: "collab-more-box", hidden: !reveal });
        toggle.addEventListener("click", () => {
            more.hidden = !more.hidden;
            toggle.setAttribute("aria-expanded", String(!more.hidden));
            toggle.querySelector(".collab-arrow").textContent = more.hidden ? "▸" : "▾";
        });
        page.append(toggle, more);
    }
    const serverList = (box, label, key, defaults, help) => {
        const area = el("textarea", { rows: 3, value: collabSettings()[key === "collabSignalServers" ? "signalServers" : "iceServers"].join("\n"), spellcheck: false, className: "collab-servers" });
        area.addEventListener("change", () => {
            const list = area.value.split("\n").map((l) => l.trim()).filter(Boolean);
            set(key, list.join("\n") === defaults.join("\n") ? null : list);
        });
        const reset = el("button", { className: "pushbutton", textContent: tr("Restablecer"), title: tr("Volver a los predeterminados.") });
        reset.addEventListener("click", () => {
            area.value = defaults.join("\n");
            set(key, null);
        });
        field(box, label, el("div", { className: "collab-servers-box" }, [area, el("div", { className: "row-buttons" }, [reset])]), help);
    };
    const p2p = section(tr("P2P (por Internet)"), more, "collab-p2p");
    p2p.append(note(tr("Los servidores de señalización solo presentan a los equipos (como los rastreadores de WebTorrent): la imagen va directa de un equipo a otro, cifrada. El código de la sesión no viaja: solo su huella.")));
    serverList(p2p, tr("Servidores de señalización:"), "collabSignalServers", DefaultSignalServers,
               tr("Direcciones wss:// de rastreadores de WebTorrent o de tools/collab-server.mjs (ws://equipo:47913/signal), una por línea. Todos los de una sesión tienen que usar al menos uno en común."));
    serverList(p2p, tr("Servidores STUN y TURN:"), "collabIceServers", DefaultIceServers,
               tr("Ayudan a atravesar los routers. «stun:servidor:puerto» o, si una red no deja conectar directamente, un TURN: «turn:usuario:contraseña@servidor:puerto»."));
    const lan = section(tr("Red local (LAN)"), more, "collab-lan");
    check(lan, tr("Anunciar la sesión en la red local"), "collabAnnounce", tr("Kaint (escritorio) la encuentra al buscar sesiones cercanas."));
    lan.append(note(tr("Para hospedar en la red local desde el navegador hace falta el servidor de Kaint para la web (node tools/collab-server.mjs); por P2P no.")));
    const view = section(tr("Vista"), more);
    check(view, tr("Mostrar los punteros de los demás"), "collabShowCursors", tr("Un triángulo del color de cada participante, con su nombre y el icono de su herramienta."));

    // «Configuración P2P…» y «Configuración LAN…» (Archivo > Colaboración) llevan a su sección.
    if (collab?.revealSection) {
        const id = collab.revealSection === "lan" ? "collab-lan" : "collab-p2p";
        collab.revealSection = "";
        setTimeout(() => page.querySelector(`#${id}`)?.scrollIntoView({ block: "start" }), 60);
    }
    return [page];
}

// El modo colaborativo en Kaint para la web (src/app/collab/CollabController.cpp y CollabDialogs.cpp
// de Kaint): unirse a una sesión, hospedarla a través de tools/collab-server.mjs, la ventana de la
// sesión, los punteros de los demás y la sección Colaboración de Configuración.
import { Settings } from "../Settings.js";
import { checkbox, Dialog, el, select, TaskDialog } from "../ui/Dialogs.js";
import { icon } from "../ui/Icons.js";
import { updateActions } from "../ui/Actions.js";
import { plain, tr } from "../i18n.js";
import { DEFAULT_PORT, normalizedCode, prettyCode } from "./Protocol.js";
import { CollabSession, deviceCode, regenerateDeviceCode } from "./Session.js";

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
                setTimeout(() => this.joinDialog(), 600);
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
        if (s.state === "connecting")
            return tr("Conectando con %1…", s.url);
        if (s.state === "waiting")
            return tr("Esperando a que el anfitrión te deje entrar…");
        const n = s.peers.size;
        const people = n === 1 ? tr("1 participante") : tr("%1 participantes", n);
        if (s.isHost)
            return tr("Hospedando «%1» · %2", s.sessionName, people);
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

    async joinDialog() {
        // Una sola a la vez (la que se abre sola al cargar la página servida por una sesión y la del menú).
        if (this.joining)
            return;
        this.joining = true;
        try {
            await this.showJoinDialog();
        } finally {
            this.joining = false;
        }
    }

    async showJoinDialog() {
        if (this.active && !(await this.leave(true)))
            return;
        const settings = collabSettings();
        const dlg = new Dialog(tr("Unirse a una sesión colaborativa"), { iconName: "collab" });
        const served = this.served;
        const address = el("input", { type: "text", placeholder: tr("Ej.: 192.168.1.20 o 192.168.1.20:47913"), style: { width: "280px" } });
        if (served?.session)
            address.value = location.host;
        const password = el("input", { type: "password", placeholder: tr("Solo si la sesión pide contraseña"), style: { width: "280px" } });
        const name = el("input", { type: "text", value: Settings.get("collabName") || "", placeholder: settings.name, maxLength: 40, style: { width: "280px" } });
        const status = el("p", { className: "collab-status-text" });
        const intro = served?.session
            ? tr("Esta página la sirve la sesión «%1» de %2 (%3).", served.session, served.host, accessText(served.auth))
            : tr("Escribe la dirección del equipo que hospeda la sesión (lo dice su ventana de la sesión).");
        const httpsNote = location.protocol === "https:"
            ? el("small", { className: "collab-note", textContent: tr("Esta página se abrió con https: el navegador no deja conectarse a una sesión de la red local. Abre la dirección http:// que muestra el anfitrión.") })
            : null;
        dlg.body.append(el("div", { className: "collab-form" }, [
            el("p", { textContent: intro }),
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
        const dlg = new Dialog(tr("Hospedar esta imagen"), { iconName: "collab" });
        const relay = this.served?.server === "relay";
        const localhost = /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname);
        const address = el("input", { type: "text", value: relay ? location.host : Settings.get("collabServer") || `localhost:${DEFAULT_PORT}`, style: { width: "260px" } });
        const key = el("input", { type: "text", placeholder: tr("Solo desde otro equipo"), style: { width: "260px" } });
        const status = el("p", { className: "collab-status-text" });
        const settings = collabSettings();
        dlg.body.append(el("div", { className: "collab-form" }, [
            el("p", { textContent: relay
                ? tr("Este navegador hospedará la imagen actual a través del servidor de Kaint para la web que abrió esta página. Los demás se unen abriendo esta misma dirección o desde Kaint.")
                : tr("Un navegador no puede abrir un servidor. Para hospedar, ejecuta en este equipo el servidor de Kaint para la web (con Node.js): «node tools/collab-server.mjs» en la carpeta de Kaint para la web, y abre la dirección que muestra (http://localhost:47913/).") }),
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
        const render = async () => {
            const items = [el("p", { className: "collab-title", textContent: this.statusText() })];
            if (this.active && s.isHost && s.state === "joined") {
                const where = this.served?.server === "relay" ? location.host : sessionUrl(s.url).replace(/^ws:\/\//, "").replace(/\/$/, "");
                items.push(el("small", { className: "collab-note", textContent: tr("Para unirse: abrir http://%1/ en un navegador de la red (cambia «localhost» por la IP de este equipo) o, desde Kaint, Archivo > Colaboración > Unirse.", where) }));
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
}

// --- Configuración > Colaboración ----------------------------------------------------------------

export function collabSettingsPage(app, refresh) {
    const collab = app?.collab;
    const set = (key, value) => Settings.set(key, value);
    const heading = (text) => el("h4", { className: "section", textContent: text });
    const note = (text) => el("small", { textContent: text, style: { opacity: 0.75 } });
    const s = collabSettings();
    const items = [el("p", { textContent: tr("Edita la imagen actual con otras personas de tu red local: un anfitrión la comparte y los demás se unen desde Kaint o desde Kaint para la web. Solo el anfitrión guarda la imagen; los invitados pueden exportar una copia.") })];

    // Sesión.
    items.push(heading(tr("Sesión")), el("p", { textContent: collab ? collab.statusText() : "" }));
    if (collab) {
        const button = (text, run, enabled = true) => {
            const b = el("button", { className: "pushbutton", textContent: plain(text), disabled: !enabled });
            b.addEventListener("click", async () => {
                await run();
                refresh();
            });
            return b;
        };
        items.push(el("div", { className: "row-buttons" }, [
            button(tr("Hospedar la imagen actual…"), () => collab.hostDialog(), !collab.active && !!app.doc),
            button(tr("Unirse a una sesión…"), () => collab.joinDialog(), !collab.active),
            button(tr("Ventana de la sesión"), () => collab.sessionDialog(), collab.active),
            button(collab.hosting ? tr("Terminar la sesión") : tr("Salir de la sesión"), () => collab.leave(true), collab.active),
        ]));
    }

    // Tú.
    const name = el("input", { type: "text", value: Settings.get("collabName") || "", placeholder: tr("Invitado"), maxLength: 40 });
    name.addEventListener("change", () => set("collabName", name.value.trim()));
    const own = checkbox(tr("Color propio"), !!s.color);
    own.title = tr("Sin marcar, el anfitrión te da uno que no use nadie más.");
    const color = el("input", { type: "color", value: s.color || "#1e88e5", disabled: !s.color });
    const applyColor = () => {
        color.disabled = !own.input.checked;
        set("collabColor", own.input.checked ? color.value : "");
    };
    own.input.addEventListener("change", applyColor);
    color.addEventListener("input", applyColor);
    const code = el("b", { className: "collab-code", textContent: deviceCode() });
    const copy = el("button", { className: "pushbutton", textContent: tr("Copiar") });
    copy.addEventListener("click", () => navigator.clipboard?.writeText(code.textContent));
    const regenerate = el("button", { className: "pushbutton", textContent: tr("Generar otro") });
    regenerate.addEventListener("click", async () => {
        const dlg = new TaskDialog(tr("Generar otro ID"), "view-refresh", tr("Los anfitriones que tengan el ID actual en su lista dejarán de reconocer este equipo."));
        const yes = dlg.addChoice("view-refresh", tr("Generar otro"), tr("Cambiar el ID de este equipo."));
        dlg.addChoice("dialog-cancel", tr("Cancelar"), tr("Conservar el ID actual."), true);
        if ((await dlg.exec()) === yes)
            code.textContent = regenerateDeviceCode();
    });
    items.push(heading(tr("Tú")), el("label", {}, [tr("Nombre en pantalla:"), name]),
               el("div", { className: "row-buttons" }, [el("span", { textContent: tr("Color del puntero:") }), own, color]),
               el("div", { className: "row-buttons" }, [el("span", { textContent: tr("ID de este equipo:") }), code, copy, regenerate]),
               note(tr("Para entrar en una sesión que solo admite equipos autorizados, dale este ID al anfitrión. El ID no viaja por la red: el equipo demuestra que lo tiene sin enviarlo.")));

    // Al hospedar.
    const sessionName = el("input", { type: "text", value: s.sessionName, placeholder: tr("Sesión de %1", s.name), maxLength: 60 });
    sessionName.addEventListener("change", () => set("collabSessionName", sessionName.value.trim()));
    const access = select([["free", tr("Libre: cualquiera de la red puede entrar")], ["approve", tr("Con permiso: apruebas a cada persona")],
                           ["password", tr("Con contraseña")], ["ids", tr("Solo equipos autorizados (por su ID)")]], s.access);
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
    const idsBox = el("div", {}, [el("span", { textContent: tr("Equipos autorizados (también entran sin esperar con «Con permiso»):") }), ids,
                                  el("div", { className: "row-buttons" }, [newId, newName, add])]);
    const syncAccess = () => {
        password.disabled = access.value !== "password";
        idsBox.classList.toggle("disabled", access.value !== "ids" && access.value !== "approve");
    };
    access.addEventListener("change", () => {
        set("collabAccess", access.value);
        syncAccess();
    });
    syncAccess();
    const check = (text, key, tip) => {
        const c = checkbox(text, !!Settings.get(key));
        c.title = tip;
        c.input.addEventListener("change", () => set(key, c.input.checked));
        return c;
    };
    items.push(heading(tr("Al hospedar")), el("label", {}, [tr("Nombre de la sesión:"), sessionName]), el("label", {}, [tr("Quién puede entrar:"), access]),
               el("label", {}, [tr("Contraseña:"), password]), idsBox,
               check(tr("Edición en tiempo real entre copias de Kaint para la web"), "collabRealtime",
                     tr("Los invitados con Kaint para la web cambian la imagen al momento. Sin marcar, también ellos sugieren sus cambios para que los aceptes.")),
               check(tr("Aceptar automáticamente los cambios sugeridos"), "collabAutoAccept", tr("Lo que sugieran los invitados se aplica a la imagen sin preguntar.")),
               check(tr("Anunciar la sesión en la red local"), "collabAnnounce", tr("Kaint (escritorio) la encuentra al buscar sesiones cercanas.")),
               note(tr("Entre Kaint y Kaint para la web la edición siempre es con confirmaciones: el invitado edita un borrador con su propio historial y lo sugiere; al aceptarlo, el cambio aparece para todos y en el historial del anfitrión como «Cambio hecho por…».")),
               note(tr("Para hospedar desde el navegador hace falta el servidor de Kaint para la web (node tools/collab-server.mjs).")),
               heading(tr("Vista")),
               check(tr("Mostrar los punteros de los demás"), "collabShowCursors", tr("Un triángulo del color de cada participante, con su nombre y el icono de su herramienta.")));
    return items;
}

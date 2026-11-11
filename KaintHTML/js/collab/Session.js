// Sesión colaborativa (src/app/collab/CollabSession.cpp de Kaint): como invitado de Kaint o de otro
// navegador, o como anfitriona a través del servidor de tools/collab-server.mjs.
//
// Se guarda la copia "canónica" de la imagen (lo último que se sabe de la del anfitrión) y lo que
// cambia en el documento se descubre comparándolo con ella tras cada paso del historial; viaja
// como operaciones (píxeles de una región con su máscara, la lista de capas o la imagen entera).
// Lo que llega de los demás se aplica a las dos copias; en el documento solo donde no hay cambios
// propios sin enviar, y lo que toca una capa en la que una herramienta está trabajando espera.
import { P2PGuest, P2PHost } from "./P2P.js";
import { Document, History, Layer } from "../core/Document.js";
import { Selection } from "../core/Selection.js";
import { clampRect, context, createCanvas, unionRect } from "../core/Raster.js";
import { tr } from "../i18n.js";
import { VERSION as APP_VERSION } from "../version.js";
import {
    VERSION, base64ToBytes, bytesToBase64, decodeMask, deviceTag, encodeMask, mergeOrder, newSessionCode, normalizedCode, proof,
} from "./Protocol.js";

const DEVICE_KEY = "kaint.collab.deviceCode";
const HISTORY_ICON = "collab";
const MAX_PIXELS = 200e6;

export function deviceCode() {
    let code = "";
    try {
        code = localStorage.getItem(DEVICE_KEY) || "";
    } catch {
        // Sin almacenamiento: un ID para esta página.
    }
    if (normalizedCode(code).length !== 12) {
        code = newCode();
        try {
            localStorage.setItem(DEVICE_KEY, code);
        } catch {
            // ignore
        }
    }
    return code;
}

export function regenerateDeviceCode() {
    const code = newCode();
    try {
        localStorage.setItem(DEVICE_KEY, code);
    } catch {
        // ignore
    }
    return code;
}

function newCode() {
    const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789", bytes = new Uint8Array(12);
    crypto.getRandomValues(bytes);
    const s = [...bytes].map((b) => alphabet[b % alphabet.length]).join("");
    return `${s.slice(0, 4)}-${s.slice(4, 8)}-${s.slice(8, 12)}`;
}

export function reasonText(reason) {
    switch (reason) {
    case "closed": return tr("El anfitrión terminó la sesión.");
    case "kicked": return tr("El anfitrión te sacó de la sesión.");
    case "password": return tr("La contraseña no es correcta.");
    case "device": return tr("Este equipo no está en la lista de equipos autorizados del anfitrión.");
    case "rejected": return tr("El anfitrión no aceptó tu solicitud.");
    case "version": return tr("La sesión usa otra versión del modo colaborativo.");
    case "lost": return tr("Se perdió la conexión con la sesión.");
    case "nohost": return tr("Todavía no hay ninguna sesión en ese servidor.");
    case "busy": return tr("Ese servidor ya tiene un anfitrión.");
    case "hostkey": return tr("La clave para hospedar no es correcta.");
    default: return reason || "";
    }
}

const props = (l) => ({ name: l.name, visible: l.visible, opacity: l.opacity, blend: l.blend });
const sameProps = (a, b) => a.name === b.name && a.visible === b.visible && a.opacity === b.opacity && a.blend === b.blend;
const propsJson = (id, p) => ({ id, name: p.name, visible: p.visible, opacity: p.opacity, blend: p.blend });

function propsFrom(o, p = { name: "", visible: true, opacity: 255, blend: "normal" }) {
    const out = { ...p };
    if (o.name !== undefined) out.name = String(o.name).slice(0, 256);
    if (o.visible !== undefined) out.visible = !!o.visible;
    if (o.opacity !== undefined) out.opacity = Math.max(0, Math.min(255, Math.round(+o.opacity || 0)));
    if (o.blend !== undefined) out.blend = String(o.blend);
    return out;
}

// --- Imágenes ----------------------------------------------------------------------------------

// Un lienzo de trabajo para pasar píxeles por el formato del navegador (premultiplicado): lo que
// se guarda en la copia canónica es lo mismo que luego se lee de las capas.
let scratch = null;
function scratchContext(w, h) {
    if (!scratch || scratch.canvas.width < w || scratch.canvas.height < h)
        scratch = context(createCanvas(Math.max(w, scratch?.canvas.width || 0), Math.max(h, scratch?.canvas.height || 0)));
    return scratch;
}

function normalize(data, w, h) {
    const ctx = scratchContext(w, h);
    ctx.putImageData(new ImageData(new Uint8ClampedArray(data.buffer, data.byteOffset, w * h * 4), w, h), 0, 0);
    return new Uint32Array(ctx.getImageData(0, 0, w, h).data.buffer);
}

async function encodePng(u32, w, h) {
    const canvas = createCanvas(w, h);
    context(canvas).putImageData(new ImageData(new Uint8ClampedArray(u32.buffer, u32.byteOffset, w * h * 4), w, h), 0, 0);
    const blob = canvas.convertToBlob ? await canvas.convertToBlob({ type: "image/png" })
        : await new Promise((resolve) => canvas.toBlob(resolve, "image/png"));
    return bytesToBase64(new Uint8Array(await blob.arrayBuffer()));
}

async function decodePng(base64, w, h) {
    const image = await decodePngAny(base64);
    return image && image.w === w && image.h === h ? image.data : null;
}

async function decodePngAny(base64) {
    const bytes = base64ToBytes(base64);
    const bitmap = await createImageBitmap(new Blob([bytes], { type: "image/png" }), { premultiplyAlpha: "none", colorSpaceConversion: "none" });
    const w = bitmap.width, h = bitmap.height;
    if (!w || !h || w * h > MAX_PIXELS) {
        bitmap.close?.();
        return null;
    }
    const ctx = scratchContext(w, h);
    ctx.clearRect(0, 0, w, h);
    ctx.drawImage(bitmap, 0, 0);
    bitmap.close?.();
    return { w, h, data: new Uint32Array(ctx.getImageData(0, 0, w, h).data.buffer) };
}

function readLayer(layer, r) {
    return new Uint32Array(layer.ctx.getImageData(r.x, r.y, r.w, r.h).data.buffer);
}

function writeLayer(layer, r, u32) {
    layer.ctx.putImageData(new ImageData(new Uint8ClampedArray(u32.buffer, u32.byteOffset, r.w * r.h * 4), r.w, r.h), r.x, r.y);
}

function region(data, width, r) {
    const out = new Uint32Array(r.w * r.h);
    for (let y = 0; y < r.h; ++y)
        out.set(data.subarray((r.y + y) * width + r.x, (r.y + y) * width + r.x + r.w), y * r.w);
    return out;
}

function putRegion(data, width, r, values) {
    for (let y = 0; y < r.h; ++y)
        data.set(values.subarray(y * r.w, (y + 1) * r.w), (r.y + y) * width + r.x);
}

// ------------------------------------------------------------------------------------------------

export class CollabSession extends EventTarget {
    // Como invitado: 'url' es ws://dirección:puerto/.
    static join(app, url, settings, password = "") {
        const s = new CollabSession(app, "guest", settings);
        s.password = password;
        s.connect(url);
        return s;
    }

    // Como anfitriona, a través del servidor de Kaint para la web.
    static host(app, doc, url, settings, hostKey = "") {
        const s = new CollabSession(app, "host", settings);
        s.hostKey = hostKey;
        s.hostDoc = doc;
        s.connect(url);
        return s;
    }

    // P2P: el navegador hospeda (con el centro de la sesión dentro) y los invitados llegan por los
    // servidores de señalización con el código.
    static hostP2P(app, doc, settings) {
        const s = new CollabSession(app, "host", settings);
        s.connection = "p2p";
        s.hostDoc = doc;
        s.code = newSessionCode();
        s.p2p = new P2PHost(settings, s.code);
        s.p2p.addEventListener("changed", () => s.emit("state"));
        s.connect(s.p2p.start(), "P2P");
        return s;
    }

    static joinP2P(app, code, settings, password = "") {
        const s = new CollabSession(app, "guest", settings);
        s.connection = "p2p";
        s.code = code;
        s.password = password;
        s.url = "P2P";
        s.progress = tr("Buscando la sesión…");
        s.p2p = new P2PGuest(settings, code);
        s.p2p.addEventListener("progress", () => {
            s.progress = tr("Conectando con el anfitrión…");
            s.emit("state");
        });
        s.p2p.connect().then((link) => {
            if (s.state === "ended")
                return link.close();
            s.progress = tr("Conectado; entrando en la sesión…");
            s.p2p = null;
            s.connect(link, "P2P");
            s.emit("state");
        }).catch((e) => s.finish(e.message === "nohost"
            ? tr("No se encontró la sesión. Revisa el código y que el anfitrión siga hospedándola (y que los dos usen algún servidor de señalización en común).")
            : e.message === "noservers" ? tr("No hay servidores de señalización en la configuración.")
            : tr("No se pudo conectar con ningún servidor de señalización.")));
        return s;
    }

    constructor(app, role, settings) {
        super();
        this.app = app;
        this.role = role;
        this.settings = { ...settings };
        this.state = "connecting";
        this.url = "";
        this.socket = null;
        // "lan" o "p2p"; el código de la sesión P2P y lo que se está haciendo al conectar.
        this.connection = "lan";
        this.code = "";
        this.progress = "";
        this.p2p = null;
        this.myId = role === "host" ? 0 : -1;
        this.peers = new Map();
        this.waiting = new Map();
        this.cursors = new Map();
        this.mode = "confirm";
        this.autoAccept = false;
        this.sessionName = "";
        this.doc = null;
        this.canon = null;
        this.idOf = new WeakMap();
        this.layerById = new Map();
        this.idPrefix = role === "host" ? "h" : "g";
        this.idCounter = 0;
        this.rev = 0;
        this.queue = [];
        this.processing = false;
        this.applying = false;
        this.dirty = new Map();
        this.sendChain = Promise.resolve();
        this.proposals = [];
        this.pendingPid = 0;
        this.nextPid = 0;
        this.draftStart = 0;
        this.hasDraft = false;
        // Cambios propios enviados (en tiempo real) cuyo eco aún no ha llegado.
        this.inFlight = 0;
        this.cursorOut = null;
        this.cursorTimer = null;
        this.cursorDirty = false;
        this.busyCheck = null;
        this.listeners = [];
        // El historial del anfitrión (los invitados lo reciben; el anfitrión lo envía un momento
        // después de cada cambio) y si su imagen está guardada.
        this.hostHistory = [];
        this.hostHistoryIndex = 0;
        this.hostSaved = false;
        this.historyTimer = null;
        // Invitado: su copia propia (exportada) y si sigue al día con la imagen de la sesión.
        this.historyGeneration = 0;
        this.exported = false;
        this.exportStamp = null;
    }

    get isHost() {
        return this.role === "host";
    }

    get isLive() {
        return this.mode === "live";
    }

    emit(type, detail = {}) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }

    setState(state) {
        if (this.state === state)
            return;
        this.state = state;
        this.emit("state");
    }

    send(message) {
        if (this.socket?.readyState === WebSocket.OPEN)
            this.socket.send(JSON.stringify(message));
    }

    // 'target': la dirección ws:// o un canal ya abierto que se usa como un WebSocket (P2P).
    connect(target, label = "") {
        this.url = label || target;
        let socket = target;
        if (typeof target === "string") {
            try {
                socket = new WebSocket(target);
            } catch (e) {
                setTimeout(() => this.finish(tr("No se pudo conectar: %1", e.message)), 0);
                return;
            }
        }
        const url = this.url;
        this.socket = socket;
        const timeout = setTimeout(() => {
            if (this.state === "connecting" && socket.readyState !== WebSocket.OPEN)
                this.finish(tr("No se pudo conectar: %1", tr("no respondió a tiempo")));
        }, 10000);
        socket.addEventListener("message", (e) => {
            let m;
            try {
                m = JSON.parse(e.data);
            } catch {
                return;
            }
            if (m && typeof m === "object")
                this.onMessage(m);
        });
        socket.addEventListener("close", () => {
            clearTimeout(timeout);
            this.finish(this.state === "connecting" ? tr("No se pudo conectar con %1.", url) : reasonText("lost"));
        });
    }

    finish(reason = "") {
        if (this.state === "ended")
            return;
        this.setState("ended");
        clearTimeout(this.cursorTimer);
        clearTimeout(this.historyTimer);
        this.queue = [];
        this.detachDocument();
        try {
            this.socket?.close();
        } catch {
            // ignore
        }
        this.p2p?.stop();
        this.p2p = null;
        this.cursors.clear();
        this.emit("cursors");
        this.emit("ended", { reason });
    }

    leave() {
        if (this.state === "ended")
            return;
        this.send(this.isHost ? { t: "end" } : { t: "leave" });
        this.finish("");
    }

    peer(id) {
        return this.peers.get(id) || null;
    }

    peerList() {
        return [...this.peers.values()].sort((a, b) => a.id - b.id);
    }

    // --- Mensajes --------------------------------------------------------------------------------

    onMessage(m) {
        switch (m.t) {
        case "hello": {
            this.sessionName = String(m.session || "").slice(0, 80);
            if (m.v !== VERSION)
                return this.finish(reasonText("version"));
            const nonce = base64ToBytes(m.nonce);
            const code = deviceCode();
            const join = { t: "join", v: VERSION, name: this.settings.name, platform: "web", app: `Kaint web ${APP_VERSION}`, device: deviceTag(code) };
            if (this.settings.color)
                join.color = this.settings.color;
            if (this.isHost) {
                join.role = "host";
                join.config = this.hostConfig();
                if (this.hostKey)
                    join.proof = proof(normalizedCode(this.hostKey), nonce);
            } else if (m.auth === "password") {
                if (!this.password)
                    return this.finish(tr("Esta sesión pide contraseña."));
                join.proof = proof(this.password, nonce);
            } else {
                join.proof = proof(normalizedCode(code), nonce);
            }
            this.send(join);
            return;
        }
        case "waiting":
            return this.setState("waiting");
        case "denied":
            return this.finish(reasonText(m.reason));
        case "joined":
            this.myId = m.you;
            this.idPrefix = this.isHost ? "h" : `${m.you}-`;
            if (!this.isHost) {
                this.mode = m.mode || "confirm";
                return;
            }
            // Anfitriona: la imagen actual pasa a ser la de la sesión.
            for (const p of m.peers || [])
                this.peers.set(p.id, p);
            this.attachDocument(this.hostDoc);
            this.adoptWorking();
            this.autoAccept = !!this.settings.autoAccept;
            this.sessionName = this.settings.sessionName || tr("Sesión de %1", this.settings.name);
            this.setState("joined");
            this.emit("peers");
            return;
        case "welcome":
            return this.onWelcome(m);
        case "peer": {
            const p = m.peer || {};
            if (m.op === "remove") {
                const known = this.peers.has(p.id);
                this.peers.delete(p.id);
                this.waiting.delete(p.id);
                this.cursors.delete(p.id);
                this.proposals = this.proposals.filter((x) => x.from !== p.id);
                this.emit("proposals");
                this.emit("cursors");
                if (known)
                    this.emit("notice", { text: tr("%1 salió de la sesión.", p.name) });
            } else {
                if (m.op === "add") {
                    this.waiting.delete(p.id);
                    this.emit("notice", { text: tr("%1 se unió a la sesión.", p.name) });
                    if (this.isHost)
                        this.sendWelcome(p);
                }
                this.peers.set(p.id, p);
            }
            this.emit("peers");
            return;
        }
        case "request":
            this.waiting.set(m.peer.id, m.peer);
            this.emit("peers");
            this.emit("request", { peer: m.peer });
            return;
        case "cursor": {
            if (m.from === this.myId)
                return;
            this.cursors.set(m.from, { x: +m.x || 0, y: +m.y || 0, tool: String(m.tool || ""), inside: !!m.inside });
            this.emit("cursors");
            return;
        }
        case "commit":
            if (this.state !== "joined" || this.isHost)
                return;
            this.rev = m.rev ?? this.rev;
            return this.enqueue({ kind: "commit", ops: m.ops || [], label: m.label || "", from: m.from, name: String(m.name || "").slice(0, 40), pid: m.pid || 0 });
        case "proposal":
            if (m.state === "accepted")
                return this.enqueue({ kind: "accepted", pid: m.pid });
            if (m.pid === this.pendingPid)
                this.pendingPid = 0;
            this.emit("draft");
            this.emit("notice", { text: tr("El anfitrión rechazó tus cambios. Siguen en tu borrador.") });
            return;
        case "settings": {
            const mode = m.mode || this.mode;
            this.autoAccept = !!m.autoAccept;
            if (mode !== this.mode) {
                this.mode = mode;
                if (this.isLive && this.doc) {
                    const raw = this.diff();
                    if (raw.length) {
                        this.adopt(raw);
                        this.inFlight++;
                        this.sendOps("change", { label: tr("Borrador") }, raw);
                    }
                    this.hasDraft = false;
                } else if (this.doc) {
                    this.draftStart = this.doc.history.index;
                    this.inFlight = 0;
                    clearTimeout(this.draftTimer);
                    this.draftTimer = setTimeout(() => this.updateDraft(), 300);
                }
                this.emit("draft");
            }
            this.emit("state");
            return;
        }
        case "change":
        case "propose":
            return this.isHost && this.onGuestChange(m);
        case "history":
            if (this.isHost)
                return;
            this.hostHistory = (Array.isArray(m.steps) ? m.steps : []).slice(0, 10000).map((x) => String(x).slice(0, 200));
            this.hostHistoryIndex = Math.max(0, Math.min(this.hostHistory.length, m.index | 0));
            this.hostSaved = !!m.saved;
            this.emit("hosthistory");
            this.emit("indicators");
            return;
        case "revert": {
            if (!this.isHost || this.state !== "joined")
                return;
            const p = this.peer(m.from);
            const steps = this.doc ? this.doc.history.steps.length - 1 : 0;
            if (!p?.history || !Number.isInteger(m.index) || m.index < 0 || m.index > steps || m.index === this.doc.history.index)
                return;
            this.emit("notice", { text: tr("%1 volvió a una versión anterior de la imagen.", p.name) });
            this.emit("revert", { index: m.index });
            return;
        }
        case "end":
            return this.finish(reasonText(m.reason));
        }
    }

    // --- Anfitriona --------------------------------------------------------------------------------

    hostConfig() {
        const s = this.settings;
        return { name: s.name, session: s.sessionName, access: s.access, password: s.password, allowedIds: s.allowedIds || [],
                 realtime: s.realtime !== false, autoAccept: !!s.autoAccept, announce: s.announce !== false };
    }

    updateSettings(settings) {
        this.settings = { ...settings };
        if (!this.isHost)
            return;
        this.autoAccept = !!settings.autoAccept;
        this.sessionName = settings.sessionName || tr("Sesión de %1", settings.name);
        this.send({ t: "config", config: this.hostConfig() });
        if (this.autoAccept)
            this.acceptAll();
        this.emit("state");
    }

    sendWelcome(peer) {
        this.queueSend(async () => {
            const doc = await this.snapshotJson(this.canon);
            this.send({ t: "welcome", to: peer.id, mode: peer.mode, autoAccept: this.autoAccept, peers: this.peerList(), doc });
            this.send({ ...this.historyMessage(), to: peer.id });
            if (this.cursorOut)
                this.send({ ...this.cursorOut, to: peer.id, from: 0 });
        });
    }

    // --- Historial de la sesión -------------------------------------------------------------------

    historyMessage() {
        const h = this.doc?.history;
        return { t: "history", steps: h ? h.steps.slice(1).map((x) => String(x.name || "")) : [], index: h ? h.index : 0,
                 saved: !!this.doc && !this.doc.isModified() };
    }

    // Anfitriona: varios pasos seguidos salen una sola vez.
    scheduleHistory() {
        clearTimeout(this.historyTimer);
        this.historyTimer = setTimeout(() => {
            if (this.isHost && this.state === "joined")
                this.queueSend(() => this.send(this.historyMessage()));
            this.emit("hosthistory");
            this.emit("indicators");
        }, 250);
    }

    // Los pasos del historial del anfitrión (sin el de la imagen original), el actual y si la
    // imagen del anfitrión está guardada.
    historySteps() {
        return this.isHost ? this.historyMessage().steps : this.hostHistory;
    }

    historyIndex() {
        return this.isHost ? this.doc?.history.index ?? 0 : this.hostHistoryIndex;
    }

    savedByHost() {
        return this.isHost ? !!this.doc && !this.doc.isModified() : this.hostSaved;
    }

    get canRevert() {
        return this.isHost || (this.state === "joined" && !!this.peer(this.myId)?.history);
    }

    // Volver a un paso del historial del anfitrión, para todos.
    revertTo(index) {
        if (this.isHost) {
            if (this.doc && index >= 0 && index < this.doc.history.steps.length)
                this.emit("revert", { index });
        } else if (this.canRevert) {
            this.send({ t: "revert", index });
        }
    }

    setHistoryPermission(id, allowed) {
        if (this.isHost)
            this.send({ t: "permit", peer: id, history: !!allowed });
    }

    // Invitado: su copia propia está al día si la imagen no cambió desde que la exportó.
    stamp() {
        const h = this.doc?.history;
        return { generation: this.historyGeneration, top: h ? h.steps[h.index] : null };
    }

    localCopy() {
        if (!this.exported)
            return "none";
        const now = this.stamp();
        return now.generation === this.exportStamp.generation && now.top === this.exportStamp.top ? "current" : "behind";
    }

    markExported() {
        this.exported = true;
        this.exportStamp = this.stamp();
        this.emit("indicators");
    }

    // Todo lo que cambia la imagen sale en el mismo orden en que se aplicó.
    queueSend(fn) {
        this.sendChain = this.sendChain.then(fn).catch((e) => console.error("collab", e));
        return this.sendChain;
    }

    onGuestChange(m) {
        const p = this.peer(m.from) || { name: "?" };
        if (m.t === "change" && p.mode === "live")
            return this.enqueue({ kind: "apply", ops: m.ops || [], label: String(m.label || "").slice(0, 200), from: m.from, name: p.name });
        this.proposals = this.proposals.filter((x) => x.from !== m.from);
        this.proposals.push({ pid: m.pid || 0, from: m.from, name: p.name, label: String(m.label || "").slice(0, 200), ops: m.ops || [], time: new Date() });
        if (this.autoAccept)
            return this.acceptProposal(m.from);
        this.emit("proposals");
        this.emit("notice", { text: tr("%1 sugiere un cambio.", p.name) });
    }

    acceptProposal(from) {
        const i = this.proposals.findIndex((p) => p.from === from);
        if (i < 0)
            return;
        const [p] = this.proposals.splice(i, 1);
        this.emit("proposals");
        this.enqueue({ kind: "apply", ops: p.ops, label: p.label, from: p.from, name: p.name, pid: p.pid });
    }

    rejectProposal(from) {
        const i = this.proposals.findIndex((p) => p.from === from);
        if (i < 0)
            return;
        const [p] = this.proposals.splice(i, 1);
        this.queueSend(() => this.send({ t: "proposal", to: from, pid: p.pid, state: "rejected" }));
        this.emit("proposals");
    }

    acceptAll() {
        while (this.proposals.length)
            this.acceptProposal(this.proposals[0].from);
    }

    admit(id, ok) {
        this.waiting.delete(id);
        this.send({ t: "admit", peer: id, ok });
        this.emit("peers");
    }

    kick(id) {
        this.send({ t: "kick", peer: id });
    }

    rename(name) {
        this.settings.name = name;
        if (this.isHost)
            this.send({ t: "config", config: this.hostConfig() });
        else
            this.send({ t: "rename", name });
    }

    // Vista previa de una sugerencia: la imagen actual con sus píxeles encima.
    async preview(proposal, maxSide = 96) {
        let area = null;
        const patches = [];
        for (const op of proposal.ops) {
            if (op.op === "pixels") {
                const r = { x: op.x, y: op.y, w: op.w, h: op.h };
                area = unionRect(area, r);
                const data = await decodePng(op.png, r.w, r.h);
                if (data) {
                    const mask = op.mask ? decodeMask(op.mask, r.w * r.h) : null;
                    if (mask)
                        for (let i = 0; i < data.length; ++i)
                            if (!mask[i])
                                data[i] = 0;
                    patches.push({ r, data });
                }
            } else {
                area = { x: 0, y: 0, w: this.canon.w, h: this.canon.h };
            }
        }
        area = clampRect(area || { x: 0, y: 0, w: this.canon.w, h: this.canon.h }, this.canon.w, this.canon.h);
        const margin = Math.max(8, Math.round(Math.max(area.w, area.h) / 8));
        area = clampRect({ x: area.x - margin, y: area.y - margin, w: area.w + 2 * margin, h: area.h + 2 * margin }, this.canon.w, this.canon.h);
        const k = Math.min(1, maxSide / Math.max(area.w, area.h));
        const out = createCanvas(Math.max(1, Math.round(area.w * k)), Math.max(1, Math.round(area.h * k)));
        const ctx = context(out);
        const flat = this.doc.flattened();
        ctx.drawImage(flat, area.x, area.y, area.w, area.h, 0, 0, out.width, out.height);
        for (const { r, data } of patches) {
            const c = createCanvas(r.w, r.h);
            context(c).putImageData(new ImageData(new Uint8ClampedArray(data.buffer), r.w, r.h), 0, 0);
            ctx.drawImage(c, (r.x - area.x) * k, (r.y - area.y) * k, r.w * k, r.h * k);
        }
        return { canvas: out, area };
    }

    // --- Invitado ----------------------------------------------------------------------------------

    async onWelcome(m) {
        const canon = await this.readSnapshot(m.doc || {});
        if (!canon)
            return this.finish(tr("La imagen de la sesión no es válida."));
        this.canon = canon;
        this.mode = m.mode || this.mode;
        this.autoAccept = !!m.autoAccept;
        this.peers.clear();
        for (const p of m.peers || [])
            this.peers.set(p.id, p);
        const doc = new Document(canon.w, canon.h, { name: canon.name || this.sessionName, background: null, historyName: tr("Unirse a la sesión"),
                                                      historyIcon: HISTORY_ICON, dpi: canon.dpi });
        doc.layers = canon.layers.map((cl) => {
            const layer = this.newLayer(cl);
            this.idOf.set(layer, cl.id);
            this.layerById.set(cl.id, layer);
            return layer;
        });
        doc.active = doc.layers.length - 1;
        this.attachDocument(doc);
        this.setState("joined");
        this.emit("peers");
        this.emit("document", { doc });
    }

    newLayer(cl) {
        const layer = new Layer(this.canon.w, this.canon.h, cl.props.name);
        layer.setProps(cl.props);
        writeLayer(layer, { x: 0, y: 0, w: this.canon.w, h: this.canon.h }, cl.data);
        return layer;
    }

    propose() {
        if (!this.doc || this.isHost || this.state !== "joined")
            return false;
        const raw = this.diff();
        if (!raw.length) {
            this.hasDraft = false;
            this.emit("draft");
            this.emit("notice", { text: tr("No hay cambios que sugerir.") });
            return false;
        }
        const steps = [];
        const remote = tr("Cambio hecho por %1", "");
        const h = this.doc.history;
        for (let i = Math.max(1, this.draftStart + 1); i <= h.index; ++i) {
            const name = h.steps[i].name;
            if (name && !name.startsWith(remote) && !steps.includes(name))
                steps.push(name);
        }
        const label = steps.length ? steps.slice(0, 3).join(", ") + (steps.length > 3 ? "…" : "") : tr("Cambios");
        this.pendingPid = ++this.nextPid;
        this.hasDraft = true;
        this.sendOps("propose", { pid: this.pendingPid, label }, raw);
        this.emit("draft");
        this.emit("notice", { text: tr("Cambios enviados al anfitrión.") });
        return true;
    }

    discardDraft() {
        if (!this.doc || this.isHost)
            return;
        this.applying = true;
        const doc = this.doc, active = doc.activeLayer;
        const layers = this.canon.layers.map((cl) => {
            let layer = this.layerById.get(cl.id);
            if (!layer) {
                layer = this.newLayer(cl);
                this.idOf.set(layer, cl.id);
                this.layerById.set(cl.id, layer);
            }
            return { layer, canvas: this.canvasOf(cl), props: { ...cl.props } };
        });
        const sizeChanged = doc.width !== this.canon.w || doc.height !== this.canon.h;
        const index = layers.findIndex((l) => l.layer === active);
        doc.restore({ width: this.canon.w, height: this.canon.h, dpi: this.canon.dpi, colorProfile: doc.colorProfile,
                      active: index >= 0 ? index : layers.length - 1,
                      selection: sizeChanged ? Selection.none(this.canon.w, this.canon.h) : doc.selection, layers });
        this.resetHistory();
        this.dirty.clear();
        this.applying = false;
        this.hasDraft = false;
        this.emit("draft");
    }

    canvasOf(cl) {
        const canvas = createCanvas(this.canon.w, this.canon.h);
        context(canvas).putImageData(new ImageData(new Uint8ClampedArray(cl.data.buffer), this.canon.w, this.canon.h), 0, 0);
        return canvas;
    }

    resetHistory() {
        const doc = this.doc;
        doc.history = new History(tr("Unirse a la sesión"), HISTORY_ICON);
        doc.cleanIndex = 0;
        this.draftStart = 0;
        this.historyGeneration++;
        doc.emit("history");
        doc.emit("modified");
    }

    updateDraft() {
        if (this.isHost || this.isLive || !this.doc)
            return;
        const differs = this.diff({ dryRun: true }).length > 0;
        if (differs !== this.hasDraft) {
            this.hasDraft = differs;
            this.emit("draft");
        }
    }

    // --- Documento -----------------------------------------------------------------------------

    attachDocument(doc) {
        this.doc = doc;
        const on = (type, fn) => {
            doc.addEventListener(type, fn);
            this.listeners.push([type, fn]);
        };
        on("pixels", (e) => {
            if (this.applying)
                return;
            const { layer, rect } = e.detail;
            for (const l of layer ? [layer] : doc.layers)
                this.dirty.set(l, unionRect(this.dirty.get(l) || null, rect));
        });
        on("history", () => {
            this.onLocalStep();
            if (this.isHost)
                this.scheduleHistory();
            else
                this.emit("indicators");
        });
        // Guardar (o volver al punto guardado) cambia la corona.
        on("modified", () => this.isHost && this.scheduleHistory());
        on("size", () => {
            if (!this.applying)
                for (const l of doc.layers)
                    this.dirty.set(l, { x: 0, y: 0, w: doc.width, h: doc.height });
        });
    }

    detachDocument() {
        for (const [type, fn] of this.listeners)
            this.doc?.removeEventListener(type, fn);
        this.listeners = [];
    }

    onLocalStep() {
        if (this.applying || !this.doc || this.state !== "joined")
            return;
        const h = this.doc.history;
        const label = h.steps[h.index]?.name || "";
        if (this.isHost || this.isLive) {
            const raw = this.diff();
            if (!raw.length)
                return;
            this.adopt(raw);
            if (this.isHost) {
                this.sendOps("commit", { from: 0, name: this.settings.name, label }, raw);
            } else {
                this.inFlight++;
                this.sendOps("change", { label }, raw);
            }
        } else {
            clearTimeout(this.draftTimer);
            this.draftTimer = setTimeout(() => this.updateDraft(), 300);
        }
    }

    idFor(layer) {
        let id = this.idOf.get(layer);
        if (id)
            return id;
        do
            id = `${this.idPrefix}${++this.idCounter}`;
        while (this.layerById.has(id));
        this.idOf.set(layer, id);
        this.layerById.set(id, layer);
        return id;
    }

    canonIndex(id) {
        return this.canon.layers.findIndex((l) => l.id === id);
    }

    // La copia canónica pasa a ser el documento (anfitriona y en tiempo real, tras enviar).
    adoptWorking() {
        const doc = this.doc, w = doc.width, h = doc.height;
        this.canon = {
            w, h, dpi: doc.dpi, name: doc.displayName(),
            layers: doc.layers.map((l) => ({ id: this.idFor(l), props: props(l), data: readLayer(l, { x: 0, y: 0, w, h }) })),
        };
        this.layerById = new Map(this.canon.layers.map((cl, i) => [cl.id, doc.layers[i]]));
        this.dirty.clear();
    }

    // Lo que cambió el documento respecto a la copia canónica, sin codificar.
    diff({ dryRun = false } = {}) {
        const doc = this.doc, canon = this.canon, raw = [];
        if (doc.width !== canon.w || doc.height !== canon.h || Math.abs(doc.dpi - canon.dpi) > 1e-9) {
            if (dryRun)
                return [true];
            raw.push({ op: "doc", w: doc.width, h: doc.height, dpi: doc.dpi, name: canon.name,
                       layers: doc.layers.map((l) => ({ id: this.idFor(l), props: props(l), data: readLayer(l, { x: 0, y: 0, w: doc.width, h: doc.height }) })) });
            return raw;
        }
        const order = doc.layers.map((l) => this.idFor(l));
        const canonOrder = canon.layers.map((l) => l.id);
        const structural = order.join("\n") !== canonOrder.join("\n") || doc.layers.some((l, i) => !sameProps(props(l), canon.layers[i].props));
        if (structural) {
            if (dryRun)
                return [true];
            raw.push({ op: "layers",
                       layers: doc.layers.map((l, i) => ({ id: order[i], props: props(l),
                                                           data: this.canonIndex(order[i]) < 0 ? readLayer(l, { x: 0, y: 0, w: canon.w, h: canon.h }) : null })),
                       removed: canonOrder.filter((id) => !order.includes(id)) });
        }
        doc.layers.forEach((layer, i) => {
            const ci = this.canonIndex(order[i]);
            const marked = this.dirty.get(layer);
            const dirty = marked ? clampRect(marked, canon.w, canon.h) : null;
            if (ci < 0 || !dirty || dirty.w <= 0 || dirty.h <= 0)
                return;
            const before = canon.layers[ci].data, after = readLayer(layer, dirty);
            let x0 = dirty.w, x1 = -1, y0 = -1, y1 = -1;
            for (let y = 0; y < dirty.h; ++y) {
                const row = (dirty.y + y) * canon.w + dirty.x;
                for (let x = 0; x < dirty.w; ++x) {
                    if (before[row + x] !== after[y * dirty.w + x]) {
                        if (x < x0) x0 = x;
                        if (x > x1) x1 = x;
                        if (y0 < 0) y0 = y;
                        y1 = y;
                    }
                }
            }
            if (y0 < 0)
                return;
            if (dryRun) {
                raw.push(true);
                return;
            }
            const r = { x: dirty.x + x0, y: dirty.y + y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
            const patch = new Uint32Array(r.w * r.h), mask = new Uint8Array(r.w * r.h);
            let all = true;
            for (let y = 0; y < r.h; ++y) {
                for (let x = 0; x < r.w; ++x) {
                    const a = before[(r.y + y) * canon.w + r.x + x], b = after[(r.y - dirty.y + y) * dirty.w + (r.x - dirty.x + x)];
                    if (a !== b) {
                        patch[y * r.w + x] = b;
                        mask[y * r.w + x] = 1;
                    } else {
                        all = false;
                    }
                }
            }
            raw.push({ op: "pixels", layer: order[i], ...r, data: patch, mask: all ? null : mask });
        });
        return dryRun ? raw.slice(0, 1) : raw;
    }

    // Lo enviado pasa a la copia canónica.
    adopt(raw) {
        const canon = this.canon;
        for (const op of raw) {
            if (op.op === "doc") {
                this.canon = { w: op.w, h: op.h, dpi: op.dpi, name: canon.name, layers: op.layers.map((l) => ({ id: l.id, props: { ...l.props }, data: l.data })) };
            } else if (op.op === "layers") {
                this.canon.layers = op.layers.map((e) => {
                    const old = this.canon.layers.find((l) => l.id === e.id);
                    return { id: e.id, props: { ...e.props }, data: e.data || old.data, png: e.data ? null : old.png };
                });
            } else if (op.op === "pixels") {
                const cl = this.canon.layers.find((l) => l.id === op.layer);
                for (let y = 0; y < op.h; ++y)
                    for (let x = 0; x < op.w; ++x)
                        if (!op.mask || op.mask[y * op.w + x])
                            cl.data[(op.y + y) * this.canon.w + op.x + x] = op.data[y * op.w + x];
                cl.png = null;
            }
        }
        this.dirty.clear();
    }

    async encodeOps(raw) {
        const ops = [];
        for (const op of raw) {
            if (op.op === "pixels") {
                const out = { op: "pixels", layer: op.layer, x: op.x, y: op.y, w: op.w, h: op.h, png: await encodePng(op.data, op.w, op.h) };
                if (op.mask)
                    out.mask = encodeMask(op.mask, op.w * op.h);
                ops.push(out);
            } else if (op.op === "layers") {
                const layers = [];
                for (const e of op.layers) {
                    const out = propsJson(e.id, e.props);
                    if (e.data)
                        out.png = await encodePng(e.data, this.canon.w, this.canon.h);
                    layers.push(out);
                }
                ops.push({ op: "layers", layers, removed: op.removed });
            } else if (op.op === "doc") {
                ops.push({ op: "doc", doc: await this.snapshotJson({ w: op.w, h: op.h, dpi: op.dpi, name: op.name, layers: op.layers }) });
            }
        }
        return ops;
    }

    // Se codifica y se envía en orden (lo que se envíe después espera).
    sendOps(type, fields, raw) {
        return this.queueSend(async () => {
            const ops = await this.encodeOps(raw);
            if (type === "commit")
                this.send({ t: "commit", rev: ++this.rev, ...fields, ops });
            else
                this.send({ t: type, ...fields, ops });
        });
    }

    async snapshotJson(canon) {
        const layers = [];
        for (const cl of canon.layers) {
            if (!cl.png)
                cl.png = await encodePng(cl.data, canon.w, canon.h);
            layers.push({ ...propsJson(cl.id, cl.props), png: cl.png });
        }
        return { rev: this.rev, w: canon.w, h: canon.h, dpi: canon.dpi, name: canon.name || "", layers };
    }

    async readSnapshot(doc) {
        const w = doc.w | 0, h = doc.h | 0;
        if (w < 1 || h < 1 || w > 65535 || h > 65535 || w * h > MAX_PIXELS)
            return null;
        const layers = [];
        for (const o of doc.layers || []) {
            const id = String(o.id || "");
            if (!id || layers.some((l) => l.id === id))
                continue;
            let data = o.png ? await decodePng(o.png, w, h) : null;
            if (!data)
                data = new Uint32Array(w * h);
            layers.push({ id, props: propsFrom(o), data });
        }
        if (!layers.length)
            return null;
        return { w, h, dpi: Math.max(1, Math.min(100000, +doc.dpi || 96)), name: String(doc.name || "").slice(0, 200), layers };
    }

    // --- Lo que llega de los demás ---------------------------------------------------------------

    enqueue(item) {
        // Se decodifica enseguida (sin bloquear) y se aplica en orden cuando se pueda.
        item.ready = this.prepare(item.ops || []);
        this.queue.push(item);
        this.processQueue();
    }

    async prepare(ops) {
        const out = [];
        for (const op of ops) {
            if (!op || typeof op !== "object")
                continue;
            if (op.op === "pixels") {
                const w = op.w | 0, h = op.h | 0;
                if (w < 1 || h < 1 || w * h > MAX_PIXELS)
                    continue;
                const data = await decodePng(op.png, w, h).catch(() => null);
                if (data)
                    out.push({ json: op, data, mask: op.mask ? decodeMask(op.mask, w * h) : null });
            } else if (op.op === "layers") {
                const decoded = new Map();
                for (const e of op.layers || [])
                    if (e && e.png) {
                        const image = await decodePngAny(e.png).catch(() => null);
                        if (image)
                            decoded.set(String(e.id), image);
                    }
                out.push({ json: op, decoded });
            } else if (op.op === "doc") {
                const canon = await this.readSnapshot(op.doc || {});
                if (canon)
                    out.push({ json: op, canon });
            }
        }
        return out;
    }

    busy() {
        return this.busyCheck ? this.busyCheck() : { busy: false, layer: null };
    }

    blocked(prepared) {
        const { busy, layer } = this.busy();
        if (!busy)
            return false;
        return prepared.some((p) => p.json.op !== "pixels" || !layer || this.layerById.get(p.json.layer) === layer);
    }

    async processQueue() {
        if (this.processing)
            return;
        this.processing = true;
        try {
            while (this.queue.length && this.doc && this.state === "joined") {
                const item = this.queue[0];
                const prepared = await item.ready;
                // Mientras una herramienta trabaja en lo que toca, espera.
                while (this.state === "joined" && (item.kind === "accepted" ? this.busy().busy : this.blocked(prepared)))
                    await new Promise((resolve) => setTimeout(resolve, 100));
                if (this.state !== "joined")
                    break;
                this.queue.shift();
                this.handle(item, prepared);
            }
        } finally {
            this.processing = false;
        }
    }

    handle(item, prepared) {
        const text = tr("Cambio hecho por %1", item.name);
        if (item.kind === "apply") {
            const ops = this.apply(prepared, text);
            this.queueSend(() => {
                if (ops.length)
                    this.send({ t: "commit", rev: ++this.rev, from: item.from, name: item.name, label: item.label, ops, ...(item.pid ? { pid: item.pid } : {}) });
                if (item.pid)
                    this.send({ t: "proposal", to: item.from, pid: item.pid, state: "accepted" });
            });
        } else if (item.kind === "commit") {
            // El eco de un cambio propio con otros más nuevos ya enviados: solo la copia canónica.
            const ownEcho = item.from === this.myId;
            if (ownEcho && this.inFlight > 0)
                this.inFlight--;
            this.apply(prepared, text, ownEcho && this.inFlight > 0);
            if (!this.isLive) {
                clearTimeout(this.draftTimer);
                this.draftTimer = setTimeout(() => this.updateDraft(), 300);
            }
        } else if (item.kind === "accepted") {
            if (item.pid === this.pendingPid)
                this.pendingPid = 0;
            // El borrador ya es de todos: su historial temporal se borra.
            this.applying = true;
            this.resetHistory();
            this.applying = false;
            this.updateDraft();
            this.emit("draft");
            this.emit("notice", { text: tr("El anfitrión aceptó tus cambios.") });
        }
    }

    // Aplica las operaciones a la copia canónica y al documento (con un paso en el historial).
    apply(prepared, historyText, canonOnly = false) {
        const doc = this.doc, effective = [], subs = [];
        if (canonOnly) {
            this.applyToCanonOnly(prepared);
            return effective;
        }
        this.applying = true;
        try {
            for (const p of prepared) {
                const op = p.json;
                if (op.op === "pixels")
                    this.applyPixels(p, effective, subs);
                else if (op.op === "layers")
                    this.applyLayers(p, effective, subs);
                else if (op.op === "doc")
                    this.applyDoc(p, effective, subs);
            }
            if (subs.length) {
                const bytes = subs.reduce((n, s) => n + (s.bytes || 0), 0);
                doc.push({ name: historyText, icon: HISTORY_ICON, bytes,
                           undo: () => [...subs].reverse().forEach((s) => s.undo()), redo: () => subs.forEach((s) => s.redo()) });
            }
        } finally {
            this.applying = false;
        }
        return effective;
    }

    // Solo la copia canónica (los píxeles, normalizados como los leería una capa).
    applyToCanonOnly(prepared) {
        for (const p of prepared) {
            const op = p.json, canon = this.canon;
            if (op.op === "pixels") {
                const cl = canon.layers.find((l) => l.id === op.layer);
                const r = { x: op.x | 0, y: op.y | 0, w: op.w | 0, h: op.h | 0 };
                if (!cl || r.x < 0 || r.y < 0 || r.x + r.w > canon.w || r.y + r.h > canon.h)
                    continue;
                const next = region(cl.data, canon.w, r);
                for (let i = 0; i < next.length; ++i)
                    if (!p.mask || p.mask[i])
                        next[i] = p.data[i];
                putRegion(cl.data, canon.w, r, normalize(next, r.w, r.h));
                cl.png = null;
            } else if (op.op === "layers") {
                const removed = new Set((op.removed || []).map(String));
                const listed = [], entries = new Map();
                for (const e of op.layers || []) {
                    const id = String(e.id || "");
                    const image = p.decoded.get(id);
                    if (!id || entries.has(id) || removed.has(id) || (this.canonIndex(id) < 0 && !(image && image.w === canon.w && image.h === canon.h)))
                        continue;
                    listed.push(id);
                    entries.set(id, e);
                }
                const next = mergeOrder(canon.layers.map((l) => l.id), listed, removed).map((id) => {
                    const old = canon.layers.find((l) => l.id === id);
                    if (old)
                        return { ...old, props: entries.has(id) ? propsFrom(entries.get(id), old.props) : old.props };
                    const layer = this.layerById.get(id);
                    const data = layer && layer.canvas.width === canon.w && layer.canvas.height === canon.h
                        ? readLayer(layer, { x: 0, y: 0, w: canon.w, h: canon.h }) : normalize(p.decoded.get(id).data, canon.w, canon.h);
                    return { id, props: propsFrom(entries.get(id)), data };
                });
                if (next.length)
                    canon.layers = next;
            } else if (op.op === "doc") {
                p.canon.name = p.canon.name || canon.name;
                this.canon = p.canon;
            }
        }
    }

    applyPixels(p, effective, subs) {
        const op = p.json, canon = this.canon;
        const cl = canon.layers.find((l) => l.id === op.layer);
        const r = { x: op.x | 0, y: op.y | 0, w: op.w | 0, h: op.h | 0 };
        if (!cl || r.x < 0 || r.y < 0 || r.x + r.w > canon.w || r.y + r.h > canon.h)
            return;
        const old = region(cl.data, canon.w, r);
        const next = old.slice();
        for (let i = 0; i < next.length; ++i)
            if (!p.mask || p.mask[i])
                next[i] = p.data[i];
        const normalized = normalize(next, r.w, r.h);
        putRegion(cl.data, canon.w, r, normalized);
        cl.png = null;
        effective.push(op);
        const layer = this.layerById.get(op.layer);
        if (!layer || !this.doc.layers.includes(layer))
            return;
        const before = readLayer(layer, r), after = before.slice();
        let changed = false;
        for (let i = 0; i < after.length; ++i) {
            if (old[i] !== normalized[i] && after[i] === old[i]) {
                after[i] = normalized[i];
                changed = true;
            }
        }
        if (!changed)
            return;
        const doc = this.doc;
        const put = (data) => {
            writeLayer(layer, r, data);
            doc.pixelsChanged(layer, r);
        };
        put(after);
        subs.push({ bytes: before.length * 8, undo: () => put(before), redo: () => put(after) });
    }

    applyLayers(p, effective, subs) {
        const op = p.json, canon = this.canon, doc = this.doc;
        const fresh = (id) => {
            const image = p.decoded.get(id);
            return image && image.w === canon.w && image.h === canon.h ? image.data : null;
        };
        const removed = new Set((op.removed || []).map(String).filter((id) => this.canonIndex(id) >= 0));
        const listed = [], entries = new Map(), cleanList = [];
        for (const e of op.layers || []) {
            const id = String(e.id || "");
            if (!id || id.length > 64 || entries.has(id) || removed.has(id))
                continue;
            if (this.canonIndex(id) < 0 && !fresh(id))
                continue;
            listed.push(id);
            entries.set(id, e);
            const { png, ...rest } = e;
            cleanList.push(this.canonIndex(id) < 0 ? e : rest);
        }
        // Copia canónica.
        const oldCanon = canon.layers;
        const order = mergeOrder(oldCanon.map((l) => l.id), listed, removed);
        const pendingNew = [];
        const next = order.map((id) => {
            const old = oldCanon.find((l) => l.id === id);
            if (old)
                return { ...old, props: entries.has(id) ? propsFrom(entries.get(id), old.props) : old.props, png: entries.has(id) ? null : old.png };
            const created = { id, props: propsFrom(entries.get(id)), data: null };
            pendingNew.push(created);
            return created;
        });
        // Las nuevas: sus píxeles (se decodifican aquí de forma síncrona desde el PNG ya leído).
        for (const cl of pendingNew)
            cl.data = fresh(cl.id);
        if (!next.length)
            return;
        canon.layers = next;
        effective.push({ op: "layers", layers: cleanList, removed: [...removed] });

        // Documento.
        const before = doc.layerState();
        const current = doc.layers.map((l, i) => this.idOf.get(l) || `~${i}`);
        const wListed = listed.filter((id) => current.includes(id) || !(oldCanon.some((l) => l.id === id) || this.layerById.has(id)));
        const wOrder = mergeOrder(current, wListed, removed);
        const active = doc.activeLayer;
        const layers = [], layerProps = [];
        for (const key of wOrder) {
            const wi = current.indexOf(key);
            if (wi >= 0) {
                const layer = doc.layers[wi];
                let pr = props(layer);
                const o = oldCanon.find((l) => l.id === key), n = next.find((l) => l.id === key);
                if (o && n && sameProps(pr, o.props))
                    pr = { ...n.props };
                layers.push(layer);
                layerProps.push(pr);
            } else {
                const cl = next.find((l) => l.id === key);
                if (!cl)
                    continue;
                const layer = this.newLayer(cl);
                this.idOf.set(layer, key);
                this.layerById.set(key, layer);
                // Lo que se guarda como canónico es lo que se lee de la capa.
                cl.data = readLayer(layer, { x: 0, y: 0, w: canon.w, h: canon.h });
                layers.push(layer);
                layerProps.push({ ...cl.props });
            }
        }
        for (const l of doc.layers)
            if (!layers.includes(l)) {
                const id = this.idOf.get(l);
                if (id)
                    this.layerById.delete(id);
            }
        if (!layers.length)
            return;
        const after = { layers, props: layerProps, active: Math.max(0, layers.indexOf(active)) };
        if (layers.indexOf(active) < 0)
            after.active = Math.min(doc.active, layers.length - 1);
        doc.restoreLayerState(after);
        subs.push({ undo: () => doc.restoreLayerState(before), redo: () => doc.restoreLayerState(after) });
    }

    applyDoc(p, effective, subs) {
        const doc = this.doc, canon = p.canon;
        canon.name = canon.name || this.canon.name;
        const before = doc.snapshot(), active = doc.activeLayer;
        const layers = canon.layers.map((cl) => {
            let layer = this.layerById.get(cl.id);
            if (!layer) {
                layer = new Layer(canon.w, canon.h, cl.props.name);
                this.idOf.set(layer, cl.id);
            }
            const canvas = createCanvas(canon.w, canon.h);
            context(canvas).putImageData(new ImageData(new Uint8ClampedArray(cl.data.buffer), canon.w, canon.h), 0, 0);
            cl.data = new Uint32Array(context(canvas).getImageData(0, 0, canon.w, canon.h).data.buffer);
            return { layer, canvas, props: { ...cl.props } };
        });
        this.layerById = new Map(canon.layers.map((cl, i) => [cl.id, layers[i].layer]));
        this.canon = canon;
        const sizeChanged = doc.width !== canon.w || doc.height !== canon.h;
        const index = layers.findIndex((l) => l.layer === active);
        const after = { width: canon.w, height: canon.h, dpi: canon.dpi, colorProfile: doc.colorProfile,
                        active: index >= 0 ? index : layers.length - 1,
                        selection: sizeChanged ? Selection.none(canon.w, canon.h) : doc.selection, layers };
        doc.restore(after);
        effective.push(p.json);
        subs.push({ bytes: layers.length * canon.w * canon.h * 8, undo: () => doc.restore(before), redo: () => doc.restore(after) });
    }

    // --- Puntero ----------------------------------------------------------------------------------

    setCursor(x, y, inside, tool) {
        if (this.state !== "joined")
            return;
        this.cursorOut = { t: "cursor", x: Math.round(x * 10) / 10, y: Math.round(y * 10) / 10, tool: tool || "", inside: !!inside };
        if (this.cursorTimer) {
            this.cursorDirty = true;
            return;
        }
        this.send(this.cursorOut);
        const tick = () => {
            if (this.cursorDirty) {
                this.cursorDirty = false;
                this.send(this.cursorOut);
                this.cursorTimer = setTimeout(tick, 33);
            } else {
                this.cursorTimer = null;
            }
        };
        this.cursorTimer = setTimeout(tick, 33);
    }
}

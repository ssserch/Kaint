// Colaboración P2P por Internet (docs/collaboration.md, «P2P»), como src/app/collab/P2P.cpp de
// Kaint: cada invitado se conecta directo con el anfitrión por un canal de datos de WebRTC y por él
// habla el mismo protocolo que en la red local. Para encontrarse usan rastreadores de WebTorrent
// (solo intercambian las ofertas y respuestas de WebRTC, firmadas con el código de la sesión).
//
// El navegador también puede hospedar: lleva dentro el centro de la sesión (Hub.js) y la página es
// un participante más, conectado a él sin red.
import { Hub } from "./Hub.js";
import { p2pRoom, signDescription, verifyDescription } from "./Protocol.js";

// Los mensajes largos van en trozos ("+…" sigue, "=…" el último), igual que en Kaint.
const CHUNK = 16 * 1024;
// Lo que se deja en la cola del canal antes de esperar a que se vacíe.
const HIGH_WATER = 4 * 1024 * 1024;
const LOW_WATER = 1024 * 1024;
const PENDING_MS = 40000;
const JOIN_MS = 45000;
const ROUND_MS = 9000;
const OFFERS_PER_ROUND = 2;
const ANNOUNCE_MS = 30000;

export const p2pSupported = () => typeof RTCPeerConnection === "function";

const randomId = () => {
    const alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const bytes = new Uint8Array(20);
    crypto.getRandomValues(bytes);
    return [...bytes].map((b) => alphabet[b % 62]).join("");
};

// "stun:servidor:puerto" o "turn:usuario:contraseña@servidor:puerto" → RTCIceServer.
export function iceServerList(lines) {
    const out = [];
    for (const raw of lines || []) {
        const line = String(raw).trim();
        const m = /^(turns?):([^:@]+):([^@]*)@(.+)$/i.exec(line);
        if (m)
            out.push({ urls: `${m[1]}:${m[4]}`, username: decodeURIComponent(m[2]), credential: decodeURIComponent(m[3]) });
        else if (/^(stun|turns?):/i.test(line))
            out.push({ urls: line });
    }
    return out;
}

// --- Canal de datos (se usa como un WebSocket: readyState, send, close y los eventos) -----------

export class RtcLink extends EventTarget {
    constructor(iceServers) {
        super();
        this.pc = new RTCPeerConnection({ iceServers: iceServerList(iceServers) });
        this.channel = null;
        this.readyState = 0;
        this.partial = "";
        this.queue = [];
        this.address = "p2p";
        this.pc.addEventListener("connectionstatechange", () => {
            if (this.pc.connectionState === "failed" || this.pc.connectionState === "closed")
                this.lost();
        });
        this.pc.addEventListener("datachannel", (e) => this.attach(e.channel));
    }

    attach(channel) {
        this.channel = channel;
        channel.binaryType = "arraybuffer";
        channel.bufferedAmountLowThreshold = LOW_WATER;
        channel.addEventListener("bufferedamountlow", () => this.flush());
        channel.addEventListener("open", () => this.opened());
        channel.addEventListener("close", () => this.lost());
        channel.addEventListener("message", (e) => this.receive(e.data));
        if (channel.readyState === "open")
            this.opened();
    }

    opened() {
        if (this.readyState !== 0)
            return;
        this.readyState = 1;
        this.dispatchEvent(new Event("open"));
    }

    lost() {
        if (this.readyState >= 2)
            return;
        const wasOpen = this.readyState === 1;
        this.readyState = 3;
        try {
            this.channel?.close();
            this.pc.close();
        } catch {
            // ignore
        }
        this.dispatchEvent(new Event(wasOpen ? "close" : "error"));
        if (wasOpen)
            return;
        this.dispatchEvent(new Event("close"));
    }

    // Espera a reunir los candidatos (o 5 s, si un STUN no responde) y da la descripción local.
    async localDescription() {
        if (this.pc.iceGatheringState !== "complete")
            await new Promise((resolve) => {
                const done = () => {
                    if (this.pc.iceGatheringState === "complete") {
                        this.pc.removeEventListener("icegatheringstatechange", done);
                        resolve();
                    }
                };
                this.pc.addEventListener("icegatheringstatechange", done);
                setTimeout(resolve, 5000);
            });
        return this.pc.localDescription?.sdp || "";
    }

    async createOffer() {
        this.attach(this.pc.createDataChannel("kaint"));
        await this.pc.setLocalDescription(await this.pc.createOffer());
        return this.localDescription();
    }

    async acceptOffer(sdp) {
        await this.pc.setRemoteDescription({ type: "offer", sdp });
        await this.pc.setLocalDescription(await this.pc.createAnswer());
        return this.localDescription();
    }

    async acceptAnswer(sdp) {
        if (this.answered)
            return false;
        this.answered = true;
        await this.pc.setRemoteDescription({ type: "answer", sdp });
        return true;
    }

    send(text) {
        if (this.readyState !== 1)
            return;
        if (text.length <= CHUNK) {
            this.queue.push(text);
        } else {
            for (let pos = 0; pos < text.length;) {
                let end = Math.min(pos + CHUNK, text.length);
                // Sin partir un par sustituto (un carácter fuera del plano básico).
                if (end < text.length && /[\ud800-\udbff]/.test(text[end - 1]))
                    --end;
                this.queue.push((end >= text.length ? "=" : "+") + text.slice(pos, end));
                pos = end;
            }
        }
        this.flush();
    }

    flush() {
        const channel = this.channel;
        if (!channel || channel.readyState !== "open")
            return;
        try {
            while (this.queue.length && channel.bufferedAmount < HIGH_WATER)
                channel.send(this.queue.shift());
        } catch {
            this.lost();
        }
    }

    receive(data) {
        if (typeof data !== "string" || !data)
            return;
        const kind = data[0];
        if (kind === "+" || kind === "=") {
            this.partial += data.slice(1);
            if (kind === "+")
                return;
            data = this.partial;
            this.partial = "";
        }
        this.dispatchEvent(new MessageEvent("message", { data }));
    }

    close() {
        if (this.readyState >= 2)
            return;
        // Lo que queda en la cola sale antes de cerrar (el aviso de que se va, por ejemplo).
        this.readyState = 2;
        const finish = () => {
            this.readyState = 1;
            this.lost();
        };
        if (!this.queue.length && !(this.channel?.bufferedAmount > 0))
            return setTimeout(finish, 200);
        const started = Date.now();
        const poll = setInterval(() => {
            this.flush();
            if ((!this.queue.length && !(this.channel?.bufferedAmount > 0)) || Date.now() - started > 3000) {
                clearInterval(poll);
                finish();
            }
        }, 50);
    }
}

// Para Hub.js: send(objeto), close(), onmessage(texto), onclose().
function hubLink(rtc) {
    const link = { address: rtc.address, maxMessage: 0, onmessage: null, onclose: null,
                   send: (object) => rtc.send(JSON.stringify(object)), close: () => rtc.close() };
    rtc.addEventListener("message", (e) => link.onmessage?.(e.data));
    rtc.addEventListener("close", () => link.onclose?.());
    return link;
}

// La página anfitriona y el Hub, sin red: un par de extremos (el de la página se usa como un
// WebSocket).
function localPair() {
    const page = new EventTarget();
    const hub = { address: "local", maxMessage: 0, onmessage: null, onclose: null };
    let open = true;
    page.readyState = 1;
    page.send = (text) => open && setTimeout(() => hub.onmessage?.(text), 0);
    hub.send = (object) => {
        const data = JSON.stringify(object);
        if (open)
            setTimeout(() => page.dispatchEvent(new MessageEvent("message", { data })), 0);
    };
    const close = () => {
        if (!open)
            return;
        open = false;
        page.readyState = 3;
        setTimeout(() => {
            hub.onclose?.();
            page.dispatchEvent(new Event("close"));
        }, 0);
    };
    page.close = close;
    hub.close = close;
    return { page, hub };
}

// --- Señalización: el protocolo de los rastreadores de WebTorrent ------------------------------

export class Signaling extends EventTarget {
    constructor(servers, room) {
        super();
        this.room = room;
        this.peerId = randomId();
        this.servers = (servers || []).map((url) => String(url).trim()).filter(Boolean)
            .map((url) => ({ url, socket: null, ready: false, failures: 0, error: "" }));
        this.running = false;
    }

    get readyCount() {
        return this.servers.filter((s) => s.ready).length;
    }

    emit(type, detail = {}) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }

    start() {
        this.running = true;
        if (!this.servers.length)
            return setTimeout(() => this.emit("unreachable", { reason: "noservers" }), 0);
        this.servers.forEach((_, i) => this.connect(i));
    }

    stop() {
        if (!this.running)
            return;
        this.running = false;
        this.servers.forEach((s, i) => {
            if (s.ready)
                this.send(i, { action: "announce", event: "stopped", info_hash: this.room, peer_id: this.peerId, numwant: 0, uploaded: 0, downloaded: 0 });
            try {
                s.socket?.close();
            } catch {
                // ignore
            }
            s.socket = null;
            s.ready = false;
        });
    }

    connect(i) {
        if (!this.running)
            return;
        const s = this.servers[i];
        let socket;
        try {
            socket = new WebSocket(s.url);
        } catch (e) {
            s.failures = 99;
            s.error = e.message;
            return this.checkUnreachable();
        }
        s.socket = socket;
        socket.addEventListener("open", () => {
            s.ready = true;
            s.failures = 0;
            this.emit("ready", { server: i });
        });
        socket.addEventListener("message", (e) => this.onText(i, e.data));
        socket.addEventListener("close", () => {
            if (s.socket !== socket)
                return;
            s.socket = null;
            s.ready = false;
            s.failures++;
            this.emit("changed");
            this.checkUnreachable();
            if (this.running)
                setTimeout(() => this.running && !s.socket && this.connect(i), Math.min(30000, 2000 * s.failures));
        });
    }

    checkUnreachable() {
        if (this.servers.every((s) => !s.ready && s.failures > 0))
            this.emit("unreachable", { reason: "unreachable" });
    }

    send(i, message) {
        const socket = this.servers[i]?.socket;
        if (socket?.readyState === WebSocket.OPEN)
            socket.send(JSON.stringify(message));
    }

    announce(offers, numwant) {
        this.servers.forEach((s, i) => {
            if (s.ready)
                this.send(i, { action: "announce", info_hash: this.room, peer_id: this.peerId, numwant, uploaded: 0, downloaded: 0, offers });
        });
    }

    sendAnswer(i, toPeer, offerId, answer) {
        this.send(i, { action: "announce", info_hash: this.room, peer_id: this.peerId, to_peer_id: toPeer, offer_id: offerId, answer });
    }

    onText(i, text) {
        let m;
        try {
            m = JSON.parse(text);
        } catch {
            return;
        }
        if (!m || m.info_hash !== this.room || !m.peer_id || m.peer_id === this.peerId || !m.offer_id)
            return;
        if (m.offer && typeof m.offer === "object")
            this.emit("offer", { server: i, peer: m.peer_id, offerId: m.offer_id, offer: m.offer });
        else if (m.answer && typeof m.answer === "object")
            this.emit("answer", { server: i, peer: m.peer_id, offerId: m.offer_id, answer: m.answer });
    }
}

// --- Anfitrión ---------------------------------------------------------------------------------

export class P2PHost extends EventTarget {
    constructor(settings, code) {
        super();
        this.settings = settings;
        this.code = code;
        this.hub = new Hub({ trustHost: (link) => link.address === "local", server: "web", app: "Kaint web" });
        this.signaling = new Signaling(settings.signalServers, p2pRoom(code));
        this.pending = new Map();
        this.timer = null;
        this.signaling.addEventListener("ready", () => {
            this.signaling.announce([], 0);
            this.emit("changed");
        });
        this.signaling.addEventListener("changed", () => this.emit("changed"));
        this.signaling.addEventListener("unreachable", () => this.emit("changed"));
        this.signaling.addEventListener("offer", (e) => this.onOffer(e.detail));
    }

    emit(type) {
        this.dispatchEvent(new Event(type));
    }

    get signalingReady() {
        return this.signaling.readyCount;
    }

    get signalingTotal() {
        return this.signaling.servers.length;
    }

    // El extremo que usa la página anfitriona (como un WebSocket).
    start() {
        const { page, hub } = localPair();
        this.hub.connect(hub);
        this.signaling.start();
        this.timer = setInterval(() => {
            this.signaling.announce([], 0);
            this.emit("changed");
        }, ANNOUNCE_MS);
        return page;
    }

    stop() {
        clearInterval(this.timer);
        this.signaling.stop();
        for (const link of this.pending.values())
            link.close();
        this.pending.clear();
    }

    async onOffer({ server, peer, offerId, offer }) {
        if (this.pending.has(offerId) || this.pending.size >= 32)
            return;
        const sdp = verifyDescription(this.code, "offer", offer.sdp);
        if (!sdp)
            return;
        const link = new RtcLink(this.settings.iceServers);
        this.pending.set(offerId, link);
        const timeout = setTimeout(() => {
            if (this.pending.get(offerId) === link) {
                this.pending.delete(offerId);
                link.close();
            }
        }, PENDING_MS);
        link.addEventListener("open", () => {
            clearTimeout(timeout);
            this.pending.delete(offerId);
            this.hub.connect(hubLink(link));
        });
        link.addEventListener("error", () => this.pending.delete(offerId));
        try {
            const answer = await link.acceptOffer(sdp);
            this.signaling.sendAnswer(server, peer, offerId, { type: "answer", sdp: signDescription(this.code, "answer", answer) });
        } catch {
            this.pending.delete(offerId);
            link.close();
        }
    }
}

// --- Invitado ----------------------------------------------------------------------------------

export class P2PGuest extends EventTarget {
    constructor(settings, code) {
        super();
        this.settings = settings;
        this.code = code;
        this.signaling = new Signaling(settings.signalServers, p2pRoom(code));
        this.offers = new Map();
        this.ready = [];
        this.done = false;
    }

    emit(type, detail = {}) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }

    // Se resuelve con el canal abierto con el anfitrión.
    connect() {
        return new Promise((resolve, reject) => {
            this.resolve = resolve;
            this.reject = reject;
            this.signaling.addEventListener("ready", () => {
                if (this.ready.length)
                    this.signaling.announce(this.ready, this.ready.length);
            });
            this.signaling.addEventListener("unreachable", (e) => this.finish(null, e.detail.reason));
            this.signaling.addEventListener("answer", (e) => this.onAnswer(e.detail));
            this.signaling.start();
            this.rounds = setInterval(() => this.round(), ROUND_MS);
            this.timeout = setTimeout(() => this.finish(null, "nohost"), JOIN_MS);
            this.round();
        });
    }

    round() {
        if (this.done)
            return;
        // Las más viejas que nadie contestó se descartan.
        while (this.offers.size > 6) {
            const [id, link] = this.offers.entries().next().value;
            this.offers.delete(id);
            link.close();
        }
        this.ready = [];
        for (let i = 0; i < OFFERS_PER_ROUND; ++i) {
            const link = new RtcLink(this.settings.iceServers);
            const offerId = randomId();
            this.offers.set(offerId, link);
            link.addEventListener("open", () => this.finish(link));
            link.addEventListener("error", () => this.offers.delete(offerId));
            link.createOffer().then((sdp) => {
                if (this.done)
                    return;
                const offer = { offer_id: offerId, offer: { type: "offer", sdp: signDescription(this.code, "offer", sdp) } };
                this.ready.push(offer);
                this.signaling.announce([offer], 1);
            }).catch(() => this.offers.delete(offerId));
        }
    }

    async onAnswer({ offerId, answer }) {
        const link = this.offers.get(offerId);
        if (!link || this.done)
            return;
        const sdp = verifyDescription(this.code, "answer", answer.sdp);
        if (!sdp)
            return;
        try {
            if (await link.acceptAnswer(sdp))
                this.emit("progress", { text: "connecting" });
        } catch {
            // Otra respuesta servirá.
        }
    }

    stop() {
        clearInterval(this.rounds);
        clearTimeout(this.timeout);
        this.signaling.stop();
        for (const link of this.offers.values())
            link.close();
        this.offers.clear();
    }

    finish(link, reason = "") {
        if (this.done)
            return;
        this.done = true;
        if (link)
            for (const [id, l] of this.offers)
                if (l === link)
                    this.offers.delete(id);
        this.stop();
        if (link)
            this.resolve(link);
        else
            this.reject(new Error(reason));
    }
}

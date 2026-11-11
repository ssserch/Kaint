// El centro de una sesión que hospeda Kaint para la web: recibe a los invitados (con la contraseña,
// su ID o el permiso del anfitrión), lleva la lista de participantes y reparte los mensajes. Lo
// usan el servidor de tools/collab-server.mjs (Node, por WebSocket) y el navegador que hospeda por
// P2P (por canales de WebRTC). El anfitrión es un participante más, con el papel "host".
//
// Cada conexión ("link") tiene: send(objeto), close(), address, maxMessage, onmessage(texto) y
// onclose().
import { VERSION, bytesToBase64, cleanName, deviceTag, normalizedCode, peerColor, proof, randomNonce } from "./Protocol.js";

const MAX_MESSAGE = 512 * 1024 * 1024;

export class Hub {
    // trustHost(link): si puede hospedar sin la clave (el mismo equipo); hostKey: la clave para los
    // demás; log(texto); server: lo que se dice en el saludo ("relay" o "web").
    constructor({ trustHost = () => false, hostKey = "", log = () => {}, server = "relay", app = "Kaint relay", port = 0 } = {}) {
        this.trustHost = trustHost;
        this.hostKey = hostKey;
        this.log = log;
        this.server = server;
        this.app = app;
        this.port = port;
        this.clients = new Map(); // link → { id, nonce, peer, joined, waiting, isHost }
        this.host = null;
        this.config = {};
        this.nextId = 1;
    }

    sessionName() {
        return (this.config.session || "").trim() || `Sesión de ${this.host?.peer.name || "Kaint"}`;
    }

    authName() {
        return this.host ? this.config.access || "approve" : "approve";
    }

    guests() {
        return [...this.clients.values()].filter((c) => c.joined && !c.isHost);
    }

    peers() {
        return [...(this.host ? [this.host.peer] : []), ...this.guests().map((c) => c.peer).sort((a, b) => a.id - b.id)];
    }

    modeFor(peer) {
        return peer.platform === "web" && this.config.realtime !== false ? "live" : "confirm";
    }

    info() {
        return { "kaint-collab": VERSION, server: this.server, hosted: !!this.host, session: this.host ? this.sessionName() : "",
                 host: this.host?.peer.name || "", auth: this.authName(), port: this.port, peers: this.peers().length };
    }

    colorFor(wanted, self = null) {
        const used = this.peers().filter((p) => p !== self && p.color).map((p) => p.color.toLowerCase());
        const hexColor = /^#[0-9a-f]{6}$/i.test(wanted || "") ? wanted.toLowerCase() : null;
        const near = (a, b) => {
            const d = [1, 3, 5].map((i) => parseInt(a.slice(i, i + 2), 16) - parseInt(b.slice(i, i + 2), 16));
            return Math.hypot(...d) < 60;
        };
        if (hexColor && !used.some((u) => near(u, hexColor)))
            return hexColor;
        for (let i = 0; i < 10; ++i)
            if (!used.some((u) => near(u, peerColor(i))))
                return peerColor(i);
        return peerColor(used.length);
    }

    toHost(message) {
        this.host?.link.send(message);
    }

    broadcast(message, except = null) {
        for (const c of this.guests())
            if (c !== except)
                c.link.send(message);
    }

    deny(client, reason) {
        client.link.send({ t: "denied", reason });
        this.clients.delete(client.link);
        client.link.close();
    }

    accept(client) {
        client.peer.color = this.colorFor(client.peer.color, client.peer);
        client.waiting = false;
        client.joined = true;
        client.link.maxMessage = MAX_MESSAGE;
        client.peer.mode = this.modeFor(client.peer);
        client.link.send({ t: "joined", you: client.peer.id, color: client.peer.color, mode: client.peer.mode });
        this.broadcast({ t: "peer", op: "add", peer: client.peer }, client);
        this.toHost({ t: "peer", op: "add", peer: client.peer });
        this.log(`${client.peer.name} se unió (${client.peer.app || client.peer.platform}).`);
    }

    onJoin(client, m) {
        if (m.v !== VERSION)
            return this.deny(client, "version");
        if (m.role === "host") {
            if (this.host)
                return this.deny(client, "busy");
            if (!this.trustHost(client.link) && m.proof !== proof(normalizedCode(this.hostKey), client.nonce))
                return this.deny(client, "hostkey");
            client.isHost = true;
            client.joined = true;
            client.link.maxMessage = MAX_MESSAGE;
            client.peer = { id: 0, name: cleanName(m.name, 0), color: /^#[0-9a-f]{6}$/i.test(m.color || "") ? m.color : peerColor(0),
                            platform: "web", app: String(m.app || "").slice(0, 60), host: true, mode: "" };
            this.config = m.config || {};
            this.host = client;
            client.link.send({ t: "joined", you: 0, color: client.peer.color, mode: "host", peers: this.peers() });
            this.log(`Hospeda ${client.peer.name}: «${this.sessionName()}» (${this.authName()}).`);
            return;
        }
        if (!this.host)
            return this.deny(client, "nohost");
        const peer = client.peer = {
            id: this.nextId++, name: cleanName(m.name, 0), color: m.color, platform: m.platform === "web" ? "web" : "desktop",
            app: String(m.app || "").slice(0, 60), host: false, mode: "", history: false,
        };
        peer.name = cleanName(m.name, peer.id);
        const device = String(m.device || "").slice(0, 64);
        const listed = !!m.proof && (this.config.allowedIds || []).some((line) => {
            const code = normalizedCode(String(line).trim().split(" ")[0]);
            return code.length === 12 && deviceTag(code) === device && proof(code, client.nonce) === m.proof;
        });
        switch (this.authName()) {
        case "free":
            return this.accept(client);
        case "password":
            if (this.config.password && proof(this.config.password, client.nonce) === m.proof)
                return this.accept(client);
            // Un segundo de espera ante cada contraseña errónea: probar muchas lleva tiempo.
            client.waiting = true;
            return setTimeout(() => this.clients.has(client.link) && this.deny(client, "password"), 1000);
        case "ids":
            return listed ? this.accept(client) : this.deny(client, "device");
        default:
            if (listed)
                return this.accept(client);
            client.waiting = true;
            peer.color = this.colorFor(peer.color, peer);
            client.link.send({ t: "waiting" });
            this.toHost({ t: "request", peer });
        }
    }

    onHostMessage(m) {
        const target = (id) => [...this.clients.values()].find((c) => c.peer?.id === id && !c.isHost);
        switch (m.t) {
        case "admit": {
            const c = target(m.peer);
            if (c?.waiting)
                m.ok ? this.accept(c) : this.deny(c, "rejected");
            return;
        }
        case "kick": {
            const c = target(m.peer);
            if (c) {
                c.link.send({ t: "end", reason: "kicked" });
                c.link.close();
            }
            return;
        }
        case "config": {
            const before = { ...this.config };
            this.config = m.config || {};
            if (m.config?.name) {
                this.host.peer.name = cleanName(m.config.name, 0);
                const update = { t: "peer", op: "update", peer: this.host.peer };
                this.broadcast(update);
                this.toHost(update);
            }
            for (const c of this.guests()) {
                const mode = this.modeFor(c.peer);
                if (mode !== c.peer.mode || before.autoAccept !== this.config.autoAccept) {
                    const changed = mode !== c.peer.mode;
                    c.peer.mode = mode;
                    c.link.send({ t: "settings", mode, autoAccept: !!this.config.autoAccept });
                    if (changed) {
                        this.broadcast({ t: "peer", op: "update", peer: c.peer });
                        this.toHost({ t: "peer", op: "update", peer: c.peer });
                    }
                }
            }
            return;
        }
        case "permit": {
            // Quién más puede volver a versiones anteriores de la imagen.
            const c = target(m.peer);
            if (c?.joined) {
                c.peer.history = !!m.history;
                const update = { t: "peer", op: "update", peer: c.peer };
                this.broadcast(update);
                this.toHost(update);
            }
            return;
        }
        case "end":
            return this.end("closed");
        case "cursor":
            return this.broadcast({ t: "cursor", from: 0, x: +m.x || 0, y: +m.y || 0, tool: String(m.tool || "").slice(0, 32), inside: !!m.inside });
        default:
            if (m.to !== undefined) {
                const c = target(m.to);
                const { to, ...rest } = m;
                if (c?.joined)
                    c.link.send(rest);
            } else {
                this.broadcast(m);
            }
        }
    }

    onGuestMessage(client, m) {
        const id = client.peer.id;
        switch (m.t) {
        case "change":
        case "propose":
            return this.toHost({ ...m, from: id });
        case "cursor": {
            const out = { t: "cursor", from: id, x: +m.x || 0, y: +m.y || 0, tool: String(m.tool || "").slice(0, 32), inside: !!m.inside };
            this.broadcast(out, client);
            return this.toHost(out);
        }
        case "rename": {
            client.peer.name = cleanName(m.name, id);
            const update = { t: "peer", op: "update", peer: client.peer };
            this.broadcast(update);
            return this.toHost(update);
        }
        case "revert":
            // Solo con el permiso del anfitrión.
            if (client.peer.history && Number.isInteger(m.index))
                this.toHost({ t: "revert", from: id, index: m.index });
            return;
        case "leave":
            return client.link.close();
        }
    }

    // Termina la sesión para todos.
    end(reason) {
        // Cerrar una conexión avisa enseguida (onclose): se quita antes de la lista.
        const ending = this.host;
        this.host = null;
        this.config = {};
        for (const c of [...this.clients.values()]) {
            if (c === ending)
                continue;
            this.clients.delete(c.link);
            c.link.send({ t: "end", reason });
            c.link.close();
        }
        if (ending) {
            this.log(`Terminó la sesión de ${ending.peer.name}.`);
            this.clients.delete(ending.link);
            ending.link.close();
        }
        this.onEnded?.();
    }

    connect(link) {
        if (this.clients.size >= 64)
            return link.close();
        const client = { link, nonce: randomNonce(), peer: null, joined: false, waiting: false, isHost: false };
        this.clients.set(link, client);
        link.onmessage = (text) => {
            let m;
            try {
                m = JSON.parse(text);
            } catch {
                return;
            }
            if (!m || typeof m !== "object")
                return;
            try {
                if (!client.joined) {
                    if (m.t === "join" && !client.waiting)
                        this.onJoin(client, m);
                    return;
                }
                if (client.isHost)
                    this.onHostMessage(m);
                else
                    this.onGuestMessage(client, m);
            } catch (e) {
                this.log(`Error: ${e.stack || e}`);
            }
        };
        link.onclose = () => {
            if (!this.clients.has(link))
                return;
            this.clients.delete(link);
            if (client.isHost) {
                this.end("closed");
            } else if (client.joined) {
                this.broadcast({ t: "peer", op: "remove", peer: client.peer });
                this.toHost({ t: "peer", op: "remove", peer: client.peer });
                this.log(`${client.peer.name} salió.`);
            } else if (client.waiting && client.peer) {
                this.toHost({ t: "peer", op: "remove", peer: client.peer });
            }
        };
        link.send({ t: "hello", v: VERSION, session: this.host ? this.sessionName() : "", host: this.host?.peer.name || "",
                    auth: this.authName(), nonce: bytesToBase64(client.nonce), server: this.server, app: this.app });
    }
}

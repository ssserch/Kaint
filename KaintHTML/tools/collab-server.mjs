#!/usr/bin/env node
// Servidor del modo colaborativo para Kaint para la web (docs/collaboration.md).
//
// Un navegador no puede abrir un servidor: para hospedar una sesión desde Kaint para la web se
// ejecuta esto en el equipo del anfitrión (sin dependencias, Node 18 o posterior):
//
//     node tools/collab-server.mjs [--port 47913] [--no-announce]
//
// y se abre http://localhost:47913/ en ese equipo: la página se une como anfitriona. Los demás
// abren http://<ip-del-anfitrión>:47913/ o, desde Kaint (escritorio), Archivo > Colaboración >
// Unirse (también encuentra la sesión en la red local). Además sirve los archivos de Kaint para la
// web, porque una página cargada con https no puede conectarse a ws:// de la red local.
import { createServer } from "node:http";
import { createHash, randomBytes } from "node:crypto";
import { createSocket } from "node:dgram";
import { readFile, stat } from "node:fs/promises";
import { networkInterfaces } from "node:os";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import {
    BEACON_PORT, DEFAULT_PORT, VERSION, cleanName, deviceTag, newDeviceCode, normalizedCode, peerColor, proof,
} from "../js/collab/Protocol.js";

const args = process.argv.slice(2);
const option = (name, fallback) => {
    const i = args.indexOf(name);
    return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
};
const PORT = Number(option("--port", DEFAULT_PORT));
const ANNOUNCE = !args.includes("--no-announce");
const ROOT = resolve(fileURLToPath(new URL("..", import.meta.url)));
const MAX_MESSAGE = 512 * 1024 * 1024;
const MAX_HELLO = 64 * 1024;
// Para hospedar desde otro equipo (el de este, por localhost, no la necesita).
const HOST_KEY = option("--host-key", newDeviceCode());

// --- WebSocket (RFC 6455) ----------------------------------------------------------------------

class Socket {
    constructor(socket, head) {
        this.socket = socket;
        this.buffer = head?.length ? Buffer.from(head) : Buffer.alloc(0);
        this.fragments = [];
        this.open = true;
        this.maxMessage = MAX_HELLO;
        this.onmessage = null;
        this.onclose = null;
        socket.setNoDelay(true);
        socket.on("data", (data) => {
            this.buffer = Buffer.concat([this.buffer, data]);
            this.read();
        });
        socket.on("close", () => this.closed());
        socket.on("error", () => this.closed());
        if (this.buffer.length)
            setImmediate(() => this.read());
    }

    get address() {
        return this.socket.remoteAddress || "";
    }

    closed() {
        if (!this.open)
            return;
        this.open = false;
        this.onclose?.();
    }

    read() {
        while (this.buffer.length >= 2) {
            const b0 = this.buffer[0], b1 = this.buffer[1];
            const fin = !!(b0 & 0x80), opcode = b0 & 0x0f, masked = !!(b1 & 0x80);
            let length = b1 & 0x7f, pos = 2;
            if (length === 126) {
                if (this.buffer.length < 4)
                    return;
                length = this.buffer.readUInt16BE(2);
                pos = 4;
            } else if (length === 127) {
                if (this.buffer.length < 10)
                    return;
                length = Number(this.buffer.readBigUInt64BE(2));
                pos = 10;
            }
            if (!masked || length > this.maxMessage) {
                this.socket.destroy();
                return;
            }
            if (this.buffer.length < pos + 4 + length)
                return;
            const mask = this.buffer.subarray(pos, pos + 4);
            const payload = Buffer.from(this.buffer.subarray(pos + 4, pos + 4 + length));
            for (let i = 0; i < payload.length; ++i)
                payload[i] ^= mask[i & 3];
            this.buffer = this.buffer.subarray(pos + 4 + length);
            if (opcode === 0x8) {
                this.frame(0x8, payload.subarray(0, 2));
                this.socket.end();
                this.closed();
                return;
            }
            if (opcode === 0x9) {
                this.frame(0xa, payload);
                continue;
            }
            if (opcode === 0x0 || opcode === 0x1 || opcode === 0x2) {
                this.fragments.push(payload);
                if (this.fragments.reduce((n, f) => n + f.length, 0) > this.maxMessage) {
                    this.socket.destroy();
                    return;
                }
                if (fin) {
                    const message = Buffer.concat(this.fragments).toString("utf8");
                    this.fragments = [];
                    this.onmessage?.(message);
                }
            }
        }
    }

    frame(opcode, payload) {
        if (this.socket.destroyed)
            return;
        const n = payload.length;
        const header = n < 126 ? Buffer.from([0x80 | opcode, n]) : n < 65536 ? Buffer.alloc(4) : Buffer.alloc(10);
        if (n >= 126 && n < 65536) {
            header[0] = 0x80 | opcode;
            header[1] = 126;
            header.writeUInt16BE(n, 2);
        } else if (n >= 65536) {
            header[0] = 0x80 | opcode;
            header[1] = 127;
            header.writeBigUInt64BE(BigInt(n), 2);
        }
        this.socket.write(Buffer.concat([header, payload]));
    }

    send(object) {
        if (this.open)
            this.frame(0x1, Buffer.from(JSON.stringify(object), "utf8"));
    }

    close() {
        if (!this.open)
            return;
        this.frame(0x8, Buffer.from([0x03, 0xe8]));
        this.socket.end();
        setTimeout(() => this.socket.destroy(), 1000).unref();
        this.closed();
    }
}

// --- Sesión ------------------------------------------------------------------------------------

const clients = new Map(); // Socket → { id, nonce, peer, joined, waiting, isHost }
let host = null;
let config = {};
let nextId = 1;

const isLoopback = (address) => address === "127.0.0.1" || address === "::1" || address === "::ffff:127.0.0.1";
const sessionName = () => (config.session || "").trim() || `Sesión de ${host?.peer.name || "Kaint"}`;
const authName = () => (host ? config.access || "approve" : "approve");
const guests = () => [...clients.values()].filter((c) => c.joined && !c.isHost);
const peers = () => [...(host ? [host.peer] : []), ...guests().map((c) => c.peer).sort((a, b) => a.id - b.id)];
const modeFor = (peer) => (peer.platform === "web" && config.realtime !== false ? "live" : "confirm");

function info() {
    return { "kaint-collab": VERSION, server: "relay", hosted: !!host, session: host ? sessionName() : "", host: host?.peer.name || "",
             auth: authName(), port: PORT };
}

function colorFor(wanted, self = null) {
    const used = peers().filter((p) => p !== self && p.color).map((p) => p.color.toLowerCase());
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

function toHost(message) {
    host?.socket.send(message);
}

function broadcast(message, except = null) {
    for (const c of guests())
        if (c !== except)
            c.socket.send(message);
}

function peerMessage(op, peer) {
    return { t: "peer", op, peer };
}

function deny(client, reason) {
    client.socket.send({ t: "denied", reason });
    clients.delete(client.socket);
    client.socket.close();
}

function accept(client) {
    client.peer.color = colorFor(client.peer.color, client.peer);
    client.waiting = false;
    client.joined = true;
    client.socket.maxMessage = MAX_MESSAGE;
    client.peer.mode = modeFor(client.peer);
    client.socket.send({ t: "joined", you: client.peer.id, color: client.peer.color, mode: client.peer.mode });
    broadcast(peerMessage("add", client.peer), client);
    toHost(peerMessage("add", client.peer));
    log(`${client.peer.name} se unió (${client.peer.app || client.peer.platform}).`);
}

function onJoin(client, m) {
    if (m.v !== VERSION)
        return deny(client, "version");
    const address = client.socket.address;
    if (m.role === "host") {
        if (host)
            return deny(client, "busy");
        if (!isLoopback(address) && m.proof !== proof(normalizedCode(HOST_KEY), client.nonce))
            return deny(client, "hostkey");
        client.isHost = true;
        client.joined = true;
        client.socket.maxMessage = MAX_MESSAGE;
        client.peer = { id: 0, name: cleanName(m.name, 0), color: /^#[0-9a-f]{6}$/i.test(m.color || "") ? m.color : peerColor(0),
                        platform: "web", app: String(m.app || "").slice(0, 60), host: true, mode: "" };
        config = m.config || {};
        host = client;
        client.socket.send({ t: "joined", you: 0, color: client.peer.color, mode: "host", peers: peers() });
        log(`Hospeda ${client.peer.name}: «${sessionName()}» (${authName()}).`);
        return;
    }
    if (!host)
        return deny(client, "nohost");
    const peer = client.peer = {
        id: nextId++, name: cleanName(m.name, 0), color: m.color, platform: m.platform === "web" ? "web" : "desktop",
        app: String(m.app || "").slice(0, 60), host: false, mode: "",
    };
    peer.name = cleanName(m.name, peer.id);
    const device = String(m.device || "").slice(0, 64);
    const listed = !!m.proof && (config.allowedIds || []).some((line) => {
        const code = normalizedCode(String(line).trim().split(" ")[0]);
        return code.length === 12 && deviceTag(code) === device && proof(code, client.nonce) === m.proof;
    });
    switch (authName()) {
    case "free":
        return accept(client);
    case "password":
        if (config.password && proof(config.password, client.nonce) === m.proof)
            return accept(client);
        client.waiting = true;
        return setTimeout(() => clients.has(client.socket) && deny(client, "password"), 1000);
    case "ids":
        return listed ? accept(client) : deny(client, "device");
    default:
        if (listed)
            return accept(client);
        client.waiting = true;
        peer.color = colorFor(peer.color, peer);
        client.socket.send({ t: "waiting" });
        toHost({ t: "request", peer });
    }
}

function onHostMessage(m) {
    const target = (id) => [...clients.values()].find((c) => c.peer?.id === id && !c.isHost);
    switch (m.t) {
    case "admit": {
        const c = target(m.peer);
        if (c?.waiting)
            m.ok ? accept(c) : deny(c, "rejected");
        return;
    }
    case "kick": {
        const c = target(m.peer);
        if (c) {
            c.socket.send({ t: "end", reason: "kicked" });
            c.socket.close();
        }
        return;
    }
    case "config": {
        const before = { ...config };
        config = m.config || {};
        if (m.config?.name) {
            host.peer.name = cleanName(m.config.name, 0);
            const update = peerMessage("update", host.peer);
            broadcast(update);
            toHost(update);
        }
        for (const c of guests()) {
            const mode = modeFor(c.peer);
            if (mode !== c.peer.mode || before.autoAccept !== config.autoAccept) {
                const changed = mode !== c.peer.mode;
                c.peer.mode = mode;
                c.socket.send({ t: "settings", mode, autoAccept: !!config.autoAccept });
                if (changed) {
                    broadcast(peerMessage("update", c.peer));
                    toHost(peerMessage("update", c.peer));
                }
            }
        }
        return;
    }
    case "end":
        return endSession("closed");
    case "cursor":
        return broadcast({ t: "cursor", from: 0, x: +m.x || 0, y: +m.y || 0, tool: String(m.tool || "").slice(0, 32), inside: !!m.inside });
    default:
        if (m.to !== undefined) {
            const c = target(m.to);
            const { to, ...rest } = m;
            if (c?.joined)
                c.socket.send(rest);
        } else {
            broadcast(m);
        }
    }
}

function onGuestMessage(client, m) {
    const id = client.peer.id;
    switch (m.t) {
    case "change":
    case "propose":
        return toHost({ ...m, from: id });
    case "cursor": {
        const out = { t: "cursor", from: id, x: +m.x || 0, y: +m.y || 0, tool: String(m.tool || "").slice(0, 32), inside: !!m.inside };
        broadcast(out, client);
        return toHost(out);
    }
    case "rename": {
        client.peer.name = cleanName(m.name, id);
        const update = peerMessage("update", client.peer);
        broadcast(update);
        return toHost(update);
    }
    case "leave":
        return client.socket.close();
    }
}

function endSession(reason) {
    // Cerrar un socket avisa enseguida (onclose): se quita antes de la lista.
    const ending = host;
    host = null;
    config = {};
    for (const c of [...clients.values()]) {
        if (c === ending)
            continue;
        clients.delete(c.socket);
        c.socket.send({ t: "end", reason });
        c.socket.close();
    }
    if (ending) {
        log(`Terminó la sesión de ${ending.peer.name}.`);
        clients.delete(ending.socket);
        ending.socket.close();
    }
}

function onConnection(socket) {
    if (clients.size >= 64)
        return socket.close();
    const client = { socket, nonce: new Uint8Array(randomBytes(16)), peer: null, joined: false, waiting: false, isHost: false };
    clients.set(socket, client);
    socket.onmessage = (text) => {
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
                    onJoin(client, m);
                return;
            }
            if (client.isHost)
                onHostMessage(m);
            else
                onGuestMessage(client, m);
        } catch (e) {
            log(`Error: ${e.stack || e}`);
        }
    };
    socket.onclose = () => {
        if (!clients.has(socket))
            return;
        clients.delete(socket);
        if (client.isHost) {
            endSession("closed");
        } else if (client.joined) {
            broadcast(peerMessage("remove", client.peer));
            toHost(peerMessage("remove", client.peer));
            log(`${client.peer.name} salió.`);
        } else if (client.waiting && client.peer) {
            toHost(peerMessage("remove", client.peer));
        }
    };
    socket.send({ t: "hello", v: VERSION, session: host ? sessionName() : "", host: host?.peer.name || "", auth: authName(),
                  nonce: Buffer.from(client.nonce).toString("base64"), server: "relay", app: "Kaint relay" });
}

// --- HTTP: Kaint para la web, /kaint-collab.json y el saludo de WebSocket ----------------------

const MIME = {
    ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8", ".json": "application/json", ".png": "image/png", ".svg": "image/svg+xml",
    ".ico": "image/x-icon", ".wasm": "application/wasm", ".ttf": "font/ttf", ".woff2": "font/woff2", ".txt": "text/plain; charset=utf-8",
    ".webmanifest": "application/manifest+json",
};

const server = createServer(async (req, res) => {
    const url = new URL(req.url, "http://x");
    if (url.pathname === "/kaint-collab.json") {
        res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" });
        res.end(JSON.stringify(info()));
        return;
    }
    let path = decodeURIComponent(url.pathname);
    if (path.endsWith("/"))
        path += "index.html";
    const file = normalize(join(ROOT, path));
    if (!file.startsWith(ROOT + sep)) {
        res.writeHead(403).end();
        return;
    }
    try {
        if (!(await stat(file)).isFile())
            throw new Error();
        let body = await readFile(file);
        // La página sabe así que la sirve una sesión (y pregunta por /kaint-collab.json para unirse).
        if (file.endsWith(`${sep}index.html`))
            body = Buffer.from(body.toString("utf8").replace("<head>", '<head><meta name="kaint-collab" content="1">'), "utf8");
        res.writeHead(200, { "Content-Type": MIME[extname(file).toLowerCase()] || "application/octet-stream", "Cache-Control": "no-cache" });
        res.end(body);
    } catch {
        res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" }).end("404");
    }
});

server.on("upgrade", (req, socket, head) => {
    const key = req.headers["sec-websocket-key"];
    if (String(req.headers.upgrade || "").toLowerCase() !== "websocket" || !key) {
        socket.destroy();
        return;
    }
    const accept = createHash("sha1").update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest("base64");
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    onConnection(new Socket(socket, head));
});

// --- Anuncio en la red local -------------------------------------------------------------------

function localAddresses() {
    const out = [];
    for (const list of Object.values(networkInterfaces()))
        for (const a of list || [])
            if (a.family === "IPv4" && !a.internal)
                out.push(a);
    return out;
}

function broadcastAddress(a) {
    const ip = a.address.split(".").map(Number), mask = a.netmask.split(".").map(Number);
    return ip.map((b, i) => (b & mask[i]) | (~mask[i] & 255)).join(".");
}

const beacon = createSocket({ type: "udp4", reuseAddr: true });
beacon.on("error", () => {});
beacon.bind(() => beacon.setBroadcast(true));
setInterval(() => {
    if (!host || !ANNOUNCE || config.announce === false)
        return;
    const data = Buffer.from(JSON.stringify({ "kaint-collab": VERSION, name: sessionName(), host: host.peer.name, port: PORT,
                                              auth: authName(), app: host.peer.app || "Kaint web", web: true, peers: peers().length }));
    const targets = new Set([...localAddresses().map(broadcastAddress), "127.0.0.1"]);
    for (const t of targets)
        beacon.send(data, BEACON_PORT, t, () => {});
}, 2000).unref();

function log(text) {
    console.log(`[${new Date().toLocaleTimeString()}] ${text}`);
}

server.listen(PORT, () => {
    console.log(`Kaint · servidor del modo colaborativo (protocolo v${VERSION}) en el puerto ${PORT}`);
    console.log(`  En este equipo, para hospedar:   http://localhost:${PORT}/`);
    for (const a of localAddresses())
        console.log(`  Para los demás de la red:       http://${a.address}:${PORT}/`);
    console.log(`  Clave para hospedar desde otro equipo: ${HOST_KEY}`);
});
server.on("error", (e) => {
    console.error(`No se pudo abrir el puerto ${PORT}: ${e.message}`);
    process.exit(1);
});

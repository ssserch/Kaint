#!/usr/bin/env node
// Servidor del modo colaborativo para Kaint para la web (docs/collaboration.md).
//
// Un navegador no puede abrir un servidor: para hospedar una sesión desde Kaint para la web se
// ejecuta esto en el equipo del anfitrión (sin dependencias, Node 18 o posterior):
//
//     node tools/collab-server.mjs [--port 47913] [--no-announce]
//
// También es un servidor de señalización para el modo P2P (ws://…/signal, el protocolo de los
// rastreadores de WebTorrent), por si se prefiere no usar los públicos.
//
// y se abre http://localhost:47913/ en ese equipo: la página se une como anfitriona. Los demás
// abren http://<ip-del-anfitrión>:47913/ o, desde Kaint (escritorio), Archivo > Colaboración >
// Unirse (también encuentra la sesión en la red local). Además sirve los archivos de Kaint para la
// web, porque una página cargada con https no puede conectarse a ws:// de la red local.
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import { createSocket } from "node:dgram";
import { readFile, stat } from "node:fs/promises";
import { networkInterfaces } from "node:os";
import { extname, join, normalize, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { Hub } from "../js/collab/Hub.js";
import { BEACON_PORT, DEFAULT_PORT, VERSION, newDeviceCode } from "../js/collab/Protocol.js";

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

// --- Sesión (js/collab/Hub.js, la misma que usa el navegador que hospeda por P2P) ----------------

const isLoopback = (address) => address === "127.0.0.1" || address === "::1" || address === "::ffff:127.0.0.1";
const hub = new Hub({ trustHost: (link) => isLoopback(link.address), hostKey: HOST_KEY, log, server: "relay", app: "Kaint relay",
                      port: PORT });
// Para el anuncio en la red local.
const sessionName = () => hub.sessionName();
const authName = () => hub.authName();
const peers = () => hub.peers();
const info = () => ({ ...hub.info(), addresses: localAddresses().map((a) => a.address) });

// --- Señalización P2P (/signal): lo mínimo del protocolo de los rastreadores de WebTorrent ------
// Cada sala ("info_hash") tiene sus participantes ("peer_id"); las ofertas de uno se reparten
// entre otros de la sala y las respuestas vuelven al que hizo la oferta. Kaint y Kaint para la web
// lo usan para encontrarse por P2P (el código de la sesión no viaja: solo su huella).

const rooms = new Map(); // info_hash → Map(peer_id → socket)

function onSignalConnection(socket) {
    socket.maxMessage = 256 * 1024;
    const joined = new Set(); // "info_hash\npeer_id"
    const leave = (room, peer) => {
        const members = rooms.get(room);
        if (members?.get(peer) === socket) {
            members.delete(peer);
            if (!members.size)
                rooms.delete(room);
        }
        joined.delete(`${room}\n${peer}`);
    };
    socket.onmessage = (text) => {
        let m;
        try {
            m = JSON.parse(text);
        } catch {
            return;
        }
        if (m?.action !== "announce" || typeof m.info_hash !== "string" || typeof m.peer_id !== "string"
            || m.info_hash.length > 64 || m.peer_id.length > 64)
            return;
        const room = m.info_hash, peer = m.peer_id;
        if (m.event === "stopped")
            return leave(room, peer);
        if (!rooms.has(room)) {
            if (rooms.size > 10000)
                return;
            rooms.set(room, new Map());
        }
        const members = rooms.get(room);
        if (members.size >= 200 && !members.has(peer))
            return;
        members.set(peer, socket);
        joined.add(`${room}\n${peer}`);
        if (m.answer && typeof m.to_peer_id === "string") {
            members.get(m.to_peer_id)?.send({ action: "announce", info_hash: room, peer_id: peer, offer_id: m.offer_id, answer: m.answer });
            return;
        }
        socket.send({ action: "announce", info_hash: room, interval: 120, complete: 0, incomplete: members.size });
        if (Array.isArray(m.offers) && m.offers.length) {
            const others = [...members.entries()].filter(([id]) => id !== peer).sort(() => Math.random() - 0.5);
            m.offers.slice(0, Math.min(others.length, 10)).forEach((o, i) => {
                if (o && typeof o.offer_id === "string" && o.offer && typeof o.offer === "object")
                    others[i][1].send({ action: "announce", info_hash: room, peer_id: peer, offer_id: o.offer_id, offer: o.offer });
            });
        }
    };
    socket.onclose = () => {
        for (const key of [...joined]) {
            const [room, peer] = key.split("\n");
            leave(room, peer);
        }
    };
}

// --- Sesiones cercanas (para la lista de Kaint para la web) ---------------------------------------
// Las que se anuncian en la red local (los avisos UDP que mandan Kaint y este servidor).

const nearby = new Map(); // "dirección:puerto" → { ...aviso, address, seen }
const listener = createSocket({ type: "udp4", reuseAddr: true });
listener.on("error", () => {});
listener.on("message", (data, rinfo) => {
    try {
        const b = JSON.parse(data.toString("utf8"));
        if (b?.["kaint-collab"] !== VERSION || !b.port)
            return;
        const address = rinfo.address === "127.0.0.1" ? (localAddresses()[0]?.address || "127.0.0.1") : rinfo.address;
        nearby.set(`${address}:${b.port}`, {
            name: String(b.name || "").slice(0, 80), host: String(b.host || "").slice(0, 60), address, port: Number(b.port) || 0,
            auth: String(b.auth || ""), app: String(b.app || "").slice(0, 60), web: !!b.web, peers: Number(b.peers) || 0, seen: Date.now(),
        });
    } catch {
        // ignore
    }
});
listener.bind(BEACON_PORT);

function nearbySessions() {
    const now = Date.now();
    for (const [key, s] of nearby)
        if (now - s.seen > 7000)
            nearby.delete(key);
    return [...nearby.values()].map(({ seen, ...s }) => s);
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
    // Kaint para la web abierta desde otra dirección también puede preguntar (CORS).
    if (url.pathname === "/kaint-collab.json" || url.pathname === "/kaint-collab-sessions.json") {
        res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store", "Access-Control-Allow-Origin": "*",
                             "Access-Control-Allow-Private-Network": "true" });
        res.end(JSON.stringify(url.pathname === "/kaint-collab.json" ? info() : nearbySessions()));
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
    const link = new Socket(socket, head);
    if (new URL(req.url, "http://x").pathname.startsWith("/signal"))
        onSignalConnection(link);
    else
        hub.connect(link);
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
    if (!hub.host || !ANNOUNCE || hub.config.announce === false)
        return;
    const data = Buffer.from(JSON.stringify({ "kaint-collab": VERSION, name: sessionName(), host: hub.host.peer.name, port: PORT,
                                              auth: authName(), app: hub.host.peer.app || "Kaint web", web: true, peers: peers().length }));
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
    console.log(`  Señalización P2P (Configuración > Colaboración > P2P): ws://<esta-dirección>:${PORT}/signal`);
});
server.on("error", (e) => {
    console.error(`No se pudo abrir el puerto ${PORT}: ${e.message}`);
    process.exit(1);
});

#include "core/plugins/PaintJs.h"

#include "core/Document.h"
#include "core/Layer.h"
#include "core/Parallel.h"
#include "core/Zip.h"
#include "core/plugins/PaintJsCanvas.h"
#include "core/plugins/PluginEffects.h"

#include "quickjs.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtDebug>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace PaintJs {

namespace {

constexpr qint64 kMaxPackage = 5 * 1024 * 1024;
constexpr qint64 kMaxImageFile = 1024LL * 1024 * 1024;
constexpr int kMaxLayers = 64;
// Lo que puede tardar en cargarse un paquete (al buscar complementos) o en abrir o guardar un
// archivo antes de darlo por colgado (los efectos se pueden cancelar).
constexpr int kLoadTimeoutMs = 15000;
constexpr int kFileTimeoutMs = 120000;

bool validId(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,79}$"), QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
}

QString text(const QJsonValue& value, const QString& fallback, int max)
{
    const QString s = value.toString().trimmed();
    return s.isEmpty() ? fallback : s.left(max);
}

// --- El paquete -----------------------------------------------------------------------------

struct Package {
    QString path;
    QString fileName;
    QString id;
    QString name;
    QString version;
    QString author;
    QString copyright;
    QString description;
    QString website;
    QString license;
    QString dialogTitle;
    // plugin.js, con las imágenes de paintDotJs.asset("...") ya dentro (como en la web).
    QByteArray source;
    QHash<QString, QByteArray> assets;
};
using PackagePtr = std::shared_ptr<const Package>;

QString mimeOf(const QString& name)
{
    static const QHash<QString, QString> mimes{
        {QStringLiteral("gif"), QStringLiteral("image/gif")},       {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},      {QStringLiteral("json"), QStringLiteral("application/json")},
        {QStringLiteral("png"), QStringLiteral("image/png")},       {QStringLiteral("svg"), QStringLiteral("image/svg+xml")},
        {QStringLiteral("txt"), QStringLiteral("text/plain")},      {QStringLiteral("webp"), QStringLiteral("image/webp")},
    };
    return mimes.value(QFileInfo(name).suffix().toLower());
}

QString dataUrl(const QString& name, const QByteArray& data)
{
    return QStringLiteral("data:%1;base64,%2").arg(mimeOf(name), QString::fromLatin1(data.toBase64()));
}

QImage imageFromDataUrl(const QString& url)
{
    static const QRegularExpression re(QStringLiteral(R"(^data:[^,;]*(;base64)?,(.*)$)"), QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(url);
    if (!m.hasMatch())
        return {};
    const QByteArray payload = m.captured(1).isEmpty() ? QByteArray::fromPercentEncoding(m.captured(2).toUtf8())
                                                       : QByteArray::fromBase64(m.captured(2).toLatin1());
    return QImage::fromData(payload);
}

// Lee el paquete como la versión web (Plugins.readPackage): plugin.json, plugin.js y las imágenes.
PackagePtr readPackage(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return {};
    }
    if (f.size() > kMaxPackage) {
        *error = QObject::tr("Un complemento no puede ocupar más de 5 MB.");
        return {};
    }
    const ZipReader zip(f.readAll());
    if (!zip.isValid()) {
        *error = QObject::tr("No es un archivo ZIP válido.");
        return {};
    }
    auto pkg = std::make_shared<Package>();
    static const QRegularExpression assetName(QStringLiteral("^assets/[A-Za-z0-9_.-]+$"));
    for (const QString& name : zip.names()) {
        if (name.endsWith(QLatin1Char('/')) || name == QLatin1String("plugin.json") || name == QLatin1String("plugin.js"))
            continue;
        if (!assetName.match(name).hasMatch()) {
            *error = QObject::tr("Archivo no válido en el complemento: %1").arg(name);
            return {};
        }
        pkg->assets.insert(name.mid(7), zip.read(name, kMaxPackage));
    }
    if (!zip.contains(QStringLiteral("plugin.json")) || !zip.contains(QStringLiteral("plugin.js"))) {
        *error = QObject::tr("El complemento debe tener plugin.json y plugin.js en la raíz.");
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(zip.read(QStringLiteral("plugin.json"), kMaxPackage), &parse);
    if (!doc.isObject()) {
        *error = QObject::tr("plugin.json no es válido: %1").arg(parse.errorString());
        return {};
    }
    const QJsonObject m = doc.object();
    if (!m.value(QStringLiteral("apiVersion")).isDouble() || m.value(QStringLiteral("apiVersion")).toInt() != kApiVersion) {
        *error = QObject::tr("Esta versión de Kaint admite la versión %1 de la API de complementos.").arg(kApiVersion);
        return {};
    }
    pkg->id = m.value(QStringLiteral("id")).toString();
    if (!validId(pkg->id)) {
        *error = QObject::tr("%1 no es válido.").arg(QObject::tr("El identificador del complemento"));
        return {};
    }
    pkg->name = text(m.value(QStringLiteral("name")), pkg->id, 80);
    pkg->version = text(m.value(QStringLiteral("version")), QStringLiteral("1.0.0"), 32);
    pkg->author = text(m.value(QStringLiteral("author")), QObject::tr("Desconocido"), 80);
    pkg->copyright = text(m.value(QStringLiteral("copyright")), QString(), 120);
    pkg->description = text(m.value(QStringLiteral("description")), QString(), 500);
    pkg->website = text(m.value(QStringLiteral("website")), QString(), 500);
    pkg->license = text(m.value(QStringLiteral("license")), QString(), 120);
    pkg->dialogTitle = text(m.value(QStringLiteral("dialogTitle")), QString(), 120);

    const QString source = QString::fromUtf8(zip.read(QStringLiteral("plugin.js"), kMaxPackage * 4));
    static const QRegularExpression assetCall(QStringLiteral(R"(paintDotJs\.asset\(\s*(["'])([A-Za-z0-9_.-]+)\1\s*\))"));
    QString out;
    qsizetype last = 0;
    for (auto it = assetCall.globalMatch(source); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString asset = match.captured(2);
        if (!pkg->assets.contains(asset) || mimeOf(asset).isEmpty()) {
            *error = QObject::tr("Falta la imagen del complemento: %1").arg(asset);
            return {};
        }
        out += QStringView(source).mid(last, match.capturedStart() - last);
        out += QLatin1Char('"') + dataUrl(asset, pkg->assets.value(asset)) + QLatin1Char('"');
        last = match.capturedEnd();
    }
    out += QStringView(source).mid(last);
    pkg->source = out.toUtf8();
    pkg->path = path;
    pkg->fileName = QFileInfo(path).fileName();
    return pkg;
}

// --- El entorno de los complementos ----------------------------------------------------------

// Lo que el navegador da y los paquetes usan (un subconjunto: ver PaintJs.h), la API paintDotJs
// (la misma que Plugins.installApi de la web) y las ayudas que usa Kaint para llamar a los
// efectos y formatos. Devuelve esas ayudas; no quedan a la vista de los complementos.
const char* const kPrelude[] = {
    R"JS((function (native) {
"use strict";
const g = globalThis;
const define = (name, value) => Object.defineProperty(g, name, { value, writable: true, configurable: true, enumerable: false });

// Lo que necesita una página web (las ventanas en HTML): en Kaint de escritorio no está.
class BrowserOnlyError extends Error {
    constructor(what) {
        super(`${what} needs a web browser: it is not available in Kaint for desktop.`);
        this.kaintBrowserOnly = true;
    }
}
const browserOnly = (what) => { throw new BrowserOnlyError(what); };

define("window", g);
define("self", g);
const show = (args) => args.map((a) => {
    if (typeof a === "string")
        return a;
    if (a instanceof Error)
        return `${a.name}: ${a.message}`;
    try {
        return JSON.stringify(a);
    } catch {
        return String(a);
    }
}).join(" ");
define("console", Object.freeze({
    log: (...a) => native.log(0, show(a)), info: (...a) => native.log(0, show(a)), debug: () => {}, trace: () => {},
    warn: (...a) => native.log(1, show(a)), error: (...a) => native.log(2, show(a)),
    assert: (c, ...a) => { if (!c) native.log(2, show(["Assertion failed:", ...a])); },
    time: () => {}, timeEnd: () => {}, group: () => {}, groupEnd: () => {}, table: () => {},
}));

define("TextEncoder", class TextEncoder {
    get encoding() { return "utf-8"; }
    encode(s = "") { return native.utf8Encode(String(s)); }
    encodeInto(s, dest) {
        const b = native.utf8Encode(String(s)), n = Math.min(b.length, dest.length);
        dest.set(b.subarray(0, n));
        return { read: String(s).length, written: n };
    }
});
define("TextDecoder", class TextDecoder {
    constructor(label = "utf-8") {
        const l = String(label).trim().toLowerCase();
        if (!["utf-8", "utf8", "unicode-1-1-utf-8"].includes(l))
            throw new RangeError(`The "${label}" encoding is not supported.`);
    }
    get encoding() { return "utf-8"; }
    decode(input) {
        if (input === undefined || input === null)
            return "";
        const bytes = ArrayBuffer.isView(input) ? new Uint8Array(input.buffer, input.byteOffset, input.byteLength) : new Uint8Array(input);
        return native.utf8Decode(bytes);
    }
});

class ImageData {
    constructor(a, b, c) {
        if (typeof a === "number") {
            if (!(a > 0 && b > 0))
                throw new RangeError("The ImageData width and height must be positive.");
            this.width = a >>> 0;
            this.height = b >>> 0;
            this.data = new Uint8ClampedArray(this.width * this.height * 4);
        } else {
            if (!(a instanceof Uint8ClampedArray))
                throw new TypeError("ImageData needs a Uint8ClampedArray.");
            this.width = b >>> 0;
            this.height = c === undefined ? a.length / 4 / this.width : c >>> 0;
            if (!(this.width > 0) || this.width * this.height * 4 !== a.length)
                throw new RangeError("The ImageData size does not match its data.");
            this.data = a;
        }
        this.colorSpace = "srgb";
    }
}
define("ImageData", ImageData);

define("setTimeout", (fn, ms, ...args) => { if (typeof fn === "function") Promise.resolve().then(() => fn(...args)); return 0; });
define("clearTimeout", () => {});
define("setInterval", () => 0);
define("clearInterval", () => {});
define("requestAnimationFrame", (fn) => { Promise.resolve().then(() => fn(0)); return 0; });
define("cancelAnimationFrame", () => {});
define("queueMicrotask", (fn) => { Promise.resolve().then(fn); });
define("addEventListener", () => {});
define("removeEventListener", () => {});
define("dispatchEvent", () => true);
define("devicePixelRatio", 1);
define("matchMedia", (media) => ({ matches: false, media: String(media), addEventListener() {}, removeEventListener() {}, addListener() {}, removeListener() {} }));
define("getComputedStyle", () => browserOnly("getComputedStyle()"));
define("alert", () => browserOnly("alert()"));
define("confirm", () => browserOnly("confirm()"));
define("prompt", () => browserOnly("prompt()"));
define("fetch", () => Promise.reject(new TypeError("Plugins have no network access in Kaint for desktop.")));
define("navigator", Object.freeze({ userAgent: "Kaint", language: "en", languages: Object.freeze(["en"]), hardwareConcurrency: 1, platform: "", onLine: false }));
define("location", Object.freeze({ href: "about:blank", origin: "null", protocol: "about:", host: "", hostname: "", port: "", pathname: "blank", search: "", hash: "" }));
if (typeof g.performance === "undefined") {
    const start = Date.now();
    define("performance", Object.freeze({ now: () => Date.now() - start, timeOrigin: start }));
}
class MemoryStorage {
    constructor() { Object.defineProperty(this, "_items", { value: new Map() }); }
    get length() { return this._items.size; }
    key(i) { return [...this._items.keys()][i] ?? null; }
    getItem(k) { return this._items.has(String(k)) ? this._items.get(String(k)) : null; }
    setItem(k, v) { this._items.set(String(k), String(v)); }
    removeItem(k) { this._items.delete(String(k)); }
    clear() { this._items.clear(); }
}
define("localStorage", new MemoryStorage());
define("sessionStorage", new MemoryStorage());

const clone = (v, seen) => {
    if (typeof v !== "object" || v === null)
        return v;
    if (seen.has(v))
        return seen.get(v);
    let out;
    if (v instanceof DataView)
        out = new DataView(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength));
    else if (ArrayBuffer.isView(v))
        out = v.slice();
    else if (v instanceof ArrayBuffer)
        out = v.slice(0);
    else if (v instanceof Date)
        out = new Date(v.getTime());
    else if (v instanceof RegExp)
        out = new RegExp(v.source, v.flags);
    else if (v instanceof ImageData)
        out = new ImageData(v.data.slice(), v.width, v.height);
    if (out) {
        seen.set(v, out);
        return out;
    }
    if (v instanceof Map) {
        out = new Map();
        seen.set(v, out);
        for (const [k, x] of v)
            out.set(clone(k, seen), clone(x, seen));
        return out;
    }
    if (v instanceof Set) {
        out = new Set();
        seen.set(v, out);
        for (const x of v)
            out.add(clone(x, seen));
        return out;
    }
    out = Array.isArray(v) ? [] : {};
    seen.set(v, out);
    for (const k of Object.keys(v))
        out[k] = clone(v[k], seen);
    return out;
};
define("structuredClone", (v) => clone(v, new Map()));

const partBytes = (part) => {
    if (part instanceof Blob)
        return part._bytes;
    if (part instanceof ArrayBuffer)
        return new Uint8Array(part);
    if (ArrayBuffer.isView(part))
        return new Uint8Array(part.buffer, part.byteOffset, part.byteLength);
    return native.utf8Encode(String(part));
};
class Blob {
    constructor(parts = [], options = {}) {
        const chunks = Array.from(parts, partBytes);
        const out = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
        let o = 0;
        for (const c of chunks) {
            out.set(c, o);
            o += c.length;
        }
        Object.defineProperty(this, "_bytes", { value: out, writable: true });
        this.type = String(options?.type || "").toLowerCase();
    }
    get size() { return this._bytes.length; }
    arrayBuffer() { return Promise.resolve(this._bytes.slice().buffer); }
    bytes() { return Promise.resolve(this._bytes.slice()); }
    text() { return Promise.resolve(native.utf8Decode(this._bytes)); }
    slice(start = 0, end = this.size, type = "") {
        const b = new Blob([], { type });
        b._bytes = this._bytes.slice(start, end);
        return b;
    }
}
define("Blob", Blob);
define("File", class File extends Blob {
    constructor(parts, name, options = {}) {
        super(parts, options);
        this.name = String(name);
        this.lastModified = Number(options.lastModified) || Date.now();
    }
});
)JS",
    R"JS(
// --- Imágenes y lienzos (canvas 2D dibujado con QPainter) ---
const base64 = (bytes) => {
    let s = "";
    for (let i = 0; i < bytes.length; i += 0x8000)
        s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
    return btoa(s);
};
const dataUrlBytes = (url) => {
    const m = /^data:([^,]*?)(;base64)?,(.*)$/s.exec(String(url));
    if (!m)
        return null;
    if (!m[2])
        return native.utf8Encode(decodeURIComponent(m[3]));
    const bin = atob(m[3]), out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; ++i)
        out[i] = bin.charCodeAt(i);
    return out;
};
const decodeBytes = (bytes) => {
    const r = bytes ? native.decodeImage(bytes) : null;
    return r ? new ImageData(r[2], r[0], r[1]) : null;
};
class ImageBitmap {
    constructor(data) {
        Object.defineProperty(this, "_data", { value: data });
        this.width = data.width;
        this.height = data.height;
    }
    close() {}
}
class Image {
    constructor(width, height) {
        Object.defineProperty(this, "_data", { value: null, writable: true });
        Object.defineProperty(this, "_listeners", { value: { load: [], error: [] } });
        this._src = "";
        this.onload = null;
        this.onerror = null;
        this.complete = false;
        this.naturalWidth = 0;
        this.naturalHeight = 0;
        this.width = width ?? 0;
        this.height = height ?? 0;
        this.crossOrigin = null;
        this.decoding = "auto";
    }
    get src() { return this._src; }
    set src(value) {
        this._src = String(value);
        // Solo imágenes dentro de la dirección (data:), como las de paintDotJs.asset().
        const data = decodeBytes(dataUrlBytes(this._src));
        this._data = data;
        this.complete = true;
        if (data) {
            this.naturalWidth = data.width;
            this.naturalHeight = data.height;
            this.width ||= data.width;
            this.height ||= data.height;
        }
        Promise.resolve().then(() => {
            const type = data ? "load" : "error", event = { type, target: this, currentTarget: this };
            if (typeof this["on" + type] === "function")
                this["on" + type](event);
            for (const fn of this._listeners[type])
                fn.call(this, event);
        });
    }
    decode() { return this._data ? Promise.resolve() : Promise.reject(new Error("The image could not be decoded.")); }
    addEventListener(type, fn) { this._listeners[type]?.push(fn); }
    removeEventListener(type, fn) {
        const list = this._listeners[type];
        const i = list ? list.indexOf(fn) : -1;
        if (i >= 0)
            list.splice(i, 1);
    }
}
define("Image", Image);
define("HTMLImageElement", Image);
define("ImageBitmap", ImageBitmap);

const CANVAS = Symbol("kaint.canvas");
// Lo que se puede dibujar: un lienzo, un ImageData o una imagen ya leída.
const sourceOf = (img) => {
    if (img && img[CANVAS])
        return img[CANVAS];
    if (img && img.canvas && img.canvas[CANVAS])
        return img.canvas[CANVAS];
    if (img instanceof ImageData)
        return img;
    if ((img instanceof Image || img instanceof ImageBitmap) && img._data)
        return img._data;
    throw new TypeError("The image is not a canvas, an ImageData or a loaded image.");
};
define("createImageBitmap", (source) => {
    try {
        if (source instanceof Blob) {
            const data = decodeBytes(source._bytes);
            return data ? Promise.resolve(new ImageBitmap(data)) : Promise.reject(new Error("The image could not be decoded."));
        }
        if (source instanceof ImageData)
            return Promise.resolve(new ImageBitmap(new ImageData(source.data.slice(), source.width, source.height)));
        const s = sourceOf(source);
        if (s instanceof ImageData)
            return Promise.resolve(new ImageBitmap(new ImageData(s.data.slice(), s.width, s.height)));
        const [w, h] = native.canvasSize(s);
        return Promise.resolve(new ImageBitmap(new ImageData(native.canvasGetImageData(s, 0, 0, w, h), w, h)));
    } catch (e) {
        return Promise.reject(e);
    }
});

class DOMMatrix {
    constructor(init) {
        const m = Array.isArray(init) && init.length >= 6 ? init : [1, 0, 0, 1, 0, 0];
        [this.a, this.b, this.c, this.d, this.e, this.f] = m.map(Number);
    }
    get m11() { return this.a; } get m12() { return this.b; } get m21() { return this.c; }
    get m22() { return this.d; } get m41() { return this.e; } get m42() { return this.f; }
    get is2D() { return true; }
    get isIdentity() { return this.a === 1 && this.b === 0 && this.c === 0 && this.d === 1 && this.e === 0 && this.f === 0; }
    transformPoint(p = {}) {
        const x = Number(p.x) || 0, y = Number(p.y) || 0;
        return { x: this.a * x + this.c * y + this.e, y: this.b * x + this.d * y + this.f, z: 0, w: 1 };
    }
}
if (typeof g.DOMMatrix === "undefined")
    define("DOMMatrix", DOMMatrix);

class CanvasGradient {
    constructor(kind, coords) { Object.defineProperty(this, "_spec", { value: { _kind: kind, _coords: coords.map(Number), _stops: [] } }); }
    addColorStop(offset, color) {
        offset = Number(offset);
        if (!(offset >= 0 && offset <= 1))
            throw new RangeError("The color stop offset must be between 0 and 1.");
        const css = native.cssColor(String(color));
        if (css === null)
            throw new SyntaxError(`"${color}" is not a color.`);
        this._spec._stops.push([offset, css]);
    }
}
class CanvasPattern {
    constructor(image, repetition) { Object.defineProperty(this, "_spec", { value: { _kind: "pattern", _image: image, _repeat: String(repetition ?? "repeat") } }); }
    setTransform() {}
}
const paintOf = (value, previous) => {
    if (typeof value === "string") {
        const css = native.cssColor(value);
        return css === null ? previous : css;
    }
    return value instanceof CanvasGradient || value instanceof CanvasPattern ? value : previous;
};

class Path2D {
    constructor(init) {
        Object.defineProperty(this, "_ops", { value: [] });
        if (init instanceof Path2D)
            this._ops.push(...init._ops);
        else if (init !== undefined)
            throw new Error("Path2D from SVG path data is not supported in Kaint for desktop.");
    }
    addPath(path) { if (path instanceof Path2D) this._ops.push(...path._ops); }
    closePath() { this._ops.push([1]); }
    moveTo(x, y) { this._ops.push([2, x, y]); }
    lineTo(x, y) { this._ops.push([3, x, y]); }
    quadraticCurveTo(a, b, x, y) { this._ops.push([4, a, b, x, y]); }
    bezierCurveTo(a, b, c, d, x, y) { this._ops.push([5, a, b, c, d, x, y]); }
    arc(x, y, r, s, e, ccw = false) {
        if (r < 0)
            throw new RangeError("The radius must not be negative.");
        this._ops.push([6, x, y, r, s, e, ccw ? 1 : 0]);
    }
    arcTo(x1, y1, x2, y2, r) { this._ops.push([7, x1, y1, x2, y2, r]); }
    ellipse(x, y, rx, ry, rot, s, e, ccw = false) {
        if (rx < 0 || ry < 0)
            throw new RangeError("The radii must not be negative.");
        this._ops.push([8, x, y, rx, ry, rot, s, e, ccw ? 1 : 0]);
    }
    rect(x, y, w, h) { this._ops.push([9, x, y, w, h]); }
    roundRect(x, y, w, h, radii = 0) { this._ops.push([10, x, y, w, h, radiusOf(radii)]); }
}
const radiusOf = (r) => {
    const first = Array.isArray(r) ? r[0] : r;
    return typeof first === "object" && first !== null ? Number(first.x) || 0 : Number(first) || 0;
};
)JS",
    R"JS(
const defaults = () => ({
    fillStyle: "#000000", strokeStyle: "#000000", lineWidth: 1, lineCap: "butt", lineJoin: "miter", miterLimit: 10,
    globalAlpha: 1, globalCompositeOperation: "source-over", font: "10px sans-serif", textAlign: "start",
    textBaseline: "alphabetic", direction: "inherit", imageSmoothingEnabled: true, imageSmoothingQuality: "low",
    lineDash: [], lineDashOffset: 0, shadowBlur: 0, shadowColor: "rgba(0, 0, 0, 0)", shadowOffsetX: 0, shadowOffsetY: 0,
    filter: "none", letterSpacing: "0px", wordSpacing: "0px", fontKerning: "auto", textRendering: "auto",
});
const COMPOSITE = new Set(["source-over", "source-in", "source-out", "source-atop", "destination-over", "destination-in",
    "destination-out", "destination-atop", "lighter", "copy", "xor", "multiply", "screen", "overlay", "darken", "lighten",
    "color-dodge", "color-burn", "hard-light", "soft-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity"]);
const oneOf = (list) => (value, previous) => list.includes(value) ? value : previous;
const positive = (value, previous) => { value = Number(value); return Number.isFinite(value) && value > 0 ? value : previous; };
const finite = (value, previous) => { value = Number(value); return Number.isFinite(value) ? value : previous; };
const setters = {
    fillStyle: paintOf, strokeStyle: paintOf, lineWidth: positive, miterLimit: positive,
    lineCap: oneOf(["butt", "round", "square"]), lineJoin: oneOf(["miter", "round", "bevel"]),
    globalAlpha: (v, p) => { v = Number(v); return Number.isFinite(v) && v >= 0 && v <= 1 ? v : p; },
    globalCompositeOperation: (v, p) => COMPOSITE.has(v) ? v : p,
    font: (v, p) => String(v).trim() ? String(v) : p,
    textAlign: oneOf(["start", "end", "left", "right", "center"]),
    textBaseline: oneOf(["top", "hanging", "middle", "alphabetic", "ideographic", "bottom"]),
    direction: oneOf(["ltr", "rtl", "inherit"]), imageSmoothingEnabled: (v) => !!v,
    imageSmoothingQuality: oneOf(["low", "medium", "high"]), lineDashOffset: finite,
    shadowBlur: finite, shadowOffsetX: finite, shadowOffsetY: finite, shadowColor: (v, p) => native.cssColor(String(v)) ?? p,
    filter: (v) => String(v), letterSpacing: (v) => String(v), wordSpacing: (v) => String(v), fontKerning: (v) => String(v),
    textRendering: (v) => String(v),
};

class CanvasRenderingContext2D {
    constructor(canvas) {
        Object.defineProperty(this, "canvas", { value: canvas, enumerable: true });
        Object.defineProperty(this, "_s", { value: defaults(), writable: true });
        Object.defineProperty(this, "_stack", { value: [], writable: true });
    }
    _reset() {
        this._s = defaults();
        this._stack = [];
    }
    get _c() { return this.canvas[CANVAS]; }
    _style(paint) {
        const s = this._s;
        return { paint: paint instanceof CanvasGradient || paint instanceof CanvasPattern ? paint._spec : paint, alpha: s.globalAlpha,
                 comp: s.globalCompositeOperation, lineWidth: s.lineWidth, lineCap: s.lineCap, lineJoin: s.lineJoin,
                 miterLimit: s.miterLimit, dash: s.lineDash, dashOffset: s.lineDashOffset, font: s.font, textAlign: s.textAlign,
                 textBaseline: s.textBaseline, smoothing: s.imageSmoothingEnabled };
    }
    getContextAttributes() { return { alpha: true, colorSpace: "srgb", desynchronized: false, willReadFrequently: false }; }
    isContextLost() { return false; }
    save() {
        this._stack.push({ ...this._s, lineDash: this._s.lineDash.slice() });
        native.canvasSave(this._c);
    }
    restore() {
        if (this._stack.length)
            this._s = this._stack.pop();
        native.canvasRestore(this._c);
    }
    reset() {
        native.canvasResize(this._c, this.canvas.width, this.canvas.height);
        this._reset();
    }
    setLineDash(list) {
        const a = Array.from(list ?? [], Number);
        if (a.some((n) => !Number.isFinite(n) || n < 0))
            return;
        this._s.lineDash = a.length % 2 ? [...a, ...a] : a;
    }
    getLineDash() { return this._s.lineDash.slice(); }
    setTransform(a, b, c, d, e, f) {
        if (typeof a === "object" && a !== null)
            ({ a = 1, b = 0, c = 0, d = 1, e = 0, f = 0 } = a);
        else if (a === undefined)
            [a, b, c, d, e, f] = [1, 0, 0, 1, 0, 0];
        native.canvasTransform(this._c, a, b, c, d, e, f, false);
    }
    resetTransform() { native.canvasTransform(this._c, 1, 0, 0, 1, 0, 0, false); }
    transform(a, b, c, d, e, f) { native.canvasTransform(this._c, a, b, c, d, e, f, true); }
    translate(x, y) { this.transform(1, 0, 0, 1, x, y); }
    scale(x, y) { this.transform(x, 0, 0, y, 0, 0); }
    rotate(r) {
        const c = Math.cos(r), s = Math.sin(r);
        this.transform(c, s, -s, c, 0, 0);
    }
    getTransform() { return new g.DOMMatrix(native.canvasGetTransform(this._c)); }
    beginPath() { native.canvasPath(this._c, 0); }
    closePath() { native.canvasPath(this._c, 1); }
    moveTo(x, y) { native.canvasPath(this._c, 2, x, y); }
    lineTo(x, y) { native.canvasPath(this._c, 3, x, y); }
    quadraticCurveTo(a, b, x, y) { native.canvasPath(this._c, 4, a, b, x, y); }
    bezierCurveTo(a, b, c, d, x, y) { native.canvasPath(this._c, 5, a, b, c, d, x, y); }
    arc(x, y, r, s, e, ccw = false) {
        if (r < 0)
            throw new RangeError("The radius must not be negative.");
        native.canvasPath(this._c, 6, x, y, r, s, e, ccw ? 1 : 0);
    }
    arcTo(x1, y1, x2, y2, r) {
        if (r < 0)
            throw new RangeError("The radius must not be negative.");
        native.canvasPath(this._c, 7, x1, y1, x2, y2, r);
    }
    ellipse(x, y, rx, ry, rot, s, e, ccw = false) {
        if (rx < 0 || ry < 0)
            throw new RangeError("The radii must not be negative.");
        native.canvasPath(this._c, 8, x, y, rx, ry, rot, s, e, ccw ? 1 : 0);
    }
    rect(x, y, w, h) { native.canvasPath(this._c, 9, x, y, w, h); }
    roundRect(x, y, w, h, radii = 0) { native.canvasPath(this._c, 10, x, y, w, h, radiusOf(radii)); }
    fill(a, b) {
        if (a instanceof Path2D)
            native.canvasFill(this._c, this._style(this._s.fillStyle), b ?? "nonzero", a._ops);
        else
            native.canvasFill(this._c, this._style(this._s.fillStyle), a ?? "nonzero");
    }
    stroke(path) {
        if (path instanceof Path2D)
            native.canvasStroke(this._c, this._style(this._s.strokeStyle), path._ops);
        else
            native.canvasStroke(this._c, this._style(this._s.strokeStyle));
    }
    clip(a, b) {
        if (a instanceof Path2D)
            native.canvasClip(this._c, b ?? "nonzero", a._ops);
        else
            native.canvasClip(this._c, a ?? "nonzero");
    }
    isPointInPath(a, b, c, d) {
        if (a instanceof Path2D)
            return native.canvasIsPointInPath(this._c, b, c, d ?? "nonzero", a._ops);
        return native.canvasIsPointInPath(this._c, a, b, c ?? "nonzero");
    }
    isPointInStroke() { return false; }
    fillRect(x, y, w, h) { native.canvasRect(this._c, this._style(this._s.fillStyle), x, y, w, h, 0); }
    strokeRect(x, y, w, h) { native.canvasRect(this._c, this._style(this._s.strokeStyle), x, y, w, h, 1); }
    clearRect(x, y, w, h) { native.canvasRect(this._c, this._style(null), x, y, w, h, 2); }
    fillText(t, x, y) { native.canvasText(this._c, this._style(this._s.fillStyle), String(t), x, y, false); }
    strokeText(t, x, y) { native.canvasText(this._c, this._style(this._s.strokeStyle), String(t), x, y, true); }
    measureText(t) {
        const [width, ascent, descent, left, right] = native.canvasMeasure(this._style(null), String(t));
        return { width, actualBoundingBoxAscent: ascent, actualBoundingBoxDescent: descent, actualBoundingBoxLeft: left,
                 actualBoundingBoxRight: right, fontBoundingBoxAscent: ascent, fontBoundingBoxDescent: descent };
    }
    drawImage(img, ...a) {
        const source = sourceOf(img), style = this._style(null);
        if (a.length >= 8)
            native.canvasDrawImage(this._c, style, source, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        else if (a.length >= 4)
            native.canvasDrawImage(this._c, style, source, undefined, undefined, undefined, undefined, a[0], a[1], a[2], a[3]);
        else
            native.canvasDrawImage(this._c, style, source, undefined, undefined, undefined, undefined, a[0], a[1]);
    }
    createImageData(a, b) {
        return a instanceof ImageData ? new ImageData(a.width, a.height) : new ImageData(Math.abs(a | 0), Math.abs(b | 0));
    }
    getImageData(x, y, w, h) {
        x |= 0; y |= 0; w |= 0; h |= 0;
        return new ImageData(native.canvasGetImageData(this._c, x, y, w, h), Math.abs(w), Math.abs(h));
    }
    putImageData(img, dx, dy, ...dirty) {
        if (!(img instanceof ImageData) && !(img && img.data && img.width))
            throw new TypeError("putImageData() needs an ImageData.");
        if (dirty.length >= 4)
            native.canvasPutImageData(this._c, img.data, img.width, img.height, dx, dy, dirty[0], dirty[1], dirty[2], dirty[3]);
        else
            native.canvasPutImageData(this._c, img.data, img.width, img.height, dx, dy);
    }
    createLinearGradient(x0, y0, x1, y1) { return new CanvasGradient("linear", [x0, y0, x1, y1]); }
    createRadialGradient(x0, y0, r0, x1, y1, r1) {
        if (r0 < 0 || r1 < 0)
            throw new RangeError("The radii must not be negative.");
        return new CanvasGradient("radial", [x0, y0, r0, x1, y1, r1]);
    }
    createConicGradient(angle, x, y) { return new CanvasGradient("conic", [angle, x, y]); }
    createPattern(image, repetition) { return new CanvasPattern(sourceOf(image), repetition); }
}
for (const [name, set] of Object.entries(setters)) {
    Object.defineProperty(CanvasRenderingContext2D.prototype, name, {
        get() { return name === "lineDash" ? this._s.lineDash.slice() : this._s[name]; },
        set(value) { this._s[name] = set(value, this._s[name]); },
        configurable: true,
    });
}

class HTMLCanvasElement {
    constructor(width = 300, height = 150) {
        Object.defineProperty(this, CANVAS, { value: native.canvasNew(width, height) });
        Object.defineProperty(this, "_context", { value: null, writable: true });
        this.style = {};
        this.dataset = {};
    }
    get width() { return native.canvasSize(this[CANVAS])[0]; }
    set width(v) {
        native.canvasResize(this[CANVAS], Number(v) || 0, this.height);
        this._context?._reset();
    }
    get height() { return native.canvasSize(this[CANVAS])[1]; }
    set height(v) {
        native.canvasResize(this[CANVAS], this.width, Number(v) || 0);
        this._context?._reset();
    }
    getContext(type) { return type === "2d" ? (this._context ??= new CanvasRenderingContext2D(this)) : null; }
    toDataURL() { return "data:image/png;base64," + base64(new Uint8Array(native.canvasPng(this[CANVAS]))); }
    toBlob(callback) {
        const blob = new Blob([native.canvasPng(this[CANVAS])], { type: "image/png" });
        Promise.resolve().then(() => callback(blob));
    }
    convertToBlob() { return Promise.resolve(new Blob([native.canvasPng(this[CANVAS])], { type: "image/png" })); }
    transferToImageBitmap() {
        const w = this.width, h = this.height;
        return new ImageBitmap(new ImageData(native.canvasGetImageData(this[CANVAS], 0, 0, w, h), w, h));
    }
    addEventListener() {}
    removeEventListener() {}
    getBoundingClientRect() {
        const w = this.width, h = this.height;
        return { x: 0, y: 0, left: 0, top: 0, width: w, height: h, right: w, bottom: h };
    }
}
define("HTMLCanvasElement", HTMLCanvasElement);
define("OffscreenCanvas", class OffscreenCanvas extends HTMLCanvasElement {
    constructor(width, height) { super(width, height); }
});
define("CanvasRenderingContext2D", CanvasRenderingContext2D);
define("OffscreenCanvasRenderingContext2D", CanvasRenderingContext2D);
define("CanvasGradient", CanvasGradient);
define("CanvasPattern", CanvasPattern);
define("Path2D", Path2D);

const element = (tag) => {
    tag = String(tag).toLowerCase();
    if (tag === "canvas")
        return new HTMLCanvasElement();
    if (tag === "img")
        return new Image();
    return browserOnly(`document.createElement("${tag}")`);
};
define("document", Object.freeze({
    createElement: element,
    createElementNS: (ns, tag) => element(tag),
    createTextNode: () => browserOnly("document.createTextNode()"),
    createDocumentFragment: () => browserOnly("document.createDocumentFragment()"),
    getElementById: () => browserOnly("document.getElementById()"),
    getElementsByTagName: () => browserOnly("document.getElementsByTagName()"),
    getElementsByClassName: () => browserOnly("document.getElementsByClassName()"),
    querySelector: () => browserOnly("document.querySelector()"),
    querySelectorAll: () => browserOnly("document.querySelectorAll()"),
    addEventListener: () => {},
    removeEventListener: () => {},
    currentScript: null, readyState: "complete", visibilityState: "visible", hidden: false, defaultView: g, title: "Kaint",
    activeElement: null,
    get body() { return browserOnly("document.body"); },
    get head() { return browserOnly("document.head"); },
    get documentElement() { return browserOnly("document.documentElement"); },
}));
)JS",
    R"JS(
// --- La API de paint.js (la misma que Plugins.installApi de la web) ---
const pixels = Object.freeze({
    selectionBounds(mask, width, height) {
        if (!mask)
            return { x: 0, y: 0, width, height };
        let left = width, top = height, right = 0, bottom = 0;
        for (let i = 0; i < mask.length; ++i)
            if (mask[i]) {
                const x = i % width, y = Math.floor(i / width);
                left = Math.min(left, x); top = Math.min(top, y);
                right = Math.max(right, x + 1); bottom = Math.max(bottom, y + 1);
            }
        return { x: left, y: top, width: Math.max(0, right - left), height: Math.max(0, bottom - top) };
    },
    sample(source, width, height, x, y) {
        const o = (Math.max(0, Math.min(height - 1, Math.round(y))) * width + Math.max(0, Math.min(width - 1, Math.round(x)))) * 4;
        return Array.from(source.subarray(o, o + 4));
    },
    rgbToHsv(r, g, b) {
        r /= 255; g /= 255; b /= 255;
        const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
        const h = d === 0 ? 0 : max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
        return [(h * 60 + 360) % 360, max === 0 ? 0 : d / max, max];
    },
    hsvToRgb(h, s, v) {
        const hue = ((((h % 360) + 360) % 360) / 60), c = v * s, x = c * (1 - Math.abs((hue % 2) - 1)), m = v - c;
        const rgb = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][Math.floor(hue) % 6];
        return rgb.map((n) => Math.round((n + m) * 255));
    },
    convolve(source, width, height, kernel, { divisor = 1, bias = 0, preserveAlpha = true } = {}) {
        const size = Math.sqrt(kernel.length);
        if (!Number.isInteger(size) || size % 2 !== 1 || size > 31 || !Number.isFinite(divisor) || divisor === 0)
            throw new Error("Convolution requires an odd square kernel (up to 31×31) and a nonzero divisor.");
        const out = new Uint8ClampedArray(source), r = Math.floor(size / 2);
        for (let y = 0; y < height; ++y)
            for (let x = 0; x < width; ++x)
                for (let c = 0; c < (preserveAlpha ? 3 : 4); ++c) {
                    let sum = 0;
                    for (let ky = 0; ky < size; ++ky)
                        for (let kx = 0; kx < size; ++kx) {
                            const sx = Math.max(0, Math.min(width - 1, x + kx - r)), sy = Math.max(0, Math.min(height - 1, y + ky - r));
                            sum += source[(sy * width + sx) * 4 + c] * kernel[ky * size + kx];
                        }
                    out[(y * width + x) * 4 + c] = sum / divisor + bias;
                }
        return out;
    },
});
const controls = Object.freeze({
    range: (key, label, min, max, defaultValue, step = 1, options = {}) => ({ type: "range", key, label, min, max, defaultValue, step, ...options }),
    checkbox: (key, label, defaultValue = false) => ({ type: "checkbox", key, label, defaultValue }),
    select: (key, label, defaultValue, choices) => ({ type: "select", key, label, defaultValue, choices }),
    color: (key, label, defaultValue = "#000000") => ({ type: "color", key, label, defaultValue }),
    text: (key, label, defaultValue = "") => ({ type: "text", key, label, defaultValue }),
});
const utils = Object.freeze({
    clamp: (v, lo, hi) => Math.max(lo, Math.min(hi, v)),
    clampByte: (v) => Math.max(0, Math.min(255, Math.round(v))),
});
const effects = Object.freeze({
    list: () => JSON.parse(native.effectsList()),
    apply: (id, source, width, height, values = null, selection = null) => {
        const out = native.effectsApply(String(id), new Uint8ClampedArray(source), width | 0, height | 0, JSON.stringify(values ?? {}));
        if (selection)
            for (let i = 0; i < selection.length; ++i)
                for (let c = 0; c < 4; ++c)
                    out[i * 4 + c] = source[i * 4 + c] + ((out[i * 4 + c] - source[i * 4 + c]) * selection[i]) / 255;
        return out;
    },
    // Abrir el diálogo de otro efecto desde un complemento: aquí, como si se cancelara.
    configure: async () => null,
});
const dialogs = Object.freeze({ create: () => browserOnly("paintDotJs.dialogs.create()") });
let target = null;
define("paintDotJs", Object.freeze({
    apiVersion: 1, controls, dialogs, effects, utils, pixels,
    // Las imágenes del paquete (las de paintDotJs.asset("...") con el nombre escrito ya van dentro).
    asset: (name) => native.asset(String(name)) ?? name,
    registerPlugin: (definition) => {
        if (!target)
            throw new Error("paintDotJs.registerPlugin() may only be called while a plugin is loading.");
        target.push(definition);
    },
}));

// --- Lo que usa Kaint (no queda a la vista de los complementos) ---
const bytesOf = (d, what) => {
    if (d instanceof Uint8ClampedArray || d instanceof Uint8Array)
        return d;
    if (ArrayBuffer.isView(d))
        return new Uint8Array(d.buffer, d.byteOffset, d.byteLength);
    if (d instanceof ArrayBuffer)
        return new Uint8Array(d);
    if (Array.isArray(d))
        return Uint8ClampedArray.from(d);
    throw new Error(`${what} is not pixel data.`);
};
return Object.freeze({
    beginLoad() { target = []; },
    endLoad() {
        const r = target;
        target = null;
        return r;
    },
    describe(def) {
        const list = (v) => Array.isArray(v) ? v.filter((x) => x && typeof x === "object") : [];
        const category = (c) => typeof c === "object" && c !== null ? { name: c.name, icon: typeof c.icon === "string" ? c.icon : undefined } : c;
        return JSON.stringify({
            effects: list(def.effects).map((e) => ({
                id: e.id, name: e.name, menu: e.menu, category: category(e.category), icon: typeof e.icon === "string" ? e.icon : undefined,
                dialogTitle: e.dialogTitle, controls: list(e.controls), render: typeof e.render === "function",
                renderPixel: typeof e.renderPixel === "function", configure: typeof e.configure === "function",
            })),
            fileTypes: list(def.fileTypes).map((t) => ({
                id: t.id, name: t.name, extensions: Array.isArray(t.extensions) ? t.extensions.map(String) : [], mime: t.mime,
                supportsLayers: t.supportsLayers === true, decode: typeof t.decode === "function", encode: typeof t.encode === "function",
                saveControls: list(t.saveControls),
            })),
        });
    },
    effect: (def, id) => (Array.isArray(def.effects) ? def.effects : []).find((e) => e && e.id === id),
    fileType: (def, id) => (Array.isArray(def.fileTypes) ? def.fileTypes : []).find((t) => t && t.id === id),
    // Como el render() de Plugins.toEffect en la web: renderPixel() solo en 'rect'; render() con
    // la imagen entera.
    render(e, src, w, h, valuesJson, selection, bounds, rect) {
        const values = JSON.parse(valuesJson);
        if (typeof e.renderPixel === "function") {
            const d = new Uint8ClampedArray(src);
            for (let y = rect.y; y < rect.y + rect.h; ++y)
                for (let x = rect.x, i = (y * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4) {
                    const r = e.renderPixel({ r: d[i], g: d[i + 1], b: d[i + 2], a: d[i + 3], x, y, width: w, height: h, values });
                    if (!r || r.length !== 4)
                        throw new Error(`${e.name}: renderPixel() must return four channels.`);
                    d[i] = r[0]; d[i + 1] = r[1]; d[i + 2] = r[2]; d[i + 3] = r[3];
                }
            return d;
        }
        const source = new Uint8ClampedArray(src), destination = new Uint8ClampedArray(source);
        const result = e.render({ source, destination, width: w, height: h, values, selection, selectionBounds: bounds });
        if (result && typeof result.then === "function")
            throw new Error(`${e.name}: render() must be synchronous.`);
        if (result === undefined || result === destination)
            return destination;
        if (result instanceof ImageData && result.width === w && result.height === h)
            return result.data;
        if (result instanceof Uint8ClampedArray && result.length === destination.length)
            return result;
        throw new Error(`${e.name}: render() returned a value that is not valid.`);
    },
    // renderPixel() en un bloque de filas (src tiene solo esas filas): Kaint reparte los bloques
    // entre los núcleos. Cada píxel solo depende de sí mismo, así que da lo mismo que de una vez.
    renderPixels(e, src, w, h, valuesJson, rect) {
        const values = JSON.parse(valuesJson), d = new Uint8ClampedArray(src);
        for (let y = rect.y; y < rect.y + rect.h; ++y)
            for (let x = rect.x, i = ((y - rect.y) * w + rect.x) * 4; x < rect.x + rect.w; ++x, i += 4) {
                const r = e.renderPixel({ r: d[i], g: d[i + 1], b: d[i + 2], a: d[i + 3], x, y, width: w, height: h, values });
                if (!r || r.length !== 4)
                    throw new Error(`${e.name}: renderPixel() must return four channels.`);
                d[i] = r[0]; d[i + 1] = r[1]; d[i + 2] = r[2]; d[i + 3] = r[3];
            }
        return d;
    },
    configure(e, src, w, h, valuesJson, bounds, title, icon) {
        return e.configure({ title, icon, source: src, width: w, height: h, selection: null, selectionBounds: bounds,
                             values: JSON.parse(valuesJson), preview: () => {}, showOriginal: () => {} });
    },
    // Para saber al cargar si configure() abre una ventana propia: con una imagen pequeña de prueba.
    dryRun(e, valuesJson) {
        const w = 8, h = 8, src = new Uint8ClampedArray(w * h * 4);
        for (let y = 2; y < 6; ++y)
            for (let x = 2; x < 6; ++x)
                src.fill(128, (y * w + x) * 4, (y * w + x) * 4 + 3), src[(y * w + x) * 4 + 3] = 255;
        return this.configure(e, src, w, h, valuesJson, { x: 0, y: 0, width: w, height: h }, String(e.dialogTitle || e.name || ""), "");
    },
    json: (value) => value === undefined ? null : JSON.stringify(value),
    decode: (t, bytes, fileName) => t.decode({ bytes, fileName }),
    decoded(r) {
        const w = Number(r?.width), h = Number(r?.height);
        if (!(Number.isInteger(w) && Number.isInteger(h) && w > 0 && h > 0 && w <= 32768 && h <= 32768))
            throw new Error("decode() returned an image size that is not valid.");
        const n = w * h * 4;
        const list = Array.isArray(r.layers) ? r.layers : [{ name: "", visible: true, opacity: 255, blendMode: "normal", data: r.data }];
        const layers = list.slice(0, 64).map((l) => {
            const data = bytesOf(l?.data, "The layer data");
            if (data.length !== n)
                throw new Error("decode() returned layer data of the wrong size.");
            return { name: l.name == null ? "" : String(l.name), visible: l.visible !== false,
                     opacity: Math.max(0, Math.min(255, Math.round(Number(l.opacity ?? 255)))), blendMode: String(l.blendMode || "normal"), data };
        });
        if (!layers.length)
            throw new Error("decode() returned no layers.");
        return { width: w, height: h, resolution: Number(r.resolution) > 0 ? Number(r.resolution) : 0,
                 activeLayer: Number.isInteger(r.activeLayer) ? r.activeLayer : layers.length - 1, layers };
    },
    encode: (t, doc, valuesJson) => t.encode({ document: doc, values: JSON.parse(valuesJson) }),
    bytes(result) {
        if (result instanceof Blob)
            return result.arrayBuffer().then((b) => new Uint8Array(b));
        if (result instanceof ArrayBuffer || ArrayBuffer.isView(result))
            return new Uint8Array(bytesOf(result, "The result"));
        throw new Error("encode() must return a Blob, an ArrayBuffer or a typed array.");
    },
});
}))JS",
};

QByteArray preludeSource()
{
    QByteArray all;
    for (const char* part : kPrelude)
        all += part;
    return all;
}

// --- Conversión de valores ------------------------------------------------------------------

QString jsString(JSContext* ctx, JSValueConst v)
{
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, v);
    if (!s) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return {};
    }
    const QString out = QString::fromUtf8(s, qsizetype(len));
    JS_FreeCString(ctx, s);
    return out;
}

JSValue newString(JSContext* ctx, const QString& s)
{
    const QByteArray utf8 = s.toUtf8();
    return JS_NewStringLen(ctx, utf8.constData(), size_t(utf8.size()));
}

// Bytes de un TypedArray o un ArrayBuffer (sin copiar); null si no lo es.
const uint8_t* viewBytes(JSContext* ctx, JSValueConst v, size_t* size)
{
    size_t offset = 0, length = 0, element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, v, &offset, &length, &element);
    if (JS_IsException(buffer)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        const uint8_t* direct = JS_GetArrayBuffer(ctx, size, v);
        if (!direct)
            JS_FreeValue(ctx, JS_GetException(ctx));
        return direct;
    }
    size_t total = 0;
    const uint8_t* data = JS_GetArrayBuffer(ctx, &total, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return nullptr;
    *size = length;
    return data + offset;
}

JSValue newClampedArray(JSContext* ctx, const uchar* data, size_t size)
{
    JSValue buffer = JS_NewArrayBufferCopy(ctx, data, size);
    if (JS_IsException(buffer))
        return buffer;
    JSValue array = JS_NewTypedArray(ctx, 1, &buffer, JS_TYPED_ARRAY_UINT8C);
    JS_FreeValue(ctx, buffer);
    return array;
}

// RGBA de 8 bits sin premultiplicar, fila tras fila sin relleno.
QByteArray rgbaBytes(const QImage& image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray out(qsizetype(rgba.width()) * rgba.height() * 4, Qt::Uninitialized);
    for (int y = 0; y < rgba.height(); ++y)
        std::memcpy(out.data() + qsizetype(y) * rgba.width() * 4, rgba.constScanLine(y), size_t(rgba.width()) * 4);
    return out;
}

QImage imageFromRgba(const uint8_t* data, int w, int h)
{
    return QImage(data, w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(kLayerFormat);
}

// Los modos de fusión de las capas con los nombres de paint.js.
QString blendName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Multiply: return QStringLiteral("multiply");
    case BlendMode::Additive: return QStringLiteral("additive");
    case BlendMode::ColorBurn: return QStringLiteral("colorBurn");
    case BlendMode::ColorDodge: return QStringLiteral("colorDodge");
    case BlendMode::Reflect: return QStringLiteral("reflect");
    case BlendMode::Glow: return QStringLiteral("glow");
    case BlendMode::Overlay: return QStringLiteral("overlay");
    case BlendMode::Difference: return QStringLiteral("difference");
    case BlendMode::Negation: return QStringLiteral("negation");
    case BlendMode::Lighten: return QStringLiteral("lighten");
    case BlendMode::Darken: return QStringLiteral("darken");
    case BlendMode::Screen: return QStringLiteral("screen");
    case BlendMode::Xor: return QStringLiteral("xor");
    default: return QStringLiteral("normal");
    }
}

BlendMode blendFromName(const QString& name)
{
    static const QHash<QString, BlendMode> modes{
        {QStringLiteral("multiply"), BlendMode::Multiply},     {QStringLiteral("additive"), BlendMode::Additive},
        {QStringLiteral("colorburn"), BlendMode::ColorBurn},   {QStringLiteral("colordodge"), BlendMode::ColorDodge},
        {QStringLiteral("reflect"), BlendMode::Reflect},       {QStringLiteral("glow"), BlendMode::Glow},
        {QStringLiteral("overlay"), BlendMode::Overlay},       {QStringLiteral("difference"), BlendMode::Difference},
        {QStringLiteral("negation"), BlendMode::Negation},     {QStringLiteral("lighten"), BlendMode::Lighten},
        {QStringLiteral("darken"), BlendMode::Darken},         {QStringLiteral("screen"), BlendMode::Screen},
        {QStringLiteral("xor"), BlendMode::Xor},
    };
    QString key = name.toLower();
    key.remove(QLatin1Char('-')).remove(QLatin1Char('_')).remove(QLatin1Char(' '));
    return modes.value(key, BlendMode::Normal);
}

QJsonValue jsonOf(const QVariant& v)
{
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double:
    case QMetaType::Float: return v.toDouble();
    case QMetaType::QString: return v.toString();
    case QMetaType::QColor: return v.value<QColor>().name(QColor::HexRgb);
    case QMetaType::QPointF: return QJsonObject{{QStringLiteral("x"), v.toPointF().x()}, {QStringLiteral("y"), v.toPointF().y()}};
    default: return QJsonValue();
    }
}

QColor colorFromJson(const QJsonValue& v, const QColor& fallback)
{
    if (v.isString()) {
        QString s = v.toString().trimmed();
        if (s.size() == 9 && s.startsWith(QLatin1Char('#')))  // #rrggbbaa (CSS) → #aarrggbb (Qt)
            s = QLatin1Char('#') + s.mid(7, 2) + s.mid(1, 6);
        const QColor c = QColor::fromString(s);
        return c.isValid() ? c : fallback;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        return QColor(o.value(QStringLiteral("r")).toInt(), o.value(QStringLiteral("g")).toInt(), o.value(QStringLiteral("b")).toInt(),
                      o.value(QStringLiteral("a")).toInt(255));
    }
    return fallback;
}

// --- El entorno ---------------------------------------------------------------------------

class PaintJsEffect;

class Sandbox {
public:
    explicit Sandbox(PackagePtr pkg = {}, const std::atomic_bool* cancel = nullptr, int timeoutMs = 0);
    ~Sandbox();
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    bool ready() const { return m_ready; }
    JSContext* ctx() const { return m_ctx; }
    // Ejecuta plugin.js y se queda con lo que registra (la definición del complemento).
    bool load(QString* error);
    JSValueConst definition() const { return m_def; }
    // __kaint.<helper>(args...) con el resultado ya resuelto si es una promesa; JS_EXCEPTION si
    // falla (con el mensaje y, si fue por algo que solo da el navegador, browserOnly).
    JSValue call(const char* helper, std::initializer_list<JSValueConst> args, QString* error, bool* browserOnly = nullptr);
    JSValue evaluate(const QByteArray& code, const char* file, QString* error);
    bool interrupted() const;
    const PackagePtr& package() const { return m_pkg; }

    static Sandbox* of(JSContext* ctx) { return static_cast<Sandbox*>(JS_GetContextOpaque(ctx)); }

private:
    static int interrupt(JSContext* ctx, void* opaque);
    JSValue settle(JSValue value, QString* error, bool* browserOnly);
    QString describe(JSValueConst error, bool* browserOnly);
    QString takeException(bool* browserOnly);
    void installNatives(JSValueConst native);

    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
    JSValue m_kaint = JS_UNDEFINED;
    JSValue m_def = JS_UNDEFINED;
    PackagePtr m_pkg;
    const std::atomic_bool* m_cancel = nullptr;
    QDeadlineTimer m_deadline;
    bool m_ready = false;
};

JSValueConst argAt(int argc, JSValueConst* argv, int i)
{
    return i < argc ? argv[i] : JS_UNDEFINED;
}

JSValue jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    int32_t level = 0;
    JS_ToInt32(ctx, &level, argAt(argc, argv, 0));
    const Sandbox* box = Sandbox::of(ctx);
    const QString who = box && box->package() ? box->package()->fileName : QStringLiteral("paint.js");
    const QString message = jsString(ctx, argAt(argc, argv, 1)).left(2000);
    if (level >= 1)
        qWarning().noquote() << QStringLiteral("[%1]").arg(who) << message;
    else
        qInfo().noquote() << QStringLiteral("[%1]").arg(who) << message;
    return JS_UNDEFINED;
}

JSValue jsUtf8Encode(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    const QByteArray utf8 = jsString(ctx, argAt(argc, argv, 0)).toUtf8();
    return JS_NewUint8ArrayCopy(ctx, reinterpret_cast<const uint8_t*>(utf8.constData()), size_t(utf8.size()));
}

JSValue jsUtf8Decode(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    size_t size = 0;
    const uint8_t* bytes = viewBytes(ctx, argAt(argc, argv, 0), &size);
    if (!bytes)
        return JS_NewString(ctx, "");
    return newString(ctx, QString::fromUtf8(reinterpret_cast<const char*>(bytes), qsizetype(size)));
}

// (bytes) → [ancho, alto, RGBA] o null.
JSValue jsDecodeImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    size_t size = 0;
    const uint8_t* bytes = viewBytes(ctx, argAt(argc, argv, 0), &size);
    if (!bytes || size == 0)
        return JS_NULL;
    const QImage image = QImage::fromData(QByteArrayView(bytes, qsizetype(size)));
    if (image.isNull() || qint64(image.width()) * image.height() > 16384LL * 16384)
        return JS_NULL;
    const QByteArray rgba = rgbaBytes(image);
    JSValue out = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, out, 0, JS_NewInt32(ctx, image.width()));
    JS_SetPropertyUint32(ctx, out, 1, JS_NewInt32(ctx, image.height()));
    JS_SetPropertyUint32(ctx, out, 2, newClampedArray(ctx, reinterpret_cast<const uchar*>(rgba.constData()), size_t(rgba.size())));
    return out;
}

// (nombre) → la imagen del paquete como data: (o null).
JSValue jsAsset(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    const Sandbox* box = Sandbox::of(ctx);
    const QString name = jsString(ctx, argAt(argc, argv, 0));
    if (!box || !box->package() || !box->package()->assets.contains(name) || mimeOf(name).isEmpty())
        return JS_NULL;
    return newString(ctx, dataUrl(name, box->package()->assets.value(name)));
}

JSValue jsEffectsList(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    QJsonArray list;
    for (const Effect* e : Effects::all()) {
        if (e->hidden())
            continue;
        QJsonObject values;
        const ParamValues defaults = Effects::defaults(*e);
        for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
            const QJsonValue v = jsonOf(it.value());
            if (!v.isNull())
                values.insert(it.key(), v);
        }
        list.append(QJsonObject{{QStringLiteral("id"), e->id()},
                                {QStringLiteral("name"), e->name()},
                                {QStringLiteral("menu"), e->isAdjustment() ? QStringLiteral("Adjustments") : QStringLiteral("Effects")},
                                {QStringLiteral("category"), e->category()},
                                {QStringLiteral("values"), values}});
    }
    return newString(ctx, QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
}

ParamValues kaintValuesFor(const Effect& effect, const QJsonObject& given);

// (id, origen, ancho, alto, valoresJSON) → Uint8ClampedArray con el efecto de Kaint aplicado.
JSValue jsEffectsApply(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    const QString id = jsString(ctx, argAt(argc, argv, 0));
    const Effect* effect = Effects::find(id);
    if (!effect)
        return JS_ThrowTypeError(ctx, "Unknown effect: %s", id.toUtf8().constData());
    int32_t w = 0, h = 0;
    JS_ToInt32(ctx, &w, argAt(argc, argv, 2));
    JS_ToInt32(ctx, &h, argAt(argc, argv, 3));
    size_t size = 0;
    const uint8_t* bytes = viewBytes(ctx, argAt(argc, argv, 1), &size);
    if (!bytes || w <= 0 || h <= 0 || size != size_t(w) * size_t(h) * 4)
        return JS_ThrowRangeError(ctx, "The source does not match the width and height.");
    const QJsonObject given = QJsonDocument::fromJson(jsString(ctx, argAt(argc, argv, 4)).toUtf8()).object();
    const QImage src = imageFromRgba(bytes, w, h);
    const std::atomic_bool never{false};
    const QImage out = Effects::apply(*effect, src, Selection(), kaintValuesFor(*effect, given), EffectContext(), never);
    const QByteArray rgba = rgbaBytes(out.isNull() ? src : out);
    return newClampedArray(ctx, reinterpret_cast<const uchar*>(rgba.constData()), size_t(rgba.size()));
}

Sandbox::Sandbox(PackagePtr pkg, const std::atomic_bool* cancel, int timeoutMs)
    : m_pkg(std::move(pkg))
    , m_cancel(cancel)
    , m_deadline(timeoutMs > 0 ? QDeadlineTimer(timeoutMs) : QDeadlineTimer(QDeadlineTimer::Forever))
{
    m_rt = JS_NewRuntime();
    if (!m_rt)
        return;
    JS_SetMemoryLimit(m_rt, sizeof(void*) >= 8 ? (size_t(3) << 30) : (size_t(1) << 30));
    // Los efectos se calculan en hilos de trabajo: menos pila que la que tienen.
    JS_SetMaxStackSize(m_rt, 512 * 1024);
    JS_SetInterruptHandler(m_rt, &Sandbox::interrupt, this);
    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx)
        return;
    JS_SetContextOpaque(m_ctx, this);
    JSValue native = JS_NewObject(m_ctx);
    installNatives(native);
    const QByteArray prelude = preludeSource();
    JSValue factory = JS_Eval(m_ctx, prelude.constData(), size_t(prelude.size()), "<kaint>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(factory)) {
        qWarning().noquote() << "paint.js:" << takeException(nullptr);
        JS_FreeValue(m_ctx, native);
        return;
    }
    m_kaint = JS_Call(m_ctx, factory, JS_UNDEFINED, 1, &native);
    JS_FreeValue(m_ctx, factory);
    JS_FreeValue(m_ctx, native);
    if (JS_IsException(m_kaint)) {
        qWarning().noquote() << "paint.js:" << takeException(nullptr);
        m_kaint = JS_UNDEFINED;
        return;
    }
    m_ready = true;
}

Sandbox::~Sandbox()
{
    if (m_ctx) {
        JS_FreeValue(m_ctx, m_def);
        JS_FreeValue(m_ctx, m_kaint);
        JS_FreeContext(m_ctx);
    }
    if (m_rt)
        JS_FreeRuntime(m_rt);
}

void Sandbox::installNatives(JSValueConst native)
{
    const struct {
        const char* name;
        JSCFunction* fn;
        int length;
    } functions[] = {
        {"log", jsLog, 2},
        {"utf8Encode", jsUtf8Encode, 1},
        {"utf8Decode", jsUtf8Decode, 1},
        {"decodeImage", jsDecodeImage, 1},
        {"asset", jsAsset, 1},
        {"effectsList", jsEffectsList, 0},
        {"effectsApply", jsEffectsApply, 5},
    };
    for (const auto& f : functions)
        JS_SetPropertyStr(m_ctx, native, f.name, JS_NewCFunction(m_ctx, f.fn, f.name, f.length));
    detail::installCanvas(m_ctx, native);
}

int Sandbox::interrupt(JSContext*, void* opaque)
{
    return static_cast<Sandbox*>(opaque)->interrupted() ? 1 : 0;
}

bool Sandbox::interrupted() const
{
    return (m_cancel && m_cancel->load()) || m_deadline.hasExpired();
}

QString Sandbox::describe(JSValueConst error, bool* browserOnly)
{
    if (browserOnly) {
        JSValue flag = JS_IsObject(error) ? JS_GetPropertyStr(m_ctx, error, "kaintBrowserOnly") : JS_UNDEFINED;
        *browserOnly = JS_ToBool(m_ctx, flag) > 0;
        JS_FreeValue(m_ctx, flag);
        // Un complemento que usa lo que en el navegador da por hecho (document.body, window.X...).
        if (!*browserOnly && JS_IsError(error)) {
            const QString message = jsString(m_ctx, error);
            static const QRegularExpression dom(QStringLiteral(R"(\b(document|HTML\w*Element|customElements|getComputedStyle)\b)"));
            *browserOnly = message.startsWith(QLatin1String("ReferenceError")) && dom.match(message).hasMatch();
        }
    }
    if (interrupted())
        return m_deadline.hasExpired() ? QObject::tr("El complemento tardó demasiado y se detuvo.") : QString();
    QString message = jsString(m_ctx, error);
    if (JS_IsError(error)) {
        // Dónde: la primera línea de la pila ("at render (plugin.js:1:234)").
        JSValue stack = JS_GetPropertyStr(m_ctx, error, "stack");
        const QString where = JS_IsString(stack) ? jsString(m_ctx, stack).section(QLatin1Char('\n'), 0, 0).trimmed() : QString();
        JS_FreeValue(m_ctx, stack);
        if (!where.isEmpty() && !message.contains(where))
            message += QStringLiteral(" (%1)").arg(where);
    }
    return message.left(1000);
}

QString Sandbox::takeException(bool* browserOnly)
{
    JSValue e = JS_GetException(m_ctx);
    const QString message = describe(e, browserOnly);
    JS_FreeValue(m_ctx, e);
    return message;
}

JSValue Sandbox::settle(JSValue value, QString* error, bool* browserOnly)
{
    if (browserOnly)
        *browserOnly = false;
    if (JS_IsException(value)) {
        *error = takeException(browserOnly);
        return JS_EXCEPTION;
    }
    while (JS_IsPromise(value)) {
        const JSPromiseStateEnum state = JS_PromiseState(m_ctx, value);
        if (state == JS_PROMISE_FULFILLED) {
            JSValue result = JS_PromiseResult(m_ctx, value);
            JS_FreeValue(m_ctx, value);
            value = result;
            continue;
        }
        if (state == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(m_ctx, value);
            JS_FreeValue(m_ctx, value);
            *error = describe(reason, browserOnly);
            JS_FreeValue(m_ctx, reason);
            return JS_EXCEPTION;
        }
        JSContext* jobContext = nullptr;
        const int ran = JS_ExecutePendingJob(m_rt, &jobContext);
        if (ran < 0) {
            JS_FreeValue(m_ctx, value);
            *error = takeException(browserOnly);
            return JS_EXCEPTION;
        }
        if (ran == 0) {
            // Sin nada más que hacer y sin terminar: espera algo que aquí no llega (un clic en una
            // ventana, un temporizador...).
            JS_FreeValue(m_ctx, value);
            if (browserOnly)
                *browserOnly = true;
            *error = QObject::tr("El complemento se quedó esperando algo que solo da un navegador web.");
            return JS_EXCEPTION;
        }
    }
    return value;
}

JSValue Sandbox::call(const char* helper, std::initializer_list<JSValueConst> args, QString* error, bool* browserOnly)
{
    if (!m_ready) {
        *error = QObject::tr("No se pudo preparar el motor de JavaScript.");
        return JS_EXCEPTION;
    }
    JSValue fn = JS_GetPropertyStr(m_ctx, m_kaint, helper);
    std::vector<JSValueConst> argv(args);
    JSValue result = JS_Call(m_ctx, fn, m_kaint, int(argv.size()), argv.data());
    JS_FreeValue(m_ctx, fn);
    return settle(result, error, browserOnly);
}

JSValue Sandbox::evaluate(const QByteArray& code, const char* file, QString* error)
{
    JSValue result = JS_Eval(m_ctx, code.constData(), size_t(code.size()), file, JS_EVAL_TYPE_GLOBAL);
    return settle(result, error, nullptr);
}

bool Sandbox::load(QString* error)
{
    if (!m_ready || !m_pkg) {
        *error = QObject::tr("No se pudo preparar el motor de JavaScript.");
        return false;
    }
    JS_FreeValue(m_ctx, call("beginLoad", {}, error));
    JSValue result = evaluate(m_pkg->source, m_pkg->fileName.toUtf8().constData(), error);
    QString ignored;
    JSValue registrations = call("endLoad", {}, &ignored);
    if (JS_IsException(result)) {
        JS_FreeValue(m_ctx, registrations);
        if (error->isEmpty())
            *error = QObject::tr("No se pudo ejecutar %1.").arg(m_pkg->fileName);
        return false;
    }
    JS_FreeValue(m_ctx, result);
    int64_t count = 0;
    JS_GetLength(m_ctx, registrations, &count);
    if (count != 1) {
        JS_FreeValue(m_ctx, registrations);
        *error = QObject::tr("%1 debe llamar a paintDotJs.registerPlugin() una vez.").arg(m_pkg->fileName);
        return false;
    }
    m_def = JS_GetPropertyUint32(m_ctx, registrations, 0);
    JS_FreeValue(m_ctx, registrations);
    if (!JS_IsObject(m_def)) {
        *error = QObject::tr("%1 debe llamar a paintDotJs.registerPlugin() una vez.").arg(m_pkg->fileName);
        return false;
    }
    return true;
}

// --- Los efectos ---------------------------------------------------------------------------

struct EffectSpec {
    QString id;
    QString name;
    QString category;  // vacía con "Root" (en Efectos, sin submenú)
    bool adjustment = false;
    QImage icon;
    bool configure = false;
    bool pixel = false;  // renderPixel() en vez de render()
};

// Los controles del paquete → parámetros del diálogo de efectos de Kaint (como paramsFor en la
// web); 'choices' guarda el valor de cada opción de las listas.
QList<EffectParam> paramsFor(const QJsonArray& controls, QHash<QString, QStringList>* choices, QString* error)
{
    QList<EffectParam> params;
    for (const QJsonValue& v : controls) {
        if (params.size() >= 32)
            break;
        const QJsonObject c = v.toObject();
        const QString key = c.value(QStringLiteral("key")).toVariant().toString();
        const QString label = c.value(QStringLiteral("label")).toVariant().toString();
        const QString type = c.value(QStringLiteral("type")).toString();
        if (key.isEmpty()) {
            *error = QObject::tr("Control sin clave (key).");
            return {};
        }
        if (type == QLatin1String("range")) {
            const double step = c.value(QStringLiteral("step")).toDouble(1) > 0 ? c.value(QStringLiteral("step")).toDouble(1) : 1;
            const double min = c.value(QStringLiteral("min")).toDouble(), max = c.value(QStringLiteral("max")).toDouble(100);
            const double def = c.value(QStringLiteral("defaultValue")).toDouble(min);
            const bool integer = step == std::floor(step) && min == std::floor(min) && max == std::floor(max);
            if (integer) {
                params << EffectParam::integer(key, label, int(min), int(max), int(std::lround(def)));
            } else {
                const int decimals = std::clamp(int(std::ceil(-std::log10(step))), 0, 4);
                params << EffectParam::real(key, label, min, max, def, decimals);
            }
        } else if (type == QLatin1String("checkbox")) {
            params << EffectParam::boolean(key, label, c.value(QStringLiteral("defaultValue")).toBool());
        } else if (type == QLatin1String("select")) {
            QStringList values, names;
            for (const QJsonValue& choice : c.value(QStringLiteral("choices")).toArray()) {
                const QJsonArray pair = choice.toArray();
                values << pair.at(0).toVariant().toString();
                names << pair.at(1).toVariant().toString();
            }
            const int def = std::max<int>(0, int(values.indexOf(c.value(QStringLiteral("defaultValue")).toVariant().toString())));
            params << EffectParam::choice(key, label, names, def);
            choices->insert(key, values);
        } else if (type == QLatin1String("color")) {
            EffectParam p = EffectParam::color(key, label, EffectParam::ColorDefault::Black);
            p.defaultValue = colorFromJson(c.value(QStringLiteral("defaultValue")), Qt::black);
            params << p;
        } else if (type == QLatin1String("text")) {
            params << EffectParam::text(key, label, c.value(QStringLiteral("defaultValue")).toVariant().toString());
        } else {
            *error = QObject::tr("Control desconocido: %1").arg(type);
            return {};
        }
    }
    return params;
}

class PaintJsEffect : public Effect {
public:
    PaintJsEffect(PackagePtr pkg, const EffectSpec& spec, QList<EffectParam> params, QHash<QString, QStringList> choices)
        : m_pkg(std::move(pkg))
        , m_spec(spec)
        , m_params(std::move(params))
        , m_choices(std::move(choices))
    {
    }

    QString id() const override { return QStringLiteral("plugin.%1.%2").arg(m_pkg->id, m_spec.id); }
    QString name() const override { return m_spec.name; }
    QString category() const override { return m_spec.category; }
    bool isAdjustment() const override { return m_spec.adjustment; }
    // Con configure() el diálogo lo pone el complemento (aquí, sin ventana); si no, el de Kaint.
    QList<EffectParam> params() const override { return m_spec.configure ? QList<EffectParam>() : m_params; }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_pkg->name; }
    QImage iconImage() const override { return m_spec.icon; }
    bool hasCustomDialog() const override { return m_spec.configure; }
    bool showCustomDialog(const QImage& src, const QRect& roi, const EffectContext& ctx, QString* error) const override;
    QString takeError() const override
    {
        const std::lock_guard lock(m_mutex);
        return std::exchange(m_error, QString());
    }
    void render(const RenderArgs& args) const override;
    void renderPixels(const RenderArgs& args, const QByteArray& valuesJson) const;

    // Valores de Kaint → los que espera el paquete (cadenas para las listas, #rrggbb los colores).
    QJsonObject pluginValues(const ParamValues& given) const
    {
        const ParamValues values = [&] {
            ParamValues v = Effects::defaults(*this);
            for (auto it = given.cbegin(); it != given.cend(); ++it)
                v.insert(it.key(), it.value());
            return v;
        }();
        QJsonObject out;
        for (const EffectParam& p : m_params) {
            const QVariant v = values.value(p.key);
            switch (p.type) {
            case EffectParam::Type::Int: out.insert(p.key, v.toInt()); break;
            case EffectParam::Type::Bool: out.insert(p.key, v.toBool()); break;
            case EffectParam::Type::Choice: {
                const QStringList list = m_choices.value(p.key);
                out.insert(p.key, list.value(v.toInt(), list.value(int(p.def))));
                break;
            }
            case EffectParam::Type::Color: out.insert(p.key, v.value<QColor>().name(QColor::HexRgb)); break;
            case EffectParam::Type::Text: out.insert(p.key, v.toString()); break;
            default: out.insert(p.key, v.toDouble()); break;
            }
        }
        return out;
    }

    // Los del paquete → los de Kaint (paintDotJs.effects.apply con un efecto de un complemento).
    ParamValues kaintValues(const QJsonObject& given) const
    {
        ParamValues out = Effects::defaults(*this);
        for (const EffectParam& p : m_params) {
            if (!given.contains(p.key))
                continue;
            const QJsonValue v = given.value(p.key);
            if (p.type == EffectParam::Type::Choice)
                out.insert(p.key, std::max<int>(0, int(m_choices.value(p.key).indexOf(v.toVariant().toString()))));
            else if (p.type == EffectParam::Type::Color)
                out.insert(p.key, colorFromJson(v, Qt::black));
            else
                out.insert(p.key, v.toVariant());
        }
        return out;
    }

private:
    void fail(const QString& message) const
    {
        const std::lock_guard lock(m_mutex);
        m_error = message;
    }

    PackagePtr m_pkg;
    EffectSpec m_spec;
    QList<EffectParam> m_params;
    QHash<QString, QStringList> m_choices;
    mutable std::mutex m_mutex;
    mutable QString m_error;
    // Lo que devolvió configure() (en JSON): son los valores con los que se calcula.
    mutable QByteArray m_configured = QByteArrayLiteral("{}");
};

QString browserOnlyMessage(const QString& name)
{
    return QObject::tr("«%1» abre una ventana propia hecha para el navegador, que la versión de escritorio de Kaint no puede "
                       "mostrar. Úsalo en la versión web de Kaint.")
        .arg(name);
}

bool PaintJsEffect::showCustomDialog(const QImage& src, const QRect& roi, const EffectContext&, QString* error) const
{
    Sandbox box(m_pkg);
    QString message;
    if (!box.load(&message)) {
        *error = message;
        return false;
    }
    JSContext* ctx = box.ctx();
    JSValue name = newString(ctx, m_spec.id);
    JSValue def = box.call("effect", {box.definition(), name}, &message);
    JS_FreeValue(ctx, name);
    if (!JS_IsObject(def)) {
        JS_FreeValue(ctx, def);
        *error = message.isEmpty() ? QObject::tr("El complemento ya no tiene el efecto %1.").arg(m_spec.name) : message;
        return false;
    }
    const QByteArray rgba = rgbaBytes(src);
    JSValue source = newClampedArray(ctx, reinterpret_cast<const uchar*>(rgba.constData()), size_t(rgba.size()));
    JSValue w = JS_NewInt32(ctx, src.width()), h = JS_NewInt32(ctx, src.height());
    const QByteArray valuesJson = [&] {
        const std::lock_guard lock(m_mutex);
        return m_configured == "{}" ? QJsonDocument(pluginValues({})).toJson(QJsonDocument::Compact) : m_configured;
    }();
    JSValue values = newString(ctx, QString::fromUtf8(valuesJson));
    JSValue bounds = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, bounds, "x", JS_NewInt32(ctx, roi.x()));
    JS_SetPropertyStr(ctx, bounds, "y", JS_NewInt32(ctx, roi.y()));
    JS_SetPropertyStr(ctx, bounds, "width", JS_NewInt32(ctx, roi.width()));
    JS_SetPropertyStr(ctx, bounds, "height", JS_NewInt32(ctx, roi.height()));
    JSValue title = newString(ctx, m_pkg->dialogTitle.isEmpty() ? m_spec.name : m_pkg->dialogTitle);
    JSValue icon = newString(ctx, QString());
    bool browserOnly = false;
    JSValue result = box.call("configure", {def, source, w, h, values, bounds, title, icon}, &message, &browserOnly);
    for (JSValue v : {def, source, values, bounds, title, icon})
        JS_FreeValue(ctx, v);
    if (JS_IsException(result)) {
        *error = browserOnly ? browserOnlyMessage(m_spec.name) : message;
        return false;
    }
    // null (o nada): se canceló.
    if (JS_IsNull(result) || JS_IsUndefined(result)) {
        JS_FreeValue(ctx, result);
        return false;
    }
    JSValue json = box.call("json", {result}, &message);
    JS_FreeValue(ctx, result);
    const QString text = JS_IsString(json) ? jsString(ctx, json) : QStringLiteral("{}");
    JS_FreeValue(ctx, json);
    const std::lock_guard lock(m_mutex);
    m_configured = text.toUtf8();
    return true;
}

// renderPixel(): por bloques de filas en paralelo, cada uno en su propio entorno (QuickJS no tiene
// JIT; así, al menos, se usan todos los núcleos).
void PaintJsEffect::renderPixels(const RenderArgs& args, const QByteArray& valuesJson) const
{
    const QRect roi = args.roi;
    const int w = args.src.width(), h = args.src.height();
    const QByteArray rgba = rgbaBytes(args.src);
    uchar* const dst = args.dst.bits();
    const qsizetype stride = args.dst.bytesPerLine();
    const QImage::Format format = args.dst.format();
    const int cores = int(std::max(1u, std::thread::hardware_concurrency()));
    std::mutex errorMutex;
    QString firstError;
    auto record = [&](const QString& message) {
        const std::lock_guard lock(errorMutex);
        if (firstError.isEmpty())
            firstError = message;
    };
    parallelRows(
        roi.top(), roi.bottom() + 1,
        [&](int y0, int y1) {
            Sandbox box(m_pkg, &args.cancel);
            QString message;
            if (!box.load(&message)) {
                record(message);
                return;
            }
            JSContext* ctx = box.ctx();
            JSValue name = newString(ctx, m_spec.id);
            JSValue def = box.call("effect", {box.definition(), name}, &message);
            JS_FreeValue(ctx, name);
            if (!JS_IsObject(def)) {
                JS_FreeValue(ctx, def);
                record(message.isEmpty() ? QObject::tr("El complemento ya no tiene el efecto %1.").arg(m_spec.name) : message);
                return;
            }
            const int rows = y1 - y0;
            JSValue source = newClampedArray(ctx, reinterpret_cast<const uchar*>(rgba.constData()) + qsizetype(y0) * w * 4,
                                             size_t(rows) * size_t(w) * 4);
            JSValue values = newString(ctx, QString::fromUtf8(valuesJson));
            JSValue rect = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rect, "x", JS_NewInt32(ctx, roi.x()));
            JS_SetPropertyStr(ctx, rect, "y", JS_NewInt32(ctx, y0));
            JS_SetPropertyStr(ctx, rect, "w", JS_NewInt32(ctx, roi.width()));
            JS_SetPropertyStr(ctx, rect, "h", JS_NewInt32(ctx, rows));
            JSValue jw = JS_NewInt32(ctx, w), jh = JS_NewInt32(ctx, h);
            JSValue result = box.call("renderPixels", {def, source, jw, jh, values, rect}, &message);
            for (JSValue v : {def, source, values, rect})
                JS_FreeValue(ctx, v);
            if (JS_IsException(result)) {
                if (!args.cancel)
                    record(message.isEmpty() ? QObject::tr("El complemento se detuvo.") : message);
                return;
            }
            size_t size = 0;
            const uint8_t* out = viewBytes(ctx, result, &size);
            if (out && size == size_t(rows) * size_t(w) * 4) {
                const QImage block = QImage(out, w, rows, w * 4, QImage::Format_RGBA8888).convertToFormat(format);
                for (int y = 0; y < rows; ++y)
                    std::memcpy(dst + (y0 + y) * stride + qsizetype(roi.left()) * 4, block.constScanLine(y) + qsizetype(roi.left()) * 4,
                                size_t(roi.width()) * 4);
            }
            JS_FreeValue(ctx, result);
        },
        std::max(16, (roi.height() + cores - 1) / cores));
    if (!firstError.isEmpty())
        fail(firstError);
    else if (args.ctx.progress)
        args.ctx.progress->done = args.ctx.progress->total.load();
}

void PaintJsEffect::render(const RenderArgs& args) const
{
    // Con configure(), los valores son lo que devolvió; si no, los del diálogo de Kaint.
    const QByteArray valuesJson = [&] {
        if (!m_spec.configure)
            return QJsonDocument(pluginValues(args.values)).toJson(QJsonDocument::Compact);
        const std::lock_guard lock(m_mutex);
        return m_configured;
    }();
    if (m_spec.pixel) {
        renderPixels(args, valuesJson);
        return;
    }
    Sandbox box(m_pkg, &args.cancel);
    QString message;
    if (!box.load(&message)) {
        if (!args.cancel)
            fail(message);
        return;
    }
    JSContext* ctx = box.ctx();
    const int w = args.src.width(), h = args.src.height();
    JSValue name = newString(ctx, m_spec.id);
    JSValue def = box.call("effect", {box.definition(), name}, &message);
    JS_FreeValue(ctx, name);
    if (!JS_IsObject(def)) {
        JS_FreeValue(ctx, def);
        fail(message.isEmpty() ? QObject::tr("El complemento ya no tiene el efecto %1.").arg(m_spec.name) : message);
        return;
    }
    const QByteArray rgba = rgbaBytes(args.src);
    JSValue source = newClampedArray(ctx, reinterpret_cast<const uchar*>(rgba.constData()), size_t(rgba.size()));
    JSValue values = newString(ctx, QString::fromUtf8(valuesJson));
    // La selección como máscara (0-255 por píxel) y su rectángulo, como en la web.
    JSValue selection = JS_NULL;
    const Selection* sel = args.selection;
    if (sel && !sel->isEmpty() && sel->mask().size() == args.src.size()) {
        const QImage mask = sel->mask().format() == QImage::Format_Alpha8 ? sel->mask() : sel->mask().convertToFormat(QImage::Format_Alpha8);
        QByteArray bytes(qsizetype(w) * h, Qt::Uninitialized);
        for (int y = 0; y < h; ++y)
            std::memcpy(bytes.data() + qsizetype(y) * w, mask.constScanLine(y), size_t(w));
        selection = JS_NewUint8ArrayCopy(ctx, reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));
    }
    auto rectObject = [&](const QRect& r, bool short_) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "x", JS_NewInt32(ctx, r.x()));
        JS_SetPropertyStr(ctx, o, "y", JS_NewInt32(ctx, r.y()));
        JS_SetPropertyStr(ctx, o, short_ ? "w" : "width", JS_NewInt32(ctx, r.width()));
        JS_SetPropertyStr(ctx, o, short_ ? "h" : "height", JS_NewInt32(ctx, r.height()));
        return o;
    };
    JSValue bounds = rectObject(args.roi, false), rect = rectObject(args.roi, true);
    JSValue jw = JS_NewInt32(ctx, w), jh = JS_NewInt32(ctx, h);
    JSValue result = box.call("render", {def, source, jw, jh, values, selection, bounds, rect}, &message);
    for (JSValue v : {def, source, values, selection, bounds, rect})
        JS_FreeValue(ctx, v);
    if (JS_IsException(result)) {
        if (!args.cancel)
            fail(message.isEmpty() ? QObject::tr("El complemento se detuvo.") : message);
        return;
    }
    size_t size = 0;
    const uint8_t* out = viewBytes(ctx, result, &size);
    if (!out || size != size_t(w) * size_t(h) * 4) {
        JS_FreeValue(ctx, result);
        fail(QObject::tr("%1: render() devolvió un valor que no vale.").arg(m_spec.name));
        return;
    }
    // Solo lo de dentro del rectángulo del efecto (fuera, la imagen queda como estaba).
    const QImage image = imageFromRgba(out, w, h);
    const QImage converted = image.convertToFormat(args.dst.format());
    for (int y = args.roi.top(); y <= args.roi.bottom(); ++y)
        std::memcpy(args.dst.scanLine(y) + qsizetype(args.roi.left()) * 4, converted.constScanLine(y) + qsizetype(args.roi.left()) * 4,
                    size_t(args.roi.width()) * 4);
    JS_FreeValue(ctx, result);
    if (args.ctx.progress)
        args.ctx.progress->done = args.ctx.progress->total.load();
}

ParamValues kaintValuesFor(const Effect& effect, const QJsonObject& given)
{
    if (const auto* own = dynamic_cast<const PaintJsEffect*>(&effect))
        return own->kaintValues(given);
    ParamValues out = Effects::defaults(effect);
    for (const EffectParam& p : effect.params()) {
        if (!given.contains(p.key))
            continue;
        const QJsonValue v = given.value(p.key);
        switch (p.type) {
        case EffectParam::Type::Int:
        case EffectParam::Type::Choice:
        case EffectParam::Type::Seed: out.insert(p.key, v.toInt(out.value(p.key).toInt())); break;
        case EffectParam::Type::Double:
        case EffectParam::Type::Angle: out.insert(p.key, v.toDouble(out.value(p.key).toDouble())); break;
        case EffectParam::Type::Bool: out.insert(p.key, v.toBool(out.value(p.key).toBool())); break;
        case EffectParam::Type::Color: out.insert(p.key, colorFromJson(v, out.value(p.key).value<QColor>())); break;
        case EffectParam::Type::Text: out.insert(p.key, v.toString()); break;
        case EffectParam::Type::Offset: {
            const QJsonObject o = v.toObject();
            const QJsonArray a = v.toArray();
            out.insert(p.key, v.isArray() ? QPointF(a.at(0).toDouble(), a.at(1).toDouble())
                                          : QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble()));
            break;
        }
        default: break;
        }
    }
    return out;
}

// Categoría de Kaint de la de un efecto del paquete (como en la web: las que ya existen, una
// nueva con ese nombre o, con "Root", directamente en Efectos).
QString categoryFor(const QString& name)
{
    static const QHash<QString, QString> known{
        {QStringLiteral("artistic"), QStringLiteral("Artistic")}, {QStringLiteral("blurs"), QStringLiteral("Blurs")},
        {QStringLiteral("color"), QStringLiteral("Color")},       {QStringLiteral("distort"), QStringLiteral("Distort")},
        {QStringLiteral("noise"), QStringLiteral("Noise")},       {QStringLiteral("object"), QStringLiteral("Object")},
        {QStringLiteral("photo"), QStringLiteral("Photo")},       {QStringLiteral("render"), QStringLiteral("Render")},
        {QStringLiteral("stylize"), QStringLiteral("Stylize")},
    };
    const QString key = name.trimmed().toLower();
    if (known.contains(key))
        return PdnEffect::kaintCategory(known.value(key));
    if (key.isEmpty() || key == QLatin1String("plugins"))
        return QObject::tr("Complementos");
    return name.trimmed().left(48);
}

// --- Los formatos -----------------------------------------------------------------------------

struct Registered {
    FileType type;
    PackagePtr pkg;
    QString localId;
    QJsonObject saveDefaults;
};

std::mutex g_typesMutex;
QList<Registered>& registry()
{
    static QList<Registered> list;
    return list;
}

// Los formatos leídos al cargar (con su paquete), hasta que se registran.
std::mutex g_pendingMutex;
QHash<QString, QList<Registered>>& pending()
{
    static QHash<QString, QList<Registered>> map;
    return map;
}

std::optional<Registered> registeredFor(const FileType& type)
{
    const std::lock_guard lock(g_typesMutex);
    for (const Registered& r : registry())
        if (r.type.plugin == type.plugin && r.type.id == type.id)
            return r;
    return std::nullopt;
}

} // namespace

QString FileType::filter() const
{
    QStringList patterns;
    for (const QString& e : extensions)
        patterns << QLatin1Char('*') + e;
    return QStringLiteral("%1 (%2)").arg(name, patterns.join(QLatin1Char(' ')));
}

bool isPackage(const QString& path)
{
    return path.endsWith(QLatin1String(".paintjs"), Qt::CaseInsensitive);
}

Info load(const QString& path)
{
    Info info;
    QString error;
    const PackagePtr pkg = readPackage(path, &error);
    if (!pkg) {
        info.errors << error;
        return info;
    }
    info.id = pkg->id;
    info.name = pkg->name;
    info.version = pkg->version;
    info.author = pkg->author;
    info.copyright = pkg->copyright;
    info.website = pkg->website;
    info.description = pkg->description;
    info.license = pkg->license;

    Sandbox box(pkg, nullptr, kLoadTimeoutMs);
    if (!box.load(&error)) {
        info.errors << error;
        return info;
    }
    JSContext* ctx = box.ctx();
    JSValue described = box.call("describe", {box.definition()}, &error);
    if (JS_IsException(described)) {
        info.errors << error;
        return info;
    }
    const QJsonObject spec = QJsonDocument::fromJson(jsString(ctx, described).toUtf8()).object();
    JS_FreeValue(ctx, described);

    for (const QJsonValue& v : spec.value(QStringLiteral("effects")).toArray()) {
        const QJsonObject e = v.toObject();
        EffectSpec s;
        s.id = e.value(QStringLiteral("id")).toString();
        if (!validId(s.id)) {
            info.errors << QObject::tr("%1 no es válido.").arg(QObject::tr("El identificador del efecto"));
            continue;
        }
        s.name = text(e.value(QStringLiteral("name")), s.id, 80);
        if (e.value(QStringLiteral("render")).toBool() == e.value(QStringLiteral("renderPixel")).toBool()) {
            info.errors << QObject::tr("El efecto %1 debe definir render() o renderPixel().").arg(s.name);
            continue;
        }
        s.adjustment = e.value(QStringLiteral("menu")).toString() == QLatin1String("Adjustments");
        const QJsonValue category = e.value(QStringLiteral("category"));
        const QString categoryName = category.isObject() ? category.toObject().value(QStringLiteral("name")).toString() : category.toString();
        if (!s.adjustment && categoryName != QLatin1String("Root"))
            s.category = categoryFor(categoryName);
        s.icon = imageFromDataUrl(e.value(QStringLiteral("icon")).toString());
        s.configure = e.value(QStringLiteral("configure")).toBool();
        s.pixel = e.value(QStringLiteral("renderPixel")).toBool();
        QHash<QString, QStringList> choices;
        QString controlError;
        QList<EffectParam> params = paramsFor(e.value(QStringLiteral("controls")).toArray(), &choices, &controlError);
        if (!controlError.isEmpty()) {
            info.errors << QStringLiteral("%1: %2").arg(s.name, controlError);
            continue;
        }
        auto* effect = new PaintJsEffect(pkg, s, std::move(params), std::move(choices));
        if (s.configure) {
            // configure() sin ventana (devuelve los valores enseguida) funciona aquí; con una ventana
            // propia en HTML, solo en la web.
            JSValue name = newString(ctx, s.id);
            JSValue def = box.call("effect", {box.definition(), name}, &error);
            JS_FreeValue(ctx, name);
            JSValue values = newString(ctx, QString::fromUtf8(QJsonDocument(effect->pluginValues({})).toJson(QJsonDocument::Compact)));
            bool browserOnly = false;
            JSValue result = box.call("dryRun", {def, values}, &error, &browserOnly);
            JS_FreeValue(ctx, def);
            JS_FreeValue(ctx, values);
            JS_FreeValue(ctx, result);
            if (browserOnly) {
                info.browserOnly << s.name;
                info.browserOnlyIcons << s.icon;
                delete effect;
                continue;
            }
        }
        info.effects << effect;
    }

    QList<Registered> types;
    static const QRegularExpression extension(QStringLiteral("^\\.[a-z0-9]{1,12}$"));
    static const QStringList reserved{QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".webp"),
                                      QStringLiteral(".gif"), QStringLiteral(".bmp"), QStringLiteral(".ora"), QStringLiteral(".pdn"),
                                      QStringLiteral(".zip"), QStringLiteral(".paintjs")};
    for (const QJsonValue& v : spec.value(QStringLiteral("fileTypes")).toArray()) {
        const QJsonObject t = v.toObject();
        const QString id = t.value(QStringLiteral("id")).toString();
        if (!validId(id)) {
            info.errors << QObject::tr("%1 no es válido.").arg(QObject::tr("El identificador del formato"));
            continue;
        }
        Registered r;
        r.pkg = pkg;
        r.localId = id;
        r.type.plugin = path;
        r.type.id = QStringLiteral("plugin.%1.%2").arg(pkg->id, id);
        r.type.name = text(t.value(QStringLiteral("name")), id, 80);
        r.type.canRead = t.value(QStringLiteral("decode")).toBool();
        r.type.canWrite = t.value(QStringLiteral("encode")).toBool();
        r.type.layers = t.value(QStringLiteral("supportsLayers")).toBool();
        bool valid = true;
        for (const QJsonValue& e : t.value(QStringLiteral("extensions")).toArray()) {
            const QString ext = e.toString().toLower();
            if (extension.match(ext).hasMatch())
                r.type.extensions << ext;
            if (reserved.contains(ext))
                valid = false;
        }
        if (!r.type.canRead && !r.type.canWrite) {
            info.errors << QObject::tr("El formato %1 debe definir decode() o encode().").arg(id);
            continue;
        }
        if (!valid || r.type.extensions.isEmpty()) {
            info.errors << QObject::tr("El formato %1 no tiene extensiones válidas.").arg(id);
            continue;
        }
        QHash<QString, QStringList> choices;
        QString controlError;
        const QList<EffectParam> saveParams = paramsFor(t.value(QStringLiteral("saveControls")).toArray(), &choices, &controlError);
        for (const EffectParam& p : saveParams) {
            if (p.type == EffectParam::Type::Choice)
                r.saveDefaults.insert(p.key, choices.value(p.key).value(int(p.def)));
            else if (p.type == EffectParam::Type::Color)
                r.saveDefaults.insert(p.key, p.defaultValue.value<QColor>().name(QColor::HexRgb));
            else if (p.type == EffectParam::Type::Bool)
                r.saveDefaults.insert(p.key, p.def != 0);
            else if (p.type == EffectParam::Type::Text)
                r.saveDefaults.insert(p.key, p.defaultValue.toString());
            else
                r.saveDefaults.insert(p.key, p.def);
        }
        info.fileTypes << r.type;
        types << r;
    }
    {
        const std::lock_guard lock(g_pendingMutex);
        pending().insert(path, types);
    }
    if (info.effects.isEmpty() && info.browserOnly.isEmpty() && info.fileTypes.isEmpty() && info.errors.isEmpty())
        info.errors << QObject::tr("Un complemento debe añadir al menos un efecto o un formato.");
    return info;
}

bool registerFileTypes(const QString& plugin, const QList<FileType>& types, QString* conflict)
{
    QList<Registered> found;
    {
        const std::lock_guard lock(g_pendingMutex);
        found = pending().value(plugin);
    }
    const std::lock_guard lock(g_typesMutex);
    for (const FileType& t : types)
        for (const Registered& other : std::as_const(registry()))
            if (other.type.plugin != plugin)
                for (const QString& e : t.extensions)
                    if (other.type.extensions.contains(e)) {
                        if (conflict)
                            *conflict = e;
                        return false;
                    }
    registry().erase(std::remove_if(registry().begin(), registry().end(), [&](const Registered& r) { return r.type.plugin == plugin; }),
                     registry().end());
    for (const FileType& t : types)
        for (const Registered& r : std::as_const(found))
            if (r.type.id == t.id)
                registry() << r;
    return true;
}

void unregisterFileTypes(const QString& plugin)
{
    const std::lock_guard lock(g_typesMutex);
    registry().erase(std::remove_if(registry().begin(), registry().end(), [&](const Registered& r) { return r.type.plugin == plugin; }),
                     registry().end());
}

QList<FileType> fileTypes()
{
    const std::lock_guard lock(g_typesMutex);
    QList<FileType> out;
    for (const Registered& r : std::as_const(registry()))
        out << r.type;
    return out;
}

std::optional<FileType> fileTypeFor(const QString& path, bool forWriting)
{
    const QString lower = path.toLower();
    const std::lock_guard lock(g_typesMutex);
    for (const Registered& r : std::as_const(registry())) {
        if (forWriting ? !r.type.canWrite : !r.type.canRead)
            continue;
        for (const QString& e : r.type.extensions)
            if (lower.endsWith(e))
                return r.type;
    }
    return std::nullopt;
}

Document* decode(const FileType& type, const QString& path, QString* error)
{
    const std::optional<Registered> reg = registeredFor(type);
    if (!reg) {
        *error = QObject::tr("El complemento de este formato ya no está activado.");
        return nullptr;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return nullptr;
    }
    if (file.size() > kMaxImageFile) {
        *error = QObject::tr("El archivo es demasiado grande.");
        return nullptr;
    }
    const QByteArray data = file.readAll();
    Sandbox box(reg->pkg, nullptr, kFileTimeoutMs);
    if (!box.load(error))
        return nullptr;
    JSContext* ctx = box.ctx();
    JSValue id = newString(ctx, reg->localId);
    JSValue def = box.call("fileType", {box.definition(), id}, error);
    JS_FreeValue(ctx, id);
    if (!JS_IsObject(def)) {
        JS_FreeValue(ctx, def);
        return nullptr;
    }
    JSValue bytes = JS_NewUint8ArrayCopy(ctx, reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()));
    JSValue name = newString(ctx, QFileInfo(path).fileName());
    JSValue result = box.call("decode", {def, bytes, name}, error);
    JS_FreeValue(ctx, def);
    JS_FreeValue(ctx, bytes);
    JS_FreeValue(ctx, name);
    if (JS_IsException(result))
        return nullptr;
    JSValue image = box.call("decoded", {result}, error);
    JS_FreeValue(ctx, result);
    if (JS_IsException(image))
        return nullptr;
    auto number = [&](JSValueConst obj, const char* key) {
        JSValue v = JS_GetPropertyStr(ctx, obj, key);
        double d = 0;
        JS_ToFloat64(ctx, &d, v);
        JS_FreeValue(ctx, v);
        return d;
    };
    const int w = int(number(image, "width")), h = int(number(image, "height"));
    const QSize size(w, h);
    JSValue list = JS_GetPropertyStr(ctx, image, "layers");
    int64_t count = 0;
    JS_GetLength(ctx, list, &count);
    QList<LayerPtr> layers;
    for (int64_t i = 0; i < count; ++i) {
        JSValue l = JS_GetPropertyUint32(ctx, list, uint32_t(i));
        JSValue pixels = JS_GetPropertyStr(ctx, l, "data");
        size_t n = 0;
        const uint8_t* rgba = viewBytes(ctx, pixels, &n);
        if (rgba && n == size_t(w) * size_t(h) * 4) {
            JSValue nameValue = JS_GetPropertyStr(ctx, l, "name");
            JSValue blend = JS_GetPropertyStr(ctx, l, "blendMode");
            JSValue visible = JS_GetPropertyStr(ctx, l, "visible");
            QString layerName = jsString(ctx, nameValue);
            if (layerName.isEmpty())
                layerName = layers.isEmpty() ? QObject::tr("Fondo") : QObject::tr("Capa %1").arg(layers.size() + 1);
            auto layer = std::make_shared<Layer>(layerName, imageFromRgba(rgba, w, h));
            layer->props.visible = JS_ToBool(ctx, visible) > 0;
            layer->props.opacity = std::clamp(int(number(l, "opacity")), 0, 255);
            layer->props.blend = blendFromName(jsString(ctx, blend));
            layers << layer;
            JS_FreeValue(ctx, nameValue);
            JS_FreeValue(ctx, blend);
            JS_FreeValue(ctx, visible);
        }
        JS_FreeValue(ctx, pixels);
        JS_FreeValue(ctx, l);
    }
    JS_FreeValue(ctx, list);
    const int active = int(number(image, "activeLayer"));
    const double resolution = number(image, "resolution");
    JS_FreeValue(ctx, image);
    if (layers.isEmpty()) {
        *error = QObject::tr("El complemento no devolvió ninguna capa.");
        return nullptr;
    }
    auto* doc = new Document(size);
    doc->replaceState(size, layers, std::clamp(active, 0, int(layers.size()) - 1));
    if (resolution > 0)
        doc->setDpi(resolution);
    doc->setHistoryRootLabel(QObject::tr("Abrir imagen"));
    return doc;
}

bool encode(const FileType& type, const Document* doc, const QString& path, QString* error)
{
    const std::optional<Registered> reg = registeredFor(type);
    if (!reg) {
        *error = QObject::tr("El complemento de este formato ya no está activado.");
        return false;
    }
    Sandbox box(reg->pkg, nullptr, kFileTimeoutMs);
    if (!box.load(error))
        return false;
    JSContext* ctx = box.ctx();
    JSValue id = newString(ctx, reg->localId);
    JSValue def = box.call("fileType", {box.definition(), id}, error);
    JS_FreeValue(ctx, id);
    if (!JS_IsObject(def)) {
        JS_FreeValue(ctx, def);
        return false;
    }
    // Como encodePlugin en la web: las capas (o la imagen combinada) en RGBA.
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, document, "width", JS_NewInt32(ctx, doc->size().width()));
    JS_SetPropertyStr(ctx, document, "height", JS_NewInt32(ctx, doc->size().height()));
    JS_SetPropertyStr(ctx, document, "resolution", JS_NewFloat64(ctx, doc->dpi()));
    JS_SetPropertyStr(ctx, document, "activeLayer", JS_NewInt32(ctx, reg->type.layers ? doc->activeIndex() : 0));
    JSValue layers = JS_NewArray(ctx);
    auto addLayer = [&](uint32_t index, const QString& name, bool visible, int opacity, BlendMode blend, const QImage& image) {
        JSValue l = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, l, "name", newString(ctx, name));
        JS_SetPropertyStr(ctx, l, "visible", JS_NewBool(ctx, visible));
        JS_SetPropertyStr(ctx, l, "opacity", JS_NewInt32(ctx, opacity));
        JS_SetPropertyStr(ctx, l, "blendMode", newString(ctx, blendName(blend)));
        JS_SetPropertyStr(ctx, l, "isBackground", JS_NewBool(ctx, index == 0));
        const QByteArray rgba = rgbaBytes(image);
        JS_SetPropertyStr(ctx, l, "data", newClampedArray(ctx, reinterpret_cast<const uchar*>(rgba.constData()), size_t(rgba.size())));
        JS_SetPropertyUint32(ctx, layers, index, l);
    };
    if (reg->type.layers) {
        uint32_t i = 0;
        for (const LayerPtr& layer : doc->layers())
            addLayer(i++, layer->props.name, layer->props.visible, layer->props.opacity, layer->props.blend, layer->image);
    } else {
        addLayer(0, doc->layerCount() ? doc->layer(0)->props.name : QString(), true, 255, BlendMode::Normal, doc->flattened());
    }
    JS_SetPropertyStr(ctx, document, "layers", layers);
    JSValue values = newString(ctx, QString::fromUtf8(QJsonDocument(reg->saveDefaults).toJson(QJsonDocument::Compact)));
    JSValue result = box.call("encode", {def, document, values}, error);
    JS_FreeValue(ctx, def);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, values);
    if (JS_IsException(result))
        return false;
    JSValue bytes = box.call("bytes", {result}, error);
    JS_FreeValue(ctx, result);
    if (JS_IsException(bytes))
        return false;
    size_t n = 0;
    const uint8_t* data = viewBytes(ctx, bytes, &n);
    QSaveFile file(path);
    const bool ok = data && file.open(QIODevice::WriteOnly) && file.write(reinterpret_cast<const char*>(data), qint64(n)) == qint64(n)
                    && file.commit();
    JS_FreeValue(ctx, bytes);
    if (!ok)
        *error = data ? file.errorString() : QObject::tr("encode() no devolvió datos.");
    return ok;
}

QString evaluateForTesting(const QString& code)
{
    Sandbox box;
    if (!box.ready())
        return QStringLiteral("Error: no engine");
    QString error;
    JSValue v = box.evaluate(code.toUtf8(), "<test>", &error);
    if (JS_IsException(v))
        return QStringLiteral("Error: ") + error;
    const QString out = jsString(box.ctx(), v);
    JS_FreeValue(box.ctx(), v);
    return out;
}

} // namespace PaintJs

// Formas personalizadas de Paint.NET (.xaml), como src/core/CustomShapes.cpp de Kaint: un
// SimpleGeometryShape con geometría de WPF (paintdotnet.github.io/apidocs, "Custom Shapes"). Se
// guardan en este navegador y aparecen en el grupo «Formas personalizadas» de la herramienta Formas.
//
// Un trazado es { rule: "evenodd" | "nonzero", subpaths: [[["M", x, y], ["L", x, y],
// ["C", x1, y1, x2, y2, x, y], ["Q", x1, y1, x, y], ["Z"]], ...] }.
import { context, createCanvas } from "./Raster.js";
import { smoothContour } from "./Selection.js";
import { remember } from "../Settings.js";
import { tr } from "../i18n.js";

const emptyPath = (rule = "evenodd") => ({ rule, subpaths: [] });

class PathBuilder {
    constructor(rule = "evenodd") {
        this.path = emptyPath(rule);
        this.current = null;
    }

    moveTo(x, y) {
        this.current = [["M", x, y]];
        this.start = { x, y };
        this.path.subpaths.push(this.current);
    }

    // Tras cerrar un contorno, el siguiente empieza donde empezaba ese (como QPainterPath).
    ensure() {
        if (!this.current)
            this.moveTo(this.start?.x ?? 0, this.start?.y ?? 0);
    }

    lineTo(x, y) {
        this.ensure();
        this.current.push(["L", x, y]);
    }

    cubicTo(x1, y1, x2, y2, x, y) {
        this.ensure();
        this.current.push(["C", x1, y1, x2, y2, x, y]);
    }

    quadTo(x1, y1, x, y) {
        this.ensure();
        this.current.push(["Q", x1, y1, x, y]);
    }

    close() {
        if (this.current && this.current.at(-1)[0] !== "Z") {
            this.current.push(["Z"]);
            const [, x, y] = this.current[0];
            this.current = null;
            this.start = { x, y };
        }
    }

    addPath(p) {
        this.path.subpaths.push(...p.subpaths.map((s) => s.map((c) => [...c])));
        this.current = null;
    }
}

// --- Sintaxis de trazados -----------------------------------------------------------------

class MarkupReader {
    constructor(text) {
        this.s = text;
        this.i = 0;
    }

    skip() {
        while (this.i < this.s.length && (/\s/.test(this.s[this.i]) || this.s[this.i] === ","))
            ++this.i;
    }

    atEnd() {
        this.skip();
        return this.i >= this.s.length;
    }

    atNumber() {
        this.skip();
        return this.i < this.s.length && /[0-9+\-.]/.test(this.s[this.i]);
    }

    command() {
        this.skip();
        return this.i < this.s.length && /\p{L}/u.test(this.s[this.i]) ? this.s[this.i++] : null;
    }

    number() {
        if (!this.atNumber())
            return null;
        const start = this.i, s = this.s;
        if (s[this.i] === "-" || s[this.i] === "+")
            ++this.i;
        let dot = false;
        while (this.i < s.length && (/[0-9]/.test(s[this.i]) || (!dot && s[this.i] === "."))) {
            dot ||= s[this.i] === ".";
            ++this.i;
        }
        if (this.i < s.length && (s[this.i] === "e" || s[this.i] === "E")) {
            let j = this.i + 1;
            if (j < s.length && (s[j] === "-" || s[j] === "+"))
                ++j;
            if (j < s.length && /[0-9]/.test(s[j])) {
                this.i = j;
                while (this.i < s.length && /[0-9]/.test(s[this.i]))
                    ++this.i;
            }
        }
        const text = s.slice(start, this.i), v = Number(text);
        if (!text || text === "-" || text === "+" || text === "." || !Number.isFinite(v)) {
            this.i = start;
            return null;
        }
        return v;
    }
}

const angleBetween = (ux, uy, vx, vy) => Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);

// Arco elíptico de WPF/SVG (por sus extremos) convertido en curvas de Bézier. 'clockwise' es el
// sentido horario en pantalla (y hacia abajo).
function arcTo(b, from, rx, ry, angleDeg, largeArc, clockwise, to) {
    if (from.x === to.x && from.y === to.y)
        return;
    rx = Math.abs(rx);
    ry = Math.abs(ry);
    if (rx < 1e-12 || ry < 1e-12) {
        b.lineTo(to.x, to.y);
        return;
    }
    const phi = (angleDeg * Math.PI) / 180, cs = Math.cos(phi), sn = Math.sin(phi);
    const dx2 = (from.x - to.x) / 2, dy2 = (from.y - to.y) / 2;
    const x1 = cs * dx2 + sn * dy2, y1 = -sn * dx2 + cs * dy2;
    const lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        rx *= Math.sqrt(lambda);
        ry *= Math.sqrt(lambda);
    }
    const num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    const coef = (largeArc !== clockwise ? 1 : -1) * Math.sqrt(Math.max(0, den > 0 ? num / den : 0));
    const cxp = (coef * rx * y1) / ry, cyp = (-coef * ry * x1) / rx;
    const cx = cs * cxp - sn * cyp + (from.x + to.x) / 2, cy = sn * cxp + cs * cyp + (from.y + to.y) / 2;
    const ux = (x1 - cxp) / rx, uy = (y1 - cyp) / ry, vx = (-x1 - cxp) / rx, vy = (-y1 - cyp) / ry;
    const theta = angleBetween(1, 0, ux, uy);
    let sweep = angleBetween(ux, uy, vx, vy);
    if (!clockwise && sweep > 0)
        sweep -= 2 * Math.PI;
    else if (clockwise && sweep < 0)
        sweep += 2 * Math.PI;
    const segments = Math.max(1, Math.ceil(Math.abs(sweep) / (Math.PI / 2) - 1e-9));
    const delta = sweep / segments, t = (4 / 3) * Math.tan(delta / 4);
    const at = (a) => [cx + rx * Math.cos(a) * cs - ry * Math.sin(a) * sn, cy + rx * Math.cos(a) * sn + ry * Math.sin(a) * cs];
    const tangent = (a) => [-rx * Math.sin(a) * cs - ry * Math.cos(a) * sn, -rx * Math.sin(a) * sn + ry * Math.cos(a) * cs];
    for (let i = 0; i < segments; ++i) {
        const a1 = theta + i * delta, a2 = a1 + delta;
        const p1 = at(a1), p2 = at(a2), t1 = tangent(a1), t2 = tangent(a2);
        const end = i === segments - 1 ? [to.x, to.y] : p2;
        b.cubicTo(p1[0] + t * t1[0], p1[1] + t * t1[1], p2[0] - t * t2[0], p2[1] - t * t2[1], end[0], end[1]);
    }
}

// Curva (Catmull-Rom) que pasa por todos los puntos: PolyCurveSegment.
function curveThrough(b, start, points) {
    const p = [start, ...points];
    for (let i = 0; i + 1 < p.length; ++i) {
        const p0 = p[Math.max(0, i - 1)], p1 = p[i], p2 = p[i + 1], p3 = p[Math.min(p.length - 1, i + 2)];
        b.cubicTo(p1.x + (p2.x - p0.x) / 6, p1.y + (p2.y - p0.y) / 6, p2.x - (p3.x - p1.x) / 6, p2.y - (p3.y - p1.y) / 6, p2.x, p2.y);
    }
}

// Trazado con la sintaxis de WPF/SVG; lanza un error si el texto no es válido.
export function parsePathMarkup(data) {
    const b = new PathBuilder("evenodd"), r = new MarkupReader(data);
    let good = true, cur = { x: 0, y: 0 }, start = { x: 0, y: 0 }, lastCtrl = { x: 0, y: 0 }, last = null, cmd = null;
    const num = () => {
        const n = r.number();
        if (n === null)
            good = false;
        return n;
    };
    const pt = (rel) => {
        const x = num(), y = good ? num() : null;
        if (!good)
            return null;
        return rel ? { x: cur.x + x, y: cur.y + y } : { x, y };
    };
    while (good && !r.atEnd()) {
        if (!r.atNumber()) {
            cmd = r.command();
            if (!cmd) {
                good = false;
                break;
            }
            if (cmd === "F") {
                const rule = num();
                if (rule === null)
                    break;
                b.path.rule = rule !== 0 ? "nonzero" : "evenodd";
                continue;
            }
        } else if (!cmd) {
            good = false;
            break;
        }
        const rel = cmd === cmd.toLowerCase();
        switch (cmd.toUpperCase()) {
        case "M": {
            const p = pt(rel);
            if (!p)
                break;
            b.moveTo(p.x, p.y);
            cur = start = p;
            cmd = rel ? "l" : "L";
            break;
        }
        case "L": {
            const p = pt(rel);
            if (p) {
                b.lineTo(p.x, p.y);
                cur = p;
            }
            break;
        }
        case "H": {
            const x = num();
            if (good) {
                cur = { x: rel ? cur.x + x : x, y: cur.y };
                b.lineTo(cur.x, cur.y);
            }
            break;
        }
        case "V": {
            const y = num();
            if (good) {
                cur = { x: cur.x, y: rel ? cur.y + y : y };
                b.lineTo(cur.x, cur.y);
            }
            break;
        }
        case "C": {
            const c1 = pt(rel), c2 = c1 && pt(rel), p = c2 && pt(rel);
            if (p) {
                b.cubicTo(c1.x, c1.y, c2.x, c2.y, p.x, p.y);
                lastCtrl = c2;
                cur = p;
            }
            break;
        }
        case "S": {
            const c2 = pt(rel), p = c2 && pt(rel);
            if (p) {
                const smooth = /[CS]/i.test(last || "");
                const c1 = smooth ? { x: 2 * cur.x - lastCtrl.x, y: 2 * cur.y - lastCtrl.y } : cur;
                b.cubicTo(c1.x, c1.y, c2.x, c2.y, p.x, p.y);
                lastCtrl = c2;
                cur = p;
            }
            break;
        }
        case "Q": {
            const c = pt(rel), p = c && pt(rel);
            if (p) {
                b.quadTo(c.x, c.y, p.x, p.y);
                lastCtrl = c;
                cur = p;
            }
            break;
        }
        case "T": {
            const p = pt(rel);
            if (p) {
                const smooth = /[QT]/i.test(last || "");
                const c = smooth ? { x: 2 * cur.x - lastCtrl.x, y: 2 * cur.y - lastCtrl.y } : cur;
                b.quadTo(c.x, c.y, p.x, p.y);
                lastCtrl = c;
                cur = p;
            }
            break;
        }
        case "A": {
            const rx = num(), ry = good ? num() : 0, angle = good ? num() : 0, large = good ? num() : 0, sweep = good ? num() : 0;
            const p = good ? pt(rel) : null;
            if (p) {
                arcTo(b, cur, rx, ry, angle, large !== 0, sweep !== 0, p);
                cur = p;
            }
            break;
        }
        case "Z":
            b.close();
            cur = start;
            if (r.atNumber())
                good = false;
            break;
        default:
            good = false;
            break;
        }
        last = cmd;
    }
    if (!good)
        throw new Error(tr("trazado no válido: «%1»", data.slice(0, 40)));
    return b.path;
}

// --- Geometría --------------------------------------------------------------------------

// Matriz [a, b, c, d, e, f] (x' = a·x + c·y + e, y' = b·x + d·y + f), como QTransform(m11, m12,
// m21, m22, dx, dy). multiply(m, n): primero m y luego n (como m * n en Qt).
const IDENTITY = [1, 0, 0, 1, 0, 0];
function multiply(m, n) {
    return [m[0] * n[0] + m[1] * n[2], m[0] * n[1] + m[1] * n[3], m[2] * n[0] + m[3] * n[2], m[2] * n[1] + m[3] * n[3],
            m[4] * n[0] + m[5] * n[2] + n[4], m[4] * n[1] + m[5] * n[3] + n[5]];
}
const translate = (x, y) => [1, 0, 0, 1, x, y];

export function transformPath(path, m) {
    const map = (x, y) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]];
    return {
        rule: path.rule,
        subpaths: path.subpaths.map((s) => s.map(([k, ...v]) => {
            const out = [k];
            for (let i = 0; i < v.length; i += 2)
                out.push(...map(v[i], v[i + 1]));
            return out;
        })),
    };
}

// Rectángulo que ocupa el trazado (con los extremos reales de las curvas, como
// QPainterPath::boundingRect).
export function pathBounds(path) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    const add = (x, y) => {
        x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y);
    };
    const roots = (a, b, c) => {
        if (Math.abs(a) < 1e-12)
            return Math.abs(b) < 1e-12 ? [] : [-c / b];
        const d = b * b - 4 * a * c;
        if (d < 0)
            return [];
        const s = Math.sqrt(d);
        return [(-b + s) / (2 * a), (-b - s) / (2 * a)];
    };
    for (const sub of path.subpaths) {
        let px = 0, py = 0;
        for (const [k, ...v] of sub) {
            if (k === "M" || k === "L") {
                add(v[0], v[1]);
                [px, py] = v;
            } else if (k === "C") {
                const [ax, ay, bx, by, cx, cy] = v;
                add(cx, cy);
                // Extremos de cada eje: donde la derivada se anula.
                for (const [p0, p1, p2, p3] of [[px, ax, bx, cx], [py, ay, by, cy]])
                    for (const t of roots(3 * (-p0 + 3 * p1 - 3 * p2 + p3), 6 * (p0 - 2 * p1 + p2), 3 * (p1 - p0)))
                        if (t > 0 && t < 1) {
                            const u = 1 - t;
                            add(u * u * u * px + 3 * u * u * t * ax + 3 * u * t * t * bx + t * t * t * cx,
                                u * u * u * py + 3 * u * u * t * ay + 3 * u * t * t * by + t * t * t * cy);
                        }
                [px, py] = [cx, cy];
            } else if (k === "Q") {
                const [ax, ay, cx, cy] = v;
                add(cx, cy);
                for (const [p0, p1, p2] of [[px, ax, cx], [py, ay, cy]]) {
                    const den = p0 - 2 * p1 + p2;
                    if (Math.abs(den) > 1e-12) {
                        const t = (p0 - p1) / den;
                        if (t > 0 && t < 1) {
                            const u = 1 - t;
                            add(u * u * px + 2 * u * t * ax + t * t * cx, u * u * py + 2 * u * t * ay + t * t * cy);
                        }
                    }
                }
                [px, py] = [cx, cy];
            }
        }
    }
    return x1 < x0 ? null : { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
}

// La forma estirada hasta llenar el rectángulo, como las integradas (CustomShapes::fitted).
export function fittedPath(path, x, y, w, h) {
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    const b = pathBounds(path) || { x: 0, y: 0, w: 1, h: 1 };
    const sx = b.w > 1e-9 ? w / b.w : 1, sy = b.h > 1e-9 ? h / b.h : 1;
    return transformPath(path, multiply(multiply(translate(-b.x, -b.y), [sx, 0, 0, sy, 0, 0]), translate(x, y)));
}

// Añade el trazado al de 'ctx' (después, fill(path.rule) o stroke()).
export function tracePath(ctx, path) {
    for (const sub of path.subpaths)
        for (const [k, ...v] of sub) {
            if (k === "M") ctx.moveTo(v[0], v[1]);
            else if (k === "L") ctx.lineTo(v[0], v[1]);
            else if (k === "C") ctx.bezierCurveTo(...v);
            else if (k === "Q") ctx.quadraticCurveTo(...v);
            else if (k === "Z") ctx.closePath();
        }
}

// --- Operaciones con trazados ----------------------------------------------------------------

// El navegador no combina trazados: CombinedGeometry y WidenedGeometry se calculan sobre una
// cuadrícula fina (1024 píxeles en el lado mayor) y se vuelve a un contorno (con los exteriores y
// los agujeros en sentidos contrarios, como oriented() de Kaint). layers: funciones que dibujan en
// el contexto (en coordenadas de la geometría); combine: de los valores de cada capa a dentro/fuera.
const GRID = 1024;
function viaGrid(bounds, layers, combine) {
    if (!bounds || bounds.w <= 0 || bounds.h <= 0)
        return emptyPath("nonzero");
    const scale = GRID / Math.max(bounds.w, bounds.h);
    const W = Math.ceil(bounds.w * scale) + 4, H = Math.ceil(bounds.h * scale) + 4;
    const masks = layers.map((draw) => {
        const canvas = createCanvas(W, H), ctx = context(canvas);
        ctx.setTransform(scale, 0, 0, scale, 2 - bounds.x * scale, 2 - bounds.y * scale);
        draw(ctx);
        const d = ctx.getImageData(0, 0, W, H).data, m = new Uint8Array(W * H);
        for (let i = 0; i < m.length; ++i)
            m[i] = d[i * 4 + 3] >= 128 ? 1 : 0;
        return m;
    });
    const out = new Uint8Array(W * H);
    for (let i = 0; i < out.length; ++i)
        out[i] = combine(masks.map((m) => m[i])) ? 255 : 0;
    const b = new PathBuilder("nonzero");
    for (const loop of smoothContour(out, W, H, { x: 0, y: 0, w: W, h: H })) {
        for (let i = 0; i < loop.length; i += 2) {
            const x = (loop[i] - 2) / scale + bounds.x, y = (loop[i + 1] - 2) / scale + bounds.y;
            i ? b.lineTo(x, y) : b.moveTo(x, y);
        }
        b.close();
    }
    return b.path;
}

function unionBounds(a, b) {
    if (!a || !b)
        return a || b;
    const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y);
    return { x, y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y };
}

const filler = (path) => (ctx) => {
    ctx.beginPath();
    tracePath(ctx, path);
    ctx.fill(path.rule);
};

// --- XAML -------------------------------------------------------------------------------

const localName = (el) => el.localName;
const attr = (el, name) => (el.getAttribute(name) ?? "").trim();
const has = (el, name) => el.hasAttribute(name);

function numbers(text) {
    const r = new MarkupReader(text), list = [];
    for (let n = r.number(); n !== null; n = r.number())
        list.push(n);
    return list;
}

function toPoint(text, fallback = { x: 0, y: 0 }) {
    const n = numbers(text);
    return n.length >= 2 ? { x: n[0], y: n[1] } : fallback;
}

function toPoints(text) {
    const n = numbers(text), list = [];
    for (let i = 0; i + 1 < n.length; i += 2)
        list.push({ x: n[i], y: n[i + 1] });
    return list;
}

function toNumber(text, fallback) {
    const v = text === "" ? NaN : Number(text);
    return Number.isFinite(v) ? v : fallback;
}

function toBool(text, fallback) {
    if (!text)
        return fallback;
    return text.toLowerCase() === "true" || text === "1";
}

class Builder {
    constructor() {
        this.error = "";
    }

    // Elementos hijos que no son propiedades (p. ej. los PathFigure de un PathGeometry), también
    // dentro de su propiedad de contenido o de una colección.
    static items(n, contentProperty) {
        const list = [];
        for (const c of n.children) {
            const name = localName(c);
            if (name.endsWith(`.${contentProperty}`)) {
                for (const g of c.children)
                    list.push(...(localName(g).endsWith("Collection") ? Builder.items(g, contentProperty) : [g]));
            } else if (name.endsWith("Collection")) {
                list.push(...Builder.items(c, contentProperty));
            } else if (!name.includes(".")) {
                list.push(c);
            }
        }
        return list;
    }

    // Valor de una propiedad dada como elemento: <Tipo.Propiedad><Valor/></Tipo.Propiedad>.
    static property(n, prop) {
        for (const c of n.children)
            if (localName(c).endsWith(`.${prop}`) && c.children.length)
                return c.children[0];
        return null;
    }

    transform(t) {
        const cx = toNumber(attr(t, "CenterX"), 0), cy = toNumber(attr(t, "CenterY"), 0);
        const around = (m) => multiply(multiply(translate(-cx, -cy), m), translate(cx, cy));
        switch (localName(t)) {
        case "ScaleTransform":
            return around([toNumber(attr(t, "ScaleX"), 1), 0, 0, toNumber(attr(t, "ScaleY"), 1), 0, 0]);
        case "RotateTransform": {
            const a = (toNumber(attr(t, "Angle"), 0) * Math.PI) / 180;
            return around([Math.cos(a), Math.sin(a), -Math.sin(a), Math.cos(a), 0, 0]);
        }
        case "TranslateTransform":
            return translate(toNumber(attr(t, "X"), 0), toNumber(attr(t, "Y"), 0));
        case "SkewTransform": {
            const ax = (toNumber(attr(t, "AngleX"), 0) * Math.PI) / 180, ay = (toNumber(attr(t, "AngleY"), 0) * Math.PI) / 180;
            return around([1, Math.tan(ay), Math.tan(ax), 1, 0, 0]);
        }
        case "MatrixTransform":
            return Builder.matrix(attr(t, "Matrix"));
        case "TransformGroup": {
            let m = IDENTITY;
            for (const c of Builder.items(t, "Children"))
                m = multiply(m, this.transform(c));
            return m;
        }
        default:
            return IDENTITY;
        }
    }

    static matrix(text) {
        const n = numbers(text);
        return n.length >= 6 ? n.slice(0, 6) : IDENTITY;
    }

    // La propiedad Transform de cualquier geometría (atributo o elemento).
    withTransform(n, path) {
        const t = Builder.property(n, "Transform");
        if (t)
            return transformPath(path, this.transform(t));
        const text = attr(n, "Transform");
        if (text && text !== "Identity")
            return transformPath(path, Builder.matrix(text));
        return path;
    }

    // Geometría de una propiedad (atributo con trazado o elemento).
    geometryProperty(n, prop) {
        if (has(n, prop))
            return this.markup(attr(n, prop));
        const g = Builder.property(n, prop);
        return g ? this.geometry(g) : null;
    }

    markup(text) {
        try {
            return parsePathMarkup(text);
        } catch (e) {
            this.error = e.message;
            return emptyPath();
        }
    }

    static fillRule(n) {
        return attr(n, "FillRule") === "Nonzero" ? "nonzero" : "evenodd";
    }

    figure(f, b) {
        let cur = toPoint(attr(f, "StartPoint"));
        b.moveTo(cur.x, cur.y);
        for (const s of Builder.items(f, "Segments")) {
            const a = (name) => attr(s, name);
            switch (localName(s)) {
            case "LineSegment":
                cur = toPoint(a("Point"), cur);
                b.lineTo(cur.x, cur.y);
                break;
            case "PolyLineSegment":
                for (const p of toPoints(a("Points"))) {
                    b.lineTo(p.x, p.y);
                    cur = p;
                }
                break;
            case "BezierSegment": {
                const p1 = toPoint(a("Point1"), cur), p2 = toPoint(a("Point2"), cur), p3 = toPoint(a("Point3"), cur);
                b.cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
                cur = p3;
                break;
            }
            case "PolyBezierSegment": {
                const p = toPoints(a("Points"));
                for (let i = 0; i + 2 < p.length; i += 3) {
                    b.cubicTo(p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, p[i + 2].x, p[i + 2].y);
                    cur = p[i + 2];
                }
                break;
            }
            case "QuadraticBezierSegment": {
                const p1 = toPoint(a("Point1"), cur), p2 = toPoint(a("Point2"), cur);
                b.quadTo(p1.x, p1.y, p2.x, p2.y);
                cur = p2;
                break;
            }
            case "PolyQuadraticBezierSegment": {
                const p = toPoints(a("Points"));
                for (let i = 0; i + 1 < p.length; i += 2) {
                    b.quadTo(p[i].x, p[i].y, p[i + 1].x, p[i + 1].y);
                    cur = p[i + 1];
                }
                break;
            }
            case "ArcSegment": {
                const end = toPoint(a("Point"), cur), size = toPoint(a("Size"));
                arcTo(b, cur, size.x, size.y, toNumber(a("RotationAngle"), 0), toBool(a("IsLargeArc"), false),
                      a("SweepDirection") === "Clockwise", end);
                cur = end;
                break;
            }
            case "PolyCurveSegment": {
                const p = toPoints(a("Points"));
                curveThrough(b, cur, p);
                if (p.length)
                    cur = p.at(-1);
                break;
            }
            default:
                this.error = tr("segmento desconocido: %1", localName(s));
            }
        }
        if (toBool(attr(f, "IsClosed"), false))
            b.close();
    }

    geometry(n) {
        const t = localName(n);
        let p = emptyPath();
        if (t === "PathGeometry") {
            const b = new PathBuilder();
            if (has(n, "Figures"))
                b.path = this.markup(attr(n, "Figures"));
            for (const f of Builder.items(n, "Figures"))
                this.figure(f, b);
            p = b.path;
            if (has(n, "FillRule") || !has(n, "Figures"))
                p.rule = Builder.fillRule(n);
        } else if (t === "GeometryGroup") {
            const b = new PathBuilder();
            for (const c of Builder.items(n, "Children"))
                b.addPath(this.geometry(c));
            p = b.path;
            p.rule = Builder.fillRule(n);
        } else if (t === "CombinedGeometry") {
            const a = this.geometryProperty(n, "Geometry1") || emptyPath(), b = this.geometryProperty(n, "Geometry2") || emptyPath();
            const mode = attr(n, "GeometryCombineMode") || "Union";
            const combine = { Intersect: ([x, y]) => x && y, Exclude: ([x, y]) => x && !y, Xor: ([x, y]) => x !== y }[mode] || (([x, y]) => x || y);
            p = viaGrid(unionBounds(pathBounds(a), pathBounds(b)), [filler(a), filler(b)], combine);
        } else if (t === "EllipseGeometry") {
            const c = toPoint(attr(n, "Center")), rx = toNumber(attr(n, "RadiusX"), 0), ry = toNumber(attr(n, "RadiusY"), 0);
            const b = new PathBuilder();
            const k = 0.5522847498;
            b.moveTo(c.x + rx, c.y);
            b.cubicTo(c.x + rx, c.y + ry * k, c.x + rx * k, c.y + ry, c.x, c.y + ry);
            b.cubicTo(c.x - rx * k, c.y + ry, c.x - rx, c.y + ry * k, c.x - rx, c.y);
            b.cubicTo(c.x - rx, c.y - ry * k, c.x - rx * k, c.y - ry, c.x, c.y - ry);
            b.cubicTo(c.x + rx * k, c.y - ry, c.x + rx, c.y - ry * k, c.x + rx, c.y);
            b.close();
            p = b.path;
        } else if (t === "RectangleGeometry") {
            const r = numbers(attr(n, "Rect"));
            if (r.length >= 4) {
                const [x, y, w, h] = r, b = new PathBuilder();
                const rx = Math.min(Math.abs(toNumber(attr(n, "RadiusX"), 0)), w / 2), ry = Math.min(Math.abs(toNumber(attr(n, "RadiusY"), 0)), h / 2);
                if (rx > 0 && ry > 0) {
                    const k = 0.5522847498;
                    b.moveTo(x + rx, y);
                    b.lineTo(x + w - rx, y);
                    b.cubicTo(x + w - rx + rx * k, y, x + w, y + ry - ry * k, x + w, y + ry);
                    b.lineTo(x + w, y + h - ry);
                    b.cubicTo(x + w, y + h - ry + ry * k, x + w - rx + rx * k, y + h, x + w - rx, y + h);
                    b.lineTo(x + rx, y + h);
                    b.cubicTo(x + rx - rx * k, y + h, x, y + h - ry + ry * k, x, y + h - ry);
                    b.lineTo(x, y + ry);
                    b.cubicTo(x, y + ry - ry * k, x + rx - rx * k, y, x + rx, y);
                } else {
                    b.moveTo(x, y);
                    b.lineTo(x + w, y);
                    b.lineTo(x + w, y + h);
                    b.lineTo(x, y + h);
                }
                b.close();
                p = b.path;
            }
        } else if (t === "LineGeometry") {
            const b = new PathBuilder(), s = toPoint(attr(n, "StartPoint")), e = toPoint(attr(n, "EndPoint"));
            b.moveTo(s.x, s.y);
            b.lineTo(e.x, e.y);
            p = b.path;
        } else if (t === "TransformedGeometry" || t === "FlattenedGeometry" || t === "OutlinedGeometry") {
            p = this.geometryProperty(n, "Geometry") || emptyPath();
        } else if (t === "WidenedGeometry") {
            // El contorno del trazo (QPainterPathStroker en Kaint).
            const g = this.geometryProperty(n, "Geometry") || emptyPath(), thickness = toNumber(attr(n, "Thickness"), 1);
            const style = Builder.property(n, "StrokeStyle"), cap = style ? attr(style, "StartLineCap") : "", join = style ? attr(style, "LineJoin") : "";
            const miter = style ? toNumber(attr(style, "MiterLimit"), 10) : 10, bounds = pathBounds(g);
            const grow = (thickness / 2) * Math.max(1, join === "Round" || join === "Bevel" ? 1 : miter);
            p = viaGrid(bounds && { x: bounds.x - grow, y: bounds.y - grow, w: bounds.w + 2 * grow, h: bounds.h + 2 * grow }, [(ctx) => {
                ctx.beginPath();
                tracePath(ctx, g);
                ctx.lineWidth = thickness;
                ctx.lineCap = cap === "Round" ? "round" : cap === "Square" ? "square" : "butt";
                ctx.lineJoin = join === "Round" ? "round" : join === "Bevel" ? "bevel" : "miter";
                ctx.miterLimit = miter;
                ctx.stroke();
            }], ([x]) => x);
        } else if (t !== "EmptyGeometry") {
            this.error = tr("geometría desconocida: %1", t);
        }
        return this.withTransform(n, p);
    }
}

// Lee una forma de un texto XAML: { name, path }; lanza un error si no es válida.
export function parseShape(xaml) {
    const doc = new DOMParser().parseFromString(xaml, "application/xml");
    const root = doc.documentElement;
    if (!root || doc.querySelector("parsererror"))
        throw new Error(doc.querySelector("parsererror")?.textContent?.split("\n")[0] || tr("archivo vacío"));
    if (localName(root) !== "SimpleGeometryShape")
        throw new Error(tr("no es un SimpleGeometryShape (%1)", localName(root)));
    const b = new Builder();
    let path;
    if (has(root, "Geometry")) {
        path = b.markup(attr(root, "Geometry"));
    } else {
        path = b.geometryProperty(root, "Geometry");
        if (!path) {
            const content = Builder.items(root, "Geometry");
            if (!content.length)
                throw new Error(tr("la forma no tiene geometría"));
            path = b.geometry(content[0]);
        }
    }
    if (b.error)
        throw new Error(b.error);
    const bounds = pathBounds(path);
    if (!bounds || bounds.w <= 0 || bounds.h <= 0)
        throw new Error(tr("la geometría está vacía"));
    return { name: attr(root, "DisplayName"), path };
}

// --- Las formas guardadas en el navegador ----------------------------------------------------

let shapes = null;

export const CustomShapes = {
    // Todas, en orden alfabético: [{ id, name, file, path }].
    all() {
        if (!shapes) {
            shapes = [];
            for (const s of remember("customShapes") || []) {
                try {
                    const parsed = parseShape(s.xaml);
                    shapes.push({ id: s.id, file: s.file, name: parsed.name || s.file.replace(/\.xaml$/i, ""), path: parsed.path });
                } catch {
                    // Forma dañada: no aparece.
                }
            }
            shapes.sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: "base" }));
        }
        return shapes;
    },

    find(id) {
        return this.all().find((s) => s.id === id) || null;
    },

    // Añade formas (File); devuelve los errores ["archivo: motivo"]. Una con el mismo nombre de
    // archivo se reemplaza.
    async add(files) {
        const stored = remember("customShapes") || [], errors = [];
        for (const f of files) {
            try {
                const xaml = await f.text();
                parseShape(xaml);
                const old = stored.findIndex((s) => s.file === f.name);
                const entry = { id: old >= 0 ? stored[old].id : `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`, file: f.name, xaml };
                if (old >= 0)
                    stored[old] = entry;
                else
                    stored.push(entry);
            } catch (e) {
                errors.push(`${f.name}: ${e.message}`);
            }
        }
        remember("customShapes", stored);
        shapes = null;
        window.dispatchEvent(new Event("kaint-shapes"));
        return errors;
    },

    remove(ids) {
        remember("customShapes", (remember("customShapes") || []).filter((s) => !ids.includes(s.id)));
        shapes = null;
        window.dispatchEvent(new Event("kaint-shapes"));
    },
};

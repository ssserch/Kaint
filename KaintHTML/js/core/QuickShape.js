// Formas rápidas (QuickShape de Kaint, como el de Procreate): al acabar un trazo a mano y dejar el
// puntero quieto, lo dibujado se reconoce como una línea, una elipse (un círculo si casi lo es), un
// rectángulo, un triángulo o un polígono, y se sustituye por la forma perfecta. Así se dibujan formas
// con el pincel o el lápiz, también con el dedo.
// recognize(points) → { kind: "line" | "ellipse" | "circle" | "rectangle" | "triangle" | "polygon" | null,
// points: [{ x, y }] } (las cerradas repiten el primer punto al final).

const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

function distanceToSegment(p, a, b) {
    const dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    if (len2 <= 1e-9)
        return dist(p, a);
    const t = Math.min(1, Math.max(0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2));
    return Math.hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

// Douglas-Peucker: los índices que hacen falta para seguir la línea con un error menor que 'eps'.
export function simplify(pts, from, to, eps, keep) {
    let worst = -1, at = -1;
    for (let i = from + 1; i < to; ++i) {
        const d = distanceToSegment(pts[i], pts[from], pts[to]);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (at >= 0 && worst > eps) {
        simplify(pts, from, at, eps, keep);
        keep.push(at);
        simplify(pts, at, to, eps, keep);
    }
}

function fitEllipse(pts) {
    let cx = 0, cy = 0;
    for (const p of pts) {
        cx += p.x;
        cy += p.y;
    }
    cx /= pts.length;
    cy /= pts.length;
    let sxx = 0, syy = 0, sxy = 0;
    for (const p of pts) {
        const dx = p.x - cx, dy = p.y - cy;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }
    const angle = 0.5 * Math.atan2(2 * sxy, sxx - syy), ca = Math.cos(angle), sa = Math.sin(angle);
    let u0 = Infinity, u1 = -Infinity, v0 = Infinity, v1 = -Infinity;
    for (const p of pts) {
        const dx = p.x - cx, dy = p.y - cy, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        u0 = Math.min(u0, u); u1 = Math.max(u1, u); v0 = Math.min(v0, v); v1 = Math.max(v1, v);
    }
    const f = { a: (u1 - u0) / 2, b: (v1 - v0) / 2, angle, error: 1 };
    f.x = cx + ca * (u0 + u1) / 2 - sa * (v0 + v1) / 2;
    f.y = cy + sa * (u0 + u1) / 2 + ca * (v0 + v1) / 2;
    if (f.a < 2 || f.b < 2)
        return f;
    let err = 0;
    for (const p of pts) {
        const dx = p.x - f.x, dy = p.y - f.y, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        err += Math.abs(Math.hypot(u / f.a, v / f.b) - 1);
    }
    f.error = err / pts.length;
    return f;
}

function ellipsePoints(f) {
    const out = [], n = Math.min(360, Math.max(32, Math.round((f.a + f.b) * 1.2))), ca = Math.cos(f.angle), sa = Math.sin(f.angle);
    for (let i = 0; i <= n; ++i) {
        const t = (2 * Math.PI * i) / n, x = f.a * Math.cos(t), y = f.b * Math.sin(t);
        out.push({ x: f.x + x * ca - y * sa, y: f.y + x * sa + y * ca });
    }
    return out;
}

function cornerAngle(a, b, c) {
    const ux = a.x - b.x, uy = a.y - b.y, vx = c.x - b.x, vy = c.y - b.y, lu = Math.hypot(ux, uy), lv = Math.hypot(vx, vy);
    if (lu < 1e-6 || lv < 1e-6)
        return 180;
    return (Math.acos(Math.min(1, Math.max(-1, (ux * vx + uy * vy) / (lu * lv)))) * 180) / Math.PI;
}

export function recognize(raw) {
    const none = { kind: null, points: [] };
    const pts = [];
    for (const p of raw)
        if (!pts.length || dist(pts[pts.length - 1], p) > 0.5)
            pts.push({ x: p.x, y: p.y });
    if (pts.length < 3)
        return none;
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity, length = 0;
    pts.forEach((p, i) => {
        x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y); x1 = Math.max(x1, p.x); y1 = Math.max(y1, p.y);
        if (i)
            length += dist(pts[i - 1], p);
    });
    const diag = Math.hypot(x1 - x0, y1 - y0);
    if (diag < 8 || length < 12)
        return none;
    const first = pts[0], last = pts[pts.length - 1], gap = dist(first, last);
    const closed = gap < Math.max(14, 0.22 * diag) && length > 2.2 * gap + diag * 0.8;
    if (!closed) {
        // Una línea: nada se aparta mucho de la recta del principio al final.
        let worst = 0;
        for (const p of pts)
            worst = Math.max(worst, distanceToSegment(p, first, last));
        return gap > 8 && worst < Math.max(4, 0.09 * gap) ? { kind: "line", points: [first, last] } : none;
    }
    // Cerrada: un polígono con pocas esquinas o una elipse.
    const ring = pts.slice();
    ring[ring.length - 1] = ring[0];
    const keep = [0];
    simplify(ring, 0, ring.length - 1, 0.07 * diag, keep);
    keep.push(ring.length - 1);
    keep.sort((a, b) => a - b);
    let poly = keep.map((i) => ring[i]);
    poly.pop();
    for (let changed = true; changed && poly.length > 3;) {
        changed = false;
        for (let i = 0; i < poly.length; ++i) {
            const n = poly.length;
            if (cornerAngle(poly[(i + n - 1) % n], poly[i], poly[(i + 1) % n]) > 155) {
                poly.splice(i, 1);
                changed = true;
                break;
            }
        }
    }
    const fit = fitEllipse(pts), corners = poly.length;
    if ((fit.error < 0.09 || (corners > 6 && fit.error < 0.16)) && fit.a >= 3 && fit.b >= 3) {
        const f = { ...fit };
        let kind = "ellipse";
        if (Math.min(f.a, f.b) / Math.max(f.a, f.b) > 0.88) {
            f.a = f.b = (f.a + f.b) / 2;
            kind = "circle";
        }
        return { kind, points: ellipsePoints(f) };
    }
    if (corners >= 3 && corners <= 8) {
        let kind = corners === 3 ? "triangle" : "polygon";
        if (corners === 4) {
            let square = true;
            for (let i = 0; i < 4; ++i)
                square &&= Math.abs(cornerAngle(poly[(i + 3) % 4], poly[i], poly[(i + 1) % 4]) - 90) < 20;
            if (square) {
                kind = "rectangle";
                const c = { x: (poly[0].x + poly[1].x + poly[2].x + poly[3].x) / 4, y: (poly[0].y + poly[1].y + poly[2].y + poly[3].y) / 4 };
                const ang = Math.atan2(poly[1].y - poly[0].y, poly[1].x - poly[0].x), ca = Math.cos(ang), sa = Math.sin(ang);
                let w = 0, h = 0;
                for (const p of poly) {
                    const dx = p.x - c.x, dy = p.y - c.y;
                    w = Math.max(w, Math.abs(dx * ca + dy * sa));
                    h = Math.max(h, Math.abs(-dx * sa + dy * ca));
                }
                // Casi recto: del todo.
                const rem = ang - Math.round(ang / (Math.PI / 2)) * (Math.PI / 2);
                const a = Math.abs(rem) < 0.12 ? ang - rem : ang, c2 = Math.cos(a), s2 = Math.sin(a);
                poly = [[-w, -h], [w, -h], [w, h], [-w, h]].map(([x, y]) => ({ x: c.x + x * c2 - y * s2, y: c.y + x * s2 + y * c2 }));
            }
        }
        return { kind, points: [...poly, poly[0]] };
    }
    if (fit.error < 0.2 && fit.a >= 3 && fit.b >= 3)
        return { kind: "ellipse", points: ellipsePoints(fit) };
    return none;
}

export const shapeNames = { line: "Línea", ellipse: "Elipse", circle: "Círculo", rectangle: "Rectángulo", triangle: "Triángulo", polygon: "Polígono" };

// El contorno de una máscara (Uint8Array de w × h, dentro desde 128) como polígonos por los bordes de
// los píxeles (con los agujeros), simplificados: para guardar una selección como forma.
export function maskOutline(mask, w, h, eps = 0.75) {
    const inside = (x, y) => x >= 0 && y >= 0 && x < w && y < h && mask[y * w + x] >= 128;
    const out = new Map(), key = (x, y) => y * (w + 1) + x;
    const add = (ax, ay, bx, by) => {
        const k = key(ax, ay);
        if (!out.has(k))
            out.set(k, []);
        out.get(k).push({ ax, ay, bx, by, used: false });
    };
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            if (!inside(x, y))
                continue;
            if (!inside(x, y - 1)) add(x, y, x + 1, y);
            if (!inside(x + 1, y)) add(x + 1, y, x + 1, y + 1);
            if (!inside(x, y + 1)) add(x + 1, y + 1, x, y + 1);
            if (!inside(x - 1, y)) add(x, y + 1, x, y);
        }
    const loops = [];
    for (const list of out.values())
        for (const start of list) {
            if (start.used)
                continue;
            const loop = [];
            let e = start;
            while (e && !e.used) {
                e.used = true;
                loop.push({ x: e.ax, y: e.ay });
                e = (out.get(key(e.bx, e.by)) || []).find((n) => !n.used);
            }
            if (loop.length < 3)
                continue;
            loop.push(loop[0]);
            const keep = [0];
            simplify(loop, 0, loop.length - 1, eps, keep);
            keep.push(loop.length - 1);
            keep.sort((a, b) => a - b);
            const simple = keep.map((i) => loop[i]);
            if (simple.length >= 4)
                loops.push(simple);
        }
    return loops;
}

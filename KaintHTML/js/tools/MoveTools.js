// Mover píxeles seleccionados y Mover selección (src/app/tools/MoveTools.cpp y TransformBox):
// arrastrar mueve, los tiradores y el borde (con la mano) cambian el tamaño (Mayús: proporción),
// el botón derecho o arrastrar justo fuera del borde gira alrededor del punto central (que se
// puede mover) y el icono de abajo a la derecha mueve. Como en Kaint, cada arrastre es un paso
// del historial y los píxeles siguen "flotando" mientras ese paso sea el último (moverlos otra vez
// no los degrada). Sin selección, un clic con Mover píxeles seleccionados selecciona todo el lienzo.
import { AppState } from "../AppState.js";
import { Tool } from "./Tool.js";
import { Selection } from "../core/Selection.js";
import { clampRect, cloneCanvas, context, createCanvas, unionRect } from "../core/Raster.js";
import { drawTransformed, prefiltered } from "../core/Resample.js";
import { handCursor, iconCursor, rotateCursor, toolPixelCursor } from "../ui/Icons.js";
import { Settings } from "../Settings.js";
import { tr } from "../i18n.js";

// Distancias en la pantalla, como TransformBox de Kaint: franja junto al borde en la que se estira
// con la mano, franja de fuera en la que se gira y radio de los tiradores.
const kEdgeBand = 6, kRotateBand = 30, kHitRadius = 8;

// Caja de transformación: rectángulo base, desplazamiento, escala y giro, y el punto de giro
// ('pivot', en las coordenadas del rectángulo base; null: su centro), que sigue a la caja.
class TransformBox {
    constructor(rect) {
        this.base = rect;
        this.tx = 0;
        this.ty = 0;
        this.sx = 1;
        this.sy = 1;
        this.angle = 0;
        this.pivot = null;
    }

    // Punto de giro en la imagen.
    pivotPoint() {
        const b = this.base;
        return this.matrix().transformPoint(this.pivot || { x: b.x + b.w / 2, y: b.y + b.h / 2 });
    }

    pivotMoved() {
        const b = this.base;
        return !!this.pivot && Math.hypot(this.pivot.x - (b.x + b.w / 2), this.pivot.y - (b.y + b.h / 2)) > 0.5;
    }

    setPivot(p) {
        const q = this.matrix().inverse().transformPoint(p);
        this.pivot = { x: q.x, y: q.y };
    }

    // Gira 'delta' radianes alrededor de 'p' (un punto de la imagen).
    rotateAround(delta, p) {
        const c = this.center(), cs = Math.cos(delta), sn = Math.sin(delta), dx = c.x - p.x, dy = c.y - p.y;
        this.tx += p.x + dx * cs - dy * sn - c.x;
        this.ty += p.y + dx * sn + dy * cs - c.y;
        this.angle += delta;
    }

    // Esquinas en la imagen: arriba a la izquierda, arriba a la derecha, abajo a la derecha y abajo a
    // la izquierda.
    corners() {
        const h = this.handles();
        return [h[0], h[2], h[4], h[6]];
    }

    center() {
        return { x: this.base.x + this.base.w / 2 + this.tx, y: this.base.y + this.base.h / 2 + this.ty };
    }

    matrix() {
        // Compuesta a mano (en dobles): con translate/rotate/scale de DOMMatrix, las esquinas
        // ajustadas a píxeles quedaban a una millonésima del entero.
        const c0 = { x: this.base.x + this.base.w / 2, y: this.base.y + this.base.h / 2 }, c = this.center();
        const cs = Math.cos(this.angle), sn = Math.sin(this.angle);
        const a = cs * this.sx, b = sn * this.sx, cc = -sn * this.sy, d = cs * this.sy;
        return new DOMMatrix([a, b, cc, d, c.x - (a * c0.x + cc * c0.y), c.y - (b * c0.x + d * c0.y)]);
    }

    isIdentity() {
        return !this.tx && !this.ty && this.sx === 1 && this.sy === 1 && !this.angle;
    }

    save() {
        return { tx: this.tx, ty: this.ty, sx: this.sx, sy: this.sy, angle: this.angle };
    }

    restore(state) {
        Object.assign(this, state);
    }

    same(state) {
        return this.tx === state.tx && this.ty === state.ty && this.sx === state.sx && this.sy === state.sy && this.angle === state.angle;
    }

    // Tiradores: esquinas y centros de los lados (en la imagen).
    handles() {
        const m = this.matrix(), b = this.base;
        const pts = [[0, 0], [0.5, 0], [1, 0], [1, 0.5], [1, 1], [0.5, 1], [0, 1], [0, 0.5]];
        return pts.map(([fx, fy]) => m.transformPoint({ x: b.x + fx * b.w, y: b.y + fy * b.h }));
    }

    // Punto de la imagen en las coordenadas de la caja (sin girar ni mover; centro en 0, 0).
    local(p) {
        const c = this.center(), dx = p.x - c.x, dy = p.y - c.y, cs = Math.cos(-this.angle), sn = Math.sin(-this.angle);
        return { x: dx * cs - dy * sn, y: dx * sn + dy * cs };
    }

    inside(p) {
        const l = this.local(p);
        return Math.abs(l.x) <= Math.abs((this.base.w * this.sx) / 2) && Math.abs(l.y) <= Math.abs((this.base.h * this.sy) / 2);
    }

    // Arrastrar un tirador: el lado contrario se queda quieto.
    resize(handle, p, keepRatio) {
        const l = this.local(p), hw = (this.base.w * this.sx) / 2, hh = (this.base.h * this.sy) / 2;
        let left = -hw, right = hw, top = -hh, bottom = hh;
        if ([0, 6, 7].includes(handle)) left = l.x;
        if ([2, 3, 4].includes(handle)) right = l.x;
        if ([0, 1, 2].includes(handle)) top = l.y;
        if ([4, 5, 6].includes(handle)) bottom = l.y;
        let sx = (right - left) / this.base.w, sy = (bottom - top) / this.base.h;
        if (keepRatio && handle % 2 === 0) {
            const k = Math.max(Math.abs(sx / this.sx), Math.abs(sy / this.sy));
            sx = Math.sign(sx || 1) * Math.abs(this.sx) * k;
            sy = Math.sign(sy || 1) * Math.abs(this.sy) * k;
            if ([0, 6].includes(handle)) left = right - sx * this.base.w;
            else right = left + sx * this.base.w;
            if ([0, 2].includes(handle)) top = bottom - sy * this.base.h;
            else bottom = top + sy * this.base.h;
        }
        if (Math.abs(sx) < 1e-3 || Math.abs(sy) < 1e-3)
            return;
        const mid = { x: (left + right) / 2, y: (top + bottom) / 2 }, cs = Math.cos(this.angle), sn = Math.sin(this.angle);
        this.tx += mid.x * cs - mid.y * sn;
        this.ty += mid.x * sn + mid.y * cs;
        this.sx = sx;
        this.sy = sy;
    }

    // Píxel a píxel (TransformBox::snappedToPixels de Kaint): sin giro, o girada 90°, 180° o
    // 270°, las esquinas caen en píxeles enteros y el tamaño cambia de píxel en píxel. Con otros
    // giros no se toca (la imagen se remuestrea igualmente).
    snapToPixels() {
        const quarter = this.angle / (Math.PI / 2), k = Math.round(quarter);
        if (Math.abs(quarter - k) > 1e-9 || this.base.w <= 0 || this.base.h <= 0)
            return;
        const m = this.matrix(), b = this.base;
        const a = m.transformPoint({ x: b.x, y: b.y }), c = m.transformPoint({ x: b.x + b.w, y: b.y + b.h });
        const axis = (from, to) => {
            const x0 = Math.round(from);
            let x1 = Math.round(to);
            if (x0 === x1)
                x1 = x0 + (to >= from ? 1 : -1);
            return [x0, x1];
        };
        const [x0, x1] = axis(a.x, c.x), [y0, y1] = axis(a.y, c.y);
        // La mitad de la diagonal, vuelta a los ejes de la caja (sin el giro), da la escala.
        const hx = (x1 - x0) / 2, hy = (y1 - y0) / 2, cs = Math.cos(-this.angle), sn = Math.sin(-this.angle);
        this.sx = (2 * (hx * cs - hy * sn)) / b.w;
        this.sy = (2 * (hx * sn + hy * cs)) / b.h;
        this.tx = (x0 + x1) / 2 - (b.x + b.w / 2);
        this.ty = (y0 + y1) / 2 - (b.y + b.h / 2);
    }
}

// Tirador al que equivale un punto de la pantalla cerca del contorno (a menos de kEdgeBand): el del
// lado más cercano (1 arriba, 3 derecha, 5 abajo, 7 izquierda) o, cerca de sus extremos, el de la
// esquina; -1 si está lejos. También devuelve la distancia al contorno.
function edgeHandle(corners, p) {
    const kCornerBand = 10;
    let handle = -1, distance = Infinity;
    for (let i = 0; i < 4; ++i) {
        const a = corners[i], b = corners[(i + 1) % 4], abx = b.x - a.x, aby = b.y - a.y;
        const len = Math.hypot(abx, aby);
        const t = len > 0 ? Math.max(0, Math.min(1, ((p.x - a.x) * abx + (p.y - a.y) * aby) / (len * len))) : 0;
        const d = Math.hypot(p.x - (a.x + abx * t), p.y - (a.y + aby * t));
        if (d >= distance)
            continue;
        distance = d;
        handle = d > kEdgeBand ? -1 : t * len < kCornerBand ? (2 * i) % 8 : (1 - t) * len < kCornerBand ? (2 * i + 2) % 8 : 2 * i + 1;
    }
    return { handle, distance };
}

// Tiradores de Kaint (Handles::draw): redondos, el botón azul de restablecer el punto de giro y el
// icono de mover (cuadro con cuatro flechas).
function drawHandle(ctx, c, shape) {
    const dark = "rgba(30,30,30,0.9)", light = "rgba(255,255,255,0.94)";
    ctx.save();
    ctx.lineWidth = 1;
    if (shape === "circle") {
        ctx.beginPath();
        ctx.arc(c.x, c.y, 4.5, 0, Math.PI * 2);
        ctx.fillStyle = light;
        ctx.fill();
        ctx.strokeStyle = dark;
        ctx.stroke();
    } else if (shape === "resetPivot") {
        ctx.translate(c.x, c.y);
        ctx.rotate(Math.PI / 4);
        ctx.fillStyle = "#0078d7";
        ctx.strokeStyle = dark;
        ctx.fillRect(-6.5, -6.5, 13, 13);
        ctx.strokeRect(-6.5, -6.5, 13, 13);
        ctx.rotate(-Math.PI / 4);
        ctx.beginPath();
        ctx.arc(0, 0, 3.5, (-30 * Math.PI) / 180, (-300 * Math.PI) / 180, true);
        ctx.strokeStyle = light;
        ctx.lineWidth = 1.4;
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(1.5, -5.5);
        ctx.lineTo(4.5, -3.5);
        ctx.lineTo(1.5, -1.5);
        ctx.closePath();
        ctx.fillStyle = light;
        ctx.fill();
    } else if (shape === "move") {
        ctx.beginPath();
        ctx.roundRect(c.x - 9, c.y - 9, 18, 18, 3);
        ctx.fillStyle = light;
        ctx.fill();
        ctx.strokeStyle = dark;
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(c.x - 6, c.y);
        ctx.lineTo(c.x + 6, c.y);
        ctx.moveTo(c.x, c.y - 6);
        ctx.lineTo(c.x, c.y + 6);
        ctx.lineWidth = 1.4;
        ctx.stroke();
        ctx.fillStyle = dark;
        for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
            const tx = c.x + dx * 7, ty = c.y + dy * 7, sx = -dy, sy = dx;
            ctx.beginPath();
            ctx.moveTo(tx, ty);
            ctx.lineTo(tx - dx * 3 + sx * 2.5, ty - dy * 3 + sy * 2.5);
            ctx.lineTo(tx - dx * 3 - sx * 2.5, ty - dy * 3 - sy * 2.5);
            ctx.closePath();
            ctx.fill();
        }
    }
    ctx.restore();
}

// Punto de giro (Handles::drawPivot): círculo con una cruz girada como la caja.
function drawPivot(ctx, c, angle) {
    ctx.save();
    ctx.translate(c.x, c.y);
    ctx.rotate(angle);
    ctx.beginPath();
    ctx.arc(0, 0, 6, 0, Math.PI * 2);
    ctx.fillStyle = "rgba(255,255,255,0.94)";
    ctx.fill();
    ctx.moveTo(-4, 0);
    ctx.lineTo(4, 0);
    ctx.moveTo(0, -4);
    ctx.lineTo(0, 4);
    ctx.strokeStyle = "rgba(30,30,30,0.9)";
    ctx.lineWidth = 1;
    ctx.stroke();
    ctx.restore();
}

class MoveToolBase extends Tool {
    get options() {
        return ["finish"];
    }

    get highlightSelection() {
        return true;
    }

    hint() {
        return tr("Arrastra la selección para moverla. Arrastre los controles para cambiar el tamaño. Arrastra con el botón derecho del ratón, o justo fuera del borde, para rotar.");
    }

    cursor() {
        return toolPixelCursor(this.id) || iconCursor(this.icon, null, null, "move");
    }

    // La sesión (píxeles o selección levantados) sigue viva mientras su paso sea el último del
    // historial de la imagen activa.
    session() {
        const s = this.current;
        if (!s || s.doc !== this.doc)
            return null;
        if (this.drag?.session === s)
            return s;
        const h = s.doc.history;
        if (!s.step || h.steps[h.index] !== s.step || (s.layer && s.layer !== s.doc.activeLayer))
            return null;
        return s;
    }

    // Caja que se ve y se agarra: la de la sesión o la de la selección (guardada mientras la
    // selección no cambie, para conservar su punto de giro).
    restBox() {
        const s = this.session();
        if (s)
            return s.box;
        const sel = this.doc?.selection, b = sel?.bounds();
        if (!b)
            return null;
        if (this.rest?.selection !== sel)
            this.rest = { selection: sel, box: new TransformBox(b) };
        return this.rest.box;
    }

    // Lo que se ve de la caja en la pantalla: tiradores, esquinas, límites, punto de giro, botón de
    // restablecerlo y el icono de mover.
    viewGeometry(box) {
        const toView = (p) => this.view.imageToView(p.x, p.y);
        const corners = box.corners().map(toView);
        const xs = corners.map((c) => c.x), ys = corners.map((c) => c.y);
        const bounds = { x0: Math.min(...xs), y0: Math.min(...ys), x1: Math.max(...xs), y1: Math.max(...ys) };
        const pivot = toView(box.pivotPoint());
        return { handles: box.handles().map(toView), corners, bounds, pivot, reset: { x: pivot.x + 18, y: pivot.y - 18 },
                 moveIcon: { x: bounds.x1 + 16, y: bounds.y1 + 16 } };
    }

    // Qué hace el botón en ese punto, como TransformBox::hit de Kaint: restablecer o mover el punto
    // de giro, un tirador, el icono de mover, la mano junto al borde (estira hacia ese lado), girar
    // justo fuera o con el botón derecho, y si no, mover.
    hit(e, box) {
        if (e.button === "right")
            return { kind: "rotate" };
        if (!box)
            return { kind: "move" };
        const g = this.viewGeometry(box), near = (v, r) => Math.hypot(v.x - e.vx, v.y - e.vy) <= r;
        if (box.pivotMoved() && near(g.reset, 9))
            return { kind: "resetPivot" };
        if (near(g.pivot, kHitRadius))
            return { kind: "pivot" };
        const handle = g.handles.findIndex((v) => near(v, kHitRadius));
        if (handle >= 0)
            return { kind: "resize", handle };
        if (near(g.moveIcon, 10))
            return { kind: "move" };
        const inside = box.inside({ x: e.x, y: e.y });
        const room = Math.min(g.bounds.x1 - g.bounds.x0, g.bounds.y1 - g.bounds.y0) > 4 * kEdgeBand;
        const edge = edgeHandle(g.corners, { x: e.vx, y: e.vy });
        if (edge.handle >= 0 && (!inside || room))
            return { kind: "resize", handle: edge.handle };
        if (!inside && edge.distance <= kRotateBand)
            return { kind: "rotate" };
        return { kind: "move" };
    }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || (e.button !== "left" && e.button !== "right"))
            return;
        const rest = this.restBox();
        let hit = this.hit(e, rest);
        // El punto de giro no levanta nada: solo se coloca (o vuelve al centro).
        if (hit.kind === "resetPivot") {
            rest.pivot = null;
            this.view.update();
            return;
        }
        if (hit.kind === "pivot") {
            this.drag = { kind: "pivot", box: rest, session: null };
            return;
        }
        const session = this.begin(e);
        if (!session)
            return;
        // La caja nueva (píxeles recién levantados) conserva el punto de giro de la que se veía.
        if (session.box !== rest && rest && !session.step && session.box.isIdentity())
            session.box.pivot = rest.pivot;
        if (this.selectedAll)
            hit = { kind: "move" };
        const box = session.box, p = { x: e.x, y: e.y }, pivot = box.pivotPoint();
        this.drag = { ...hit, session, start: box.save(), from: p, pivot, fromAngle: Math.atan2(p.y - pivot.y, p.x - pivot.x) };
        if (hit.kind === "resize")
            this.view.setCursor(handCursor(true));
    }

    onPointerMove(e) {
        if (!this.drag) {
            this.hoverCursor(e);
            return;
        }
        if (this.drag.kind === "pivot") {
            this.drag.box.setPivot({ x: e.x, y: e.y });
            this.view.update();
            return;
        }
        this.applyMove(e);
    }

    // La capa se vuelve a dibujar una vez por fotograma: con ratones rápidos llegaban decenas de
    // movimientos por fotograma y cada uno la redibujaba (iba a tirones).
    scheduleRender(session) {
        this.renderPending = session;
        if (!this.renderFrame)
            this.renderFrame = requestAnimationFrame(() => {
                this.renderFrame = 0;
                const s = this.renderPending;
                this.renderPending = null;
                if (s && this.drag?.session === s)
                    this.render(s);
            });
    }

    cancelRender() {
        if (this.renderFrame)
            cancelAnimationFrame(this.renderFrame);
        this.renderFrame = 0;
        this.renderPending = null;
    }

    applyMove(e) {
        const p = { x: e.x, y: e.y }, d = this.drag, box = d.session.box;
        // Píxel a píxel (por defecto, como la selección): nada queda a medio píxel.
        const snap = !AppState.selectionAntialias();
        if (d.kind === "move") {
            box.tx = Math.round(d.start.tx + p.x - d.from.x);
            box.ty = Math.round(d.start.ty + p.y - d.from.y);
            if (snap)
                box.snapToPixels();
        } else if (d.kind === "resize") {
            box.resize(d.handle, p, e.shift);
            if (snap)
                box.snapToPixels();
        } else {
            // Alrededor del punto de giro del principio del arrastre (desde la caja de entonces, así
            // el ajuste a píxeles no se va acumulando).
            box.restore(d.start);
            let delta = Math.atan2(p.y - d.pivot.y, p.x - d.pivot.x) - d.fromAngle;
            if (e.shift)
                delta = Math.round((d.start.angle + delta) / (Math.PI / 12)) * (Math.PI / 12) - d.start.angle;
            box.rotateAround(delta, d.pivot);
            if (snap)
                box.snapToPixels();
            this.setHint(tr("Ángulo: %1°", ((-box.angle * 180) / Math.PI).toFixed(2)));
            this.view.setCursor(rotateCursor(this.angleTo(box, e)));
        }
        this.scheduleRender(d.session);
    }

    onPointerUp(e) {
        const d = this.drag;
        if (!d)
            return;
        this.cancelRender();
        this.drag = null;
        this.selectedAll = false;
        if (d.kind === "pivot") {
            this.view.update();
            return;
        }
        this.setHint(this.hint());
        if (d.kind === "resize" && e)
            this.hoverCursor(e);
        if (d.session.box.same(d.start)) {
            this.unmoved(d.session);
            return;
        }
        this.commit(d.session, this.name, this.icon);
    }

    hoverCursor(e) {
        const box = this.restBox();
        if (!box || this.doc.selection.isEmpty()) {
            this.view.setCursor(this.cursor());
            return;
        }
        const hit = this.hit(e, box);
        this.view.setCursor(hit.kind === "resize" ? handCursor(false)
                            : hit.kind === "rotate" ? rotateCursor(this.angleTo(box, e))
                            : hit.kind === "pivot" || hit.kind === "resetPivot" ? "default" : this.cursor());
    }

    // Dirección (en la vista) desde el puntero hacia el punto de giro: la de la flecha de girar.
    angleTo(box, e) {
        const c = box.pivotPoint(), v = this.view.imageToView(c.x, c.y);
        return Math.atan2(v.y - e.vy, v.x - e.vx);
    }

    isBusy() {
        return !!this.drag && this.drag.kind !== "pivot";
    }

    // Se deja de arrastrar sin aplicar nada (Esc, deshacer durante el arrastre).
    cancel() {
        const d = this.drag;
        if (!d)
            return;
        this.cancelRender();
        this.drag = null;
        this.selectedAll = false;
        if (d.kind === "pivot") {
            this.view.update();
            return;
        }
        d.session.box.restore(d.start);
        this.unmoved(d.session);
        this.view.update();
    }

    finish() {
        this.cancel();
        this.current = null;
    }

    deactivate() {
        this.finish();
        this.view.update();
    }

    onKeyDown(e) {
        // Flechas: mover un píxel, como un arrastre corto.
        const step = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
        if (!step || this.drag || !this.doc || this.doc.selection.isEmpty())
            return false;
        const session = this.begin({ button: "left", ctrl: false });
        if (!session)
            return false;
        session.box.tx += step[0];
        session.box.ty += step[1];
        this.render(session);
        this.commit(session, this.name, this.icon);
        return true;
    }

    // Como el cuadro de transformación de Kaint: tiradores redondos, el punto de giro (un círculo
    // con una cruz que gira con la caja), el botón para devolverlo al centro si se movió y el icono
    // de mover abajo a la derecha. El contorno es el de la selección (lo dibuja el lienzo).
    paintOverlay(ctx, view) {
        const s = this.session();
        const dragging = this.drag && this.drag.kind !== "pivot";
        const box = dragging ? this.drag.session.box : this.restBox();
        if (!box || (!s && this.doc.selection.isEmpty()))
            return;
        // La selección se ve donde quedará; mientras se arrastra (salvo que en Configuración no se
        // oculte nada), solo el icono de mover y, al girar, el punto de giro.
        if (s && dragging)
            view.strokeSelection(ctx, s.selBase, box.matrix());
        const g = this.viewGeometry(box);
        const hide = dragging && Settings.get("hideMoveBox") !== false;
        ctx.save();
        if (!hide)
            for (const v of g.handles)
                drawHandle(ctx, v, "circle");
        if (!hide || this.drag.kind === "rotate") {
            drawPivot(ctx, g.pivot, box.angle);
            if (box.pivotMoved() && !hide)
                drawHandle(ctx, g.reset, "resetPivot");
        }
        drawHandle(ctx, g.moveIcon, "move");
        ctx.restore();
    }

    hidesSelectionOutline() {
        return !!this.drag && this.drag.kind !== "pivot" && Settings.get("hideMoveBox") !== false;
    }
}

export class MoveSelectedTool extends MoveToolBase {
    get options() { return ["interpolation", "gamma", "selectionQuality", "finish"]; }

    activate() {
        super.activate();
        if (!this.watchingSampling) {
            this.watchingSampling = true;
            AppState.addEventListener("tools", () => this.applySampling());
        }
    }

    // El muestreo y la corrección gamma de la barra se aplican al momento a lo que se mueve
    // (MoveSelectedTool::applySampling de Kaint).
    applySampling() {
        const s = this.session();
        if (!s?.step || this.drag || s.sampling === `${this.settings.moveInterpolation || "bicubic"}/${this.settings.moveGamma !== false}`)
            return;
        const doc = s.doc, layer = s.layer;
        this.render(s, true);
        doc.preview = null;
        layer.ctx.clearRect(0, 0, doc.width, doc.height);
        layer.ctx.drawImage(s.preview, 0, 0);
        s.step.after = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        doc.pixelsChanged(layer, null);
    }

    get id() { return "moveselectedpixels"; }
    get name() { return tr("Mover píxeles seleccionados"); }
    get icon() { return "moveselectedpixels"; }
    get letter() { return "M"; }

    // Sesión para arrastrar: la que sigue viva o, si no, los píxeles de la selección levantados.
    // Sin selección, como en Paint.NET, el clic selecciona todo el lienzo (ya se ve al pulsar).
    begin(e) {
        const doc = this.doc;
        if (!doc.activeLayer)
            return null;
        let session = e.ctrl ? null : this.session();
        if (!session && doc.selection.isEmpty()) {
            const before = doc.selection;
            doc.setSelection(Selection.rect(doc.width, doc.height, 0, 0, doc.width, doc.height));
            doc.pushSelection(before, tr("Seleccionar todo"), "edit-select-all");
            this.selectedAll = true;
        }
        if (!session) {
            session = this.lift(!!e.ctrl);
            this.current = session;
        }
        return session;
    }

    // Levanta los píxeles seleccionados y deja el hueco (con Ctrl, una copia donde estaban).
    lift(keepCopy) {
        const doc = this.doc, sel = doc.selection, layer = doc.activeLayer;
        const b = sel.bounds();
        const floating = createCanvas(b.w, b.h);
        const f = context(floating);
        f.drawImage(layer.canvas, -b.x, -b.y);
        f.globalCompositeOperation = "destination-in";
        f.drawImage(sel.clipCanvas(AppState.selectionAntialias()), -b.x, -b.y);
        const base = cloneCanvas(layer.canvas);
        if (!keepCopy) {
            const h = context(base);
            h.globalCompositeOperation = "destination-out";
            h.drawImage(sel.clipCanvas(AppState.selectionAntialias()), 0, 0);
        }
        return { doc, layer, base, floating, selBase: sel, box: new TransformBox(b), preview: createCanvas(doc.width, doc.height), step: null };
    }

    // La capa con los píxeles en su sitio nuevo. Como en Paint.NET (Configuración), los movidos
    // reemplazan a los de debajo, también a los transparentes.
    render(s, exact = false) {
        const doc = s.doc, p = context(s.preview), m = s.box.matrix();
        // Solo se vuelve a dibujar la zona que cambia (donde estaban los píxeles y donde quedan): con
        // la capa entera en cada movimiento, las imágenes grandes iban a tirones.
        const now = this.floatingBounds(s, m);
        const area = s.drawn && s.drawnFor === s.preview ? unionRect(s.drawn, now) : { x: 0, y: 0, w: doc.width, h: doc.height };
        s.drawn = now;
        s.drawnFor = s.preview;
        if (area.w <= 0 || area.h <= 0)
            return;
        p.save();
        p.setTransform(1, 0, 0, 1, 0, 0);
        p.beginPath();
        p.rect(area.x, area.y, area.w, area.h);
        p.clip();
        p.globalCompositeOperation = "source-over";
        p.clearRect(area.x, area.y, area.w, area.h);
        p.drawImage(s.base, area.x, area.y, area.w, area.h, area.x, area.y, area.w, area.h);
        const smooth = s.box.sx !== 1 || s.box.sy !== 1 || !!s.box.angle;
        if (Settings.get("replacePixels")) {
            p.setTransform(m);
            p.globalCompositeOperation = "destination-out";
            p.imageSmoothingEnabled = smooth;
            p.drawImage(s.selBase.maskCanvas(), 0, 0);
            p.globalCompositeOperation = "source-over";
        }
        this.drawFloating(s, p, m.multiply(new DOMMatrix().translate(s.box.base.x, s.box.base.y)), exact);
        p.restore();
        doc.setPreview({ layer: s.layer, canvas: s.preview }, area);
    }

    // Rectángulo (en la imagen) que ocupan los píxeles con la transformación 'm', con un margen para
    // lo que el remuestreo pinta fuera del borde.
    floatingBounds(s, m) {
        const b = s.box.base;
        const pts = [[b.x, b.y], [b.x + b.w, b.y], [b.x, b.y + b.h], [b.x + b.w, b.y + b.h]].map(([x, y]) => m.transformPoint({ x, y }));
        const x0 = Math.floor(Math.min(...pts.map((q) => q.x))) - 3, y0 = Math.floor(Math.min(...pts.map((q) => q.y))) - 3;
        const x1 = Math.ceil(Math.max(...pts.map((q) => q.x))) + 3, y1 = Math.ceil(Math.max(...pts.map((q) => q.y))) + 3;
        return clampRect({ x: x0, y: y0, w: x1 - x0, h: y1 - y0 }, s.doc.width, s.doc.height);
    }

    // Los píxeles levantados con la transformación, el muestreo y la gamma de la barra
    // (FloatingPixels::render de Kaint): al reducir se promedian antes por áreas; el bicúbico y el
    // bilineal con gamma se calculan aquí (mientras se arrastra, si la zona es grande, se ve antes
    // con el del navegador; al soltar queda el exacto).
    drawFloating(s, p, full, exact) {
        const doc = s.doc, interp = this.settings.moveInterpolation || "bicubic", gamma = this.settings.moveGamma !== false;
        s.sampling = `${interp}/${gamma}`;
        if (full.a === 1 && full.b === 0 && full.c === 0 && full.d === 1 && Number.isInteger(full.e) && Number.isInteger(full.f)) {
            p.setTransform(1, 0, 0, 1, 0, 0);
            p.imageSmoothingEnabled = false;
            p.drawImage(s.floating, full.e, full.f);
            return;
        }
        if (s.floatingFor !== s.floating) {
            s.floatingFor = s.floating;
            s.floatingData = context(s.floating).getImageData(0, 0, s.floating.width, s.floating.height);
            s.prefilter = {};
        }
        const pre = prefiltered(s.floatingData, full, gamma && interp !== "nearest", s.prefilter);
        const m = pre.matrix, iw = pre.image.width, ih = pre.image.height;
        const xs = [0, iw, 0, iw].map((x, i) => m.a * x + m.c * [0, 0, ih, ih][i] + m.e);
        const ys = [0, iw, 0, iw].map((x, i) => m.b * x + m.d * [0, 0, ih, ih][i] + m.f);
        const x0 = Math.max(0, Math.floor(Math.min(...xs)) - 2), y0 = Math.max(0, Math.floor(Math.min(...ys)) - 2);
        const x1 = Math.min(doc.width, Math.ceil(Math.max(...xs)) + 2), y1 = Math.min(doc.height, Math.ceil(Math.max(...ys)) + 2);
        if (x1 <= x0 || y1 <= y0)
            return;
        const ours = interp !== "nearest" && (interp !== "bilinear" || gamma);
        // Mientras se arrastra, el remuestreo exacto (en JavaScript) solo en zonas pequeñas: en las
        // grandes cada movimiento tardaba demasiado. Al soltar queda el exacto.
        if (ours && (exact || (x1 - x0) * (y1 - y0) <= 40000)) {
            const tmp = new ImageData(x1 - x0, y1 - y0);
            drawTransformed(tmp, pre.image, new DOMMatrix().translate(-x0, -y0).multiply(m), interp, gamma);
            const c = createCanvas(tmp.width, tmp.height);
            context(c).putImageData(tmp, 0, 0);
            p.setTransform(1, 0, 0, 1, 0, 0);
            p.drawImage(c, x0, y0);
            return;
        }
        let source = s.floating;
        if (pre.image !== s.floatingData) {
            if (s.prefilter.canvasFor !== pre.image) {
                s.prefilter.canvasFor = pre.image;
                s.prefilter.canvas = createCanvas(iw, ih);
                context(s.prefilter.canvas).putImageData(pre.image, 0, 0);
            }
            source = s.prefilter.canvas;
        }
        p.setTransform(m);
        p.imageSmoothingEnabled = interp !== "nearest";
        p.imageSmoothingQuality = interp === "bicubic" || interp === "anisotropic" ? "high" : interp === "multisample" ? "medium" : "low";
        p.drawImage(source, 0, 0);
    }

    unmoved(s) {
        // Se soltó sin mover: la capa no cambia (la selección de todo, si se hizo, se queda).
        if (s.doc.preview?.canvas === s.preview)
            s.doc.setPreview(null);
        if (!s.step)
            this.current = null;
    }

    // Un paso del historial con la capa y la selección de antes y de después.
    commit(s, name, icon, selectionBefore = s.doc.selection) {
        const doc = s.doc, layer = s.layer;
        this.render(s, true);
        const before = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        doc.preview = null;
        layer.ctx.clearRect(0, 0, doc.width, doc.height);
        layer.ctx.drawImage(s.preview, 0, 0);
        const after = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        const selAfter = s.selBase.transformed(s.box.matrix(), this.settings.selectionAntialias);
        const apply = (data, sel) => {
            layer.ctx.putImageData(data, 0, 0);
            doc.setSelection(sel);
            doc.pixelsChanged(layer, null);
        };
        s.step = { name, icon, bytes: before.data.length + after.data.length, after,
                   undo: () => apply(before, selectionBefore), redo: () => apply(s.step.after, selAfter) };
        doc.selection = selAfter;
        doc.push(s.step);
        doc.pixelsChanged(layer, null);
        doc.emit("selection");
        this.current = s;
    }

    // Píxeles pegados: se colocan como un paso "Pegar" y quedan flotando (sin hueco debajo) para
    // moverlos después.
    startFloating(image, x, y, name) {
        const doc = this.doc;
        this.finish();
        const floating = createCanvas(image.width, image.height);
        context(floating).drawImage(image, 0, 0);
        const rect = { x, y, w: image.width, h: image.height };
        const s = {
            doc, layer: doc.activeLayer, base: cloneCanvas(doc.activeLayer.canvas), floating,
            selBase: Selection.rect(doc.width, doc.height, x, y, image.width, image.height), box: new TransformBox(rect),
            preview: createCanvas(doc.width, doc.height), step: null,
        };
        this.render(s);
        this.commit(s, name, "edit-paste");
    }
}

export class MoveSelectionTool extends MoveToolBase {
    get options() { return ["moveQuality", "finish"]; }

    get id() { return "moveselection"; }
    get name() { return tr("Mover selección"); }
    get icon() { return "moveselection"; }
    get letter() { return "M"; }

    begin(e) {
        const doc = this.doc;
        let session = e.ctrl ? null : this.session();
        if (!session) {
            if (doc.selection.isEmpty())
                return null;
            session = { doc, selBase: doc.selection, box: new TransformBox(doc.selection.bounds()), step: null };
            this.current = session;
        }
        return session;
    }

    render() {
        this.view.update();
    }

    unmoved(s) {
        if (!s.step)
            this.current = null;
        this.view.update();
    }

    commit(s, name, icon) {
        const doc = s.doc, before = doc.selection;
        const after = s.selBase.transformed(s.box.matrix(), AppState.selectionAntialias());
        s.step = { name, icon, undo: () => doc.setSelection(before), redo: () => doc.setSelection(after) };
        doc.setSelection(after);
        doc.push(s.step);
        this.current = s;
    }

    paintOverlay(ctx, view) {
        if (this.drag?.session)
            view.strokeSelection(ctx, this.drag.session.selBase, this.drag.session.box.matrix());
        super.paintOverlay(ctx, view);
    }
}

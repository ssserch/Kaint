// Diálogo de un ajuste o efecto (src/app/dialogs/EffectDialog.cpp de Kaint): el mismo control
// por tipo de parámetro (deslizador con caja y restablecer, selector de ángulo, centro sobre la
// miniatura, globo de rotación, color con transparencia, curvas, niveles...), pestañas, casillas
// en el título, valores vinculados, Ctrl+Z / Ctrl+Y para los cambios del diálogo y la vista previa
// calculada en segundo plano (EffectRunner).
import { AppState } from "../AppState.js";
import { automaticLevels, curveLut, defaultCurves, defaultLevels, defaultValues, histogram, isReadOnly, levelsLut } from "../core/Effects.js";
import { startEffect } from "../core/EffectRunner.js";
import { checkerPattern, clampRect, cloneCanvas, context, createCanvas, thumbnail } from "../core/Raster.js";
import { colorDialog } from "./ColorDialog.js";
import { Dialog, el } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { tr } from "../i18n.js";

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const clone = (v) => JSON.parse(JSON.stringify(v));
const theme = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
const cssColor = (c) => `rgba(${c.r},${c.g},${c.b},${(c.a ?? 255) / 255})`;

// --- Ejecución ----------------------------------------------------------------------------

// Lo que necesita un efecto de la imagen abierta: la capa activa, la zona (la selección o toda
// la imagen), sus píxeles, la máscara de la selección y los colores.
export function effectInput(doc) {
    const layer = doc.activeLayer, sel = doc.selection;
    const rect = clampRect(sel.bounds() || { x: 0, y: 0, w: doc.width, h: doc.height }, doc.width, doc.height);
    const src = layer.ctx.getImageData(0, 0, doc.width, doc.height);
    const mask = sel.clipMask(AppState.selectionAntialias()) || null;
    return { doc, layer, rect, src, mask, info: { primary: { ...AppState.primary }, secondary: { ...AppState.secondary }, selection: mask } };
}

// Mezcla el resultado con la capa según la selección (los bordes suavizados se ponderan por el
// alfa, como Effects::apply de Kaint) y devuelve los píxeles del rectángulo.
function blendSelection(input, out) {
    const { rect, src, mask } = input, w = src.width, s = src.data, o = out.data;
    const data = new ImageData(Math.max(1, rect.w), Math.max(1, rect.h)), d = data.data;
    for (let y = 0; y < rect.h; ++y)
        for (let x = 0; x < rect.w; ++x) {
            const p = (y + rect.y) * w + rect.x + x, i = p * 4, j = (y * rect.w + x) * 4;
            const t = mask ? mask[p] : 255;
            if (t >= 255) {
                d[j] = o[i]; d[j + 1] = o[i + 1]; d[j + 2] = o[i + 2]; d[j + 3] = o[i + 3];
            } else if (t <= 0) {
                d[j] = s[i]; d[j + 1] = s[i + 1]; d[j + 2] = s[i + 2]; d[j + 3] = s[i + 3];
            } else {
                const it = 255 - t, wa = s[i + 3] * it, wb = o[i + 3] * t, sum = wa + wb;
                if (sum === 0) {
                    for (let c = 0; c < 3; ++c)
                        d[j + c] = Math.floor((s[i + c] * it + o[i + c] * t + 127) / 255);
                    d[j + 3] = 0;
                } else {
                    const half = Math.floor(sum / 2);
                    for (let c = 0; c < 3; ++c)
                        d[j + c] = Math.floor((s[i + c] * wa + o[i + c] * wb + half) / sum);
                    d[j + 3] = Math.floor((sum + 127) / 255);
                }
            }
        }
    return data;
}

// Calcula el efecto aquí mismo → { rect, data } (lo usan los complementos con diálogo propio).
export function renderEffect(doc, effect, values) {
    const input = effectInput(doc);
    return { rect: input.rect, data: blendSelection(input, effect.render(input.src, values, input.rect, input.info)) };
}

// Calcula el efecto en segundo plano → { promise (de { rect, data } o null si se cancela), cancel }.
export function computeEffect(input, effect, values, onProgress = null) {
    const job = startEffect(effect, input.src, values, input.rect, input.info, onProgress);
    return { cancel: job.cancel, promise: job.promise.then((out) => out && { rect: input.rect, data: blendSelection(input, out) }) };
}

// --- Controles ----------------------------------------------------------------------------

// Lienzo nítido en pantallas de alta densidad: devuelve el contexto listo para pintar en píxeles
// CSS (el tamaño es el de la caja del elemento).
function begin(canvas) {
    const dpr = window.devicePixelRatio || 1, w = canvas.clientWidth || parseFloat(canvas.style.width), h = canvas.clientHeight || parseFloat(canvas.style.height);
    canvas.width = Math.max(1, Math.round(w * dpr));
    canvas.height = Math.max(1, Math.round(h * dpr));
    const p = canvas.getContext("2d");
    p.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { p, w, h };
}

const fixedCanvas = (w, h, className) => el("canvas", { className, style: { width: `${w}px`, height: `${h}px` } });

// Título de un parámetro. Paint.NET 5 solo pone la línea al lado en los selectores de ángulo y
// de color.
function header(text, line) {
    return el("div", { className: `fx-header${line ? " line" : ""}` }, [el("span", { textContent: text })]);
}

function resetButton(onClick) {
    const b = el("button", { className: "tbutton fx-reset", type: "button", title: tr("Restablecer") }, [icon("edit-reset", 16)]);
    b.addEventListener("click", onClick);
    return b;
}

function checkbox(text, checked, onChange) {
    const input = el("input", { type: "checkbox", checked: !!checked });
    input.addEventListener("change", () => onChange(input.checked));
    return { element: el("label", { className: "fx-check" }, [input, text]), input };
}

const format = (v, decimals) => Number(v).toFixed(decimals);

// Caja numérica (QSpinBox / QDoubleSpinBox): avisa al escribir y al usar las flechas.
function numberBox(min, max, decimals, value, onChange) {
    const step = decimals > 0 ? 10 ** -decimals * 10 : 1;
    const box = el("input", { type: "number", min, max, step, value: format(value, decimals) });
    box.addEventListener("input", () => {
        const v = Number(box.value);
        if (box.value !== "" && Number.isFinite(v) && v >= min && v <= max)
            onChange(v);
    });
    box.addEventListener("change", () => {
        const v = clamp(Number.isFinite(Number(box.value)) && box.value !== "" ? Number(box.value) : min, min, max);
        box.value = format(v, decimals);
        onChange(v);
    });
    box.set = (v) => (box.value = format(v, decimals));
    return box;
}

// Fila "deslizador + caja numérica + restablecer" (con la franja de degradado debajo si el
// parámetro la tiene: Matiz, Temperatura...).
function sliderRow(min, max, decimals, def, current, tip, onChange, gradient = null) {
    const scale = 10 ** decimals;
    const range = el("input", { type: "range", min: Math.round(min * scale), max: Math.round(max * scale), step: 1, value: Math.round(current * scale) });
    const box = numberBox(min, max, decimals, current, (v) => {
        range.value = Math.round(v * scale);
        onChange(v);
    });
    if (tip)
        range.title = box.title = tip;
    range.addEventListener("input", () => {
        const v = Number(range.value) / scale;
        box.set(v);
        onChange(v);
    });
    const set = (v) => {
        range.value = Math.round(v * scale);
        box.set(v);
    };
    const reset = resetButton(() => {
        set(def);
        onChange(def);
    });
    const column = el("div", { className: "fx-slider" }, [range]);
    if (gradient?.length)
        column.appendChild(el("div", { className: "fx-gradient",
                                       style: { background: `linear-gradient(to right, ${gradient.map(([r, g, b]) => `rgb(${r},${g},${b})`).join(", ")})` } }));
    return { row: el("div", { className: "fx-row" }, [column, box, reset]), set };
}

// Arrastre con el puntero capturado.
function drag(canvas, onDown, onMove, onUp = null) {
    canvas.addEventListener("pointerdown", (e) => {
        if (onDown(e) === false)
            return;
        canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener("pointermove", (e) => {
        if (canvas.hasPointerCapture(e.pointerId))
            onMove(e);
    });
    canvas.addEventListener("pointerup", (e) => onUp?.(e));
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
}

const localPos = (canvas, e) => {
    const r = canvas.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
};

// Selector circular de ángulo (AngleChooserControl de Paint.NET): arrastrar fija el ángulo;
// Mayús lo ajusta de 15 en 15°.
function angleChooser(min, max, value, onChange) {
    const canvas = fixedCanvas(64, 64, "fx-angle-chooser");
    canvas.title = tr("Arrastra para elegir el ángulo (Mayús: de 15 en 15°)");
    let current = clamp(value, min, max);
    const paint = () => {
        const { p } = begin(canvas);
        const c = 32, r = 29;
        p.lineWidth = 1;
        p.strokeStyle = theme("--border");
        p.fillStyle = theme("--base");
        p.beginPath();
        p.arc(c, c, r, 0, Math.PI * 2);
        p.fill();
        p.stroke();
        const a = (current * Math.PI) / 180, tip = [c + Math.cos(a) * (r - 2), c - Math.sin(a) * (r - 2)];
        p.strokeStyle = p.fillStyle = theme("--accent");
        p.lineWidth = 2;
        p.lineCap = "round";
        p.beginPath();
        p.moveTo(c, c);
        p.lineTo(tip[0], tip[1]);
        p.stroke();
        p.beginPath();
        p.arc(tip[0], tip[1], 3, 0, Math.PI * 2);
        p.arc(c, c, 2, 0, Math.PI * 2);
        p.fill();
    };
    const pick = (e) => {
        const { x, y } = localPos(canvas, e), dx = x - 32, dy = y - 32;
        if (!dx && !dy)
            return;
        let deg = (Math.atan2(-dy, dx) * 180) / Math.PI;
        if (e.shiftKey)
            deg = Math.round(deg / 15) * 15;
        while (deg < min) deg += 360;
        while (deg > max) deg -= 360;
        deg = clamp(deg, min, max);
        if (Math.abs(deg - current) < 1e-9)
            return;
        current = deg;
        paint();
        onChange(Number(deg.toFixed(2)));
    };
    drag(canvas, (e) => (e.button === 0 ? pick(e) : false), pick);
    requestAnimationFrame(paint);
    return { element: canvas, set(v) { current = clamp(v, min, max); paint(); } };
}

// Selector de centro/desplazamiento sobre una miniatura de la imagen (PanControl de Paint.NET).
// El valor va de -1 a 1 en cada eje.
function panControl(image, value, onChange) {
    const canvas = fixedCanvas(180, 120, "fx-pan");
    let current = { ...value };
    const area = () => {
        const aw = 176, ah = 116, s = image ? Math.min(aw / image.width, ah / image.height) : 1;
        const w = image ? image.width * s : aw, h = image ? image.height * s : ah;
        return { x: (180 - w) / 2, y: (120 - h) / 2, w, h };
    };
    const paint = () => {
        const { p } = begin(canvas), r = area();
        p.save();
        p.beginPath();
        p.rect(r.x, r.y, r.w, r.h);
        p.clip();
        p.fillStyle = checkerPattern(p);
        p.fillRect(r.x, r.y, r.w, r.h);
        p.restore();
        if (image) {
            p.imageSmoothingQuality = "high";
            p.drawImage(image, r.x, r.y, r.w, r.h);
        }
        p.strokeStyle = theme("--border");
        p.lineWidth = 1;
        p.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
        const cx = r.x + r.w / 2 + (current.x * r.w) / 2, cy = r.y + r.h / 2 + (current.y * r.h) / 2;
        for (const [color, width] of [["#fff", 3], ["#000", 1]]) {
            p.strokeStyle = color;
            p.lineWidth = width;
            p.beginPath();
            p.moveTo(cx - 9, cy); p.lineTo(cx - 3, cy);
            p.moveTo(cx + 3, cy); p.lineTo(cx + 9, cy);
            p.moveTo(cx, cy - 9); p.lineTo(cx, cy - 3);
            p.moveTo(cx, cy + 3); p.lineTo(cx, cy + 9);
            p.stroke();
            p.beginPath();
            p.arc(cx, cy, 6, 0, Math.PI * 2);
            p.stroke();
        }
    };
    const pick = (e) => {
        const { x, y } = localPos(canvas, e), r = area();
        current = { x: clamp((x - r.x - r.w / 2) / (r.w / 2), -1, 1), y: clamp((y - r.y - r.h / 2) / (r.h / 2), -1, 1) };
        paint();
        onChange({ ...current });
    };
    drag(canvas, (e) => (e.button === 0 ? pick(e) : false), pick);
    canvas.style.cursor = "crosshair";
    requestAnimationFrame(paint);
    return { element: canvas, set(v) { current = { x: clamp(v.x, -1, 1), y: clamp(v.y, -1, 1) }; paint(); } };
}

// Globo de "Rotación y giro" (RollBallAndSliders de Paint.NET): muestra en 3D el ángulo y la
// inclinación. Arrastrar inclina; con el botón derecho (o Mayús) gira.
function rollBall(maxTilt, value, onChange) {
    const canvas = fixedCanvas(96, 96, "fx-rollball");
    canvas.title = tr("Arrastra para inclinar; con el botón derecho (o Mayús) para girar");
    let v = { ...value }, rotating = false, last = null;
    const c = 48, radius = 45;
    const paint = () => {
        const { p } = begin(canvas);
        const shade = p.createRadialGradient(c - radius * 0.35, c - radius * 0.35, 0, c - radius * 0.35, c - radius * 0.35, radius * 1.4);
        shade.addColorStop(0, theme("--button-hover") || theme("--button"));
        shade.addColorStop(1, theme("--button"));
        p.fillStyle = shade;
        p.strokeStyle = theme("--border");
        p.lineWidth = 1;
        p.beginPath();
        p.arc(c, c, radius, 0, Math.PI * 2);
        p.fill();
        p.stroke();
        // La rotación del globo es la misma que aplica Giro y zum a la imagen: girar, inclinar
        // hacia la dirección. Se mira desde el polo (+z).
        const deg = Math.PI / 180, a = v.x * deg, dir = v.y * deg, tilt = v.z * deg;
        const rotZ = (q, t) => [q[0] * Math.cos(t) - q[1] * Math.sin(t), q[0] * Math.sin(t) + q[1] * Math.cos(t), q[2]];
        const transform = (x, y, z) => {
            let q = rotZ(rotZ([x, y, z], a), -dir);
            q = [q[0], q[1] * Math.cos(tilt) - q[2] * Math.sin(tilt), q[1] * Math.sin(tilt) + q[2] * Math.cos(tilt)];
            return rotZ(q, dir);
        };
        const text = theme("--text");
        const segment = (u, w) => {
            const visible = u[2] + w[2] > 0;
            p.globalAlpha = visible ? 200 / 255 : 40 / 255;
            p.strokeStyle = text;
            p.lineWidth = visible ? 1 : 0.8;
            p.beginPath();
            p.moveTo(c + u[0] * radius, c + u[1] * radius);
            p.lineTo(c + w[0] * radius, c + w[1] * radius);
            p.stroke();
        };
        for (let polar = 15; polar <= 90; polar += 15) {
            const t = polar * deg;
            let prev = null;
            for (let i = 0; i <= 48; ++i) {
                const ph = (i * 2 * Math.PI) / 48, q = transform(Math.sin(t) * Math.cos(ph), Math.sin(t) * Math.sin(ph), Math.cos(t));
                if (prev)
                    segment(prev, q);
                prev = q;
            }
        }
        for (let m = 0; m < 12; ++m) {
            const ph = (m * 2 * Math.PI) / 12;
            let prev = null;
            for (let i = 0; i <= 12; ++i) {
                const t = (i * (Math.PI / 2)) / 12, q = transform(Math.sin(t) * Math.cos(ph), Math.sin(t) * Math.sin(ph), Math.cos(t));
                if (prev)
                    segment(prev, q);
                prev = q;
            }
        }
        p.globalAlpha = 1;
        const pole = transform(0, 0, 1);
        p.fillStyle = p.strokeStyle = theme("--accent");
        p.lineWidth = 2;
        p.beginPath();
        p.arc(c + pole[0] * radius, c + pole[1] * radius, 2.5, 0, Math.PI * 2);
        p.fill();
        p.stroke();
    };
    const emit = () => {
        paint();
        onChange({ x: Number(v.x.toFixed(2)), y: Number(v.y.toFixed(2)), z: Number(v.z.toFixed(2)) });
    };
    const dragTo = (pos) => {
        // La distancia al centro es la inclinación y el ángulo, su dirección: el polo del globo
        // (el centro de la imagen) queda bajo el puntero.
        const vx = (pos.x - c) / radius, vy = (pos.y - c) / radius, len = Math.min(1, Math.hypot(vx, vy));
        const tilt = Math.min(maxTilt, (Math.asin(len) * 180) / Math.PI);
        let direction = len > 1e-6 ? (Math.atan2(vy, vx) * 180) / Math.PI + 90 : v.y;
        if (direction > 180)
            direction -= 360;
        v = { ...v, y: direction, z: tilt };
        emit();
    };
    drag(canvas, (e) => {
        if (e.button !== 0 && e.button !== 2)
            return false;
        rotating = e.button === 2 || e.shiftKey;
        last = localPos(canvas, e);
        canvas.style.cursor = "grabbing";
        if (!rotating)
            dragTo(last);
    }, (e) => {
        const pos = localPos(canvas, e);
        if (!rotating)
            return dragTo(pos);
        const before = Math.atan2(last.y - c, last.x - c), now = Math.atan2(pos.y - c, pos.x - c);
        let angle = v.x + ((now - before) * 180) / Math.PI;
        while (angle > 180) angle -= 360;
        while (angle < -180) angle += 360;
        last = pos;
        v = { ...v, x: angle };
        emit();
    }, () => (canvas.style.cursor = "grab"));
    canvas.style.cursor = "grab";
    requestAnimationFrame(paint);
    return { element: canvas, set(nv) { v = { ...nv }; paint(); } };
}

// Botón con una muestra del color; abre el selector de color con transparencia.
function colorButton(value, onChange) {
    let current = { ...value };
    const sample = el("span");
    const button = el("button", { className: "fx-color", type: "button", title: tr("Haz clic para elegir el color") }, [sample]);
    const paint = () => sample.style.setProperty("--c", cssColor(current));
    button.addEventListener("click", async () => {
        const c = await colorDialog(current);
        if (c) {
            current = c;
            paint();
            onChange({ ...c });
        }
    });
    paint();
    return { element: button, set(v) { current = { ...v }; paint(); } };
}

// Pestañas (QTabWidget) para los parámetros con 'tab'.
function tabWidget() {
    const bar = el("div", { className: "fx-tabbar", role: "tablist" }), pages = el("div", { className: "fx-pages" });
    const list = [];
    const show = (index) => list.forEach((t, i) => {
        t.tab.classList.toggle("current", i === index);
        t.page.style.display = i === index ? "" : "none";
    });
    return {
        element: el("div", { className: "fx-tabs" }, [bar, pages]),
        add(title) {
            const index = list.length;
            const tab = el("button", { className: "fx-tab", type: "button", role: "tab", textContent: title });
            const page = el("div", { className: "fx-page" });
            tab.addEventListener("click", () => show(index));
            bar.appendChild(tab);
            pages.appendChild(page);
            list.push({ tab, page });
            show(0);
            return page;
        },
    };
}

// --- Curvas -------------------------------------------------------------------------------

const CHANNEL_COLORS = [null, "rgb(220,40,40)", "rgb(40,170,40)", "rgb(40,80,230)"];

// Editor de curvas de Paint.NET: luminosidad o RGB, con casillas para editar a la vez los canales
// rojo, verde y azul que estén marcados. Clic para añadir o arrastrar puntos; clic derecho para
// quitarlos.
function curvesEditor(value, onChange) {
    let v = clone(value);
    let channel = 0, followers = [], dragIndex = -1;
    const mode = el("select", {}, [el("option", { value: 0, textContent: tr("Luminosidad") }), el("option", { value: 1, textContent: tr("RGB") })]);
    const checks = [tr("Rojo"), tr("Verde"), tr("Azul")].map((t) => checkbox(t, true, () => updateChannel()));
    const coords = el("span", { className: "coords" });
    const canvas = el("canvas", { className: "fx-curves" });
    const help = el("div", { className: "fx-description", textContent: tr("Sugerencia: pulse con el botón secundario del ratón para quitar puntos de control.") });
    const element = el("div", { className: "fx-curves-editor" }, [
        header(tr("Mapa de transferencia"), true),
        el("div", { className: "fx-row" }, [mode]),
        canvas,
        el("div", { className: "fx-row" }, [...checks.map((c) => c.element), el("span", { style: { flex: 1 } }), coords]),
        help,
    ]);
    const area = () => {
        const side = Math.min(canvas.clientWidth, canvas.clientHeight) - 10;
        return { x: 5, y: 5, side };
    };
    const toWidget = (pt) => {
        const a = area();
        return [a.x + (pt[0] / 255) * a.side, a.y + a.side - (pt[1] / 255) * a.side];
    };
    const toValue = (x, y) => {
        const a = area();
        return [clamp(((x - a.x) / a.side) * 255, 0, 255), clamp(((a.y + a.side - y) / a.side) * 255, 0, 255)];
    };
    const points = () => v.points[channel];
    const propagate = () => {
        for (const ch of followers)
            if (ch !== channel)
                v.points[ch] = points().map((q) => [...q]);
    };
    const paint = () => {
        if (!canvas.clientWidth)
            return;
        const { p } = begin(canvas), a = area();
        p.fillStyle = theme("--base");
        p.fillRect(a.x, a.y, a.side, a.side);
        p.strokeStyle = theme("--border");
        p.lineWidth = 1;
        p.setLineDash([1, 2]);
        p.beginPath();
        for (let i = 1; i < 4; ++i) {
            const t = Math.round((a.side * i) / 4) + 0.5;
            p.moveTo(a.x + t, a.y); p.lineTo(a.x + t, a.y + a.side);
            p.moveTo(a.x, a.y + t); p.lineTo(a.x + a.side, a.y + t);
        }
        p.moveTo(a.x, a.y + a.side);
        p.lineTo(a.x + a.side, a.y);
        p.stroke();
        p.setLineDash([]);
        p.strokeRect(a.x + 0.5, a.y + 0.5, a.side - 1, a.side - 1);
        const drawCurve = (ch, active) => {
            const lut = curveLut(v.points[ch]);
            p.beginPath();
            for (let i = 0; i < 256; ++i) {
                const [x, y] = toWidget([i, lut[i]]);
                i ? p.lineTo(x, y) : p.moveTo(x, y);
            }
            p.strokeStyle = ch === 0 ? theme("--text") : CHANNEL_COLORS[ch];
            p.globalAlpha = active ? 1 : 90 / 255;
            p.lineWidth = active ? 2 : 1;
            p.stroke();
            p.globalAlpha = 1;
        };
        if (v.mode === 1) {
            for (let ch = 1; ch < 4; ++ch)
                if (ch !== channel && !followers.includes(ch))
                    drawCurve(ch, false);
            for (const ch of followers)
                if (ch !== channel)
                    drawCurve(ch, true);
        }
        drawCurve(channel, true);
        p.strokeStyle = theme("--text");
        p.fillStyle = theme("--base");
        p.lineWidth = 1;
        for (const pt of points()) {
            const [x, y] = toWidget(pt);
            p.fillRect(x - 4, y - 4, 8, 8);
            p.strokeRect(x - 3.5, y - 3.5, 7, 7);
        }
    };
    const hit = (x, y) => points().findIndex((pt) => {
        const [px, py] = toWidget(pt);
        return Math.hypot(px - x, py - y) <= 7;
    });
    const edited = () => {
        propagate();
        paint();
        onChange(clone(v));
    };
    drag(canvas, (e) => {
        const { x, y } = localPos(canvas, e), index = hit(x, y), pts = points();
        if (e.button === 2) {
            if (index >= 0 && pts.length > 2) {
                pts.splice(index, 1);
                edited();
            }
            return false;
        }
        if (e.button !== 0)
            return false;
        if (index >= 0) {
            dragIndex = index;
            return;
        }
        const pt = toValue(x, y).map((n) => Math.round(n));
        pts.push(pt);
        pts.sort((q, w) => q[0] - w[0]);
        dragIndex = pts.indexOf(pt);
        edited();
    }, (e) => {
        if (dragIndex < 0)
            return;
        const { x, y } = localPos(canvas, e), pts = points();
        const lo = dragIndex > 0 ? pts[dragIndex - 1][0] + 1 : 0, hi = dragIndex < pts.length - 1 ? pts[dragIndex + 1][0] - 1 : 255;
        const pt = toValue(x, y).map((n) => Math.round(n));
        pts[dragIndex] = [clamp(pt[0], lo, hi), pt[1]];
        edited();
    }, () => (dragIndex = -1));
    canvas.addEventListener("pointermove", (e) => {
        const { x, y } = localPos(canvas, e), a = area();
        const inside = x >= a.x && y >= a.y && x <= a.x + a.side && y <= a.y + a.side;
        const [vx, vy] = toValue(x, y);
        coords.textContent = inside ? `(${Math.round(vx)}, ${Math.round(vy)})` : "";
    });
    canvas.addEventListener("pointerleave", () => (coords.textContent = ""));
    const updateChannel = () => {
        const rgb = v.mode === 1;
        checks.forEach((c) => (c.element.style.display = rgb ? "" : "none"));
        if (!rgb) {
            channel = 0;
            followers = [];
        } else {
            followers = [1, 2, 3].filter((ch) => checks[ch - 1].input.checked);
            channel = followers[0] ?? 1;
        }
        paint();
    };
    mode.addEventListener("change", () => {
        v.mode = Number(mode.value);
        updateChannel();
        onChange(clone(v));
    });
    const reset = el("button", { className: "pushbutton", type: "button", textContent: tr("Restablecer") });
    reset.addEventListener("click", () => {
        v = { ...defaultCurves(), mode: v.mode };
        paint();
        onChange(clone(v));
    });
    new ResizeObserver(paint).observe(canvas);
    mode.value = v.mode;
    updateChannel();
    return {
        element,
        buttons: [reset],
        set(nv) {
            v = clone(nv);
            mode.value = v.mode;
            updateChannel();
        },
    };
}

// --- Niveles ------------------------------------------------------------------------------

// Histograma vertical de los canales R, G y B (el blanco arriba).
function paintHistogram(canvas, hist, shown, towardsRight) {
    const { p, w, h } = begin(canvas);
    p.fillStyle = theme("--base");
    p.fillRect(0, 0, w, h);
    let peak = 1;
    for (let c = 0; c < 3; ++c)
        if (shown[c])
            for (const n of hist[c])
                peak = Math.max(peak, n);
    const dark = document.documentElement.classList.contains("dark");
    p.globalCompositeOperation = dark ? "screen" : "multiply";
    const colors = ["rgba(230,40,40,0.47)", "rgba(40,190,40,0.47)", "rgba(40,80,240,0.47)"];
    for (let c = 0; c < 3; ++c) {
        if (!shown[c])
            continue;
        const base = towardsRight ? 0.5 : w - 0.5, dir = towardsRight ? 1 : -1;
        p.beginPath();
        p.moveTo(base, h - 0.5);
        for (let i = 0; i < 256; ++i)
            p.lineTo(base + dir * Math.sqrt(hist[c][i] / peak) * (w - 1), h - 0.5 - ((h - 1) * i) / 255);
        p.lineTo(base, 0.5);
        p.closePath();
        p.fillStyle = colors[c];
        p.fill();
    }
    p.globalCompositeOperation = "source-over";
    p.strokeStyle = theme("--border");
    p.lineWidth = 1;
    p.strokeRect(0.5, 0.5, w - 1, h - 1);
}

// Barra de degradado vertical con tiradores triangulares (los de Niveles).
function levelsGradient(handles, onMove) {
    const canvas = el("canvas", { className: "fx-levels-bar" });
    let values = handles === 2 ? [0, 255] : [0, 128, 255], tint = "#fff", dragIndex = -1;
    const bar = () => ({ x: 7, y: 6, w: canvas.clientWidth - 14, h: canvas.clientHeight - 12 });
    const yFor = (v) => {
        const b = bar();
        return b.y + b.h - (b.h * v) / 255;
    };
    const paint = () => {
        if (!canvas.clientHeight)
            return;
        const { p, w } = begin(canvas), b = bar();
        const g = p.createLinearGradient(0, b.y + b.h, 0, b.y);
        g.addColorStop(0, "#000");
        g.addColorStop(1, tint);
        p.fillStyle = g;
        p.fillRect(b.x, b.y, b.w, b.h);
        p.strokeStyle = theme("--border");
        p.lineWidth = 1;
        p.strokeRect(b.x + 0.5, b.y + 0.5, b.w - 1, b.h - 1);
        values.forEach((v, i) => {
            const y = yFor(v), t = values.length === 1 ? 0.5 : i / (values.length - 1), grey = Math.round(t * 255);
            p.fillStyle = `rgb(${grey},${grey},${grey})`;
            p.strokeStyle = theme("--text");
            for (const pts of [[[b.x - 1, y], [0.5, y - 5], [0.5, y + 5]], [[b.x + b.w + 1, y], [w - 0.5, y - 5], [w - 0.5, y + 5]]]) {
                p.beginPath();
                pts.forEach(([x, yy], k) => (k ? p.lineTo(x, yy) : p.moveTo(x, yy)));
                p.closePath();
                p.fill();
                p.stroke();
            }
        });
    };
    const valueAt = (y) => {
        const b = bar();
        return clamp(((b.y + b.h - y) / b.h) * 255, 0, 255);
    };
    const move = (e) => {
        if (dragIndex < 0)
            return;
        let v = valueAt(localPos(canvas, e).y);
        if (dragIndex > 0)
            v = Math.max(v, values[dragIndex - 1] + 1);
        if (dragIndex < values.length - 1)
            v = Math.min(v, values[dragIndex + 1] - 1);
        values[dragIndex] = v;
        paint();
        onMove(dragIndex, v);
    };
    drag(canvas, (e) => {
        if (e.button !== 0)
            return false;
        const y = localPos(canvas, e).y;
        let best = Infinity;
        values.forEach((v, i) => {
            const d = Math.abs(yFor(v) - y);
            if (d < best) {
                best = d;
                dragIndex = i;
            }
        });
        move(e);
    }, move, () => (dragIndex = -1));
    new ResizeObserver(paint).observe(canvas);
    return {
        element: canvas,
        set(v) { values = [...v]; paint(); },
        setTint(c) { tint = c; paint(); },
    };
}

// Editor de Niveles de Paint.NET: histogramas de entrada y salida, entrada negro/blanco, salida
// negro/gris (gamma)/blanco, Automático, Restablecer y casillas R, V, A para elegir qué canales se
// ajustan.
function levelsEditor(input, value, onChange) {
    let v = clone(value), syncing = false;
    const { src, rect, layer } = input;
    const inHistData = histogram(src, rect);
    // Muestra pequeña (como mucho 256 px de lado) para el histograma de salida.
    const scale = Math.min(1, 256 / Math.max(1, rect.w, rect.h));
    const small = createCanvas(Math.max(1, Math.round(rect.w * scale)), Math.max(1, Math.round(rect.h * scale)));
    const sctx = context(small);
    sctx.imageSmoothingEnabled = false;
    sctx.drawImage(layer.canvas, rect.x, rect.y, Math.max(1, rect.w), Math.max(1, rect.h), 0, 0, small.width, small.height);
    const smallData = sctx.getImageData(0, 0, small.width, small.height);

    const inHist = el("canvas", { className: "fx-histogram" }), outHist = el("canvas", { className: "fx-histogram" });
    const spin = (def, onSet) => numberBox(0, 255, 0, def, (n) => !syncing && onSet(Math.round(n)));
    const inLow = spin(0, (n) => edit((l, c) => (l.inLow[c] = n)));
    const inHigh = spin(255, (n) => edit((l, c) => (l.inHigh[c] = n)));
    const outLow = spin(0, (n) => edit((l, c) => (l.outLow[c] = n)));
    const outHigh = spin(255, (n) => edit((l, c) => (l.outHigh[c] = n)));
    const gamma = numberBox(0.1, 10, 2, 1, (n) => !syncing && edit((l, c) => (l.gamma[c] = n)));
    const swatches = [0, 1, 2, 3, 4].map(() => el("span", { className: "fx-swatch" }));
    const inBar = levelsGradient(2, (i, n) => {
        const box = i === 0 ? inLow : inHigh;
        box.set(Math.round(n));
        box.dispatchEvent(new Event("input"));
    });
    const outBar = levelsGradient(3, (i, n) => {
        if (i === 0 || i === 2) {
            const box = i === 0 ? outLow : outHigh;
            box.set(Math.round(n));
            box.dispatchEvent(new Event("input"));
            return;
        }
        const lo = Number(outLow.value), hi = Number(outHigh.value);
        const t = clamp((n - lo) / Math.max(1, hi - lo), 0.001, 0.999);
        gamma.set(clamp(Math.log(t) / Math.log(0.5), 0.1, 10));
        gamma.dispatchEvent(new Event("input"));
    });
    const checks = [[tr("R"), tr("Rojo")], [tr("V"), tr("Verde")], [tr("A"), tr("Azul")]].map(([t, tip]) => {
        const c = checkbox(t, true, () => sync());
        c.element.title = tip;
        return c;
    });
    const column = (...items) => el("div", { className: "fx-levels-column" }, items);
    const spacer = () => el("span", { style: { flex: 1 } });
    const grid = el("div", { className: "fx-levels" }, [
        header(tr("Histograma de entrada"), true), el("div", { className: "span2" }, [header(tr("Entrada"), true)]),
        el("div", { className: "span2" }, [header(tr("Salida"), true)]), header(tr("Histograma de salida"), true),
        inHist, column(inHigh, swatches[0], spacer(), swatches[1], inLow), inBar.element, outBar.element,
        column(outHigh, swatches[2], gamma, swatches[3], spacer(), swatches[4], outLow), outHist,
    ]);
    const element = el("div", { className: "fx-levels-editor" }, [grid, el("div", { className: "fx-row fx-center" }, checks.map((c) => c.element))]);

    const channels = () => [0, 1, 2].filter((c) => checks[c].input.checked);
    const edit = (f) => {
        for (const c of channels())
            f(v, c);
        sync();
        onChange(clone(v));
    };
    const refreshOutput = () => {
        const d = smallData.data, lr = levelsLut(v, 0), lg = levelsLut(v, 1), lb = levelsLut(v, 2);
        const out = [new Float64Array(256), new Float64Array(256), new Float64Array(256)];
        for (let i = 0; i < d.length; i += 4)
            if (d[i + 3]) {
                ++out[0][lr[d[i]]];
                ++out[1][lg[d[i + 1]]];
                ++out[2][lb[d[i + 2]]];
            }
        return out;
    };
    let outHistData = null;
    const sync = () => {
        const chans = channels();
        const avg = (arr) => (chans.length ? chans.reduce((s, c) => s + arr[c], 0) / chans.length : arr[0]);
        syncing = true;
        inLow.set(Math.round(avg(v.inLow)));
        inHigh.set(Math.round(avg(v.inHigh)));
        outLow.set(Math.round(avg(v.outLow)));
        outHigh.set(Math.round(avg(v.outHigh)));
        gamma.set(avg(v.gamma));
        syncing = false;
        const lo = Number(outLow.value), hi = Number(outHigh.value), g = Number(gamma.value);
        const mid = lo + (hi - lo) * Math.pow(0.5, g);
        inBar.set([Number(inLow.value), Number(inHigh.value)]);
        outBar.set([lo, mid, hi]);
        const shown = [0, 1, 2].map((c) => checks[c].input.checked);
        const tint = `rgb(${shown[0] ? 255 : 0},${shown[1] ? 255 : 0},${shown[2] ? 255 : 0})`;
        inBar.setTint(tint);
        outBar.setTint(tint);
        const swatch = (s, n) => (s.style.background = `rgb(${shown[0] ? n : 0},${shown[1] ? n : 0},${shown[2] ? n : 0})`);
        swatch(swatches[0], Number(inHigh.value));
        swatch(swatches[1], Number(inLow.value));
        swatch(swatches[2], hi);
        swatch(swatches[3], Math.round(mid));
        swatch(swatches[4], lo);
        outHistData = refreshOutput();
        paintHistograms();
    };
    const paintHistograms = () => {
        const shown = [0, 1, 2].map((c) => checks[c].input.checked);
        if (inHist.clientHeight)
            paintHistogram(inHist, inHistData, shown, false);
        if (outHist.clientHeight && outHistData)
            paintHistogram(outHist, outHistData, shown, true);
    };
    new ResizeObserver(paintHistograms).observe(inHist);
    const autoButton = el("button", { className: "pushbutton", type: "button", textContent: tr("Automático"),
                                      title: tr("Ajusta los niveles según el histograma de la imagen") });
    autoButton.addEventListener("click", () => {
        v = automaticLevels(src, rect);
        sync();
        onChange(clone(v));
    });
    const reset = el("button", { className: "pushbutton", type: "button", textContent: tr("Restablecer") });
    reset.addEventListener("click", () => {
        v = defaultLevels();
        sync();
        onChange(clone(v));
    });
    sync();
    return {
        element,
        buttons: [autoButton, reset],
        set(nv) {
            v = clone(nv);
            sync();
        },
    };
}

// --- Diálogo ------------------------------------------------------------------------------

function resolveColor(c) {
    if (c && typeof c === "object")
        return { r: c.r, g: c.g, b: c.b, a: c.a ?? 255 };
    if (c === "secondary")
        return { ...AppState.secondary, a: AppState.secondary.a ?? 255 };
    if (c === "black")
        return { r: 0, g: 0, b: 0, a: 255 };
    if (c === "white")
        return { r: 255, g: 255, b: 255, a: 255 };
    return { ...AppState.primary, a: AppState.primary.a ?? 255 };
}

// Miniatura de la zona del efecto (la de los selectores de centro).
function zoneThumbnail(input) {
    const { rect, layer } = input;
    const zone = createCanvas(Math.max(1, rect.w), Math.max(1, rect.h));
    context(zone).drawImage(layer.canvas, rect.x, rect.y, Math.max(1, rect.w), Math.max(1, rect.h), 0, 0, zone.width, zone.height);
    return thumbnail(zone, 220, 160, false);
}

// Muestra el diálogo con la vista previa en la capa. Devuelve null si se cancela o
// { values, result, input }: 'result' es la última vista previa si ya está calculada con esos
// valores (si no, null y quien llama la calcula).
export async function effectDialog(app, effect, initial) {
    const doc = app.doc, input = effectInput(doc), layer = input.layer, params = effect.params;
    let values = { ...defaultValues(effect), ...clone(initial || {}) };
    for (const p of params)
        if (p.type === "color")
            values[p.key] = resolveColor(values[p.key]);

    const dlg = new Dialog(tr(effect.name), { iconName: `effect-${effect.id}` });
    // Sin oscurecer la imagen, para ver bien la vista previa (como Kaint).
    dlg.backdrop.classList.add("see-through");
    dlg.element.classList.add("effect-dialog");
    const root = dlg.body;
    const setters = new Map(), containers = new Map(), bodies = new Map(), extraButtons = [];
    let thumb = null;
    const zoneThumb = () => (thumb ||= zoneThumbnail(input));

    // Casillas que van en el título de otro parámetro (no se muestran solas).
    const titleChecks = new Set(params.filter((p) => p.enableKey).map((p) => p.enableKey));
    let tabs = null;
    const pages = new Map();
    for (const p of params) {
        if (!p.tab || pages.has(p.tab))
            continue;
        if (!tabs) {
            tabs = tabWidget();
            root.appendChild(tabs.element);
        }
        pages.set(p.tab, tabs.add(tr(p.tab)));
    }

    // Deshacer y rehacer los cambios del diálogo: un paso por cambio (un arrastre seguido cuenta
    // como uno).
    const snapshot = () => clone(values);
    let committed = snapshot(), undoStack = [], redoStack = [], stepOpen = false, stepTimer = 0, restoring = false, linking = false;
    const closeStep = () => {
        clearTimeout(stepTimer);
        stepOpen = false;
        committed = snapshot();
    };
    const updateReadOnly = () => {
        for (const p of params) {
            containers.get(p.key)?.classList.toggle("disabled", isReadOnly(p, values));
            bodies.get(p.key)?.classList.toggle("disabled", !values[p.enableKey]);
        }
    };
    const set = (key, value) => {
        values[key] = value;
        if (!linking) {
            linking = true;
            const p = params.find((q) => q.key === key);
            if (p?.link && values[p.link])
                for (const other of params) {
                    if (other.key === key || other.link !== p.link)
                        continue;
                    const v = other.type === "int" ? clamp(Math.round(value), other.min, other.max) : clamp(value, other.min, other.max);
                    values[other.key] = v;
                    setters.get(other.key)?.(v);
                }
            const first = value === true && params.find((q) => q.link === key);
            if (first)
                for (const other of params)
                    if (other.link === key && other.key !== first.key) {
                        values[other.key] = values[first.key];
                        setters.get(other.key)?.(values[first.key]);
                    }
            linking = false;
        }
        updateReadOnly();
        if (!restoring && JSON.stringify(values) !== JSON.stringify(committed)) {
            if (!stepOpen) {
                undoStack.push(committed);
                redoStack = [];
                stepOpen = true;
            }
            clearTimeout(stepTimer);
            stepTimer = setTimeout(closeStep, 600);
        }
        requestPreview();
    };
    const stepHistory = (redo) => {
        closeStep();
        const from = redo ? redoStack : undoStack, to = redo ? undoStack : redoStack;
        if (!from.length)
            return;
        to.push(snapshot());
        const target = from.pop();
        restoring = true;
        for (const [key, v] of Object.entries(target))
            setters.get(key)?.(v);
        restoring = false;
        values = target;
        committed = snapshot();
        updateReadOnly();
        requestPreview();
    };

    for (const p of params) {
        if (titleChecks.has(p.key))
            continue;
        const container = el("div", { className: `fx-param fx-type-${p.type}` });
        (pages.get(p.tab) || root).appendChild(container);
        containers.set(p.key, container);
        let target = container, titled = false;
        if (p.enableKey) {
            const check = checkbox(tr(p.label), values[p.enableKey], (on) => set(p.enableKey, on));
            setters.set(p.enableKey, (v) => (check.input.checked = !!v));
            const inner = el("div");
            container.append(check.element, inner);
            bodies.set(p.key, inner);
            target = inner;
            titled = true;
        }
        const title = (line) => !titled && p.label && target.appendChild(header(tr(p.label), line));
        const describe = () => p.description && p.type !== "bool" && target.appendChild(el("div", { className: "fx-description", textContent: tr(p.description) }));
        switch (p.type) {
        case "int":
        case "real": {
            title(false);
            describe();
            const decimals = p.type === "int" ? 0 : p.decimals ?? 2;
            const row = sliderRow(p.min, p.max, decimals, p.def, values[p.key] ?? p.def, "", (v) => set(p.key, p.type === "int" ? Math.round(v) : v), p.gradient);
            setters.set(p.key, row.set);
            target.appendChild(row.row);
            break;
        }
        case "bool": {
            if (p.description)
                title(false);
            const check = checkbox(tr(p.description || p.label), values[p.key], (on) => set(p.key, on));
            setters.set(p.key, (v) => (check.input.checked = !!v));
            target.appendChild(check.element);
            break;
        }
        case "choice": {
            const select = el("select", {}, p.choices.map((t, i) => el("option", { value: i, textContent: tr(t) })));
            select.value = values[p.key];
            select.addEventListener("change", () => set(p.key, Number(select.value)));
            setters.set(p.key, (v) => (select.value = v));
            target.appendChild(el("label", { className: "fx-choice" }, [el("span", { textContent: `${tr(p.label)}:` }), select]));
            break;
        }
        case "rollball": {
            title(true);
            let vec = { x: 0, y: 0, z: 0, ...values[p.key] };
            const changed = (part) => {
                vec = { ...vec, ...part };
                ball.set(vec);
                set(p.key, { ...vec });
            };
            const rows = [
                sliderRow(-180, 180, 2, 0, vec.x, tr("Ángulo"), (x) => changed({ x })),
                sliderRow(-180, 180, 2, 0, vec.y, tr("Dirección de la inclinación"), (y) => changed({ y })),
                sliderRow(-p.max, p.max, 2, 0, vec.z, tr("Inclinación"), (z) => changed({ z })),
            ];
            const ball = rollBall(p.max, vec, (nv) => {
                vec = nv;
                rows[0].set(nv.x); rows[1].set(nv.y); rows[2].set(nv.z);
                set(p.key, { ...nv });
            });
            setters.set(p.key, (nv) => {
                vec = { ...nv };
                ball.set(vec);
                rows[0].set(vec.x); rows[1].set(vec.y); rows[2].set(vec.z);
            });
            target.appendChild(el("div", { className: "fx-row fx-top" }, [ball.element, el("div", { className: "fx-sliders" }, rows.map((r) => r.row))]));
            break;
        }
        case "seed": {
            describe();
            const button = el("button", { className: "pushbutton", type: "button", textContent: tr(p.label || "Aleatorizar"),
                                          title: tr("Genera otro resultado aleatorio") });
            button.addEventListener("click", () => set(p.key, Math.floor(Math.random() * 256)));
            target.appendChild(el("div", { className: "fx-row" }, [button]));
            break;
        }
        case "angle": {
            title(true);
            describe();
            const decimals = p.decimals ?? 2;
            const box = numberBox(p.min, p.max, decimals, values[p.key], (v) => {
                chooser.set(v);
                set(p.key, v);
            });
            const chooser = angleChooser(p.min, p.max, values[p.key], (v) => {
                box.set(v);
                set(p.key, v);
            });
            const reset = resetButton(() => {
                chooser.set(p.def);
                box.set(p.def);
                set(p.key, p.def);
            });
            setters.set(p.key, (v) => {
                chooser.set(v);
                box.set(v);
            });
            target.appendChild(el("div", { className: "fx-row" }, [chooser.element, el("span", { style: { flex: 1 } }), box, reset]));
            break;
        }
        case "offset": {
            title(false);
            describe();
            const decimals = p.decimals ?? 2, minY = p.minY ?? p.min, maxY = p.maxY ?? p.max, def = p.def || { x: 0, y: 0 };
            const toPan = (v) => ({ x: p.max > p.min ? ((v.x - p.min) / (p.max - p.min)) * 2 - 1 : 0, y: maxY > minY ? ((v.y - minY) / (maxY - minY)) * 2 - 1 : 0 });
            const fromPan = (v) => ({ x: Number((p.min + ((v.x + 1) / 2) * (p.max - p.min)).toFixed(decimals)),
                                      y: Number((minY + ((v.y + 1) / 2) * (maxY - minY)).toFixed(decimals)) });
            let cur = { x: 0, y: 0, ...values[p.key] };
            const changed = (part) => {
                cur = { ...cur, ...part };
                pan.set(toPan(cur));
                set(p.key, { ...cur });
            };
            const rx = sliderRow(p.min, p.max, decimals, def.x, cur.x, tr("X"), (x) => changed({ x }));
            const ry = sliderRow(minY, maxY, decimals, def.y, cur.y, tr("Y"), (y) => changed({ y }));
            const pan = panControl(zoneThumb(), toPan(cur), (pv) => {
                cur = fromPan(pv);
                rx.set(cur.x);
                ry.set(cur.y);
                set(p.key, { ...cur });
            });
            setters.set(p.key, (v) => {
                cur = { ...v };
                rx.set(cur.x);
                ry.set(cur.y);
                pan.set(toPan(cur));
            });
            target.appendChild(el("div", { className: "fx-row fx-top" }, [pan.element, el("div", { className: "fx-sliders" }, [rx.row, ry.row])]));
            break;
        }
        case "color": {
            title(true);
            describe();
            const button = colorButton(values[p.key], (c) => set(p.key, c));
            setters.set(p.key, button.set);
            target.appendChild(el("div", { className: "fx-row" }, [button.element]));
            break;
        }
        case "curves": {
            const editor = curvesEditor(values[p.key] || defaultCurves(), (v) => set(p.key, v));
            extraButtons.push(...editor.buttons);
            setters.set(p.key, editor.set);
            target.appendChild(editor.element);
            break;
        }
        case "levels": {
            const editor = levelsEditor(input, values[p.key] || defaultLevels(), (v) => set(p.key, v));
            extraButtons.push(...editor.buttons);
            setters.set(p.key, editor.set);
            target.appendChild(editor.element);
            break;
        }
        case "text": {
            title(false);
            describe();
            const maxLength = p.max > 0 ? p.max : 4096;
            const edit = p.multiline ? el("textarea", { value: values[p.key] ?? "", maxLength, rows: 5 })
                                     : el("input", { type: "text", value: values[p.key] ?? "", maxLength });
            edit.addEventListener("input", () => set(p.key, edit.value));
            setters.set(p.key, (v) => (edit.value = v ?? ""));
            target.appendChild(edit);
            break;
        }
        }
    }
    updateReadOnly();
    if (effect.note)
        root.appendChild(el("div", { className: "fx-note", textContent: tr(effect.note) }));

    // Botones: los propios del editor (Restablecer, Automático) a la izquierda.
    for (const b of extraButtons)
        dlg.buttons.appendChild(b);
    extraButtons.at(-1)?.classList.add("push-left");
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);

    // Ctrl+Z / Ctrl+Y (o Ctrl+Mayús+Z) deshacen y rehacen los cambios del diálogo; en los campos de
    // texto siguen siendo los del propio campo.
    dlg.element.addEventListener("keydown", (e) => {
        if (!(e.ctrlKey || e.metaKey) || e.altKey)
            return;
        const key = e.key.toLowerCase(), undo = key === "z" && !e.shiftKey, redo = key === "y" || (key === "z" && e.shiftKey);
        if (!undo && !redo)
            return;
        if (e.target.matches?.("textarea, input[type=text]"))
            return;
        e.preventDefault();
        e.stopPropagation();
        stepHistory(redo);
    }, true);

    // Vista previa en segundo plano: cada cambio cancela la anterior (como EffectPreview).
    const preview = cloneCanvas(layer.canvas);
    let job = null, previewTimer = 0, last = null;
    function requestPreview() {
        clearTimeout(previewTimer);
        previewTimer = setTimeout(startPreview, 40);
    }
    const startPreview = () => {
        job?.cancel();
        const snap = snapshot(), key = JSON.stringify(snap);
        const current = computeEffect(input, effect, snap, (f) => app.setEffectProgress?.(f));
        job = current;
        current.promise.then((res) => {
            if (job !== current)
                return;
            job = null;
            app.setEffectProgress?.(null);
            if (!res)
                return;
            last = { key, rect: res.rect, data: res.data };
            context(preview).putImageData(res.data, res.rect.x, res.rect.y);
            doc.setPreview({ layer, canvas: preview }, res.rect);
        }, (err) => {
            if (job === current) {
                job = null;
                app.setEffectProgress?.(null);
            }
            console.error(err);
        });
    };
    startPreview();

    const accepted = await dlg.exec();
    clearTimeout(previewTimer);
    clearTimeout(stepTimer);
    const key = JSON.stringify(values);
    const ready = accepted && !job && last?.key === key ? { rect: last.rect, data: last.data } : null;
    if (!ready) {
        job?.cancel();
        job = null;
        app.setEffectProgress?.(null);
    }
    if (!accepted) {
        doc.setPreview(null);
        return null;
    }
    return { values, result: ready, input };
}

// Diálogo de un ajuste o efecto (src/app/dialogs/EffectDialog.cpp): un control por parámetro
// con su botón de restablecer y la vista previa al momento en la capa activa.
import { AppState } from "../AppState.js";
import { curveTable, defaultValues } from "../core/Effects.js";
import { cloneCanvas, context, hex, parseHex } from "../core/Raster.js";
import { Dialog, el } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { tr } from "../i18n.js";

// Ejecuta el efecto sobre la capa activa (solo dentro de la selección) → { rect, data }.
export function renderEffect(doc, effect, values) {
    const layer = doc.activeLayer, sel = doc.selection;
    const rect = sel.bounds() || { x: 0, y: 0, w: doc.width, h: doc.height };
    const src = layer.ctx.getImageData(0, 0, doc.width, doc.height);
    const info = { primary: AppState.primary, secondary: AppState.secondary, selection: sel.mask || null };
    const out = effect.render(src, values, rect, info);
    const data = new ImageData(rect.w, rect.h);
    const o = out.data, s = src.data, d = data.data, w = doc.width;
    for (let y = 0; y < rect.h; ++y)
        for (let x = 0; x < rect.w; ++x) {
            const i = ((rect.y + y) * w + rect.x + x) * 4, j = (y * rect.w + x) * 4;
            const m = sel.mask ? sel.mask[(rect.y + y) * w + rect.x + x] / 255 : 1;
            for (let c = 0; c < 4; ++c)
                d[j + c] = s[i + c] + (o[i + c] - s[i + c]) * m;
        }
    return { rect, data };
}

function resolveColor(def) {
    if (def === "primary") return { ...AppState.primary };
    if (def === "secondary") return { ...AppState.secondary };
    if (def === "black") return { r: 0, g: 0, b: 0, a: 255 };
    return def;
}

// Editor de curvas: arrastrar los puntos; clic para añadir; arrastrarlos fuera para quitarlos.
function curveEditor(points, onChange) {
    const size = 256;
    const canvas = el("canvas", { className: "curve-editor", width: size, height: size, style: { width: `${size}px`, height: `${size}px` } });
    let pts = points.map((p) => [...p]), dragging = -1;
    const draw = () => {
        const ctx = canvas.getContext("2d"), css = getComputedStyle(document.documentElement);
        ctx.clearRect(0, 0, size, size);
        ctx.strokeStyle = css.getPropertyValue("--line");
        ctx.lineWidth = 1;
        for (let i = 1; i < 4; ++i) {
            ctx.beginPath();
            ctx.moveTo((i * size) / 4 + 0.5, 0); ctx.lineTo((i * size) / 4 + 0.5, size);
            ctx.moveTo(0, (i * size) / 4 + 0.5); ctx.lineTo(size, (i * size) / 4 + 0.5);
            ctx.stroke();
        }
        const t = curveTable(pts);
        ctx.strokeStyle = css.getPropertyValue("--text");
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        for (let x = 0; x < 256; ++x)
            x ? ctx.lineTo(x, 255 - t[x]) : ctx.moveTo(x, 255 - t[x]);
        ctx.stroke();
        ctx.fillStyle = css.getPropertyValue("--accent");
        for (const [x, y] of pts) {
            ctx.beginPath();
            ctx.arc(x, 255 - y, 4, 0, Math.PI * 2);
            ctx.fill();
        }
    };
    const at = (e) => {
        const r = canvas.getBoundingClientRect();
        return [Math.round(Math.min(255, Math.max(0, e.clientX - r.left))), Math.round(Math.min(255, Math.max(0, 255 - (e.clientY - r.top))))];
    };
    canvas.addEventListener("pointerdown", (e) => {
        const p = at(e);
        dragging = pts.findIndex(([x, y]) => Math.hypot(x - p[0], y - p[1]) < 8);
        if (dragging < 0) {
            pts.push(p);
            dragging = pts.length - 1;
        }
        canvas.setPointerCapture(e.pointerId);
        draw();
        onChange(pts);
    });
    canvas.addEventListener("pointermove", (e) => {
        if (dragging < 0)
            return;
        const r = canvas.getBoundingClientRect(), p = at(e);
        const outside = e.clientX < r.left - 20 || e.clientX > r.right + 20 || e.clientY < r.top - 20 || e.clientY > r.bottom + 20;
        if (outside && pts.length > 2) {
            pts.splice(dragging, 1);
            dragging = -1;
        } else {
            pts[dragging] = p;
        }
        draw();
        onChange(pts);
    });
    canvas.addEventListener("pointerup", () => (dragging = -1));
    canvas.reset = (p) => {
        pts = p.map((q) => [...q]);
        draw();
    };
    draw();
    return canvas;
}

export async function effectDialog(app, effect, initial) {
    const doc = app.doc;
    const values = { ...defaultValues(effect), ...(initial || {}) };
    for (const p of effect.params)
        if (p.type === "color")
            values[p.key] = resolveColor(values[p.key]);
    const dlg = new Dialog(tr(effect.name), { iconName: `effect-${effect.id}` });
    const body = el("div", { style: { width: "340px" } });
    dlg.body.appendChild(body);
    const layer = doc.activeLayer;
    const preview = cloneCanvas(layer.canvas);
    let timer = 0;
    const update = () => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            const { rect, data } = renderEffect(doc, effect, values);
            context(preview).putImageData(data, rect.x, rect.y);
            doc.setPreview({ layer, canvas: preview }, rect);
        }, 40);
    };
    const resetButton = (fn) => {
        const b = el("button", { className: "tbutton", title: tr("Restablecer") }, [icon("edit-reset", 16)]);
        b.addEventListener("click", fn);
        return b;
    };
    for (const p of effect.params) {
        const box = el("div", { className: "param" });
        if (p.label && p.type !== "bool" && p.type !== "seed")
            box.appendChild(el("div", { className: "label", textContent: tr(p.label) }));
        if (p.type === "int" || p.type === "real" || p.type === "angle") {
            const step = p.step ?? (p.type === "real" ? 1 / 10 ** (p.decimals ?? 2) : 1);
            const range = el("input", { type: "range", min: p.min, max: p.max, step, value: values[p.key] });
            const num = el("input", { type: "number", min: p.min, max: p.max, step, value: values[p.key] });
            const setValue = (v) => {
                v = Math.min(p.max, Math.max(p.min, Number(v)));
                values[p.key] = v;
                range.value = num.value = v;
                update();
            };
            range.addEventListener("input", () => setValue(range.value));
            num.addEventListener("change", () => setValue(num.value));
            box.append(range, num, resetButton(() => setValue(p.def)));
        } else if (p.type === "bool") {
            const c = el("input", { type: "checkbox", checked: values[p.key] });
            c.addEventListener("change", () => { values[p.key] = c.checked; update(); });
            box.appendChild(el("label", { style: { gridColumn: "1 / -1" } }, [c, tr(p.label)]));
        } else if (p.type === "choice") {
            const s = el("select", {}, p.choices.map((t, i) => el("option", { value: i, textContent: tr(t) })));
            s.value = values[p.key];
            s.addEventListener("change", () => { values[p.key] = Number(s.value); update(); });
            box.append(s, el("span"), resetButton(() => { s.value = values[p.key] = p.def; update(); }));
        } else if (p.type === "offset") {
            const sliders = ["x", "y"].map((axis) => {
                const range = el("input", { type: "range", min: -1, max: 1, step: 0.01, value: values[p.key][axis] });
                range.addEventListener("input", () => { values[p.key] = { ...values[p.key], [axis]: Number(range.value) }; update(); });
                return range;
            });
            box.append(el("span", { style: { display: "grid", gap: "2px" } }, sliders), el("span"),
                       resetButton(() => { values[p.key] = { x: 0, y: 0 }; sliders.forEach((s) => (s.value = 0)); update(); }));
        } else if (p.type === "color") {
            const input = el("input", { type: "color", value: `#${hex(values[p.key])}` });
            input.addEventListener("input", () => { values[p.key] = { ...parseHex(input.value), a: 255 }; update(); });
            box.append(input);
        } else if (p.type === "text") {
            const input = el("input", { type: "text", value: values[p.key], maxLength: 4096 });
            input.addEventListener("input", () => { values[p.key] = input.value; update(); });
            box.append(input, el("span"), resetButton(() => { input.value = values[p.key] = p.def; update(); }));
        } else if (p.type === "seed") {
            const b = el("button", { className: "pushbutton", textContent: tr("Reiniciar") });
            b.addEventListener("click", () => { values.seed = Math.floor(Math.random() * 1e9); update(); });
            box.append(el("span", { textContent: tr("Semilla aleatoria:") }), b);
        } else if (p.type === "curve") {
            const editor = curveEditor(values[p.key], (pts) => { values[p.key] = pts.map((q) => [...q]); update(); });
            box.append(el("div", { style: { gridColumn: "1 / -1" } }, [editor]));
            box.append(resetButton(() => { values[p.key] = p.def.map((q) => [...q]); editor.reset(values[p.key]); update(); }));
        }
        body.appendChild(box);
    }
    if (!effect.params.length)
        body.appendChild(el("p", { textContent: tr(effect.name) }));
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    update();
    const result = await dlg.exec();
    clearTimeout(timer);
    doc.setPreview(null);
    return result ? values : null;
}

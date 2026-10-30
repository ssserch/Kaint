// Selector de color con transparencia, el que abren los botones de color de los efectos de Kaint
// (QColorDialog con canal alfa): rueda de tono y saturación, brillo, RGB, MSV, opacidad y código
// hexadecimal, con el color de antes y el nuevo uno junto al otro.
import { hex, hsvToRgb, parseHex, rgbToHsv } from "../core/Raster.js";
import { Dialog, el } from "./Dialogs.js";
import { ChannelSlider, ColorWheel } from "./Panels.js";
import { tr, trc } from "../i18n.js";

const css = (c) => `rgba(${c.r},${c.g},${c.b},${(c.a ?? 255) / 255})`;

export async function colorDialog(initial, title = tr("Elegir color")) {
    const before = { r: initial.r, g: initial.g, b: initial.b, a: initial.a ?? 255 };
    let color = { ...before }, hsv = rgbToHsv(color);
    const dlg = new Dialog(title);
    const root = el("div", { className: "color-dialog" });
    dlg.body.appendChild(root);

    const setColor = (c, fromHsv = null) => {
        color = { r: c.r, g: c.g, b: c.b, a: c.a ?? 255 };
        if (fromHsv)
            hsv = fromHsv;
        else {
            const n = rgbToHsv(color);
            // Sin saturación o sin brillo, el tono y la saturación de antes se conservan.
            hsv = { h: n.s > 0 && n.v > 0 ? n.h : hsv.h, s: n.v > 0 ? n.s : hsv.s, v: n.v };
        }
        sync();
    };
    const setHsv = (h) => setColor({ ...hsvToRgb(h.h, h.s, h.v), a: color.a }, h);

    const wheel = new ColorWheel((h) => setHsv({ ...h }));
    wheel.canvas.title = tr("Ctrl: solo tono · Alt: solo saturación · Mayús: radios de 15°");
    const value = new ChannelSlider("v", true, (v) => setHsv({ ...hsv, v: v / 100 }));
    value.canvas.title = tr("Brillo");
    const swatch = el("div", { className: "compare", title: tr("Color anterior (izquierda) y nuevo (derecha)") },
                      [el("span", { className: "old" }), el("span", { className: "new" })]);
    swatch.querySelector(".old").style.setProperty("--c", css(before));
    swatch.querySelector(".old").addEventListener("click", () => setColor(before));
    root.appendChild(el("div", { className: "left" }, [el("div", { className: "wheel-row" }, [wheel.box, value.canvas]), swatch]));

    const grid = el("div", { className: "channels" });
    root.appendChild(grid);
    const rows = [];
    const section = (text) => grid.appendChild(el("div", { className: "section", textContent: text }));
    const row = (channel, label) => {
        const apply = (v) => {
            if (channel === "h") setHsv({ ...hsv, h: v });
            else if (channel === "s") setHsv({ ...hsv, s: v / 100 });
            else if (channel === "v") setHsv({ ...hsv, v: v / 100 });
            else setColor({ ...color, [channel]: v });
        };
        const slider = new ChannelSlider(channel, false, apply);
        const spin = el("input", { type: "number", min: 0, max: slider.maximum, step: 1 });
        spin.addEventListener("input", () => {
            const v = Number(spin.value);
            if (spin.value !== "" && Number.isFinite(v))
                apply(Math.min(slider.maximum, Math.max(0, Math.round(v))));
        });
        spin.addEventListener("change", () => (spin.value = slider.value()));
        grid.append(el("span", { textContent: label }), slider.canvas, spin);
        rows.push({ slider, spin });
    };
    section("RGB");
    row("r", tr("R:")); row("g", tr("V:")); row("b", tr("A:"));
    section(tr("MSV (HSV)"));
    row("h", tr("M:")); row("s", tr("S:")); row("v", trc("valor (MSV)", "V:"));
    section(tr("Opacidad (canal alfa)"));
    row("a", "");
    const hexInput = el("input", { type: "text", className: "hex", maxLength: 9, title: tr("RRGGBB o RRGGBBAA") });
    grid.append(el("span", { textContent: tr("Hex:") }), hexInput);
    let hexEdited = false;
    hexInput.addEventListener("input", () => {
        hexEdited = true;
        hexInput.value = hexInput.value.replace(/[^#0-9a-f]/gi, "");
        const t = hexInput.value.replace(/#/g, "");
        const c = t.length === 6 || t.length === 8 ? parseHex(t) : null;
        if (c)
            setColor(t.length === 6 ? { ...c, a: color.a } : c);
    });
    hexInput.addEventListener("change", () => {
        hexEdited = false;
        sync();
    });

    function sync() {
        wheel.set(hsv);
        value.set(color, hsv);
        for (const { slider, spin } of rows) {
            slider.set(color, hsv);
            if (document.activeElement !== spin)
                spin.value = slider.value();
        }
        if (!hexEdited)
            hexInput.value = hex(color, color.a < 255);
        swatch.querySelector(".new").style.setProperty("--c", css(color));
    }

    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    sync();
    // La rueda y los deslizadores se pintan cuando ya tienen tamaño.
    requestAnimationFrame(() => {
        wheel.resized();
        sync();
    });
    return (await dlg.exec()) ? color : null;
}

// Prueba de humo de Kaint para la web en Chromium sin pantalla (Playwright): usa las herramientas,
// los menús y los diálogos como lo haría el ratón y comprueba el resultado.
//   npm install playwright && npx playwright install chromium
//   node tests/smoke.mjs [carpeta para capturas]
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || "playwright");
const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const outDir = process.argv[2];
const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".png": "image/png", ".svg": "image/svg+xml" };

// Servidor de archivos (los módulos no se cargan desde file://).
const server = createServer(async (req, res) => {
    const path = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname)).replace(/^(\.\.[/\\])+/, "");
    try {
        const data = await readFile(join(root, path === "/" ? "index.html" : path));
        res.writeHead(200, { "Content-Type": types[extname(path)] || "application/octet-stream" });
        res.end(data);
    } catch {
        res.writeHead(404);
        res.end();
    }
}).listen(0, "127.0.0.1");
await new Promise((r) => server.once("listening", r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

let failures = 0;
const check = (ok, what) => {
    if (!ok) {
        failures++;
        console.error(`FALLO: ${what}`);
    }
};

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1400, height: 860 }, acceptDownloads: true });
const errors = [];
page.on("pageerror", (e) => errors.push(`${e.message}\n${e.stack}`));
page.on("console", (m) => m.type() === "error" && errors.push(m.text()));
await page.goto(url);
await page.evaluate(() => localStorage.setItem("kaint.settings", JSON.stringify({ language: "es", colorScheme: 1 })));
await page.reload();
await page.waitForFunction(() => window.kaint?.doc);

const at = (x, y) => page.evaluate(([x, y]) => {
    const k = window.kaint, r = k.view.canvas.getBoundingClientRect(), p = k.view.imageToView(x, y);
    return { x: r.left + p.x, y: r.top + p.y };
}, [x, y]);
const drag = async (pts, button = "left") => {
    let p = await at(...pts[0]);
    await page.mouse.move(p.x, p.y);
    await page.mouse.down({ button });
    for (const q of pts.slice(1)) {
        p = await at(...q);
        await page.mouse.move(p.x, p.y, { steps: 4 });
    }
    await page.mouse.up({ button });
};
const click = async (x, y, button = "left") => {
    const p = await at(x, y);
    await page.mouse.click(p.x, p.y, { button });
};
const pixel = (x, y, layer = null) => page.evaluate(([x, y, layer]) => {
    const d = window.kaint.doc, l = layer === null ? d.activeLayer : d.layers[layer];
    return [...l.ctx.getImageData(x, y, 1, 1).data];
}, [x, y, layer]);
const lastStep = () => page.evaluate(() => {
    const h = window.kaint.doc.history;
    return h.steps[h.index].name;
});
const tool = async (id) => {
    await page.evaluate((id) => window.kaint.tools.select(id), id);
    await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));
};
const setColors = (p, s) => page.evaluate(async ([p, s]) => {
    const { AppState } = await import("./js/AppState.js");
    AppState.setPrimary(p);
    AppState.setSecondary(s);
}, [p, s]);
const shot = (name) => outDir && page.screenshot({ path: join(outDir, name) });

// Lápiz y goma
await setColors({ r: 0, g: 0, b: 255, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await tool("pencil");
await drag([[10, 10], [40, 10]]);
check(JSON.stringify(await pixel(20, 10)) === "[0,0,255,255]", "el lápiz pinta píxeles del color principal");
await tool("eraser");
await drag([[20, 10], [21, 10]]);
check((await pixel(20, 10))[3] < 255, "la goma borra");

// El bloqueo de la goma: degradado con un clic fuera del lienzo, goma con Ctrl+clic fuera del
// lienzo y luego cualquier herramienta (antes el trazo vacío fallaba y no dejaba cambiar).
{
    const toolButton = (name) => page.locator(`.tools-grid .tbutton[title^="${name}"]`);
    await toolButton("Degradado").click();
    await click(-30, -30);
    await toolButton("Goma").click();
    const steps = await page.evaluate(() => window.kaint.doc.history.steps.length);
    await page.keyboard.down("Control");
    await click(-40, -40);
    await page.keyboard.up("Control");
    await toolButton("Lápiz").click();
    const after = await page.evaluate(() => ({ tool: window.kaint.tools.current.id, steps: window.kaint.doc.history.steps.length }));
    check(after.tool === "pencil" && after.steps === steps, `tras Ctrl+clic con la goma fuera del lienzo se cambia de herramienta (${JSON.stringify(after)})`);
}

// Selección, degradado, línea, texto y formas: el puntero con la herramienta en miniatura al lado
// (en Formas, la forma elegida).
{
    const cursors = await page.evaluate(async () => {
        const { AppState } = await import("./js/AppState.js");
        const k = window.kaint, out = {};
        for (const id of ["rectangleselect", "lassoselect", "ellipseselect", "gradient", "linecurve", "text", "shapes"]) {
            k.tools.select(id);
            out[id] = k.view.canvas.style.cursor;
        }
        const old = AppState.tools.shape;
        AppState.updateTools({ shape: "heart" });
        out.heart = k.view.canvas.style.cursor;
        AppState.updateTools({ shape: old });
        k.tools.select("pencil");
        return out;
    });
    const arrows = ["rectangleselect", "lassoselect", "ellipseselect", "gradient", "linecurve", "shapes"];
    check(arrows.every((id) => cursors[id].includes("image-set") && cursors[id].endsWith(" 0 0, default"))
          && cursors.text.endsWith(" 4 8, text") && cursors.heart !== cursors.shapes && cursors.gradient !== cursors.linecurve,
          "las herramientas con miniatura muestran el puntero con su icono al lado");
}

// Cubo de pintura (relleno global y contiguo) y varita mágica
await setColors({ r: 0, g: 160, b: 0, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await tool("paintbucket");
await click(400, 300);
check(JSON.stringify(await pixel(400, 300)) === "[0,160,0,255]", "el cubo rellena con el color principal");
await tool("magicwand");
await click(400, 300);
check(await page.evaluate(() => !window.kaint.doc.selection.isEmpty()), "la varita mágica selecciona la zona del color");
await page.keyboard.press("Control+d");

// Selección elíptica y de lazo
await tool("ellipseselect");
await drag([[100, 100], [160, 140], [200, 180]]);
const ellipse = await page.evaluate(() => {
    const s = window.kaint.doc.selection;
    return { inside: s.value(150, 140), corner: s.value(101, 101), bounds: s.bounds(), tool: window.kaint.tools.current.id };
});
check(ellipse.inside === 255 && ellipse.corner === 0, `la selección elíptica deja fuera las esquinas (${JSON.stringify(ellipse)})`);
await tool("lassoselect");
await drag([[300, 300], [400, 300], [400, 400], [300, 400]]);
check(await page.evaluate(() => window.kaint.doc.selection.value(350, 350) === 255), "el lazo selecciona su interior");
await page.keyboard.press("Control+d");

// Selector de color y volver a colorear
await tool("colorpicker");
await click(400, 300, "right");
check(await page.evaluate(async () => (await import("./js/AppState.js")).AppState.secondary.g === 160), "el selector toma el color secundario con el botón derecho");
await setColors({ r: 200, g: 0, b: 0, a: 255 }, { r: 0, g: 160, b: 0, a: 255 });
await tool("recolor");
await drag([[400, 300], [420, 300]]);
check((await pixel(410, 300))[0] > 150, "volver a colorear cambia el color secundario por el principal");

// Tampón de clonar
await page.evaluate(() => window.kaint.addLayer());
await tool("clonestamp");
await page.evaluate(() => window.kaint.doc.setActiveIndex(0));
const src = await at(410, 300);
await page.keyboard.down("Control");
await page.mouse.click(src.x, src.y);
await page.keyboard.up("Control");
await drag([[100, 400], [105, 400]]);
check((await pixel(102, 400, 0))[0] > 150, "el tampón copia desde el origen");

// Capas: propiedades (con vista previa), subir, combinar y la animación de las filas
await page.evaluate(() => window.kaint.doc.setActiveIndex(1));
const propsDone = page.evaluate(() => window.kaint.layerProperties());
await page.waitForSelector(".dialog");
await page.fill(".dialog input[type=text]", "Capa de prueba");
await page.selectOption(".dialog select", "multiply");
await page.click(".dialog .pushbutton.default");
await propsDone;
check(await page.evaluate(() => window.kaint.doc.activeLayer.name === "Capa de prueba" && window.kaint.doc.activeLayer.blend === "multiply"),
      "las propiedades de la capa cambian el nombre y el modo");
check(await lastStep() === "Propiedades de la capa", "las propiedades quedan en el historial");
await page.evaluate(() => window.kaint.moveLayerTo(1, 0));
check(await page.evaluate(() => document.querySelector(".layer-list .row").getAnimations().length > 0), "las filas de las capas se deslizan al moverlas");
await page.evaluate(() => window.kaint.doc.setActiveIndex(1));
await page.evaluate(() => window.kaint.mergeDown());
check(await page.evaluate(() => window.kaint.doc.layers.length === 1), "combinar con la inferior deja una capa");

// Imagen: girar, voltear, recortar y tamaño del lienzo
await page.evaluate(() => window.kaint.transformImage("cw"));
check(await page.evaluate(() => window.kaint.doc.width === 600 && window.kaint.doc.height === 800), "girar 90° intercambia el ancho y el alto");
await page.keyboard.press("Control+z");
check(await page.evaluate(() => window.kaint.doc.width === 800), "deshacer el giro");
await tool("rectangleselect");
await drag([[50, 50], [100, 90], [150, 150]]);
const cropSel = await page.evaluate(() => window.kaint.doc.selection.bounds());
check(cropSel && cropSel.w === 100 && cropSel.h === 100, `la selección rectangular mide 100 × 100 (${JSON.stringify(cropSel)})`);
await page.keyboard.press("Control+Shift+x");
check(await page.evaluate(() => window.kaint.doc.width === 100 && window.kaint.doc.height === 100), "recortar según la selección");
await page.keyboard.press("Control+z");
const sizeDone = page.evaluate(() => window.kaint.canvasSize());
await page.waitForSelector(".dialog");
await page.fill(".dialog input[type=number] >> nth=0", "900");
await page.click(".dialog .pushbutton.default");
await sizeDone;
check(await page.evaluate(() => window.kaint.doc.width === 900), "tamaño del lienzo");

// Efectos con diálogo (Curvas, Matiz y saturación) y de un clic (Sepia con su atajo)
for (const id of ["huesat", "curves", "dropshadow", "clouds"]) {
    const done = page.evaluate(async (id) => {
        const { findEffect } = await import("./js/core/Effects.js");
        return window.kaint.runEffect(findEffect(id), false);
    }, id);
    await page.waitForSelector(".dialog");
    await page.waitForTimeout(150);
    await page.click(".dialog .pushbutton.default");
    await done;
}
check(await lastStep() === "Nubes", "los efectos con diálogo se aplican");
await page.keyboard.press("Control+Shift+i");
check(await lastStep() === "Invertir colores", "el atajo de Invertir colores aplica el efecto");
await shot("efectos.png");

// Menús: Alt+letra abre Archivo; Ayuda con Documentación, Foro y Tutoriales
await page.locator("#view").focus();
await page.keyboard.press("Alt+a");
check(await page.evaluate(() => document.querySelectorAll(".popup-menu").length === 1), "Alt+A abre el menú Archivo");
await page.keyboard.press("Escape");
await page.click("#corner-buttons .tbutton:last-child");
const help = await page.evaluate(() => [...document.querySelectorAll(".popup-menu .item")].map((i) => i.textContent));
check(help.some((t) => t.startsWith("Documentación")) && help.some((t) => t.startsWith("Foro")) && help.some((t) => t.startsWith("Tutoriales")),
      "la Ayuda tiene Documentación, Foro y Tutoriales");
check(await page.evaluate(() => document.querySelector(".popup-menu img[src$='face-smile.svg']") !== null), "Donar lleva la carita");
await page.keyboard.press("Escape");

// Guardar en OpenRaster y volver a abrirlo con sus capas
await page.evaluate(() => window.kaint.addLayer());
await page.evaluate(() => { window.showSaveFilePicker = undefined; });
const download = page.waitForEvent("download");
await page.keyboard.press("Control+s");
await page.waitForSelector(".dialog");
await page.selectOption(".dialog select", "ora");
await page.click(".dialog .pushbutton.default");
const file = await download;
const path = await file.path();
const reopened = await page.evaluate(async (bytes) => {
    const { openFile } = await import("./js/core/ImageIO.js");
    const doc = await openFile(new File([new Uint8Array(bytes)], "prueba.ora"));
    return { layers: doc.layers.length, w: doc.width };
}, [...(await readFile(path))]);
check(reopened.layers === 2 && reopened.w === 900, "el .ora guardado se abre con sus capas");

// Imagen nueva, Configuración (ventanas translúcidas) y Acerca de
const newDone = page.evaluate(() => window.kaint.newImage());
await page.waitForSelector(".dialog");
await page.fill(".dialog input[type=number] >> nth=0", "320");
await page.fill(".dialog input[type=number] >> nth=1", "200");
await page.click(".dialog .pushbutton.default");
await newDone;
check(await page.evaluate(() => window.kaint.docs.length === 2 && window.kaint.doc.width === 320 && window.kaint.doc.height === 200),
      "imagen nueva de 320 × 200 en otra pestaña");
check(await page.evaluate(() => document.querySelectorAll(".image-tab").length === 2), "una pestaña por imagen");
page.evaluate(async () => (await import("./js/ui/Dialogs.js")).settingsDialog());
await page.waitForSelector(".dialog .settings");
const translucent = page.locator(".dialog .settings label", { hasText: "Ventanas translúcidas" }).locator("input");
await translucent.uncheck();
check(await page.evaluate(() => !document.getElementById("workspace").classList.contains("translucent")), "desactivar las ventanas translúcidas");
await translucent.check();
await page.click(".dialog .pushbutton.default");
page.evaluate(() => window.kaint.actions.get("about").trigger());
await page.waitForSelector(".dialog .about img.logo");
await page.keyboard.press("Escape");
check(await page.evaluate(() => !document.querySelector(".dialog")), "Esc cierra Acerca de");
await page.evaluate(() => window.kaint.closeDocument(window.kaint.doc, false));
const swapped = await page.evaluate(async () => {
    const { AppState } = await import("./js/AppState.js");
    const before = AppState.primary;
    document.getElementById("view").focus();
    document.getElementById("view").dispatchEvent(new KeyboardEvent("keydown", { key: "x", code: "KeyX", bubbles: true }));
    return AppState.secondary === before;
});
check(swapped, "X intercambia los colores");

// Animaciones desactivadas: sin deslizamiento
await page.evaluate(async () => (await import("./js/Settings.js")).Settings.set("animations", false));
await page.evaluate(() => window.kaint.moveLayerTo(1, 0));
check(await page.evaluate(() => document.documentElement.classList.contains("no-animations")
                          && document.querySelector(".layer-list .row").getAnimations().length === 0), "sin animaciones no se deslizan");

// Como Kaint de escritorio: iconos de herramientas dibujados (Formas ocupa su fila), barras con
// los PNG de los menús e icono de la herramienta en la barra de estado.
await page.evaluate(() => window.kaint.addDocument(new window.kaint.doc.constructor(400, 300)));
await tool("paintbrush");
const icons = await page.evaluate(() => {
    const buttons = [...document.querySelectorAll(".tools-grid .tbutton")];
    const last = buttons[buttons.length - 1].getBoundingClientRect(), first = buttons[0].getBoundingClientRect();
    return { drawn: buttons.every((b) => b.querySelector(".kicon").tagName === "CANVAS"), lastWide: last.width > first.width * 1.5,
             toolbar: [...document.querySelectorAll("#main-toolbar .kicon")].every((i) => i.tagName === "IMG"),
             status: document.querySelector(".status-bar .hint-icon .kicon")?.dataset.icon };
});
check(icons.drawn && icons.lastWide && icons.toolbar && icons.status === "paintbrush", `iconos de herramientas y barras (${JSON.stringify(icons)})`);

// Mover píxeles seleccionados: sin selección, un clic selecciona todo; cada arrastre es un paso
await setColors({ r: 0, g: 0, b: 0, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await tool("pencil");
await drag([[50, 50], [60, 50]]);
await tool("moveselectedpixels");
await click(200, 150);
check(await lastStep() === "Seleccionar todo", "un clic con Mover píxeles seleccionados sin selección selecciona todo");
await drag([[55, 50], [55, 80]]);
// Se mueve toda la capa (con su fondo blanco): arriba queda el hueco transparente.
check(await lastStep() === "Mover píxeles seleccionados" && JSON.stringify(await pixel(55, 80)) === "[0,0,0,255]" && (await pixel(55, 10))[3] === 0,
      "mover píxeles deja un paso al soltar");
await page.keyboard.press("Control+z");
check(JSON.stringify(await pixel(55, 50)) === "[0,0,0,255]" && (await pixel(55, 10))[3] === 255, "deshacer el movimiento");
await page.keyboard.press("Control+d");

// Formas, cubo, degradado y texto: un paso al crear, "Editar…" al cambiar y "Finalizar…" con
// Intro; al volver a un paso se sigue editando.
await tool("shapes");
await drag([[100, 100], [160, 160]]);
check(await lastStep() === "Rectángulo", "la forma entra en el historial al soltar");
await drag([[130, 130], [150, 150]]);
check(await lastStep() === "Editar forma", "mover la forma es «Editar forma»");
await page.keyboard.press("Control+z");
check(await lastStep() === "Rectángulo" && await page.evaluate(() => window.kaint.tools.current.editing), "deshacer vuelve a la forma y sigue editable");
await page.keyboard.press("Enter");
check(await lastStep() === "Finalizar forma", "Intro termina la forma");
await tool("paintbucket");
await click(300, 250);
check(await lastStep() === "Cubo de pintura", "el cubo entra en el historial al momento");
await setColors({ r: 200, g: 0, b: 0, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await page.keyboard.press("Enter");
check(JSON.stringify(await pixel(300, 250)) === "[200,0,0,255]" && await lastStep() === "Finalizar cubo de pintura",
      "el relleno sigue a los colores hasta terminar");
await tool("text");
await click(20, 200);
await page.keyboard.type("Kaint");
check(await lastStep() === "Texto", "lo escrito aparece en el historial al momento");
await page.keyboard.press("Escape");
check(await lastStep() === "Finalizar texto", "Esc termina el texto");

// Varita mágica: la selección sigue a la tolerancia hasta pulsar Intro
await tool("magicwand");
await click(380, 20);
const wand1 = await page.evaluate(() => window.kaint.doc.selection.area());
await page.evaluate(async () => (await import("./js/AppState.js")).AppState.updateTools({ tolerance: 100 }));
const wand2 = await page.evaluate(() => window.kaint.doc.selection.area());
await page.keyboard.press("Enter");
check(wand2 > wand1 && await lastStep() === "Varita mágica", `la varita sigue a la tolerancia (${wand1} → ${wand2})`);
await page.evaluate(async () => (await import("./js/AppState.js")).AppState.updateTools({ tolerance: 50 }));
await page.keyboard.press("Control+d");

// Zum a un rectángulo: justo ese rectángulo, sin margen
await tool("zoom");
await drag([[100, 100], [200, 150]]);
const fit = await page.evaluate(() => {
    const v = window.kaint.view, s = v.viewSize(), a = v.imageToView(100, 100), b = v.imageToView(200, 150);
    return Math.abs(b.x - a.x - s.w) < 2 || Math.abs(b.y - a.y - s.h) < 2;
});
check(fit, "el zum llena la vista con el rectángulo");
await page.evaluate(() => window.kaint.view.zoomToWindow());

// Reglas: oscuras como las de paint.js y las ventanas no las tapan
await page.evaluate(() => window.kaint.actions.get("rulers").trigger());
await page.evaluate(() => new Promise((r) => setTimeout(r, 100)));
const rulers = await page.evaluate(() => {
    const ruler = document.querySelector("canvas.ruler.h"), d = ruler.getContext("2d").getImageData(1, 1, 1, 1).data;
    return { dark: d[0] < 60, panels: [...document.querySelectorAll(".floating-panel")].every((p) => p.offsetLeft >= 20 && p.offsetTop >= 20) };
});
check(rulers.dark && rulers.panels, `reglas (${JSON.stringify(rulers)})`);
await page.evaluate(() => window.kaint.actions.get("rulers").trigger());

// Atajos de teclado editables
const keys = await page.evaluate(() => {
    const app = window.kaint, taken = app.setShortcut("save", "Ctrl+O");
    const r = taken?.id === "open" && app.actions.get("open").shortcuts.length === 0;
    app.resetShortcuts();
    return r && app.actions.get("open").shortcuts[0] === "Ctrl+O";
});
check(keys, "cambiar un atajo se lo quita a la acción que lo usaba");

// Paquete de iconos de paint.js: cambia el icono de Abrir y el de la herramienta Texto
const pack = await page.evaluate(async () => {
    const { zip } = await import("./js/core/Zip.js");
    const { IconPacks } = await import("./js/IconPacks.js");
    const png = async (color) => {
        const c = new OffscreenCanvas(16, 16), x = c.getContext("2d");
        x.fillStyle = color;
        x.fillRect(0, 0, 16, 16);
        return new Uint8Array(await (await c.convertToBlob({ type: "image/png" })).arrayBuffer());
    };
    const enc = new TextEncoder();
    const file = new File([zip([
        { name: "p/asset-pack.json", data: enc.encode(JSON.stringify({ formatVersion: 1, id: "smoke.pack", name: "Prueba" })) },
        { name: "p/icons/menu_file_open_icon.png", data: await png("#f00") },
        { name: "p/icons/text_tool_icon.png", data: await png("#00f") },
    ])], "p.zip");
    const { id } = await IconPacks.install(file);
    await IconPacks.activate(id);
    const text = document.querySelectorAll(".tools-grid .tbutton")[16].querySelector(".kicon");
    const ok = text.tagName === "IMG" && document.querySelector('#main-toolbar img[data-icon="document-open"]').src.startsWith("blob:");
    await IconPacks.remove(id);
    return ok && document.querySelectorAll(".tools-grid .tbutton")[16].querySelector(".kicon").tagName === "CANVAS";
});
check(pack, "un paquete de iconos cambia los iconos y al quitarlo vuelven los de Kaint");

// Complementos (.paintjs): uno de prueba con un efecto por píxel y un formato de archivo.
const plugin = await page.evaluate(async () => {
    const { zip } = await import("./js/core/Zip.js");
    const { Plugins } = await import("./js/plugins/Plugins.js");
    const { Effects } = await import("./js/core/Effects.js");
    const enc = new TextEncoder();
    const manifest = { id: "smoke.invert", name: "Prueba", version: "1.0.0", apiVersion: 1, author: "Kaint", website: "https://example.com/",
                       description: "Prueba", category: "Effects", license: "MIT" };
    const source = `paintDotJs.registerPlugin({ effects: [{ id: "invert-red", name: "Invert Red", category: "Color",
        controls: [paintDotJs.controls.range("amount", "Amount", 0, 1, 1, 0.01)],
        renderPixel({ r, g, b, a, values }) { return [r + (255 - 2 * r) * values.amount, g, b, a]; } }],
        fileTypes: [{ id: "raw", name: "Raw", extensions: [".rawsmoke"], decode() { return { width: 2, height: 1, data: new Uint8Array([255,0,0,255, 0,0,255,255]) }; },
                      encode({ document }) { return new Uint8Array(document.layers[0].data); } }] });`;
    const file = new File([zip([{ name: "plugin.json", data: enc.encode(JSON.stringify(manifest)) }, { name: "plugin.js", data: enc.encode(source) }])], "p.paintjs");
    await Plugins.install(file);
    const app = window.kaint, layer = app.doc.activeLayer;
    layer.ctx.fillStyle = "#ff0000";
    layer.ctx.fillRect(0, 0, 4, 4);
    await app.runEffect(Effects.find((e) => e.id === "plugin.smoke.invert.invert-red"), true);
    const px = [...layer.ctx.getImageData(1, 1, 1, 1).data];
    await app.openFile(new File([new Uint8Array(4)], "x.rawsmoke"));
    const opened = app.doc.width === 2 && [...app.doc.activeLayer.ctx.getImageData(0, 0, 1, 1).data][0] === 255;
    await app.closeDocument(app.doc, false);
    return { px, opened, any: Plugins.anyEffects(), step: app.doc.history.steps[app.doc.history.index].name };
});
check(plugin.px[0] === 0 && plugin.any && plugin.step === "Invert Red" && plugin.opened, `complementos (${JSON.stringify(plugin)})`);
await page.click("#menubar >> text=Efectos");
const effectsMenu = await page.evaluate(() => [...document.querySelectorAll(".popup-menu")].pop()?.textContent || "");
check(effectsMenu.includes("Complementos") && effectsMenu.includes("Color"), "el menú Efectos muestra «Complementos...» y la categoría del complemento");
await page.keyboard.press("Escape");

// Paleta como la de Kaint: dos filas (32 colores) y el menú con las paletas de muestra.
const palette = await page.evaluate(async () => {
    const before = document.querySelectorAll(".colors-panel .palette .swatch").length;
    document.querySelector(".colors-panel .palette-menu").click();
    const items = [...document.querySelectorAll(".popup-menu .item")].map((i) => i.textContent);
    [...document.querySelectorAll(".popup-menu .item")].find((i) => i.textContent.includes("Arcoíris"))?.click();
    const { AppState, rainbowPalette, samePalette } = await import("./js/AppState.js");
    const rainbow = samePalette(AppState.palette, rainbowPalette());
    AppState.resetPalette();
    return { before, items: items.length, rainbow };
});
check(palette.before === 32 && palette.items >= 6 && palette.rainbow, `paleta (${JSON.stringify(palette)})`);

// Tablero de transparencia como el de Kaint, temas Windows 98 y personalizado, cursores con icono
// y overscroll.
const misc = await page.evaluate(async () => {
    const { checkerPattern } = await import("./js/core/Raster.js");
    const { Settings } = await import("./js/Settings.js");
    const c = document.createElement("canvas");
    c.width = c.height = 16;
    const ctx = c.getContext("2d");
    ctx.fillStyle = checkerPattern(ctx, 200);
    ctx.fillRect(0, 0, 16, 16);
    const light = ctx.getImageData(12, 2, 1, 1).data[0], dark = ctx.getImageData(2, 2, 1, 1).data[0];
    Settings.set("colorScheme", 4);
    const classic = document.documentElement.classList.contains("classic");
    Settings.set("customTheme", { base: "dark", colors: { accent: "#ff0000" } });
    Settings.set("colorScheme", 5);
    const custom = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() === "#ff0000"
        && document.documentElement.classList.contains("dark");
    Settings.set("colorScheme", 1);
    window.kaint.tools.select("pencil");
    const cursor = document.getElementById("view").style.cursor;
    const view = window.kaint.view;
    view.zoomToWindow();
    view.scrollBy(-5000, 0);
    const over = view.origin.x > 24;
    view.zoomToWindow();
    return { light, dark, classic, custom, cursor: cursor.includes("image-set") && cursor.includes(" 2 18"), over };
});
check(misc.light === 200 && misc.dark === 150 && misc.classic && misc.custom && misc.cursor && misc.over, `apariencia y cámara (${JSON.stringify(misc)})`);

// Todas las páginas de Configuración se abren sin errores.
for (const id of ["interface", "canvas", "tools", "tablet", "plugins", "updates", "diagnostics", "iconpacks", "fonts", "keyboard"]) {
    // Sin esperar al diálogo (se resuelve al cerrarlo).
    await page.evaluate(async (id) => {
        (await import("./js/ui/Dialogs.js")).settingsDialog(window.kaint, id);
    }, id);
    await page.waitForSelector(".settings .page > *");
    await page.keyboard.press("Escape");
}

await shot("final.png");
for (const e of errors) {
    failures++;
    console.error(`ERROR: ${e}`);
}
await browser.close();
server.close();
console.log(failures ? `${failures} fallos` : "La prueba de humo de la web pasó.");
process.exitCode = failures ? 1 : 0;

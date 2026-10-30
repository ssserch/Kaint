// Lista los textos de la web que no tienen traducción al inglés (ni en las de Kaint ni en las
// propias de i18n.js).
//   node tools/check-i18n.mjs
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const files = [];
const walk = (dir) => {
    for (const f of readdirSync(dir)) {
        const p = join(dir, f);
        if (statSync(p).isDirectory()) walk(p);
        else if (p.endsWith(".js") && !p.includes("/i18n/")) files.push(p);
    }
};
walk(join(root, "js"));

const unquote = (s) => JSON.parse(`"${s}"`);
const texts = new Set();
const str = String.raw`"((?:[^"\\]|\\.)*)"`;
for (const file of files) {
    const src = readFileSync(file, "utf8");
    for (const m of src.matchAll(new RegExp(String.raw`\btrn?\(\s*` + str, "g"))) texts.add(unquote(m[1]));
    // Concatenaciones tr("a" + "b")
    for (const m of src.matchAll(new RegExp(String.raw`\btr\(\s*` + str + String.raw`\s*\+\s*` + str, "g"))) {
        texts.delete(unquote(m[1]));
        texts.add(unquote(m[1]) + unquote(m[2]));
    }
    if (file.endsWith("Effects.js")) {
        for (const m of src.matchAll(new RegExp(String.raw`name:\s*` + str, "g"))) texts.add(unquote(m[1]));
        for (const m of src.matchAll(new RegExp(String.raw`P\.\w+\(\s*"[^"]*",\s*` + str, "g"))) texts.add(unquote(m[1]));
        for (const m of src.matchAll(/P\.choice\([^\]]*\[([^\]]*)\]/g))
            for (const c of m[1].matchAll(new RegExp(str, "g"))) texts.add(unquote(c[1]));
        const cats = src.match(/Categories = \{([\s\S]*?)\};/);
        for (const c of cats[1].matchAll(new RegExp(":\\s*" + str, "g"))) texts.add(unquote(c[1]));
    }
    if (file.endsWith("Icons.js")) {
        const groups = src.match(/ShapeGroups = \[([\s\S]*?)\n\];/);
        for (const c of groups[1].matchAll(new RegExp(String.raw`\[\s*` + str + String.raw`,\s*\[`, "g"))) texts.add(unquote(c[1]));
        const shapes = src.match(/ShapeTypes = \[([\s\S]*?)\];/);
        for (const c of shapes[1].matchAll(new RegExp(String.raw`\[\s*"[^"]*",\s*` + str, "g"))) texts.add(unquote(c[1]));
    }
    if (file.endsWith("FillPattern.js"))
        for (const m of src.matchAll(new RegExp(String.raw`\[\s*` + str + String.raw`,\s*\[`, "g"))) texts.add(unquote(m[1]));
    if (file.endsWith("Raster.js")) {
        const names = src.match(/BlendNames = \{([\s\S]*?)\};/);
        for (const c of names[1].matchAll(new RegExp(str, "g"))) texts.add(unquote(c[1]));
    }
}
texts.delete("");
const en = (await import(join(root, "js/i18n/en.js"))).default;
const i18n = readFileSync(join(root, "js/i18n.js"), "utf8");
const extra = new Set([...i18n.matchAll(new RegExp(String.raw`^\s*` + str + ":", "gm"))].map((m) => unquote(m[1])));
const missing = [...texts].filter((t) => !(t in en) && !extra.has(t) && /[a-záéíóúñ]/i.test(t)).sort();
console.log(`${texts.size} textos · ${missing.length} sin traducir`);
for (const t of missing) console.log(JSON.stringify(t));
process.exitCode = missing.length ? 1 : 0;

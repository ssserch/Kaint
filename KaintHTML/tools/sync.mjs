// Copia de Kaint lo que comparte con la web: las traducciones al inglés (translations/kaint_en.ts →
// js/i18n/en.js; los textos de la web son los mismos que los del programa, en español, como en el
// código de Kaint) y la versión (version.json → js/version.js).
//   node tools/sync.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const ts = readFileSync(join(here, "../../translations/kaint_en.ts"), "utf8");
const decode = (s) => s.replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, "\"").replace(/&apos;/g, "'")
    .replace(/&#x([0-9a-f]+);/gi, (_, h) => String.fromCodePoint(parseInt(h, 16))).replace(/&amp;/g, "&");
const table = {};
for (const m of ts.matchAll(/<message>\s*<source>([\s\S]*?)<\/source>\s*<translation(?: type="[^"]*")?>([\s\S]*?)<\/translation>/g)) {
    const source = decode(m[1]);
    const translation = decode(m[2]);
    if (translation && !(source in table))
        table[source] = translation;
}
const keys = Object.keys(table).sort();
let out = "// Generado por tools/sync.mjs a partir de translations/kaint_en.ts: no se edita a mano.\n";
out += "export default {\n";
for (const k of keys)
    out += `    ${JSON.stringify(k)}: ${JSON.stringify(table[k])},\n`;
out += "};\n";
writeFileSync(join(here, "../js/i18n/en.js"), out);
const version = JSON.parse(readFileSync(join(here, "../../version.json"), "utf8"));
writeFileSync(join(here, "../js/version.js"),
              "// Generado por tools/sync.mjs a partir de version.json: no se edita a mano.\n" +
              `export const VERSION = ${JSON.stringify(version.version)};\n` +
              `export const REPOSITORY = ${JSON.stringify(version.repository)};\n`);
console.log(`${keys.length} textos · versión ${version.version}`);

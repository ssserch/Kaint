// Configuración > Personalización (src/app/Customization.cpp de Kaint): en qué menú sale cada efecto
// (también los de los complementos: por ejemplo, uno de un complemento en Efectos > Desenfoques),
// qué efectos se ocultan y qué herramientas hay en la ventana Herramientas y en qué orden.
import { Categories } from "./core/Effects.js";
import { Settings } from "./Settings.js";
import { tr } from "./i18n.js";

// Ubicaciones: "adjustments" (Ajustes), "effects" (el menú Efectos, sin submenú) o
// "effects/<submenú>": los de Kaint por su clave ("effects/blurs"), así no cambian con el idioma;
// los de los complementos y los nuevos, por su nombre.
export const Customization = {
    locations() {
        return Settings.get("effectLocations") || {};
    },

    defaultLocation(e) {
        if (!e.category)
            return e.menu === "effects" ? "effects" : "adjustments";
        // Un submenú de un complemento con el nombre de uno de Kaint ("Color") es ese mismo.
        return `effects/${e.category.startsWith("plugin:") ? this.submenuKey(e.categoryName || e.category.slice(7)) : e.category}`;
    },

    location(e) {
        return this.locations()[e.id] || this.defaultLocation(e);
    },

    // Nombre visible del submenú de una clave.
    submenuName(key) {
        return Categories[key] ? tr(Categories[key]) : key;
    },

    // Clave de un nombre visible (uno de Kaint escrito en el idioma de la interfaz, su clave).
    submenuKey(name) {
        const found = Object.entries(Categories).find(([, n]) => tr(n).toLowerCase() === name.toLowerCase() || n.toLowerCase() === name.toLowerCase());
        return found ? found[0] : name;
    },

    locationName(loc) {
        if (loc === "adjustments")
            return tr("Ajustes");
        if (loc.startsWith("effects/"))
            return `${tr("Efectos")} > ${this.submenuName(loc.slice(8))}`;
        return tr("Efectos");
    },

    // { adjustment, submenu } (clave del submenú de Efectos o null: directamente en Efectos).
    placement(e) {
        const loc = this.location(e);
        return { adjustment: loc === "adjustments", submenu: loc.startsWith("effects/") ? loc.slice(8) : null };
    },

    isMoved(id) {
        return Object.hasOwn(this.locations(), id);
    },

    isHidden(id) {
        return (Settings.get("hiddenEffects") || []).includes(id);
    },

    setLocation(effects, loc) {
        const map = { ...this.locations() };
        for (const e of effects) {
            if (!loc || loc === this.defaultLocation(e))
                delete map[e.id];
            else
                map[e.id] = loc;
        }
        Settings.set("effectLocations", map);
    },

    setHidden(ids, hidden) {
        const set = new Set(Settings.get("hiddenEffects") || []);
        for (const id of ids)
            hidden ? set.add(id) : set.delete(id);
        Settings.set("hiddenEffects", [...set].sort());
    },

    hasEffectChanges() {
        return Object.keys(this.locations()).length > 0 || (Settings.get("hiddenEffects") || []).length > 0;
    },

    resetEffects(ids = null) {
        if (!ids) {
            Settings.set("effectLocations", {});
            Settings.set("hiddenEffects", []);
            return;
        }
        const map = { ...this.locations() };
        ids.forEach((id) => delete map[id]);
        Settings.set("effectLocations", map);
        this.setHidden(ids, false);
    },

    // Herramientas en el orden elegido (las nuevas, al final en su orden de fábrica).
    toolOrder(defaults) {
        const saved = (Settings.get("toolOrder") || []).filter((id) => defaults.includes(id));
        return [...new Set([...saved, ...defaults])];
    },

    isToolHidden(id) {
        return (Settings.get("hiddenTools") || []).includes(id);
    },

    setToolOrder(order) {
        Settings.set("toolOrder", order);
    },

    setToolHidden(id, hidden) {
        const set = new Set(Settings.get("hiddenTools") || []);
        hidden ? set.add(id) : set.delete(id);
        Settings.set("hiddenTools", [...set]);
    },

    hasToolChanges() {
        return (Settings.get("toolOrder") || []).length > 0 || (Settings.get("hiddenTools") || []).length > 0;
    },

    resetTools() {
        Settings.set("toolOrder", []);
        Settings.set("hiddenTools", []);
    },
};

// Unidades de medida (Ruler.cpp de Kaint): píxeles, pulgadas y centímetros, para las reglas y la
// barra de estado. 'dpi' es la resolución de la imagen.
import { tr } from "../i18n.js";

export const unitSuffix = (unit) => [tr("px"), tr("in"), tr("cm")][unit] ?? tr("px");

export const pixelsPerUnit = (unit, dpi) => (unit === 1 ? dpi : unit === 2 ? dpi / 2.54 : 1);

// Una medida en píxeles con la unidad indicada (las pulgadas y los centímetros con dos decimales).
export const formatLength = (pixels, unit, dpi) => (unit ? (pixels / pixelsPerUnit(unit, dpi)).toFixed(2) : String(Math.round(pixels)));

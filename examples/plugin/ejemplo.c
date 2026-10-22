/*
 * Complemento de ejemplo para Kaint (API C de src/core/plugins/KaintPlugin.h).
 *
 * Añade dos efectos:
 *   · Ajustes > Intercambiar canales: reordena los canales R, G y B.
 *   · Efectos > Color > Tinte de color: mezcla la imagen con un color.
 *
 * Compilar e instalar a mano:
 *   cc -shared -fPIC -O2 -I../../src/core/plugins -o kaint-ejemplo.so ejemplo.c
 *   cp kaint-ejemplo.so ~/.local/share/Kaint/Effects/
 */
#include "KaintPlugin.h"

#include <stddef.h>

static const KaintParam swap_params[] = {
    {KAINT_PARAM_CHOICE, "order", "Orden", 0, 3, 0, 0, "BGR\nGBR\nBRG\nRBG"},
};

static void swap_render(const KaintRenderArgs* a)
{
    const int order = (int)a->values[0];
    for (int y = a->y; y < a->y + a->height; ++y) {
        if (*a->cancel)
            return;
        const uint8_t* s = a->src->pixels + (size_t)y * (size_t)a->src->stride + (size_t)a->x * 4;
        uint8_t* d = a->dst->pixels + (size_t)y * (size_t)a->dst->stride + (size_t)a->x * 4;
        for (int x = 0; x < a->width; ++x, s += 4, d += 4) {
            const uint8_t b = s[0], g = s[1], r = s[2];
            switch (order) {
            case 0: d[2] = b; d[1] = g; d[0] = r; break; /* BGR */
            case 1: d[2] = g; d[1] = b; d[0] = r; break; /* GBR */
            case 2: d[2] = b; d[1] = r; d[0] = g; break; /* BRG */
            default: d[2] = r; d[1] = b; d[0] = g; break; /* RBG */
            }
            d[3] = s[3];
        }
    }
}

static const KaintParam tint_params[] = {
    {KAINT_PARAM_COLOR, "color", "Color", 0, 0, (double)0xffff8000u, 0, NULL},
    {KAINT_PARAM_INT, "amount", "Intensidad", 0, 100, 50, 0, NULL},
};

static void tint_render(const KaintRenderArgs* a)
{
    const uint32_t c = (uint32_t)a->values[0];
    const int amount = (int)a->values[1];
    const int cr = (int)((c >> 16) & 0xff), cg = (int)((c >> 8) & 0xff), cb = (int)(c & 0xff);
    for (int y = a->y; y < a->y + a->height; ++y) {
        if (*a->cancel)
            return;
        const uint8_t* s = a->src->pixels + (size_t)y * (size_t)a->src->stride + (size_t)a->x * 4;
        uint8_t* d = a->dst->pixels + (size_t)y * (size_t)a->dst->stride + (size_t)a->x * 4;
        for (int x = 0; x < a->width; ++x, s += 4, d += 4) {
            d[0] = (uint8_t)((s[0] * (100 - amount) + cb * amount + 50) / 100);
            d[1] = (uint8_t)((s[1] * (100 - amount) + cg * amount + 50) / 100);
            d[2] = (uint8_t)((s[2] * (100 - amount) + cr * amount + 50) / 100);
            d[3] = s[3]; /* el alfa no cambia */
        }
    }
}

static const KaintEffectInfo effects[] = {
    {"kaint.ejemplo.intercambiar", "Intercambiar canales", NULL, swap_params, 1, swap_render},
    {"kaint.ejemplo.tinte", "Tinte de color", "Color", tint_params, 2, tint_render},
};

static const KaintPluginInfo info = {
    KAINT_PLUGIN_API_VERSION,
    "Ejemplo de complemento",
    "1.0",
    "Kaint",
    "Muestra cómo escribir un complemento nativo con la API C de Kaint.",
    "https://github.com/ssserch/Kaint",
    effects,
    2,
};

KAINT_PLUGIN_EXPORT const KaintPluginInfo* kaint_plugin_info(void)
{
    return &info;
}

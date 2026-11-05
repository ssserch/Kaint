/*
 * Complemento de ejemplo para Kaint (API C de src/core/plugins/KaintPlugin.h).
 *
 * Añade dos efectos:
 *   · Ajustes > Intercambiar canales: reordena los canales R, G y B.
 *   · Efectos > Color > Tinte de color: mezcla la imagen con un color.
 *
 * El tinte lleva su propio icono (API 2): un cuadrado de 16x16 dibujado en
 * píxeles. También se puede dar una imagen codificada (icon_data e
 * icon_data_size, p. ej. un PNG incrustado con xxd -i) o un archivo junto al
 * complemento (icon_file = "tinte.png").
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

/* Icono del tinte: un degradado de naranja a transparente con borde oscuro
   (BGRA, alfa directo), dibujado la primera vez que Kaint lo pide. */
static uint8_t tint_icon_pixels[16 * 16 * 4];
static const KaintImage tint_icon = {16, 16, 16 * 4, tint_icon_pixels};

static void draw_tint_icon(void)
{
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t* p = tint_icon_pixels + (y * 16 + x) * 4;
            const int border = x == 1 || x == 14 || y == 1 || y == 14;
            const int inside = x > 1 && x < 14 && y > 1 && y < 14;
            if (border && x >= 1 && x <= 14 && y >= 1 && y <= 14) {
                p[0] = 0x30; p[1] = 0x30; p[2] = 0x30; p[3] = 0xff;
            } else if (inside) {
                p[0] = 0x00; p[1] = (uint8_t)(0x80 + y * 4); p[2] = 0xff; p[3] = (uint8_t)(255 - x * 12);
            } else {
                p[0] = p[1] = p[2] = p[3] = 0;
            }
        }
}

static const KaintEffectInfo effects[] = {
    {"kaint.ejemplo.intercambiar", "Intercambiar canales", NULL, swap_params, 1, swap_render, NULL, NULL, 0, NULL},
    {"kaint.ejemplo.tinte", "Tinte de color", "Color", tint_params, 2, tint_render, &tint_icon, NULL, 0, NULL},
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
    draw_tint_icon();
    return &info;
}

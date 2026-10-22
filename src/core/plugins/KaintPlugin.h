/*
 * API de complementos nativos de Kaint (C, ABI estable).
 *
 * Un complemento es una biblioteca compartida (.so) que exporta la función
 * kaint_plugin_info(). Kaint la busca en ~/.local/share/Kaint/Effects y en
 * las carpetas de complementos incluidos. Ver README.md («Complementos»).
 *
 * Los píxeles son BGRA de 8 bits por canal con alfa directo (no
 * premultiplicado), el mismo formato que las capas de Kaint y que el
 * ColorBgra de Paint.NET. Los colores van como 0xAARRGGBB.
 *
 * Compilar:  cc -shared -fPIC -O2 -o mi-efecto.so mi-efecto.c
 */
#ifndef KAINT_PLUGIN_H
#define KAINT_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KAINT_PLUGIN_API_VERSION 1

#if defined(__GNUC__)
#define KAINT_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define KAINT_PLUGIN_EXPORT
#endif

typedef enum KaintParamType {
    KAINT_PARAM_INT = 0,    /* deslizador entero (min..max) */
    KAINT_PARAM_DOUBLE = 1, /* deslizador real con 'decimals' decimales */
    KAINT_PARAM_BOOL = 2,   /* casilla (0 o 1) */
    KAINT_PARAM_CHOICE = 3, /* lista: 'choices' separadas por '\n'; valor = índice */
    KAINT_PARAM_ANGLE = 4,  /* selector de ángulo en grados (min..max) */
    KAINT_PARAM_COLOR = 5,  /* color 0xAARRGGBB; 'def' = color inicial */
    KAINT_PARAM_SEED = 6    /* botón "Nueva semilla" (0..255) */
} KaintParamType;

typedef struct KaintParam {
    int type;           /* KaintParamType */
    const char* key;    /* identificador (ASCII, único en el efecto) */
    const char* label;  /* texto visible (UTF-8) */
    double min;
    double max;
    double def;
    int decimals;
    const char* choices; /* solo KAINT_PARAM_CHOICE */
} KaintParam;

typedef struct KaintImage {
    int width;
    int height;
    int stride;      /* bytes por fila */
    uint8_t* pixels; /* BGRA, alfa directo */
} KaintImage;

typedef struct KaintRenderArgs {
    const KaintImage* src; /* capa completa (solo lectura) */
    KaintImage* dst;       /* copia de src: escribir solo dentro del rectángulo */
    int x, y, width, height; /* rectángulo a calcular (límites de la selección) */
    const double* values;  /* un valor por parámetro, en el orden de 'params' */
    uint32_t primary;      /* color principal 0xAARRGGBB */
    uint32_t secondary;    /* color secundario */
    const volatile int* cancel; /* distinto de 0: dejar de calcular cuanto antes */
} KaintRenderArgs;

typedef struct KaintEffectInfo {
    const char* id;       /* identificador único (p. ej. "org.ejemplo.invertir") */
    const char* name;     /* nombre en el menú (UTF-8) */
    const char* category; /* submenú de Efectos; NULL = menú Ajustes */
    const KaintParam* params;
    int param_count;
    /* Kaint la llama desde varios hilos a la vez con rectángulos
       distintos: debe ser reentrante. */
    void (*render)(const KaintRenderArgs* args);
} KaintEffectInfo;

typedef struct KaintPluginInfo {
    int api_version; /* KAINT_PLUGIN_API_VERSION */
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    const char* website;
    const KaintEffectInfo* effects;
    int effect_count;
} KaintPluginInfo;

/* Punto de entrada que Kaint resuelve con dlsym. */
typedef const KaintPluginInfo* (*KaintPluginInfoFunction)(void);
#define KAINT_PLUGIN_ENTRY "kaint_plugin_info"

#ifdef __cplusplus
}
#endif

#endif /* KAINT_PLUGIN_H */

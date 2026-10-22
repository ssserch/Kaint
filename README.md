# Kaint

Editor de imágenes con capas para Linux, inspirado en **Paint.NET**. Está escrito
desde cero en C++20 y Qt 6, pensado para KDE Plasma en Wayland, e intenta que quien
viene de Paint.NET se sienta en casa: las mismas herramientas, los mismos atajos, los
mismos efectos y una interfaz que se le parece mucho.

> Kaint es un proyecto independiente. No está afiliado ni respaldado por dotPDN LLC.
> Paint.NET es una marca registrada de dotPDN LLC.

## Qué trae

- **Capas** con modos de fusión, opacidad, propiedades e historial con límite de memoria.
- **Herramientas**: selección rectangular, elíptica, lazo y varita mágica (con los modos
  unión, excluir, intersección y O exclusivo), mover píxeles y mover selección (con
  escala, giro y punto de giro movible), pincel, lápiz, goma, tampón de clonar, volver a
  colorear, bote de pintura, degradados, líneas y curvas, formas, texto, selector de
  color, zum y mano. Barra de opciones con el mismo orden que Paint.NET.
- **Ajustes y efectos** de Paint.NET (desenfoques, distorsiones, artísticos,
  fotografía, representación, ruido, estilizado...) con vista previa en el lienzo.
- **Formatos**: OpenRaster (.ora, con capas), PNG, JPEG, WebP, AVIF, JPEG XL, BMP, TIFF,
  TGA, GIF animado (cada fotograma es una capa) e ICO con varios tamaños. Abre archivos
  **.pdn** de Paint.NET 3, 4 y 5 con sus capas.
- **Complementos**: los suyos propios (.so) y muchos complementos de Paint.NET (.dll).
- **Formas personalizadas** de Paint.NET (.xaml).
- En inglés (predeterminado) y en español: *Settings > User Interface > Language*.
- Tema del sistema, claro u oscuro, con los iconos de Breeze. El claro y el oscuro no
  dependen del tema de Plasma (usan el estilo Fusion con la paleta de Breeze); pasar del
  tema del sistema a uno propio, o al revés, se completa al reiniciar Kaint. También se
  puede elegir al abrirlo: `kaint --color-scheme system|light|dark` (se guarda).
- Rich Presence de Discord opcional.

## Instalar

En Arch Linux:

```bash
sudo pacman -S --needed gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons
# Opcionales: más formatos, diálogos de Qt traducidos y complementos de Paint.NET
sudo pacman -S --needed qt6-imageformats kimageformats qt6-translations mono libgdiplus

./build.sh --install     # compila e instala en ~/.local (aparece en el menú)
```

Otras opciones de `build.sh`:

```bash
./build.sh               # compila en build/
./build.sh --run         # compila y abre Kaint
./build.sh --test        # compila y pasa las pruebas
./build.sh --help        # todas las opciones
```

Si Qt está instalado en otra ruta, exporta `CMAKE_PREFIX_PATH` antes de compilar.

## Complementos

*Efectos > Complementos...* muestra los complementos instalados y permite activarlos o
desactivarlos al momento. **Añadir…** copia los archivos que elijas (.dll o .so) a
`~/.local/share/Kaint/Effects/`, y **Abrir la carpeta** / **Abrir su carpeta** abren esa
carpeta o la del complemento seleccionado. Si un complemento trae varios archivos,
añádelos todos o copia su carpeta entera ahí.

**Complementos de Paint.NET (.dll).** Necesitan `mono` y `libgdiplus`. Kaint los ejecuta
en un proceso aparte con una capa de compatibilidad basada en el código abierto de
Paint.NET 3.36; el diálogo de cada efecto lo dibuja Kaint con sus propios controles.
Funcionan los escritos para Paint.NET 3.x y 4.x, también los de 4.3/4.4 compilados para
.NET moderno que usan la API clásica. No pueden funcionar los que usan la API de efectos
de GPU de Paint.NET 5 (Direct2D, solo existe en Windows); esos no aparecen en la lista
salvo que lo pidas.

**Complementos nativos (.so).** API en C con ABI estable:
[`src/core/plugins/KaintPlugin.h`](src/core/plugins/KaintPlugin.h). Hay un ejemplo en
[`examples/plugin/`](examples/plugin/):

```bash
cc -shared -fPIC -O2 -Isrc/core/plugins -o kaint-ejemplo.so examples/plugin/ejemplo.c
cp kaint-ejemplo.so ~/.local/share/Kaint/Effects/
```

## Formas personalizadas

La herramienta Formas carga las formas personalizadas de Paint.NET (archivos `.xaml`,
el formato descrito en la [documentación de la API de Paint.NET](https://paintdotnet.github.io/apidocs/)).
Cópialas en `~/.local/share/Kaint/Shapes/`; aparecen al final del desplegable de formas
la próxima vez que abras Kaint. Hay dos ejemplos en [`examples/shapes/`](examples/shapes/).

## GIF animados

Cada fotograma de un GIF es una capa (la de abajo es el primero) y su duración va en el
nombre de la capa, por ejemplo `Fotograma 1 (100ms)`. *Imagen > Animación...* edita las
duraciones con una vista previa, y al guardar se elige si el GIF va animado, cuántas veces
se repite y cómo se tratan la transparencia y el tramado.

## Discord

Kaint puede mostrar en tu perfil de Discord qué estás editando. Funciona con la
aplicación de Discord abierta en el mismo equipo y se configura en
*Configuración > Discord*. Para compilar tu propia versión, pon el *Application ID* de
tu aplicación de Discord en `client_id` de `discord.json`.

## Para desarrolladores

| Carpeta | Contenido |
|---|---|
| `src/core/` | Núcleo sin interfaz: documento, capas, selección, historial, efectos, formatos, complementos. |
| `src/app/` | Interfaz con Qt Widgets: ventana, lienzo, paneles, diálogos y herramientas. |
| `src/pdnbridge/` | Capa de compatibilidad con los complementos de Paint.NET (C#, mono). |
| `examples/` | Complemento nativo y formas personalizadas de ejemplo. |
| `tests/` | Pruebas del núcleo y prueba de la interfaz sin pantalla. |
| `translations/` | Traducciones (inglés). |

Los textos del código están en español y `translations/kaint_en.ts` tiene la traducción
al inglés (se compila dentro del programa). Tras cambiar textos, actualiza el `.ts` con
`cmake --build build -t update_translations` y tradúcelo con Qt Linguist.

`version.json` guarda la versión y el repositorio que consulta *Configuración >
Actualizaciones*. La opción de CMake `KAINT_PDN_BRIDGE` (activada por defecto) compila la
capa de compatibilidad si encuentra `mcs`.

## Licencias

`src/pdnbridge/` contiene código de Paint.NET 3.36, publicado con licencia MIT
(ver `src/pdnbridge/License-PaintDotNet-3.36.txt`).

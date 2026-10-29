#!/usr/bin/env bash
# Capturas de muestra de Kaint con la configuración de fábrica (para GitHub o para compartir).
#
# Cópialo dentro de la carpeta de compilación (junto al ejecutable kaint) y ejecútalo allí:
#   ./screenpreview.sh
# Pregunta las imágenes (la primera es la que se ve; las demás quedan abiertas en la barra de
# arriba), el tema, el estilo de la interfaz (Kaint o Paint.NET), el idioma, si los iconos de
# herramientas llevan los colores activos, las reglas, las ventanas translúcidas, la resolución y
# el formato, y guarda la captura en screenpreview-screenshots/.
# No toca la configuración de Kaint: cada captura usa una configuración de fábrica temporal.
set -euo pipefail

DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
OUT_DIR="$DIR/screenpreview-screenshots"

c_bold=$'\e[1m'; c_red=$'\e[31m'; c_yellow=$'\e[33m'; c_green=$'\e[32m'; c_reset=$'\e[0m'
if [[ ! -t 1 ]]; then c_bold=""; c_red=""; c_yellow=""; c_green=""; c_reset=""; fi
fail() { echo "${c_red}$*${c_reset}" >&2; exit 1; }
warn() { echo "${c_yellow}$*${c_reset}" >&2; }

# --- Ejecutable -------------------------------------------------------------
KAINT=""
for candidate in "$DIR/kaint" "$DIR/bin/kaint"; do
    if [[ -x "$candidate" ]]; then KAINT="$candidate"; break; fi
done
if [[ -z "$KAINT" ]] && command -v kaint >/dev/null 2>&1; then
    KAINT="$(command -v kaint)"
    warn "No hay un kaint junto al script: se usa el instalado ($KAINT)."
fi
[[ -n "$KAINT" ]] || fail "No encuentro kaint: copia este script dentro de la carpeta de compilación (junto al ejecutable kaint)."
QT_QPA_PLATFORM=offscreen "$KAINT" --help 2>/dev/null | grep -q -- "--screenpreview" \
    || fail "Este kaint ($KAINT) no tiene el modo de capturas: vuelve a compilarlo."
QT_QPA_PLATFORM=offscreen "$KAINT" --help 2>/dev/null | grep -q -- "--preview-colored-icons" \
    || fail "Este kaint ($KAINT) es anterior a la opción de los iconos de herramientas: vuelve a compilarlo."
QT_QPA_PLATFORM=offscreen "$KAINT" --help 2>/dev/null | grep -q -- "--preview-style" \
    || fail "Este kaint ($KAINT) es anterior al estilo de Paint.NET: vuelve a compilarlo."

# Pregunta de sí o no; la respuesta vacía es la de fábrica ($2: yes o no).
ask_yes_no() {
    local prompt="$1" default="$2" choice
    while true; do
        read -r -p "$prompt" choice
        case "${choice,,}" in
            "") echo "$default"; return ;;
            s|si|sí|y|yes) echo yes; return ;;
            n|no) echo no; return ;;
            *) warn "Escribe s o n." ;;
        esac
    done
}

# --- Imágenes ---------------------------------------------------------------
# Admite rutas arrastradas a la terminal: entre comillas, con espacios escapados o file://.
clean_path() {
    local p="$1"
    p="${p#"${p%%[![:space:]]*}"}"; p="${p%"${p##*[![:space:]]}"}"
    if [[ ${#p} -ge 2 && ( ( "${p:0:1}" == "'" && "${p: -1}" == "'" ) || ( "${p:0:1}" == '"' && "${p: -1}" == '"' ) ) ]]; then
        p="${p:1:${#p}-2}"
    fi
    if [[ "$p" == file://* ]]; then
        p="${p#file://}"
        p="$(printf '%b' "${p//%/\\x}")"
    fi
    p="${p//\\ / }"
    [[ "$p" == "~/"* ]] && p="$HOME/${p#\~/}"
    printf '%s' "$p"
}

ask_image() {
    local answer path
    while true; do
        read -e -r -p "$1" answer
        path="$(clean_path "$answer")"
        if [[ -f "$path" ]]; then
            readlink -f "$path"
            return
        fi
        warn "No existe el archivo: ${path:-(vacío)}"
    done
}

echo "${c_bold}Capturas de muestra de Kaint${c_reset} (configuración de fábrica)"
echo
IMAGES=()
IMAGES+=("$(ask_image "Imagen que mostrar (.ora o cualquier formato compatible): ")")
while true; do
    read -r -p "¿Añadir otra imagen abierta en la barra de arriba? [s/N] " more
    [[ "$more" =~ ^[sSyY] ]] || break
    IMAGES+=("$(ask_image "Otra imagen: ")")
done

# --- Tema -------------------------------------------------------------------
echo
echo "Tema:"
echo "  1) Oscuro"
echo "  2) Claro"
echo "  3) KDE"
echo "  4) Windows 98"
THEME=""
while [[ -z "$THEME" ]]; do
    read -r -p "Elige [1-4]: " choice
    case "${choice,,}" in
        1|oscuro|dark)   THEME=dark ;;
        2|claro|light)   THEME=light ;;
        3|kde)           THEME=kde ;;
        4|windows98|98)  THEME=windows98 ;;
        *) warn "Escribe 1, 2, 3 o 4." ;;
    esac
done
if [[ "$THEME" == kde && "${XDG_CURRENT_DESKTOP:-}" != *KDE* ]]; then
    warn "El tema KDE necesita Plasma (plasma-integration); fuera de KDE la captura saldrá con el estilo de Qt."
fi

# --- Estilo de la interfaz --------------------------------------------------
# El de Kaint o la recreación de Paint.NET 5 (más compacta, ventanas sin borde...).
echo
echo "Estilo de la interfaz:"
echo "  1) Kaint"
echo "  2) Paint.NET"
STYLE=""
while [[ -z "$STYLE" ]]; do
    read -r -p "Elige [1-2] (1): " choice
    case "${choice,,}" in
        ""|1|kaint)              STYLE=kaint ;;
        2|paint.net|paintnet|pdn) STYLE=paintnet ;;
        *) warn "Escribe 1 o 2." ;;
    esac
done
[[ "$STYLE" == paintnet && "$THEME" == kde ]] && warn "El estilo de Paint.NET no se aplica con el tema KDE."

# --- Idioma -----------------------------------------------------------------
echo
echo "Idioma:"
echo "  1) English"
echo "  2) Español"
LANGUAGE=""
while [[ -z "$LANGUAGE" ]]; do
    read -r -p "Elige [1-2] (1): " choice
    case "${choice,,}" in
        ""|1|en|english)          LANGUAGE=en ;;
        2|es|español|espanol)     LANGUAGE=es ;;
        *) warn "Escribe 1 o 2." ;;
    esac
done

# --- Iconos de herramientas, reglas y ventanas -----------------------------
# El cubo, el pincel, cambiar color y el degradado pueden mostrar los colores activos.
echo
COLORED="$(ask_yes_no "¿Iconos de herramientas con los colores activos? [S/n] " yes)"
RULERS="$(ask_yes_no "¿Mostrar las reglas? [s/N] " no)"
TRANSLUCENT="$(ask_yes_no "¿Ventanas translúcidas (como de fábrica)? [S/n] " yes)"

# --- Resolución -------------------------------------------------------------
RESOLUTIONS=(1280x720 1366x768 1600x900 1920x1080 2560x1440 3840x2160)
echo
echo "Resolución:"
for i in "${!RESOLUTIONS[@]}"; do
    echo "  $((i + 1))) ${RESOLUTIONS[i]/x/ × }"
done
echo "  0) Personalizada"
SIZE=""
while [[ -z "$SIZE" ]]; do
    read -r -p "Elige [0-${#RESOLUTIONS[@]}]: " choice
    if [[ "$choice" == 0 ]]; then
        while [[ -z "$SIZE" ]]; do
            read -r -p "Ancho x alto (p. ej. 1440x900): " custom
            custom="${custom//[[:space:]]/}"; custom="${custom//×/x}"; custom="${custom,,}"
            if [[ "$custom" =~ ^([0-9]+)x([0-9]+)$ ]] && (( BASH_REMATCH[1] >= 800 && BASH_REMATCH[2] >= 600 )); then
                SIZE="$custom"
            else
                warn "Escribe ancho x alto, como mínimo 800x600."
            fi
        done
    elif [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#RESOLUTIONS[@]} )); then
        SIZE="${RESOLUTIONS[choice - 1]}"
    else
        warn "Escribe un número de la lista."
    fi
done
WIDTH="${SIZE%x*}"
# A partir de 4K la interfaz va al 200 %, como en una pantalla de alta densidad.
SCALE=1
(( WIDTH >= 3200 )) && SCALE=2

# --- Formato ----------------------------------------------------------------
echo
FORMAT=""
while [[ -z "$FORMAT" ]]; do
    read -r -p "Formato [png/jpg] (png): " choice
    case "${choice,,}" in
        ""|png|1) FORMAT=png ;;
        jpg|jpeg|2) FORMAT=jpg ;;
        *) warn "Escribe png o jpg." ;;
    esac
done

# --- Captura ----------------------------------------------------------------
mkdir -p "$OUT_DIR"
OUTPUT="$OUT_DIR/kaint-$STYLE-$THEME-$LANGUAGE-$SIZE-$(date +%Y%m%d-%H%M%S).$FORMAT"
echo
echo "Creando la captura…"
if "$KAINT" --screenpreview "$OUTPUT" --color-scheme "$THEME" --preview-size "$SIZE" --preview-scale "$SCALE" \
        --preview-colored-icons "$COLORED" --preview-style "$STYLE" --preview-language "$LANGUAGE" \
        --preview-rulers "$RULERS" --preview-translucent "$TRANSLUCENT" \
        -- "${IMAGES[@]}" 2> >(grep -v "propagateSizeHints" >&2); then
    echo "${c_green}Lista:${c_reset} $OUTPUT"
else
    fail "No se pudo crear la captura."
fi

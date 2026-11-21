#!/usr/bin/env bash
# Capturas de muestra de Kaint con la configuración de fábrica (para GitHub o para compartir).
#
# Se ejecuta desde el código de Kaint (usa build/kaint) o desde la carpeta de compilación (junto al
# ejecutable kaint):
#   ./screenpreview.sh
# Pregunta las imágenes (la primera es la que se ve; las demás quedan abiertas en la barra de
# arriba), el tema (o todos), el estilo de la interfaz (Kaint o Paint.NET), la barra de título y sus
# botones (los del tema: los de XP con el tema Windows XP, los de cristal con Windows 7...), el
# idioma, si los iconos de herramientas llevan los colores activos, las reglas, las ventanas
# translúcidas, la resolución y el formato, y guarda las capturas en screenpreview-screenshots/.
# No toca la configuración de Kaint: cada captura usa una configuración de fábrica temporal.
#
# Sin preguntas (para el README): una captura de cada tema con la misma imagen y, con --win7-colors,
# también los temas Windows 7 y Windows 7 oscuro con los colores de ventana de Windows 7 (Cielo,
# Crepúsculo, Mar...).
#   ./screenpreview.sh --all --image img/other/kaintqt6.ora [--out img/themes] [--size 1920x1080]
#                      [--style kaint|paintnet] [--lang en|es] [--format png|jpg] [--win7-colors]
set -euo pipefail

DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
OUT_DIR="$DIR/screenpreview-screenshots"

c_bold=$'\e[1m'; c_red=$'\e[31m'; c_yellow=$'\e[33m'; c_green=$'\e[32m'; c_reset=$'\e[0m'
if [[ ! -t 1 ]]; then c_bold=""; c_red=""; c_yellow=""; c_green=""; c_reset=""; fi
fail() { echo "${c_red}$*${c_reset}" >&2; exit 1; }
warn() { echo "${c_yellow}$*${c_reset}" >&2; }

# Los temas: nombre para --color-scheme, nombre para mostrar y nombre del archivo.
THEMES=(
    "dark|Oscuro|dark"
    "light|Claro|light"
    "blue|Azul (Paint.NET)|blue"
    "windows7|Windows 7|windows7"
    "windows7dark|Windows 7 oscuro|windows7-dark"
    "windowsxp|Windows XP|windowsxp"
    "windows98|Windows 98|windows98"
    "windows98alt|Windows 98 ALT|windows98-alt"
    "windows7basic|Windows 7 Basic|windows7-basic"
    "windows7basicdark|Windows 7 Basic oscuro|windows7-basic-dark"
    "windows8|Windows 8|windows8"
    "windows8dark|Windows 8 oscuro|windows8-dark"
    "ubuntu|Ubuntu|ubuntu"
    "ubuntudark|Ubuntu oscuro|ubuntu-dark"
    "gnome|GNOME|gnome"
    "gnomedark|GNOME oscuro|gnome-dark"
    "mint|Linux Mint|mint"
    "mintdark|Linux Mint oscuro|mint-dark"
    "elementary|elementary OS|elementary"
    "elementarydark|elementary OS oscuro|elementary-dark"
    "kde|KDE|kde"
)
# Los colores de ventana de Windows 7 (Personalizar > Color de ventana), aproximados.
WIN7_COLORS=(
    "sky|Cielo|#74b8fc" "twilight|Crepúsculo|#3b5aa6" "sea|Mar|#2fa4b8" "leaf|Hoja|#5bb73a"
    "sun|Sol|#f6cc26" "pumpkin|Calabaza|#f48c1f" "ruby|Rubí|#c81d33" "violet|Violeta|#8b43c7"
)
WIN7=0

# --- Opciones sin preguntas ------------------------------------------------------
ALL=0
IMAGES=()
CLI_SIZE=""; CLI_STYLE=""; CLI_LANG=""; CLI_FORMAT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)    ALL=1 ;;
        --win7-colors) WIN7=1 ;;
        --image)  IMAGES+=("$(readlink -f "${2:?falta la imagen de --image}")"); shift ;;
        --out)    OUT_DIR="$(mkdir -p "${2:?falta la carpeta de --out}" && cd "$2" && pwd)"; shift ;;
        --size)   CLI_SIZE="${2:?falta el tamaño de --size}"; shift ;;
        --style)  CLI_STYLE="${2:?falta el estilo de --style}"; shift ;;
        --lang)   CLI_LANG="${2:?falta el idioma de --lang}"; shift ;;
        --format) CLI_FORMAT="${2:?falta el formato de --format}"; shift ;;
        -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) fail "Opción desconocida: $1 (mira --help)" ;;
    esac
    shift
done

# --- Ejecutable -------------------------------------------------------------
KAINT=""
for candidate in "$DIR/kaint" "$DIR/bin/kaint" "$DIR/build/kaint" "$DIR/build-debug/kaint"; do
    if [[ -x "$candidate" ]]; then KAINT="$candidate"; break; fi
done
if [[ -z "$KAINT" ]] && command -v kaint >/dev/null 2>&1; then
    KAINT="$(command -v kaint)"
    warn "No hay un kaint compilado aquí: se usa el instalado ($KAINT)."
fi
[[ -n "$KAINT" ]] || fail "No encuentro kaint: compílalo (./build.sh --build) o copia este script junto al ejecutable kaint."
HELP="$(QT_QPA_PLATFORM=offscreen "$KAINT" --help 2>/dev/null || true)"
for option in --screenpreview --preview-colored-icons --preview-style --preview-title-buttons --preview-frame; do
    grep -q -- "$option" <<<"$HELP" || fail "Este kaint ($KAINT) es anterior a la opción $option: vuelve a compilarlo."
done

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

# Elige una opción de una lista numerada ($1: pregunta, $2: la de fábrica, 1-n; el resto: opciones).
ask_choice() {
    local prompt="$1" default="$2"; shift 2
    local options=("$@") i choice
    for i in "${!options[@]}"; do
        echo "  $((i + 1))) ${options[i]}" >&2
    done
    while true; do
        read -r -p "$prompt [1-${#options[@]}] ($default): " choice
        [[ -z "$choice" ]] && choice=$default
        if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#options[@]} )); then
            echo "$choice"
            return
        fi
        warn "Escribe un número del 1 al ${#options[@]}."
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
        # Vacío: la imagen de muestra de Kaint (el logo), si está.
        [[ -z "$path" && -f "$DIR/img/other/kaintqt6.ora" ]] && path="$DIR/img/other/kaintqt6.ora"
        if [[ -f "$path" ]]; then
            readlink -f "$path"
            return
        fi
        warn "No existe el archivo: ${path:-(vacío)}"
    done
}

RESOLUTIONS=(1280x720 1366x768 1600x900 1920x1080 2560x1440 3840x2160)
if (( ALL )) && (( ${#IMAGES[@]} == 0 )); then
    [[ -f "$DIR/img/other/kaintqt6.ora" ]] || fail "--all necesita una imagen (--image)."
    IMAGES=("$DIR/img/other/kaintqt6.ora")
fi

if (( ! ALL )); then
    echo "${c_bold}Capturas de muestra de Kaint${c_reset} (configuración de fábrica)"
    echo
    IMAGES+=("$(ask_image "Imagen que mostrar (Intro: la de muestra de Kaint): ")")
    while true; do
        read -r -p "¿Añadir otra imagen abierta en la barra de arriba? [s/N] " more
        [[ "$more" =~ ^[sSyY] ]] || break
        IMAGES+=("$(ask_image "Otra imagen: ")")
    done

    # --- Tema -------------------------------------------------------------------
    echo
    echo "Tema:"
    labels=()
    for t in "${THEMES[@]}"; do labels+=("$(cut -d'|' -f2 <<<"$t")"); done
    labels+=("Todos (una captura de cada uno)")
    choice=$(ask_choice "Elige" 1 "${labels[@]}")
    if (( choice == ${#labels[@]} )); then
        ALL=1
    else
        CHOSEN_THEMES=("${THEMES[choice - 1]}")
    fi

    # --- Estilo de la interfaz --------------------------------------------------
    echo
    echo "Estilo de la interfaz:"
    STYLE=$( (( $(ask_choice "Elige" 2 "Kaint" "Paint.NET 5") == 1 )) && echo kaint || echo paintnet)

    # --- Barra de título --------------------------------------------------------
    # La de Kaint, con los botones del tema (los de XP con Windows XP, los de cristal con
    # Windows 7, en relieve con Windows 98) o los que se elijan.
    echo
    FRAME="$(ask_yes_no "¿Con la barra de título de Kaint (como en Linux)? [S/n] " yes)"
    BUTTONS=auto
    if [[ "$FRAME" == yes ]]; then
        echo "Botones de la barra de título:"
        names=(auto win11 win10 macos win7 xp win98)
        choice=$(ask_choice "Elige" 1 "Los del tema (automáticos)" "Windows 11" "Windows 10" "macOS" "Windows 7" "Windows XP" "Windows 98")
        BUTTONS=${names[choice - 1]}
    fi

    # --- Idioma -----------------------------------------------------------------
    echo
    echo "Idioma:"
    LANGUAGE=$( (( $(ask_choice "Elige" 1 "English" "Español") == 1 )) && echo en || echo es)

    # --- Iconos de herramientas, reglas y ventanas -----------------------------
    echo
    COLORED="$(ask_yes_no "¿Iconos de herramientas con los colores activos? [S/n] " yes)"
    RULERS="$(ask_yes_no "¿Mostrar las reglas? [s/N] " no)"
    TRANSLUCENT="$(ask_yes_no "¿Ventanas translúcidas (como de fábrica)? [S/n] " yes)"

    # --- Resolución -------------------------------------------------------------
    echo
    echo "Resolución:"
    res_labels=()
    for r in "${RESOLUTIONS[@]}"; do res_labels+=("${r/x/ × }"); done
    res_labels+=("Personalizada")
    choice=$(ask_choice "Elige" 3 "${res_labels[@]}")
    if (( choice == ${#res_labels[@]} )); then
        SIZE=""
        while [[ -z "$SIZE" ]]; do
            read -r -p "Ancho x alto (p. ej. 1440x900): " custom
            custom="${custom//[[:space:]]/}"; custom="${custom//×/x}"; custom="${custom,,}"
            if [[ "$custom" =~ ^([0-9]+)x([0-9]+)$ ]] && (( BASH_REMATCH[1] >= 800 && BASH_REMATCH[2] >= 600 )); then
                SIZE="$custom"
            else
                warn "Escribe ancho x alto, como mínimo 800x600."
            fi
        done
    else
        SIZE="${RESOLUTIONS[choice - 1]}"
    fi

    # --- Formato ----------------------------------------------------------------
    echo
    FORMAT=$( (( $(ask_choice "Formato" 1 "PNG" "JPG") == 1 )) && echo png || echo jpg)
else
    STYLE="${CLI_STYLE:-paintnet}"
    [[ "$STYLE" == pdn || "$STYLE" == paint.net ]] && STYLE=paintnet
    LANGUAGE="${CLI_LANG:-en}"
    SIZE="${CLI_SIZE:-1920x1080}"
    FORMAT="${CLI_FORMAT:-png}"
    FRAME=yes; BUTTONS=auto; COLORED=yes; RULERS=no; TRANSLUCENT=yes
fi
if (( ALL )); then
    CHOSEN_THEMES=()
    for t in "${THEMES[@]}"; do
        # El tema KDE solo sale bien en Plasma.
        [[ "$t" == kde\|* && "${XDG_CURRENT_DESKTOP:-}" != *KDE* ]] && continue
        CHOSEN_THEMES+=("$t")
    done
fi
[[ "$SIZE" =~ ^([0-9]+)x([0-9]+)$ ]] || fail "Tamaño no válido: $SIZE (ancho x alto, como 1600x900)."
WIDTH="${SIZE%x*}"
# A partir de 4K la interfaz va al 200 %, como en una pantalla de alta densidad.
SCALE=1
(( WIDTH >= 3200 )) && SCALE=2

# --- Capturas -------------------------------------------------------------------
mkdir -p "$OUT_DIR"
echo
made=0
for t in "${CHOSEN_THEMES[@]}"; do
    IFS='|' read -r scheme label file <<<"$t"
    if [[ "$scheme" == kde && "${XDG_CURRENT_DESKTOP:-}" != *KDE* ]]; then
        warn "El tema KDE necesita Plasma (plasma-integration); fuera de KDE la captura saldrá con el estilo de Qt."
    fi
    if (( ALL )); then
        OUTPUT="$OUT_DIR/$file.$FORMAT"
    else
        OUTPUT="$OUT_DIR/kaint-$STYLE-$file-$LANGUAGE-$SIZE-$(date +%Y%m%d-%H%M%S).$FORMAT"
    fi
    echo "Creando la captura: $label…"
    if "$KAINT" --screenpreview "$OUTPUT" --color-scheme "$scheme" --preview-size "$SIZE" --preview-scale "$SCALE" \
            --preview-colored-icons "$COLORED" --preview-style "$STYLE" --preview-language "$LANGUAGE" \
            --preview-rulers "$RULERS" --preview-translucent "$TRANSLUCENT" \
            --preview-frame "$FRAME" --preview-title-buttons "$BUTTONS" \
            -- "${IMAGES[@]}" 2> >(grep -v "propagateSizeHints" >&2); then
        echo "${c_green}Lista:${c_reset} $OUTPUT"
        made=$((made + 1))
    else
        warn "No se pudo crear la captura de $label."
    fi
done
# Windows 7 y Windows 7 oscuro con cada color de ventana (con la misma resolución que las demás).
if (( WIN7 )); then
    for variant in "windows7|windows7|Windows 7" "windows7dark|windows7-dark|Windows 7 oscuro"; do
        IFS='|' read -r scheme prefix variant_label <<<"$variant"
        for c in "${WIN7_COLORS[@]}"; do
            IFS='|' read -r file label color <<<"$c"
            OUTPUT="$OUT_DIR/$prefix-$file.$FORMAT"
            echo "Creando la captura: $variant_label, $label…"
            if "$KAINT" --screenpreview "$OUTPUT" --color-scheme "$scheme" --preview-size "$SIZE" --preview-scale "$SCALE" \
                    --preview-colored-icons "$COLORED" --preview-style "$STYLE" --preview-language "$LANGUAGE" \
                    --preview-rulers no --preview-translucent "$TRANSLUCENT" --preview-frame yes --preview-title-buttons auto \
                    --preview-win7-color "$color" -- "${IMAGES[@]}" 2> >(grep -v "propagateSizeHints" >&2); then
                echo "${c_green}Lista:${c_reset} $OUTPUT"
                made=$((made + 1))
            fi
        done
    done
fi
(( made > 0 )) || fail "No se pudo crear ninguna captura."

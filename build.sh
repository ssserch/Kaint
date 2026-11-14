#!/usr/bin/env bash
# Compila e instala Kaint en Linux (Arch, Debian/Ubuntu, Alpine...), FreeBSD y macOS, y crea sus
# paquetes (opciones: ./build.sh --help). Sin opciones, un menú pregunta qué compilar.
set -euo pipefail

# La ruta del propio script (el menú se vuelve a llamar con ella, también con "bash build.sh").
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")"

BUILD_TYPE=Release
BUILD_DIR=build
PREFIX="$HOME/.local"
DO_CLEAN=0
DO_INSTALL=0
DO_RUN=0
DO_TEST=0
DO_DEB=0
DO_PKG=0
DO_FLATPAK=0
DO_APPIMAGE=0
DO_TAR=0
DO_WEB=0
DO_DEBUG=0
DO_AVX2=0
DO_MUSL=0
DO_ARM64=0
DO_WINDOWS=0
DO_APK=""
ASSUME_YES=0
KAINTHTML=""
KEYSTORE=""
KEY_ALIAS=""
JOBS=""
USE_CCACHE=1
WINSETUP=""
APP_ID=io.github.ssserch.Kaint
START_TIME=$SECONDS

# --- Paquetes (necesarios y recomendados) -----------------------------------
ARCH_REQUIRED=(gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons libdatachannel)
ARCH_OPTIONAL=(qt6-imageformats kimageformats qt6-translations qt6-multimedia mono libgdiplus)
DEB_REQUIRED=(build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev
              qt6-tools-dev-tools qt6-l10n-tools qt6-svg-plugins qt6-wayland zlib1g-dev libgl-dev
              libssl-dev breeze-icon-theme)
DEB_OPTIONAL=(qt6-image-formats-plugins kimageformat6-plugins qt6-translations-l10n qt6-multimedia-dev mono-devel libgdiplus)
# --deb: CPack calcula las dependencias del paquete con file y dpkg-shlibdeps
ARCH_DEB_TOOLS=(file dpkg)
DEB_DEB_TOOLS=(file dpkg-dev)

# --- Herramientas del AppImage (versiones fijas, comprobadas con su SHA-256) -------------------
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1
LINUXDEPLOY_QT_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1
declare -A APPIMAGE_TOOL_SHA256=(
    [linuxdeploy-x86_64.AppImage]=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
    [linuxdeploy-aarch64.AppImage]=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
    [linuxdeploy-plugin-qt-x86_64.AppImage]=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
    [linuxdeploy-plugin-qt-aarch64.AppImage]=bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1
)

c_bold=$'\e[1m'; c_red=$'\e[31m'; c_yellow=$'\e[33m'; c_green=$'\e[32m'; c_reset=$'\e[0m'
if [[ ! -t 1 ]]; then c_bold=""; c_red=""; c_yellow=""; c_green=""; c_reset=""; fi

VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' version.json)
HOST_OS=$(uname -s)
HOST_ARCH=$(uname -m)
[[ "$HOST_ARCH" == arm64 ]] && HOST_ARCH=aarch64
case "$HOST_OS" in
    Linux) OS_NAME=linux; OS_LABEL="Linux" ;;
    FreeBSD) OS_NAME=freebsd; OS_LABEL="FreeBSD" ;;
    Darwin) OS_NAME=macos; OS_LABEL="macOS" ;;
    MINGW*|MSYS*|CYGWIN*) OS_NAME=windows; OS_LABEL="Windows" ;;
    *) OS_NAME=$(echo "$HOST_OS" | tr '[:upper:]' '[:lower:]'); OS_LABEL=$HOST_OS ;;
esac
if [[ "$OS_NAME" == linux && -f /etc/os-release ]]; then
    OS_LABEL="Linux ($(. /etc/os-release && echo "${PRETTY_NAME:-$NAME}"))"
fi
is_musl() { ldd --version 2>&1 | grep -qi musl; }
container_tool() { command -v podman 2>/dev/null || command -v docker 2>/dev/null || true; }

# --- Menú (sin opciones) ----------------------------------------------------
# Como buildexample.txt: el logo, el sistema y la versión, y qué hacer. Con «Compilar» o
# «Depurar», otro menú con los formatos; los que este sistema no puede crear bien salen en rojo
# con «(no recomendado)».
logo() {
    cat <<'EOF'
▄▄▄   ▄▄▄
███ ▄███▀       ▀▀         ██
███████    ▀▀█▄ ██  ████▄ ▀██▀▀
███▀███▄  ▄█▀██ ██  ██ ██  ██
███  ▀███ ▀█▄██ ██▄ ██ ██  ██
EOF
}

# Formatos: etiqueta, opciones de build.sh y si este sistema los crea bien (1) o no (0).
menu_formats() {
    local linux=0 deb=0 pkg=0 flatpak=0 x64=0 arm=0 freebsd=0 macos=0 avx2=0 musl=0 ctr=0 win=0 winarm=0 apk=0
    [[ "$OS_NAME" == windows ]] && win=1
    [[ "$OS_NAME" == windows && "${PROCESSOR_ARCHITECTURE:-}" == ARM64 ]] && winarm=1
    [[ "$OS_NAME" == linux ]] && linux=1
    [[ "$OS_NAME" == freebsd ]] && freebsd=1
    [[ "$OS_NAME" == macos ]] && macos=1
    [[ "$HOST_ARCH" == x86_64 ]] && x64=1
    [[ "$HOST_ARCH" == aarch64 ]] && arm=1
    [[ -n "$(container_tool)" ]] && ctr=1
    (( linux )) && command -v dpkg-deb >/dev/null 2>&1 && deb=1
    (( linux )) && command -v makepkg >/dev/null 2>&1 && pkg=1
    (( linux )) && { command -v flatpak-builder >/dev/null 2>&1 || command -v flatpak >/dev/null 2>&1; } && flatpak=1
    (( x64 )) && grep -qw avx2 /proc/cpuinfo 2>/dev/null && avx2=1
    (( linux )) && { is_musl || (( ctr )); } && musl=1
    # La versión móvil: con el SDK de Android y Gradle de Linux, macOS o Windows (build.bat).
    (( linux || macos || win )) && apk=1
    FORMATS=(
        "Ejecutable de $OS_LABEL $HOST_ARCH (solo compilar, en build/)|--build|$(( linux || freebsd || macos ))"
        "Instalar en este equipo (~/.local)|--install|$(( linux || freebsd || macos ))"
        "Paquete .deb (Debian/Ubuntu)|--deb|$deb"
        "Paquete de Arch (.pkg.tar.zst)|--pkg|$pkg"
        "Flatpak|--flatpak|$flatpak"
        "AppImage $HOST_ARCH|--appimage|$(( linux && (x64 || arm) ))"
        "AppImage x86_64 AVX2|--appimage --avx2|$(( linux && avx2 ))"
        "AppImage aarch64 (arm64)|--appimage --arm64|$(( linux && (arm || ctr) ))"
        "tar.gz de $OS_LABEL $HOST_ARCH|--tar|$(( linux || freebsd || macos ))"
        "tar.gz Linux x86_64 AVX2|--tar --avx2|$(( linux && avx2 ))"
        "tar.gz Linux musl (Alpine)|--tar --musl|$musl"
        "tar.gz Linux aarch64 (arm64)|--tar --arm64|$(( linux && (arm || ctr) ))"
        "tar.gz Linux aarch64 musl|--tar --arm64 --musl|$(( linux && ctr ))"
        "tar.gz FreeBSD x86_64|--tar --freebsd|$freebsd"
        "tar.gz macOS arm64|--tar --macos|$(( macos && arm ))"
        "Windows x86_64 (.zip)|--windows zip|$win"
        "Windows x86_64 AVX2 (.zip)|--windows zip-avx2|$win"
        "Windows arm64 (.zip)|--windows zip-arm64|$winarm"
        "Instalador de Windows (setup.exe, con los datos de winsetup.json)|--windows setup|$win"
        "Versión web (Kaint.HTML, .zip)|--web|1"
        "Versión móvil (Android, .apk)|--apk|$apk"
        "Versión móvil para Google Play (.aab)|--aab|$apk"
    )
}

run_menu() {
    clear 2>/dev/null || true
    logo
    echo
    echo "$OS_LABEL - Kaint $VERSION"
    echo
    echo "(1) Compilar"
    echo "(2) Depurar"
    echo "(3) Salir"
    echo
    local choice debug=() tries=0
    while :; do
        read -r -p "> " choice || exit 0
        case "$choice" in
            1) break ;;
            2) debug=(--debug); break ;;
            3) exit 0 ;;
        esac
        # Una respuesta que no vale vuelve a preguntar (sin nadie que responda, no sin fin).
        (( ++tries >= 5 )) && exit 0
        echo "Elige 1, 2 o 3."
    done
    menu_formats
    echo
    local i=1 entry label flags ok
    for entry in "${FORMATS[@]}"; do
        IFS='|' read -r label flags ok <<< "$entry"
        if (( ok )); then
            printf '(%d) %s\n' "$i" "$label"
        else
            printf '(%d) %s %s(no recomendado)%s\n' "$i" "$label" "$c_red" "$c_reset"
        fi
        ((i++))
    done
    echo "(0) Volver"
    echo
    tries=0
    while :; do
        read -r -p "> " choice || exit 0
        if [[ "$choice" == 0 ]]; then
            exec bash "$SELF" --menu
        fi
        [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#FORMATS[@]} )) && break
        (( ++tries >= 5 )) && exit 0
        echo "${c_red}Elige un número del 0 al ${#FORMATS[@]}.${c_reset}"
    done
    IFS='|' read -r label flags ok <<< "${FORMATS[choice-1]}"
    # La versión móvil y la web no se compilan: no tienen variante de depuración.
    [[ "$flags" == --apk || "$flags" == --aab || "$flags" == --web ]] && debug=()
    if (( ! ok )); then
        echo "${c_yellow}«$label» no se crea bien en $OS_LABEL: puede fallar o salir incompleto.${c_reset}"
        read -r -p "¿Seguir de todos modos? [s/N] " answer
        [[ "$answer" == [sSyY]* ]] || exit 0
    fi
    # shellcheck disable=SC2086
    exec bash "$SELF" $flags "${debug[@]}"
}

if [[ $# -eq 0 && -t 0 && -t 1 ]] || [[ "${1:-}" == --menu ]]; then
    run_menu
fi

# --- Opciones ---------------------------------------------------------------
usage() {
    cat <<EOF
Uso: $0 [opciones]

  -b, --build          solo compila el ejecutable en build/ (con --debug, en build-debug/), sin
                       instalar ni empaquetar: lo mismo que sin opciones fuera de una terminal
  -d, --debug          compila con símbolos de depuración (build-debug/)
  -c, --clean          borra la carpeta de compilación antes de empezar
  -j, --jobs N         compila con N hilos (por defecto, todos los del procesador)
  -t, --test           ejecuta las pruebas del núcleo después de compilar
  -i, --install        instala (por defecto en ~/.local, sin sudo)
      --prefix RUTA    prefijo de instalación (p. ej. /usr/local; usará sudo si hace falta)
  -r, --run            ejecuta Kaint al terminar
      --deb            crea el paquete .deb en build-deb/ (necesita dpkg)
      --pkg            crea el paquete de Arch en build-pkg/ (necesita makepkg)
      --flatpak        crea e instala el Flatpak con el código de esta carpeta (necesita flatpak-builder)
      --appimage       crea el AppImage en build-appimage/ (descarga linuxdeploy)
      --tar            crea un .tar.gz portable (Linux, FreeBSD o macOS: el de este sistema) en build-tar/
      --avx2           compila para procesadores con AVX2 (x86-64-v3): más rápido, no va en los antiguos
      --musl           compila para musl (Alpine) dentro de un contenedor de Alpine (podman o docker)
      --arm64          compila para aarch64: nativo en un equipo arm64; si no, en un contenedor arm64
                       (podman o docker con qemu-user-static)
      --freebsd        (con --tar) el .tar.gz de FreeBSD: solo en FreeBSD
      --macos          (con --tar) el .tar.gz de macOS con Kaint.app: solo en macOS
      --windows FORMATO  zip, zip-avx2, zip-arm64 o setup: solo con build.bat, en Windows (la
                       información del instalador, setup, se cambia en winsetup.json)
      --winsetup RUTA  (con --windows setup) otro archivo en vez de winsetup.json
      --web            crea el .zip de la versión web (Kaint.HTML) en build-web/
      --apk            crea la versión móvil (Android) en build-apk/Kaint-VERSIÓN.apk
      --aab            crea el App Bundle para Google Play en build-aab/Kaint-VERSIÓN.aab
      --kainthtml RUTA (con --apk o --aab) otra carpeta de KaintHTML (por defecto, la de aquí)
      --keystore RUTA  (con --apk o --aab) la llave (.jks o .keystore) con la que firmar
      --alias NOMBRE   (con --apk o --aab) el usuario (alias) de esa llave
  -y, --yes            (con --apk o --aab) descarga sin preguntar lo que falte
      --no-ccache      no usa ccache ni sccache aunque estén instalados
  -h, --help           muestra esta ayuda

Sin opciones, un menú pregunta qué compilar (en rojo, lo que este sistema no crea bien).
Con ccache o sccache instalados, se usan solos: volver a compilar tarda mucho menos.
Versión móvil: necesita un JDK del 17 al 25 (mejor el 21); el SDK de Android (en \$ANDROID_HOME o
~/Android/Sdk) y Gradle se descargan la primera vez. Las contraseñas de la llave se preguntan o se
toman de KAINT_STORE_PASSWORD y KAINT_KEY_PASSWORD.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--build)   ;;
        -d|--debug)   DO_DEBUG=1 ;;
        -c|--clean)   DO_CLEAN=1 ;;
        -t|--test)    DO_TEST=1 ;;
        -i|--install) DO_INSTALL=1 ;;
        --prefix)     PREFIX="${2:?falta la ruta de --prefix}"; shift ;;
        -r|--run)     DO_RUN=1 ;;
        --deb)        DO_DEB=1; BUILD_TYPE=Release; BUILD_DIR=build-deb; PREFIX=/usr ;;
        --pkg)        DO_PKG=1 ;;
        --flatpak)    DO_FLATPAK=1 ;;
        --appimage)   DO_APPIMAGE=1; BUILD_TYPE=Release; BUILD_DIR=build-appimage; PREFIX=/usr ;;
        --tar)        DO_TAR=1; BUILD_TYPE=Release; BUILD_DIR=build-tar; PREFIX=/usr ;;
        --avx2)       DO_AVX2=1 ;;
        --musl)       DO_MUSL=1 ;;
        --arm64)      DO_ARM64=1 ;;
        --freebsd)    [[ "$OS_NAME" == freebsd ]] || { echo "${c_red}El .tar.gz de FreeBSD se crea en FreeBSD (./build.sh --tar).${c_reset}" >&2; exit 1; } ;;
        --macos)      [[ "$OS_NAME" == macos ]] || { echo "${c_red}El .tar.gz de macOS se crea en macOS (./build.sh --tar).${c_reset}" >&2; exit 1; } ;;
        --windows)    DO_WINDOWS=1; WINDOWS_FORMAT="${2:-zip}"; shift ;;
        --winsetup)   WINSETUP="${2:?falta la ruta de --winsetup}"; shift ;;
        --web)        DO_WEB=1 ;;
        --apk)        DO_APK=apk ;;
        --aab)        DO_APK=aab ;;
        -y|--yes)     ASSUME_YES=1 ;;
        --kainthtml)  KAINTHTML="${2:?falta la carpeta de --kainthtml}"; shift ;;
        --keystore)   KEYSTORE="${2:?falta la llave de --keystore}"; shift ;;
        --alias)      KEY_ALIAS="${2:?falta el alias de --alias}"; shift ;;
        -j|--jobs)    JOBS="${2:?falta el número de hilos de --jobs}"; shift
                      [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo "${c_red}--jobs quiere un número de hilos, no «$JOBS».${c_reset}" >&2; exit 1; } ;;
        --no-ccache)  USE_CCACHE=0 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "${c_red}Opción desconocida: $1${c_reset}" >&2; usage; exit 1 ;;
    esac
    shift
done

# Al terminar bien, lo que ha tardado; si algo falla, dónde.
STEP=""
on_exit() {
    local rc=$?
    local took=$(( SECONDS - START_TIME ))
    if (( rc != 0 )) && [[ -n "$STEP" ]]; then
        echo "${c_red}Falló: $STEP.${c_reset}" >&2
    elif (( rc == 0 && took > 0 )); then
        if (( took >= 60 )); then echo "Tiempo: $(( took / 60 )) min $(( took % 60 )) s"; else echo "Tiempo: $took s"; fi
    fi
}
trap on_exit EXIT

# Un archivo creado, con su tamaño.
report() {
    local what=$1 file=$2 note=${3:-} size=""
    if [[ -f "$file" ]]; then
        size=$(du -h "$file" 2>/dev/null | cut -f1)
        [[ -n "$size" ]] && size=" ($size)"
    fi
    echo "${c_green}$what:${c_reset} $file$size${note:+ $note}"
}

if (( DO_DEBUG )); then
    BUILD_TYPE=Debug
    [[ "$BUILD_DIR" == build ]] && BUILD_DIR=build-debug || BUILD_DIR="$BUILD_DIR-debug"
fi
(( DO_AVX2 )) && BUILD_DIR="$BUILD_DIR-avx2"
# En un contenedor (musl o arm64), su propia carpeta: la de este equipo no se mezcla con la suya.
BUILD_DIR="$BUILD_DIR${KAINT_BUILD_SUFFIX:-}"

# --- Windows (solo con build.bat) ---------------------------------------------
if (( DO_WINDOWS )); then
    if [[ "$OS_NAME" != windows ]]; then
        echo "${c_red}Los paquetes de Windows se crean en Windows con build.bat (--zip, --zip --avx2, --zip --arm64, --setup).${c_reset}" >&2
        exit 1
    fi
    # En Git Bash o MSYS2: lo hace build.bat.
    case "$WINDOWS_FORMAT" in
        zip) bat_args=(--zip) ;;
        zip-avx2) bat_args=(--zip --avx2) ;;
        zip-arm64) bat_args=(--zip --arm64) ;;
        setup) bat_args=(--setup) ;;
        *) echo "${c_red}Formato de Windows desconocido: $WINDOWS_FORMAT${c_reset}" >&2; exit 1 ;;
    esac
    (( DO_DEBUG )) && bat_args+=(--debug)
    [[ -n "$JOBS" ]] && bat_args+=(--jobs "$JOBS")
    [[ -n "$WINSETUP" ]] && bat_args+=(--winsetup "$(cygpath -w "$WINSETUP" 2>/dev/null || echo "$WINSETUP")")
    (( USE_CCACHE )) || bat_args+=(--no-ccache)
    exec cmd //c build.bat "${bat_args[@]}"
fi

# --- Versión móvil (Kaint.APK) ----------------------------------------------------
# La versión web de Kaint (KaintHTML) como aplicación de Android, con el proyecto de android/: un
# APK para instalarla en el teléfono (build-apk/) o un App Bundle para Google Play (build-aab/).
# Hacen falta un JDK del 17 al 25 (mejor el 21); el SDK de Android y Gradle se descargan la primera
# vez (comprobados con su suma) y las licencias del SDK las acepta quien compila.
ANDROID_PLATFORM="platforms;android-36"
ANDROID_BUILD_TOOLS="build-tools;36.1.0"
ANDROID_CMDLINE_BUILD=16111833
GRADLE_VERSION=9.8.2
ANDROID_CACHE="${KAINT_ANDROID_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/kaint-android}"
ANDROID_KEY_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/Kaint/android"

# Lee una respuesta del terminal (también si la salida va a un archivo); sin terminal, de la
# entrada estándar.
TTY=/dev/stdin
{ : </dev/tty; } 2>/dev/null && TTY=/dev/tty
ask() {
    local reply=""
    read -r -p "$1" reply <"$TTY" || true
    printf '%s' "$reply"
}
ask_secret() {
    local reply=""
    read -r -s -p "$1" reply <"$TTY" || true
    echo >&2
    printf '%s' "$reply"
}
confirm() {
    (( ASSUME_YES )) && return 0
    local reply
    reply=$(ask "$1 [s/N]: ")
    [[ $reply =~ ^[sSyY] ]]
}
die() { echo "${c_red}Error:${c_reset} $*" >&2; exit 1; }
expand_path() {
    local p=$1
    p="${p/#\~/$HOME}"
    [[ $p == /* ]] || p="$PWD/$p"
    printf '%s' "$p"
}
java_major() {
    "$1" -XshowSettings:properties -version 2>&1 | sed -n 's/.*java.specification.version = \([0-9]*\).*/\1/p' | head -n1
}
find_java() {
    local candidates=() c home major
    [[ -n ${JAVA_HOME:-} ]] && candidates+=("$JAVA_HOME")
    for home in /usr/lib/jvm/java-21* /usr/lib/jvm/java-17* /usr/lib/jvm/java-2[2-5]* /usr/lib/jvm/* \
                /Library/Java/JavaVirtualMachines/*/Contents/Home /opt/homebrew/opt/openjdk*/libexec/openjdk.jdk/Contents/Home; do
        [[ -d $home ]] && candidates+=("$home")
    done
    if [[ "$OS_NAME" == macos && -x /usr/libexec/java_home ]]; then
        home=$(/usr/libexec/java_home -v 21 2>/dev/null || /usr/libexec/java_home 2>/dev/null || true)
        [[ -n $home ]] && candidates+=("$home")
    fi
    if command -v java >/dev/null; then
        candidates+=("$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")")
    fi
    for c in "${candidates[@]}"; do
        [[ -x $c/bin/java && -x $c/bin/javac ]] || continue
        major=$(java_major "$c/bin/java")
        if [[ -n $major && $major -ge 17 && $major -le 25 ]]; then
            printf '%s' "$c"
            return 0
        fi
    done
    return 1
}
sha_check() {
    # sha_check ALGORITMO SUMA ARCHIVO (sha1sum/sha256sum o, en macOS, shasum).
    local bits=$1 sum=$2 file=$3
    if command -v "sha${bits}sum" >/dev/null 2>&1; then
        echo "$sum  $file" | "sha${bits}sum" -c --quiet - >/dev/null 2>&1
    else
        echo "$sum  $file" | shasum -a "$bits" -c - >/dev/null 2>&1
    fi
}

build_mobile() {
    local kind=$DO_APK out="$PWD/build-$DO_APK"
    case "$OS_NAME" in
        linux) cmdline_os=linux; cmdline_sha1=e025545c62a8e64c7559119566a569fb1dec5f60 ;;
        macos)
            if [[ "$HOST_ARCH" == aarch64 ]]; then
                cmdline_os=mac_arm64; cmdline_sha1=ad03dc49bfacfd52c110b14104ea548b8a07e830
            else
                cmdline_os=mac_x86_64; cmdline_sha1=112cf9618794a997ff273537d55bee02c22abffe
            fi ;;
        *) die "la versión móvil se crea en Linux, macOS o Windows (build.bat --$kind)" ;;
    esac
    local cmdline_url="https://dl.google.com/android/repository/commandlinetools-${cmdline_os}-${ANDROID_CMDLINE_BUILD}_latest.zip"

    echo "${c_bold}Kaint.APK${c_reset}: la versión web de Kaint como aplicación de Android ($( [[ $kind == apk ]] && echo "APK" || echo "App Bundle para Google Play" ))."

    # La carpeta de KaintHTML (la de este repositorio, salvo que se pida otra).
    STEP="buscar KaintHTML"
    if [[ -z $KAINTHTML ]]; then
        local default="$PWD/KaintHTML" reply=""
        if [[ -t 0 ]] && (( ! ASSUME_YES )); then
            reply=$(ask "Carpeta de KaintHTML (Intro: $default): ")
        fi
        KAINTHTML=${reply:-$default}
    fi
    KAINTHTML=$(expand_path "$KAINTHTML")
    [[ -f $KAINTHTML/index.html && -f $KAINTHTML/js/main.js ]] \
        || die "«$KAINTHTML» no es la carpeta de KaintHTML (falta index.html o js/main.js)"

    # La llave: la de Google Play (App Bundle, o la que se dé) o una propia de este equipo (APK).
    STEP="preparar la llave"
    local store_password=${KAINT_STORE_PASSWORD:-} key_password=${KAINT_KEY_PASSWORD:-}
    if [[ $kind == aab || -n $KEYSTORE ]]; then
        [[ -n $KEYSTORE ]] || KEYSTORE=$(ask "Ubicación de la llave (.jks o .keystore): ")
        KEYSTORE=$(expand_path "$KEYSTORE")
        [[ -f $KEYSTORE ]] || die "no existe la llave «$KEYSTORE»"
        [[ -n $KEY_ALIAS ]] || KEY_ALIAS=$(ask "Usuario (alias) de la llave: ")
        [[ -n $KEY_ALIAS ]] || die "falta el usuario (alias) de la llave"
        [[ -n $store_password ]] || store_password=$(ask_secret "Contraseña de la llave: ")
        [[ -n $key_password ]] || key_password=$(ask_secret "Contraseña del usuario (Intro: la misma): ")
        key_password=${key_password:-$store_password}
    else
        # Para instalar encima una versión nueva hay que firmarla con la misma llave: se conserva.
        KEYSTORE="$ANDROID_KEY_DIR/kaint-apk.jks"
        KEY_ALIAS=kaint
        store_password=kaint-apk
        key_password=kaint-apk
    fi

    # Java, unzip y curl.
    STEP="buscar Java"
    local java_home
    java_home=$(find_java) || die "hace falta un JDK del 17 al 25 (mejor el 21).
  Arch Linux:     sudo pacman -S jdk21-openjdk
  Debian/Ubuntu:  sudo apt install openjdk-21-jdk   (u openjdk-17-jdk)
  macOS:          brew install openjdk@21"
    export JAVA_HOME=$java_home
    echo "Java:   $JAVA_HOME ($(java_major "$JAVA_HOME/bin/java"))"
    command -v unzip >/dev/null || die "hace falta unzip"
    command -v curl >/dev/null || die "hace falta curl"

    # SDK de Android.
    STEP="preparar el SDK de Android"
    local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [[ -z $sdk ]]; then
        [[ "$OS_NAME" == macos ]] && sdk="$HOME/Library/Android/sdk" || sdk="$HOME/Android/Sdk"
    fi
    local sdkmanager="$sdk/cmdline-tools/latest/bin/sdkmanager"
    if [[ ! -x $sdkmanager ]]; then
        echo "${c_yellow}No está el SDK de Android en $sdk${c_reset} (se puede elegir otra carpeta con ANDROID_HOME)."
        confirm "¿Descargar las herramientas de línea de órdenes del SDK de Android (unos 150 MB)?" \
            || die "sin el SDK de Android no se puede compilar"
        mkdir -p "$ANDROID_CACHE" "$sdk/cmdline-tools"
        local zip="$ANDROID_CACHE/$(basename "$cmdline_url")"
        if [[ ! -f $zip ]]; then
            curl -fL --progress-bar -o "$zip.part" "$cmdline_url"
            mv "$zip.part" "$zip"
        fi
        sha_check 1 "$cmdline_sha1" "$zip" || { rm -f "$zip"; die "la descarga del SDK no coincide con su suma"; }
        local tmp
        tmp=$(mktemp -d)
        unzip -q "$zip" -d "$tmp"
        rm -rf "$sdk/cmdline-tools/latest"
        mv "$tmp/cmdline-tools" "$sdk/cmdline-tools/latest"
        rm -rf "$tmp"
    fi
    local missing=() pkg
    for pkg in "$ANDROID_PLATFORM" "$ANDROID_BUILD_TOOLS"; do
        [[ -d "$sdk/${pkg//;//}" ]] || missing+=("$pkg")
    done
    if (( ${#missing[@]} )); then
        echo "Instalando del SDK de Android: ${missing[*]}"
        if [[ ! -d $sdk/licenses ]] || ! grep -qs . "$sdk/licenses/android-sdk-license"; then
            echo "Para instalarlos hay que aceptar las licencias del SDK de Android:"
            "$sdkmanager" --sdk_root="$sdk" --licenses <"$TTY"
        fi
        "$sdkmanager" --sdk_root="$sdk" "${missing[@]}"
    fi

    # Gradle.
    STEP="preparar Gradle"
    local gradle="$ANDROID_CACHE/gradle-$GRADLE_VERSION/bin/gradle"
    if [[ ! -x $gradle ]]; then
        confirm "¿Descargar Gradle $GRADLE_VERSION (unos 140 MB)?" || die "sin Gradle no se puede compilar"
        mkdir -p "$ANDROID_CACHE"
        local base="https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip"
        local gzip="$ANDROID_CACHE/gradle-$GRADLE_VERSION-bin.zip" expected
        curl -fL --progress-bar -o "$gzip" "$base"
        expected=$(curl -fsSL "$base.sha256")
        sha_check 256 "$expected" "$gzip" || { rm -f "$gzip"; die "la descarga de Gradle no coincide con su suma"; }
        unzip -q -o "$gzip" -d "$ANDROID_CACHE"
        rm -f "$gzip"
    fi

    # El proyecto, con KaintHTML dentro (en una copia: android/ no se toca).
    STEP="preparar el proyecto de Android"
    local major minor patch code work="$out/project"
    IFS=. read -r major minor patch <<<"$VERSION"
    code=$(( ${major:-0} * 10000 + ${minor:-0} * 100 + ${patch//[^0-9]/} + 0 ))
    echo "Preparando Kaint.APK $VERSION ($kind) en $work"
    rm -rf "$work"
    mkdir -p "$out"
    cp -r "$PWD/android" "$work"
    rm -rf "$work/.gradle" "$work/app/build" "$work/build"
    local assets="$work/app/src/main/assets/www"
    mkdir -p "$assets"
    tar -C "$KAINTHTML" --exclude=./tests --exclude=./tools --exclude=./node_modules --exclude=./.git \
        --exclude=./README.md --exclude=./package.json --exclude=./package-lock.json -cf - . | tar -C "$assets" -xf -
    # El icono de Kaint (Android escala el que más se acerca a cada densidad).
    local pair density file
    for pair in "mdpi:kaint-64.png" "xxxhdpi:kaint-256.png"; do
        density=${pair%%:*} file=${pair#*:}
        if [[ -f $KAINTHTML/assets/$file ]]; then
            mkdir -p "$work/app/src/main/res/mipmap-$density"
            cp "$KAINTHTML/assets/$file" "$work/app/src/main/res/mipmap-$density/ic_launcher.png"
        fi
    done
    compgen -G "$work/app/src/main/res/mipmap-*/ic_launcher.png" >/dev/null || die "falta el icono (assets/kaint-256.png de KaintHTML)"
    echo "sdk.dir=$sdk" >"$work/local.properties"

    # La llave propia del APK se crea la primera vez; cualquier llave se comprueba antes de compilar.
    STEP="comprobar la llave"
    if [[ $KEYSTORE == "$ANDROID_KEY_DIR/kaint-apk.jks" && ! -f $KEYSTORE ]]; then
        echo "Creando la llave de este equipo para firmar el APK ($KEYSTORE)"
        mkdir -p "$ANDROID_KEY_DIR"
        "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$KEYSTORE" -alias "$KEY_ALIAS" -keyalg RSA -keysize 4096 -validity 10000 \
            -storepass "$store_password" -keypass "$key_password" -dname "CN=Kaint, O=Kaint" >/dev/null
    fi
    KAINT_CHECK_PASSWORD=$store_password "$JAVA_HOME/bin/keytool" -list -keystore "$KEYSTORE" -alias "$KEY_ALIAS" \
        -storepass:env KAINT_CHECK_PASSWORD >/dev/null 2>&1 \
        || die "no se pudo abrir la llave «$KEYSTORE» con ese usuario (alias) y contraseña"

    # Compilar (las contraseñas van por variables de entorno, no en la línea de órdenes).
    STEP="compilar la versión móvil con Gradle"
    local task=assembleRelease
    [[ $kind == aab ]] && task=bundleRelease
    echo "${c_bold}Compilando ($task)...${c_reset}"
    KAINT_STORE_FILE=$KEYSTORE KAINT_STORE_PASSWORD=$store_password KAINT_KEY_ALIAS=$KEY_ALIAS KAINT_KEY_PASSWORD=$key_password \
        ANDROID_HOME=$sdk "$gradle" -p "$work" --no-daemon --console=plain \
        -PkaintVersionName="$VERSION" -PkaintVersionCode="$code" "$task"

    # Todos los archivos de la web tienen que estar dentro: si falta uno de sus módulos de
    # JavaScript, la aplicación se queda en blanco al abrirse.
    STEP="comprobar el resultado"
    local result final prefix inside missing_files
    if [[ $kind == aab ]]; then
        result="$work/app/build/outputs/bundle/release/app-release.aab"; final="$out/Kaint-$VERSION.aab"; prefix="base/assets/www"
    else
        result="$work/app/build/outputs/apk/release/app-release.apk"; final="$out/Kaint-$VERSION.apk"; prefix="assets/www"
    fi
    [[ -f $result ]] || die "Gradle terminó pero no está $result"
    inside=$(unzip -Z1 "$result" | sed -n "s|^$prefix/||p" | sort)
    missing_files=$(comm -23 <(cd "$assets" && find . -type f | sed 's|^\./||' | sort) <(printf '%s\n' "$inside"))
    [[ -z $missing_files ]] || die "faltan archivos de la web dentro de $(basename "$result") ($prefix):
$(printf '%s\n' "$missing_files" | head -n 20)"
    cp "$result" "$final"
    STEP=""
    echo
    report "Versión móvil" "$final"
    if [[ $kind == apk ]]; then
        echo "  Para instalarla con el teléfono conectado (depuración USB): adb install -r \"$final\""
        echo "  Para instalar encima una versión nueva, se firma con la misma llave: $KEYSTORE"
    else
        echo "  Súbelo a Google Play Console (Versiones > Crear una versión)."
    fi
}

if [[ -n "$DO_APK" ]]; then
    # En Windows (Git Bash o MSYS2), lo hace build.bat, sin WSL.
    if [[ "$OS_NAME" == windows ]]; then
        bat_args=(--"$DO_APK")
        (( ASSUME_YES )) && bat_args+=(--yes)
        [[ -n $KAINTHTML ]] && bat_args+=(--kainthtml "$(cygpath -w "$KAINTHTML" 2>/dev/null || echo "$KAINTHTML")")
        [[ -n $KEYSTORE ]] && bat_args+=(--keystore "$(cygpath -w "$KEYSTORE" 2>/dev/null || echo "$KEYSTORE")")
        [[ -n $KEY_ALIAS ]] && bat_args+=(--alias "$KEY_ALIAS")
        exec cmd //c build.bat "${bat_args[@]}"
    fi
    build_mobile
    exit 0
fi

# --- Versión web ----------------------------------------------------------------
if (( DO_WEB )); then
    echo "${c_bold}Creando el .zip de la versión web...${c_reset}"
    mkdir -p build-web
    out="build-web/kaint-web-$VERSION.zip"
    rm -f "$out"
    if command -v zip >/dev/null 2>&1; then
        (cd KaintHTML && zip -qr "../$out" . -x 'tests/*' -x 'node_modules/*' -x 'package.json' -x 'package-lock.json')
    else
        py=""
        for candidate in python3 python; do
            if "$candidate" -c "import zipfile" >/dev/null 2>&1; then py=$candidate; break; fi
        done
        [[ -n "$py" ]] || { echo "${c_red}Hace falta zip o Python para crear el .zip.${c_reset}" >&2; exit 1; }
        STEP="crear el .zip de la versión web"
        "$py" - "$out" <<'PY'
import os, sys, zipfile
out = sys.argv[1]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk("KaintHTML"):
        dirs[:] = [d for d in dirs if d not in ("tests", "node_modules")]
        for f in files:
            if f in ("package.json", "package-lock.json"):
                continue
            path = os.path.join(root, f)
            z.write(path, os.path.relpath(path, "KaintHTML"))
PY
    fi
    STEP=""
    report "Versión web" "$out" "(sírvela con cualquier servidor web estático)"
    exit 0
fi

# --- musl o arm64 en un contenedor ----------------------------------------------
# Lo que este equipo no compila de forma nativa: el mismo build.sh dentro de un contenedor de
# Alpine (musl) o de Debian (glibc), de la arquitectura pedida (arm64 con qemu-user-static).
if [[ -z "${KAINT_IN_CONTAINER:-}" ]] && { (( DO_MUSL )) && ! is_musl || (( DO_ARM64 )) && [[ "$HOST_ARCH" != aarch64 ]]; }; then
    tool=$(container_tool)
    if [[ -z "$tool" ]]; then
        echo "${c_red}Hace falta podman o docker para compilar para musl o arm64 en este equipo.${c_reset}" >&2
        exit 1
    fi
    platform=linux/amd64
    (( DO_ARM64 )) && platform=linux/arm64
    inner=(--tar)
    (( DO_APPIMAGE )) && inner=(--appimage)
    (( DO_DEBUG )) && inner+=(--debug)
    # ca-certificates: sin ellos no se descargan por HTTPS libdatachannel ni linuxdeploy (con
    # --no-install-recommends, Debian no lo instala junto a curl).
    if (( DO_MUSL )); then
        image=docker.io/library/alpine:3.22
        setup="apk add --no-cache bash build-base cmake ninja qt6-qtbase-dev qt6-qttools-dev qt6-qtsvg-dev qt6-qtwayland-dev zlib-dev openssl-dev ca-certificates curl file"
    else
        image=docker.io/library/debian:trixie
        setup="apt-get update && apt-get install -y --no-install-recommends ${DEB_REQUIRED[*]} ca-certificates curl file"
    fi
    build_suffix=""
    (( DO_ARM64 )) && build_suffix="-arm64"
    (( DO_MUSL )) && build_suffix="$build_suffix-musl"
    echo "${c_bold}Compilando en un contenedor ($image, $platform)...${c_reset}"
    "$tool" run --rm --platform "$platform" -v "$PWD:/src" -w /src -e KAINT_IN_CONTAINER=1 \
        -e KAINT_BUILD_SUFFIX="$build_suffix" \
        -e KAINT_TAR_SUFFIX="$( (( DO_MUSL )) && echo -musl )" "$image" sh -c "$setup && ./build.sh ${inner[*]}"
    exit 0
fi

# --- Dependencias -----------------------------------------------------------
check_packages() {
    local installed=$1 install_cmd=$2; shift 2
    local -n required=$1 optional=$2
    local missing=() optional_missing=() pkg
    for pkg in "${required[@]}"; do $installed "$pkg" || missing+=("$pkg"); done
    if (( ${#missing[@]} )); then
        echo "${c_red}${c_bold}Faltan paquetes necesarios:${c_reset} ${missing[*]}" >&2
        echo "Instálalos con:" >&2
        echo "  $install_cmd ${missing[*]}" >&2
        exit 1
    fi
    for pkg in "${optional[@]}"; do $installed "$pkg" || optional_missing+=("$pkg"); done
    if (( ${#optional_missing[@]} )); then
        echo "${c_yellow}Recomendado (opcional):${c_reset} $install_cmd ${optional_missing[*]}"
        if [[ " ${optional_missing[*]} " == *" mono"* ]]; then
            echo "  Sin mono no se compila la capa de compatibilidad con los complementos de Paint.NET."
        elif [[ " ${optional_missing[*]} " == *" libgdiplus "* ]]; then
            echo "  Sin libgdiplus muchos complementos de Paint.NET no pueden cargarse (usan System.Drawing)."
        fi
        # El .deb se reparte: mejor decidir antes de crearlo sin esas funciones.
        if (( DO_DEB )) && [[ -t 0 ]]; then
            local answer
            read -r -p "¿Crear el .deb sin ellos? [s/N] " answer
            [[ "$answer" == [sSyY]* ]] || { echo "Cancelado."; exit 1; }
        fi
    fi
}
pacman_installed() { pacman -Qq "$1" >/dev/null 2>&1; }
dpkg_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }

if (( DO_FLATPAK )); then
    : # Flatpak trae sus propias dependencias (runtime y SDK de KDE)
elif [[ -n "${KAINT_IN_CONTAINER:-}" ]] || is_musl || [[ "$OS_NAME" != linux ]]; then
    : # Contenedor, Alpine, FreeBSD o macOS: las dependencias las instala quien compila
elif [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    echo "${c_yellow}CMAKE_PREFIX_PATH definido (Qt propio): se omite la comprobación de paquetes.${c_reset}"
elif command -v pacman >/dev/null 2>&1; then
    (( DO_DEB )) && ARCH_REQUIRED+=("${ARCH_DEB_TOOLS[@]}")
    check_packages pacman_installed "sudo pacman -S --needed" ARCH_REQUIRED ARCH_OPTIONAL
elif command -v dpkg-query >/dev/null 2>&1; then
    (( DO_DEB )) && DEB_REQUIRED+=("${DEB_DEB_TOOLS[@]}")
    check_packages dpkg_installed "sudo apt install" DEB_REQUIRED DEB_OPTIONAL
else
    echo "${c_yellow}No se encontró pacman ni apt; se omite la comprobación de dependencias.${c_reset}"
fi

# --- Paquete de Arch ---------------------------------------------------------
if (( DO_PKG )); then
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "${c_red}Falta makepkg (paquete pacman): --pkg solo funciona en Arch Linux.${c_reset}" >&2
        exit 1
    fi
    echo "${c_bold}Creando el paquete de Arch...${c_reset}"
    mkdir -p build-pkg
    (cd packaging/arch && KAINT_SOURCE_DIR="$PWD/../.." BUILDDIR="$PWD/../../build-pkg" \
        PKGDEST="$PWD/../../build-pkg" makepkg --force --cleanbuild)
    pkg=$(ls -t build-pkg/kaint-[0-9]*.pkg.tar.* | head -n 1)
    echo "${c_green}Paquete:${c_reset} $pkg"
    echo "  Instálalo con: sudo pacman -U $pkg"
    exit 0
fi

# --- Flatpak ----------------------------------------------------------------
if (( DO_FLATPAK )); then
    if command -v flatpak-builder >/dev/null 2>&1; then
        builder=(flatpak-builder)
    elif flatpak info org.flatpak.Builder >/dev/null 2>&1; then
        builder=(flatpak run org.flatpak.Builder)
    else
        echo "${c_red}Falta flatpak-builder.${c_reset} Instálalo con uno de estos:" >&2
        echo "  sudo pacman -S flatpak-builder" >&2
        echo "  sudo apt install flatpak-builder" >&2
        echo "  flatpak install flathub org.flatpak.Builder" >&2
        exit 1
    fi
    echo "${c_bold}Creando el Flatpak...${c_reset}"
    mkdir -p build-flatpak
    # El manifiesto de Flathub descarga la versión publicada; aquí el módulo kaint (el último)
    # usa esta carpeta, sin lo que ignora git.
    {
        awk '{ line[NR] = $0 } /^    sources:$/ { last = NR } END { for (i = 1; i <= last; i++) print line[i] }' \
            "packaging/flatpak/$APP_ID.yml"
        echo "      - type: dir"
        printf '        path: "%s"\n' "$PWD"
        echo "        skip:"
        echo '          - ".git"'
        while read -r ignored || [[ -n "$ignored" ]]; do
            [[ -n "$ignored" && "$ignored" != \#* ]] && printf '          - "%s"\n' "${ignored%/}"
        done < .gitignore
    } > "build-flatpak/$APP_ID.yml"
    flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
    "${builder[@]}" --user --install --install-deps-from=flathub --force-clean \
        --state-dir=build-flatpak/.flatpak-builder build-flatpak/build-dir "build-flatpak/$APP_ID.yml"
    echo "${c_green}Instalado:${c_reset} flatpak run $APP_ID (también en el menú de aplicaciones)"
    exit 0
fi

# --- Compilación ------------------------------------------------------------
# Una carpeta configurada desde otra ruta (en un contenedor, o con el proyecto en otro sitio) hace
# fallar a CMake: se empieza de cero.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && (( ! DO_CLEAN )); then
    cache_src=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")
    if [[ -n "$cache_src" && "$cache_src" != "$PWD" ]]; then
        echo "${c_yellow}$BUILD_DIR/ se configuró desde $cache_src: se borra.${c_reset}"
        DO_CLEAN=1
    fi
fi
if (( DO_CLEAN )); then
    echo "${c_bold}Borrando $BUILD_DIR/${c_reset}"
    rm -rf -- "$BUILD_DIR"
fi

echo "${c_bold}Configurando ($BUILD_TYPE)...${c_reset}"
extra_cmake=()
# ccache o sccache, si están: volver a compilar tarda mucho menos.
if (( USE_CCACHE )); then
    for launcher in ccache sccache; do
        if command -v "$launcher" >/dev/null 2>&1; then
            echo "Caché:  $(command -v "$launcher")"
            extra_cmake+=(-DCMAKE_C_COMPILER_LAUNCHER="$launcher" -DCMAKE_CXX_COMPILER_LAUNCHER="$launcher")
            break
        fi
    done
fi
# AVX2: el nivel x86-64-v3 (Haswell, 2013, y posteriores).
if (( DO_AVX2 )); then
    if [[ "$HOST_ARCH" != x86_64 ]]; then
        echo "${c_red}--avx2 es solo para x86_64.${c_reset}" >&2
        exit 1
    fi
    extra_cmake+=(-DCMAKE_C_FLAGS="-march=x86-64-v3" -DCMAKE_CXX_FLAGS="-march=x86-64-v3")
fi
# macOS: el .app con su icono (el de Qt de Homebrew, si no se dio otro).
if [[ "$OS_NAME" == macos && -z "${CMAKE_PREFIX_PATH:-}" ]] && command -v brew >/dev/null 2>&1; then
    extra_cmake+=(-DCMAKE_PREFIX_PATH="$(brew --prefix qt)")
fi
STEP="configurar con CMake"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" "${extra_cmake[@]}"

jobs=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}
echo "${c_bold}Compilando con $jobs hilos...${c_reset}"
STEP="compilar"
cmake --build "$BUILD_DIR" --parallel "$jobs"

if (( DO_TEST )); then
    STEP="las pruebas"
    echo "${c_bold}Ejecutando pruebas...${c_reset}"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if (( DO_DEB )); then
    STEP="crear el paquete .deb"
    echo "${c_bold}Creando el paquete .deb...${c_reset}"
    (cd "$BUILD_DIR" && cpack -G DEB)
    report "Paquete" "$(ls "$BUILD_DIR"/kaint_*.deb)"
    echo "  Instálalo con: sudo apt install ./$(ls "$BUILD_DIR"/kaint_*.deb)"
fi

# --- AppImage ---------------------------------------------------------------
if (( DO_APPIMAGE )); then
    STEP="crear el AppImage"
    echo "${c_bold}Creando el AppImage...${c_reset}"
    arch=$(uname -m)
    tools="$PWD/$BUILD_DIR/appimage-tools"
    mkdir -p "$tools"
    for tool in linuxdeploy linuxdeploy-plugin-qt; do
        file="$tool-$arch.AppImage"
        sum=${APPIMAGE_TOOL_SHA256[$file]:-}
        if [[ -z "$sum" ]]; then
            echo "${c_red}El AppImage solo se puede crear en x86_64 o aarch64 (esto es $arch).${c_reset}" >&2
            exit 1
        fi
        if ! echo "$sum  $tools/$file" | sha256sum -c --status 2>/dev/null; then
            url=$LINUXDEPLOY_URL
            [[ "$tool" == linuxdeploy-plugin-qt ]] && url=$LINUXDEPLOY_QT_URL
            echo "Descargando $file..."
            curl -fL --progress-bar -o "$tools/$file.part" "$url/$file"
            if ! echo "$sum  $tools/$file.part" | sha256sum -c --status; then
                rm -f "$tools/$file.part"
                echo "${c_red}$file no coincide con su SHA-256: no se usa.${c_reset}" >&2
                exit 1
            fi
            mv "$tools/$file.part" "$tools/$file"
        fi
        chmod +x "$tools/$file"
    done

    # El complemento de Qt copia los plugins del Qt con el que se compiló (lo dice qmake).
    qt_dir=$(sed -n 's/^Qt6_DIR:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")
    qt_prefix=$(cd "$qt_dir/../../.." && pwd)
    qmake=""
    for candidate in "$qt_prefix/bin/qmake6" "$qt_prefix/bin/qmake" "$qt_prefix/lib/qt6/bin/qmake" \
                     "$(command -v qmake6 || true)"; do
        if [[ -n "$candidate" && -x "$candidate" ]]; then qmake=$candidate; break; fi
    done
    if [[ -z "$qmake" ]]; then
        echo "${c_red}No encuentro qmake de Qt 6 (paquete qt6-base o qt6-base-dev-tools).${c_reset}" >&2
        exit 1
    fi
    platforms="$("$qmake" -query QT_INSTALL_PLUGINS)/platforms"
    # Un Qt fuera del sistema (CMAKE_PREFIX_PATH): linuxdeploy busca allí sus bibliotecas.
    qt_libs=$("$qmake" -query QT_INSTALL_LIBS)
    # Wayland (Qt 6.10 o más lo tiene en un solo plugin) y offscreen para --screenpreview
    extra_platforms="libqoffscreen.so"
    if [[ -f "$platforms/libqwayland.so" ]]; then
        extra_platforms="libqwayland.so;$extra_platforms"
    elif [[ -f "$platforms/libqwayland-egl.so" ]]; then
        extra_platforms="libqwayland-egl.so;libqwayland-generic.so;$extra_platforms"
    fi

    appdir="$PWD/$BUILD_DIR/AppDir"
    rm -rf "$appdir"
    DESTDIR="$appdir" cmake --install "$BUILD_DIR" >/dev/null
    rm -rf "$appdir/usr/include"
    # Dentro del AppImage, el .desktop llama a kaint sin ruta.
    sed -i 's|^Exec=.*|Exec=kaint %F|' "$appdir/usr/share/applications/$APP_ID.desktop"
    version=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' version.json)
    # (Sin «$( (( DO_AVX2 )) && ... )»: sin AVX2 devuelve 1 y set -e corta aquí sin decir nada.)
    avx2_suffix=""
    (( DO_AVX2 )) && avx2_suffix="-avx2"
    output="Kaint-$version-$arch$avx2_suffix.AppImage"
    rm -f "$BUILD_DIR/$output"
    (cd "$BUILD_DIR" && PATH="$tools:$PATH" LD_LIBRARY_PATH="$qt_libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        QMAKE="$qmake" EXTRA_QT_MODULES="svg;waylandcompositor" \
        EXTRA_PLATFORM_PLUGINS="$extra_platforms" APPIMAGE_EXTRACT_AND_RUN=1 LDAI_OUTPUT="$output" \
        "$tools/linuxdeploy-$arch.AppImage" --appdir "$appdir" --plugin qt --output appimage \
            --desktop-file "$appdir/usr/share/applications/$APP_ID.desktop" \
            --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$APP_ID.png")
    report "AppImage" "$BUILD_DIR/$output"
    echo "  Ejecútalo con: chmod +x $BUILD_DIR/$output && ./$BUILD_DIR/$output"
fi

# --- .tar.gz portable ------------------------------------------------------------
# Lo instalado (bin, lib, share) en una carpeta Kaint/, con Qt dentro en macOS (macdeployqt). En
# Linux y FreeBSD usa el Qt del sistema (como los .tar.gz de otros programas Qt): para llevar Qt
# dentro, mejor el AppImage.
if (( DO_TAR )); then
    STEP="crear el .tar.gz"
    echo "${c_bold}Creando el .tar.gz...${c_reset}"
    arch=$HOST_ARCH
    suffix="${KAINT_TAR_SUFFIX:-}"
    (( DO_AVX2 )) && suffix="-avx2$suffix"
    is_musl && [[ "$suffix" != *-musl* ]] && suffix="$suffix-musl"
    [[ "$OS_NAME" == macos ]] && arch=arm64
    name="Kaint-$VERSION-$OS_NAME-$arch$suffix"
    stage="$PWD/$BUILD_DIR/tar/$name"
    rm -rf "$PWD/$BUILD_DIR/tar"
    mkdir -p "$stage"
    DESTDIR="$PWD/$BUILD_DIR/tar/root" cmake --install "$BUILD_DIR" >/dev/null
    cp -R "$PWD/$BUILD_DIR/tar/root/usr/." "$stage/"
    rm -rf "$stage/include"
    if [[ "$OS_NAME" == macos ]]; then
        app=$(find "$BUILD_DIR" -maxdepth 2 -name 'kaint.app' -o -maxdepth 2 -name 'Kaint.app' | head -n 1)
        if [[ -n "$app" ]] && command -v macdeployqt >/dev/null 2>&1; then
            cp -R "$app" "$stage/Kaint.app"
            macdeployqt "$stage/Kaint.app" >/dev/null
        fi
    fi
    cat > "$stage/README.txt" <<EOF
Kaint $VERSION ($OS_LABEL, $arch$suffix)

Ejecuta bin/kaint (en macOS, Kaint.app). Necesita Qt 6.5 o posterior instalado en el sistema
(en Linux y FreeBSD); el AppImage y el Flatpak lo llevan dentro.
EOF
    (cd "$PWD/$BUILD_DIR/tar" && tar -czf "../$name.tar.gz" "$name")
    report "Paquete" "$BUILD_DIR/$name.tar.gz"
fi

# --- Instalación ------------------------------------------------------------
if (( DO_INSTALL )); then
    STEP="instalar en $PREFIX"
    echo "${c_bold}Instalando en $PREFIX...${c_reset}"
    mkdir -p "$PREFIX" 2>/dev/null || true
    if [[ -w "$PREFIX" ]]; then
        cmake --install "$BUILD_DIR"
    else
        sudo cmake --install "$BUILD_DIR"
    fi
    command -v update-desktop-database >/dev/null && update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
    echo "${c_green}Instalado:${c_reset} $PREFIX/bin/kaint (también en el menú de aplicaciones)"
fi

STEP=""
echo "${c_green}${c_bold}Listo:${c_reset} $BUILD_DIR/kaint"

if (( DO_RUN )); then
    trap - EXIT
    on_exit
    exec "$BUILD_DIR/kaint"
fi

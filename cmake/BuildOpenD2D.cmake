# Puente de OpenD2D (https://github.com/daijro/opend2d) para los complementos de Paint.NET.
#
# Se ejecuta en cada compilación (cmake -P, desde el objetivo «opend2d» de CMakeLists.txt):
#   1. El código: SOURCE (una carpeta, como 2dlocal/) o el repositorio oficial (REPO), clonado en
#      WORK/src la primera vez y actualizado las siguientes. REF elige la versión: "latest" (lo más
#      nuevo de la rama principal), "main" (lo mismo) o una etiqueta publicada (v0.1.0-beta.1...).
#   2. cargo compila sus bibliotecas (d2d1, dwrite y windowscodecs) en modo release.
#   3. Se copian a OUT (junto al ejecutable kaint: build/opend2d/) con un archivo VERSION.
# Sin red, se usa lo que se descargó la vez anterior. Si no hay nada que compilar, Kaint se compila
# igual, sin OpenD2D (salvo con REQUIRED=ON).
#
# Variables: CARGO, GIT, REPO, REF, SOURCE, WORK, OUT, REQUIRED.

cmake_minimum_required(VERSION 3.21)

function(opend2d_skip why)
    if(REQUIRED STREQUAL "ON")
        message(FATAL_ERROR "OpenD2D: ${why}")
    endif()
    message(WARNING "OpenD2D: ${why}. Kaint se compila sin OpenD2D (los complementos de Paint.NET usan mono sin él, o Wine).")
    file(REMOVE_RECURSE "${OUT}")
    return()
endfunction()

if(NOT REF)
    set(REF latest)
endif()

# --- 1. El código ---------------------------------------------------------------
set(label "")
if(SOURCE)
    get_filename_component(src "${SOURCE}" ABSOLUTE)
    if(NOT EXISTS "${src}/Cargo.toml")
        opend2d_skip("${src} no tiene el código de OpenD2D (falta Cargo.toml)")
        return()
    endif()
    set(label "local (${src})")
else()
    if(NOT GIT)
        opend2d_skip("hace falta git para descargar OpenD2D")
        return()
    endif()
    set(src "${WORK}/src")
    if(REF STREQUAL "latest" OR REF STREQUAL "main")
        set(branch "")
    else()
        set(branch "${REF}")
    endif()
    if(NOT EXISTS "${src}/.git")
        file(REMOVE_RECURSE "${src}")
        file(MAKE_DIRECTORY "${WORK}")
        if(branch)
            set(clone_args --depth 1 --branch "${branch}")
        else()
            set(clone_args --depth 1)
        endif()
        message(STATUS "OpenD2D: descargando ${REPO} (${REF})")
        execute_process(COMMAND "${GIT}" clone --quiet ${clone_args} "${REPO}" "${src}" RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            file(REMOVE_RECURSE "${src}")
            opend2d_skip("no se pudo descargar ${REPO} (¿sin conexión?)")
            return()
        endif()
        set(just_cloned TRUE)
        file(TOUCH "${WORK}/fetched-${REF}")
    else()
        # Ya estaba: se trae la versión pedida (o lo último de la rama principal). Como mucho una vez
        # por hora (en cada compilación, con ninja, sería ir a la red en cada cambio de una línea).
        string(TIMESTAMP now "%s")
        set(stamp "${WORK}/fetched-${REF}")
        set(recent FALSE)
        if(EXISTS "${stamp}")
            file(TIMESTAMP "${stamp}" last "%s")
            math(EXPR age "${now} - ${last}")
            if(age LESS 3600)
                set(recent TRUE)
            endif()
        endif()
    endif()
    if(EXISTS "${src}/.git" AND NOT recent AND NOT just_cloned)
        if(branch)
            set(fetch_ref "refs/tags/${branch}:refs/tags/${branch}")
            set(checkout "${branch}")
        else()
            set(fetch_ref HEAD)
            set(checkout FETCH_HEAD)
        endif()
        execute_process(COMMAND "${GIT}" -C "${src}" fetch --quiet --depth 1 origin ${fetch_ref}
                        RESULT_VARIABLE rc ERROR_QUIET)
        if(rc EQUAL 0)
            execute_process(COMMAND "${GIT}" -C "${src}" checkout --quiet --force ${checkout} RESULT_VARIABLE rc)
        endif()
        if(NOT rc EQUAL 0)
            message(WARNING "OpenD2D: no se pudo actualizar (¿sin conexión?); se usa la versión descargada antes")
        else()
            file(TOUCH "${stamp}")
        endif()
    endif()
    execute_process(COMMAND "${GIT}" -C "${src}" rev-parse --short HEAD OUTPUT_VARIABLE commit
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(REF STREQUAL "latest" OR REF STREQUAL "main")
        set(label "main ${commit}")
    else()
        set(label "${REF} (${commit})")
    endif()
endif()

# --- 2. Compilar ----------------------------------------------------------------
if(NOT CARGO)
    opend2d_skip("hace falta cargo (Rust) para compilar OpenD2D")
    return()
endif()
if(EXISTS "${WORK}/target/release/libd2d1.so" OR EXISTS "${WORK}/target/release/libd2d1.dylib")
    message(STATUS "OpenD2D: compilando ${label}")
else()
    message(STATUS "OpenD2D: compilando ${label} (la primera vez tarda unos minutos)")
endif()
# Lo que escribe cargo (sus avisos se repiten en cada compilación) solo se muestra si falla.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${WORK}/target"
            "${CARGO}" build --release --quiet -p d2d-ffi -p dwrite-ffi -p wic-ffi
    WORKING_DIRECTORY "${src}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE cargo_out
    ERROR_VARIABLE cargo_out)
if(NOT rc EQUAL 0)
    string(LENGTH "${cargo_out}" len)
    if(len GREATER 6000)
        math(EXPR start "${len} - 6000")
        string(SUBSTRING "${cargo_out}" ${start} -1 cargo_out)
    endif()
    message("${cargo_out}")
    opend2d_skip("cargo no pudo compilarlo")
    return()
endif()

# --- 3. Junto a Kaint -----------------------------------------------------------
if(APPLE)
    set(ext dylib)
else()
    set(ext so)
endif()
file(MAKE_DIRECTORY "${OUT}")
foreach(lib d2d1 dwrite windowscodecs)
    set(built "${WORK}/target/release/lib${lib}.${ext}")
    if(NOT EXISTS "${built}")
        opend2d_skip("falta lib${lib}.${ext} después de compilar")
        return()
    endif()
    file(COPY_FILE "${built}" "${OUT}/lib${lib}.${ext}" ONLY_IF_DIFFERENT)
endforeach()
foreach(doc LICENSE THIRD-PARTY-NOTICES)
    if(EXISTS "${src}/${doc}")
        file(COPY_FILE "${src}/${doc}" "${OUT}/${doc}" ONLY_IF_DIFFERENT)
    endif()
endforeach()
file(WRITE "${OUT}/VERSION.new" "${label}\n")
file(COPY_FILE "${OUT}/VERSION.new" "${OUT}/VERSION" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}/VERSION.new")
message(STATUS "OpenD2D: listo (${label}) en ${OUT}")

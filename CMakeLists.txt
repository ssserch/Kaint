cmake_minimum_required(VERSION 3.21)

# La versión y el repositorio se configuran en version.json antes de compilar.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" KAINT_VERSION_JSON)
string(JSON KAINT_VERSION GET "${KAINT_VERSION_JSON}" version)
string(JSON KAINT_REPOSITORY GET "${KAINT_VERSION_JSON}" repository)
string(REGEX REPLACE "^https?://github.com/" "" KAINT_GITHUB "${KAINT_REPOSITORY}")
string(REGEX REPLACE "/$" "" KAINT_GITHUB "${KAINT_GITHUB}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.json)

# Rich Presence de Discord: el ID de la aplicación se configura en discord.json
# ("PON___AQUI" = sin configurar). El token no se lee: no hace falta.
set(KAINT_DISCORD_CLIENT_ID "")
set(KAINT_DISCORD_LARGE_IMAGE "kaint")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/discord.json")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/discord.json" KAINT_DISCORD_JSON)
    string(JSON KAINT_DISCORD_CLIENT_ID ERROR_VARIABLE discord_error GET "${KAINT_DISCORD_JSON}" client_id)
    string(JSON KAINT_DISCORD_LARGE_IMAGE ERROR_VARIABLE discord_error GET "${KAINT_DISCORD_JSON}" large_image)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS discord.json)
endif()
if(NOT KAINT_DISCORD_CLIENT_ID MATCHES "^[0-9]+$")
    message(STATUS "Kaint: Rich Presence de Discord sin configurar (pon el ID de la aplicación en discord.json)")
    set(KAINT_DISCORD_CLIENT_ID "")
endif()

project(Kaint VERSION ${KAINT_VERSION} LANGUAGES CXX C
        DESCRIPTION "Editor de imágenes al estilo Paint.NET para KDE Plasma")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilación" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Network LinguistTools)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

qt_standard_project_setup()
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Núcleo: documento, capas, selección, historial, efectos y formatos.
# Solo depende de QtGui (QImage/QPainter), nunca de QtWidgets.
# ---------------------------------------------------------------------------
add_library(kaintcore STATIC
    src/core/BlendMode.cpp
    src/core/Commands.cpp
    src/core/CustomShapes.cpp
    src/core/Document.cpp
    src/core/FillPattern.cpp
    src/core/FloodFill.cpp
    src/core/Gif.cpp
    src/core/ImageIO.cpp
    src/core/LayerEdit.cpp
    src/core/Ora.cpp
    src/core/Palette.cpp
    src/core/Pdn.cpp
    src/core/Raster.cpp
    src/core/Resample.cpp
    src/core/Selection.cpp
    src/core/Stroke.cpp
    src/core/effects/Effect.cpp
    src/core/effects/Adjustments.cpp
    src/core/effects/Blurs.cpp
    src/core/effects/ColorEffects.cpp
    src/core/effects/Distort.cpp
    src/core/effects/LayerEffects.cpp
    src/core/effects/NoiseEffects.cpp
    src/core/effects/Photo.cpp
    src/core/effects/Render.cpp
    src/core/effects/RenderBlend.cpp
    src/core/effects/Stylize.cpp
    src/core/effects/Artistic.cpp
    src/core/plugins/PdnBridge.cpp
    src/core/plugins/PluginEffects.cpp
    src/core/plugins/PluginManager.cpp
)
target_include_directories(kaintcore PUBLIC src)
target_link_libraries(kaintcore PUBLIC Qt6::Gui ZLIB::ZLIB Threads::Threads)

# ---------------------------------------------------------------------------
# Aplicación (interfaz Qt Widgets)
# ---------------------------------------------------------------------------
add_library(kaintui STATIC
    src/app/AppState.cpp
    src/app/Canvas.cpp
    src/app/DiscordPresence.cpp
    src/app/Icons.cpp
    src/app/MainWindow.cpp
    src/app/Ruler.cpp
    src/app/Theme.cpp
    src/app/ToolOptionsBar.cpp
    src/app/Workspace.cpp
    src/app/widgets/BarSlider.cpp
    src/app/widgets/ChoiceButton.cpp
    src/app/widgets/ColorWidgets.cpp
    src/app/widgets/CurvesEditor.cpp
    src/app/widgets/EffectControls.cpp
    src/app/widgets/FlowLayout.cpp
    src/app/widgets/FontChooser.cpp
    src/app/panels/ColorsPanel.cpp
    src/app/panels/HistoryPanel.cpp
    src/app/panels/LayersPanel.cpp
    src/app/panels/ToolsPanel.cpp
    src/app/dialogs/DialogExec.cpp
    src/app/dialogs/EffectDialog.cpp
    src/app/dialogs/ImageDialogs.cpp
    src/app/dialogs/SaveConfigDialog.cpp
    src/app/dialogs/SettingsDialog.cpp
    src/app/dialogs/TaskDialog.cpp
    src/app/tools/Handles.cpp
    src/app/tools/Tool.cpp
    src/app/tools/TransformBox.cpp
    src/app/tools/ToolManager.cpp
    src/app/tools/SelectionTools.cpp
    src/app/tools/MoveTools.cpp
    src/app/tools/ViewTools.cpp
    src/app/tools/PaintTools.cpp
    src/app/tools/FillTools.cpp
    src/app/tools/ShapeTools.cpp
    src/app/tools/TextTool.cpp
)
target_link_libraries(kaintui PUBLIC kaintcore Qt6::Widgets Qt6::PrintSupport Qt6::Network)
target_compile_definitions(kaintui PUBLIC KAINT_VERSION="${PROJECT_VERSION}"
                           KAINT_REPOSITORY="${KAINT_REPOSITORY}" KAINT_GITHUB="${KAINT_GITHUB}"
                           KAINT_DISCORD_CLIENT_ID="${KAINT_DISCORD_CLIENT_ID}"
                           KAINT_DISCORD_LARGE_IMAGE="${KAINT_DISCORD_LARGE_IMAGE}")

# ---------------------------------------------------------------------------
# Iconos: los de img/ si existen (ver README) y, si no, los de emergencia de
# data/. Se incluyen como recursos (:/kaint/...) y se instalan en hicolor.
# ---------------------------------------------------------------------------
set(KAINT_ICON_RESOURCES)
set(KAINT_ICON_PNG_SIZES)
foreach(size 16 22 24 32 48 64 128 256 512)
    set(png "${CMAKE_CURRENT_SOURCE_DIR}/img/kaint-${size}.png")
    if(EXISTS "${png}")
        list(APPEND KAINT_ICON_PNG_SIZES ${size})
        list(APPEND KAINT_ICON_RESOURCES "${png}")
        set_source_files_properties("${png}" PROPERTIES QT_RESOURCE_ALIAS "icons/kaint-${size}.png")
    endif()
endforeach()
# SVG: el de img/ o, si no hay ningún icono en img/, el de emergencia de data/.
set(KAINT_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/img/kaint.svg")
if(NOT EXISTS "${KAINT_ICON_SVG}")
    if(KAINT_ICON_PNG_SIZES)
        set(KAINT_ICON_SVG "")
    else()
        message(STATUS "Kaint: img/ no tiene iconos; se usa el icono de emergencia data/kaint.svg")
        set(KAINT_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/data/kaint.svg")
    endif()
endif()
if(KAINT_ICON_SVG)
    set_source_files_properties("${KAINT_ICON_SVG}" PROPERTIES QT_RESOURCE_ALIAS "icons/kaint.svg")
    list(APPEND KAINT_ICON_RESOURCES "${KAINT_ICON_SVG}")
endif()
file(GLOB KAINT_TOOL_ICONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/img/tools/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/img/tools/*.png")
foreach(icon ${KAINT_TOOL_ICONS})
    get_filename_component(name "${icon}" NAME)
    set_source_files_properties("${icon}" PROPERTIES QT_RESOURCE_ALIAS "tools/${name}")
    list(APPEND KAINT_ICON_RESOURCES "${icon}")
endforeach()
list(LENGTH KAINT_TOOL_ICONS KAINT_TOOL_ICON_COUNT)
message(STATUS "Kaint: iconos de la aplicación en PNG: ${KAINT_ICON_PNG_SIZES}; iconos de herramientas: ${KAINT_TOOL_ICON_COUNT}")
qt_add_resources(kaintui kaint_icons PREFIX "/kaint" FILES ${KAINT_ICON_RESOURCES})


qt_add_executable(kaint src/app/main.cpp)
target_link_libraries(kaint PRIVATE kaintui)

# Las cabeceras también tienen textos traducibles: lupdate tiene que verlas.
file(GLOB_RECURSE KAINT_CORE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h")
file(GLOB_RECURSE KAINT_UI_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.h")
target_sources(kaintcore PRIVATE ${KAINT_CORE_HEADERS})
target_sources(kaintui PRIVATE ${KAINT_UI_HEADERS})

# Traducciones: el código está en español y translations/ tiene las demás
# (el inglés es el idioma predeterminado). Se compilan dentro del programa en
# :/i18n. Para actualizar los .ts tras cambiar textos: cmake --build build -t update_translations
file(GLOB KAINT_TS_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.ts")
qt_add_translations(TARGETS kaintui SOURCE_TARGETS kaintcore kaintui kaint TS_FILES ${KAINT_TS_FILES}
                    RESOURCE_PREFIX "/i18n" LUPDATE_OPTIONS -no-obsolete -locations none)

# ---------------------------------------------------------------------------
# Capa de compatibilidad con los complementos de Paint.NET (opcional).
# Con mono (mcs) se compilan las bibliotecas PaintDotNet.*.dll (código de
# Paint.NET 3.36, licencia MIT, más lo que añadió Paint.NET 4) y el proceso
# auxiliar kaint-pdn-host.exe. Sin mono, Kaint funciona igual pero no carga
# complementos .dll.
# ---------------------------------------------------------------------------
option(KAINT_PDN_BRIDGE "Compilar la capa de compatibilidad con Paint.NET (necesita mono)" ON)
find_program(KAINT_MCS mcs)
if(KAINT_PDN_BRIDGE AND KAINT_MCS)
    set(PDN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/pdnbridge")
    set(PDN_OUT "${CMAKE_BINARY_DIR}/pdnbridge")
    file(MAKE_DIRECTORY "${PDN_OUT}")
    # El código de Paint.NET es de terceros: sin avisos (-warn:0).
    set(PDN_MCS_FLAGS -target:library -unsafe -optimize -warn:0 -r:System.Drawing -r:System.Windows.Forms)
    set(PDN_REFS)
    set(PDN_OUTPUTS)
    foreach(part Base Core Data Effects)
        file(GLOB_RECURSE PDN_${part}_SOURCES CONFIGURE_DEPENDS "${PDN_DIR}/${part}/*.cs")
        set(dll "${PDN_OUT}/PaintDotNet.${part}.dll")
        add_custom_command(OUTPUT "${dll}"
            COMMAND ${KAINT_MCS} ${PDN_MCS_FLAGS} ${PDN_REFS} -out:${dll} ${PDN_${part}_SOURCES}
            DEPENDS ${PDN_${part}_SOURCES} ${PDN_OUTPUTS}
            COMMENT "Compilando PaintDotNet.${part}.dll (mono)" VERBATIM)
        list(APPEND PDN_REFS -r:${dll})
        list(APPEND PDN_OUTPUTS "${dll}")
    endforeach()
    # Kaint.Runtime.dll: fachada para los complementos de .NET moderno (ver
    # src/pdnbridge/Compat/GenerateRuntimeFacade.cs); se genera con el mono instalado.
    set(PDN_FACADE_GEN "${PDN_OUT}/GenerateRuntimeFacade.exe")
    set(PDN_FACADE_CS "${PDN_OUT}/KaintRuntimeFacade.cs")
    set(PDN_FACADE "${PDN_OUT}/Kaint.Runtime.dll")
    find_program(KAINT_MONO mono)
    add_custom_command(OUTPUT "${PDN_FACADE}"
        COMMAND ${KAINT_MCS} -target:exe -warn:0 -r:System.Core -out:${PDN_FACADE_GEN}
                ${PDN_DIR}/Compat/GenerateRuntimeFacade.cs
        COMMAND ${KAINT_MONO} ${PDN_FACADE_GEN} ${PDN_FACADE_CS}
        COMMAND ${KAINT_MCS} -target:library -warn:0 -r:System -r:System.Core -r:System.Configuration
                -out:${PDN_FACADE} ${PDN_FACADE_CS}
        DEPENDS "${PDN_DIR}/Compat/GenerateRuntimeFacade.cs"
        COMMENT "Generando Kaint.Runtime.dll (mono)" VERBATIM)
    list(APPEND PDN_OUTPUTS "${PDN_FACADE}")
    file(GLOB PDN_HOST_SOURCES CONFIGURE_DEPENDS "${PDN_DIR}/Host/*.cs")
    set(PDN_HOST "${PDN_OUT}/kaint-pdn-host.exe")
    add_custom_command(OUTPUT "${PDN_HOST}"
        COMMAND ${KAINT_MCS} -target:exe -unsafe -optimize -warn:4 -warnaserror+ -r:System.Drawing
                -r:System.Windows.Forms -r:System.Core ${PDN_REFS} -out:${PDN_HOST} ${PDN_HOST_SOURCES}
        DEPENDS ${PDN_HOST_SOURCES} ${PDN_OUTPUTS}
        COMMENT "Compilando kaint-pdn-host.exe (mono)" VERBATIM)
    add_custom_target(pdnbridge ALL DEPENDS ${PDN_OUTPUTS} "${PDN_HOST}")
    install(FILES ${PDN_OUTPUTS} "${PDN_HOST}" "${PDN_DIR}/License-PaintDotNet-3.36.txt"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/kaint/pdnbridge)
    message(STATUS "Kaint: capa de compatibilidad con Paint.NET activada (${KAINT_MCS})")
else()
    message(STATUS "Kaint: sin mono (mcs); no se compila la capa de compatibilidad con Paint.NET")
endif()

install(FILES src/core/plugins/KaintPlugin.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kaint)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kaintcore PRIVATE -Wall -Wextra)
    target_compile_options(kaintui PRIVATE -Wall -Wextra)
    target_compile_options(kaint PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Pruebas del núcleo (sin interfaz)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    # Complemento nativo de ejemplo (lo cargan las pruebas).
    add_library(kaint_example_plugin MODULE examples/plugin/ejemplo.c)
    target_include_directories(kaint_example_plugin PRIVATE src/core/plugins)
    set_target_properties(kaint_example_plugin PROPERTIES PREFIX "" OUTPUT_NAME "kaint-ejemplo"
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-plugins" C_VISIBILITY_PRESET hidden
                          AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_options(kaint_example_plugin PRIVATE -Wall -Wextra)

    add_executable(kaint_core_tests tests/core_tests.cpp)
    target_link_libraries(kaint_core_tests PRIVATE kaintcore)
    target_compile_definitions(kaint_core_tests PRIVATE
        KAINT_TEST_PLUGIN="$<TARGET_FILE:kaint_example_plugin>"
        KAINT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(kaint_core_tests kaint_example_plugin)
    if(TARGET pdnbridge)
        add_dependencies(kaint_core_tests pdnbridge)
    endif()
    add_test(NAME core COMMAND kaint_core_tests)

    # Prueba de humo de la interfaz (sin pantalla, plataforma "offscreen").
    add_executable(kaint_ui_smoke tests/ui_smoke.cpp)
    target_link_libraries(kaint_ui_smoke PRIVATE kaintui)
    add_test(NAME ui_smoke COMMAND kaint_ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# ---------------------------------------------------------------------------
# Instalación: binario, lanzador .desktop, icono y tipo MIME
# ---------------------------------------------------------------------------
install(TARGETS kaint RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(data/io.github.kaint.Kaint.desktop.in io.github.kaint.Kaint.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.kaint.Kaint.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
if(KAINT_ICON_SVG)
    install(FILES "${KAINT_ICON_SVG}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME io.github.kaint.Kaint.svg)
endif()
foreach(size ${KAINT_ICON_PNG_SIZES})
    install(FILES img/kaint-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME io.github.kaint.Kaint.png)
endforeach()

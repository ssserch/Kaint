cmake_minimum_required(VERSION 3.21)

# Versión y repositorio (version.json)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" KAINT_VERSION_JSON)
string(JSON KAINT_VERSION GET "${KAINT_VERSION_JSON}" version)
string(JSON KAINT_REPOSITORY GET "${KAINT_VERSION_JSON}" repository)
string(REGEX REPLACE "^https?://github.com/" "" KAINT_GITHUB "${KAINT_REPOSITORY}")
string(REGEX REPLACE "/$" "" KAINT_GITHUB "${KAINT_GITHUB}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.json)

# ID de la aplicación (archivo .desktop, icono, metainfo y Flathub)
set(KAINT_APP_ID "io.github.ssserch.Kaint")

# Rich Presence de Discord (discord.json)
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
find_package(Threads REQUIRED)

# zlib (del sistema o descargada)
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "Kaint: no hay zlib en el sistema; se descarga zlib 1.3.1")
    include(FetchContent)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON)
    FetchContent_MakeAvailable(zlib)
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL ON)
    target_include_directories(zlibstatic INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# Windows y MSVC
if(WIN32)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus /permissive-)
endif()

qt_standard_project_setup()
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Núcleo (solo QtGui)
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
    src/core/PngWriter.cpp
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
    src/app/PdnIcons.cpp
    src/app/Recovery.cpp
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
    src/app/widgets/WaveLabel.cpp
    src/app/panels/ColorsPanel.cpp
    src/app/panels/HistoryPanel.cpp
    src/app/panels/LayersPanel.cpp
    src/app/panels/ToolsPanel.cpp
    src/app/dialogs/AboutDialog.cpp
    src/app/dialogs/DialogExec.cpp
    src/app/dialogs/EffectDialog.cpp
    src/app/dialogs/ImageDialogs.cpp
    src/app/dialogs/SaveConfigDialog.cpp
    src/app/dialogs/SettingsDialog.cpp
    src/app/dialogs/TaskDialog.cpp
    src/app/dialogs/ThemeDialog.cpp
    src/app/tools/Handles.cpp
    src/app/tools/Tool.cpp
    src/app/tools/TransformBox.cpp
    src/app/tools/ToolManager.cpp
    src/app/tools/SelectionTools.cpp
    src/app/tools/MoveTools.cpp
    src/app/tools/ViewTools.cpp
    src/app/tools/PaintTools.cpp
    src/app/tools/EditableSteps.cpp
    src/app/tools/FillTools.cpp
    src/app/tools/ShapeTools.cpp
    src/app/tools/TextTool.cpp
)
target_link_libraries(kaintui PUBLIC kaintcore Qt6::Widgets Qt6::PrintSupport Qt6::Network)
# Modo oscuro del sistema (portal de freedesktop)
find_package(Qt6 QUIET COMPONENTS DBus)
if(TARGET Qt6::DBus)
    target_link_libraries(kaintui PUBLIC Qt6::DBus)
    target_compile_definitions(kaintui PRIVATE KAINT_HAVE_DBUS)
endif()
target_compile_definitions(kaintui PUBLIC KAINT_VERSION="${PROJECT_VERSION}" KAINT_APP_ID="${KAINT_APP_ID}"
                           KAINT_REPOSITORY="${KAINT_REPOSITORY}" KAINT_GITHUB="${KAINT_GITHUB}"
                           KAINT_DISCORD_CLIENT_ID="${KAINT_DISCORD_CLIENT_ID}"
                           KAINT_DISCORD_LARGE_IMAGE="${KAINT_DISCORD_LARGE_IMAGE}")
# Búsqueda de versiones nuevas en GitHub (sin ella, Kaint lo deja en manos de Flathub o del
# gestor de paquetes y no usa la red)
option(KAINT_UPDATE_CHECK "Buscar versiones nuevas de Kaint en GitHub" ON)
if(KAINT_UPDATE_CHECK)
    target_compile_definitions(kaintui PUBLIC KAINT_UPDATE_CHECK)
endif()

# ---------------------------------------------------------------------------
# Iconos (img/ o, si no hay, data/)
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

# Iconos de Breeze incluidos (data/icons)
file(GLOB_RECURSE KAINT_THEME_ICONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/kaint-breeze*/*")
qt_add_resources(kaintui kaint_icon_themes PREFIX "/kaint/icon-themes" BASE data/icons FILES ${KAINT_THEME_ICONS})


qt_add_executable(kaint src/app/main.cpp)
target_link_libraries(kaint PRIVATE kaintui)
# Ejecutable de Windows
if(WIN32)
    set_target_properties(kaint PROPERTIES WIN32_EXECUTABLE ON)
    configure_file(data/kaint.rc.in kaint.rc @ONLY)
    target_sources(kaint PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/kaint.rc")
    find_package(Qt6 REQUIRED COMPONENTS Svg)
    target_link_libraries(kaint PRIVATE Qt6::Svg)
    target_link_libraries(kaintui PRIVATE dwmapi)
endif()

# Cabeceras (para lupdate)
file(GLOB_RECURSE KAINT_CORE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h")
file(GLOB_RECURSE KAINT_UI_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.h")
target_sources(kaintcore PRIVATE ${KAINT_CORE_HEADERS})
target_sources(kaintui PRIVATE ${KAINT_UI_HEADERS})

# Traducciones (cmake --build build -t update_translations)
file(GLOB KAINT_TS_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.ts")
qt_add_translations(TARGETS kaintui SOURCE_TARGETS kaintcore kaintui kaint TS_FILES ${KAINT_TS_FILES}
                    RESOURCE_PREFIX "/i18n" LUPDATE_OPTIONS -no-obsolete -locations none)

# ---------------------------------------------------------------------------
# Complementos de Paint.NET (opcional): mcs y mono en Linux, csc en Windows
# ---------------------------------------------------------------------------
option(KAINT_PDN_BRIDGE "Compilar la capa de compatibilidad con Paint.NET" ON)
if(WIN32)
    find_program(KAINT_MCS csc PATHS "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319"
                 "$ENV{WINDIR}/Microsoft.NET/Framework/v4.0.30319" NO_DEFAULT_PATH)
    set(PDN_RUN)
    set(PDN_STRICT -warn:4)
else()
    find_program(KAINT_MCS mcs)
    find_program(KAINT_MONO mono)
    set(PDN_RUN ${KAINT_MONO})
    set(PDN_STRICT -warn:4 -warnaserror+)
endif()

# Argumentos del compilador de C# en un .rsp con rutas nativas (csc no entiende "C:/...")
function(pdn_rsp var name output)
    cmake_parse_arguments(ARG "" "" "FLAGS;REFS;SOURCES" ${ARGN})
    set(text "")
    foreach(flag ${ARG_FLAGS})
        string(APPEND text "${flag}\n")
    endforeach()
    foreach(path ${ARG_REFS})
        cmake_path(NATIVE_PATH path native)
        string(APPEND text "\"-r:${native}\"\n")
    endforeach()
    cmake_path(NATIVE_PATH output native)
    string(APPEND text "\"-out:${native}\"\n")
    foreach(path ${ARG_SOURCES})
        cmake_path(NATIVE_PATH path native)
        string(APPEND text "\"${native}\"\n")
    endforeach()
    set(rsp "${PDN_OUT}/${name}.rsp")
    file(CONFIGURE OUTPUT "${rsp}" CONTENT "${text}" @ONLY)
    cmake_path(NATIVE_PATH rsp native)
    set(${var} "@${native}" PARENT_SCOPE)
endfunction()

if(KAINT_PDN_BRIDGE AND KAINT_MCS AND (WIN32 OR KAINT_MONO))
    set(PDN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/pdnbridge")
    set(PDN_OUT "${CMAKE_BINARY_DIR}/pdnbridge")
    file(MAKE_DIRECTORY "${PDN_OUT}")
    set(PDN_REFS)
    set(PDN_OUTPUTS)
    foreach(part Base Core Data Effects)
        file(GLOB_RECURSE PDN_${part}_SOURCES CONFIGURE_DEPENDS "${PDN_DIR}/${part}/*.cs")
        set(dll "${PDN_OUT}/PaintDotNet.${part}.dll")
        pdn_rsp(rsp PaintDotNet.${part} "${dll}"
                FLAGS -target:library -unsafe -optimize -warn:0 -r:System.Drawing.dll -r:System.Windows.Forms.dll
                REFS ${PDN_REFS} SOURCES ${PDN_${part}_SOURCES})
        add_custom_command(OUTPUT "${dll}"
            COMMAND ${KAINT_MCS} -nologo ${rsp}
            DEPENDS ${PDN_${part}_SOURCES} ${PDN_OUTPUTS}
            COMMENT "Compilando PaintDotNet.${part}.dll" VERBATIM)
        list(APPEND PDN_REFS "${dll}")
        list(APPEND PDN_OUTPUTS "${dll}")
    endforeach()
    set(PDN_FACADE_GEN "${PDN_OUT}/GenerateRuntimeFacade.exe")
    set(PDN_FACADE_CS "${PDN_OUT}/KaintRuntimeFacade.cs")
    set(PDN_FACADE "${PDN_OUT}/Kaint.Runtime.dll")
    pdn_rsp(gen_rsp GenerateRuntimeFacade "${PDN_FACADE_GEN}" FLAGS -target:exe -warn:0 -r:System.Core.dll
            SOURCES "${PDN_DIR}/Compat/GenerateRuntimeFacade.cs")
    pdn_rsp(facade_rsp Kaint.Runtime "${PDN_FACADE}"
            FLAGS -target:library -warn:0 -r:System.dll -r:System.Core.dll -r:System.Configuration.dll
            SOURCES "${PDN_FACADE_CS}")
    cmake_path(NATIVE_PATH PDN_FACADE_CS facade_cs)
    add_custom_command(OUTPUT "${PDN_FACADE}"
        COMMAND ${KAINT_MCS} -nologo ${gen_rsp}
        COMMAND ${PDN_RUN} ${PDN_FACADE_GEN} ${facade_cs}
        COMMAND ${KAINT_MCS} -nologo ${facade_rsp}
        DEPENDS "${PDN_DIR}/Compat/GenerateRuntimeFacade.cs"
        COMMENT "Generando Kaint.Runtime.dll" VERBATIM)
    list(APPEND PDN_OUTPUTS "${PDN_FACADE}")
    file(GLOB PDN_HOST_SOURCES CONFIGURE_DEPENDS "${PDN_DIR}/Host/*.cs")
    set(PDN_HOST "${PDN_OUT}/kaint-pdn-host.exe")
    pdn_rsp(rsp kaint-pdn-host "${PDN_HOST}"
            FLAGS -target:exe -unsafe -optimize ${PDN_STRICT} -r:System.Drawing.dll -r:System.Windows.Forms.dll
                  -r:System.Core.dll
            REFS ${PDN_REFS} SOURCES ${PDN_HOST_SOURCES})
    add_custom_command(OUTPUT "${PDN_HOST}"
        COMMAND ${KAINT_MCS} -nologo ${rsp}
        DEPENDS ${PDN_HOST_SOURCES} ${PDN_OUTPUTS}
        COMMENT "Compilando kaint-pdn-host.exe" VERBATIM)
    add_custom_target(pdnbridge ALL DEPENDS ${PDN_OUTPUTS} "${PDN_HOST}")
    install(FILES ${PDN_OUTPUTS} "${PDN_HOST}" "${PDN_DIR}/License-PaintDotNet-3.36.txt"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/kaint/pdnbridge)
    message(STATUS "Kaint: capa de compatibilidad con Paint.NET activada (${KAINT_MCS})")
else()
    message(STATUS "Kaint: sin compilador de C#; no se compila la capa de compatibilidad con Paint.NET")
endif()

install(FILES src/core/plugins/KaintPlugin.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kaint)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(KAINT_WARNINGS -Wall -Wextra)
elseif(MSVC)
    set(KAINT_WARNINGS /W3)
endif()
target_compile_options(kaintcore PRIVATE ${KAINT_WARNINGS})
target_compile_options(kaintui PRIVATE ${KAINT_WARNINGS})
target_compile_options(kaint PRIVATE ${KAINT_WARNINGS})

# ---------------------------------------------------------------------------
# Pruebas del núcleo (sin interfaz)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    # Complementos de prueba
    add_library(kaint_example_plugin MODULE examples/plugin/ejemplo.c)
    target_include_directories(kaint_example_plugin PRIVATE src/core/plugins)
    set_target_properties(kaint_example_plugin PROPERTIES PREFIX "" OUTPUT_NAME "kaint-ejemplo"
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-plugins" C_VISIBILITY_PRESET hidden
                          AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_options(kaint_example_plugin PRIVATE ${KAINT_WARNINGS})

    add_executable(kaint_core_tests tests/core_tests.cpp)
    target_link_libraries(kaint_core_tests PRIVATE kaintcore)
    target_compile_definitions(kaint_core_tests PRIVATE
        KAINT_TEST_PLUGIN="$<TARGET_FILE:kaint_example_plugin>"
        KAINT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        KAINT_APP_ID="${KAINT_APP_ID}" KAINT_VERSION="${PROJECT_VERSION}")
    add_dependencies(kaint_core_tests kaint_example_plugin)
    if(TARGET pdnbridge)
        set(PDN_TEST_PLUGIN "${CMAKE_BINARY_DIR}/test-plugins/KaintTestEffect.dll")
        pdn_rsp(rsp KaintTestEffect "${PDN_TEST_PLUGIN}" FLAGS -target:library ${PDN_STRICT} -r:System.Drawing.dll
                REFS ${PDN_REFS} SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/pdnplugin/KaintTestEffect.cs")
        add_custom_command(OUTPUT "${PDN_TEST_PLUGIN}"
            COMMAND ${KAINT_MCS} -nologo ${rsp}
            DEPENDS tests/pdnplugin/KaintTestEffect.cs ${PDN_OUTPUTS}
            COMMENT "Compilando KaintTestEffect.dll" VERBATIM)
        add_custom_target(kaint_test_pdn_plugin DEPENDS "${PDN_TEST_PLUGIN}")
        add_dependencies(kaint_core_tests pdnbridge kaint_test_pdn_plugin)
        target_compile_definitions(kaint_core_tests PRIVATE KAINT_TEST_PDN_PLUGIN="${PDN_TEST_PLUGIN}")
    endif()
    add_test(NAME core COMMAND kaint_core_tests)

    # Prueba de la interfaz (sin pantalla)
    add_executable(kaint_ui_smoke tests/ui_smoke.cpp)
    target_link_libraries(kaint_ui_smoke PRIVATE kaintui)
    add_test(NAME ui_smoke COMMAND kaint_ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Ficha de la aplicación y archivo .desktop (si están las herramientas de freedesktop)
    find_program(KAINT_APPSTREAMCLI appstreamcli)
    if(KAINT_APPSTREAMCLI)
        add_test(NAME metainfo COMMAND ${KAINT_APPSTREAMCLI} validate --no-net
                 "${CMAKE_CURRENT_SOURCE_DIR}/data/${KAINT_APP_ID}.metainfo.xml")
    endif()
    find_program(KAINT_DESKTOP_FILE_VALIDATE desktop-file-validate)
    if(KAINT_DESKTOP_FILE_VALIDATE AND UNIX AND NOT APPLE)
        add_test(NAME desktop_file COMMAND ${KAINT_DESKTOP_FILE_VALIDATE} "${CMAKE_CURRENT_BINARY_DIR}/${KAINT_APP_ID}.desktop")
    endif()
endif()

# ---------------------------------------------------------------------------
# Instalación
# ---------------------------------------------------------------------------
install(TARGETS kaint RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(WIN32)
    qt_generate_deploy_app_script(TARGET kaint OUTPUT_SCRIPT KAINT_DEPLOY_SCRIPT NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${KAINT_DEPLOY_SCRIPT}")
endif()
if(UNIX AND NOT APPLE)
    configure_file(data/${KAINT_APP_ID}.desktop.in ${KAINT_APP_ID}.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${KAINT_APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # Ficha de la aplicación para Flathub, Discover y GNOME Software
    install(FILES data/${KAINT_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    if(KAINT_ICON_SVG)
        install(FILES "${KAINT_ICON_SVG}"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
                RENAME ${KAINT_APP_ID}.svg)
    endif()
    foreach(size ${KAINT_ICON_PNG_SIZES})
        install(FILES img/kaint-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME ${KAINT_APP_ID}.png)
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Paquete .deb (./build.sh --deb)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "kaint")
set(CPACK_PACKAGE_VENDOR "SSSerch")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Paint.NET-style image editor for Linux")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Kaint is a recreation of Paint.NET for Linux, built with C++ and Qt 6.\n\
Layers with blend modes, selections, editable tools, adjustments and\n\
effects with live preview, Paint.NET plugins and OpenRaster projects.")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "SSSerch")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${KAINT_REPOSITORY}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-svg-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "breeze-icon-theme, qt6-wayland, qt6-image-formats-plugins, kimageformat6-plugins, qt6-translations-l10n")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "mono-runtime, libmono-system-windows-forms4.0-cil, libgdiplus")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)

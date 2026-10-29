; Instalador de Kaint para Windows 10 y 11 (NSIS 3). Lo crea "build.bat --setup":
;
;   makensis /DVERSION=1.1.0 /DSOURCE=<carpeta con bin\kaint.exe> /DOUTFILE=<instalador.exe>
;            /DROOT=<carpeta del código> kaint.nsi
;
; Se instala para el usuario actual (sin pedir permisos de administrador) en
; %LOCALAPPDATA%\Programs\Kaint, con acceso en el menú Inicio, y aparece en "Aplicaciones
; instaladas" con su desinstalador (Uninstall.exe) como cualquier otra aplicación.

Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"

!macro Require NAME
  !ifndef ${NAME}
    !error "Falta /D${NAME} (usa build.bat --setup)"
  !endif
!macroend
!insertmacro Require VERSION
!insertmacro Require SOURCE
!insertmacro Require OUTFILE
!insertmacro Require ROOT

!define APP "Kaint"
!define PUBLISHER "SSSerch"
!define WEBSITE "https://github.com/ssserch/Kaint"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Kaint"
!define SETTINGS_KEY "Software\Kaint"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
BrandingText "${APP} ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName" "${APP}"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=0 "FileDescription" "${APP} Setup"
VIAddVersionKey /LANG=0 "LegalCopyright" "GPL-3.0"

; --- Páginas ------------------------------------------------------------------
!define MUI_ICON "${ROOT}\data\kaint.ico"
!define MUI_UNICON "${ROOT}\data\kaint.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\kaint.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${ROOT}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES

; El idioma es el de Windows (español o, si no, inglés), sin preguntarlo.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

LangString SecApp ${LANG_ENGLISH} "Kaint (required)"
LangString SecApp ${LANG_SPANISH} "Kaint (necesario)"
LangString SecDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString SecDesktop ${LANG_SPANISH} "Acceso directo en el escritorio"
LangString SecUnApp ${LANG_ENGLISH} "Kaint"
LangString SecUnApp ${LANG_SPANISH} "Kaint"
LangString SecUnData ${LANG_ENGLISH} "Also delete my settings, palettes, plugins, icon packs, fonts and recovery copies"
LangString SecUnData ${LANG_SPANISH} "Borrar también mi configuración, paletas, complementos, paquetes de iconos, fuentes y copias de recuperación"
LangString Running ${LANG_ENGLISH} "Kaint is running. Close it and click OK to continue."
LangString Running ${LANG_SPANISH} "Kaint está abierto. Ciérralo y pulsa Aceptar para continuar."

; --- Instalación --------------------------------------------------------------
Function .onInit
    ; Mismo sitio que la instalación anterior (también la de build.bat --install).
    ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
    StrCmp $0 "" +2
    StrCpy $INSTDIR $0
FunctionEnd

; Kaint no puede estar abierto mientras se reemplazan o se borran sus archivos.
!macro WaitForKaint UN
Function ${UN}WaitForKaint
    IfFileExists "$INSTDIR\bin\kaint.exe" 0 done
    retry:
    ClearErrors
    FileOpen $0 "$INSTDIR\bin\kaint.exe" a
    IfErrors 0 free
    MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "$(Running)" IDOK retry
    Abort
    free:
    FileClose $0
    done:
FunctionEnd
!macroend
!insertmacro WaitForKaint ""
!insertmacro WaitForKaint "un."

Section "$(SecApp)" SecApp
    SectionIn RO
    Call WaitForKaint
    ; Las carpetas de una versión anterior se reemplazan enteras (no deben quedar archivos de
    ; otra versión de Qt).
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\share"
    RMDir /r "$INSTDIR\include"
    RMDir /r "$INSTDIR\plugins"
    RMDir /r "$INSTDIR\translations"
    Delete "$INSTDIR\build.bat"
    SetOutPath "$INSTDIR"
    File /r "${SOURCE}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    SetOutPath "$INSTDIR\bin"
    CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\bin\kaint.exe" "" "$INSTDIR\bin\kaint.exe" 0

    ; Entrada de "Aplicaciones instaladas"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\bin\kaint.exe,0"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${WEBSITE}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink" "${WEBSITE}/issues"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

Section /o "$(SecDesktop)" SecDesktop
    SetOutPath "$INSTDIR\bin"
    CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\bin\kaint.exe" "" "$INSTDIR\bin\kaint.exe" 0
SectionEnd

; --- Desinstalación (en el desinstalador, $INSTDIR es su propia carpeta) -------
Section "un.$(SecUnApp)" SecUnApp
    SectionIn RO
    Call un.WaitForKaint
    Delete "$SMPROGRAMS\${APP}.lnk"
    Delete "$DESKTOP\${APP}.lnk"
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\share"
    RMDir /r "$INSTDIR\include"
    RMDir /r "$INSTDIR\plugins"
    RMDir /r "$INSTDIR\translations"
    Delete "$INSTDIR\build.bat"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd

; Lo que Kaint guarda para el usuario (QSettings, QStandardPaths); las herramientas que
; descarga build.bat (%LOCALAPPDATA%\Kaint\toolchain) no se tocan.
Section /o "un.$(SecUnData)" SecUnData
    DeleteRegKey HKCU "${SETTINGS_KEY}"
    RMDir /r "$APPDATA\Kaint"
    RMDir /r "$LOCALAPPDATA\Kaint\Kaint"
    RMDir /r "$LOCALAPPDATA\Kaint\Effects"
    RMDir /r "$LOCALAPPDATA\Kaint\Shapes"
    Delete "$LOCALAPPDATA\Kaint\discord.json"
    RMDir "$LOCALAPPDATA\Kaint"
SectionEnd

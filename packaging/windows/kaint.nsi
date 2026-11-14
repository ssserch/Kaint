; Instalador de Kaint para Windows 10 y 11 (NSIS 3). Lo crea "build.bat --setup":
;
;   makensis /INPUTCHARSET UTF8 /DSOURCE=<carpeta con bin\kaint.exe> /DCONFIG=<winsetup.nsh> kaint.nsi
;
; Toda su información (nombre, versión, autor, web, iconos, licencia, carpeta de instalación,
; páginas, accesos directos, idiomas y textos) sale de winsetup.json, en la carpeta del código:
; packaging\windows\winsetup.ps1 lo convierte en el winsetup.nsh que se incluye aquí.
;
; Se instala para el usuario actual (sin pedir permisos de administrador) en
; %LOCALAPPDATA%\Programs\Kaint, con acceso en el menú Inicio, y aparece en "Aplicaciones
; instaladas" con su desinstalador (Uninstall.exe) como cualquier otra aplicación.

Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"

!macro Require NAME
  !ifndef ${NAME}
    !error "Falta /D${NAME} (usa build.bat --setup)"
  !endif
!macroend
!insertmacro Require SOURCE
!insertmacro Require CONFIG

; APP, APP_VERSION, FILE_VERSION, PUBLISHER, WEBSITE, SUPPORT_URL, COPYRIGHT, FILE_DESCRIPTION,
; BRANDING, OUTFILE, INSTALL_FOLDER, APP_EXE, UNINSTALL_KEY, COMPRESSION, DESKTOP_SHORTCUT y, si
; están activados, SOLID_COMPRESSION, APP_ICON, UNINSTALLER_ICON, LICENSE_FILE, PAGE_*,
; RUN_AFTER_INSTALL y START_MENU_SHORTCUT; más los macros KAINT_LANGUAGES, KAINT_STRINGS y
; KAINT_DELETE_USER_DATA.
!include "${CONFIG}"

!ifdef SOLID_COMPRESSION
  SetCompressor /SOLID ${COMPRESSION}
!else
  SetCompressor ${COMPRESSION}
!endif

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "${INSTALL_FOLDER}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
BrandingText "${BRANDING}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "${FILE_VERSION}"
VIAddVersionKey /LANG=0 "ProductName" "${APP}"
VIAddVersionKey /LANG=0 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=0 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=0 "FileDescription" "${FILE_DESCRIPTION}"
VIAddVersionKey /LANG=0 "LegalCopyright" "${COPYRIGHT}"

; --- Páginas ------------------------------------------------------------------
!ifdef APP_ICON
  !define MUI_ICON "${APP_ICON}"
!endif
!ifdef UNINSTALLER_ICON
  !define MUI_UNICON "${UNINSTALLER_ICON}"
!endif
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!ifdef RUN_AFTER_INSTALL
  !define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!endif

!ifdef PAGE_WELCOME
  !insertmacro MUI_PAGE_WELCOME
!endif
!ifdef PAGE_LICENSE
  !insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!endif
!ifdef PAGE_COMPONENTS
  !insertmacro MUI_PAGE_COMPONENTS
!endif
!ifdef PAGE_FOLDER
  !insertmacro MUI_PAGE_DIRECTORY
!endif
!insertmacro MUI_PAGE_INSTFILES
!ifdef PAGE_FINISH
  !insertmacro MUI_PAGE_FINISH
!endif

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES

; El idioma es el de Windows si está entre los de winsetup.json (si no, el primero), sin preguntarlo.
!insertmacro KAINT_LANGUAGES
!insertmacro KAINT_STRINGS

; --- Instalación --------------------------------------------------------------
Function .onInit
    ; Mismo sitio que la instalación anterior (también la de build.bat --install).
    ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
    StrCmp $0 "" +2
    StrCpy $INSTDIR $0
FunctionEnd

; El programa no puede estar abierto mientras se reemplazan o se borran sus archivos.
!macro WaitForKaint UN
Function ${UN}WaitForKaint
    IfFileExists "$INSTDIR\${APP_EXE}" 0 done
    retry:
    ClearErrors
    FileOpen $0 "$INSTDIR\${APP_EXE}" a
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

; El acceso directo se abre en la carpeta del programa.
!macro Shortcut LINK
    ${GetParent} "$INSTDIR\${APP_EXE}" $1
    SetOutPath "$1"
    CreateShortcut "${LINK}" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
!macroend

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
    ; El Uninstall.exe de build.bat --install no hace falta: este instalador escribe el suyo.
    File /r /x Uninstall.exe "${SOURCE}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
!ifdef START_MENU_SHORTCUT
    !insertmacro Shortcut "$SMPROGRAMS\${APP}.lnk"
!endif
!if "${DESKTOP_SHORTCUT}" == "always"
    !insertmacro Shortcut "$DESKTOP\${APP}.lnk"
!endif

    ; Entrada de "Aplicaciones instaladas"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${WEBSITE}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink" "${SUPPORT_URL}"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

; El acceso directo del escritorio como componente: con la casilla sin marcar o marcada.
!if "${DESKTOP_SHORTCUT}" == "optional"
Section /o "$(SecDesktop)" SecDesktop
    !insertmacro Shortcut "$DESKTOP\${APP}.lnk"
SectionEnd
!else if "${DESKTOP_SHORTCUT}" == "checked"
Section "$(SecDesktop)" SecDesktop
    !insertmacro Shortcut "$DESKTOP\${APP}.lnk"
SectionEnd
!endif

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

; Lo que el programa guarda para el usuario (QSettings, QStandardPaths), según «userData» de
; winsetup.json; las herramientas que descarga build.bat (%LOCALAPPDATA%\Kaint\toolchain) no se tocan.
Section /o "un.$(SecUnData)" SecUnData
    !insertmacro KAINT_DELETE_USER_DATA
SectionEnd

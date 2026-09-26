; This Source Code Form is subject to the terms of the Mozilla Public
; License, v. 2.0. If a copy of the MPL was not distributed with this
; file, You can obtain one at https://mozilla.org/MPL/2.0/.

; OpenShape's Windows installer (NSIS 3, Unicode). Built by
; scripts/windows/make-installer.sh, which passes:
;   VERSION      e.g. 0.1.0 (numeric: also the setup file's version resource)
;   SOURCE_DIR   the packaged folder (dist/OpenShape), Windows path
;   FILES_NSH    generated list of the packaged files (for the uninstaller)
;   SIZE_KB      installed size in KiB
;   ICON_FILE    resources\icons\openshape.ico
;   LICENSE_FILE the MPL-2.0 text (CRLF line breaks)
;   OUTFILE      the setup .exe to write
;   TEST_DESKTOP_DIR (tests only) replaces the desktop folder
;
; Per user, no administrator rights: installs into
; %LOCALAPPDATA%\Programs\OpenShape, writes only HKCU keys. Upgrading over an
; existing installation replaces the program files (the previous
; uninstaller removes exactly the files it installed); shortcuts, pins and
; the file association stay. The uninstaller never touches the user's
; projects, settings (HKCU\Software\OpenShape) or data folder
; (%LOCALAPPDATA%\OpenShape).
;
; Command line: setup.exe [/S] [/DESKTOP] [/D=<folder>]
;   /S        silent; /DESKTOP adds a desktop shortcut (the dialog asks
;   instead); /D must come last, unquoted.

Unicode true
ManifestDPIAware true
ManifestSupportedOS all
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "WinMessages.nsh"
!include "${FILES_NSH}"

!define APP_NAME "OpenShape"
!define PUBLISHER "OpenShape contributors"
!define APP_URL "https://github.com/SamuelAirs/openshape"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenShape"
!define PROGID "OpenShape.Project"
!define EXTENSION ".openshape"
!ifdef TEST_DESKTOP_DIR
  !define DESKTOP_DIR "${TEST_DESKTOP_DIR}"
!else
  !define DESKTOP_DIR "$DESKTOP"
!endif
!define SHCNE_ASSOCCHANGED 0x08000000

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP_NAME} ${VERSION} Setup"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (C) ${PUBLISHER}. Mozilla Public License 2.0."

!define MUI_ICON "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_ABORTWARNING

Var PreviousDir     ; an existing installation's folder (upgrade)
Var DesktopLink     ; the desktop shortcut an earlier install created, if any

!insertmacro MUI_PAGE_WELCOME
!define MUI_LICENSEPAGE_TEXT_TOP "OpenShape is free software under the Mozilla Public License 2.0."
!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\OpenShape.exe"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Create a desktop shortcut"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateDesktopShortcut
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishShow
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE FinishLeave
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Waits until OpenShape.exe in <dir> is not running (a running program's
; files cannot be replaced). Silent runs give up instead (exit code 2).
!macro WAIT_NOT_RUNNING un
Function ${un}WaitNotRunning
  Exch $0
  Push $1
  retry:
    IfFileExists "$0\OpenShape.exe" 0 done
    ClearErrors
    FileOpen $1 "$0\OpenShape.exe" a
    IfErrors running
    FileClose $1
    Goto done
  running:
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OpenShape is running from $0.$\r$\n$\r$\nClose it, then click Retry." /SD IDCANCEL IDRETRY retry
    Abort "OpenShape is running from $0: close it and try again."
  done:
  Pop $1
  Pop $0
FunctionEnd
!macroend
!insertmacro WAIT_NOT_RUNNING ""
!insertmacro WAIT_NOT_RUNNING "un."

Function .onInit
  SetShellVarContext current
  SetRegView 64
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "OpenShape needs a 64-bit version of Windows." /SD IDOK
    SetErrorLevel 4
    Quit
  ${EndIf}
  ReadRegStr $PreviousDir HKCU "${UNINST_KEY}" "InstallLocation"
  ReadRegStr $DesktopLink HKCU "${UNINST_KEY}" "DesktopShortcut"
FunctionEnd

Section "OpenShape" SecMain
  SectionIn RO
  Push $INSTDIR
  Call WaitNotRunning

  ; Upgrade: the previous version's uninstaller removes the files it
  ; installed (and nothing else); shortcuts and registry entries are
  ; rewritten below.
  ${If} $PreviousDir != ""
  ${AndIf} ${FileExists} "$PreviousDir\Uninstall.exe"
    Push $PreviousDir
    Call WaitNotRunning
    DetailPrint "Removing the previous version from $PreviousDir"
    ExecWait '"$PreviousDir\Uninstall.exe" /S /UPGRADE _?=$PreviousDir' $0
    ${If} $0 != 0
      Abort "The previous version could not be removed (error $0). Close OpenShape and try again."
    ${EndIf}
    ${If} $PreviousDir != $INSTDIR
      Delete "$PreviousDir\Uninstall.exe"
      RMDir "$PreviousDir"
    ${EndIf}
  ${EndIf}

  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Start menu (per user, no folder: one entry, as Windows recommends).
  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\OpenShape.exe" "" "$INSTDIR\OpenShape.exe" 0 SW_SHOWNORMAL "" "Direct, precise solid CAD"

  ; Apps & features entry.
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\OpenShape.exe,0"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "${APP_URL}"
  WriteRegStr HKCU "${UNINST_KEY}" "HelpLink" "${APP_URL}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" ${SIZE_KB}
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

  ; .openshape files open in OpenShape (per user).
  WriteRegStr HKCU "Software\Classes\${EXTENSION}" "" "${PROGID}"
  WriteRegStr HKCU "Software\Classes\${EXTENSION}" "Content Type" "application/x-openshape"
  WriteRegStr HKCU "Software\Classes\${EXTENSION}\OpenWithProgids" "${PROGID}" ""
  WriteRegStr HKCU "Software\Classes\${PROGID}" "" "OpenShape project"
  WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\OpenShape.exe,0"
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\OpenShape.exe" "%1"'
  System::Call 'shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i 0, p 0, p 0)'

  ; Silent installs ask for a desktop shortcut with /DESKTOP; the dialog
  ; asks on its last page.
  ${If} ${Silent}
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/DESKTOP" $1
    ${IfNot} ${Errors}
      Call CreateDesktopShortcut
    ${ElseIf} $DesktopLink != ""
      ; An upgrade keeps the shortcut an earlier install created.
      Call CreateDesktopShortcut
    ${EndIf}
  ${EndIf}
SectionEnd

Function CreateDesktopShortcut
  CreateShortcut "${DESKTOP_DIR}\${APP_NAME}.lnk" "$INSTDIR\OpenShape.exe" "" "$INSTDIR\OpenShape.exe" 0 SW_SHOWNORMAL "" "Direct, precise solid CAD"
  WriteRegStr HKCU "${UNINST_KEY}" "DesktopShortcut" "${DESKTOP_DIR}\${APP_NAME}.lnk"
FunctionEnd

; Finish page: the desktop checkbox shows whether a shortcut exists.
Function FinishShow
  ${If} $DesktopLink != ""
    SendMessage $mui.FinishPage.ShowReadme ${BM_SETCHECK} ${BST_CHECKED} 0
  ${EndIf}
FunctionEnd

Function FinishLeave
  SendMessage $mui.FinishPage.ShowReadme ${BM_GETCHECK} 0 0 $0
  ${If} $0 != ${BST_CHECKED}
  ${AndIf} $DesktopLink != ""
    Delete "$DesktopLink"
    DeleteRegValue HKCU "${UNINST_KEY}" "DesktopShortcut"
  ${EndIf}
FunctionEnd

; ---- Uninstaller ------------------------------------------------------------

Function un.onInit
  SetShellVarContext current
  SetRegView 64
FunctionEnd

Section "Uninstall"
  Push $INSTDIR
  Call un.WaitNotRunning

  ; Exactly the files the installer put there (FILES_NSH), then the folders
  ; if they are empty: anything else in them stays.
  !insertmacro OPENSHAPE_UNINSTALL_FILES
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  ; /UPGRADE (run by a newer installer): keep shortcuts and registry entries.
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/UPGRADE" $1
  ${IfNot} ${Errors}
    Return
  ${EndIf}

  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  ; Only the desktop shortcut this installer made (never one the user made).
  ReadRegStr $2 HKCU "${UNINST_KEY}" "DesktopShortcut"
  ${If} $2 != ""
    Delete "$2"
  ${EndIf}

  ; The association, if it is still ours.
  ReadRegStr $3 HKCU "Software\Classes\${EXTENSION}" ""
  ${If} $3 == "${PROGID}"
    DeleteRegValue HKCU "Software\Classes\${EXTENSION}" ""
    DeleteRegValue HKCU "Software\Classes\${EXTENSION}" "Content Type"
  ${EndIf}
  DeleteRegValue HKCU "Software\Classes\${EXTENSION}\OpenWithProgids" "${PROGID}"
  DeleteRegKey /ifempty HKCU "Software\Classes\${EXTENSION}\OpenWithProgids"
  DeleteRegKey /ifempty HKCU "Software\Classes\${EXTENSION}"
  DeleteRegKey HKCU "Software\Classes\${PROGID}"
  DeleteRegKey HKCU "${UNINST_KEY}"
  System::Call 'shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i 0, p 0, p 0)'
SectionEnd

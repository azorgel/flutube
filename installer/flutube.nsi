; FluTube — Windows NSIS Installer Script
; Build with: makensis /DBUILD_DIR=<path> /DDIST_DIR=<path> flutube.nsi
; Requires NSIS 3.x — https://nsis.sourceforge.io/Download

!include "MUI2.nsh"

; ── Metadata ──────────────────────────────────────────────────────────────────
Name              "FluTube"
OutFile           "${DIST_DIR}\FluTube Installer.exe"
Unicode           True
RequestExecutionLevel admin

; VST3 standard install location (64-bit Program Files\Common Files\VST3)
InstallDir        "$COMMONFILES64\VST3"

; ── MUI Interface ─────────────────────────────────────────────────────────────
!define MUI_WELCOMEPAGE_TITLE "FluTube v1.0"
!define MUI_WELCOMEPAGE_TEXT  "This wizard will install the FluTube VST3 plugin.$\r$\n$\r$\nFluTube lets you load any YouTube audio directly into your DAW — paste a URL, hit Load, and play it like a sampler instrument.$\r$\n$\r$\nClick Next to continue."

!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_TEXT   "FluTube has been installed successfully.\
$\r$\n$\r$\nTo use the plugin in Ableton Live:$\r$\n\
  1. Open Preferences > Plug-Ins$\r$\n\
  2. Enable VST3 Plug-In Custom Folder or check VST3 system folder$\r$\n\
  3. Click Rescan Plug-Ins$\r$\n\
  4. Add a MIDI track and load FluTube from the instrument browser"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Install section ───────────────────────────────────────────────────────────
Section "FluTube VST3" SecMain
    SectionIn RO   ; required — cannot be deselected

    ; Use 64-bit registry and filesystem paths
    SetRegView 64

    ; Copy VST3 bundle to C:\Program Files\Common Files\VST3\
    SetOutPath "$COMMONFILES64\VST3"
    File /r "${BUILD_DIR}\FluTube_artefacts\Release\VST3\FluTube.vst3"

    ; Write uninstaller to its own directory
    SetOutPath "$PROGRAMFILES64\Volta Labs\FluTube"
    WriteUninstaller "$PROGRAMFILES64\Volta Labs\FluTube\uninstall.exe"

    ; Add/Remove Programs registration
    WriteRegStr  HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "DisplayName"      "FluTube"
    WriteRegStr  HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "DisplayVersion"   "1.0"
    WriteRegStr  HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "Publisher"        "Volta Labs"
    WriteRegStr  HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "InstallLocation"  "$COMMONFILES64\VST3"
    WriteRegStr  HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "UninstallString"  "$PROGRAMFILES64\Volta Labs\FluTube\uninstall.exe"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube" \
                 "NoRepair" 1
SectionEnd

; ── Uninstall section ─────────────────────────────────────────────────────────
Section "Uninstall"
    SetRegView 64

    RMDir /r "$COMMONFILES64\VST3\FluTube.vst3"
    Delete   "$PROGRAMFILES64\Volta Labs\FluTube\uninstall.exe"
    RMDir    "$PROGRAMFILES64\Volta Labs\FluTube"
    RMDir    "$PROGRAMFILES64\Volta Labs"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\FluTube"
SectionEnd

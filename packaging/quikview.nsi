; QuikView 安装包脚本 (NSIS 3.x, Unicode)
; 用法:
;   makensis /DSRC="<payload目录>" /DOUTFILE="QuikView-setup.exe" packaging\quikview.nsi
; payload 目录由 windeployqt + mingw 运行库闭包生成, 见 packaging/README.md
Unicode true
!include "MUI2.nsh"

; ---------- 可覆盖参数 ----------
!ifndef SRC
  !define SRC "staging"
!endif
!ifndef OUTFILE
  !define OUTFILE "QuikView-setup.exe"
!endif

; ---------- 基本定义 ----------
!define APPNAME      "QuikView"
!define APPVERSION   "0.4.0-dev"
!define PUBLISHER    "QuikView Project"

VIProductVersion "0.4.0.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} Installer"
VIAddVersionKey "FileVersion" "${APPVERSION}"
VIAddVersionKey "ProductVersion" "${APPVERSION}"
VIAddVersionKey "LegalCopyright" "${PUBLISHER}"

Name "${APPNAME} ${APPVERSION}"
OutFile "${OUTFILE}"
; Default to D:\Entertainment\QuikView (keep installers off C:), user can change in dir page
InstallDir "D:\Entertainment\QuikView"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; ---------- 图标 ----------
!define MUI_ICON   "${SRC}\quikview.ico"
!define MUI_UNICON "${SRC}\quikview.ico"

; ---------- 完成页运行 ----------
!define MUI_FINISHPAGE_RUN "$INSTDIR\quikview.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APPNAME} after installation"

; ---------- 页面 ----------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

; ---------- 安装节 ----------
Section "${APPNAME} 程序主文件" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${SRC}\*.*"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\quikview.exe"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\卸载 ${APPNAME}.lnk" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" $INSTDIR
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
      "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
      "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
      "DisplayIcon" "$INSTDIR\quikview.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
      "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
      "Publisher" "${PUBLISHER}"
SectionEnd

Section "桌面快捷方式" SEC_DESKTOP
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\quikview.exe"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MAIN} "QuikView 主程序及运行库（必需）"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "在桌面创建 ${APPNAME} 快捷方式"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------- 卸载节 ----------
Section "Uninstall"
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\卸载 ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
  DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd

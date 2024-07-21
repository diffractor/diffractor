;NSIS Modern User Interface

!define PRODUCT_NAME "Diffractor"
!define PRODUCT32_EXE "diffractor.exe"
!define PRODUCT64_EXE "diffractor64.exe"
!define PRODUCT_PUBLISHER "Diffractor"
!define BUILD_NUM "1180"
!define PRODUCT_VERSION "126.0"
!define FILE_VERSION "1.26.0.${BUILD_NUM}"
!define PRODUCT_WEB_SITE "http://www.Diffractor.com/"
!define PRODUCT_STARTMENU_REGVAL "NSIS:StartMenuDir"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_SETTINGS_KEY "Software\${PRODUCT_NAME}"
!define PRODUCT_DEFAULT_DIR_KEY "${PRODUCT_SETTINGS_KEY}\InstallDir"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; Language Selection Dialog Settings
!define MUI_LANGDLL_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_LANGDLL_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "NSIS:Language"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW MyWelcomeShowCallback

Unicode True

;--------------------------------
;Include Modern UI

!include nsDialogs.nsh
!include MUI2.nsh
!include WinMessages.nsh 
!include WinVer.nsh
!include FileFunc.nsh
!include StrFunc.nsh
!include x64.nsh

${StrStr} # Supportable for Install Sections and Functions
 
Function un.CloseProgram 
  Exch $1
  Push $0
  loop:
    FindWindow $0 $1
    IntCmp $0 0 done
      SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 100 
    Goto loop 
  done: 
  Pop $0 
  Pop $1
FunctionEnd

Function .CloseProgram 
  Exch $1
  Push $0
  loop:
    FindWindow $0 $1
    IntCmp $0 0 done
      SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 100 
    Goto loop 
  done: 
  Pop $0 
  Pop $1
FunctionEnd


;--------------------------------

!ifdef SHCNE_ASSOCCHANGED
!undef SHCNE_ASSOCCHANGED
!endif
!define SHCNE_ASSOCCHANGED 0x08000000

!ifdef SHCNF_FLUSH
!undef SHCNF_FLUSH
!endif
!define SHCNF_FLUSH        0x1000

!ifdef SHCNF_IDLIST
!undef SHCNF_IDLIST
!endif
!define SHCNF_IDLIST       0x0000

!macro UPDATEFILEASSOC
  IntOp $1 ${SHCNE_ASSOCCHANGED} | 0
  IntOp $0 ${SHCNF_IDLIST} | ${SHCNF_FLUSH}
; Using the system.dll plugin to call the SHChangeNotify Win32 API function so we
; can update the shell.
  System::Call "shell32::SHChangeNotify(i,i,i,i) (${SHCNE_ASSOCCHANGED}, $0, 0, 0)"
!macroend

;--------------------------------
;General

Name "${PRODUCT_NAME}"
Caption "${PRODUCT_NAME} ${PRODUCT_VERSION} Setup"
BrandingText "${PRODUCT_NAME} ${PRODUCT_VERSION}.${BUILD_NUM}"

OutFile "..\diffractor-setup.exe"
InstallDir "$LOCALAPPDATA\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DEFAULT_DIR_KEY}" ""
ShowInstDetails show
ShowUnInstDetails show
SetCompressor /SOLID /FINAL lzma
SetCompressorDictSize 128
Icon "Diffractor.ico"
XPStyle on
RequestExecutionLevel user

VIProductVersion "${FILE_VERSION}"
VIAddVersionKey ProductName "${PRODUCT_NAME}"
VIAddVersionKey ProductVersion "${FILE_VERSION}"
VIAddVersionKey CompanyName "${PRODUCT_PUBLISHER}"
VIAddVersionKey CompanyWebsite "${PRODUCT_WEB_SITE}"
VIAddVersionKey FileVersion "${FILE_VERSION}"
VIAddVersionKey FileDescription ""
VIAddVersionKey LegalCopyright "Copyright (C) 2022 Zac Walker"



;--------------------------------
;Interface Settings

;Show all languages, despite user's codepage
!define MUI_LANGDLL_ALLLANGUAGES

!define MUI_ABORTWARNING
!define MUI_ICON "Diffractor.ico"
!define MUI_UNICON "Diffractor.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "logo.bmp"
!define MUI_HEADERIMAGE_BITMAP_NOSTRETCH
!define MUI_HEADERIMAGE_RIGHT
!define MUI_WELCOMEFINISHPAGE_BITMAP "install.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "uninstall.bmp"
!define MUI_COMPONENTSPAGE_SMALLDESC



;--------------------------------
;Pages

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "License.txt"
;!insertmacro MUI_PAGE_COMPONENTS
;!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION "StartDiffractor"
!insertmacro MUI_PAGE_FINISH
  
!insertmacro MUI_UNPAGE_WELCOME
;!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH 

;--------------------------------
;Languages

!insertmacro MUI_LANGUAGE "English" ; The first language is the default language
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Spanish"
;!insertmacro MUI_LANGUAGE "Czech"

;--------------------------------
;Reserve Files
  
;If you are using solid compression, files that are required before
;the actual installation should be stored first in the data block,
;because this will make your installer start faster.
  
!insertmacro MUI_RESERVEFILE_LANGDLL

Function .onInit
  ${IfNot} ${AtLeastWin7}
    MessageBox MB_OK "Sadly, Diffractor requires Windows 7 or above."
    Quit
  ${EndIf}

  Var /GLOBAL DEF_LANG
  ReadRegStr $DEF_LANG HKCU "${PRODUCT_SETTINGS_KEY}" "lang"

  ${If} $DEF_LANG == "de"
	StrCpy $LANGUAGE 1031
  ${ElseIf} $DEF_LANG == "cs"
	StrCpy $LANGUAGE 1029
  ${ElseIf} $DEF_LANG == "es"
	StrCpy $LANGUAGE 1034
  ${ElseIf} $DEF_LANG == "ja"
	StrCpy $LANGUAGE 1041
  ${EndIf}

  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd
Function un.onInit
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd
Function .onInstSuccess
	${GetParameters} $1
	${StrStr} $0 $1 "/RR"
	${If} $0 != ""
		${If} ${RunningX64}
			Exec "$INSTDIR\${PRODUCT64_EXE}"
		${Else}
			Exec "$INSTDIR\${PRODUCT32_EXE}"
		${EndIf} 		
	${EndIf}
FunctionEnd
Function StartDiffractor
    ${If} ${RunningX64}
		Exec "$INSTDIR\${PRODUCT64_EXE}"
	${Else}
		Exec "$INSTDIR\${PRODUCT32_EXE}"
	${EndIf} 		
FunctionEnd

Function MyWelcomeShowCallback
	Var /global welcome_text
   ${If} $LANGUAGE == 1031
		StrCpy  $welcome_text "Superschnelle Suche, Betrachtung und Vergleich von Fotos oder Videos.$\r$\n$\r$\nOptimiert für Ihre Grafikkarte und Ihren PC."
	${ElseIf} $LANGUAGE == 1029		
		StrCpy  $welcome_text "Lehká správa fotografií a médií.$\r$\n$\r$\nOptimalizováno pro vaši grafickou kartu a PC."
	${ElseIf} $LANGUAGE == 1041		
		StrCpy  $welcome_text "写真やビデオを超高速で検索、表示、比較します。$\r$\n$\r$\nグラフィックス カードと PC に合わせて最適化されています。"
	${Else}
		StrCpy  $welcome_text "Superfast searching, viewing and comparing of photos or videos.$\r$\n$\r$\nOptimized for your graphics card and PC."
	${EndIf} 
	SendMessage $mui.WelcomePage.Text ${WM_SETTEXT} 0 "STR:$welcome_text"
FunctionEnd

;--------------------------------
;Installer Sections

Section "Diffractor"

	Var /GLOBAL PRODUCT_EXE

	${If} ${RunningX64}
		StrCpy $PRODUCT_EXE "${PRODUCT64_EXE}"
	${Else}
		StrCpy $PRODUCT_EXE "${PRODUCT32_EXE}"
	${EndIf}  

	Push "DIFF_MAIN"
	Call .CloseProgram
    
	SetOverwrite ifnewer
	
	SetOutPath $INSTDIR\languages  
;	File "..\exe\languages\cs.po"
	File "..\exe\languages\de.po"
	File "..\exe\languages\it.po"
	File "..\exe\languages\es.po"
	File "..\exe\languages\ja.po"

	SetOutPath $INSTDIR\dictionaries  
	File "..\exe\dictionaries\en_US.aff"
	File "..\exe\dictionaries\en_US.dic"

	SetOutPath $INSTDIR	
	File "..\exe\location-countries.txt"
	File "..\exe\location-places.txt"
	File "..\exe\location-states.txt"
	File "..\exe\diffractor-tools.json"

	SetOverwrite on
	File "..\exe\${PRODUCT32_EXE}"
	File "..\exe\${PRODUCT64_EXE}"
		
    ${If} ${Silent}
        ; Dont add desktop icon if in silent mode
    ${Else}
		CreateShortCut "$STARTMENU\Programs\Diffractor.lnk" "$INSTDIR\$PRODUCT_EXE" "" "$INSTDIR\$PRODUCT_EXE" 0
		CreateShortCut "$DESKTOP\Diffractor.lnk" "$INSTDIR\$PRODUCT_EXE" "" "$INSTDIR\$PRODUCT_EXE" 0
	${EndIf}
  
	; Write the uninstall keys for Windows
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\$PRODUCT_EXE"	
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
	WriteRegDWord SHCTX "${PRODUCT_UNINST_KEY}" "EstimatedSize" 20000
	WriteRegDWORD SHCTX "${PRODUCT_UNINST_KEY}" "NoModify" 1
	WriteRegDWORD SHCTX "${PRODUCT_UNINST_KEY}" "NoRepair" 1
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegStr SHCTX "${PRODUCT_UNINST_KEY}" "InstallLocation" "$\"$INSTDIR$\""
	
	WriteRegStr SHCTX "${PRODUCT_SETTINGS_KEY}" "" $INSTDIR	

	WriteRegStr SHCTX "Software\Classes\Folder\shell\diffractor" "" "Diffractor"
  	WriteRegStr SHCTX "Software\Classes\Folder\shell\diffractor\command" "" '"$INSTDIR\$PRODUCT_EXE" "%1"'
  	WriteRegStr SHCTX "Software\Classes\File\shell\diffractor" "" "Diffractor"
  	WriteRegStr SHCTX "Software\Classes\File\shell\diffractor\command" "" '"$INSTDIR\$PRODUCT_EXE" "%1"'

	WriteUninstaller "$INSTDIR\Uninstall.exe"

	WriteRegStr HKCU "Software\Classes\.jpe\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.jpeg\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.jpg\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.png\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.gif\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.mp4\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.mov\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.avi\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.mp3\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.cr2\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.cr3\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Classes\.webp\OpenWithProgids" "${PRODUCT_NAME}" ""

	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.jpe\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.jpeg\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.jpg\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.png\OpenWithProgids" "${PRODUCT_NAME}" ""
	WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.gif\OpenWithProgids" "${PRODUCT_NAME}" ""

	;WriteRegDWORD HKCU "Software\Classes\${PRODUCT_NAME}" "EditFlags" 65536
  
	WriteRegStr HKCU "Software\Classes\${PRODUCT_NAME}" "" "Diffractor"
	WriteRegStr HKCU "Software\Classes\${PRODUCT_NAME}" "FriendlyTypeName" "Diffractor"
	;WriteRegStr HKCU "Software\Classes\${PRODUCT_NAME}\DefaultIcon" "" "$INSTDIR\Clipboard.ico"
	WriteRegStr HKCU "Software\Classes\${PRODUCT_NAME}\shell\open\command" "" '"$INSTDIR\${PRODUCT_NAME}.exe" "%1"'

	!insertmacro UPDATEFILEASSOC

	${If} $LANGUAGE == 1031
		WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "lang" "de"
	${ElseIf} $LANGUAGE == 1029
		WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "lang" "cs"
	${ElseIf} $LANGUAGE == 1034
		WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "lang" "es"
	${ElseIf} $LANGUAGE == 1041
		WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "lang" "ja"
	${Else}
		WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "lang" "en"
	${EndIf} 

	WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "install_lang" $LANGUAGE

SectionEnd

 
;--------------------------------
;Uninstaller Section

Section "Uninstall"

	; ExecWait '"taskkill" /f /IM diffractor.exe' $0

	Push "DIFF_MAIN"
	Call un.CloseProgram

	; remove registry keys
	DeleteRegKey SHCTX "${PRODUCT_UNINST_KEY}"
	DeleteRegKey SHCTX "${PRODUCT_SETTINGS_KEY}"
	DeleteRegKey SHCTX "Software\Classes\Folder\shell\diffractor"
	DeleteRegKey SHCTX "Software\Classes\File\shell\diffractor"

	DeleteRegValue HKCU "Software\Classes\.jpe\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.jpeg\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.jpg\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.png\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.gif\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.mp4\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.mov\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.avi\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.mp3\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.cr2\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.cr3\OpenWithProgids" "${PRODUCT_NAME}"
	DeleteRegValue HKCU "Software\Classes\.webp\OpenWithProgids" "${PRODUCT_NAME}"

	DeleteRegKey HKCU "Software\Classes\${PRODUCT_NAME}"

	;DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
	;DeleteRegKey HKCU "Software\${PRODUCT_NAME}"

	!insertmacro UPDATEFILEASSOC
	
	Delete /REBOOTOK "$INSTDIR\${PRODUCT32_EXE}"
	Delete /REBOOTOK "$INSTDIR\${PRODUCT64_EXE}"
	Delete $INSTDIR\uninstall.exe
	Delete "$STARTMENU\Programs\Diffractor.lnk"
	Delete "$DESKTOP\Diffractor.lnk"

	RMDir /r /REBOOTOK "$INSTDIR"

SectionEnd  

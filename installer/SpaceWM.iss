; SpaceWM Inno Setup script — built by scripts/package.ps1 -Installer
; Manual build (from repo root):
;   ISCC.exe /DAppVersion=0.1.0 /DStageDir=..\dist\SpaceWM-win64 /DOutputDir=..\dist installer\SpaceWM.iss
; All defines default to paths relative to this script when omitted.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir AddBackslash(SourcePath) + "..\dist\SpaceWM-win64"
#endif
#ifndef OutputDir
  #define OutputDir AddBackslash(SourcePath) + "..\dist"
#endif

[Setup]
AppId={{F0C52504-DB7A-4296-AC54-DBB74DD3A024}
AppName=SpaceWM
AppVersion={#AppVersion}
AppPublisher=ji-tz
AppPublisherURL=https://github.com/ji-tz/SpaceWM
AppSupportURL=https://github.com/ji-tz/SpaceWM/issues
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\SpaceWM
DefaultGroupName=SpaceWM
DisableProgramGroupPage=yes
; Per-user install — no admin elevation required.
PrivilegesRequired=lowest
OutputBaseFilename=SpaceWM-Setup-x64
OutputDir={#OutputDir}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\SpaceWM.exe
UninstallDisplayName=SpaceWM
CloseApplications=yes

; Inno Setup does not bundle a Simplified Chinese translation file, so the
; wizard UI stays English (custom strings below are ours and stay Chinese).
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: unchecked

[Files]
; Staged portable layout: SpaceWM.exe next to Qt/CRT runtime (see scripts/package.ps1).
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\SpaceWM"; Filename: "{app}\SpaceWM.exe"; Comment: "按显示器独立的虚拟桌面（Spaces）管理器"
Name: "{autoprograms}\卸载 SpaceWM"; Filename: "{uninstallexe}"
Name: "{autodesktop}\SpaceWM"; Filename: "{app}\SpaceWM.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SpaceWM.exe"; Description: "启动 SpaceWM"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; TR/EH runtime logs: detectProjectRoot() falls back to the exe's parent dir
; for deployed copies, so logs may live in {app} or right above it.
Type: filesandordirs; Name: "{app}\TR"
Type: filesandordirs; Name: "{app}\EH"
Type: filesandordirs; Name: "{localappdata}\Programs\TR"
Type: filesandordirs; Name: "{localappdata}\Programs\EH"

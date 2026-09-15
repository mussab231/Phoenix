; Phoenix Download Manager - Inno Setup 6 script.
; Runs from tools/release; sources the staged build in dist\Phoenix.
; The phoenix:// protocol is registered by the app itself on first run
; (HKCU, no admin), so the installer does not need elevated per-machine keys.

#define MyAppName "Phoenix Download Manager"
#define MyAppVersion "0.9.0"
#define MyAppPublisher "Phoenix"
#define MyAppExeName "Phoenix.exe"

[Setup]
AppId={{5E3F8B1A-3D47-4C8B-9F2A-2D6B4C7E9F01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Phoenix
DefaultGroupName=Phoenix
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=..\..\dist
OutputBaseFilename=Phoenix-{#MyAppVersion}-setup
SetupIconFile=..\..\assets\phoenix.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
DisableProgramGroupPage=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start Phoenix with Windows"; Flags: unchecked

[Files]
Source: "..\..\dist\Phoenix\Phoenix.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\Phoenix\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Phoenix"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Phoenix"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Start with Windows (optional task).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Phoenix"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart
; Clean up the app settings key on uninstall.
Root: HKCU; Subkey: "Software\Phoenix\Phoenix"; Flags: uninsdeletekeyifempty

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,Phoenix}"; Flags: nowait postinstall skipifsilent
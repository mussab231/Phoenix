; Phoenix Download Manager - Inno Setup 6 script.
; Runs from tools/release; sources the staged build in dist\Phoenix.
; The phoenix:// protocol is registered by the app itself on first run
; (HKCU, no admin), so the installer does not need elevated per-machine keys.

#define MyAppName "Phoenix Download Manager"
#define MyAppVersion "1.0.0"
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
Name: "browserintegration"; Description: "Install browser integration (Chrome / Edge extension + native host)"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "..\..\dist\Phoenix\Phoenix.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\Phoenix\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; The unpacked extension the user loads via chrome://extensions (Load unpacked).
Source: "..\..\tools\native\extension\*"; DestDir: "{app}\browser-extension"; Flags: recursesubdirs createallsubdirs; Tasks: browserintegration
; Host registration script, run post-install when the task is selected.
Source: "..\..\tools\native\install.ps1"; DestDir: "{app}"; Tasks: browserintegration; Flags: ignoreversion
Source: "..\..\tools\native\uninstall-host.ps1"; DestDir: "{app}"; Tasks: browserintegration; Flags: ignoreversion

[Icons]
Name: "{group}\Phoenix"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Phoenix"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Start with Windows (optional task).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Phoenix"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart
; Clean up the app settings key on uninstall.
Root: HKCU; Subkey: "Software\Phoenix\Phoenix"; Flags: uninsdeletekeyifempty

[UninstallDelete]
; The native host manifest written by install.ps1 at install time.
Type: files; Name: "{app}\manifest.chrome.json"

[UninstallRun]
; Remove the native messaging host registration so the installed extension does
; not point at a deleted executable. Inno Setup treats { as a constant, so the
; cleanup lives in its own script instead of an inline -Command string.
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\uninstall-host.ps1"""; Tasks: browserintegration; RunOnceId: "RemovePhoenixHost"

[Run]
; Register the native messaging host (HKCU, no admin) so the bundled extension
; can hand links to Phoenix. The extension's fixed key gives a stable ID, so
; the script runs unattended.
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\install.ps1"" -PhoenixExe ""{app}\Phoenix.exe"""; Tasks: browserintegration; Description: "Registering the browser native host..."; StatusMsg: "Registering browser integration"
; Point the user at the folder to load in chrome://extensions.
Filename: "explorer.exe"; Parameters: """{app}\browser-extension"""; Tasks: browserintegration; Description: "Open the extension folder (load it in chrome://extensions)"; Flags: postinstall skipifsilent unchecked
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,Phoenix}"; Flags: nowait postinstall skipifsilent
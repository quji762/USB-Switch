; QuickEject installer (Inno Setup 6).
;
; Build order matters: build.ps1 puts quickeject.exe in ..\dist first, and this
; script reads the version from that binary so there is no version to keep in
; sync by hand.
;
;   .\build.ps1
;   iscc installer\quickeject.iss
;
; Notes for a shipped product:
;   * Per-user install into %LOCALAPPDATA%\Programs (no elevation prompt).
;     Switch DefaultDirName to {autopf} and PrivilegesRequired=admin for a
;     machine-wide install.
;   * Authenticode signing: add "SignTool=..." to [Setup] and a signing task,
;     otherwise SmartScreen will warn on first run.
;   * Run-at-sign-in is deliberately NOT created here: the in-app setting owns
;     that registry value, so installer and UI can never disagree.

#define MyAppName "QuickEject"
#define MyAppExeName "quickeject.exe"
#define MyAppVersion GetVersionNumbersString("..\dist\quickeject.exe")

[Setup]
AppId={{88DB2805-7CF6-449D-9C44-A6239B4A86CD}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher=QuickEject Project
AppComments=USB safe removal tray tool
AppContact=https://example.invalid/quickeject
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\dist\installer
OutputBaseFilename=quickeject-setup-{#MyAppVersion}
SetupIconFile=..\resources\quickeject.ico
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\dist\quickeject.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent

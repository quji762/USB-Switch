; USB-Switch installer (Inno Setup 6).
;
; Build order matters: build.ps1 puts usb-switch.exe in ..\dist first, and this
; script reads the version from that binary so there is no version to keep in
; sync by hand.
;
;   .\build.ps1
;   iscc installer\usb-switch.iss
;
; Notes for a shipped product:
;   * Per-user install into %LOCALAPPDATA%\Programs (no elevation prompt).
;     Switch DefaultDirName to {autopf} and PrivilegesRequired=admin for a
;     machine-wide install.
;   * Authenticode signing: add "SignTool=..." to [Setup] and a signing task,
;     otherwise SmartScreen will warn on first run.
;   * Run-at-sign-in is deliberately NOT created here: the in-app setting owns
;     that registry value, so installer and UI can never disagree.

#define MyAppName "USB-Switch"
#define MyAppExeName "usb-switch.exe"
#define MyAppVersion GetVersionNumbersString("..\dist\usb-switch.exe")

[Setup]
AppId={{88DB2805-7CF6-449D-9C44-A6239B4A86CD}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher=USB-Switch Project
AppComments=USB safe removal tray tool
AppPublisherURL=https://github.com/quji762/USB-Switch
AppSupportURL=https://github.com/quji762/USB-Switch/issues
AppMutex=Local\USB-Switch,Local\QuickEject
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\dist\installer
OutputBaseFilename=usb-switch-setup-{#MyAppVersion}
SetupIconFile=..\resources\usb-switch.ico
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "languages\ChineseSimplified.isl"

[CustomMessages]
english.SettingsShortcut=USB-Switch Settings
chinesesimplified.SettingsShortcut=USB-Switch 设置

english.LegacyNameConflict=Setup found a file named quickeject.exe that it could not identify as the previous QuickEject application. Choose a different installation folder or move that file yourself before continuing.
chinesesimplified.LegacyNameConflict=安装目录中有一个 quickeject.exe，无法确认它是旧版 QuickEject。请选择其他安装目录，或自行移走该文件后再继续。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\dist\usb-switch.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

; Remove the old executable when upgrading an installation that used the same AppId.
[InstallDelete]
Type: files; Name: "{app}\quickeject.exe"; Check: IsLegacyExecutable

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:SettingsShortcut}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--settings"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

function IsLegacyExecutable: Boolean;
var
  Shell, Folder, Item: Variant;
begin
  Result := False;
  if not FileExists(ExpandConstant('{app}\quickeject.exe')) then exit;
  try
    Shell := CreateOleObject('Shell.Application');
    Folder := Shell.NameSpace(ExpandConstant('{app}'));
    Item := Folder.ParseName('quickeject.exe');
    Result := (CompareText(Item.ExtendedProperty('System.Software.ProductName'), 'QuickEject') = 0) and
              (CompareText(Item.ExtendedProperty('System.Company'), 'QuickEject Project') = 0) and
              (CompareText(Item.ExtendedProperty('System.OriginalFileName'), 'quickeject.exe') = 0);
  except
    Log('Could not identify legacy executable: ' + GetExceptionMessage);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if FileExists(ExpandConstant('{app}\quickeject.exe')) and not IsLegacyExecutable then
    Result := CustomMessage('LegacyNameConflict');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  OldCommand, NewCommand, ExistingCommand: String;
begin
  if CurStep <> ssPostInstall then exit;
  OldCommand := '"' + ExpandConstant('{app}\quickeject.exe') + '"';
  NewCommand := '"' + ExpandConstant('{app}\{#MyAppExeName}') + '"';
  if RegQueryStringValue(HKCU, RunKey, 'QuickEject', ExistingCommand) and
     (CompareText(ExistingCommand, OldCommand) = 0) then begin
    if not RegValueExists(HKCU, RunKey, 'USB-Switch') then begin
      if not RegWriteStringValue(HKCU, RunKey, 'USB-Switch', NewCommand) then begin
        Log('Failed to migrate legacy startup entry');
        exit;
      end;
    end;
    if RegQueryStringValue(HKCU, RunKey, 'USB-Switch', ExistingCommand) and
       (CompareText(ExistingCommand, NewCommand) = 0) then
      if not RegDeleteValue(HKCU, RunKey, 'QuickEject') then
        Log('Failed to remove legacy startup entry');
  end;
end;

procedure RemoveOwnedRunEntry(const ValueName, ExeName: String);
var
  Command, InstalledCommand: String;
begin
  InstalledCommand := '"' + ExpandConstant('{app}\' + ExeName) + '"';
  if RegQueryStringValue(HKCU, RunKey,
      ValueName, Command) and (CompareText(Command, InstalledCommand) = 0) then
    if not RegDeleteValue(HKCU, RunKey, ValueName) then
      Log('Failed to remove startup entry ' + ValueName);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    RemoveOwnedRunEntry('USB-Switch', '{#MyAppExeName}');
    if not FileExists(ExpandConstant('{app}\quickeject.exe')) or IsLegacyExecutable then
      RemoveOwnedRunEntry('QuickEject', 'quickeject.exe');
  end;
end;

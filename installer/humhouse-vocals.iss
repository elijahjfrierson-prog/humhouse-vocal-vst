; HumHouse Vocals — Inno Setup Installer Script
; Installs VST3 for Windows (all DAWs)
;
; Build with: iscc installer/humhouse-vocals.iss
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)

#define MyAppName      "HumHouse Vocals"
#define MyAppVersion   "1.5.2"
#define MyAppPublisher "HumHouse"
#define MyAppURL       "https://github.com/elijahjfrierson-prog/humhouse-vocal-vst"

[Setup]
AppId={{F3A7D1E2-8B4C-4F5A-9D6E-7C8B9A0E1F2D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autocf64}\VST3
DefaultGroupName={#MyAppName}
LicenseFile=eula.txt
OutputDir=..\build\installer
OutputBaseFilename=HumHouse-Vocals-v1.5.2-Installer
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousAppDir=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; VST3 plugin → system-wide VST3 folder (all DAWs: FL Studio, Ableton,
; Reaper, Cubase, Studio One, Bitwig, etc.)
Source: "..\build\HumHouseVocals_artefacts\Release\VST3\HumHouse Vocals.vst3\*"; \
    DestDir: "{autocf64}\VST3\HumHouse Vocals.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{autocf64}\VST3\HumHouse Vocals.vst3"

[Messages]
WelcomeLabel2=This will install {#MyAppName} v{#MyAppVersion} VST3 plugin.%n%nThe plugin will be placed in your VST3 folder so FL Studio, Ableton, and other DAWs can find it automatically.%n%nAfter installation: FL Studio → Options → Manage Plugins → Start Scan

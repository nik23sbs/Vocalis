; Installer Windows di Vocalis (Inno Setup 6)
; ISCC /DVersion=0.3.0 /DArt=<cartella artefatti Release> /DModels=<cartella modelli> installer\windows\Vocalis.iss
#ifndef Version
  #define Version "0.3.1"
#endif

[Setup]
AppId={{7C1B6E52-3F0A-4B8E-9C5D-56A1B2C3D4E5}
AppName=Vocalis
AppVersion={#Version}
AppPublisher=Vocalis
DefaultDirName={autopf}\Vocalis
DefaultGroupName=Vocalis
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=Vocalis-{#Version}-Windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Vocalis {#Version}

[Languages]
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[Messages]
it.WelcomeLabel2=Vocalis 3 trasforma un testo in un vocal neurale gia' a tempo: parlato, rap o cantato su note MIDI, con armonie, doppi, miscela di voci ed effetti.%n%nVerranno installati il plugin VST3, l'app e la voce neurale (circa 350 MB).

[Types]
Name: "full"; Description: "Installazione completa"
Name: "custom"; Description: "Personalizzata"; Flags: iscustom

[Components]
Name: "vst3";   Description: "Plugin VST3 (Ableton Live, FL Studio, Cubase, Reaper...)"; Types: full custom
Name: "app";    Description: "App standalone"; Types: full custom
Name: "models"; Description: "Voce neurale (modello Kokoro, ~350 MB)"; Types: full custom

[Files]
Source: "{#Art}\VST3\Vocalis.vst3\*"; DestDir: "{commoncf64}\VST3\Vocalis.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Art}\Standalone\*"; DestDir: "{app}"; Components: app; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Models}\kokoro-v1.0-ctrl.onnx"; DestDir: "{commonappdata}\Vocalis\models"; Components: models; Flags: ignoreversion
Source: "{#Models}\voices-v1.0.bin"; DestDir: "{commonappdata}\Vocalis\models"; Components: models; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Vocalis"; Filename: "{app}\Vocalis.exe"; Components: app

[Run]
Filename: "{app}\Vocalis.exe"; Description: "Apri Vocalis"; Flags: nowait postinstall skipifsilent; Components: app

; ==============================================================================
; Script de Inno Setup 6 para Melodeus DJ Workstation
; Diseñado y orquestado por danedsant con Antigravity y OpenCode
; Modelo de IA: Gemini 3.8 Flash High (Google DeepMind)
; ==============================================================================

#define MyAppName "Melodeus"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "danedsant"
#define MyAppExeName "Melodeus.exe"

[Setup]
; Identificador único de aplicación (GUID)
AppId={{D37E6B4A-6E4F-4C81-9C3E-A53FE63B15E2}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=license.txt
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; Directorio de salida y nombre del ejecutable instalador
OutputDir=Output
OutputBaseFilename=Melodeus_Setup_v1.1
SetupIconFile=..\app\res\melodeus.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

; Gráficos y presentación visual (Logo visible y grande de Melodeus / Colosseum)
WizardImageFile=banner.bmp
WizardSmallImageFile=small.bmp
WizardImageStretch=no
WizardStyle=modern

; Compresión ultra-eficiente LZMA2 (comprime ~150 MB a ~50 MB)
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Ejecutable principal compilado con icono oficial incrustado
Source: "..\build\app\Release\Melodeus.exe"; DestDir: "{app}"; DestName: "{#MyAppExeName}"; Flags: ignoreversion

; Shaders D3D12 compilados (necesarios para renderizado de waveform a 144Hz)
Source: "..\build\app\Release\waveform.vso"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\app\Release\waveform.pso"; DestDir: "{app}"; Flags: ignoreversion

; Tipografías oficiales e iconos de la interfaz (Inter, ClashDisplay, JetBrains Mono, Phosphor)
Source: "..\app\fonts\*"; DestDir: "{app}\fonts"; Flags: ignoreversion recursesubdirs createallsubdirs

; Herramientas de streaming (yt-dlp y ffmpeg para YouTube y SoundCloud)
; Se distribuyen en {app}\tools y en {app}\external para detección universal inmediata
Source: "..\external\yt-dlp\yt-dlp.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\external\ffmpeg\bin\ffmpeg.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\external\yt-dlp\yt-dlp.exe"; DestDir: "{app}\external\yt-dlp"; Flags: ignoreversion
Source: "..\external\ffmpeg\bin\ffmpeg.exe"; DestDir: "{app}\external\ffmpeg\bin"; Flags: ignoreversion

; ONNX Runtime DLL para análisis neural
Source: "..\external\onnxruntime\runtimes\win-x64\native\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Términos, condiciones, créditos de autor y licencias de terceros
Source: "license.txt"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

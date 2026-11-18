; Instalador do Kinect Webcam (Inno Setup 6). Gere com installer\build-installer.cmd.
; Resultado: dist\KinectWebcam-Setup-<versão>.exe

#define AppVersion "1.1.0"

[Setup]
AppId={{B3F1E2A4-6C7D-4E8F-9A0B-1C2D3E4F5A6B}
AppName=Kinect Webcam
AppVersion={#AppVersion}
AppVerName=Kinect Webcam {#AppVersion}
DefaultDirName={autopf}\KinectVCam
DefaultGroupName=Kinect Webcam
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=KinectWebcam-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Câmera virtual (MFCreateVirtualCamera) exige Windows 11 (build 22000+)
MinVersion=10.0.22000
; Não deixar o Restart Manager tentar fechar o serviço Frame Server
CloseApplications=no
UninstallDisplayIcon={app}\KinectBridge.exe
UninstallDisplayName=Kinect Webcam

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Iniciar o Kinect Webcam junto com o Windows (em segundo plano, sem ícone; o Kinect só liga quando um app usa a câmera)"

[Files]
Source: "..\bin\KinectVCam.dll"; DestDir: "{app}"; Flags: ignoreversion regserver restartreplace uninsrestartdelete
Source: "..\bin\KinectBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\Microsoft.Kinect.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\VCamTest.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Dirs]
Name: "{commonappdata}\KinectVCam"

[Icons]
Name: "{group}\Kinect Webcam (configurações)"; Filename: "{app}\KinectBridge.exe"; Parameters: "--tray"
Name: "{group}\Ativar câmera do Kinect"; Filename: "{app}\KinectBridge.exe"; Parameters: "--on"
Name: "{group}\Desativar câmera do Kinect"; Filename: "{app}\KinectBridge.exe"; Parameters: "--off"
Name: "{group}\Desinstalar Kinect Webcam"; Filename: "{uninstallexe}"

[Run]
; Pasta de log da DLL: o serviço Frame Server roda como LOCAL SERVICE (S-1-5-19)
Filename: "{sys}\icacls.exe"; Parameters: """{commonappdata}\KinectVCam"" /grant *S-1-5-19:(OI)(CI)M /grant *S-1-5-32-545:(OI)(CI)M"; Flags: runhidden
; Registra as câmeras como o usuário (não elevado) e grava a preferência "ligadas"
Filename: "{app}\KinectBridge.exe"; Parameters: "--on"; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\KinectBridge.exe"; Parameters: "--autostart on"; Tasks: autostart; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\KinectBridge.exe"; Parameters: "--background"; Description: "Iniciar o Kinect Webcam agora (em segundo plano, sem ícone)"; Flags: postinstall nowait runasoriginaluser runhidden

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im KinectBridge.exe"; Flags: runhidden; RunOnceId: "KillBridge"
Filename: "{app}\KinectBridge.exe"; Parameters: "--off"; Flags: runhidden waituntilterminated; RunOnceId: "CamerasOff"
Filename: "{app}\KinectBridge.exe"; Parameters: "--autostart off"; Flags: runhidden waituntilterminated; RunOnceId: "AutostartOff"
Filename: "{sys}\net.exe"; Parameters: "stop FrameServerMonitor"; Flags: runhidden; RunOnceId: "StopFSM"
Filename: "{sys}\net.exe"; Parameters: "stop FrameServer"; Flags: runhidden; RunOnceId: "StopFS"

[UninstallDelete]
Type: filesandordirs; Name: "{commonappdata}\KinectVCam"

[Code]
const
  RuntimeUrl = 'https://www.microsoft.com/download/details.aspx?id=44559';

function InitializeSetup(): Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;
  if not FileExists(ExpandConstant('{sys}\Kinect20.dll')) then
  begin
    if MsgBox('O "Kinect for Windows Runtime 2.0" não foi encontrado.' + #13#10#13#10 +
              'Ele é necessário para o Kinect funcionar. A instalação pode continuar, mas as câmeras ' +
              'vão mostrar "SEM SINAL" até você instalar o runtime.' + #13#10#13#10 +
              'Abrir a página oficial da Microsoft para baixar o runtime agora?',
              mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', RuntimeUrl, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  // Fecha o KinectBridge e para o Frame Server, que pode estar com a DLL antiga carregada.
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im KinectBridge.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServerMonitor', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\net.exe'), 'stop FrameServer', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;


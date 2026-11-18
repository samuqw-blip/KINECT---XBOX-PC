@echo off
rem Instala em "C:\Program Files\KinectVCam" e registra a DLL (pede administrador uma vez).
net session >nul 2>&1 || (
  powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%~f0'"
  goto :eof
)
setlocal
set "SRC=%~dp0..\bin"
set "DST=%ProgramFiles%\KinectVCam"
if not exist "%SRC%\KinectVCam.dll" ( echo Compile primeiro com build.cmd na raiz do projeto. & pause & exit /b 1 )

taskkill /im KinectBridge.exe /f >nul 2>&1
rem O Frame Server pode estar com a DLL antiga aberta.
net stop FrameServerMonitor >nul 2>&1
net stop FrameServer >nul 2>&1

if not exist "%DST%" mkdir "%DST%"
copy /y "%SRC%\KinectVCam.dll" "%DST%\" || goto :fail
copy /y "%SRC%\KinectBridge.exe" "%DST%\" || goto :fail
copy /y "%SRC%\Microsoft.Kinect.dll" "%DST%\" || goto :fail

rem Pasta de log da DLL (escrita pelo serviço Frame Server, conta LOCAL SERVICE).
if not exist "%ProgramData%\KinectVCam" mkdir "%ProgramData%\KinectVCam"
icacls "%ProgramData%\KinectVCam" /grant *S-1-5-19:(OI)(CI)M /grant *S-1-5-32-545:(OI)(CI)M >nul

regsvr32 /s "%DST%\KinectVCam.dll" || goto :fail
echo Instalado em %DST%
exit /b 0
:fail
echo FALHA na instalacao.
pause
exit /b 1

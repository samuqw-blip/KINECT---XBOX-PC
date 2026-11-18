@echo off
rem Remove as câmeras, desregistra a DLL e apaga "C:\Program Files\KinectVCam".
setlocal
set "DST=%ProgramFiles%\KinectVCam"
net session >nul 2>&1
if errorlevel 1 (
  rem Parte do usuário: remove as câmeras e o "iniciar com o Windows", depois pede administrador.
  if exist "%DST%\KinectBridge.exe" (
    taskkill /im KinectBridge.exe /f >nul 2>&1
    "%DST%\KinectBridge.exe" --off
    "%DST%\KinectBridge.exe" --autostart off
  )
  powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%~f0'"
  goto :eof
)
net stop FrameServerMonitor >nul 2>&1
net stop FrameServer >nul 2>&1
if exist "%DST%\KinectVCam.dll" regsvr32 /s /u "%DST%\KinectVCam.dll"
rmdir /s /q "%DST%"
echo Desinstalado.

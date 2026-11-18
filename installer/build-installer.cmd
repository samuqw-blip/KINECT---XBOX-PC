@echo off
rem Compila o projeto e gera o instalador em dist\ (requer Inno Setup 6: winget install JRSoftware.InnoSetup).
setlocal
cd /d "%~dp0.."
call "%CD%\build.cmd" || exit /b 1
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" ( echo Inno Setup 6 nao encontrado. Instale com: winget install JRSoftware.InnoSetup & exit /b 1 )
"%ISCC%" /Q installer\KinectWebcam.iss || exit /b 1
echo Instalador gerado em %CD%\dist

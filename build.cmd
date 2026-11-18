@echo off
rem Compila tudo para .\bin (Release x64). Requer VS 2022 Build Tools (C++ e .NET Framework 4.8).
setlocal
set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
)
cd /d "%~dp0"
if exist src\KinectVCam\KinectVCam.vcxproj (
  "%MSBUILD%" src\KinectVCam\KinectVCam.vcxproj /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 || exit /b 1
)
"%MSBUILD%" src\KinectBridge\KinectBridge.csproj /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 || exit /b 1
rem Ferramenta de teste (lista câmeras e lê quadros via Media Foundation)
set "VCVARS=%MSBUILD:\MSBuild\Current\Bin\MSBuild.exe=%\VC\Auxiliary\Build\vcvars64.bat"
if not exist obj mkdir obj
call "%VCVARS%" >nul && cl /nologo /O2 /EHsc /utf-8 /Foobj\ tools\VCamTest\VCamTest.cpp /Fe:bin\VCamTest.exe mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib >nul || exit /b 1
echo OK - binarios em %~dp0bin

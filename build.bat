@echo off
REM Builds ClusteredLightingLab with MSVC. No external dependencies.
setlocal

set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo Could not find vcvars64.bat at:
  echo   %VCVARS%
  echo Edit this script to point at your Visual Studio installation.
  exit /b 1
)

call "%VCVARS%" >nul || exit /b 1

REM The /Fo and /Fe paths are quoted and absolute: a bare "/Fobuild\" ends in a
REM backslash, which cmd treats as escaping the following space and writes the
REM .obj files into the project root instead.
if not exist build mkdir build
if not exist build\tests mkdir build\tests

cl /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc "/Fo%CD%\build\\" /Fe"%CD%\build\ClusteredLightingLab.exe" src\main.cpp src\gl\gl_loader.cpp src\scene\SoALights.cpp src\renderer\ClusteredRenderer.cpp src\renderer\ClusterMath.cpp /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup user32.lib gdi32.lib shell32.lib opengl32.lib

if errorlevel 1 exit /b 1

REM ---- correctness tests (no GPU needed) ----
cl /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc "/Fo%CD%\build\tests\\" /Fe"%CD%\build\ClusterMathTests.exe" src\tests\ClusterMathTests.cpp src\renderer\ClusterMath.cpp

if errorlevel 1 exit /b 1

echo.
echo Built build\ClusteredLightingLab.exe and build\ClusterMathTests.exe
echo Running correctness tests...
build\ClusterMathTests.exe
endlocal

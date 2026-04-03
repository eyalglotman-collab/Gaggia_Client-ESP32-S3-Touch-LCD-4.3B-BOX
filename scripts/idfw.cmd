@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"

set "IDF_PATH=C:\Espressif\.espressif\v5.5.2\esp-idf"
set "IDF_TOOLS_PATH=C:\Espressif"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\python_env\idf5.5_py3.11_env"
set "PYTHON_EXE=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe"
set "BUILD_DIR=%REPO_ROOT%\.idfbuild"
set "CACHE_DIR=%REPO_ROOT%\.cache"

if not exist "%PYTHON_EXE%" (
    echo ESP-IDF Python not found at "%PYTHON_EXE%"
    exit /b 1
)

if not exist "%IDF_PATH%\tools\idf.py" (
    echo ESP-IDF not found at "%IDF_PATH%"
    exit /b 1
)

if not exist "%CACHE_DIR%" mkdir "%CACHE_DIR%"
if not exist "%CACHE_DIR%\Espressif\ComponentManager" mkdir "%CACHE_DIR%\Espressif\ComponentManager"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "XDG_CACHE_HOME=%CACHE_DIR%"
set "PYTHONNOUSERSITE=True"
set "PYTHONPATH="
set "PYTHONHOME="

set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;C:\Program Files\Git\cmd;C:\Program Files\Git\mingw64\bin;C:\Program Files\Git\usr\bin;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin;C:\Espressif;%PATH%"

set "HAS_BUILD=0"
set "HAS_FLASH=0"
set "NORMALIZED_ARGS="

:NormalizeArgs
if "%~1"=="" goto NormalizeDone
set "ARG=%~1"
if /I "!ARG!"=="flash-only" set "ARG=flash"
if /I "!ARG!"=="build" set "HAS_BUILD=1"
if /I "!ARG!"=="flash" set "HAS_FLASH=1"
if defined NORMALIZED_ARGS (
    set "NORMALIZED_ARGS=!NORMALIZED_ARGS! !ARG!"
) else (
    set "NORMALIZED_ARGS=!ARG!"
)
shift
goto NormalizeArgs

:NormalizeDone
if not defined NORMALIZED_ARGS set "NORMALIZED_ARGS=build"
if "!HAS_FLASH!"=="1" if "!HAS_BUILD!"=="0" (
    echo Flash-only mode requested ^(no build step^).
)

echo Releasing COM port before idf action...
call :ReleaseCom

"%PYTHON_EXE%" "%IDF_PATH%\tools\idf.py" -B "%BUILD_DIR%" -DIDF_TARGET=esp32s3 !NORMALIZED_ARGS!
set "CMD_EXIT=%ERRORLEVEL%"

echo Releasing COM port after idf action...
call :ReleaseCom

exit /b %CMD_EXIT%

:ReleaseCom
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "try { Invoke-WebRequest -Uri 'http://localhost:8000/api/transport/release-com' -Method Post -TimeoutSec 3 -UseBasicParsing -ErrorAction SilentlyContinue | Out-Null } catch {}; Start-Sleep -Milliseconds 500" >nul 2>nul
exit /b 0

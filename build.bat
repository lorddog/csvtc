:: CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
::
:: Copyright (c) 2026 LordDog
:: Open source — feel free to use, modify, and distribute.
:: Credits are appreciated but not required.
::
:: Aufruf:  build.bat [Release|Debug]

@echo off
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Config %CONFIG%
exit /b %ERRORLEVEL%

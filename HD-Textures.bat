@echo off
rem Builds HD textures from YOUR copy of the game, on your PC.
rem Step 1: play - every texture the game shows is saved once.
rem Step 2: the textures are upscaled into Game\textures (loaded at the next start).
rem Nothing is uploaded; the results are derived from your game files and stay local.
setlocal
cd /d "%~dp0"
if not exist "Game\Splatoon.exe" (echo Run Setup.bat first. & pause & exit /b 1)
tasklist /FI "IMAGENAME eq Splatoon.exe" 2>nul | find /I "Splatoon.exe" >nul && (echo Close Splatoon first. & pause & exit /b 1)
where python >nul 2>nul || (echo Python 3 is needed: https://www.python.org/downloads/ & pause & exit /b 1)
set "DUMP=%~dp0Game\texture-dump"
if not exist "%DUMP%" mkdir "%DUMP%"
echo.
echo Step 1 of 2: the game starts. Visit the places you want in HD
echo (plaza, Octo Valley, levels), then close the game.
echo.
set "RBREW_TEXTURE_DUMP=%DUMP%"
start "" /wait "Game\Splatoon.exe"
set "RBREW_TEXTURE_DUMP="
echo.
echo Step 2 of 2: upscaling (a few minutes with an NVIDIA card, much longer on the CPU).
echo You can stop with Ctrl+C and run this file again later; finished textures are kept.
echo.
python "scripts\hd_textures.py" "%DUMP%" "Game\textures"
echo.
echo Finished. The HD textures load the next time you start the game.
pause

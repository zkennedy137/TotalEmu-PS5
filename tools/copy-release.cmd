@echo off
rem PS Launcher - copies the current build and its changelog to the archive folder, then unzips the build.
rem Run on Windows with the Y: drive mapped to the build machine's pslauncher-shimfix folder.

set "SRC=Y:\pslauncher-shimfix"
set "DEST=C:\Users\Zachary J Kennedy\Downloads"
set "ARCHIVE=%DEST%\Archived PSLauncher Builds"

rem The version is the newest "## [vX.Y.Z]" heading in CHANGELOG.md (the first match).
for /f "tokens=2 delims=[]" %%V in ('findstr /B /C:"## [v" "%SRC%\CHANGELOG.md"') do (
    if not defined VERSION set "VERSION=%%V"
)
if not defined VERSION (
    echo No version found in "%SRC%\CHANGELOG.md"
    pause
    exit /b 1
)

if not exist "%SRC%\dist\PPSA97500.zip" (
    echo The build was not found: "%SRC%\dist\PPSA97500.zip"
    echo Check that the Y: drive is mapped and the build has been made.
    pause
    exit /b 1
)
if not exist "%SRC%\CHANGELOG.md" (
    echo The changelog was not found: "%SRC%\CHANGELOG.md"
    pause
    exit /b 1
)

if not exist "%ARCHIVE%" mkdir "%ARCHIVE%"
if errorlevel 1 (
    echo Could not create "%ARCHIVE%"
    pause
    exit /b 1
)

copy /Y "%SRC%\dist\PPSA97500.zip" "%ARCHIVE%\PSLauncher_%VERSION%.zip"
if errorlevel 1 (
    echo The build could not be copied.
    pause
    exit /b 1
)

copy /Y "%SRC%\CHANGELOG.md" "%ARCHIVE%\CHANGELOG.md"
if errorlevel 1 (
    echo The changelog could not be copied.
    pause
    exit /b 1
)

rem Unzip the build into Downloads, with WinRAR. The zip stays in the archive.
set "WINRAR="
if exist "%ProgramFiles%\WinRAR\WinRAR.exe" set "WINRAR=%ProgramFiles%\WinRAR\WinRAR.exe"
if exist "%ProgramFiles(x86)%\WinRAR\WinRAR.exe" set "WINRAR=%ProgramFiles(x86)%\WinRAR\WinRAR.exe"
if not defined WINRAR (
    echo WinRAR was not found. The zip was copied to "%ARCHIVE%\PSLauncher_%VERSION%.zip" but not unzipped.
    pause
    exit /b 1
)
"%WINRAR%" x -y "%ARCHIVE%\PSLauncher_%VERSION%.zip" "%DEST%\PSLauncher_%VERSION%\"
if errorlevel 1 (
    echo WinRAR could not unzip "%ARCHIVE%\PSLauncher_%VERSION%.zip"
    pause
    exit /b 1
)

echo Version %VERSION%
echo Unzipped to "%DEST%\PSLauncher_%VERSION%"
echo Copied the build to "%ARCHIVE%\PSLauncher_%VERSION%.zip"
echo Copied the changelog to "%ARCHIVE%\CHANGELOG.md"
pause

@echo off
rem Build nexplorer.exe with MSYS2 UCRT64 g++ (static, no runtime DLLs).
rem Double-click it, or run it from a terminal. Set NOPAUSE=1 to skip the final "press any key".
setlocal
set PATH=C:\msys64\ucrt64\bin;%PATH%
set FIND=%SystemRoot%\System32\find.exe
cd /d "%~dp0"
set RC=0

where g++ >nul 2>nul
if errorlevel 1 (
  echo [!] g++ was not found. Install MSYS2 and its UCRT64 toolchain:
  echo     pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib
  echo     ^(expected in C:\msys64\ucrt64\bin^)
  set RC=1
  goto :end
)

"%SystemRoot%\System32\tasklist.exe" /fi "imagename eq nexplorer.exe" 2>nul | "%FIND%" /i "nexplorer.exe" >nul
if not errorlevel 1 (
  echo [!] nexplorer.exe is running. Close it first: Windows does not allow replacing a running program.
  set RC=1
  goto :end
)

if not exist build mkdir build
echo [1/2] Resources ^(icon, manifest^)...
windres res\app.rc -O coff -o build\app.res.o
if errorlevel 1 (set RC=1& goto :end)

echo [2/2] Compiling nExplorer - this takes about a minute, please wait...
g++ -std=c++17 -O2 -s -municode -mwindows -static -Wall -Wno-missing-field-initializers ^
  src\main.cpp src\pane.cpp src\addrbar.cpp src\sidebar.cpp src\drag.cpp src\fs.cpp src\columns.cpp src\clip.cpp src\fileops.cpp src\profiles.cpp src\drives.cpp src\dialogs.cpp src\theme.cpp src\toolbar.cpp src\search.cpp src\favs.cpp src\backdrop.cpp src\scrollbar.cpp ^
  build\app.res.o -o nexplorer.exe ^
  -lcomctl32 -lshlwapi -lshell32 -lole32 -luuid -luxtheme -lmpr -lsetupapi -lcfgmgr32 -ldwmapi -lwindowscodecs -lmsimg32 -lcomdlg32 -lz
if errorlevel 1 (set RC=1& goto :end)

echo.
echo Built nexplorer.exe
for %%F in (nexplorer.exe) do echo     %%~fF  (%%~zF bytes)

:end
if not "%RC%"=="0" (
  echo.
  echo Build FAILED.
)
rem keep the window open when started by double-click (cmd /c "...build.bat")
if not defined NOPAUSE echo %cmdcmdline% | "%FIND%" /i "%~nx0" >nul && (echo. & pause)
exit /b %RC%
